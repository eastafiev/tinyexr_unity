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

    [Header("Display")]
    [SerializeField] private Renderer targetRenderer;
    [SerializeField] private bool showInfo = true;

    private void Start()
    {
        LoadAndDisplayEXR();
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

            // Option 1: Simple load
            Texture2D texture = TinyEXRLoader.LoadEXR(filePath, loadAsLinear);

            // Option 2: Load with custom options
            /*
            var options = new TinyEXRLoader.ConversionOptions
            {
                flipY = 1,
                targetChannels = 4,
                gammaCorrection = gammaCorrection
            };
            Texture2D texture = TinyEXRLoader.LoadEXRWithOptions(filePath, options);
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
