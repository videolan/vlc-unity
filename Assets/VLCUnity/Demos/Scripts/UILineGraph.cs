using System;
using UnityEngine;
using UnityEngine.UI;

namespace LibVLCSharp
{
    public class UILineGraph : MaskableGraphic
    {
        private float[] DataPoints = new float[60];
        private float LineThickness = 2.0f;

        private Color LineColor = new Color32(0, 212, 255, 255);
        private Color FillColorTop = new Color32(0, 212, 255, 100);
        private Color FillColorBottom = new Color32(0, 212, 255, 0);

        public void AddPoint(float value)
        {
            Array.Copy(DataPoints, 1, DataPoints, 0, DataPoints.Length - 1);
            DataPoints[^1] = value;
            SetVerticesDirty();
        }

        protected override void OnPopulateMesh(VertexHelper vh)
        {
            vh.Clear();

            Rect rect = GetPixelAdjustedRect();
            if (rect.width <= 0 || rect.height <= 0)
                return;

            float yAxisLimit = CalculateYAxisLimit();
            float step = rect.width / (DataPoints.Length - 1);

            for (int i = 0; i < DataPoints.Length - 1; i++)
            {
                DrawSegment(vh, rect, i, step, yAxisLimit);
            }
        }

        private float CalculateYAxisLimit()
        {
            float maxDataValue = 0f;

            for (int i = 0; i < DataPoints.Length; i++)
            {
                if (DataPoints[i] > maxDataValue)
                    maxDataValue = DataPoints[i];
            }

            return Mathf.Max(1f, maxDataValue * 1.2f);
        }

        private void DrawSegment(VertexHelper vh, Rect rect, int index, float step, float yAxisLimit)
        {
            float val1 = Mathf.Clamp01(DataPoints[index] / yAxisLimit);
            float val2 = Mathf.Clamp01(DataPoints[index + 1] / yAxisLimit);

            float x1 = rect.x + (index * step);
            float x2 = rect.x + ((index + 1) * step);
            float y1 = rect.y + (val1 * rect.height);
            float y2 = rect.y + (val2 * rect.height);

            DrawFill(vh, x1, x2, y1, y2, rect.y);
            DrawLine(vh, x1, x2, y1, y2);
        }

        private void DrawFill(VertexHelper vh, float x1, float x2, float y1, float y2, float baseY)
        {
            int vIndex = vh.currentVertCount;
            UIVertex v = UIVertex.simpleVert;

            v.position = new Vector2(x1, baseY);
            v.color = FillColorBottom;
            vh.AddVert(v);

            v.position = new Vector2(x1, y1);
            v.color = FillColorTop;
            vh.AddVert(v);

            v.position = new Vector2(x2, y2);
            v.color = FillColorTop;
            vh.AddVert(v);

            v.position = new Vector2(x2, baseY);
            v.color = FillColorBottom;
            vh.AddVert(v);

            vh.AddTriangle(vIndex, vIndex + 1, vIndex + 2);
            vh.AddTriangle(vIndex, vIndex + 2, vIndex + 3);
        }

        private void DrawLine(VertexHelper vh, float x1, float x2, float y1, float y2)
        {
            int vIndex = vh.currentVertCount;

            Vector2 p1 = new(x1, y1);
            Vector2 p2 = new(x2, y2);
            Vector2 dir = (p2 - p1).normalized;

            if (dir == Vector2.zero)
                dir = Vector2.right;

            Vector2 normal = new Vector2(-dir.y, dir.x) * (LineThickness * 0.5f);
            UIVertex v = UIVertex.simpleVert;

            v.position = p1 - normal;
            v.color = LineColor;
            vh.AddVert(v);

            v.position = p1 + normal;
            v.color = LineColor;
            vh.AddVert(v);

            v.position = p2 + normal;
            v.color = LineColor;
            vh.AddVert(v);

            v.position = p2 - normal;
            v.color = LineColor;
            vh.AddVert(v);

            vh.AddTriangle(vIndex, vIndex + 1, vIndex + 2);
            vh.AddTriangle(vIndex, vIndex + 2, vIndex + 3);
        }
    }
}
