// Standalone benchmark for LoadEXRWithResizeForUnity.
// Decodes an EXR and resizes it without Unity, and can memcmp the result
// against LoadEXR() plus the original scalar bilinear resize.
// --no-resize calls LoadEXRWithOptionsForUnity and compares it to LoadEXR
// plus the same gamma, flip, and channel options.

#include "unity_tinyexr_plugin.h"
#include "../../tinyexr.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <thread>
#include <chrono>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace {

struct BenchOptions {
    const char* filename;
    int width;
    int height;
    int warmup;
    int runs;
    int validate;
    int noResize;
    int flipY;
    int channels;
    float gamma;
};

struct ProcessMemorySample {
    unsigned long long privateBytes;
    unsigned long long workingSetBytes;
    unsigned long long peakWorkingSetBytes;
};

// Read commit charge and working set for the bench process.
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
#endif
    return sample;
}

static double NowMs() {
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Original scalar bilinear resize, kept here so plugin changes stay comparable.
static float* ResizeBilinearReference(
    const float* srcData,
    int srcWidth,
    int srcHeight,
    int targetWidth,
    int targetHeight,
    int channels
) {
    const size_t count = static_cast<size_t>(targetWidth) *
        static_cast<size_t>(targetHeight) * static_cast<size_t>(channels);
    float* dstData = static_cast<float*>(malloc(count * sizeof(float)));
    if (!dstData) {
        return nullptr;
    }

    const float xRatio = static_cast<float>(srcWidth) / targetWidth;
    const float yRatio = static_cast<float>(srcHeight) / targetHeight;
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
                dstData[(y * targetWidth + x) * channels + c] =
                    top * (1.0f - yWeight) + bottom * yWeight;
            }
        }
    }
    return dstData;
}

static void ApplyGammaReference(float* data, int width, int height, int channels, float gamma) {
    if (gamma == 1.0f) {
        return;
    }
    const int total = width * height * channels;
    for (int i = 0; i < total; i++) {
        data[i] = powf(std::max(0.0f, data[i]), gamma);
    }
}

static void FlipVerticalReference(float* data, int width, int height, int channels) {
    const int rowSize = width * channels;
    float* tempRow = static_cast<float*>(malloc(static_cast<size_t>(rowSize) * sizeof(float)));
    if (!tempRow) {
        return;
    }
    for (int y = 0; y < height / 2; y++) {
        int topOffset = y * rowSize;
        int bottomOffset = (height - 1 - y) * rowSize;
        memcpy(tempRow, data + topOffset, static_cast<size_t>(rowSize) * sizeof(float));
        memcpy(data + topOffset, data + bottomOffset, static_cast<size_t>(rowSize) * sizeof(float));
        memcpy(data + bottomOffset, tempRow, static_cast<size_t>(rowSize) * sizeof(float));
    }
    free(tempRow);
}

// LoadEXR plus the original post-process, used only for pixel comparison.
// --no-resize keeps the file dimensions and skips bilinear resize.
static int LoadReference(
    const BenchOptions& options,
    float** outData,
    int* outWidth,
    int* outHeight,
    int* outChannels
) {
    float* rgba = nullptr;
    int width = 0;
    int height = 0;
    const char* err = nullptr;
    int ret = LoadEXR(&rgba, &width, &height, options.filename, &err);
    if (ret != TINYEXR_SUCCESS) {
        fprintf(stderr, "[bench] reference LoadEXR failed: %s\n", err ? err : "unknown");
        if (err) {
            FreeEXRErrorMessage(err);
        }
        return ret;
    }

    int imageWidth = width;
    int imageHeight = height;
    float* image = rgba;
    if (!options.noResize && (width != options.width || height != options.height)) {
        image = ResizeBilinearReference(rgba, width, height, options.width, options.height, 4);
        free(rgba);
        if (!image) {
            fprintf(stderr, "[bench] reference resize allocation failed\n");
            return TINYEXR_ERROR_INVALID_DATA;
        }
        imageWidth = options.width;
        imageHeight = options.height;
    }

    ApplyGammaReference(image, imageWidth, imageHeight, 4, options.gamma);
    if (options.flipY) {
        FlipVerticalReference(image, imageWidth, imageHeight, 4);
    }

    if (options.channels == 3) {
        const size_t pixelCount = static_cast<size_t>(imageWidth) * static_cast<size_t>(imageHeight);
        float* rgb = static_cast<float*>(malloc(pixelCount * 3 * sizeof(float)));
        if (!rgb) {
            free(image);
            return TINYEXR_ERROR_INVALID_DATA;
        }
        for (size_t i = 0; i < pixelCount; i++) {
            rgb[i * 3 + 0] = image[i * 4 + 0];
            rgb[i * 3 + 1] = image[i * 4 + 1];
            rgb[i * 3 + 2] = image[i * 4 + 2];
        }
        free(image);
        image = rgb;
        *outChannels = 3;
    } else {
        *outChannels = 4;
    }

    *outWidth = imageWidth;
    *outHeight = imageHeight;
    *outData = image;
    return TINYEXR_SUCCESS;
}

