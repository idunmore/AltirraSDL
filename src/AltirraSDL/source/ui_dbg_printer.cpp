//	AltirraSDL - Dear ImGui debugger printer output pane
//	Replaces Win32 ATPrinterOutputWindow (uidbgprinteroutput.cpp).
//	Displays text output from emulated printers.  Supports multiple
//	printer outputs via a dropdown selector, with Clear button.
//	Graphical printer output is rendered as a rasterized image with
//	zoom/pan controls and export to PNG/PDF/SVG.

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/file.h>
#include <vd2/system/strutil.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/function.h>
#include <vd2/system/vectors.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "printeroutput.h"
#include "display_backend.h"
#include "gl_helpers.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// =========================================================================
// Graphical printer renderer — software rasterizer
// =========================================================================
//
// This is a simplified scalar version of the Windows 8x8 anti-aliased
// rasterizer. It renders dots and vectors to an XRGB8888 framebuffer
// that is then uploaded to an OpenGL or SDL texture for ImGui display.

namespace {

struct PrinterViewTransform {
	float mOriginX = 0;		// document-space left edge of viewport (mm)
	float mOriginY = 0;		// document-space top edge of viewport (mm)
	float mPixelsPerMM = 1;
	float mMMPerPixel = 1;
};

// Render dots and vectors to a framebuffer.
// The framebuffer is stored top-down (row 0 = top of image).
static void RenderPrinterOutput(
	ATPrinterGraphicalOutput& output,
	const PrinterViewTransform& vt,
	uint32 *framebuffer,
	uint32 w, uint32 h,
	float dotRadiusMM)
{
	if (w == 0 || h == 0)
		return;

	// Clear to white
	std::fill(framebuffer, framebuffer + w * h, 0xFFFFFFFF);

	// Compute document-space bounds of the viewport
	const float docX1 = vt.mOriginX;
	const float docY1 = vt.mOriginY;
	const float docX2 = vt.mOriginX + (float)w * vt.mMMPerPixel;
	const float docY2 = vt.mOriginY + (float)h * vt.mMMPerPixel;

	const bool hasVectors = output.HasVectors();

	// Pre-cull
	ATPrinterGraphicalOutput::CullInfo cullInfo;
	vdrect32f cullRect(docX1, docY1, docX2, docY2);
	bool hasDots = output.PreCull(cullInfo, cullRect);

	if (!hasDots && !hasVectors)
		return;

	// Render parameters
	const float subPixelsPerMM = vt.mPixelsPerMM * 4.0f;	// 4x4 sub-sampling
	const float dotRadius = dotRadiusMM;
	const float dotRadiusSq = dotRadius * dotRadius;

	using RenderDot = ATPrinterGraphicalOutput::RenderDot;
	using RenderVector = ATPrinterGraphicalOutput::RenderVector;
	vdfastvector<RenderDot> dotBuf;
	vdfastvector<RenderVector> vecBuf;

	// 4x4 anti-aliasing buffer (one row of pixels × 4 sub-rows)
	std::vector<uint16> abuf(w * 4, 0);

	for (uint32 yoff = 0; yoff < h; ++yoff) {
		const float docRowYC = docY1 + ((float)yoff + 0.5f) * vt.mMMPerPixel;
		const float docRowYD = 0.5f * vt.mMMPerPixel;
		const float docRowY1 = docRowYC - docRowYD;
		const float docRowY2 = docRowYC + docRowYD;

		// Cull dots to this scanline
		dotBuf.clear();
		vecBuf.clear();

		vdrect32f lineCullRect(docX1 - vt.mMMPerPixel * 0.5f, docRowY1,
			docX2 + vt.mMMPerPixel * 0.5f, docRowY2);
		output.ExtractNextLineDots(dotBuf, cullInfo, lineCullRect);

		if (hasVectors) {
			output.ExtractVectors(vecBuf, lineCullRect);

			// Add endpoint dots from vectors
			vdrect32f dotCullRect(
				lineCullRect.left - dotRadiusMM,
				lineCullRect.top - dotRadiusMM,
				lineCullRect.right + dotRadiusMM,
				lineCullRect.bottom + dotRadiusMM);

			for (const RenderVector& v : vecBuf) {
				if (dotCullRect.contains(vdpoint32f{v.mX1, v.mY1}))
					dotBuf.push_back(RenderDot{v.mX1, v.mY1, v.mLinearColor});
				if (dotCullRect.contains(vdpoint32f{v.mX2, v.mY2}))
					dotBuf.push_back(RenderDot{v.mX2, v.mY2, v.mLinearColor});
			}
		}

		// Clear AA buffer
		std::fill(abuf.begin(), abuf.end(), (uint16)0);

		// Render dots into AA buffer (4x4 sub-sampling, grayscale only for simplicity)
		for (const RenderDot& dot : dotBuf) {
			float dy = dot.mY - docRowYC;
			if (fabsf(dy) >= docRowYD + dotRadius)
				continue;

			// Process 4 sub-rows
			for (int sr = 0; sr < 4; ++sr) {
				float subRowY = docRowYC + ((float)sr - 1.5f) * vt.mMMPerPixel / 4.0f;
				float sdy = dot.mY - subRowY;
				float dxSq = dotRadiusSq - sdy * sdy;
				if (dxSq <= 0.0f)
					continue;

				float dx = sqrtf(dxSq);
				float xc = (dot.mX - docX1) * vt.mPixelsPerMM;
				float x1f = xc - dx * vt.mPixelsPerMM;
				float x2f = xc + dx * vt.mPixelsPerMM;

				int ix1 = std::max(0, (int)ceilf(x1f));
				int ix2 = std::min((int)w, (int)ceilf(x2f));

				uint16 *row = &abuf[sr * w];
				for (int x = ix1; x < ix2; ++x)
					row[x] = std::min<uint16>(row[x] + 4, 16);
			}
		}

		// Render vectors into AA buffer
		for (const RenderVector& v : vecBuf) {
			float dx = v.mX2 - v.mX1;
			float dy = v.mY2 - v.mY1;
			float lenSq = dx * dx + dy * dy;
			if (lenSq < 1e-6f)
				continue;

			float invLen = 1.0f / sqrtf(lenSq);
			// Perpendicular (points left): (-dy, dx) * dotRadius / len
			float px = -dy * invLen * dotRadius;
			float py = dx * invLen * dotRadius;

			// Four corners of the line rectangle
			float cx[4] = {
				v.mX1 + px, v.mX1 - px,
				v.mX2 - px, v.mX2 + px
			};
			float cy[4] = {
				v.mY1 + py, v.mY1 - py,
				v.mY2 - py, v.mY2 + py
			};

			for (int sr = 0; sr < 4; ++sr) {
				float subRowY = docRowYC + ((float)sr - 1.5f) * vt.mMMPerPixel / 4.0f;

				// Find x-range of the rectangle at this y using edge intersections
				float xmin = 1e10f, xmax = -1e10f;
				for (int e = 0; e < 4; ++e) {
					int e2 = (e + 1) & 3;
					float ey1 = cy[e], ey2 = cy[e2];
					if ((ey1 <= subRowY && ey2 >= subRowY) ||
						(ey2 <= subRowY && ey1 >= subRowY)) {
						float t = (ey1 == ey2) ? 0.5f : (subRowY - ey1) / (ey2 - ey1);
						float ex = cx[e] + t * (cx[e2] - cx[e]);
						xmin = std::min(xmin, ex);
						xmax = std::max(xmax, ex);
					}
				}

				if (xmin >= xmax)
					continue;

				float xc1 = (xmin - docX1) * vt.mPixelsPerMM;
				float xc2 = (xmax - docX1) * vt.mPixelsPerMM;
				int ix1 = std::max(0, (int)ceilf(xc1));
				int ix2 = std::min((int)w, (int)ceilf(xc2));

				uint16 *row = &abuf[sr * w];
				for (int x = ix1; x < ix2; ++x)
					row[x] = std::min<uint16>(row[x] + 4, 16);
			}
		}

		// Downsample AA buffer to framebuffer pixel row
		uint32 *dst = &framebuffer[yoff * w];
		for (uint32 x = 0; x < w; ++x) {
			uint32 coverage = abuf[0 * w + x] + abuf[1 * w + x] +
				abuf[2 * w + x] + abuf[3 * w + x];
			// coverage is 0..64, map to luminance 255..0
			uint32 luma = 255 - (coverage * 255 / 64);
			dst[x] = 0xFF000000 | (luma << 16) | (luma << 8) | luma;
		}
	}
}

// =========================================================================
// PNG encoding — minimal encoder using SDL surfaces
// =========================================================================

static bool SaveFramebufferAsBMP(const uint32 *framebuffer, int w, int h, const char *path) {
	SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h,
		SDL_PIXELFORMAT_ARGB8888,
		(void *)framebuffer, w * 4);
	if (!surface)
		return false;

