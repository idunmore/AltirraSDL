//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <intrin.h>
#include <vd2/system/binary.h>
#include <vd2/system/color.h>
#include <vd2/system/constexpr.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/vecmath.h>
#include "printerrasterizer.h"

ATPrinterRasterizer::ATPrinterRasterizer() {
	for(int i=0; i<256; ++i) {
		uint8 v = i;

		v -= (v >> 1) & 0x55;
		v = (v & 0x33) + ((v & 0xCC) >> 2);

		mPopCnt8[i] = (v & 0x0F) + (v >> 4);
	}

	static constexpr float kRange = kWhiteLevel - kBlackLevel;
	for(int i=0; i<=64; ++i) {
		float luma = kWhiteLevel - kRange * (float)i / 64.0f;
		uint32 v = VDColorRGB(vdfloat32x3::set(luma, luma, luma)).LinearToSRGB().ToRGB8();

		mGammaTable[i] = v;
	}

	// Threshold table for 8x8 dither.
	static constexpr uint8 kDitherPattern[] {
		64,  1, 57, 37, 43, 44, 22, 11,
		60, 30, 15, 62, 31, 54, 27, 52,
		26, 13, 63, 38, 19, 48, 24, 12,
		 6,  3, 56, 28, 14,  7, 58, 29,
		55, 34, 17, 49, 33, 41, 45, 47,
		46, 23, 50, 25, 53, 35, 40, 20,
		10,  5, 59, 36, 18,  9, 61, 39,
		42, 21, 51, 32, 16,  8,  4,  2
	};

	static_assert(vdcountof(kDitherPattern) == 64);

	// Convert the threshold table to an order table, and also transpose the
	// dither matrix if we're using NEON.
	//
	// Scalar and SSE2 use book order, where each byte corresponds to a subrow.
	// NEON is more complex and instead uses the order:
	//
	//  63               48 47               32 31               16 15                0
	// |  7 : 3  |  7 : 3  |  6 : 2  |  6 : 2  |  5 : 1  |  5 : 1  |  4 : 0  |  4 : 0  |   x-position
	// |7531 7531|6420 6420|7531 7531|6420 6420|7531 7531|6420 6420|7531 7531|6420 6420|   y-subrow

	static constexpr auto kDitherOrder = [] {
		VDCxArray<uint8, 64> r;

		for(unsigned i=0; i<64; ++i) {
			unsigned j = kDitherPattern[i];

#ifdef VD_CPU_ARM64
			// ARM64 uses a transposed order compared to x64.
			unsigned x = i & 7;
			unsigned y = i >> 3;
			unsigned bitPos = (y >> 1) + (y & 1 ? 8 : 0) + (x & 3)*16 + (x & 4);

			if (bitPos >= 64)
				throw;

			r[j - 1] = bitPos;
#else
			r[j - 1] = i;
#endif
		}

		return r;
	}();

	memset(mLevelDithers, 0xFF, sizeof mLevelDithers);

	for(int ch=0; ch<3; ++ch) {
		uint32 offset = 7 * ch;
		uint64 dither = 0;

		for(int level = 0; level < 64; ++level) {
			dither |= UINT64_C(1) << kDitherOrder[(level + offset) & 63];

			VDWriteUnalignedLEU64(mLevelDithers[level + 1][ch], ~dither);
		}
	}
}

ATPrinterRasterizer::~ATPrinterRasterizer() {
}

void ATPrinterRasterizer::TrimBuffers() {
	vdfastvector<uint32> fb;
	fb.swap(mFrameBuffer);

	vdfastvector<uint8> ab;
	ab.swap(mABuffer);
}

