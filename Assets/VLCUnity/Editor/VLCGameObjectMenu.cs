using UnityEditor;
using UnityEngine;
using UnityEngine.UI;

namespace LibVLCSharp
{
    public static class VLCGameObjectMenu
    {
        private static readonly Vector2 DefaultVideoSize = new Vector2(640f, 360f);

        [MenuItem("GameObject/VLC/Media Player (No Display)", false, 50)]
        private static void CreateVLCMediaPlayer(MenuCommand menuCommand)
        {
            GameObject go = new GameObject("VLC Media Player");
            go.AddComponent<VLCMediaPlayer>();

            GameObjectUtility.SetParentAndAlign(go, menuCommand.context as GameObject);

            Undo.RegisterCreatedObjectUndo(go, "Create " + go.name);

            Selection.activeObject = go;
        }

        [MenuItem("GameObject/VLC/Media Player + UI Display", false, 51)]
        private static void CreateVLCMediaPlayerWithUIDisplay(MenuCommand menuCommand)
        {
            const string undoName = "Create VLC Media Player + UI Display";
            int undoGroup = Undo.GetCurrentGroup();
            Undo.SetCurrentGroupName(undoName);

            GameObject parent = GetUIParent(menuCommand.context as GameObject, undoName);
            GameObject go = new GameObject(
                "VLC Media Player + UI Display",
                typeof(RectTransform),
                typeof(CanvasRenderer),
                typeof(RawImage),
                typeof(VLCMediaPlayer),
                typeof(VLCDisplayUGUI));

            GameObjectUtility.SetParentAndAlign(go, parent);
            go.layer = LayerMask.NameToLayer("UI");

            RectTransform rectTransform = go.GetComponent<RectTransform>();
            rectTransform.sizeDelta = DefaultVideoSize;

            VLCMediaPlayer mediaPlayer = go.GetComponent<VLCMediaPlayer>();
            VLCDisplayUGUI display = go.GetComponent<VLCDisplayUGUI>();
            display.mediaPlayer = mediaPlayer;

            go.GetComponent<RawImage>().raycastTarget = false;

            Undo.RegisterCreatedObjectUndo(go, undoName);
            Undo.CollapseUndoOperations(undoGroup);

            Selection.activeObject = go;
        }

        private static GameObject GetUIParent(GameObject context, string undoName)
        {
            Canvas parentCanvas = context != null ? context.GetComponentInParent<Canvas>() : null;
            if (parentCanvas != null)
                return context.transform is RectTransform ? context : parentCanvas.gameObject;

            GameObject canvasObject = new GameObject(
                "Canvas",
                typeof(RectTransform),
                typeof(Canvas),
                typeof(CanvasScaler),
                typeof(GraphicRaycaster));
            canvasObject.layer = LayerMask.NameToLayer("UI");
            canvasObject.GetComponent<Canvas>().renderMode = RenderMode.ScreenSpaceOverlay;

            Undo.RegisterCreatedObjectUndo(canvasObject, undoName);
            return canvasObject;
        }
    }
}
