// pngread.cpp - (c) 2015 by Arthur Langereis (@zenmumbler)
// this file is hosted at: http://github.com/zenmumbler/pngread
// for license and info, go there.

#include "zlibredux/zlib.h"

#include <netinet/in.h>

#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include <algorithm>
#include <cassert>
#include <chrono>


class Stopwatch {
	std::chrono::high_resolution_clock::time_point start = std::chrono::high_resolution_clock::now();

public:
	uint64_t lap_us() {
		using namespace std::chrono;
		auto n = high_resolution_clock::now();
		auto us = duration_cast<microseconds>(n - start);
		start = n;
		return us.count();
	}
};


struct ChunkHeader {
	uint32_t dataSize;
	uint32_t chunkType;
};

constexpr uint32_t fourCharCode(char a, char b, char c, char d) {
	return (d << 24) | (c << 16) | (b << 8) | a;
}


enum ChunkType : uint32_t {
	HeaderChunk = fourCharCode('I','H','D','R'),
	ImageDataChunk = fourCharCode('I','D','A','T'),
	EndChunk = fourCharCode('I','E','N','D')
};

enum class ColorType : uint8_t {
	Grayscale = 0,
	RGB = 2,
	Palette = 3,
	GrayscaleAlpha = 4,
	RGBA = 6
};


// for MSVC, remove the __attribute__ thing wrap this struct
// in pragma pack (push, 1) and pragma pack (pop) 
struct IHDRChunk {
	uint32_t Width;        // Width of image in pixels
	uint32_t Height;       // Height of image in pixels
	uint8_t BitDepth;      // Bits per pixel or per sample
	uint8_t ColorType;     // Color interpretation indicator
	uint8_t Compression;   // Compression type indicator
	uint8_t Filter;        // Filter type indicator 
	uint8_t Interlace;     // Type of interlacing scheme used
} __attribute__((__packed__));

static_assert(sizeof(IHDRChunk) == 13, "");


