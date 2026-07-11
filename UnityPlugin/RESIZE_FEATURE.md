# EXR Resize Feature

The TinyEXR Unity plugin now supports loading EXR images with resizing (upscaling or downscaling) during the load operation. This is particularly useful for:
- Downscaling high-resolution EXR files for better performance
- Creating texture mipmaps at different resolutions
- Memory optimization by loading only the required size

## Features

- **Bilinear interpolation** for high-quality resizing
- **Synchronous and Asynchronous APIs** for Unity
- **Zero memory overhead** - resize happens during loading, no intermediate full-resolution texture
- **Compatible with all existing options** (gamma correction, channel conversion, etc.)

## Usage Examples

### Synchronous Loading with Resize

```csharp
using TinyEXR;

// Simple resize - downscale to 512x512
Texture2D texture = TinyEXRLoader.LoadEXR("path/to/file.exr", 512, 512, linear: true);

// With custom options
var options = new TinyEXRLoader.ConversionOptions
{
    flipY = 1,
    targetChannels = 4,
    gammaCorrection = 2.2f
};
Texture2D texture = TinyEXRLoader.LoadEXRWithResize("path/to/file.exr", 1024, 1024, options);
```

### Asynchronous Loading with Resize (Recommended)

For better performance and to avoid frame drops, especially with large images:

```csharp
using TinyEXR;
using UnityEngine;

public class AsyncEXRLoader : MonoBehaviour
{
    async void Start()
    {
        try
        {
            // Load and resize asynchronously
            Texture2D texture = await TinyEXRLoader.LoadEXRAsync("path/to/file.exr", 1024, 1024, linear: true);
            
            // Apply the texture
            GetComponent<Renderer>().material.mainTexture = texture;
            Debug.Log($"Loaded and resized to: {texture.width}x{texture.height}");
        }
        catch (System.Exception e)
        {
            Debug.LogError($"Failed to load EXR: {e.Message}");
        }
    }
}
```

### Dynamic Resolution Based on Device

```csharp
using TinyEXR;
using UnityEngine;

public class AdaptiveEXRLoader : MonoBehaviour
{
    async void Start()
    {
        string filePath = Application.streamingAssetsPath + "/highres.exr";
        
        // Get original dimensions
        Vector2Int originalSize = TinyEXRLoader.GetImageSize(filePath);
        Debug.Log($"Original: {originalSize.x}x{originalSize.y}");
        
        // Calculate target size based on device
        int targetSize = SystemInfo.graphicsMemorySize > 4096 ? 2048 : 1024;
        
        // Load with appropriate resolution
        Texture2D texture = await TinyEXRLoader.LoadEXRAsync(
            filePath, 
            targetSize, 
            targetSize, 
            linear: true
        );
        
        GetComponent<Renderer>().material.mainTexture = texture;
    }
}
```

## API Reference

### Synchronous Methods

```csharp
// Load with resize
Texture2D LoadEXR(string filePath, int width, int height, bool linear = true)

// Load with resize and custom options
Texture2D LoadEXRWithResize(string filePath, int width, int height, ConversionOptions options)
```

### Asynchronous Methods

```csharp
// Load with resize asynchronously
async Task<Texture2D> LoadEXRAsync(string filePath, int width, int height, bool linear = true)

// Load with resize and custom options asynchronously
async Task<Texture2D> LoadEXRWithResizeAsync(string filePath, int width, int height, ConversionOptions options)
```

## Performance Considerations

### When to Use Async

- **Large images** (>2K resolution)
- **Multiple images** loading in sequence
- **During gameplay** to avoid frame drops
- **Resize operations** (always recommended for resize)

### When Synchronous is OK

- **Small images** (<1K resolution)
- **Loading screens** where frame drops are acceptable
- **Editor tools** where blocking is acceptable

## Example: Complete Loading System

```csharp
using TinyEXR;
using UnityEngine;
using System.Threading.Tasks;

public class EXRLoadingManager : MonoBehaviour
{
    [SerializeField] private string exrFileName = "environment.exr";
    [SerializeField] private Renderer targetRenderer;
    [SerializeField] private int maxTextureSize = 2048;
    
    async void Start()
    {
        await LoadEXRWithOptimalSize();
    }
    
    private async Task LoadEXRWithOptimalSize()
    {
        string filePath = System.IO.Path.Combine(Application.streamingAssetsPath, exrFileName);
        
        if (!System.IO.File.Exists(filePath))
        {
            Debug.LogError($"File not found: {filePath}");
            return;
        }
        
        try
        {
            // Get original size
            Vector2Int originalSize = TinyEXRLoader.GetImageSize(filePath);
            Debug.Log($"Original EXR: {originalSize.x}x{originalSize.y}");
            
            // Calculate optimal size
            int width = Mathf.Min(originalSize.x, maxTextureSize);
            int height = Mathf.Min(originalSize.y, maxTextureSize);
            
            // Maintain aspect ratio
            float aspectRatio = (float)originalSize.x / originalSize.y;
            if (width < height)
            {
                height = Mathf.RoundToInt(width / aspectRatio);
            }
            else
            {
                width = Mathf.RoundToInt(height * aspectRatio);
            }
            
            Debug.Log($"Loading at: {width}x{height}");
            
            // Load asynchronously with resize
            Texture2D texture = await TinyEXRLoader.LoadEXRAsync(filePath, width, height, linear: true);
            
            // Apply to renderer
            if (targetRenderer != null)
            {
                targetRenderer.material.mainTexture = texture;
                Debug.Log("? EXR loaded and applied successfully!");
            }
        }
        catch (System.Exception e)
        {
            Debug.LogError($"Failed to load EXR: {e.Message}\n{e.StackTrace}");
        }
    }
}
```

## Technical Details

### Resize Algorithm

The plugin uses **bilinear interpolation** for resizing:
- Fast and efficient
- Good quality for most use cases
- Hardware-agnostic (runs on all platforms)

### Memory Usage

When using resize:
1. Original EXR is decoded at full resolution in native memory
2. Resize is performed in native memory
3. Only the resized data is copied to Unity managed memory
4. Full-resolution data is freed immediately

This means the peak memory usage is: `original_size + resized_size` (native) + `resized_size` (managed)

### Thread Safety

- **Synchronous methods**: Run on the calling thread (usually Unity main thread)
- **Async methods**: Native loading/resizing runs on background thread, texture creation on main thread

## Building

After modifying the native plugin, rebuild using:

**Windows:**
```batch
build_unity.bat
```

**Linux/macOS:**
```bash
./build_unity.sh
```

The plugin will be automatically copied to the Unity project's `Plugins` folder.
