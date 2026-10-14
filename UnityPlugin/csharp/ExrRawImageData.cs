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
        /// Tightly packed float pixels. One float[] stays under the runtime array
        /// limit for images whose byte size is 2 GiB or larger (16384x8192 RGBA float).
        /// </summary>
        public float[] Data { get; set; } // Maybe it worth to use NativeArray[float], cause further we copy float[] into it.
    }
}
