// Unity native EXR loader. Pixel loads map the EXR once, decode through
// TinyEXR's worker pool, and return malloc'd floats that FreeImageData
// releases with free(). The resize, options, and layer paths share that
// decode and log each stage to stderr.
#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
// ZIP/PIZ chunks are independent. TinyEXR's worker pool decodes them in parallel.
#define TINYEXR_USE_THREAD 1
#include "../../tinyexr.h"
#include <thread>
#include "unity_tinyexr_plugin.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <set>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <chrono>
#ifdef _WIN32
#include <psapi.h>
#endif

// Helper: Duplicate string
static char* DuplicateString(const char* str) {
    if (!str) return nullptr;
    size_t len = strlen(str);
    char* copy = new char[len + 1];
    strcpy(copy, str);
    return copy;
}

// Helper: Flip image vertically
static void FlipVertical(float* data, int width, int height, int channels) {
    int rowSize = width * channels;
    float* tempRow = new float[rowSize];
    
    for (int y = 0; y < height / 2; y++) {
        int topOffset = y * rowSize;
        int bottomOffset = (height - 1 - y) * rowSize;
        
        memcpy(tempRow, data + topOffset, rowSize * sizeof(float));
        memcpy(data + topOffset, data + bottomOffset, rowSize * sizeof(float));
        memcpy(data + bottomOffset, tempRow, rowSize * sizeof(float));
    }
    
    delete[] tempRow;
}

// Helper: Apply gamma correction
static void ApplyGamma(float* data, int width, int height, int channels, float gamma) {
    if (gamma == 1.0f) return;
    
    int totalPixels = width * height * channels;
    for (int i = 0; i < totalPixels; i++) {
        data[i] = powf(std::max(0.0f, data[i]), gamma);
    }
}

// Process memory at a stage boundary. Private bytes are the commit charge.
struct ProcessMemorySample {
    unsigned long long privateBytes;
    unsigned long long workingSetBytes;
    unsigned long long peakWorkingSetBytes;
};

// Read the current process commit and working set.
static ProcessMemorySample SampleProcessMemory() {
    ProcessMemorySample sample;
    sample.privateBytes = 0;
    sample.workingSetBytes = 0;
    sample.peakWorkingSetBytes = 0;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters;
    memset(&counters, 0, sizeof(counters));
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
            sizeof(counters))) {
        sample.privateBytes = static_cast<unsigned long long>(counters.PrivateUsage);
        sample.workingSetBytes = static_cast<unsigned long long>(counters.WorkingSetSize);
        sample.peakWorkingSetBytes = static_cast<unsigned long long>(counters.PeakWorkingSetSize);
    }
#elif defined(__linux__)
    FILE* status = fopen("/proc/self/status", "r");
    if (status) {
        char line[256];
        while (fgets(line, sizeof(line), status)) {
            unsigned long long kib = 0;
            if (sscanf(line, "VmRSS: %llu", &kib) == 1) {
                sample.workingSetBytes = kib * 1024ull;
            } else if (sscanf(line, "VmHWM: %llu", &kib) == 1) {
                sample.peakWorkingSetBytes = kib * 1024ull;
            } else if (sscanf(line, "VmSize: %llu", &kib) == 1) {
                sample.privateBytes = kib * 1024ull;
            }
        }
        fclose(status);
    }
#endif
    return sample;
}

// High-resolution tick for stage timings.
static long long StageNow() {
#ifdef _WIN32
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#else
    return static_cast<long long>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}

// Convert a StageNow() delta to milliseconds.
static double StageElapsedMs(long long startTicks) {
#ifdef _WIN32
    static LARGE_INTEGER frequency = {};
    if (frequency.QuadPart == 0) {
        QueryPerformanceFrequency(&frequency);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart - startTicks) * 1000.0 /
        static_cast<double>(frequency.QuadPart);
#else
    long long now = StageNow();
    return static_cast<double>(now - startTicks) / 1000000.0;
#endif
}

// Log one native stage. Memory figures are sampled at the end of the stage.
static void LogStage(const char* stage, double milliseconds) {
    ProcessMemorySample memory = SampleProcessMemory();
    fprintf(stderr,
        "[TinyEXR] stage=%s ms=%.3f private_bytes=%llu working_set=%llu peak_working_set=%llu\n",
        stage,
        milliseconds,
        memory.privateBytes,
        memory.workingSetBytes,
        memory.peakWorkingSetBytes);
    fflush(stderr);
}

// Touch each mapped page so file-read time is not mixed into decompression.
static void PrefetchMappedFile(const unsigned char* data, size_t size) {
    if (!data || size == 0) {
        return;
    }

    size_t pageSize = 4096;
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    if (info.dwPageSize > 0) {
        pageSize = static_cast<size_t>(info.dwPageSize);
    }
#endif

    // Keep the read live so the compiler cannot delete the scan.
    volatile unsigned char sink = 0;
    for (size_t offset = 0; offset < size; offset += pageSize) {
        sink = data[offset];
    }
    sink = data[size - 1];
    (void)sink;
}

// Byte count of an RGBA float image, or 0 when the product would overflow.
static size_t RgbaFloatBytes(int width, int height) {
    if (width <= 0 || height <= 0) {
        return 0;
    }
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    if (w > (std::numeric_limits<size_t>::max() / 4) / sizeof(float) / h) {
        return 0;
    }
    return w * h * 4 * sizeof(float);
}

