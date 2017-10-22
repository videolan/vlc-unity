using UnityEngine;
using System.Runtime.InteropServices;

public class TestPlugin : MonoBehaviour
{
    [DllImport("TestPlugin", EntryPoint="generate_image")]
    private static extern void GenerateImage(Color32[] buffer, int width, int height);

    void Start ()
    {
        var startTime = Time.realtimeSinceStartup;

        var pixels = new Color32[1024 * 1024];
        GenerateImage(pixels, 1024, 1024);

        var endTime = Time.realtimeSinceStartup;
        Debug.Log("execution time = " + (endTime - startTime));

        var texture = new Texture2D(1024, 1024);
        texture.wrapMode = TextureWrapMode.Clamp;
        texture.SetPixels32(pixels);
        texture.Apply();

        GetComponent<Renderer>().material.mainTexture = texture;
    }
}
