# pngread
A small PNG file reader implementation aimed at game dev.<br>
(c) 2015 by Arthur Langereis (@zenmumbler)

This implements a functional but limited PNG file loader _without_ using libpng.

## Features and Omissions

- Read grayscale, grayscale+alpha, RGB and RGBA image data
- Paletted image files **not** supported
- 8-bit colour components only
- Ignores gamma, custom transparency and other optional blocks

Essentially, this is aimed at getting common, non-paletted PNG files into your program in a simple way.
If you already use a library like libpng then you don't need this, only use this if you want to
use a lightweight PNG loader for component image formats, typically in games and such.

## Example usage

    int main() {
      PNGFile png { "main.png" };
      std::ofstream out { "out.raw", std::ios::out | std::ios::binary };

      for (int32_t row = 0; row < png.height(); ++row) {
        auto line = png.rowDataPointer(row);
        out.write((char*)line, png.rowBytes());
      }
    }

This takes a PNG file and outputs the raw image data in another file. Note that you must request
the row pointer for each row separately. Also see the stuff in pngread.cpp.

## Notes and Caveats

PNG loading requires inflate from zlib, I have included a minimal distribution of zlib 1.2.8
that only has the files required to have inflate working. There are other inflate implementations,
you can try and use another one.

PNG loading is relatively slow. PNG's image data is both compressed and pre-filtered to allow
for higher compression rates. PNGs are optimised for file size, not access size. For optimum
speed, use other compressed formats like DDS or plain formats like BMP. If distribution size is
an issue though and you don't mind the multi-ms load times of PNGs, they are handy.

This sample code uses asserts for things you may or not may not want to crash on, like unsupported
PNG formats. You will want to modify things to fit in your project.

I use a modified version of this code in my [stardazed game library](http://github.com/zenmumbler/stardazed/)

## License and Acknowledgements

You can use, modify and incorporate the code in the pngread.cpp file in your own projects,
commercial or not, freely. If you do use a (modified) version of the code in pngread.cpp,
then I ask you credit me by my name and twitter handler as on the top of this file.

zlib is Copyright (C) 1995-2013 Jean-loup Gailly and Mark Adler, see http://zlib.net/