// Fill one output row range with the original scalar bilinear kernel.
static void ResizeBilinearRows(
    const float* srcData,
    int srcWidth,
    int srcHeight,
    float* dstData,
    int targetWidth,
    int targetHeight,
    int channels,
    int yBegin,
    int yEnd
) {
    // Ratios match the previous full-image loop so weights stay bit-identical.
    const float xRatio = static_cast<float>(srcWidth) / targetWidth;
    const float yRatio = static_cast<float>(srcHeight) / targetHeight;

    for (int y = yBegin; y < yEnd; y++) {
        for (int x = 0; x < targetWidth; x++) {
            float srcX = x * xRatio;
            float srcY = y * yRatio;

            int x0 = static_cast<int>(srcX);
            int y0 = static_cast<int>(srcY);
            int x1 = std::min(x0 + 1, srcWidth - 1);
            int y1 = std::min(y0 + 1, srcHeight - 1);

            float xWeight = srcX - x0;
            float yWeight = srcY - y0;

            for (int c = 0; c < channels; c++) {
                float p00 = srcData[(y0 * srcWidth + x0) * channels + c];
                float p10 = srcData[(y0 * srcWidth + x1) * channels + c];
                float p01 = srcData[(y1 * srcWidth + x0) * channels + c];
                float p11 = srcData[(y1 * srcWidth + x1) * channels + c];

                float top = p00 * (1.0f - xWeight) + p10 * xWeight;
                float bottom = p01 * (1.0f - xWeight) + p11 * xWeight;

                dstData[(y * targetWidth + x) * channels + c] =
                    top * (1.0f - yWeight) + bottom * yWeight;
            }
        }
    }
}

// Resize the whole image on the calling thread into a caller-owned buffer.
static void ResizeBilinearInto(
    const float* srcData,
    int srcWidth,
    int srcHeight,
    float* dstData,
    int targetWidth,
    int targetHeight,
    int channels
) {
    ResizeBilinearRows(
        srcData, srcWidth, srcHeight,
        dstData, targetWidth, targetHeight,
        channels, 0, targetHeight);
}

// Helper: Extract layer names from channels
static void ExtractLayers(const EXRHeader& header, std::vector<std::string>& layerNames) {
    std::set<std::string> uniqueLayers;
    
    for (int i = 0; i < header.num_channels; i++) {
        std::string channelName(header.channels[i].name);
        size_t dotPos = channelName.find('.');
        
        if (dotPos != std::string::npos) {
            std::string layerName = channelName.substr(0, dotPos);
            uniqueLayers.insert(layerName);
        }
    }
    
    layerNames.assign(uniqueLayers.begin(), uniqueLayers.end());
}

// Helper: Initialize compatibility info to defaults
static void InitCompatibilityInfo(UnityExrCompatibilityInfo* info) {
    memset(info, 0, sizeof(UnityExrCompatibilityInfo));
    info->reason = UNITY_EXR_SUPPORT_OK;
}

// Helper: Fill dimensions from EXR header
static void FillDimensionsFromHeader(const EXRHeader& header, UnityExrCompatibilityInfo* info) {
    info->width = header.data_window.max_x - header.data_window.min_x + 1;
    info->height = header.data_window.max_y - header.data_window.min_y + 1;
    info->numChannels = header.num_channels;
    info->compressionType = header.compression_type;
    info->isTiled = header.tiled ? 1 : 0;
}

// Helper: Check if header has spectral layout attribute
static bool HasSpectralLayout(const EXRHeader& header) {
    for (int i = 0; i < header.num_custom_attributes; i++) {
        if (strcmp(header.custom_attributes[i].name, "spectralLayoutVersion") == 0) {
            return true;
        }
    }
    return false;
}

// Helper: Check if channels in a layer use OpenEXR luminance/chroma (Y/RY/BY)
static bool HasYcaLayout(const EXRHeader& header, const char* layerName) {
    bool hasY = false;
    bool hasRY = false;
    bool hasBY = false;
    const std::string layerPrefix =
        (layerName != nullptr && layerName[0] != '\0')
            ? std::string(layerName) + "."
            : std::string();

    for (int i = 0; i < header.num_channels; i++) {
        std::string channelName(header.channels[i].name);
        std::string shortName = channelName;

        if (!layerPrefix.empty()) {
            if (channelName.find(layerPrefix) != 0) {
                continue;
            }
            shortName = channelName.substr(layerPrefix.size());
        } else {
            const size_t dotPos = channelName.find('.');
            if (dotPos != std::string::npos && dotPos != 0) {
                continue;
            }
            if (dotPos == 0 && dotPos + 1 < channelName.size()) {
                shortName = channelName.substr(dotPos + 1);
            }
        }

        if (shortName == "Y") hasY = true;
        if (shortName == "RY") hasRY = true;
        if (shortName == "BY") hasBY = true;
    }

    return hasY && hasRY && hasBY;
}

// Helper: Check if compression type is supported by this build
static bool IsCompressionSupported(int compressionType) {
    if (compressionType == TINYEXR_COMPRESSIONTYPE_DWAA ||
        compressionType == TINYEXR_COMPRESSIONTYPE_DWAB) {
        return false;
    }
#if !TINYEXR_USE_ZFP
    if (compressionType == TINYEXR_COMPRESSIONTYPE_ZFP) {
        return false;
    }
#endif
    return true;
}