static int LoadUnderTest(
    const BenchOptions& options,
    float** outData,
    UnityImageInfo* outInfo,
    double* outWallMs
) {
    UnityConversionOptions conversion;
    conversion.flipY = options.flipY;
    conversion.targetChannels = options.channels;
    conversion.gammaCorrection = options.gamma;

    char* errorMsg = nullptr;
    const double start = NowMs();
    int ret = 0;
    if (options.noResize) {
        // Full-resolution options path, compared with LoadEXR under --validate.
        ret = LoadEXRWithOptionsForUnity(
            options.filename,
            outData,
            outInfo,
            &conversion,
            &errorMsg);
    } else {
        ret = LoadEXRWithResizeForUnity(
            options.filename,
            options.width,
            options.height,
            outData,
            outInfo,
            &conversion,
            &errorMsg);
    }
    *outWallMs = NowMs() - start;
    if (ret != TINYEXR_SUCCESS) {
        fprintf(stderr, "[bench] %s failed: %s\n",
            options.noResize ? "LoadEXRWithOptionsForUnity" : "LoadEXRWithResizeForUnity",
            errorMsg ? errorMsg : "unknown");
    }
    if (errorMsg) {
        FreeErrorMessage(errorMsg);
    }
    return ret;
}

static int CompareImages(
    const float* reference,
    const float* actual,
    size_t count
) {
    if (memcmp(reference, actual, count * sizeof(float)) == 0) {
        fprintf(stderr, "[bench] validate=exact pixels=%llu\n",
            static_cast<unsigned long long>(count));
        return 0;
    }

    size_t mismatches = 0;
    size_t first = 0;
    float maxAbs = 0.0f;
    for (size_t i = 0; i < count; i++) {
        const float delta = fabsf(reference[i] - actual[i]);
        if (reference[i] != actual[i]) {
            if (mismatches == 0) {
                first = i;
            }
            mismatches++;
            if (delta > maxAbs) {
                maxAbs = delta;
            }
        }
    }
    fprintf(stderr,
        "[bench] validate=mismatch pixels=%llu first=%llu ref=%g actual=%g max_abs=%g\n",
        static_cast<unsigned long long>(mismatches),
        static_cast<unsigned long long>(first),
        reference[first],
        actual[first],
        maxAbs);
    return 1;
}

static void PrintUsage() {
    fprintf(stderr,
        "Usage: exr_resize_bench [file] [--width N] [--height N] [--runs N] [--warmup N]\n"
        "         [--validate] [--no-resize] [--flip 0|1] [--channels 3|4] [--gamma F]\n"
        "Default file: EXR_Assets/shanghai_riverside_17k.exr\n"
        "Default size: 16384x8192, flipY=1, channels=4, gamma=1\n"
        "--no-resize loads through LoadEXRWithOptionsForUnity at the file's size.\n");
}