std::optional<VDPixmap> ATPrinterRasterizer::Render(ATPrinterGraphicalOutput *output, const ViewTransform& viewTransform, sint32 x, sint32 y, uint32 w, uint32 h, bool force) {
	// compute pixel bounding box
	const sint32 viewix1 = x;
	const sint32 viewiy1 = y;
	const sint32 viewix2 = x + w;
	const sint32 viewiy2 = y + h;
	const float viewfx1 = (float)viewix1;
	const float viewfy1 = (float)viewiy1;
	const float viewfx2 = (float)viewix2;
	const float viewfy2 = (float)viewiy2;

	// compute document space bounding box
	const float viewDocX1 = viewTransform.mOriginX + viewfx1 * viewTransform.mMMPerPixel;
	const float viewDocY1 = viewTransform.mOriginY + viewfy1 * viewTransform.mMMPerPixel;
	const float viewDocX2 = viewTransform.mOriginX + viewfx2 * viewTransform.mMMPerPixel;
	const float viewDocY2 = viewTransform.mOriginY + viewfy2 * viewTransform.mMMPerPixel;

	// pre-cull line range to entire vertical range
	const bool hasVectors = output && output->HasVectors();

	ATPrinterGraphicalOutput::CullInfo cullInfo;
	float dotRadiusMM = 1.0f;

	if (output) {
		if (!output->PreCull(cullInfo, vdrect32f(viewDocX1, viewDocY1, viewDocX2, viewDocY2)) && !force && !hasVectors)
			return std::nullopt;

		dotRadiusMM = output->GetGraphicsSpec().mDotRadiusMM;
	}

	// clear framebuffer
	mFrameBuffer.resize(w * h);
	std::fill(mFrameBuffer.begin(), mFrameBuffer.end(), 0xFFFFFFFF);

	// initialize antialiasing buffer
	const size_t abufferSize = w * (hasVectors ? 24 : 8);

	if (mABuffer.size() < abufferSize)
		mABuffer.resize(abufferSize);

	// render one scan line at a time
	const float viewSubPixelsPerMM = viewTransform.mPixelsPerMM * 8.0f;
	const float rowCenterToFirstSubRowOffset = viewTransform.mMMPerPixel * (-3.5f / 8.0f);
	const float subRowStep = viewTransform.mMMPerPixel / 8.0f;
	const float pixelsPerSubRow = subRowStep * viewSubPixelsPerMM;
	const float dotRadius = dotRadiusMM;
	const float dotRadiusSq = dotRadius * dotRadius;
	const sint32 subw = w * 8;

	alignas(16) static constexpr float kDither[8] {
		(0.0f - 3.5f) / 8.0f,
		(4.0f - 3.5f) / 8.0f,
		(2.0f - 3.5f) / 8.0f,
		(6.0f - 3.5f) / 8.0f,
		(1.0f - 3.5f) / 8.0f,
		(5.0f - 3.5f) / 8.0f,
		(3.0f - 3.5f) / 8.0f,
		(7.0f - 3.5f) / 8.0f,
	};

	vdfloat32x4 subRowYOffsets_a = vdfloat32x4::set(kDither[0] + 0.0f, kDither[1] + 1.0f, kDither[2] + 2.0f, kDither[3] + 3.0f) * subRowStep + rowCenterToFirstSubRowOffset;
	vdfloat32x4 subRowYOffsets_b = vdfloat32x4::set(kDither[4] + 4.0f, kDither[5] + 5.0f, kDither[6] + 6.0f, kDither[7] + 7.0f) * subRowStep + rowCenterToFirstSubRowOffset;

	for(uint32 yoff = 0; yoff < h; ++yoff) {
		const float docRowYC = viewDocY1 + ((float)yoff + 0.5f) * viewTransform.mMMPerPixel;
		const float docRowYD = 0.5f * viewTransform.mMMPerPixel;

		// pre-cull lines to scan line vertical range
		const float docRowY1 = docRowYC - docRowYD;
		const float docRowY2 = docRowYC + docRowYD;

		// pre-cull dots to scan line
		mDotCullBuffer.clear();
		mVectorCullBuffer.clear();

		if (output) {
			const vdrect32f cullRect { viewDocX1 - viewTransform.mMMPerPixel * 0.5f, docRowY1, viewDocX2 + viewTransform.mMMPerPixel * 0.5f, docRowY2 };
			output->ExtractNextLineDots(mDotCullBuffer, cullInfo, cullRect);

			if (hasVectors) {
				vdrect32f dotCullRect {
					cullRect.left - dotRadiusMM,
					cullRect.top - dotRadiusMM,
					cullRect.right + dotRadiusMM,
					cullRect.bottom + dotRadiusMM
				};

				output->ExtractVectors(mVectorCullBuffer, cullRect);

				for (const RenderVector& v : mVectorCullBuffer) {
					if (dotCullRect.contains(vdpoint32f { v.mX1, v.mY1 }))
						mDotCullBuffer.emplace_back(v.mX1, v.mY1, v.mLinearColor);

					if (dotCullRect.contains(vdpoint32f { v.mX2, v.mY2 }))
						mDotCullBuffer.emplace_back(v.mX2, v.mY2, v.mLinearColor);
				}
			}
		}

		// clear the antialiasing buffer
		uint8 *abuf = mABuffer.data();
		memset(abuf, 0, abufferSize);

		// render all dots
		vdfloat32x4 ditherx_a = vdfloat32x4::set(kDither[0], kDither[1], kDither[2], kDither[3]);
		vdfloat32x4 ditherx_b = vdfloat32x4::set(kDither[4], kDither[5], kDither[6], kDither[7]);

		const float docRowYDPlusDotR = docRowYD + dotRadius;
		const float subwf = (float)subw;
		for(const RenderDot& VDRESTRICT dot : mDotCullBuffer) {
			float dy = dot.mY - docRowYC;

			// vertical cull to row
			if (fabsf(dy) >= docRowYDPlusDotR)
				continue;

			// process one subrow at a time
			sint32 subSpans[2][8];

			vdfloat32x4 z = vdfloat32x4::zero();
			vdfloat32x4 vsubwf = vdfloat32x4::set1(subwf);
			vdfloat32x4 dy2_a = dy - subRowYOffsets_a;
			vdfloat32x4 dy2_b = dy - subRowYOffsets_b;
			vdfloat32x4 dx_a = sqrt(max(dotRadiusSq - dy2_a*dy2_a, z)) * viewSubPixelsPerMM;
			vdfloat32x4 dx_b = sqrt(max(dotRadiusSq - dy2_b*dy2_b, z)) * viewSubPixelsPerMM;

			// compute x range in document space
			const float xc = (dot.mX - viewDocX1) * viewSubPixelsPerMM;
			const vdfloat32x4 xc_a = xc + ditherx_a;
			const vdfloat32x4 xc_b = xc + ditherx_b;
			const vdfloat32x4 x1_a = min(max(xc_a - dx_a, z), vsubwf);
			const vdfloat32x4 x1_b = min(max(xc_b - dx_b, z), vsubwf);
			const vdfloat32x4 x2_a = min(max(xc_a + dx_a, z), vsubwf);
			const vdfloat32x4 x2_b = min(max(xc_b + dx_b, z), vsubwf);

			// convert x range to subpixels
			const vdint32x4 ix1_a = ceilint(x1_a);
			const vdint32x4 ix1_b = ceilint(x1_b);
			const vdint32x4 ix2_a = ceilint(x2_a);
			const vdint32x4 ix2_b = ceilint(x2_b);

			// horizontally clip
			storeu(&subSpans[0][0], ix1_a);
			storeu(&subSpans[0][4], ix1_b);
			storeu(&subSpans[1][0], ix2_a);
			storeu(&subSpans[1][4], ix2_b);

			RenderTrapezoid(subSpans, dot.mLinearColor, hasVectors);
		}

		// render all vectors
		for(const RenderVector& v : mVectorCullBuffer) {
			// load vector -- note that this is always pre-sorted top-down so y1 <= y2
			const float x1 = v.mX1;
			const float y1 = v.mY1;
			const float x2 = v.mX2;
			const float y2 = v.mY2;

			// compute perpendicular vector (points left)
			const float dx = x2 - x1;
			const float dy = y2 - y1;
			const float l2 = dx*dx + dy*dy;
			if (l2 < 1e-4f)
				continue;

			const float ps = l2 > 1e-6f ? dotRadiusMM / sqrtf(l2) : 0.0f;
			const float xp = -dy * ps;
			const float yp = dx * ps;

			// init subrow edges
			const vdfloat32x4 vsubwf = vdfloat32x4::set1((float)subw);

			vdfloat32x4 subSpansF[2][2];
			sint32 subSpans[2][8];

			for(int i=0; i<2; ++i) {
				// we need a slight offset here to account for dithering
				subSpansF[0][i] = vdfloat32x4::set1(-1.0f);
				subSpansF[1][i] = vsubwf + vdfloat32x4::set1(1.0f);
			}

			// compute edges in counterclockwise order
			struct Edge {
				float x;
				float y;
				float dx;
				float dy;
			} edges[4] {
				{ x1 + xp, y1 + yp, dx, dy },
				{ x1 - xp, y1 - yp, -dx, -dy },
				{ x2, y2, dy, -dx },
				{ x1, y1, -dy, dx },
			};

			// render descending edges on left and ascending edges on right
			vdfloat32x4 suby_a = docRowYC + subRowYOffsets_a;
			vdfloat32x4 suby_b = docRowYC + subRowYOffsets_b;

			for(const Edge& edge : edges) {
				// check if edge is horizontal
				if (fabsf(edge.dy) < 1e-4f) {
					vdfloat32x4 vedgey = vdfloat32x4::set1(edge.y);

					// check orientation
					if (edge.dx < 0) {
						// edge going left -- inward facing down
						subSpansF[1][0] = min(subSpansF[1][0], nonzeromask(vsubwf, cmpge(suby_a, vedgey)));
						subSpansF[1][1] = min(subSpansF[1][1], nonzeromask(vsubwf, cmpge(suby_b, vedgey)));
					} else {
						// edge going right -- inward facing up
						subSpansF[1][0] = min(subSpansF[1][0], nonzeromask(vsubwf, cmpgt(vedgey, suby_a)));
						subSpansF[1][1] = min(subSpansF[1][1], nonzeromask(vsubwf, cmpgt(vedgey, suby_b)));
					}
				} else {
					const float edgeSlope = edge.dx / edge.dy;
					const float subxinc = edgeSlope * pixelsPerSubRow;
					const float subx0 = ((edge.x - viewDocX1) + (suby_a.x() - edge.y) * edgeSlope) * viewSubPixelsPerMM - 0.5f;

					if (edge.dy > 0) {
						subSpansF[0][0] = max(subSpansF[0][0], subx0 + subxinc*vdfloat32x4::set(0, 1, 2, 3));
						subSpansF[0][1] = max(subSpansF[0][1], subx0 + subxinc*vdfloat32x4::set(4, 5, 6, 7));
					} else {
						subSpansF[1][0] = min(subSpansF[1][0], subx0 + subxinc*vdfloat32x4::set(0, 1, 2, 3));
						subSpansF[1][1] = min(subSpansF[1][1], subx0 + subxinc*vdfloat32x4::set(4, 5, 6, 7));
					}
				}
			}

			// convert subspan edges to integer subpixel coordinates
			storeu(&subSpans[0][0], ceilint(min(max(subSpansF[0][0] + ditherx_a, vdfloat32x4::zero()), vsubwf)));
			storeu(&subSpans[0][4], ceilint(min(max(subSpansF[0][1] + ditherx_b, vdfloat32x4::zero()), vsubwf)));
			storeu(&subSpans[1][0], ceilint(min(max(subSpansF[1][0] + ditherx_a, vdfloat32x4::zero()), vsubwf)));
			storeu(&subSpans[1][4], ceilint(min(max(subSpansF[1][1] + ditherx_b, vdfloat32x4::zero()), vsubwf)));

			RenderTrapezoid(subSpans, v.mLinearColor, true);
		}

		// render antialiasing buffer row to framebuffer
		Downsample8x8(&mFrameBuffer[w * (h - 1 - yoff)], abuf, w, hasVectors);
	}

	VDPixmap px {};
	px.w = w;
	px.h = h;
	px.data = &mFrameBuffer[w * (h - 1)];
	px.pitch = -(ptrdiff_t)w*4;
	px.format = nsVDPixmap::kPixFormat_XRGB8888;
	return px;
}

