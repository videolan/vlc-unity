#include "IUnityInterface.h"

namespace
{
    int mandelbrot(int px, int py, int width, int height)
    {
        auto c_r = 2.5f * px / width  - 2;
        auto c_i = 2.0f * py / height - 1;
        auto z_r = 0.0f;
        auto z_i = 0.0f;
        auto itr = 0;

        while (itr < 300 && z_r * z_r + z_i * z_i < 4)
        {
            auto z_r2 = z_r * z_r - z_i * z_i + c_r;
            auto z_i2 = z_r * z_i + z_i * z_r + c_i;
            z_r = z_r2;
            z_i = z_i2;
            itr++;
        }

        return itr;
    }
}

extern "C" void UNITY_INTERFACE_EXPORT generate_image(unsigned char* data, int width, int height)
{
    int offs = 0;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int m = mandelbrot(x, y, width, height);
            data[offs++] = m * 31;
            data[offs++] = m * 37;
            data[offs++] = m * 41;
            data[offs++] = 255;
        }
    }
}