	bool ok = SDL_SaveBMP(surface, path);
	SDL_DestroySurface(surface);
	return ok;
}

// Minimal PNG writer — writes uncompressed (store) DEFLATE blocks.
// This avoids any external dependency.
static bool SaveFramebufferAsPNG(const uint32 *framebuffer, int w, int h, const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;

	auto writeBE32 = [](uint8 *p, uint32 v) {
		p[0] = (uint8)(v >> 24);
		p[1] = (uint8)(v >> 16);
		p[2] = (uint8)(v >> 8);
		p[3] = (uint8)v;
	};

	// CRC32 table
	static uint32 crcTable[256];
	static bool crcInit = false;
	if (!crcInit) {
		for (uint32 n = 0; n < 256; n++) {
			uint32 c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
			crcTable[n] = c;
		}
		crcInit = true;
	}

	auto crc32 = [&](const uint8 *data, size_t len) -> uint32 {
		uint32 c = 0xFFFFFFFF;
		for (size_t i = 0; i < len; i++)
			c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >> 8);
		return c ^ 0xFFFFFFFF;
	};

	auto writeChunk = [&](const char *type, const uint8 *data, uint32 len) {
		uint8 hdr[8];
		writeBE32(hdr, len);
		memcpy(hdr + 4, type, 4);
		fwrite(hdr, 1, 8, f);
		// CRC covers type + data
		uint32 crc = 0xFFFFFFFF;
		for (int i = 4; i < 8; i++)
			crc = crcTable[(crc ^ hdr[i]) & 0xFF] ^ (crc >> 8);
		for (uint32 i = 0; i < len; i++)
			crc = crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
		crc ^= 0xFFFFFFFF;
		fwrite(data, 1, len, f);
		uint8 crcBuf[4];
		writeBE32(crcBuf, crc);
		fwrite(crcBuf, 1, 4, f);
	};

	// PNG signature
	static const uint8 sig[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	fwrite(sig, 1, 8, f);

	// IHDR
	{
		uint8 ihdr[13];
		writeBE32(ihdr, (uint32)w);
		writeBE32(ihdr + 4, (uint32)h);
		ihdr[8] = 8;	// bit depth
		ihdr[9] = 2;	// color type: RGB
		ihdr[10] = 0;	// compression
		ihdr[11] = 0;	// filter
		ihdr[12] = 0;	// interlace
		writeChunk("IHDR", ihdr, 13);
	}

	// IDAT — build raw image data (filter byte 0 + RGB for each row),
	// then wrap in zlib store blocks.
	// Each row: 1 filter byte + w*3 RGB bytes
	const uint32 rowBytes = 1 + w * 3;
	const uint64 rawSize = (uint64)rowBytes * h;

	// Build raw image
	std::vector<uint8> raw(rawSize);
	for (int y = 0; y < h; y++) {
		uint8 *rowDst = &raw[y * rowBytes];
		rowDst[0] = 0;	// no filter
		const uint32 *rowSrc = &framebuffer[y * w];
		for (int x = 0; x < w; x++) {
			uint32 px = rowSrc[x];
			rowDst[1 + x * 3 + 0] = (uint8)(px >> 16);	// R
			rowDst[1 + x * 3 + 1] = (uint8)(px >> 8);		// G
			rowDst[1 + x * 3 + 2] = (uint8)(px);			// B
		}
	}

	// Wrap in zlib (store blocks) — header(2) + blocks + adler32(4)
	// Each store block: 5-byte header + up to 65535 data bytes
	uint32 adler_a = 1, adler_b = 0;
	for (size_t i = 0; i < raw.size(); i++) {
		adler_a = (adler_a + raw[i]) % 65521;
		adler_b = (adler_b + adler_a) % 65521;
	}
	uint32 adler32 = (adler_b << 16) | adler_a;

	size_t numBlocks = (raw.size() + 65534) / 65535;
	size_t zlibSize = 2 + numBlocks * 5 + raw.size() + 4;
	std::vector<uint8> zlib(zlibSize);
	zlib[0] = 0x78;	// CMF
	zlib[1] = 0x01;	// FLG (no dict, level 0)

	size_t zpos = 2;
	size_t remaining = raw.size();
	size_t srcPos = 0;
	while (remaining > 0) {
		uint16 blockLen = (uint16)std::min<size_t>(remaining, 65535);
		bool last = (remaining <= 65535);
		zlib[zpos++] = last ? 0x01 : 0x00;
		zlib[zpos++] = (uint8)(blockLen & 0xFF);
		zlib[zpos++] = (uint8)(blockLen >> 8);
		uint16 nlen = ~blockLen;
		zlib[zpos++] = (uint8)(nlen & 0xFF);
		zlib[zpos++] = (uint8)(nlen >> 8);
		memcpy(&zlib[zpos], &raw[srcPos], blockLen);
		zpos += blockLen;
		srcPos += blockLen;
		remaining -= blockLen;
	}
	writeBE32(&zlib[zpos], adler32);
	zpos += 4;

	writeChunk("IDAT", zlib.data(), (uint32)zpos);

	// IEND
	writeChunk("IEND", nullptr, 0);

	fclose(f);
	return true;
}

