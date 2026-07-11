#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 1
#include "../../tinyexr.h"
#include "unity_tinyexr_plugin.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <set>

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

// Helper: Bilinear resize
static float* ResizeBilinear(const float* srcData, int srcWidth, int srcHeight, int targetWidth, int targetHeight, int channels) {
    float* dstData = new float[targetWidth * targetHeight * channels];
    
    float xRatio = static_cast<float>(srcWidth) / targetWidth;
    float yRatio = static_cast<float>(srcHeight) / targetHeight;
    
    for (int y = 0; y < targetHeight; y++) {
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
                
                dstData[(y * targetWidth + x) * channels + c] = top * (1.0f - yWeight) + bottom * yWeight;
            }
        }
    }
    
    return dstData;
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

    // Layered channels require LoadEXRWithLayer()
    std::vector<std::string> layers;
    ExtractLayers(exrHeader, layers);
    outInfo->hasLayers = layers.empty() ? 0 : 1;
    outInfo->layerCount = static_cast<int>(layers.size());

    if (!layers.empty()) {
        if (layerName == nullptr || layerName[0] == '\0') {
            outInfo->isSupported = 0;
            outInfo->reason = UNITY_EXR_SUPPORT_REQUIRES_LAYER;
            FreeEXRHeader(&exrHeader);
            if (outErrorMsg) {
                *outErrorMsg = DuplicateString(
                    "EXR contains layered channels; use LoadLayer() with a layer name");
            }
            return TINYEXR_ERROR_LAYER_NOT_FOUND;
        }

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

    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* err = nullptr;

    int ret = LoadEXR(&rgba, &width, &height, filename, &err);

    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Unknown error loading EXR");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    // Apply gamma correction
    if (options->gammaCorrection != 1.0f) {
        ApplyGamma(rgba, width, height, 4, options->gammaCorrection);
    }

    // Flip vertically for Unity
    if (options->flipY) {
        FlipVertical(rgba, width, height, 4);
    }

    // Handle channel conversion
    if (options->targetChannels == 3) {
        float* rgb = new float[width * height * 3];
        for (int i = 0; i < width * height; i++) {
            rgb[i * 3 + 0] = rgba[i * 4 + 0];
            rgb[i * 3 + 1] = rgba[i * 4 + 1];
            rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        free(rgba);
        rgba = rgb;
        outInfo->numChannels = 3;
        outInfo->hasAlpha = 0;
    } else {
        outInfo->numChannels = 4;
        outInfo->hasAlpha = 1;
    }

    outInfo->width = width;
    outInfo->height = height;
    outInfo->pixelType = 2; // FLOAT

    *outData = rgba;
    if (outErrorMsg) *outErrorMsg = nullptr;
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

    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* err = nullptr;

    int ret = LoadEXRWithLayer(&rgba, &width, &height, filename, layerName, &err);

    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Failed to load layer");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    // Flip for Unity
    FlipVertical(rgba, width, height, 4);

    outInfo->width = width;
    outInfo->height = height;
    outInfo->numChannels = 4;
    outInfo->pixelType = 2;
    outInfo->hasAlpha = 1;

    *outData = rgba;
    if (outErrorMsg) *outErrorMsg = nullptr;
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

// Load EXR with resize
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

    if (targetWidth <= 0 || targetHeight <= 0) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString("Invalid target dimensions");
        }
        return TINYEXR_ERROR_INVALID_ARGUMENT;
    }

    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* err = nullptr;

    int ret = LoadEXR(&rgba, &width, &height, filename, &err);

    if (ret != TINYEXR_SUCCESS) {
        if (outErrorMsg) {
            *outErrorMsg = DuplicateString(err ? err : "Unknown error loading EXR");
        }
        if (err) FreeEXRErrorMessage(err);
        return ret;
    }

    // Resize if needed
    float* resizedData = rgba;
    if (width != targetWidth || height != targetHeight) {
        resizedData = ResizeBilinear(rgba, width, height, targetWidth, targetHeight, 4);
        free(rgba);
        rgba = nullptr;
    }

    // Apply gamma correction
    if (options->gammaCorrection != 1.0f) {
        ApplyGamma(resizedData, targetWidth, targetHeight, 4, options->gammaCorrection);
    }

    // Flip vertically for Unity
    if (options->flipY) {
        FlipVertical(resizedData, targetWidth, targetHeight, 4);
    }

    // Handle channel conversion
    if (options->targetChannels == 3) {
        float* rgb = new float[targetWidth * targetHeight * 3];
        for (int i = 0; i < targetWidth * targetHeight; i++) {
            rgb[i * 3 + 0] = resizedData[i * 4 + 0];
            rgb[i * 3 + 1] = resizedData[i * 4 + 1];
            rgb[i * 3 + 2] = resizedData[i * 4 + 2];
        }
        if (rgba != nullptr) {
            free(resizedData);
        } else {
            delete[] resizedData;
        }
        resizedData = rgb;
        outInfo->numChannels = 3;
        outInfo->hasAlpha = 0;
    } else {
        outInfo->numChannels = 4;
        outInfo->hasAlpha = 1;
    }

    outInfo->width = targetWidth;
    outInfo->height = targetHeight;
    outInfo->pixelType = 2; // FLOAT

    *outData = resizedData;
    if (outErrorMsg) *outErrorMsg = nullptr;
    return TINYEXR_SUCCESS;
}

// Memory cleanup functions
UNITY_TINYEXR_API void UNITY_CALL FreeImageData(float* data) {
    if (data) {
        // LoadEXR uses malloc, others use new[]
        // For safety, we'll use free for data from LoadEXR
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