// Helper: Free multipart header array allocated by ParseEXRMultipartHeaderFromFile
static void FreeMultipartHeaders(EXRHeader** headers, int numHeaders) {
    if (!headers) return;
    for (int i = 0; i < numHeaders; i++) {
        if (headers[i]) {
            FreeEXRHeader(headers[i]);
            free(headers[i]);
        }
    }
    free(headers);
}

// Helper: Check version flags that block LoadEXR()
static bool IsMultipartOrDeep(const EXRVersion& version) {
    return version.multipart != 0 || version.non_image != 0;
}

// Get EXR info without loading pixel data
UNITY_TINYEXR_API int UNITY_CALL GetEXRInfo(
    const char* filename,
    UnityImageInfo* outInfo,
    char** outErrorMsg
) {
    if (!filename || !outInfo) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromFile(&exrVersion, filename);
    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid EXR file");
        }
        return ret;
    }

    // Reject multipart/deep before parsing header (matches LoadEXR() gate)
    if (IsMultipartOrDeep(exrVersion)) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(
                "Loading multipart or DeepImage is not supported  in LoadEXR() API");
        }
        return TINYEXR_ERROR_INVALID_DATA;
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);
    const char* err = nullptr;

    ret = ParseEXRHeaderFromFile(&exrHeader, &exrVersion, filename, &err);
    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Failed to parse header");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    outInfo->width = exrHeader.data_window.max_x - exrHeader.data_window.min_x + 1;
    outInfo->height = exrHeader.data_window.max_y - exrHeader.data_window.min_y + 1;
    outInfo->numChannels = exrHeader.num_channels;
    outInfo->pixelType = exrHeader.pixel_types[0];
    outInfo->hasAlpha = 0;

    // Check for alpha channel
    for (int i = 0; i < exrHeader.num_channels; i++) {
        if (strcmp(exrHeader.channels[i].name, "A") == 0 ||
            strstr(exrHeader.channels[i].name, ".A") != nullptr) {
            outInfo->hasAlpha = 1;
            break;
        }
    }

    FreeEXRHeader(&exrHeader);
    if (outErrorMsg) *outErrorMsg = nullptr;
    return TINYEXR_SUCCESS;
}

// Get load compatibility without decoding pixel data
UNITY_TINYEXR_API int UNITY_CALL GetEXRCompatibilityForUnity(
    const char* filename,
    const char* layerName,
    UnityExrCompatibilityInfo* outInfo,
    char** outErrorMsg
) {
    if (!filename || !outInfo) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    InitCompatibilityInfo(outInfo);

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromFile(&exrVersion, filename);
    if (ret != TINYEXR_SUCCESS) {
        outInfo->isSupported = 0;
        outInfo->reason = UNITY_EXR_SUPPORT_NOT_EXR;
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid EXR file");
        }
        return ret;
    }

    outInfo->isMultipart = exrVersion.multipart ? 1 : 0;
    outInfo->isDeepImage = exrVersion.non_image ? 1 : 0;
    outInfo->isTiled = exrVersion.tiled ? 1 : 0;

    // Multipart files are not supported by LoadEXR()
    if (exrVersion.multipart) {
        const char* err = nullptr;
        EXRHeader** mpHeaders = nullptr;
        int numParts = 0;
        ret = ParseEXRMultipartHeaderFromFile(&mpHeaders, &numParts, &exrVersion, filename, &err);
        if (ret == TINYEXR_SUCCESS && numParts > 0 && mpHeaders && mpHeaders[0]) {
            FillDimensionsFromHeader(*mpHeaders[0], outInfo);
        }
        outInfo->partCount = numParts;
        if (mpHeaders) {
            FreeMultipartHeaders(mpHeaders, numParts);
        }
        if (err) FreeEXRErrorMessage(err);

        outInfo->isSupported = 0;
        outInfo->reason = UNITY_EXR_SUPPORT_MULTIPART;
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(
                "Loading multipart or DeepImage is not supported  in LoadEXR() API");
        }
        return TINYEXR_ERROR_INVALID_DATA;
    }

    // Deep image files are not supported by LoadEXR()
    if (exrVersion.non_image) {
        EXRHeader exrHeader;
        InitEXRHeader(&exrHeader);
        const char* err = nullptr;
        ret = ParseEXRHeaderFromFile(&exrHeader, &exrVersion, filename, &err);
        if (ret == TINYEXR_SUCCESS) {
            FillDimensionsFromHeader(exrHeader, outInfo);
            FreeEXRHeader(&exrHeader);
        }
        if (err) FreeEXRErrorMessage(err);

        outInfo->isSupported = 0;
        outInfo->reason = UNITY_EXR_SUPPORT_DEEP_IMAGE;
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(
                "Loading multipart or DeepImage is not supported  in LoadEXR() API");
        }
        return TINYEXR_ERROR_INVALID_DATA;
    }

    // Single-part regular image: parse header for full compatibility checks
    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);
    const char* err = nullptr;

    ret = ParseEXRHeaderFromFile(&exrHeader, &exrVersion, filename, &err);
    if (ret != TINYEXR_SUCCESS) {
        outInfo->isSupported = 0;
        outInfo->reason = UNITY_EXR_SUPPORT_HEADER_ERROR;
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Failed to parse header");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    FillDimensionsFromHeader(exrHeader, outInfo);
    outInfo->isTiled = exrHeader.tiled ? 1 : 0;

    // Spectral EXR is not supported by the Unity loader path
    if (HasSpectralLayout(exrHeader)) {
        outInfo->isSpectral = 1;
        outInfo->isSupported = 0;
        outInfo->reason = UNITY_EXR_SUPPORT_SPECTRAL;
        FreeEXRHeader(&exrHeader);
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Spectral EXR is not supported");
        }
        return TINYEXR_ERROR_UNSUPPORTED_FORMAT;
    }

    // Unsupported compression in this build
    if (!IsCompressionSupported(exrHeader.compression_type)) {
        outInfo->isSupported = 0;
        outInfo->reason = UNITY_EXR_SUPPORT_COMPRESSION;
        FreeEXRHeader(&exrHeader);
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Unsupported EXR compression type");
        }
        return TINYEXR_ERROR_UNSUPPORTED_FORMAT;
    }

    // Layered channels are supported via LoadEXRLayerForUnity()
    std::vector<std::string> layers;
    ExtractLayers(exrHeader, layers);
    outInfo->hasLayers = layers.empty() ? 0 : 1;
    outInfo->layerCount = static_cast<int>(layers.size());

    if (!layers.empty() && layerName != nullptr && layerName[0] != '\0') {
        // When a layer name is provided, verify it exists before reporting supported.
        bool found = false;
        for (size_t i = 0; i < layers.size(); i++) {
            if (layers[i] == layerName) {
                found = true;
                break;
            }
        }
        if (!found) {
            outInfo->isSupported = 0;
            outInfo->reason = UNITY_EXR_SUPPORT_LAYER_NOT_FOUND;
            FreeEXRHeader(&exrHeader);
            if (outErrorMsg) {
                *outErrorMsg = DuplicateString("Layer Not Found");
            }
            return TINYEXR_ERROR_LAYER_NOT_FOUND;
        }
    }

    outInfo->isYca = HasYcaLayout(exrHeader, layerName) ? 1 : 0;

    FreeEXRHeader(&exrHeader);
    outInfo->isSupported = 1;
    outInfo->reason = UNITY_EXR_SUPPORT_OK;
    if (outErrorMsg) *outErrorMsg = nullptr;
    return TINYEXR_SUCCESS;
}