void ATPrinterRasterizer::RenderTrapezoid(const sint32 subSpans[2][8], uint32 linearColor, bool rgb) {
	uint8 *VDRESTRICT abuf = mABuffer.data();

	if (rgb) {
#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
		if (SSE2_enabled)
			return RenderTrapezoid_SSE2<true>(subSpans, linearColor);

		return RenderTrapezoidRGB_Scalar(subSpans, linearColor);
#elif defined(VD_CPU_ARM64)
		return RenderTrapezoid_NEON<true>(subSpans, linearColor);
#endif

	} else {
#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
		if (SSE2_enabled)
			return RenderTrapezoid_SSE2<false>(subSpans, linearColor);
#elif defined(VD_CPU_ARM64)
		return RenderTrapezoid_NEON<false>(subSpans, linearColor);
#endif

		for(uint32 subRow = 0; subRow < 8; ++subRow) {
			const sint32 csubx1 = subSpans[0][subRow];
			const sint32 csubx2 = subSpans[1][subRow];

			if (csubx1 >= csubx2)
				continue;

			// draw bits
			const uint32 ucsubx1 = (uint32)csubx1;
			const uint32 ucsubx2 = (uint32)csubx2;
			const uint32 maskx1 = ucsubx1 >> 3;
			const uint32 maskx2 = (ucsubx2 - 1) >> 3;
			const uint8 mask1 = 0xFF >> (ucsubx1 & 7);
			const uint8 mask2 = 0xFF << ((8 - ucsubx2) & 7);
			uint8 *__restrict dst = abuf + subRow + maskx1*8;

			if (maskx1 == maskx2) {
				*dst |= mask1 & mask2;
			} else {
				*dst |= mask1;
				dst += 8;

				for(uint32 x = maskx1 + 1; x < maskx2; ++x) {
					*dst = 0xFF;
					dst += 8;
				}

				*dst |= mask2;
			}
		}
	}
}