// =========================================================================
// Minimal PDF writer — single page with embedded RGB image
// =========================================================================

static bool SaveFramebufferAsPDF(const uint32 *framebuffer, int w, int h, const char *path) {
	FILE *f = fopen(path, "wb");
	if (!f)
		return false;

	// Build raw RGB data
	std::vector<uint8> rgb(w * h * 3);
	for (int i = 0; i < w * h; i++) {
		uint32 px = framebuffer[i];
		rgb[i * 3 + 0] = (uint8)(px >> 16);
		rgb[i * 3 + 1] = (uint8)(px >> 8);
		rgb[i * 3 + 2] = (uint8)(px);
	}

	// PDF page size: fit image at 72 DPI scale
	// We use the actual document DPI that was used for rendering
	float pageW = (float)w;
	float pageH = (float)h;

	std::vector<long> objOffsets;
	auto recordObj = [&]() {
		objOffsets.push_back(ftell(f));
	};

	fprintf(f, "%%PDF-1.4\n%%\x80\x80\x80\x80\n");

	// Object 1: Catalog
	recordObj();
	fprintf(f, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

	// Object 2: Pages
	recordObj();
	fprintf(f, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

	// Object 3: Page
	recordObj();
	fprintf(f, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %g %g] /Contents 4 0 R /Resources << /XObject << /Im0 5 0 R >> >> >>\nendobj\n", pageW, pageH);

	// Object 4: Page content stream — draw image
	char contentStr[256];
	snprintf(contentStr, sizeof(contentStr), "q %g 0 0 %g 0 0 cm /Im0 Do Q", pageW, pageH);
	int contentLen = (int)strlen(contentStr);
	recordObj();
	fprintf(f, "4 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", contentLen, contentStr);

	// Object 5: Image XObject
	recordObj();
	fprintf(f, "5 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Length %d >>\nstream\n", w, h, (int)rgb.size());
	fwrite(rgb.data(), 1, rgb.size(), f);
	fprintf(f, "\nendstream\nendobj\n");

	// Cross-reference table
	long xrefOffset = ftell(f);
	fprintf(f, "xref\n0 %d\n", (int)objOffsets.size() + 1);
	fprintf(f, "0000000000 65535 f \n");
	for (size_t i = 0; i < objOffsets.size(); i++)
		fprintf(f, "%010ld 00000 n \n", objOffsets[i]);

	fprintf(f, "trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%ld\n%%%%EOF\n",
		(int)objOffsets.size() + 1, xrefOffset);

	fclose(f);
	return true;
}

// =========================================================================
// Minimal SVG writer — uses dot patterns like Windows version
// =========================================================================

static bool SavePrinterOutputAsSVG(ATPrinterGraphicalOutput& output, const char *path) {
	const ATPrinterGraphicsSpec& spec = output.GetGraphicsSpec();
	vdrect32f docBounds = output.GetDocumentBounds();

	static constexpr float kUnitsPerMM = 100.0f;

	// Round off and ensure non-zero size
	docBounds.left = roundf(docBounds.left * kUnitsPerMM) / kUnitsPerMM;
	docBounds.top = roundf(docBounds.top * kUnitsPerMM) / kUnitsPerMM;
	docBounds.right = roundf(docBounds.right * kUnitsPerMM) / kUnitsPerMM;
	docBounds.bottom = roundf(docBounds.bottom * kUnitsPerMM) / kUnitsPerMM;
	docBounds.right = std::max<float>(docBounds.right, docBounds.left + 10.0f);
	docBounds.bottom = std::max<float>(docBounds.bottom, docBounds.top + 10.0f);

	const float width = docBounds.width();
	const float height = docBounds.height();

	FILE *f = fopen(path, "w");
	if (!f)
		return false;

	fprintf(f, "<?xml version=\"1.0\" standalone=\"yes\"?>\n");
	fprintf(f, "<!DOCTYPE svg PUBLIC \"-//W3C/DTD/SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n");
	fprintf(f, "<svg width=\"%gmm\" height=\"%gmm\" viewBox=\"0 0 %d %d\" version=\"1.1\""
		" xmlns=\"http://www.w3.org/2000/svg\" xmlns:l=\"http://www.w3.org/1999/xlink\">\n",
		roundf(width * kUnitsPerMM) / kUnitsPerMM,
		roundf(height * kUnitsPerMM) / kUnitsPerMM,
		(int)roundf(width * kUnitsPerMM),
		(int)roundf(height * kUnitsPerMM));

	ATPrinterGraphicalOutput::CullInfo cullInfo {};
	const float dotDY = spec.mVerticalDotPitchMM * kUnitsPerMM * (spec.mbBit0Top ? 1.0f : -1.0f);
	const int dotRadius = (int)roundf(spec.mDotRadiusMM * 100);

	uint32 dotMasksUsed[8] {};

	if (output.PreCull(cullInfo, docBounds)) {
		vdfastvector<ATPrinterGraphicalOutput::RenderColumn> cols;
		float rawLineY = 0;
		while (output.ExtractNextLine(cols, rawLineY, cullInfo, docBounds)) {
			uint32 allDotMask = 0;
			for (auto& col : cols)
				allDotMask |= col.mPins;

			const float lineY = (rawLineY - docBounds.top) * kUnitsPerMM;

			for (int i = 0; i < 4; ++i) {
				const int subMaskShift = i * 8;
				if (!((allDotMask >> subMaskShift) & 0xFF))
					continue;

				fprintf(f, "<g transform=\"translate(0,%d)\">\n",
					(int)roundf(lineY + dotDY * 8 * i));

				for (auto& col : cols) {
					const int dotX = (int)roundf((col.mX - docBounds.left) * kUnitsPerMM);
					const uint8 subMask = (uint8)(col.mPins >> subMaskShift);
					if (subMask) {
						dotMasksUsed[subMask >> 5] |= UINT32_C(1) << (subMask & 31);
						fprintf(f, "<use x=\"%d\" l:href=\"#m%02X\"/>\n", dotX, subMask);
					}
				}

				fprintf(f, "</g>\n");
			}
		}
	}

	// Vectors
	vdfastvector<ATPrinterGraphicalOutput::RenderVector> rvectors;
	output.ExtractVectors(rvectors, docBounds);

	if (!rvectors.empty()) {
		// Group by color — just use black for simplicity matching most printer output
		fprintf(f, "<g style=\"stroke:#000000; stroke-width:%d; stroke-linecap:round; fill:none\">\n",
			dotRadius * 2);
		for (const auto& rv : rvectors) {
			fprintf(f, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\"/>\n",
				(int)roundf((rv.mX1 - docBounds.left) * kUnitsPerMM),
				(int)roundf((rv.mY1 - docBounds.top) * kUnitsPerMM),
				(int)roundf((rv.mX2 - docBounds.left) * kUnitsPerMM),
				(int)roundf((rv.mY2 - docBounds.top) * kUnitsPerMM));
		}
		fprintf(f, "</g>\n");
	}

	// Defs for dot patterns
	if (std::any_of(std::begin(dotMasksUsed), std::end(dotMasksUsed),
			[](uint32 v) { return v != 0; })) {
		fprintf(f, "<defs>\n");
		for (int i = 1; i < 256; ++i) {
			if (dotMasksUsed[i >> 5] & (UINT32_C(1) << (i & 31))) {
				fprintf(f, "<path id=\"m%02X\" fill=\"black\" stroke=\"none\" d=\"", i);
				uint32 mask = (uint32)i;
				bool first = true;
				while (mask) {
					int dotIndex = 0;
					uint32 tmp = mask;
					// Find lowest set bit index
					while (!(tmp & 1)) { dotIndex++; tmp >>= 1; }
					mask &= mask - 1;

					if (!first)
						fprintf(f, " ");
					first = false;
					fprintf(f, "M0,%d a%d,%d 0 0 0 0,%d a%d,%d 0 0 0 0,%d",
						(int)roundf(dotDY * (float)dotIndex),
						dotRadius, dotRadius, 2 * dotRadius,
						dotRadius, dotRadius, -2 * dotRadius);
				}
				fprintf(f, "\"/>\n");
			}
		}
		fprintf(f, "</defs>\n");
	}

	fprintf(f, "</svg>\n");
	fclose(f);
	return true;
}

// File dialog callback data for async save dialogs
struct PrinterSaveRequest {
	enum class Format { PNG96, PNG300, PDF, SVG, BMP };
	Format mFormat;
	ATPrinterGraphicalOutput *mpOutput;
	float mDotRadiusMM;
};

static PrinterSaveRequest s_pendingSaveRequest {};
static std::string s_pendingSavePath;
static bool s_hasPendingSave = false;

static void SDLCALL PrinterSaveFileCallback(void *userdata, const char *const *filelist, int filter) {
	(void)userdata;
	(void)filter;
	if (!filelist || !filelist[0])
		return;

	s_pendingSavePath = filelist[0];
	s_hasPendingSave = true;
}

}  // namespace

// =========================================================================
// Printer output pane
// =========================================================================

class ATImGuiPrinterOutputPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiPrinterOutputPaneImpl();
	~ATImGuiPrinterOutputPaneImpl() override;

	bool Render() override;

private:
	void RefreshOutputList();
	void AttachToTextOutput(int index);
	void AttachToGraphicalOutput(int index);
	void DetachFromOutput();
	void UpdateTextBuffer();

	// Graphical rendering
	void RenderGraphicalOutput();
	void UpdateGraphicalTexture(uint32 w, uint32 h);
	void DestroyGraphicalTexture();
	void *GetGraphicalImTextureID() const;

	// Export
	void RenderToFramebuffer(float dpi, std::vector<uint32> &fb, int &outW, int &outH);
	void ProcessPendingSave();

	// Current output tracking
	int mCurrentOutputIdx = -1;
	ATPrinterOutput *mpCurrentOutput = nullptr;
	ATPrinterGraphicalOutput *mpCurrentGfxOutput = nullptr;
	size_t mLastTextOffset = 0;
	std::string mTextBuffer;		// UTF-8 converted text for ImGui display
	bool mbNeedsScroll = false;

	// Graphical view state
	float mViewCenterX = 0;		// document-space center (mm)
	float mViewCenterY = 0;
	float mZoomClicks = 0;
	float mViewPixelsPerMM = 96.0f / 25.4f;	// ~3.78 px/mm at 96 DPI
	float mViewMMPerPixel = 25.4f / 96.0f;
	float mDotRadiusMM = 0;
	float mPageWidthMM = 0;
	float mPageVBorderMM = 0;
	float mViewCursorY = 0;		// print head Y position (mm)

	bool mbDragging = false;
	float mDragLastX = 0;
	float mDragLastY = 0;

	static constexpr float kZoomMin = -10.0f;
	static constexpr float kZoomMax = 25.0f;

	// Texture for graphical output
	GLuint mGLTexture = 0;
	SDL_Texture *mpSDLTexture = nullptr;
	int mTexW = 0;
	int mTexH = 0;
	std::vector<uint32> mFramebuffer;

	// Invalidation tracking
	bool mbGfxInvalidated = true;
	vdfunction<void()> mGfxOnInvalidation;

	// Invalidation callback
	vdfunction<void()> mOnInvalidation;

	// Output list (rebuilt on open and on events)
	struct OutputInfo {
		VDStringA mName;
		bool mbIsGraphical;
		int mIndex;				// index into text or graphical output list
	};
	std::vector<OutputInfo> mOutputList;
	bool mbOutputListDirty = true;

	// Event subscriptions
	vdfunction<void(ATPrinterOutput&)> mOnAddedOutput;
	vdfunction<void(ATPrinterOutput&)> mOnRemovingOutput;
};