// Copy a TinyEXR error into a Unity-owned string. Defined with the decode helpers.
static void PublishError(const char* fallback, const char* tinyErr, char** outErrorMsg);

// Single-map decode plus interleave. Defined with the other decode helpers.
static int LoadMappedInterleavedRgba(
    const char* filename,
    const char* layerName,
    float** outRgba,
    int* outWidth,
    int* outHeight,
    double* outInterleaveAllocMs,
    char** outErrorMsg);

// Load EXR with default options
UNITY_TINYEXR_API int UNITY_CALL LoadEXRForUnity(
    const char* filename,
    float** outData,
    UnityImageInfo* outInfo,
    char** outErrorMsg
) {
    UnityConversionOptions defaultOptions;
    defaultOptions.flipY = 1;
    defaultOptions.targetChannels = 4;
    defaultOptions.gammaCorrection = 1.0f;
    
    return LoadEXRWithOptionsForUnity(filename, outData, outInfo, &defaultOptions, outErrorMsg);
}

// Load EXR with options
UNITY_TINYEXR_API int UNITY_CALL LoadEXRWithOptionsForUnity(
    const char* filename,
    float** outData,
    UnityImageInfo* outInfo,
    UnityConversionOptions* options,
    char** outErrorMsg
) {
    if (!filename || !outData || !outInfo || !options) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    // A failed call must not leave a previous pointer for the caller to free.
    *outData = nullptr;
    if (outErrorMsg) {
        *outErrorMsg = nullptr;
    }

    const long long totalStart = StageNow();
    float* rgba = nullptr;
    int width = 0;
    int height = 0;

    // Null layer keeps LoadEXR's default filter. The buffer is malloc'd.
    int ret = LoadMappedInterleavedRgba(
        filename, nullptr, &rgba, &width, &height, nullptr, outErrorMsg);
    if (ret != TINYEXR_SUCCESS) {
        return ret;
    }

    // Gamma and flip still see four channels, matching the previous order.
    long long stageStart = StageNow();
    if (options->gammaCorrection != 1.0f) {
        ApplyGamma(rgba, width, height, 4, options->gammaCorrection);
    }
    LogStage("gamma", StageElapsedMs(stageStart));

    stageStart = StageNow();
    if (options->flipY) {
        FlipVertical(rgba, width, height, 4);
    }
    LogStage("flip", StageElapsedMs(stageStart));

    // Optional RGB strip. malloc, not new[], so FreeImageData can free() it.
    if (options->targetChannels == 3) {
        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        if (pixelCount > (std::numeric_limits<size_t>::max() / 3) / sizeof(float)) {
            free(rgba);
            PublishError("Image dimensions are too large to allocate", nullptr, outErrorMsg);
            return TINYEXR_ERROR_DATA_TOO_LARGE;
        }
        stageStart = StageNow();
        float* rgb = static_cast<float*>(malloc(pixelCount * 3 * sizeof(float)));
        LogStage("alloc_channels", StageElapsedMs(stageStart));
        if (!rgb) {
            free(rgba);
            PublishError("Failed to allocate RGB buffer", nullptr, outErrorMsg);
            return TINYEXR_ERROR_INVALID_DATA;
        }
        stageStart = StageNow();
        for (size_t i = 0; i < pixelCount; i++) {
            rgb[i * 3 + 0] = rgba[i * 4 + 0];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        LogStage("channel_convert", StageElapsedMs(stageStart));
        free(rgba);
        rgba = rgb;
        outInfo->numChannels = 3;
        outInfo->hasAlpha = 0;
    } else {
        LogStage("alloc_channels", 0.0);
        LogStage("channel_convert", 0.0);
        outInfo->numChannels = 4;
        outInfo->hasAlpha = 1;
    }

    outInfo->width = width;
    outInfo->height = height;
    outInfo->pixelType = 2; // FLOAT
    *outData = rgba;
    LogStage("total", StageElapsedMs(totalStart));
    return TINYEXR_SUCCESS;
}

// Load specific layer
UNITY_TINYEXR_API int UNITY_CALL LoadEXRLayerForUnity(
    const char* filename,
    const char* layerName,
    float** outData,
    UnityImageInfo* outInfo,
    char** outErrorMsg
) {
    if (!filename || !layerName || !outData || !outInfo) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    *outData = nullptr;
    if (outErrorMsg) {
        *outErrorMsg = nullptr;
    }

    float* rgba = nullptr;
    int width = 0;
    int height = 0;

    // Named layer uses the same map and interleave as the default load.
    int ret = LoadMappedInterleavedRgba(
        filename, layerName, &rgba, &width, &height, nullptr, outErrorMsg);
    if (ret != TINYEXR_SUCCESS) {
        return ret;
    }

    // Previous success path always flipped and returned four float channels.
    long long stageStart = StageNow();
    FlipVertical(rgba, width, height, 4);
    LogStage("flip", StageElapsedMs(stageStart));

    outInfo->width = width;
    outInfo->height = height;
    outInfo->numChannels = 4;
    outInfo->pixelType = 2;
    outInfo->hasAlpha = 1;
    *outData = rgba;
    return TINYEXR_SUCCESS;
}

// Get layer count
UNITY_TINYEXR_API int UNITY_CALL GetEXRLayerCount(
    const char* filename,
    int* outLayerCount,
    char** outErrorMsg
) {
    if (!filename || !outLayerCount) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromFile(&exrVersion, filename);
    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid EXR file");
        }
        return ret;
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);
    const char* err = nullptr;

    ret = ParseEXRHeaderFromFile(&exrHeader, &exrVersion, filename, &err);
    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Failed to parse header");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    std::vector<std::string> layers;
    ExtractLayers(exrHeader, layers);
    *outLayerCount = static_cast<int>(layers.size());

    FreeEXRHeader(&exrHeader);
    if (outErrorMsg) *outErrorMsg = nullptr;
    return TINYEXR_SUCCESS;
}