void ATPrinterRasterizer::RenderTrapezoidRGB_Scalar(const sint32 subSpans[2][8], uint32 linearColor) {
	uint8 *VDRESTRICT abuf = mABuffer.data();
	const uint8 (&VDRESTRICT redDither)[8] = mLevelDithers[(linearColor >> 16)&0xFF][0];
	const uint8 (&VDRESTRICT grnDither)[8] = mLevelDithers[(linearColor >>  8)&0xFF][1];
	const uint8 (&VDRESTRICT bluDither)[8] = mLevelDithers[(linearColor >>  0)&0xFF][2];

	for(uint32 subRow = 0; subRow < 8; ++subRow) {
		const sint32 csubx1 = subSpans[0][subRow];
		const sint32 csubx2 = subSpans[1][subRow];

		if (csubx1 >= csubx2)
			continue;

		const uint8 rdither = redDither[subRow];
		const uint8 gdither = grnDither[subRow];
		const uint8 bdither = bluDither[subRow];

		// draw bits
		const uint32 ucsubx1 = (uint32)csubx1;
		const uint32 ucsubx2 = (uint32)csubx2;
		const uint32 maskx1 = ucsubx1 >> 3;
		const uint32 maskx2 = (ucsubx2 - 1) >> 3;
		const uint8 mask1 = 0xFF >> (ucsubx1 & 7);
		const uint8 mask2 = 0xFF << ((8 - ucsubx2) & 7);
		uint8 *VDRESTRICT dst = abuf + subRow + maskx1*24;

		if (maskx1 == maskx2) {
			const uint8 mask = mask1 & mask2;

			dst[0] |= rdither & mask;
			dst[8] |= gdither & mask;
			dst[16] |= bdither & mask;
		} else {
			dst[0] |= rdither & mask1;
			dst[8] |= gdither & mask1;
			dst[16] |= bdither & mask1;
			dst += 24;

			for(uint32 x = maskx1 + 1; x < maskx2; ++x) {
				dst[0] |= rdither;
				dst[8] |= gdither;
				dst[16] |= bdither;
				dst += 24;
			}

			dst[0] |= rdither & mask2;
			dst[8] |= gdither & mask2;
			dst[16] |= bdither & mask2;
		}
	}
}

