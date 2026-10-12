# TinyEXR Unity Plugin

A native Unity plugin for loading OpenEXR (.exr) files with multi-layer support.

## Features

- ? Load EXR files as Unity Texture2D
- ? Multi-layer EXR support
- ? Automatic coordinate system conversion (Unity bottom-left origin)
- ? Gamma correction support
- ? RGB/RGBA channel conversion
- ? All TinyEXR compression formats supported

## Building

Every successful build stages a Unity Assets-shaped package at repo-root `dist/` and opens that folder:

```
dist/
  Plugins/x86_64/UnityTinyEXR.dll          (Windows)
  Plugins/macOS/libUnityTinyEXR.bundle     (macOS)
  Plugins/Linux/x86_64/libUnityTinyEXR.so  (Linux)
  Scripts/TinyEXR/TinyEXRLoader.cs
```

Copy `dist/Plugins` and `dist/Scripts` into your Unity project's `Assets/` folder.

### Windows

```cmd
build.bat
```

Or with Unity deployment (also copies into the project):

```cmd
build.bat --unity "C:\Path\To\UnityProject"
```

### Linux/macOS

```bash
chmod +x build.sh
./build.sh
```

Or with Unity deployment:

```bash
./build.sh --unity "/path/to/UnityProject"
```

### Build Options

- `--debug` - Build in Debug mode
- `--unity PATH` - Deploy to Unity project at PATH
- `--clean` - Clean build directory before building
- `--verbose` - Show verbose build output
- `--help` - Show help message

## Installation (Manual)

1. Build the plugin using the build script (`dist/` opens when finished)
2. Copy `dist/Plugins` and `dist/Scripts` into your Unity project's `Assets/`:

```
YourUnityProject/
  Assets/
    Plugins/
      x86_64/
        UnityTinyEXR.dll       (Windows)
      macOS/
        libUnityTinyEXR.bundle (macOS)
      Linux/x86_64/
        libUnityTinyEXR.so     (Linux)
    Scripts/
      TinyEXR/
        TinyEXRLoader.cs
```

## Usage

### Basic Loading

```csharp
using TinyEXR;

// Load EXR file
Texture2D texture = TinyEXRLoader.LoadEXR("path/to/file.exr");

// Apply to material
renderer.material.mainTexture = texture;
```

### With Options

```csharp
using TinyEXR;

var options = new TinyEXRLoader.ConversionOptions
{
    flipY = 1,              // Flip for Unity coordinates
    targetChannels = 4,     // RGBA
    gammaCorrection = 1.0f  // Linear (use 2.2 for sRGB)
};

Texture2D texture = TinyEXRLoader.LoadEXRWithOptions("path/to/file.exr", options);
```

### Multi-Layer Support

```csharp
using TinyEXR;

// Get all layers
var layers = TinyEXRLoader.GetLayers("multilayer.exr");

foreach (var layer in layers)
{
    Debug.Log($"Layer: {layer.name}, Channels: {layer.channelCount}");
}

// Load specific layer
Texture2D diffuse = TinyEXRLoader.LoadLayer("multilayer.exr", "diffuse");
Texture2D specular = TinyEXRLoader.LoadLayer("multilayer.exr", "specular");
```

### Get File Info

```csharp
using TinyEXR;

// Get image size quickly (doesn't load pixels)
Vector2Int size = TinyEXRLoader.GetImageSize("file.exr");
Debug.Log($"Size: {size.x}x{size.y}");

// Get detailed information
var info = TinyEXRLoader.GetInfo("file.exr");
Debug.Log($"Size: {info.width}x{info.height}");
Debug.Log($"Channels: {info.numChannels}");
Debug.Log($"Has Alpha: {info.hasAlpha != 0}");
```

## Example Scripts

See `UnityPlugin/examples/` for complete examples:

- `EXRLoaderExample.cs` - Basic EXR loading and display
- `EXRLayerViewer.cs` - Multi-layer EXR viewer with UI

## API Reference

### TinyEXRLoader

| Method | Description |
|--------|-------------|
| `LoadEXR(string path, bool linear)` | Load EXR file with default settings |
| `LoadEXRWithOptions(string path, ConversionOptions)` | Load with custom options |
| `LoadLayer(string path, string layerName, bool linear)` | Load specific layer |
| `GetImageSize(string path)` | Get image dimensions as Vector2Int (fast) |
| `GetInfo(string path)` | Get detailed file information without loading pixels |
| `GetLayers(string path)` | Get all layer names and channel counts |

### Structures

```csharp
struct ImageInfo
{
    int width;
    int height;
    int numChannels;
    int pixelType;
    int hasAlpha;
}

struct ConversionOptions
{
    int flipY;              // 0 or 1
    int targetChannels;     // 3 or 4
    float gammaCorrection;  // 1.0 = linear, 2.2 = sRGB
}

struct LayerInfo
{
    string name;           // Layer name
    int channelCount;      // Number of channels in layer
}
```

## Requirements

- Unity 2019.4 or later
- Windows, macOS, or Linux (x86_64)
- CMake 3.5+
- Ninja build system (or Visual Studio on Windows)

## Building from Source

### Prerequisites

**Windows:**
- Visual Studio 2019 or later
- CMake 3.5+
- Ninja (optional, can use Visual Studio generator)

**Linux:**
```bash
sudo apt-get install cmake ninja-build g++
```

**macOS:**
```bash
brew install cmake ninja
```

### Build Steps

1. Clone the repository with submodules:
```bash
git clone https://github.com/syoyo/tinyexr.git
cd tinyexr
git checkout unity_texture2d
```

2. Build:
```bash
./build.sh  # Linux/macOS
build.bat   # Windows
```

3. The plugin will be in `build/bin/`

## License

This plugin uses TinyEXR which is licensed under the 3-clause BSD license.

## Credits

- TinyEXR by Syoyo Fujita: https://github.com/syoyo/tinyexr
- Unity plugin wrapper by [Your Name]