// Get layer info
UNITY_TINYEXR_API int UNITY_CALL GetEXRLayerInfo(
    const char* filename,
    UnityLayerInfo** outLayers,
    int* outLayerCount,
    char** outErrorMsg
) {
    if (!filename || !outLayers || !outLayerCount) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    EXRVersion exrVersion;
    int ret = ParseEXRVersionFromFile(&exrVersion, filename);
    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid EXR file");
        }
        return ret;
    }

    EXRHeader exrHeader;
    InitEXRHeader(&exrHeader);
    const char* err = nullptr;

    ret = ParseEXRHeaderFromFile(&exrHeader, &exrVersion, filename, &err);
    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Failed to parse header");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    std::vector<std::string> layers;
    ExtractLayers(exrHeader, layers);

    *outLayerCount = static_cast<int>(layers.size());
    *outLayers = new UnityLayerInfo[*outLayerCount];

    for (int i = 0; i < *outLayerCount; i++) {
        strncpy((*outLayers)[i].name, layers[i].c_str(), 255);
        (*outLayers)[i].name[255] = '\0';
        
        // Count channels for this layer
        int channelCount = 0;
        for (int j = 0; j < exrHeader.num_channels; j++) {
            std::string channelName(exrHeader.channels[j].name);
            if (channelName.find(layers[i] + ".") == 0) {
                channelCount++;
            }
        }
        (*outLayers)[i].channelCount = channelCount;
    }

    FreeEXRHeader(&exrHeader);
    if (outErrorMsg) *outErrorMsg = nullptr;
    return TINYEXR_SUCCESS;
}