#if defined(VD_CPU_X64) || defined(VD_CPU_X86)
template<bool T_RGB>
void ATPrinterRasterizer::RenderTrapezoid_SSE2(const sint32 subSpans[2][8], uint32 linearColor) {
#if defined(VD_CPU_X64)
	[[maybe_unused]] uint64 rdither;
	[[maybe_unused]] uint64 gdither;
	[[maybe_unused]] uint64 bdither;

	if constexpr (T_RGB) {
		rdither = VDReadUnalignedU64(mLevelDithers[(linearColor >> 16)&0xFF][0]);
		gdither = VDReadUnalignedU64(mLevelDithers[(linearColor >>  8)&0xFF][1]);
		bdither = VDReadUnalignedU64(mLevelDithers[(linearColor >>  0)&0xFF][2]);
	}
#else
	[[maybe_unused]] uint32 rdither1;
	[[maybe_unused]] uint32 rdither2;
	[[maybe_unused]] uint32 gdither1;
	[[maybe_unused]] uint32 gdither2;
	[[maybe_unused]] uint32 bdither1;
	[[maybe_unused]] uint32 bdither2;

	if constexpr (T_RGB) {
		rdither1 = VDReadUnalignedU32(&mLevelDithers[(linearColor >> 16)&0xFF][0][0]);
		rdither2 = VDReadUnalignedU32(&mLevelDithers[(linearColor >> 16)&0xFF][0][4]);
		gdither1 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  8)&0xFF][1][0]);
		gdither2 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  8)&0xFF][1][4]);
		bdither1 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  0)&0xFF][2][0]);
		bdither2 = VDReadUnalignedU32(&mLevelDithers[(linearColor >>  0)&0xFF][2][4]);
	}
