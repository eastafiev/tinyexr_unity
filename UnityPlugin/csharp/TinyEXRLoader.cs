using System;
using System.IO;
using System.Runtime.InteropServices;
using Unity.Collections;
using UnityEngine;

namespace TinyEXR
{
    public static class UTinyEXRLoader
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

            public static ConversionOptions Default => Linear;

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

        /// <summary>
        /// Reason why an EXR file may not be loadable via LoadEXR().
        /// </summary>
        public enum ExrSupportReason
        {
            Ok = 0,
            NotExr = 1,
            Multipart = 2,
            DeepImage = 3,
            RequiresLayer = 4,
            LayerNotFound = 5,
            Spectral = 6,
            Compression = 7,
            HeaderError = 8
        }

        /// <summary>
        /// Header-based load compatibility (no pixel decode).
        /// </summary>
        [StructLayout(LayoutKind.Sequential)]
        public struct ExrCompatibility
        {
            public int isSupported;
            public int reason;
            public int isMultipart;
            public int isDeepImage;
            public int isTiled;
            public int isSpectral;
            public int isYca;
            public int hasLayers;
            public int layerCount;
            public int partCount;
            public int compressionType;
            public int width;
            public int height;
            public int numChannels;

            public bool IsSupported => isSupported != 0;
            public ExrSupportReason ReasonCode => (ExrSupportReason)reason;
            public bool IsMultipart => isMultipart != 0;
            public bool IsDeepImage => isDeepImage != 0;
            public bool IsTiled => isTiled != 0;
            public bool IsSpectral => isSpectral != 0;
            public bool IsYca => isYca != 0;
            public bool HasLayers => hasLayers != 0;

            /// <summary>
            /// Human-readable explanation matching native loader errors.
            /// </summary>
            public string GetReasonMessage()
            {
                switch (ReasonCode)
                {
                    case ExrSupportReason.Ok:
                        return "EXR is supported by LoadEXR().";
                    case ExrSupportReason.NotExr:
                        return "Invalid EXR file.";
                    case ExrSupportReason.Multipart:
                        return "Loading multipart or DeepImage is not supported yet";
                    case ExrSupportReason.DeepImage:
                        return "Loading multipart or DeepImage is not supported yet.";
                    case ExrSupportReason.RequiresLayer:
                        return "EXR contains layered channels; use LoadLayer() with a layer name.";
                    case ExrSupportReason.LayerNotFound:
                        return "Layer Not Found.";
                    case ExrSupportReason.Spectral:
                        return "Spectral EXR is not supported.";
                    case ExrSupportReason.Compression:
                        return "Unsupported EXR compression type.";
                    case ExrSupportReason.HeaderError:
                        return "Failed to parse EXR header.";
                    default:
                        return "Unknown compatibility issue.";
                }
            }
        }

        #region DLL Imports

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int LoadEXRWithOptionsForUnity(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            out IntPtr outData,
            out ImageInfo outInfo,
            ref ConversionOptions options,
            out IntPtr outErrorMsg
        );