// Copy decoded planes into malloc'd RGBA. Matches LoadEXR() channel order.
// layerName null or empty selects LoadEXR's default layer filter ("").
static int InterleaveDecodedToRgba(
    const EXRHeader& header,
    const EXRImage& image,
    const char* layerName,
    float** outRgba,
    int* outWidth,
    int* outHeight,
    double* outAllocMs,
    const char** err
) {
    *outRgba = nullptr;
    *outAllocMs = 0.0;

    int idxR = -1;
    int idxG = -1;
    int idxB = -1;
    int idxA = -1;
    int idxY = -1;
    int idxRY = -1;
    int idxBY = -1;

    // Empty and null both mean "channels with no layer prefix", same as LoadEXR().
    const bool defaultLayer = (layerName == nullptr || layerName[0] == '\0');
    const std::string layer = defaultLayer ? std::string() : std::string(layerName);

    std::vector<tinyexr::LayerChannel> channels;
    tinyexr::ChannelsInLayer(header, layer, channels);
    if (channels.size() < 1) {
        if (defaultLayer) {
            tinyexr::SetErrorMessage(
                "Layer Not Found. Seems EXR contains channels with layer(e.g. `diffuse.R`). if you are using LoadEXR(), please try LoadEXRWithLayer(). LoadEXR() cannot load EXR having channels with layer.",
                err);
        } else {
            tinyexr::SetErrorMessage("Layer Not Found", err);
        }
        return TINYEXR_ERROR_LAYER_NOT_FOUND;
    }

    for (size_t c = 0; c < channels.size(); c++) {
        const tinyexr::LayerChannel& ch = channels[c];
        if (ch.name == "R") idxR = int(ch.index);
        else if (ch.name == "G") idxG = int(ch.index);
        else if (ch.name == "B") idxB = int(ch.index);
        else if (ch.name == "A") idxA = int(ch.index);
        else if (ch.name == "Y") idxY = int(ch.index);
        else if (ch.name == "RY") idxRY = int(ch.index);
        else if (ch.name == "BY") idxBY = int(ch.index);
    }

    // YCA conversion allocates its own RGBA buffer.
    if (tinyexr::IsLuminanceChromaLayout(channels)) {
        if (!tinyexr::ConvertYCAToRGBA(header, image, idxY, idxRY, idxBY, idxA, outRgba, err)) {
            return TINYEXR_ERROR_INVALID_DATA;
        }
        *outWidth = image.width;
        *outHeight = image.height;
        return TINYEXR_SUCCESS;
    }

    const size_t rgbaBytes = RgbaFloatBytes(image.width, image.height);
    if (rgbaBytes == 0) {
        tinyexr::SetErrorMessage("Image dimensions are too large to allocate", err);
        return TINYEXR_ERROR_DATA_TOO_LARGE;
    }

    long long allocStart = StageNow();
    float* rgba = static_cast<float*>(malloc(rgbaBytes));
    *outAllocMs = StageElapsedMs(allocStart);
    if (!rgba) {
        tinyexr::SetErrorMessage("Failed to allocate interleaved RGBA buffer", err);
        return TINYEXR_ERROR_INVALID_DATA;
    }

    if (channels.size() == 1) {
        int chIdx = int(channels.front().index);
        if (header.tiled) {
            const size_t tileSizeX = static_cast<size_t>(header.tile_size_x);
            const size_t tileSizeY = static_cast<size_t>(header.tile_size_y);
            for (int it = 0; it < image.num_tiles; it++) {
                for (size_t j = 0; j < tileSizeY; j++) {
                    for (size_t i = 0; i < tileSizeX; i++) {
                        const size_t ii = static_cast<size_t>(image.tiles[it].offset_x) * tileSizeX + i;
                        const size_t jj = static_cast<size_t>(image.tiles[it].offset_y) * tileSizeY + j;
                        if (ii >= static_cast<size_t>(image.width) || jj >= static_cast<size_t>(image.height)) {
                            continue;
                        }
                        const size_t idx = ii + jj * static_cast<size_t>(image.width);
                        const size_t srcIdx = i + j * tileSizeX;
                        unsigned char** src = image.tiles[it].images;
                        const float val = reinterpret_cast<float**>(src)[chIdx][srcIdx];
                        rgba[4 * idx + 0] = val;
                        rgba[4 * idx + 1] = val;
                        rgba[4 * idx + 2] = val;
                        rgba[4 * idx + 3] = val;
                    }
                }
            }
        } else {
            const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
            for (size_t i = 0; i < pixelCount; i++) {
                const float val = reinterpret_cast<float**>(image.images)[chIdx][i];
                rgba[4 * i + 0] = val;
                rgba[4 * i + 1] = val;
                rgba[4 * i + 2] = val;
                rgba[4 * i + 3] = val;
            }
        }
    } else {
        if (idxR == -1 || idxG == -1 || idxB == -1) {
            free(rgba);
            const char* missing = "R channel not found";
            if (idxR != -1 && idxG == -1) missing = "G channel not found";
            if (idxR != -1 && idxG != -1 && idxB == -1) missing = "B channel not found";
            tinyexr::SetErrorMessage(missing, err);
            return TINYEXR_ERROR_INVALID_DATA;
        }

        if (header.tiled) {
            const size_t tileSizeX = static_cast<size_t>(header.tile_size_x);
            const size_t tileSizeY = static_cast<size_t>(header.tile_size_y);
            for (int it = 0; it < image.num_tiles; it++) {
                for (size_t j = 0; j < tileSizeY; j++) {
                    for (size_t i = 0; i < tileSizeX; i++) {
                        const size_t ii = static_cast<size_t>(image.tiles[it].offset_x) * tileSizeX + i;
                        const size_t jj = static_cast<size_t>(image.tiles[it].offset_y) * tileSizeY + j;
                        if (ii >= static_cast<size_t>(image.width) || jj >= static_cast<size_t>(image.height)) {
                            continue;
                        }
                        const size_t idx = ii + jj * static_cast<size_t>(image.width);
                        const size_t srcIdx = i + j * tileSizeX;
                        unsigned char** src = image.tiles[it].images;
                        rgba[4 * idx + 0] = reinterpret_cast<float**>(src)[idxR][srcIdx];
                        rgba[4 * idx + 1] = reinterpret_cast<float**>(src)[idxG][srcIdx];
                        rgba[4 * idx + 2] = reinterpret_cast<float**>(src)[idxB][srcIdx];
                        rgba[4 * idx + 3] = (idxA != -1)
                            ? reinterpret_cast<float**>(src)[idxA][srcIdx]
                            : 1.0f;
                    }
                }
            }
        } else {
            const size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
            float** planes = reinterpret_cast<float**>(image.images);
            float* rowR = planes[idxR];
            float* rowG = planes[idxG];
            float* rowB = planes[idxB];
            float* rowA = (idxA != -1) ? planes[idxA] : nullptr;
            for (size_t i = 0; i < pixelCount; i++) {
                rgba[4 * i + 0] = rowR[i];
                rgba[4 * i + 1] = rowG[i];
                rgba[4 * i + 2] = rowB[i];
                rgba[4 * i + 3] = rowA ? rowA[i] : 1.0f;
            }
        }
    }

    *outRgba = rgba;
    *outWidth = image.width;
    *outHeight = image.height;
    return TINYEXR_SUCCESS;
}

