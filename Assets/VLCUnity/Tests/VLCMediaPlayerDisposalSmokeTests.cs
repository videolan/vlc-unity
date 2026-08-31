using System;
using System.Collections;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
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

        readonly System.Collections.Generic.List<GameObject> _createdObjects = new();
        GameObject _cameraObject;

        [UnitySetUp]
        public IEnumerator EnterPlayModeForDisposalSmoke()
        {
            yield return new EnterPlayMode();

            // A camera guarantees that Unity services queued plugin events even
            // when the scene that was open before the test contained no camera.
            _cameraObject = new GameObject("VLC disposal smoke camera");
            _cameraObject.AddComponent<Camera>();
            _createdObjects.Add(_cameraObject);
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

            // Ensure a failed assertion does not leave deferred native cleanup
            // queued for the next test or Editor Play Mode session.
            TextureHelper.QueueRendererCleanup();
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
            string mediaPath = ResolveMediaPath();
            if (mediaPath == null)
            {
                Assert.Ignore(
                    $"Set {MediaEnvironmentVariable} to a short local video " +
                    "to run the native disposal smoke test.");
            }

            // Warm up LibVLC, the graphics backend, and any driver-level caches
            // before taking the Linux descriptor baseline.
            yield return CreatePlayAndDispose(mediaPath, "warm-up player");
            int descriptorBaseline = CountOpenFileDescriptors();

            // CancelPreload disposes a background MediaPlayer through a path
            // separate from VLCMediaPlayer.DestroyMediaPlayer().
            yield return CancelPreloadAndDisposeOwner(mediaPath);

            // Exercise callers that use LibVLCSharp directly rather than the
            // VLCMediaPlayer component's managed cleanup helper.
            yield return CreatePlayAndDisposeDirectly(mediaPath);

            // Dispose while frames may still be in flight, then recreate twice.
            // Reaching a new output texture proves the previous generation did
            // not leave callback or graphics state that crashes its successor.
            yield return CreatePlayAndDispose(mediaPath, "recreated player 1");
            yield return CreatePlayAndDispose(mediaPath, "recreated player 2");

            int finalDescriptorCount = CountOpenFileDescriptors();
            if (descriptorBaseline >= 0 && finalDescriptorCount >= 0)
            {
                Assert.That(
                    finalDescriptorCount,
                    Is.LessThanOrEqualTo(descriptorBaseline + 1),
                    "Linux file-descriptor count grew across disposal. " +
                    $"Baseline={descriptorBaseline}, final={finalDescriptorCount}.");
            }
        }

        IEnumerator CreatePlayAndDispose(string mediaPath, string objectName)
        {
            GameObject playerObject = new GameObject(objectName);
            _createdObjects.Add(playerObject);
            VLCMediaPlayer player = playerObject.AddComponent<VLCMediaPlayer>();
            player.playOnAwake = false;

            Task openTask = player.OpenAsync(mediaPath);
            yield return WaitForTask(openTask, $"opening media for {objectName}");

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

        IEnumerator CancelPreloadAndDisposeOwner(string mediaPath)
        {
            GameObject playerObject = new GameObject("preload cancellation player");
            _createdObjects.Add(playerObject);
            VLCMediaPlayer player = playerObject.AddComponent<VLCMediaPlayer>();
            player.playOnAwake = false;

            Task preloadTask = player.PreloadAsync(mediaPath);
            yield return null;
            player.CancelPreload();
            yield return WaitForTask(preloadTask, "cancelling preload");

            Assert.That(player.CurrentPreloadState, Is.EqualTo(VLCMediaPlayer.PreloadState.None));
            yield return WaitForRendererDrain("CancelPreload()");

            UnityEngine.Object.Destroy(playerObject);
            yield return null;
            yield return WaitForRendererDrain("preload owner disposal");
        }

        static IEnumerator CreatePlayAndDisposeDirectly(string mediaPath)
        {
            MediaPlayer directPlayer = null;
            Media directMedia = null;
            Texture2D directTexture = null;

            try
            {
                directPlayer = new MediaPlayer(VLCMediaPlayer.LibVLC);
                directMedia = new Media(new Uri(mediaPath));
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

        static IEnumerator WaitForTask(Task task, string operation)
        {
            for (int frame = 0;
                 frame < OperationTimeoutFrames && !task.IsCompleted;
                 ++frame)
            {
                yield return null;
            }

            Assert.That(
                task.IsCompleted,
                Is.True,
                $"Timed out while {operation} after {OperationTimeoutFrames} frames.");
            Assert.That(task.IsCanceled, Is.False, $"Task was cancelled while {operation}.");
            if (task.IsFaulted)
            {
                Assert.Fail(
                    $"Task failed while {operation}: " +
                    task.Exception?.Flatten().InnerException);
            }
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

            configured = configured.Trim().Trim('"');
            if (Uri.TryCreate(configured, UriKind.Absolute, out Uri uri) &&
                !string.Equals(uri.Scheme, Uri.UriSchemeFile, StringComparison.OrdinalIgnoreCase))
            {
                Assert.Fail(
                    $"{MediaEnvironmentVariable} must point to a local media file, " +
                    $"not a {uri.Scheme} URI.");
            }

            string localPath = uri != null && uri.IsFile
                ? uri.LocalPath
                : Path.GetFullPath(configured);
            if (!File.Exists(localPath))
            {
                Assert.Fail(
                    $"{MediaEnvironmentVariable} does not exist: {localPath}");
            }
            return new Uri(localPath).AbsoluteUri;
        }

        static int CountOpenFileDescriptors()
        {
#if UNITY_EDITOR_LINUX
            try
            {
                return Directory.EnumerateFileSystemEntries("/proc/self/fd").Count();
            }
            catch (IOException)
            {
                return -1;
            }
            catch (UnauthorizedAccessException)
            {
                return -1;
            }
#else
            return -1;
#endif
        }
    }
}
