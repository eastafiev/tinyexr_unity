#ifndef UNITY_TINYEXR_PLUGIN_H
#define UNITY_TINYEXR_PLUGIN_H

#ifdef _WIN32
    #ifdef UNITY_TINYEXR_EXPORTS
        #define UNITY_TINYEXR_API __declspec(dllexport)
    #else
        #define UNITY_TINYEXR_API __declspec(dllimport)
    #endif
    #define UNITY_CALL __stdcall
#else
    #define UNITY_TINYEXR_API __attribute__((visibility("default")))
    #define UNITY_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Image info structure
typedef struct {
    int width;
    int height;
    int numChannels;
    int pixelType;  // 0=UINT, 1=HALF, 2=FLOAT
    int hasAlpha;
} UnityImageInfo;

// Conversion options
typedef struct {
    int flipY;            // 1=flip vertically for Unity coordinate system
    int targetChannels;   // 3=RGB, 4=RGBA
    float gammaCorrection; // 1.0=linear, 2.2=sRGB
} UnityConversionOptions;

// Layer info
typedef struct {
    char name[256];
    int channelCount;
} UnityLayerInfo;

// Core loading functions
UNITY_TINYEXR_API int UNITY_CALL LoadEXRForUnity(
    const char* filename,
    float** outData,
    UnityImageInfo* outInfo,
    char** outErrorMsg
);

UNITY_TINYEXR_API int UNITY_CALL LoadEXRWithOptionsForUnity(
    const char* filename,
    float** outData,
    UnityImageInfo* outInfo,
    UnityConversionOptions* options,
    char** outErrorMsg
);

UNITY_TINYEXR_API int UNITY_CALL LoadEXRWithResizeForUnity(
    const char* filename,
    int targetWidth,
    int targetHeight,
    float** outData,
    UnityImageInfo* outInfo,
    UnityConversionOptions* options,
    char** outErrorMsg
);

UNITY_TINYEXR_API int UNITY_CALL LoadEXRLayerForUnity(
    const char* filename,
    const char* layerName,
    float** outData,
    UnityImageInfo* outInfo,
    char** outErrorMsg
);

// Layer enumeration
UNITY_TINYEXR_API int UNITY_CALL GetEXRLayerCount(
    const char* filename,
    int* outLayerCount,
    char** outErrorMsg
);

UNITY_TINYEXR_API int UNITY_CALL GetEXRLayerInfo(
    const char* filename,
    UnityLayerInfo** outLayers,
    int* outLayerCount,
    char** outErrorMsg
);

// Memory management
UNITY_TINYEXR_API void UNITY_CALL FreeImageData(float* data);
UNITY_TINYEXR_API void UNITY_CALL FreeLayerInfo(UnityLayerInfo* layers);
UNITY_TINYEXR_API void UNITY_CALL FreeErrorMessage(char* errorMsg);

// Utility functions
UNITY_TINYEXR_API int UNITY_CALL GetEXRInfo(
    const char* filename,
    UnityImageInfo* outInfo,
    char** outErrorMsg
);

#ifdef __cplusplus
}
#endif

#endif // UNITY_TINYEXR_PLUGIN_H