// Copy a TinyEXR error into a Unity-owned string and release the source.
static void PublishError(const char* fallback, const char* tinyErr, char** outErrorMsg) {
    if (outErrorMsg) {
        *outErrorMsg = DuplicateString((tinyErr && tinyErr[0]) ? tinyErr : fallback);
    }
    if (tinyErr) {
        FreeEXRErrorMessage(tinyErr);
    }
}

// Map the file once, decode to planar images, then interleave to malloc'd RGBA.
// The map ends before interleave so its pages are not held with the RGBA buffer.
// layerName null or empty uses LoadEXR's default layer. On failure every native
// buffer is released and outErrorMsg is published the same way as the resize path.
static int LoadMappedInterleavedRgba(
    const char* filename,
    const char* layerName,
    float** outRgba,
    int* outWidth,
    int* outHeight,
    double* outInterleaveAllocMs,
    char** outErrorMsg
) {
    *outRgba = nullptr;
    *outWidth = 0;
    *outHeight = 0;
    if (outInterleaveAllocMs) {
        *outInterleaveAllocMs = 0.0;
    }

    const char* err = nullptr;
    EXRHeader exrHeader;
    EXRImage exrImage;
    InitEXRHeader(&exrHeader);
    InitEXRImage(&exrImage);

    // Map once. Header parse and pixel decode share this view of the file.
    {
        long long stageStart = StageNow();
        MemoryMappedFile mapped(filename);
        LogStage("file_map", StageElapsedMs(stageStart));
        if (!mapped.valid() || mapped.data == nullptr || mapped.size < 16) {
            PublishError("Cannot read file", nullptr, outErrorMsg);
            FreeEXRHeader(&exrHeader);
            return TINYEXR_ERROR_CANT_OPEN_FILE;
        }

        stageStart = StageNow();
        PrefetchMappedFile(mapped.data, mapped.size);
        LogStage("file_read", StageElapsedMs(stageStart));

        stageStart = StageNow();
        EXRVersion exrVersion;
        int ret = ParseEXRVersionFromMemory(&exrVersion, mapped.data, mapped.size);
        if (ret != TINYEXR_SUCCESS) {
            LogStage("header_parse", StageElapsedMs(stageStart));
            PublishError("Failed to open EXR file or read version info from EXR file.", nullptr, outErrorMsg);
            FreeEXRHeader(&exrHeader);
            return ret;
        }
        if (exrVersion.multipart || exrVersion.non_image) {
            LogStage("header_parse", StageElapsedMs(stageStart));
            PublishError("Loading multipart or DeepImage is not supported  in LoadEXR() API", nullptr, outErrorMsg);
            FreeEXRHeader(&exrHeader);
            return TINYEXR_ERROR_INVALID_DATA;
        }

        ret = ParseEXRHeaderFromMemory(&exrHeader, &exrVersion, mapped.data, mapped.size, &err);
        LogStage("header_parse", StageElapsedMs(stageStart));
        if (ret != TINYEXR_SUCCESS) {
            PublishError("Failed to parse header", err, outErrorMsg);
            FreeEXRHeader(&exrHeader);
            return ret;
        }

        // Read HALF channels as FLOAT, same as LoadEXR().
        for (int i = 0; i < exrHeader.num_channels; i++) {
            if (exrHeader.pixel_types[i] == TINYEXR_PIXELTYPE_HALF) {
                exrHeader.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
            }
        }

        stageStart = StageNow();
        fprintf(stderr, "[TinyEXR] decode_threads=%u\n",
            std::thread::hardware_concurrency());
        ret = LoadEXRImageFromMemory(&exrImage, &exrHeader, mapped.data, mapped.size, &err);
        LogStage("decompress", StageElapsedMs(stageStart));
        if (ret != TINYEXR_SUCCESS) {
            PublishError("Unknown error loading EXR", err, outErrorMsg);
            FreeEXRHeader(&exrHeader);
            FreeEXRImage(&exrImage);
            return ret;
        }
    }

    // Interleave after the map is gone. YCA, grayscale, and tiled order match LoadEXR().
    double interleaveAllocMs = 0.0;
    long long stageStart = StageNow();
    int ret = InterleaveDecodedToRgba(
        exrHeader, exrImage, layerName, outRgba, outWidth, outHeight, &interleaveAllocMs, &err);
    const double interleaveMs = StageElapsedMs(stageStart) - interleaveAllocMs;
    if (outInterleaveAllocMs) {
        *outInterleaveAllocMs = interleaveAllocMs;
    }
    LogStage("alloc_interleaved", interleaveAllocMs);
    LogStage("interleave", interleaveMs < 0.0 ? 0.0 : interleaveMs);
    FreeEXRHeader(&exrHeader);
    FreeEXRImage(&exrImage);
    if (ret != TINYEXR_SUCCESS) {
        // Interleave frees a partial RGBA buffer before it returns an error.
        *outRgba = nullptr;
        PublishError("Unknown error loading EXR", err, outErrorMsg);
        return ret;
    }
    return TINYEXR_SUCCESS;
}