ATImGuiPrinterOutputPaneImpl::ATImGuiPrinterOutputPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_PrinterOutput, "Printer Output")
{
	// Subscribe to printer output manager events
	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());

	mOnAddedOutput = [this](ATPrinterOutput&) {
		mbOutputListDirty = true;
	};
	mOnRemovingOutput = [this](ATPrinterOutput& output) {
		if (mpCurrentOutput == &output)
			DetachFromOutput();
		mbOutputListDirty = true;
	};

	mgr.OnAddedOutput.Add(&mOnAddedOutput);
	mgr.OnRemovingOutput.Add(&mOnRemovingOutput);

	// Auto-attach to first available output
	RefreshOutputList();
	if (!mOutputList.empty()) {
		for (int i = 0; i < (int)mOutputList.size(); ++i) {
			if (!mOutputList[i].mbIsGraphical) {
				AttachToTextOutput(mOutputList[i].mIndex);
				mCurrentOutputIdx = i;
				break;
			}
		}
	}
}

ATImGuiPrinterOutputPaneImpl::~ATImGuiPrinterOutputPaneImpl() {
	DetachFromOutput();
	DestroyGraphicalTexture();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	mgr.OnAddedOutput.Remove(&mOnAddedOutput);
	mgr.OnRemovingOutput.Remove(&mOnRemovingOutput);
}