#endif

	// compute min/max of non-empty scans
	sint32 minSubX1 = 0x7FFFFFFF;
	sint32 maxSubX2 = 0;

	for(int i=0; i<8; ++i) {
		sint32 subx1 = subSpans[0][i];
		sint32 subx2 = subSpans[1][i];

		if (subx1 < subx2) {
			if (minSubX1 > subx1)
				minSubX1 = subx1;

			if (maxSubX2 < subx2)
				maxSubX2 = subx2;
		}
	}

	if (minSubX1 >= maxSubX2)
		return;

	// expand to byte boundaries
	minSubX1 &= ~(sint32)7;
	maxSubX2 = (maxSubX2 + 7) & ~(sint32)7;

	// load subscan ranges
	__m128i subX1A = _mm_loadu_si128((const __m128i *)&subSpans[0][0]);
	__m128i subX1B = _mm_loadu_si128((const __m128i *)&subSpans[0][4]);
	__m128i subX2A = _mm_loadu_si128((const __m128i *)&subSpans[1][0]);
	__m128i subX2B = _mm_loadu_si128((const __m128i *)&subSpans[1][4]);

	// rasterize blocks
	uint8 *__restrict dst = mABuffer.data();
	
	if constexpr (T_RGB) {
		dst += (ptrdiff_t)minSubX1*3;
	} else {
		dst += (ptrdiff_t)minSubX1;
	}

	for(sint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 192) {
		// compute block-relative bounds
		__m128i blockPos = _mm_set1_epi32(blockStart);
		__m128i blockSubX1A = _mm_sub_epi32(subX1A, blockPos);
		__m128i blockSubX1B = _mm_sub_epi32(subX1B, blockPos);
		__m128i blockSubX2A = _mm_sub_epi32(subX2A, blockPos);
		__m128i blockSubX2B = _mm_sub_epi32(subX2B, blockPos);

		// compress to signed bytes with saturation
		__m128i blockSubX1 = _mm_packs_epi32(blockSubX1A, blockSubX1B);
		__m128i blockSubX2 = _mm_packs_epi32(blockSubX2A, blockSubX2B);
		__m128i blockSubX1X2b = _mm_packus_epi16(blockSubX1, blockSubX2);

		// convert to base and width
		__m128i blockSubX1DXb = _mm_subs_epu8(blockSubX1X2b, _mm_slli_si128(blockSubX1X2b, 8));

		// double up bytes and rebias for signed math
		__m128i blockSubX1bb = _mm_unpacklo_epi8(blockSubX1DXb, blockSubX1DXb);
		__m128i blockSubDXbb = _mm_xor_si128(_mm_unpackhi_epi8(blockSubX1DXb, blockSubX1DXb), _mm_set1_epi8(-0x80));

		// set up 64 ranges
		__m128i blockSubX1bbl = _mm_unpacklo_epi16(blockSubX1bb, blockSubX1bb);
		__m128i blockSubX1bbh = _mm_unpackhi_epi16(blockSubX1bb, blockSubX1bb);
		__m128i blockSubDXbbl = _mm_unpacklo_epi16(blockSubDXbb, blockSubDXbb);
		__m128i blockSubDXbbh = _mm_unpackhi_epi16(blockSubDXbb, blockSubDXbb);

		__m128i row01_x1 = _mm_shuffle_epi32(blockSubX1bbl, 0b0'01010000);
		__m128i row01_dx = _mm_shuffle_epi32(blockSubDXbbl, 0b0'01010000);
		__m128i row23_x1 = _mm_shuffle_epi32(blockSubX1bbl, 0b0'11111010);
		__m128i row23_dx = _mm_shuffle_epi32(blockSubDXbbl, 0b0'11111010);
		__m128i row45_x1 = _mm_shuffle_epi32(blockSubX1bbh, 0b0'01010000);
		__m128i row45_dx = _mm_shuffle_epi32(blockSubDXbbh, 0b0'01010000);
		__m128i row67_x1 = _mm_shuffle_epi32(blockSubX1bbh, 0b0'11111010);
		__m128i row67_dx = _mm_shuffle_epi32(blockSubDXbbh, 0b0'11111010);

		// set up bit position counter
		__m128i pos = _mm_set_epi8(-128, -127, -126, -125, -124, -123, -122, -121, -128, -127, -126, -125, -124, -123, -122, -121);
		__m128i posinc = _mm_set1_epi8(8);

 		size_t byteCnt = std::min<size_t>(maxSubX2 - blockStart, 192) >> 3;
		while(byteCnt--) {
			// compute (pos >= x1 && pos < x1+dx) as (dx-128 > (pos - x1)-128) to bit mask
#if defined(VD_CPU_X64)
			const uint64 mask
				=  (uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row01_dx, _mm_sub_epi8(pos, row01_x1)))
				+ ((uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row23_dx, _mm_sub_epi8(pos, row23_x1))) << 16)
				+ ((uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row45_dx, _mm_sub_epi8(pos, row45_x1))) << 32)
				+ ((uint64)_mm_movemask_epi8(_mm_cmpgt_epi8(row67_dx, _mm_sub_epi8(pos, row67_x1))) << 48);

			if constexpr (T_RGB) {
				VDWriteUnalignedU64(dst +  0, VDReadUnalignedU64(dst +  0) | (mask & rdither));
				VDWriteUnalignedU64(dst +  8, VDReadUnalignedU64(dst +  8) | (mask & gdither));
				VDWriteUnalignedU64(dst + 16, VDReadUnalignedU64(dst + 16) | (mask & bdither));
			} else {
				VDWriteUnalignedU64(dst +  0, VDReadUnalignedU64(dst +  0) | mask);
			}
#else
			const uint32 mask1
				=  (uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row01_dx, _mm_sub_epi8(pos, row01_x1)))
				+ ((uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row23_dx, _mm_sub_epi8(pos, row23_x1))) << 16);
			const uint32 mask2
				=  (uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row45_dx, _mm_sub_epi8(pos, row45_x1)))
				+ ((uint32)_mm_movemask_epi8(_mm_cmpgt_epi8(row67_dx, _mm_sub_epi8(pos, row67_x1))) << 16);

			if constexpr (T_RGB) {
				VDWriteUnalignedU32(dst +  0, VDReadUnalignedU32(dst +  0) | (mask1 & rdither1));
				VDWriteUnalignedU32(dst +  4, VDReadUnalignedU32(dst +  4) | (mask2 & rdither2));
				VDWriteUnalignedU32(dst +  8, VDReadUnalignedU32(dst +  8) | (mask1 & gdither1));
				VDWriteUnalignedU32(dst + 12, VDReadUnalignedU32(dst + 12) | (mask2 & gdither2));
				VDWriteUnalignedU32(dst + 16, VDReadUnalignedU32(dst + 16) | (mask1 & bdither1));
				VDWriteUnalignedU32(dst + 20, VDReadUnalignedU32(dst + 20) | (mask2 & bdither2));
			} else {
				VDWriteUnalignedU32(dst +  0, VDReadUnalignedU32(dst +  0) | mask1);
				VDWriteUnalignedU32(dst +  4, VDReadUnalignedU32(dst +  4) | mask2);
			}
#endif

			pos = _mm_adds_epi8(pos, posinc);

			if constexpr (T_RGB) {
				dst += 24;
			} else {
				dst += 8;
			}
		}
	}
}
#endif

#if defined(VD_CPU_ARM64)
template<bool T_RGB>
void ATPrinterRasterizer::RenderTrapezoid_NEON(const sint32 subSpans[2][8], uint32 linearColor) {
	[[maybe_unused]] uint8x8_t rdither;
	[[maybe_unused]] uint8x8_t gdither;
	[[maybe_unused]] uint8x8_t bdither;

	if constexpr (T_RGB) {
		rdither = vld1_u8(mLevelDithers[(linearColor >> 16)&0xFF][0]);
		gdither = vld1_u8(mLevelDithers[(linearColor >>  8)&0xFF][1]);
		bdither = vld1_u8(mLevelDithers[(linearColor >>  0)&0xFF][2]);
	}

	// load subscan ranges
	const uint32x4_t subX1A = vreinterpretq_u32_s32(vld1q_s32(&subSpans[0][0]));
	const uint32x4_t subX1B = vreinterpretq_u32_s32(vld1q_s32(&subSpans[0][4]));
	const uint32x4_t subX2A = vreinterpretq_u32_s32(vld1q_s32(&subSpans[1][0]));
	const uint32x4_t subX2B = vreinterpretq_u32_s32(vld1q_s32(&subSpans[1][4]));

	// compute mask of valid ranges
	const uint32x4_t validMaskA = vcltq_u32(subX1A, subX2A);
	const uint32x4_t validMaskB = vcltq_u32(subX1B, subX2B);

	// compute min/max of non-empty scans
	uint32 minSubX1 = vminvq_u32(vminq_u32(vornq_u32(subX1A, validMaskA), vornq_u32(subX1B, validMaskB)));
	uint32 maxSubX2 = vmaxvq_u32(vmaxq_u32(vandq_u32(subX2A, validMaskA), vandq_u32(subX2B, validMaskB)));

	if (minSubX1 >= maxSubX2)
		return;

	// expand to byte boundaries
	minSubX1 &= ~(uint32)7;
	maxSubX2 = (maxSubX2 + 7) & ~(uint32)7;

	// rasterize blocks
	uint8 *VDRESTRICT dst = mABuffer.data();
	
	if constexpr (T_RGB) {
		dst += (ptrdiff_t)minSubX1*3;
	} else {
		dst += (ptrdiff_t)minSubX1;
	}

	for(uint32 blockStart = minSubX1; blockStart < maxSubX2; blockStart += 192) {
		// compute block-relative bounds
		const uint32x4_t blockPos = vmovq_n_u32(blockStart);
		const uint32x4_t blockSubX1A = vqsubq_u32(subX1A, blockPos);
		const uint32x4_t blockSubX1B = vqsubq_u32(subX1B, blockPos);
		const uint32x4_t blockSubX2A = vqsubq_u32(subX2A, blockPos);
		const uint32x4_t blockSubX2B = vqsubq_u32(subX2B, blockPos);

		// compress to unsigned bytes with saturation
		const uint16x4_t zu16 = vmov_n_u16(0);
		const uint8x8_t blockSubX1 = vqmovn_u16(vcombine_u16(vqmovn_u32(blockSubX1A), vqmovn_u32(blockSubX1B)));
		const uint8x8_t blockSubX2 = vqmovn_u16(vcombine_u16(vqmovn_u32(blockSubX2A), vqmovn_u32(blockSubX2B)));
		const uint8x8_t blockSubDX = vqsub_u8(blockSubX2, blockSubX1);

		// broadcast 8x
		const uint8x16_t row01_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 0));
		const uint8x16_t row23_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 1));
		const uint8x16_t row45_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 2));
		const uint8x16_t row67_x1 = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubX1), 3));
		const uint8x16_t row01_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 0));
		const uint8x16_t row23_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 1));
		const uint8x16_t row45_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 2));
		const uint8x16_t row67_dx = vreinterpretq_u8_u16(vdupq_lane_u16(vreinterpret_u16_u8(blockSubDX), 3));

		// set up bit position counter
		static constexpr uint8_t kInitialBitPos[] { 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7 };
		uint8x16_t pos = vld1q_u8(kInitialBitPos);
		uint8x16_t posinc = vmovq_n_u8(8);

		size_t byteCnt = std::min<size_t>(maxSubX2 - blockStart, 192) >> 3;
		while(byteCnt--) {
			// compute (pos - x1) < dx
			uint8x16_t mask16 =         vcltq_u8(vsubq_u8(pos, row01_x1), row01_dx);
			mask16 = vsliq_n_u8(mask16, vcltq_u8(vsubq_u8(pos, row23_x1), row23_dx), 1);
			mask16 = vsliq_n_u8(mask16, vcltq_u8(vsubq_u8(pos, row45_x1), row45_dx), 2);
			mask16 = vsliq_n_u8(mask16, vcltq_u8(vsubq_u8(pos, row67_x1), row67_dx), 3);

			uint8x8_t mask = vsli_n_u8(vget_low_u8(mask16), vget_high_u8(mask16), 4);

			if constexpr (T_RGB) {
				vst1_u8(dst +  0, vorr_u8(vld1_u8(dst +  0), vand_u8(mask, rdither)));
				vst1_u8(dst +  8, vorr_u8(vld1_u8(dst +  8), vand_u8(mask, gdither)));
				vst1_u8(dst + 16, vorr_u8(vld1_u8(dst + 16), vand_u8(mask, bdither)));
				dst += 24;
			} else {
				vst1_u8(dst, vorr_u8(vld1_u8(dst), mask));
				dst += 8;
			}

			pos = vqaddq_u8(pos, posinc);
		}
	}
}
#endif