// Load EXR with resize
// public API
UNITY_TINYEXR_API int UNITY_CALL LoadEXRWithResizeForUnity(
    const char* filename,
    int targetWidth,
    int targetHeight,
    float** outData,
    UnityImageInfo* outInfo,
    UnityConversionOptions* options,
    char** outErrorMsg
) {
    if (!filename || !outData || !outInfo || !options) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid parameters");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    // Clear outputs so a failed call never leaves a dangling pointer.
    *outData = nullptr;
    if (outErrorMsg) {
        *outErrorMsg = nullptr;
    }

    if (targetWidth <= 0 || targetHeight <= 0) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid target dimensions");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    const long long totalStart = StageNow();
    double allocMs = 0.0;
    float* rgba = nullptr;
    float* resizedData = nullptr;
    int width = 0;
    int height = 0;

    // Shared single-map decode. Null layer is LoadEXR's default filter.
    double interleaveAllocMs = 0.0;
    int ret = LoadMappedInterleavedRgba(
        filename, nullptr, &rgba, &width, &height, &interleaveAllocMs, outErrorMsg);
    if (ret != TINYEXR_SUCCESS) {
        return ret;
    }
    allocMs += interleaveAllocMs;

    resizedData = rgba;
    long long stageStart = 0;
    if (width != targetWidth || height != targetHeight) {
        const size_t dstBytes = RgbaFloatBytes(targetWidth, targetHeight);
        stageStart = StageNow();
        resizedData = (dstBytes == 0) ? nullptr : static_cast<float*>(malloc(dstBytes));
        const double resizeAllocMs = StageElapsedMs(stageStart);
        allocMs += resizeAllocMs;
        LogStage("alloc_resize", resizeAllocMs);
        if (!resizedData) {
            free(rgba);
            PublishError("Failed to allocate resized image", nullptr, outErrorMsg);
            return TINYEXR_ERROR_INVALID_DATA;
        }

        stageStart = StageNow();
        ResizeBilinearInto(rgba, width, height, resizedData, targetWidth, targetHeight, 4);
        LogStage("resize", StageElapsedMs(stageStart));
        free(rgba);
        rgba = nullptr;
    } else {
        LogStage("alloc_resize", 0.0);
        LogStage("resize", 0.0);
    }

    stageStart = StageNow();
    if (options->gammaCorrection != 1.0f) {
        ApplyGamma(resizedData, targetWidth, targetHeight, 4, options->gammaCorrection);
    }
    LogStage("gamma", StageElapsedMs(stageStart));

    stageStart = StageNow();
    if (options->flipY) {
        FlipVertical(resizedData, targetWidth, targetHeight, 4);
    }
    LogStage("flip", StageElapsedMs(stageStart));

    // Strip alpha after color conversion so gamma and flip still see RGBA.
    if (options->targetChannels == 3) {
        const size_t pixelCount = static_cast<size_t>(targetWidth) * static_cast<size_t>(targetHeight);
        if (pixelCount > (std::numeric_limits<size_t>::max() / 3) / sizeof(float)) {
            free(resizedData);
            PublishError("Image dimensions are too large to allocate", nullptr, outErrorMsg);
            return TINYEXR_ERROR_DATA_TOO_LARGE;
        }
        stageStart = StageNow();
        float* rgb = static_cast<float*>(malloc(pixelCount * 3 * sizeof(float)));
        const double channelAllocMs = StageElapsedMs(stageStart);
        allocMs += channelAllocMs;
        LogStage("alloc_channels", channelAllocMs);
        if (!rgb) {
            free(resizedData);
            PublishError("Failed to allocate RGB buffer", nullptr, outErrorMsg);
            return TINYEXR_ERROR_INVALID_DATA;
        }
        stageStart = StageNow();
        for (size_t i = 0; i < pixelCount; i++) {
            rgb[i * 3 + 0] = resizedData[i * 4 + 0];
            rgb[i * 3 + 1] = resizedData[i * 4 + 1];
            rgb[i * 3 + 2] = resizedData[i * 4 + 2];
        }
        LogStage("channel_convert", StageElapsedMs(stageStart));
        free(resizedData);
        resizedData = rgb;
        outInfo->numChannels = 3;
        outInfo->hasAlpha = 0;
    } else {
        LogStage("alloc_channels", 0.0);
        LogStage("channel_convert", 0.0);
        outInfo->numChannels = 4;
        outInfo->hasAlpha = 1;
    }

    outInfo->width = targetWidth;
    outInfo->height = targetHeight;
    outInfo->pixelType = 2; // FLOAT
    *outData = resizedData;

    LogStage("alloc", allocMs);
    LogStage("total", StageElapsedMs(totalStart));
    return TINYEXR_SUCCESS;
}

// Memory cleanup functions
UNITY_TINYEXR_API void UNITY_CALL FreeImageData(float* data) {
    if (data) {
        // Image buffers from every Unity load path are malloc'd.
        free(data);
    }
}

UNITY_TINYEXR_API void UNITY_CALL FreeLayerInfo(UnityLayerInfo* layers) {
    if (layers) {
        delete[] layers;
    }
}

UNITY_TINYEXR_API void UNITY_CALL FreeErrorMessage(char* errorMsg) {
    if (errorMsg) {
        delete[] errorMsg;
    }
}