void ATImGuiPrinterOutputPaneImpl::RefreshOutputList() {
	mbOutputListDirty = false;
	mOutputList.clear();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());

	uint32 textCount = mgr.GetOutputCount();
	for (uint32 i = 0; i < textCount; ++i) {
		ATPrinterOutput& out = mgr.GetOutput(i);
		OutputInfo info;
		info.mName = VDTextWToU8(VDStringW(out.GetName()));
		info.mbIsGraphical = false;
		info.mIndex = (int)i;
		mOutputList.push_back(std::move(info));
	}

	uint32 gfxCount = mgr.GetGraphicalOutputCount();
	for (uint32 i = 0; i < gfxCount; ++i) {
		ATPrinterGraphicalOutput& out = mgr.GetGraphicalOutput(i);
		OutputInfo info;
		info.mName = VDTextWToU8(VDStringW(out.GetName()));
		info.mName += " (graphical)";
		info.mbIsGraphical = true;
		info.mIndex = (int)i;
		mOutputList.push_back(std::move(info));
	}
}

void ATImGuiPrinterOutputPaneImpl::AttachToTextOutput(int index) {
	DetachFromOutput();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	if (index < 0 || index >= (int)mgr.GetOutputCount())
		return;

	mpCurrentOutput = &mgr.GetOutput(index);
	mLastTextOffset = 0;
	mTextBuffer.clear();

	// Set up invalidation callback
	mOnInvalidation = [this]() {
		// Text was added — will be picked up next frame
	};
	mpCurrentOutput->SetOnInvalidation(mOnInvalidation);

	UpdateTextBuffer();
}