void ATPrinterRasterizer::Downsample8x8(uint32 *dst, const uint8 *src, size_t w, bool rgb) {
	if (rgb) {
#ifdef VD_CPU_X64
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_POPCNT64(dst, src, w);
#endif

#ifdef VD_CPU_X86
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return DownsampleRGB8x8_POPCNT32(dst, src, w);
#endif

#ifdef VD_CPU_ARM64
		return DownsampleRGB8x8_NEON(dst, src, w);
#else
		DownsampleRGB8x8_Scalar(dst, src, w);
#endif
	} else {
#ifdef VD_CPU_X64
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return Downsample8x8_POPCNT64(dst, src, w);
#endif

#ifdef VD_CPU_X86
		if (VDCheckAllExtensionsEnabled(VDCPUF_SUPPORTS_POPCNT))
			return Downsample8x8_POPCNT32(dst, src, w);
#endif

#ifdef VD_CPU_ARM64
		return Downsample8x8_NEON(dst, src, w);
#else
		Downsample8x8_Scalar(dst, src, w);
#endif
	}
}

void ATPrinterRasterizer::Downsample8x8_Scalar(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 level
			= mPopCnt8[asrc[0]]
			+ mPopCnt8[asrc[1]]
			+ mPopCnt8[asrc[2]]
			+ mPopCnt8[asrc[3]]
			+ mPopCnt8[asrc[4]]
			+ mPopCnt8[asrc[5]]
			+ mPopCnt8[asrc[6]]
			+ mPopCnt8[asrc[7]];
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}

