namespace TinyEXR
{
    /// <summary>
    /// Decoded EXR pixel payload returned by <see cref="UTinyEXRLoader"/>.
    /// </summary>
    public class ExrRawImageData
    {
        public int Width { get; set; }
        public int Height { get; set; }
        public int NumChannels { get; set; }

        /// <summary>
        /// Tightly packed managed float pixels retained for compatibility.
        /// Prefer NativeExrBuffer for texture upload to avoid the native-to-managed copy.
        /// </summary>
        public float[] Data { get; set; }
    }
}
