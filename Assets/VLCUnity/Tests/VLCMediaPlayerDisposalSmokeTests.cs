using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using NUnit.Framework;
using UnityEngine;
using UnityEngine.TestTools;

namespace LibVLCSharp.Tests
{
    /// <summary>
    /// GPU-backed disposal smoke coverage. Set VLC_UNITY_DISPOSAL_TEST_MEDIA to
    /// a short local video before running this test from the EditMode test tab.
    /// The test enters Play Mode itself so Unity processes native render events.
    /// </summary>
    public sealed class VLCMediaPlayerDisposalSmokeTests
    {
        const string MediaEnvironmentVariable = "VLC_UNITY_DISPOSAL_TEST_MEDIA";
        const int OperationTimeoutFrames = 300;
        const int CleanupTimeoutFrames = 120;

        readonly List<GameObject> _createdObjects = new();
        string _mediaPath;

        [UnitySetUp]
        public IEnumerator EnterPlayModeForDisposalSmoke()
        {
            yield return new EnterPlayMode();

            _mediaPath = ResolveMediaPath();
            if (_mediaPath == null)
            {
                Assert.Ignore(
                    $"Set {MediaEnvironmentVariable} to a short local video " +
                    "to run the native disposal smoke test.");
            }

            var cameraObject = new GameObject("VLC disposal smoke camera");
            cameraObject.AddComponent<Camera>();
            _createdObjects.Add(cameraObject);
        }

        [UnityTearDown]
        public IEnumerator LeavePlayModeAfterDisposalSmoke()
        {
            foreach (GameObject createdObject in _createdObjects)
            {
                if (createdObject != null)
                    UnityEngine.Object.Destroy(createdObject);
            }
            _createdObjects.Clear();

            yield return null;

            for (int frame = 0;
                 frame < CleanupTimeoutFrames && TextureHelper.HasRetiredRenderers();
                 ++frame)
            {
                yield return null;
            }

            yield return new ExitPlayMode();
        }

        [UnityTest]
        [Timeout(120000)]
        public IEnumerator DisposalDrainsRenderersAndPlaybackCanBeRecreated()
        {
            yield return CreatePlayAndDispose("warm-up player");
            yield return CreatePlayAndDisposeDirectly();
            yield return CreatePlayAndDispose("recreated player");
        }

        IEnumerator CreatePlayAndDispose(string objectName)
        {
            GameObject playerObject = new GameObject(objectName);
            _createdObjects.Add(playerObject);
            VLCMediaPlayer player = playerObject.AddComponent<VLCMediaPlayer>();
            player.playOnAwake = false;

            player.Open(_mediaPath);

            for (int frame = 0;
                 frame < OperationTimeoutFrames && player.OutputTexture == null;
                 ++frame)
            {
                yield return null;
            }
            Assert.That(
                player.OutputTexture,
                Is.Not.Null,
                $"{objectName} did not produce an output texture within " +
                $"{OperationTimeoutFrames} frames.");

            // Give the render thread a chance to have a copy/import in flight
            // when OnDestroy retires the native renderer.
            yield return null;
            yield return null;

            UnityEngine.Object.Destroy(playerObject);
            yield return null;
            yield return WaitForRendererDrain(objectName);
        }

        IEnumerator CreatePlayAndDisposeDirectly()
        {
            MediaPlayer directPlayer = null;
            Media directMedia = null;
            Texture2D directTexture = null;

            try
            {
                directPlayer = new MediaPlayer(VLCMediaPlayer.LibVLC);
                directMedia = new Media(new Uri(_mediaPath));
                directPlayer.Media = directMedia;
                directPlayer.Play();

                for (int frame = 0;
                     frame < OperationTimeoutFrames && directTexture == null;
                     ++frame)
                {
                    directTexture = TextureHelper.CreateNativeTexture(
                        directPlayer, linear: true);
                    yield return null;
                }
                Assert.That(
                    directTexture,
                    Is.Not.Null,
                    "Direct MediaPlayer did not produce a native texture within " +
                    $"{OperationTimeoutFrames} frames.");

                // Pump at least one frame through the renderer before disposing
                // its MediaPlayer without VLCMediaPlayer.DestroyMediaPlayer().
                TextureHelper.UpdateTexture(directTexture, directPlayer);
                yield return null;
                TextureHelper.UpdateTexture(directTexture, directPlayer);

                UnityEngine.Object.Destroy(directTexture);
                directTexture = null;
                directPlayer.Stop();
                directPlayer.Dispose();
                directPlayer = null;
                directMedia.Dispose();
                directMedia = null;
            }
            finally
            {
                if (directTexture != null)
                    UnityEngine.Object.Destroy(directTexture);
                directPlayer?.Dispose();
                directMedia?.Dispose();
            }

            yield return null;
            yield return WaitForRendererDrain("direct MediaPlayer.Dispose()");
        }

        static IEnumerator WaitForRendererDrain(string operation)
        {
            for (int frame = 0;
                 frame < CleanupTimeoutFrames && TextureHelper.HasRetiredRenderers();
                 ++frame)
            {
                yield return null;
            }

            Assert.That(
                TextureHelper.HasRetiredRenderers(),
                Is.False,
                $"Native renderers remained retired after {operation} for " +
                $"{CleanupTimeoutFrames} frames.");
        }

        static string ResolveMediaPath()
        {
            string configured = Environment.GetEnvironmentVariable(MediaEnvironmentVariable);
            if (string.IsNullOrWhiteSpace(configured))
                return null;

            string localPath = Path.GetFullPath(configured.Trim().Trim('"'));
            Assert.That(File.Exists(localPath), Is.True,
                $"{MediaEnvironmentVariable} does not exist: {localPath}");
            return new Uri(localPath).AbsoluteUri;
        }
    }
}