void ATImGuiPrinterOutputPaneImpl::AttachToGraphicalOutput(int index) {
	DetachFromOutput();

	auto& mgr = static_cast<ATPrinterOutputManager&>(g_sim.GetPrinterOutputManager());
	if (index < 0 || index >= (int)mgr.GetGraphicalOutputCount())
		return;

	mpCurrentGfxOutput = &mgr.GetGraphicalOutput(index);

	const ATPrinterGraphicsSpec& spec = mpCurrentGfxOutput->GetGraphicsSpec();
	mPageWidthMM = spec.mPageWidthMM;
	mPageVBorderMM = spec.mPageVBorderMM;
	mDotRadiusMM = spec.mDotRadiusMM;

	// Set up invalidation callback
	mGfxOnInvalidation = [this]() {
		mbGfxInvalidated = true;
	};
	mpCurrentGfxOutput->SetOnInvalidation(mGfxOnInvalidation);

	// Track print head position
	mpCurrentGfxOutput->SetOnVerticalMove([this](float y) {
		mViewCursorY = y;
	});

	// Reset view
	mZoomClicks = 0;
	mViewPixelsPerMM = 96.0f / 25.4f;
	mViewMMPerPixel = 25.4f / 96.0f;
	mViewCenterX = mPageWidthMM * 0.5f;
	mViewCenterY = mPageVBorderMM;
	mViewCursorY = (float)mpCurrentGfxOutput->GetVerticalPos();
	mbGfxInvalidated = true;
}

void ATImGuiPrinterOutputPaneImpl::DetachFromOutput() {
	if (mpCurrentOutput) {
		mpCurrentOutput->SetOnInvalidation(vdfunction<void()>());
		mpCurrentOutput = nullptr;
	}
	if (mpCurrentGfxOutput) {
		mpCurrentGfxOutput->SetOnInvalidation(vdfunction<void()>());
		mpCurrentGfxOutput->SetOnVerticalMove(vdfunction<void(float)>());
		mpCurrentGfxOutput = nullptr;
	}
	mCurrentOutputIdx = -1;
}

void ATImGuiPrinterOutputPaneImpl::UpdateTextBuffer() {
	if (!mpCurrentOutput)
		return;

	size_t len = mpCurrentOutput->GetLength();
	if (len > mLastTextOffset) {
		const wchar_t *text = mpCurrentOutput->GetTextPointer(mLastTextOffset);
		size_t newChars = len - mLastTextOffset;

		// Convert wchar_t to UTF-8 for ImGui
		VDStringW wstr(text, newChars);
		VDStringA utf8 = VDTextWToU8(wstr);
		mTextBuffer.append(utf8.c_str(), utf8.size());

		mLastTextOffset = len;
		mbNeedsScroll = true;
	}

	mpCurrentOutput->Revalidate();
}

void ATImGuiPrinterOutputPaneImpl::DestroyGraphicalTexture() {
	if (mGLTexture) {
		glDeleteTextures(1, &mGLTexture);
		mGLTexture = 0;
	}
	if (mpSDLTexture) {
		SDL_DestroyTexture(mpSDLTexture);
		mpSDLTexture = nullptr;
	}
	mTexW = 0;
	mTexH = 0;
}

void *ATImGuiPrinterOutputPaneImpl::GetGraphicalImTextureID() const {
	IDisplayBackend *backend = ATUIGetDisplayBackend();
	if (backend && backend->GetType() == DisplayBackendType::OpenGL33)
		return (void *)(intptr_t)mGLTexture;
	return (void *)(intptr_t)mpSDLTexture;
}

void ATImGuiPrinterOutputPaneImpl::UpdateGraphicalTexture(uint32 w, uint32 h) {
	if (w == 0 || h == 0)
		return;

	// Render to framebuffer
	mFramebuffer.resize(w * h);

	PrinterViewTransform vt;
	vt.mOriginX = mViewCenterX - (float)w * 0.5f * mViewMMPerPixel;
	vt.mOriginY = mViewCenterY - (float)h * 0.5f * mViewMMPerPixel;
	vt.mPixelsPerMM = mViewPixelsPerMM;
	vt.mMMPerPixel = mViewMMPerPixel;

	RenderPrinterOutput(*mpCurrentGfxOutput, vt, mFramebuffer.data(), w, h, mDotRadiusMM);

	// Draw print head cursor — gray triangle on left edge
	{
		float cursorPixelY = (mViewCursorY - vt.mOriginY) * vt.mPixelsPerMM;
		int cy = (int)cursorPixelY;
		if (cy >= -10 && cy < (int)h + 10) {
			for (int dy = -5; dy <= 5; ++dy) {
				int py = cy + dy;
				if (py < 0 || py >= (int)h) continue;
				int triWidth = 6 - abs(dy);
				for (int dx = 0; dx < triWidth && dx < (int)w; ++dx)
					mFramebuffer[py * w + dx] = 0xFF808080;
			}
		}
	}

	// Upload to texture
	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL33;

	if (useGL) {
		if (!mGLTexture || mTexW != (int)w || mTexH != (int)h) {
			if (mGLTexture)
				glDeleteTextures(1, &mGLTexture);

			mTexW = (int)w;
			mTexH = (int)h;
			mGLTexture = GLCreateTexture2D(mTexW, mTexH,
				GL_RGBA8, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
				mFramebuffer.data(), true);
		} else {
			glBindTexture(GL_TEXTURE_2D, mGLTexture);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mTexW, mTexH,
				GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, mFramebuffer.data());
		}
	} else {
		if (!mpSDLTexture || mTexW != (int)w || mTexH != (int)h) {
			if (mpSDLTexture)
				SDL_DestroyTexture(mpSDLTexture);

			SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
			if (!renderer) return;

			mTexW = (int)w;
			mTexH = (int)h;
			mpSDLTexture = SDL_CreateTexture(renderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				mTexW, mTexH);
			if (!mpSDLTexture) return;
		}

		void *pixels = nullptr;
		int pitch = 0;
		if (SDL_LockTexture(mpSDLTexture, nullptr, &pixels, &pitch)) {
			for (uint32 y = 0; y < h; ++y)
				memcpy((uint8 *)pixels + y * pitch, &mFramebuffer[y * w], w * 4);
			SDL_UnlockTexture(mpSDLTexture);
		}
	}

	mbGfxInvalidated = false;
}