static int ParseArgs(int argc, char** argv, BenchOptions* options) {
    options->filename = "EXR_Assets/shanghai_riverside_17k.exr";
    options->width = 16384;
    options->height = 8192;
    options->warmup = 1;
    options->runs = 1;
    options->validate = 0;
    options->noResize = 0;
    options->flipY = 1;
    options->channels = 4;
    options->gamma = 1.0f;

    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        if (strcmp(arg, "--help") == 0) {
            PrintUsage();
            return 1;
        }
        if (strcmp(arg, "--validate") == 0) {
            options->validate = 1;
            continue;
        }
        if (strcmp(arg, "--no-resize") == 0) {
            options->noResize = 1;
            continue;
        }
        if (i + 1 >= argc) {
            fprintf(stderr, "[bench] missing value for %s\n", arg);
            return 2;
        }
        if (strcmp(arg, "--width") == 0) {
            options->width = atoi(argv[++i]);
        } else if (strcmp(arg, "--height") == 0) {
            options->height = atoi(argv[++i]);
        } else if (strcmp(arg, "--runs") == 0) {
            options->runs = atoi(argv[++i]);
        } else if (strcmp(arg, "--warmup") == 0) {
            options->warmup = atoi(argv[++i]);
        } else if (strcmp(arg, "--flip") == 0) {
            options->flipY = atoi(argv[++i]);
        } else if (strcmp(arg, "--channels") == 0) {
            options->channels = atoi(argv[++i]);
        } else if (strcmp(arg, "--gamma") == 0) {
            options->gamma = static_cast<float>(atof(argv[++i]));
        } else if (arg[0] != '-') {
            options->filename = arg;
        } else {
            fprintf(stderr, "[bench] unknown argument %s\n", arg);
            return 2;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    BenchOptions options;
    const int parseResult = ParseArgs(argc, argv, &options);
    if (parseResult != 0) {
        return parseResult == 1 ? 0 : parseResult;
    }

    fprintf(stderr, "[bench] file=%s target=%dx%d warmup=%d runs=%d validate=%d no_resize=%d flip=%d channels=%d gamma=%g\n",
        options.filename, options.width, options.height, options.warmup, options.runs,
        options.validate, options.noResize, options.flipY, options.channels, options.gamma);
    fprintf(stderr, "[bench] hardware_concurrency=%u\n",
        std::thread::hardware_concurrency());

    for (int i = 0; i < options.warmup; i++) {
        float* data = nullptr;
        UnityImageInfo info;
        double wallMs = 0.0;
        fprintf(stderr, "[bench] warmup %d begin\n", i + 1);
        int ret = LoadUnderTest(options, &data, &info, &wallMs);
        if (ret != 0) {
            return 1;
        }
        fprintf(stderr, "[bench] warmup %d wall_ms=%.3f size=%dx%d channels=%d\n",
            i + 1, wallMs, info.width, info.height, info.numChannels);
        FreeImageData(data);
    }

    for (int i = 0; i < options.runs; i++) {
        float* data = nullptr;
        UnityImageInfo info;
        double wallMs = 0.0;
        ProcessMemorySample before = SampleProcessMemory();
        fprintf(stderr, "[bench] run %d begin private_bytes=%llu working_set=%llu\n",
            i + 1, before.privateBytes, before.workingSetBytes);
        int ret = LoadUnderTest(options, &data, &info, &wallMs);
        ProcessMemorySample after = SampleProcessMemory();
        if (ret != 0) {
            return 1;
        }
        fprintf(stderr,
            "[bench] run %d wall_ms=%.3f size=%dx%d channels=%d private_bytes=%llu working_set=%llu peak_working_set=%llu\n",
            i + 1, wallMs, info.width, info.height, info.numChannels,
            after.privateBytes, after.workingSetBytes, after.peakWorkingSetBytes);
        FreeImageData(data);
    }

    if (options.validate) {
        float* reference = nullptr;
        int referenceWidth = 0;
        int referenceHeight = 0;
        int referenceChannels = 0;
        fprintf(stderr, "[bench] reference begin\n");
        const double referenceStart = NowMs();
        int ret = LoadReference(
            options, &reference, &referenceWidth, &referenceHeight, &referenceChannels);
        const double referenceMs = NowMs() - referenceStart;
        if (ret != 0) {
            return 1;
        }
        fprintf(stderr, "[bench] reference wall_ms=%.3f size=%dx%d channels=%d\n",
            referenceMs, referenceWidth, referenceHeight, referenceChannels);

        float* actual = nullptr;
        UnityImageInfo info;
        double wallMs = 0.0;
        ret = LoadUnderTest(options, &actual, &info, &wallMs);
        if (ret != 0) {
            free(reference);
            return 1;
        }
        if (info.width != referenceWidth || info.height != referenceHeight ||
            info.numChannels != referenceChannels) {
            fprintf(stderr, "[bench] validate=size mismatch actual=%dx%d ch=%d ref=%dx%d ch=%d\n",
                info.width, info.height, info.numChannels,
                referenceWidth, referenceHeight, referenceChannels);
            free(reference);
            FreeImageData(actual);
            return 1;
        }
        const size_t count = static_cast<size_t>(info.width) *
            static_cast<size_t>(info.height) * static_cast<size_t>(info.numChannels);
        const int compare = CompareImages(reference, actual, count);
        free(reference);
        FreeImageData(actual);
        if (compare != 0) {
            return 1;
        }
    }

    return 0;
}
