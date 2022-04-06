#! /bin/sh
convert watermark.png RGBA:watermark.raw
xxd -i watermark.png watermark.png.h
xxd -i watermark.raw watermark.raw.h