int inflateBuffer(const std::vector<uint8_t>& source, std::vector<uint8_t>& dest) {
	// WARNING: this usage of inflate is specialized for this use case.
	// We know the expanded size up front and pass in the fully allocated
	// in and out buffers and thus only need a single inflate() call, see
	// http://zlib.net/zpipe.c for a full, compatible version of this.
	z_stream strm;
	strm.zalloc = nullptr;
	strm.zfree = nullptr;
	strm.opaque = nullptr;
	strm.avail_in = static_cast<uint32_t>(source.size());
	strm.next_in = const_cast<uint8_t*>(source.data());
	strm.avail_out = static_cast<uint32_t>(dest.size());
	strm.next_out = dest.data();
	auto ret = inflateInit(&strm);
	if (ret != Z_OK)
		return ret;

	ret = inflate(&strm, Z_NO_FLUSH);
	inflateEnd(&strm);
	return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


enum LineFilter : uint8_t {
	LFNone = 0,
	LFSub = 1,
	LFUp = 2,
	LFAverage = 3,
	LFPaeth = 4
};


class PNGFile {
	uint32_t width_ = 0, height_ = 0, bpp_ = 0;
	std::vector<uint8_t> compressedData_, imageData_;

	void nextChunk(std::istream& png) {
		std::vector<uint8_t> tempData(8192); // seems to be common IDAT data size
		auto appender = std::back_inserter(compressedData_);

		ChunkHeader chdr;
		png.read(reinterpret_cast<char*>(&chdr), sizeof(ChunkHeader));
		chdr.dataSize = ntohl(chdr.dataSize);

		auto typeName = std::string { reinterpret_cast<char*>(&chdr.chunkType), reinterpret_cast<char*>(&chdr.chunkType) + 4 };
		// std::cout << "Chunk: " << typeName << '\n';
		// std::cout << "Size : " << chdr.dataSize << '\n';

		switch (chdr.chunkType) {
			case HeaderChunk:
			{
				IHDRChunk ihdr;
				png.read(reinterpret_cast<char*>(&ihdr), sizeof(IHDRChunk));
				width_ = ntohl(ihdr.Width);
				height_ = ntohl(ihdr.Height);
				std::cout << "Width : " << width_ << '\n';
				std::cout << "Height: " << height_ << '\n';
				std::cout << "Bits  : " << (int)ihdr.BitDepth << '\n';
				std::cout << "Kind  : " << (int)ihdr.ColorType << '\n';
				std::cout << "Compression : " << (int)ihdr.Compression << '\n';
				std::cout << "Filter : " << (int)ihdr.Filter << '\n';
				std::cout << "Interlace : " << (int)ihdr.Interlace << '\n';

				assert(ihdr.BitDepth == 8);
				assert((ColorType)ihdr.ColorType != ColorType::Palette);
				assert(ihdr.Filter == 0);
				assert(ihdr.Interlace == 0);

				switch ((ColorType)ihdr.ColorType) {
					case ColorType::RGB: bpp_ = 3; break;
					case ColorType::GrayscaleAlpha: bpp_ = 2; break;
					case ColorType::RGBA: bpp_ = 4; break;
					default: bpp_ = 1; break;
				}

				compressedData_.reserve(width_ * height_); // reasonable amount of mem for deflated data
				break;
			}

			case ImageDataChunk:
			{
				if (chdr.dataSize > tempData.size())
					tempData.resize(chdr.dataSize);
				png.read(reinterpret_cast<char*>(tempData.data()), chdr.dataSize);
				std::copy(tempData.begin(), tempData.begin() + chdr.dataSize, appender);
				break;
			}

			default:
				// unsupported chunks are ignored
				png.seekg(chdr.dataSize, std::ios::cur);
				break;
		}

		// skip crc
		png.seekg(4, std::ios::cur);
	}

	void unfilterImage() {
		// Reverse the filtering done by the PNG encoder program to restore the original image.
		// See: http://www.fileformat.info/format/png/corion.htm for more info.

		auto addv = [](uint32_t a, uint32_t b) {
			return (a + b) & 0xff;
		};

		auto rowPtr = imageData_.data();
		auto rowBytes = width_ * bpp_;
		auto rowPitch = rowBytes + 1;

		for (auto lineIx = 0u; lineIx < height_; ++lineIx) {
			LineFilter filter = (LineFilter)(*rowPtr++);
			assert((uint32_t)filter < 5);

			auto row = rowPtr;
			auto bytes = rowBytes;

			if (filter != LFNone) {
				if (filter == LFSub) {
					row += bpp_;
					bytes -= bpp_;

					while (bytes--) {
						*row = addv(*row, *(row - bpp_));
						++row;
					}
				}
				else if (filter == LFUp) {
					while (bytes--) {
						*row = addv(*row, *(row - rowPitch));
						++row;
					}
				}
				else if (filter == LFAverage) {
					if (lineIx == 0) {
						rowPtr += bpp_;
						bytes -= bpp_;

						while (bytes--) {
							*row = addv(*row, *(row - bpp_) >> 1);
							++row;
						}
					}
					else {
						while (bytes) {
							if (bytes + bpp_ <= rowBytes)
								*row = addv(*row, (*(row - bpp_) + *(row - rowPitch)) >> 1);
							else
								*row = addv(*row, (*(row - rowPitch)) >> 1);
							++row;
							--bytes;
						}
					}
				}
				else { // filter == LFPaeth
					auto paethPredictor = [](int a, int b, int c) -> int {
						using std::abs;
						// a = left, b = above, c = upper left
						auto p = a + b - c;    // initial estimate
						auto pa = abs(p - a);  // distances to a, b, c
						auto pb = abs(p - b);
						auto pc = abs(p - c);
						// return nearest of a,b,c,
						// breaking ties in order a,b,c.
						if (pa <= pb && pa <= pc)
							return a;
						if (pb <= pc)
							return b;
						return c;
					};

					if (lineIx == 0) {
						rowPtr += bpp_;
						bytes -= bpp_;

						while (bytes--) {
							*row = addv(*row, paethPredictor(*(row - bpp_), 0, 0));
							++row;
						}
					}
					else {
						auto rowAbove = row - rowPitch;
						auto rowLeft = row;                 // row and rowAbove are first pushed +bpp_ bytes in first loop
						auto rowAboveLeft = row - rowPitch; // placing these to row(Above) - bpp_ positions for the 2nd loop

						auto firstBytes = bpp_;
						bytes -= bpp_;

						while (firstBytes--) {
							*row = addv(*row, paethPredictor(0, *rowAbove, 0));
							++row;
							++rowAbove;
						}

						while (bytes--) {
							*row = addv(*row, paethPredictor(*rowLeft, *rowAbove, *rowAboveLeft));

							++row;
							++rowLeft;
							++rowAbove;
							++rowAboveLeft;
						}
					}
				}
			}

			rowPtr += rowBytes;
		}
	}

public:
	PNGFile(const std::string& resourcePath) {
		Stopwatch timer;
		std::ifstream png { resourcePath, std::ios::binary };

		uint8_t realSig[8], expectedSig[8] = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A };
		png.read(reinterpret_cast<char*>(realSig), 8);
		assert(std::equal(realSig, realSig + 8, expectedSig, expectedSig + 8));

		while (png)
			nextChunk(png);
		std::cout << "chunks  : " << timer.lap_us() << " us\n";

		imageData_.resize((rowBytes() + 1) * height());
		inflateBuffer(compressedData_, imageData_);
		std::cout << "inflate : " << timer.lap_us() << " us\n";

		unfilterImage();
		std::cout << "unfilter: " << timer.lap_us() << " us\n";
	}

	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }
	uint32_t bytesPerPixel() const { return bpp_; }
	uint32_t rowBytes() const { return width_ * bpp_; }

	uint8_t* rowDataPointer(uint32_t row) {
		return imageData_.data() + (row * (rowBytes() + 1)) + 1;
	}
};


int main() {
	PNGFile png { "main.png" };
	std::ofstream out { "out.raw", std::ios::out | std::ios::binary };

	for (int32_t row = 0; row < png.height(); ++row) {
		auto line = png.rowDataPointer(row);
		out.write((char*)line, png.rowBytes());
	}
}
