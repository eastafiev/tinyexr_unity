# TinyEXR Unity Plugin Changelog

## [Unreleased]

### Added
- `GetImageSize(string filePath)` method - Returns image dimensions as `Vector2Int` without loading pixel data
  - Fast alternative to `GetInfo()` when you only need width and height
  - Example: `Vector2Int size = TinyEXRLoader.GetImageSize("file.exr");`

### Changed
- Updated `EXRLoaderExample.cs` to demonstrate `GetImageSize()` usage
- Updated README with new method documentation

### Fixed
- Fixed missing `miniz.h` include path in CMakeLists.txt files
- Fixed incorrect miniz.c path in root CMakeLists.txt
- Resolved CMake compiler test failures by using `C:\D\` path format instead of `D:\`

## [1.0.0] - 2025-02-06

### Initial Release
- Load EXR files as Unity Texture2D
- Multi-layer EXR support
- Automatic coordinate system conversion (Unity bottom-left origin)
- Gamma correction support
- RGB/RGBA channel conversion
- All TinyEXR compression formats supported
- Cross-platform: Windows, macOS, Linux
- Header-only implementation with miniz compression
