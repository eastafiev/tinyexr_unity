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