void ATImGuiPrinterOutputPaneImpl::RenderToFramebuffer(float dpi, std::vector<uint32> &fb, int &outW, int &outH) {
	if (!mpCurrentGfxOutput) {
		outW = outH = 0;
		return;
	}

	vdrect32f docBounds = mpCurrentGfxOutput->GetDocumentBounds();

	// Ensure minimum size
	if (docBounds.width() < 1.0f) docBounds.right = docBounds.left + 10.0f;
	if (docBounds.height() < 1.0f) docBounds.bottom = docBounds.top + 10.0f;

	const float mmToInches = 1.0f / 25.4f;
	outW = std::max(1, (int)ceilf(docBounds.width() * mmToInches * dpi));
	outH = std::max(1, (int)ceilf(docBounds.height() * mmToInches * dpi));

	// Limit to reasonable size
	if ((int64_t)outW * outH > 64 * 1024 * 1024) {
		float scale = sqrtf(64.0f * 1024 * 1024 / ((float)outW * outH));
		outW = (int)(outW * scale);
		outH = (int)(outH * scale);
	}

	fb.resize(outW * outH);

	PrinterViewTransform vt;
	vt.mOriginX = docBounds.left;
	vt.mOriginY = docBounds.top;
	vt.mPixelsPerMM = mmToInches * dpi;
	vt.mMMPerPixel = 1.0f / vt.mPixelsPerMM;

	RenderPrinterOutput(*mpCurrentGfxOutput, vt, fb.data(), outW, outH, mDotRadiusMM);
}

void ATImGuiPrinterOutputPaneImpl::ProcessPendingSave() {
	if (!s_hasPendingSave || !mpCurrentGfxOutput)
		return;

	s_hasPendingSave = false;
	const std::string path = s_pendingSavePath;

	switch (s_pendingSaveRequest.mFormat) {
		case PrinterSaveRequest::Format::PNG96: {
			std::vector<uint32> fb;
			int w, h;
			RenderToFramebuffer(96.0f, fb, w, h);
			if (w > 0 && h > 0)
				SaveFramebufferAsPNG(fb.data(), w, h, path.c_str());
			break;
		}
		case PrinterSaveRequest::Format::PNG300: {
			std::vector<uint32> fb;
			int w, h;
			RenderToFramebuffer(300.0f, fb, w, h);
			if (w > 0 && h > 0)
				SaveFramebufferAsPNG(fb.data(), w, h, path.c_str());
			break;
		}
		case PrinterSaveRequest::Format::PDF: {
			std::vector<uint32> fb;
			int w, h;
			RenderToFramebuffer(300.0f, fb, w, h);
			if (w > 0 && h > 0)
				SaveFramebufferAsPDF(fb.data(), w, h, path.c_str());
			break;
		}
		case PrinterSaveRequest::Format::SVG:
			SavePrinterOutputAsSVG(*mpCurrentGfxOutput, path.c_str());
			break;
		default:
			break;
	}
}