#ifdef VD_CPU_X64
VD_CPU_TARGET("popcnt")
void ATPrinterRasterizer::Downsample8x8_POPCNT64(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t level = _mm_popcnt_u64(VDReadUnalignedU64(asrc));
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

#ifdef VD_CPU_X86
void ATPrinterRasterizer::Downsample8x8_POPCNT32(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t level = _mm_popcnt_u32(VDReadUnalignedU32(asrc))
			+ _mm_popcnt_u32(VDReadUnalignedU32(asrc + 4));
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = mGammaTable[level];
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATPrinterRasterizer::Downsample8x8_NEON(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;
	const auto *__restrict gtab = mGammaTable;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 level = vaddv_u8(vcnt_u8(vld1_u8(asrc)));
		asrc += 8;

		// convert coverage to sRGB color
		*fbdst++ = gtab[level];
	}
}
#endif

void ATPrinterRasterizer::DownsampleRGB8x8_Scalar(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 rlevel
			= mPopCnt8[asrc[0]]
			+ mPopCnt8[asrc[1]]
			+ mPopCnt8[asrc[2]]
			+ mPopCnt8[asrc[3]]
			+ mPopCnt8[asrc[4]]
			+ mPopCnt8[asrc[5]]
			+ mPopCnt8[asrc[6]]
			+ mPopCnt8[asrc[7]];

		const uint32 glevel
			= mPopCnt8[asrc[8]]
			+ mPopCnt8[asrc[9]]
			+ mPopCnt8[asrc[10]]
			+ mPopCnt8[asrc[11]]
			+ mPopCnt8[asrc[12]]
			+ mPopCnt8[asrc[13]]
			+ mPopCnt8[asrc[14]]
			+ mPopCnt8[asrc[15]];

		const uint32 blevel
			= mPopCnt8[asrc[16]]
			+ mPopCnt8[asrc[17]]
			+ mPopCnt8[asrc[18]]
			+ mPopCnt8[asrc[19]]
			+ mPopCnt8[asrc[20]]
			+ mPopCnt8[asrc[21]]
			+ mPopCnt8[asrc[22]]
			+ mPopCnt8[asrc[23]];

		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}

#ifdef VD_CPU_X64
VD_CPU_TARGET("popcnt")
void ATPrinterRasterizer::DownsampleRGB8x8_POPCNT64(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc));
		const size_t glevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc+8));
		const size_t blevel = _mm_popcnt_u64(VDReadUnalignedU64(asrc+16));
		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif

#ifdef VD_CPU_X86
void ATPrinterRasterizer::DownsampleRGB8x8_POPCNT32(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const size_t rlevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+4));
		const size_t glevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc+8))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+12));
		const size_t blevel = _mm_popcnt_u32(VDReadUnalignedU32(asrc+16))
							+ _mm_popcnt_u32(VDReadUnalignedU32(asrc+20));
		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif

#ifdef VD_CPU_ARM64
void ATPrinterRasterizer::DownsampleRGB8x8_NEON(uint32 *dst, const uint8 *src, size_t w) {
	uint32 *__restrict fbdst = dst;
	const uint8 *__restrict asrc = src;

	for(size_t x = 0; x < w; ++x) {
		// compute coverage by counting bits within 8x8 window
		const uint32 rlevel = vaddv_u8(vcnt_u8(vld1_u8(asrc)));
		const uint32 glevel = vaddv_u8(vcnt_u8(vld1_u8(asrc+8)));
		const uint32 blevel = vaddv_u8(vcnt_u8(vld1_u8(asrc+16)));
		asrc += 24;

		// convert coverage to sRGB color
		*fbdst++
			= (mGammaTable[rlevel] & 0xFF0000)
			+ (mGammaTable[glevel] & 0x00FF00)
			+ (mGammaTable[blevel] & 0x0000FF)
			;
	}
}
#endif
