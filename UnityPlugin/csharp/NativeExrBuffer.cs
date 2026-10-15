using System;

namespace TinyEXR
{
    /// <summary>
    /// Owns decoded EXR pixels allocated by the UnityTinyEXR native plugin.
    /// Dispose after the pixels have been uploaded to a Unity texture.
    /// </summary>
    public sealed class NativeExrBuffer : IDisposable
    {
        private IntPtr _dataPointer;
        private bool _disposed;

        internal NativeExrBuffer(IntPtr dataPointer, UTinyEXRLoader.ImageInfo info)
        {
            // The successful native decode transfers ownership of this pointer to the buffer.
            if (dataPointer == IntPtr.Zero)
                throw new ArgumentException("Native EXR data pointer cannot be zero.", nameof(dataPointer));

            Info = info;
            FloatCount = checked((long)info.width * info.height * info.numChannels);
            ByteCount = checked(FloatCount * sizeof(float));
            _dataPointer = dataPointer;
        }

        ~NativeExrBuffer()
        {
            // Guard against native memory leaks when a caller fails to dispose the decoded image.
            Dispose(false);
        }

        public UTinyEXRLoader.ImageInfo Info { get; }
        public int Width => Info.width;
        public int Height => Info.height;
        public int NumChannels => Info.numChannels;
        public long FloatCount { get; }
        public long ByteCount { get; }

        internal IntPtr DataPointer
        {
            get
            {
                if (_disposed)
                    throw new ObjectDisposedException(nameof(NativeExrBuffer));

                return _dataPointer;
            }
        }

        // public API - add this line before public API methods
        public void Dispose()
        {
            // Free through the same native module that allocated the decoded pixels.
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        private void Dispose(bool disposing)
        {
            // Make disposal idempotent because upload and caller cleanup may share a failure path.
            if (_disposed)
                return;

            if (_dataPointer != IntPtr.Zero)
            {
                UTinyEXRLoader.ReleaseImageData(_dataPointer);
                _dataPointer = IntPtr.Zero;
            }

            _disposed = true;
        }
    }
}