void ATImGuiPrinterOutputPaneImpl::RenderGraphicalOutput() {
	if (!mpCurrentGfxOutput)
		return;

	// Process any pending save requests
	ProcessPendingSave();

	// Context menu
	if (ImGui::BeginPopupContextWindow("PrinterGfxContext")) {
		if (ImGui::MenuItem("Clear")) {
			mpCurrentGfxOutput->Clear();
			mViewCenterY = 0;
			mbGfxInvalidated = true;
		}

		if (ImGui::BeginMenu("Save As")) {
			auto startSave = [&](PrinterSaveRequest::Format fmt, const char *filterName, const char *ext) {
				s_pendingSaveRequest.mFormat = fmt;
				s_pendingSaveRequest.mpOutput = mpCurrentGfxOutput;
				s_pendingSaveRequest.mDotRadiusMM = mDotRadiusMM;

				SDL_DialogFileFilter filter;
				filter.name = filterName;
				filter.pattern = ext;
				SDL_ShowSaveFileDialog(PrinterSaveFileCallback, nullptr,
					g_pWindow, &filter, 1, nullptr);
			};

			if (ImGui::MenuItem("PNG Image (96 DPI)"))
				startSave(PrinterSaveRequest::Format::PNG96, "PNG Image (*.png)", "png");
			if (ImGui::MenuItem("PNG Image (300 DPI)"))
				startSave(PrinterSaveRequest::Format::PNG300, "PNG Image (*.png)", "png");
			if (ImGui::MenuItem("PDF Document"))
				startSave(PrinterSaveRequest::Format::PDF, "PDF Document (*.pdf)", "pdf");
			if (ImGui::MenuItem("SVG 1.1 Document"))
				startSave(PrinterSaveRequest::Format::SVG, "SVG Document (*.svg)", "svg");

			ImGui::EndMenu();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Reset View")) {
			mZoomClicks = 0;
			mViewPixelsPerMM = 96.0f / 25.4f;
			mViewMMPerPixel = 25.4f / 96.0f;
			mViewCenterX = mPageWidthMM * 0.5f;
			mViewCenterY = mPageVBorderMM;
			mbGfxInvalidated = true;
		}

		if (ImGui::MenuItem("Set Print Position")) {
			// Jump view to current print head position
			mViewCenterY = mViewCursorY;
			mbGfxInvalidated = true;
		}

		ImGui::EndPopup();
	}

	// Get available region
	ImVec2 avail = ImGui::GetContentRegionAvail();
	int viewW = std::max(1, (int)avail.x);
	int viewH = std::max(1, (int)avail.y);

	// Handle zoom (mouse wheel)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
		float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			float newZoom = std::clamp(mZoomClicks + wheel, kZoomMin, kZoomMax);
			if (newZoom != mZoomClicks) {
				// Zoom centered on mouse cursor position
				ImVec2 mousePos = ImGui::GetMousePos();
				ImVec2 windowPos = ImGui::GetCursorScreenPos();
				float relX = mousePos.x - windowPos.x;
				float relY = mousePos.y - windowPos.y;

				// Convert mouse position to document space before zoom
				float docMouseX = mViewCenterX + (relX - viewW * 0.5f) * mViewMMPerPixel;
				float docMouseY = mViewCenterY + (relY - viewH * 0.5f) * mViewMMPerPixel;

				mZoomClicks = newZoom;
				float basePixelsPerMM = 96.0f / 25.4f;
				mViewPixelsPerMM = basePixelsPerMM * powf(2.0f, mZoomClicks / 5.0f);
				mViewMMPerPixel = 1.0f / mViewPixelsPerMM;

				// Adjust center so the point under the mouse stays put
				mViewCenterX = docMouseX - (relX - viewW * 0.5f) * mViewMMPerPixel;
				mViewCenterY = docMouseY - (relY - viewH * 0.5f) * mViewMMPerPixel;

				mbGfxInvalidated = true;
			}
		}
	}

	// Handle pan (mouse drag)
	if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGui::GetIO().KeyCtrl) {
		mbDragging = true;
		mDragLastX = ImGui::GetMousePos().x;
		mDragLastY = ImGui::GetMousePos().y;
	}

	if (mbDragging) {
		if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			float dx = ImGui::GetMousePos().x - mDragLastX;
			float dy = ImGui::GetMousePos().y - mDragLastY;
			if (dx != 0.0f || dy != 0.0f) {
				mViewCenterX -= dx * mViewMMPerPixel;
				mViewCenterY -= dy * mViewMMPerPixel;
				mDragLastX = ImGui::GetMousePos().x;
				mDragLastY = ImGui::GetMousePos().y;
				mbGfxInvalidated = true;
			}
		} else {
			mbDragging = false;
		}
	}

	// Keyboard navigation
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput) {
		float scrollStep = ImGui::GetIO().KeyCtrl ? 1.0f : 100.0f;

		if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
			mViewCenterX -= scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
			mViewCenterX += scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
			mViewCenterY -= scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
			mViewCenterY += scrollStep * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
			mViewCenterY -= (float)viewH * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
			mViewCenterY += (float)viewH * mViewMMPerPixel;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_KeypadAdd) ||
			(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Equal))) {
			mZoomClicks = std::min(mZoomClicks + 1.0f, kZoomMax);
			mViewPixelsPerMM = (96.0f / 25.4f) * powf(2.0f, mZoomClicks / 5.0f);
			mViewMMPerPixel = 1.0f / mViewPixelsPerMM;
			mbGfxInvalidated = true;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract) ||
			(ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Minus))) {
			mZoomClicks = std::max(mZoomClicks - 1.0f, kZoomMin);
			mViewPixelsPerMM = (96.0f / 25.4f) * powf(2.0f, mZoomClicks / 5.0f);
			mViewMMPerPixel = 1.0f / mViewPixelsPerMM;
			mbGfxInvalidated = true;
		}
	}

	// Render and display
	if (mbGfxInvalidated)
		UpdateGraphicalTexture(viewW, viewH);

	void *texID = GetGraphicalImTextureID();
	if (texID) {
		ImVec2 uv1(0, 0);
		ImVec2 uv2((float)viewW / (float)mTexW, (float)viewH / (float)mTexH);
		ImGui::Image((ImTextureID)texID, ImVec2((float)viewW, (float)viewH), uv1, uv2);
	}
}

bool ATImGuiPrinterOutputPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (mbOutputListDirty)
		RefreshOutputList();

	// Toolbar: output selector + clear button
	{
		// Output selector dropdown
		const char *currentName = (mCurrentOutputIdx >= 0 && mCurrentOutputIdx < (int)mOutputList.size())
			? mOutputList[mCurrentOutputIdx].mName.c_str()
			: "(none)";

		ImGui::SetNextItemWidth(200);
		if (ImGui::BeginCombo("##output", currentName)) {
			for (int i = 0; i < (int)mOutputList.size(); ++i) {
				bool selected = (i == mCurrentOutputIdx);
				if (ImGui::Selectable(mOutputList[i].mName.c_str(), selected)) {
					if (i != mCurrentOutputIdx) {
						if (!mOutputList[i].mbIsGraphical) {
							AttachToTextOutput(mOutputList[i].mIndex);
							mCurrentOutputIdx = i;
						} else {
							AttachToGraphicalOutput(mOutputList[i].mIndex);
							mCurrentOutputIdx = i;
						}
					}
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			if (mpCurrentOutput) {
				mpCurrentOutput->Clear();
				mTextBuffer.clear();
				mLastTextOffset = 0;
			}
			if (mpCurrentGfxOutput) {
				mpCurrentGfxOutput->Clear();
				mViewCenterY = 0;
				mbGfxInvalidated = true;
			}
		}
	}

	ImGui::Separator();

	// Check if current selection is graphical
	bool isGraphical = (mCurrentOutputIdx >= 0 && mCurrentOutputIdx < (int)mOutputList.size()
		&& mOutputList[mCurrentOutputIdx].mbIsGraphical);

	if (isGraphical) {
		RenderGraphicalOutput();
	} else if (mpCurrentOutput) {
		// Update text buffer from printer output
		UpdateTextBuffer();

		// Text output area
		if (ImGui::BeginChild("PrinterText", ImVec2(0, 0), ImGuiChildFlags_None,
				ImGuiWindowFlags_HorizontalScrollbar)) {
			if (!mTextBuffer.empty())
				ImGui::TextUnformatted(mTextBuffer.c_str(), mTextBuffer.c_str() + mTextBuffer.size());

			if (mbNeedsScroll) {
				ImGui::SetScrollHereY(1.0f);
				mbNeedsScroll = false;
			}
		}
		ImGui::EndChild();
	} else {
		ImGui::TextDisabled("(no printer output available)");
	}

	// Escape → focus Console (matches Windows pattern)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		ATUIDebuggerFocusConsole();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsurePrinterOutputPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_PrinterOutput)) {
		auto *pane = new ATImGuiPrinterOutputPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}
