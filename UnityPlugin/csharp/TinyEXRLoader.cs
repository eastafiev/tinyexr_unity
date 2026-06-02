using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace TinyEXR
{
    /// <summary>
    /// Unity wrapper for TinyEXR native plugin
    /// </summary>
    public static class TinyEXRLoader
    {
        private const string DLL_NAME = "UnityTinyEXR";

        [StructLayout(LayoutKind.Sequential)]
        public struct ImageInfo
        {
            public int width;
            public int height;
            public int numChannels;
            public int pixelType;
            public int hasAlpha;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct ConversionOptions
        {
            public int flipY;
            public int targetChannels;
            public float gammaCorrection;

            public static ConversionOptions Default => new ConversionOptions
            {
                flipY = 1,
                targetChannels = 4,
                gammaCorrection = 1.0f
            };

            public static ConversionOptions Linear => new ConversionOptions
            {
                flipY = 1,
                targetChannels = 4,
                gammaCorrection = 1.0f
            };

            public static ConversionOptions SRGB => new ConversionOptions
            {
                flipY = 1,
                targetChannels = 4,
                gammaCorrection = 2.2f
            };
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct LayerInfo
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string name;
            public int channelCount;
        }

        #region DLL Imports

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int LoadEXRForUnity(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            out IntPtr outData,
            out ImageInfo outInfo,
            out IntPtr outErrorMsg
        );

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int LoadEXRWithOptionsForUnity(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            out IntPtr outData,
            out ImageInfo outInfo,
            ref ConversionOptions options,
            out IntPtr outErrorMsg
        );

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int LoadEXRLayerForUnity(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            [MarshalAs(UnmanagedType.LPStr)] string layerName,
            out IntPtr outData,
            out ImageInfo outInfo,
            out IntPtr outErrorMsg
        );

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int GetEXRInfo(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            out ImageInfo outInfo,
            out IntPtr outErrorMsg
        );

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int GetEXRLayerInfo(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            out IntPtr outLayers,
            out int outLayerCount,
            out IntPtr outErrorMsg
        );

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern void FreeImageData(IntPtr data);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern void FreeLayerInfo(IntPtr layers);

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern void FreeErrorMessage(IntPtr errorMsg);

        #endregion

        /// <summary>
        /// Load EXR file as Texture2D with default settings
        /// </summary>
        public static Texture2D LoadEXR(string filePath, bool linear = true)
        {
            var options = linear ? ConversionOptions.Linear : ConversionOptions.SRGB;
            return LoadEXRWithOptions(filePath, options);
        }

        /// <summary>
        /// Load EXR file with custom options
        /// </summary>
        public static Texture2D LoadEXRWithOptions(string filePath, ConversionOptions options)
        {
            IntPtr dataPtr = IntPtr.Zero;
            IntPtr errorPtr = IntPtr.Zero;

            try
            {
                ImageInfo info;
                int result = LoadEXRWithOptionsForUnity(filePath, out dataPtr, out info, ref options, out errorPtr);

                if (result != 0)
                {
                    string errorMsg = GetErrorMessage(errorPtr);
                    throw new Exception($"Failed to load EXR: {errorMsg}");
                }

                return CreateTexture(dataPtr, info, options.gammaCorrection == 1.0f);
            }
            finally
            {
                if (dataPtr != IntPtr.Zero) FreeImageData(dataPtr);
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }
        }

        /// <summary>
        /// Load specific layer from multi-layer EXR
        /// </summary>
        public static Texture2D LoadLayer(string filePath, string layerName, bool linear = true)
        {
            IntPtr dataPtr = IntPtr.Zero;
            IntPtr errorPtr = IntPtr.Zero;

            try
            {
                ImageInfo info;
                int result = LoadEXRLayerForUnity(filePath, layerName, out dataPtr, out info, out errorPtr);

                if (result != 0)
                {
                    string errorMsg = GetErrorMessage(errorPtr);
                    throw new Exception($"Failed to load layer '{layerName}': {errorMsg}");
                }

                return CreateTexture(dataPtr, info, linear);
            }
            finally
            {
                if (dataPtr != IntPtr.Zero) FreeImageData(dataPtr);
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }
        }

        /// <summary>
        /// Get EXR file information without loading pixels
        /// </summary>
        public static ImageInfo GetInfo(string filePath)
        {
            IntPtr errorPtr = IntPtr.Zero;

            try
            {
                ImageInfo info;
                int result = GetEXRInfo(filePath, out info, out errorPtr);

                if (result != 0)
                {
                    string errorMsg = GetErrorMessage(errorPtr);
                    throw new Exception($"Failed to get EXR info: {errorMsg}");
                }

                return info;
            }
            finally
            {
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }
        }

        /// <summary>
        /// Get image size (width, height) without loading pixels
        /// </summary>
        public static Vector2Int GetImageSize(string filePath)
        {
            ImageInfo info = GetInfo(filePath);
            return new Vector2Int(info.width, info.height);
        }

        /// <summary>
        /// Get all layer names from multi-layer EXR
        /// </summary>
        public static LayerInfo[] GetLayers(string filePath)
        {
            IntPtr layersPtr = IntPtr.Zero;
            IntPtr errorPtr = IntPtr.Zero;

            try
            {
                int layerCount;
                int result = GetEXRLayerInfo(filePath, out layersPtr, out layerCount, out errorPtr);

                if (result != 0)
                {
                    string errorMsg = GetErrorMessage(errorPtr);
                    throw new Exception($"Failed to get layers: {errorMsg}");
                }

                if (layerCount == 0)
                    return new LayerInfo[0];

                LayerInfo[] layers = new LayerInfo[layerCount];
                int structSize = Marshal.SizeOf(typeof(LayerInfo));

                for (int i = 0; i < layerCount; i++)
                {
                    IntPtr ptr = new IntPtr(layersPtr.ToInt64() + i * structSize);
                    layers[i] = Marshal.PtrToStructure<LayerInfo>(ptr);
                }

                return layers;
            }
            finally
            {
                if (layersPtr != IntPtr.Zero) FreeLayerInfo(layersPtr);
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }
        }

        #region Helper Methods

        private static Texture2D CreateTexture(IntPtr dataPtr, ImageInfo info, bool linear)
        {
            TextureFormat format = info.numChannels == 4 ? TextureFormat.RGBAFloat : TextureFormat.RGBFloat;
            Texture2D texture = new Texture2D(info.width, info.height, format, false, linear);

            int pixelCount = info.width * info.height * info.numChannels;
            float[] pixels = new float[pixelCount];
            Marshal.Copy(dataPtr, pixels, 0, pixelCount);

            Color[] colors = new Color[info.width * info.height];
            for (int i = 0; i < colors.Length; i++)
            {
                int idx = i * info.numChannels;
                colors[i] = new Color(
                    pixels[idx + 0],
                    pixels[idx + 1],
                    pixels[idx + 2],
                    info.numChannels == 4 ? pixels[idx + 3] : 1.0f
                );
            }

            texture.SetPixels(colors);
            texture.Apply();

            return texture;
        }

        private static string GetErrorMessage(IntPtr errorPtr)
        {
            if (errorPtr == IntPtr.Zero)
                return "Unknown error";

            return Marshal.PtrToStringAnsi(errorPtr) ?? "Unknown error";
        }

        #endregion
    }
}
