using UnityEngine;
using System.IO;
using TinyEXR;

/// <summary>
/// Example script demonstrating how to load and display EXR files in Unity
/// </summary>
public class EXRLoaderExample : MonoBehaviour
{
    [Header("EXR File Settings")]
    [Tooltip("Name of the EXR file in StreamingAssets folder")]
    [SerializeField] private string exrFileName = "example.exr";
    
    [SerializeField] private bool loadAsLinear = true;
    [SerializeField] private float gammaCorrection = 1.0f;

    [Header("Resize Settings")]
    [Tooltip("Enable to resize the texture during load")]
    [SerializeField] private bool enableResize = false;
    [SerializeField] private int targetWidth = 1024;
    [SerializeField] private int targetHeight = 1024;
    [Tooltip("Use async loading (recommended for large images or resize operations)")]
    [SerializeField] private bool useAsyncLoading = false;

    [Header("Display")]
    [SerializeField] private Renderer targetRenderer;
    [SerializeField] private bool showInfo = true;

    private void Start()
    {
        if (useAsyncLoading && enableResize)
        {
            LoadAndDisplayEXRAsync();
        }
        else
        {
            LoadAndDisplayEXR();
        }
    }

    private async void LoadAndDisplayEXRAsync()
    {
        string filePath = Path.Combine(Application.streamingAssetsPath, exrFileName);

        if (!File.Exists(filePath))
        {
            Debug.LogError($"EXR file not found: {filePath}");
            return;
        }

        try
        {
            // Get original image size first
            Vector2Int originalSize = TinyEXRLoader.GetImageSize(filePath);
            Debug.Log($"?? Original EXR Size: {originalSize.x}x{originalSize.y}");

            if (enableResize)
            {
                Debug.Log($"?? Resizing to: {targetWidth}x{targetHeight}");
            }

            // Load with async resize
            Texture2D texture = await TinyEXRLoader.LoadEXRAsync(filePath, targetWidth, targetHeight, loadAsLinear);

            Debug.Log($"? Loaded EXR: {texture.width}x{texture.height}, Format: {texture.format}");

            // Display info
            if (showInfo)
            {
                var info = TinyEXRLoader.GetInfo(filePath);
                Debug.Log($"  Channels: {info.numChannels}, Has Alpha: {info.hasAlpha != 0}");
            }

            // Apply to renderer
            if (targetRenderer != null)
            {
                targetRenderer.material.mainTexture = texture;
                Debug.Log("??? Applied texture to renderer");
            }
        }
        catch (System.Exception e)
        {
            Debug.LogError($"Failed to load EXR: {e.Message}");
        }
    }

    private void LoadAndDisplayEXR()
    {
        string filePath = Path.Combine(Application.streamingAssetsPath, exrFileName);

        if (!File.Exists(filePath))
        {
            Debug.LogError($"EXR file not found: {filePath}");
            return;
        }

        try
        {
            // Get image size first (fast, doesn't load pixels)
            Vector2Int size = TinyEXRLoader.GetImageSize(filePath);
            Debug.Log($"?? EXR Size: {size.x}x{size.y}");

            Texture2D texture;

            if (enableResize)
            {
                // Option 1: Load with resize (synchronous)
                Debug.Log($"?? Resizing to: {targetWidth}x{targetHeight}");
                texture = TinyEXRLoader.LoadEXR(filePath, targetWidth, targetHeight, loadAsLinear);
            }
            else
            {
                // Option 2: Simple load without resize
                texture = TinyEXRLoader.LoadEXR(filePath, loadAsLinear);
            }

            // Option 3: Load with custom options
            /*
            var options = new TinyEXRLoader.ConversionOptions
            {
                flipY = 1,
                targetChannels = 4,
                gammaCorrection = gammaCorrection
            };
            
            if (enableResize)
            {
                texture = TinyEXRLoader.LoadEXRWithResize(filePath, targetWidth, targetHeight, options);
            }
            else
            {
                texture = TinyEXRLoader.LoadEXRWithOptions(filePath, options);
            }
            */

            Debug.Log($"? Loaded EXR: {texture.width}x{texture.height}, Format: {texture.format}");

            // Display info
            if (showInfo)
            {
                var info = TinyEXRLoader.GetInfo(filePath);
                Debug.Log($"  Channels: {info.numChannels}, Has Alpha: {info.hasAlpha != 0}");
            }

            // Apply to renderer
            if (targetRenderer != null)
            {
                targetRenderer.material.mainTexture = texture;
                Debug.Log("??? Applied texture to renderer");
            }
        }
        catch (System.Exception e)
        {
            Debug.LogError($"Failed to load EXR: {e.Message}");
        }
    }

    [ContextMenu("Reload EXR")]
    public void ReloadEXR()
    {
        LoadAndDisplayEXR();
    }
}