        [DllImport(DLL_NAME, CallingConvention = CallingConvention.StdCall)]
        private static extern int LoadEXRWithResizeForUnity(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            int targetWidth,
            int targetHeight,
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
        private static extern int GetEXRCompatibilityForUnity(
            [MarshalAs(UnmanagedType.LPStr)] string filename,
            [MarshalAs(UnmanagedType.LPStr)] string layerName,
            out ExrCompatibility outInfo,
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
        private static Texture2D LoadEXRWithOptions(string filePath, ConversionOptions options)
        {
            ValidateFilePath(filePath);

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
        /// Load EXR file with resize (synchronous)
        /// </summary>
        public static Texture2D LoadEXR(string filePath, int width, int height, bool linear = true)
        {
            var options = linear ? ConversionOptions.Linear : ConversionOptions.SRGB;
            return LoadEXRWithResize(filePath, width, height, options);
        }

        /// <summary>
        /// Load EXR file with resize and custom options (synchronous).
        /// Must be called from the Unity main thread because it creates a Texture2D.
        /// </summary>
        public static Texture2D LoadEXRWithResize(string filePath, int width, int height, ConversionOptions options)
        {
            var raw = LoadEXRWithResizeRaw(filePath, width, height, options);
            return CreateTextureFromRaw(raw, options.gammaCorrection == 1.0f);
        }

        /// <summary>
        /// Load and resize EXR into managed pixel bytes. Safe to call from any thread.
        /// </summary>
        public static ExrRawImageData LoadEXRWithResizeRaw(string filePath, int width, int height, ConversionOptions options)
        {
            ValidateFilePath(filePath);
            ValidateDimensions(width, height);

            IntPtr dataPtr = IntPtr.Zero;
            IntPtr errorPtr = IntPtr.Zero;

            try
            {
                ImageInfo info;
                int result = LoadEXRWithResizeForUnity(filePath, width, height, out dataPtr, out info, ref options,
                    out errorPtr);

                if (result != 0)
                {
                    string errorMsg = GetErrorMessage(errorPtr);
                    throw new Exception($"Failed to load EXR with resize: {errorMsg}");
                }

                return CopyNativeToRaw(dataPtr, info);
            }
            catch (Exception ex)
            { 
                
                Debug.LogError($"{ex.Message}");
                throw new InvalidOperationException();
            }
            
            finally
            {
                if (dataPtr != IntPtr.Zero) FreeImageData(dataPtr);
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }

           
        }

        /// <summary>
        /// Load full-size EXR into managed pixel bytes. Safe to call from any thread.
        /// </summary>
        public static ExrRawImageData LoadEXRRaw(string filePath, ConversionOptions options)
        {
            ValidateFilePath(filePath);

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

                return CopyNativeToRaw(dataPtr, info);
            }
            finally
            {
                if (dataPtr != IntPtr.Zero) FreeImageData(dataPtr);
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }
        }

        /// <summary>
        /// Create a Texture2D from raw EXR pixel data. Must be called on the Unity main thread.
        /// </summary>
        public static Texture2D CreateTextureFromRaw(ExrRawImageData raw, bool linear)
        {
            if (raw == null)
                throw new ArgumentNullException(nameof(raw));
            if (raw.Data == null || raw.Data.Length == 0)
                throw new ArgumentException("Raw EXR data is empty.", nameof(raw));

            long pixelCount = (long)raw.Width * raw.Height;
            if (pixelCount <= 0)
                throw new ArgumentException($"Invalid EXR dimensions: {raw.Width}x{raw.Height}.", nameof(raw));

            ValidateChannelCount(raw.NumChannels);

            long expectedFloats = pixelCount * raw.NumChannels;
            if (raw.Data.Length != expectedFloats)
            {
                throw new ArgumentException(
                    $"EXR float count mismatch: got {raw.Data.Length}, expected {expectedFloats} for " +
                    $"{raw.Width}x{raw.Height} with {raw.NumChannels} channels.",
                    nameof(raw));
            }

            return CreateTextureFromFloats(raw.Data, raw.Width, raw.Height, raw.NumChannels, linear);
        }

        /// <summary>
        /// Load EXR file with resize asynchronously (Unity-friendly).
        /// Intended for callers already on the main thread.
        /// If the caller runs on a background thread, use LoadEXRWithResizeRaw and CreateTextureFromRaw
        /// after switching to the main thread instead.
        /// </summary>
        public static async System.Threading.Tasks.Task<Texture2D> LoadEXRAsync(string filePath, int width, int height, bool linear = true)
        {
            var options = linear ? ConversionOptions.Linear : ConversionOptions.SRGB;
            return await LoadEXRWithResizeAsync(filePath, width, height, options);
        }

        /// <summary>
        /// Load EXR file with resize and custom options asynchronously (Unity-friendly).
        /// Intended for callers already on the main thread.
        /// If the caller runs on a background thread, use LoadEXRWithResizeRaw and CreateTextureFromRaw
        /// after switching to the main thread instead.
        /// </summary>
        public static async System.Threading.Tasks.Task<Texture2D> LoadEXRWithResizeAsync(string filePath, int width, int height, ConversionOptions options)
        {
            // Decode and resize on a worker thread; resume on the caller context so texture upload stays on main thread.
            var raw = await System.Threading.Tasks.Task.Run(() =>
                LoadEXRWithResizeRaw(filePath, width, height, options)).ConfigureAwait(true);

            return CreateTextureFromRaw(raw, options.gammaCorrection == 1.0f);
        }

        /// <summary>
        /// Load specific layer from multi-layer EXR
        /// </summary>
        public static Texture2D LoadLayer(string filePath, string layerName, bool linear = true)
        {
            ValidateFilePath(filePath);
            if (string.IsNullOrWhiteSpace(layerName))
                throw new ArgumentException("Layer name cannot be null or empty.", nameof(layerName));

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
        /// Check whether LoadEXR() can load this file (header pre-flight, no pixel decode).
        /// </summary>
        public static bool IsLoadSupported(string filePath)
        {
            return GetCompatibility(filePath).IsSupported;
        }

        /// <summary>
        /// Check load compatibility for LoadEXR() without decoding pixels.
        /// </summary>
        public static ExrCompatibility GetCompatibility(string filePath)
        {
            return GetCompatibility(filePath, null);
        }

        /// <summary>
        /// Check load compatibility for LoadEXR() or LoadLayer(layerName).
        /// </summary>
        public static ExrCompatibility GetCompatibility(string filePath, string layerName)
        {
            ValidateFilePath(filePath);

            IntPtr errorPtr = IntPtr.Zero;

            try
            {
                ExrCompatibility compat;
                GetEXRCompatibilityForUnity(filePath, layerName, out compat, out errorPtr);
                return compat;
            }
            finally
            {
                if (errorPtr != IntPtr.Zero) FreeErrorMessage(errorPtr);
            }
        }

        /// <summary>
        /// Get EXR file information without loading pixels
        /// </summary>
        public static ImageInfo GetInfo(string filePath)
        {
            ValidateFilePath(filePath);

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
            ValidateFilePath(filePath);

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

        private static void ValidateFilePath(string filePath)
        {
            if (string.IsNullOrWhiteSpace(filePath))
                throw new ArgumentException("File path cannot be null or empty.", nameof(filePath));

            if (!File.Exists(filePath))
                throw new FileNotFoundException($"File does not exist: {filePath}", filePath);
        }

        private static void ValidateDimensions(int width, int height)
        {
            if (width <= 0 || height <= 0)
                throw new ArgumentOutOfRangeException(nameof(width),
                    $"Invalid resize dimensions: {width}x{height}.");
        }

        private static ExrRawImageData CopyNativeToRaw(IntPtr dataPtr, ImageInfo info)
        {
            ValidateChannelCount(info.numChannels);

            // A byte[] cannot hold 2 GiB. 16384x8192 RGBA float is exactly that size,
            // so copy floats instead: the same image is 536,870,912 elements.
            const int maxArrayLength = 0x7FFFFFC7;
            long floatCount;
            try
            {
                floatCount = checked((long)info.width * info.height * info.numChannels);
            }
            catch (OverflowException)
            {
                throw new InvalidOperationException(
                    $"EXR buffer is too large to copy: {info.width}x{info.height}, {info.numChannels} channels.");
            }

            if (info.width <= 0 || info.height <= 0 || floatCount <= 0 || floatCount > maxArrayLength)
            {
                throw new InvalidOperationException(
                    $"EXR buffer is too large to copy: {info.width}x{info.height}, {info.numChannels} channels ({floatCount} floats).");
            }

            var data = new float[(int)floatCount];
            Marshal.Copy(dataPtr, data, 0, data.Length);

            return new ExrRawImageData
            {
                Width = info.width,
                Height = info.height,
                NumChannels = info.numChannels,
                Data = data
            };
        }

        private static void ValidateChannelCount(int numChannels)
        {
            if (numChannels < 1 || numChannels > 4)
                throw new NotSupportedException($"Unsupported EXR channel count: {numChannels}. Expected 1-4 channels.");
        }

        private static Texture2D CreateTexture(IntPtr dataPtr, ImageInfo info, bool linear)
        {
            ValidateChannelCount(info.numChannels);
            return CreateTextureFromBytes(
                dataPtr,
                info.width * info.height * info.numChannels * sizeof(float),
                info.width,
                info.height,
                info.numChannels,
                linear);
        }

        private static Texture2D CreateTextureFromFloats(float[] pixels, int width, int height, int numChannels, bool linear)
        {
            if (numChannels != 4)
            {
                throw new NotSupportedException(
                    $"RGBAFloat upload requires 4 channels, got {numChannels}.");
            }

            var texture = new Texture2D(width, height, TextureFormat.RGBAFloat, false, linear);
            try
            {
                var destination = texture.GetPixelData<float>(0);
                if (destination.Length != pixels.Length)
                {
                    throw new InvalidOperationException(
                        $"Texture float buffer is {destination.Length}, EXR has {pixels.Length}.");
                }

                // SetPixelData builds a byte span with an int length, which overflows at 2 GiB.
                // Copy into the texture CPU buffer in slices that stay under that limit.
                const int floatsPerSlice = 32 * 1024 * 1024;
                int offset = 0;
                while (offset < pixels.Length)
                {
                    int count = Math.Min(floatsPerSlice, pixels.Length - offset);
                    NativeArray<float>.Copy(pixels, offset, destination, offset, count);
                    offset += count;
                }

                texture.Apply();
                return texture;
            }
            catch
            {
                UnityEngine.Object.Destroy(texture);
                throw;
            }
        }

        private static Texture2D CreateTextureFromBytes(
            IntPtr dataPtr, int byteCount, int width, int height, int numChannels, bool linear)
        {
            var texture = new Texture2D(width, height, TextureFormat.RGBAFloat, false, linear);
            texture.LoadRawTextureData(dataPtr, byteCount);
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
