//	AltirraSDL - Help → Contents block-stream renderer + data accessor.
//
//	The baked help blob is a gzip-wrapped binary image of the entire help
//	corpus.  We decompress it once on first use and walk the topic-block
//	stream each frame to render onto the help window's content pane.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vector>
#include <cstring>
#include <cctype>
#include <algorithm>

#include <vd2/system/file.h>
#include <vd2/system/zip.h>

#ifndef ALTIRRA_NO_SDL3_IMAGE
#include <SDL3_image/SDL_image.h>
#endif

#include "help_data.h"
#include "ui_help_contents.h"
#include "display_backend.h"
#include "gl_helpers.h"

extern SDL_Window *g_pWindow;
extern IDisplayBackend *ATUIGetDisplayBackend();
extern ImFont *ATUIGetFontMono();
extern ImFont *ATUIGetFontUI();

namespace ATHelp {

namespace {

constexpr uint32_t kHeaderSize       = 8 * 4;

uint32_t Read32(const uint8_t *p) {
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

uint16_t Read16(const uint8_t *p) {
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

HelpData s_data;

bool EqualsCaseInsensitive(const char *a, const char *b) {
	while (*a && *b) {
		if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
			return false;
		++a; ++b;
	}
	return *a == 0 && *b == 0;
}

bool StartsWith(const char *s, const char *prefix) {
	while (*prefix) {
		if (*s != *prefix) return false;
		++s; ++prefix;
	}
	return true;
}

} // namespace

// ===========================================================================
// HelpData
// ===========================================================================

HelpData &GetHelpData() { return s_data; }

bool HelpData::EnsureLoaded() {
	if (mLoaded) return true;
	if (mFailed) return false;

	if (kATHelpDataSize < 16) { mFailed = true; return false; }
	const uint8_t *blob = kATHelpData;
	if (std::memcmp(blob, "ATHELP\x00\x01", 8) != 0) {
		mFailed = true; return false;
	}
	uint32_t uncompressedSize = Read32(blob + 8);
	uint32_t compressedSize   = Read32(blob + 12);
	if (16 + compressedSize > kATHelpDataSize) {
		mFailed = true; return false;
	}

	// Decompress via VirtualDub's gunzip stream — already linked by
	// AltirraSDL through the system library, no extra dependency.
	mInflated.resize(uncompressedSize);
	try {
		VDMemoryStream src(blob + 16, compressedSize);
		VDGUnzipStream gz(&src, (uint64)compressedSize);
		uint32_t total = 0;
		while (total < uncompressedSize) {
			sint32 n = gz.ReadData(mInflated.data() + total,
			                       (sint32)(uncompressedSize - total));
			if (n <= 0) break;
			total += (uint32_t)n;
		}
		if (total != uncompressedSize) {
			mFailed = true; return false;
		}
	} catch (...) {
		mFailed = true; return false;
	}
	const uint8_t *base = mInflated.data();
	uint32_t payloadSize = uncompressedSize;
	if (payloadSize < kHeaderSize) {
		mFailed = true; return false;
	}

	uint32_t topicCount    = Read32(base + 0);
	uint32_t imageCount    = Read32(base + 4);
	uint32_t tocOff        = Read32(base + 8);
	uint32_t topicTableOff = Read32(base + 12);
	uint32_t imageTableOff = Read32(base + 16);
	uint32_t stringPoolOff = Read32(base + 20);
	uint32_t stringPoolSize= Read32(base + 24);
	(void)stringPoolSize;

	// String pool: u32 count, u32 offsets[count], (u16 len; bytes)[]
	const uint8_t *spBase = base + stringPoolOff;
	mStringCount = Read32(spBase);
	mStringPoolOffsets = spBase + 4;
	mStringPoolData = spBase + 4 + mStringCount * 4;
	mStringPoolDataSize = stringPoolSize;

	// TOC header: u16 firstRootOff (or 0xFFFF), u16 reserved,
	// u32 totalNodeBytes.  Then preorder node array of TocNode.
	const uint8_t *tocBase = base + tocOff;
	mTocFirstRootOff = Read16(tocBase + 0);
	// uint16 reserved at +2, uint32 totalSize at +4 — both skipped.
	mTocNodes = reinterpret_cast<const TocNode *>(tocBase + 8);

	// Topic table.
	mTopics.clear();
	mTopics.reserve(topicCount);
	const uint8_t *tt = base + topicTableOff;
	for (uint32_t i = 0; i < topicCount; ++i) {
		Topic t{};
		t.filenameIdx = Read16(tt + 0);
		t.titleIdx    = Read16(tt + 2);
		uint32_t streamOff = Read32(tt + 4);
		uint32_t streamSz  = Read32(tt + 8);
		uint32_t anchorOff = Read32(tt + 12);
		t.anchorCount = Read32(tt + 16);
		t.blockCount  = Read32(tt + 20);
		t.streamBegin = base + streamOff;
		t.streamEnd   = t.streamBegin + streamSz;
		t.anchorTable = base + anchorOff;
		mTopics.push_back(t);
		tt += 24;
	}

	// Image table.
	mImages.clear();
	mImages.reserve(imageCount);
	const uint8_t *it = base + imageTableOff;
	for (uint32_t i = 0; i < imageCount; ++i) {
		ImageRec r{};
		r.nameIdx = Read16(it + 0);
		uint32_t off = Read32(it + 2);
		r.size = Read32(it + 6);
		r.data = base + off;
		mImages.push_back(r);
		it += 10;
	}

	mLoaded = true;
	return true;
}

const char *HelpData::GetString(uint16_t idx) const {
	if (idx == 0 || idx >= mStringCount)
		return "";
	uint32_t off = Read32(mStringPoolOffsets + (uint32_t)idx * 4);
	// Layout per entry: u16 len, bytes[len], u8 NUL.  We return a
	// pointer to the bytes (which is NUL-terminated).
	return reinterpret_cast<const char *>(mStringPoolData + off + 2);
}

int HelpData::FindTopicByFilename(const char *filename) const {
	if (!filename || !*filename) return -1;
	// Accept "foo.html", "foo.xml", or "foo".  Source uses the .xml
	// form; convert.
	std::string s = filename;
	auto pos = s.find('#');
	if (pos != std::string::npos) s.resize(pos);
	// Strip extension, then re-add .xml.
	auto dot = s.find_last_of('.');
	if (dot != std::string::npos) s.resize(dot);
	s += ".xml";
	for (size_t i = 0; i < mTopics.size(); ++i) {
		const char *fn = GetString(mTopics[i].filenameIdx);
		if (EqualsCaseInsensitive(fn, s.c_str()))
			return (int)i;
	}
	return -1;
}

const ImageRec *HelpData::FindImageByNameIdx(uint16_t nameIdx) const {
	for (auto &im : mImages)
		if (im.nameIdx == nameIdx)
			return &im;
	return nullptr;
}

int HelpData::FindAnchor(const Topic &t, const char *anchorName) const {
	if (!anchorName || !*anchorName) return -1;
	const uint8_t *p = t.anchorTable;
	for (uint32_t i = 0; i < t.anchorCount; ++i) {
		uint16_t strIdx = Read16(p + 0);
		uint32_t blkIdx = Read32(p + 2);
		const char *s = GetString(strIdx);
		if (std::strcmp(s, anchorName) == 0)
			return (int)blkIdx;
		p += 6;
	}
	return -1;
}

// ===========================================================================
// Run decoding
// ===========================================================================

size_t DecodeRuns(const uint8_t *payload, size_t len,
                  std::vector<Run> &out)
{
	out.clear();
	if (len < 2) return 0;
	uint16_t count = Read16(payload);
	const uint8_t *p = payload + 2;
	const uint8_t *end = payload + len;
	for (uint16_t i = 0; i < count; ++i) {
		if (p + 3 > end) { out.clear(); return 0; }
		Run r{};
		r.flags   = p[0];
		r.textIdx = Read16(p + 1);
		p += 3;
		if (r.flags & kStyleLink) {
			if (p + 2 > end) { out.clear(); return 0; }
			r.hrefIdx = Read16(p);
			p += 2;
		}
		out.push_back(r);
	}
	return out.size();
}

// ===========================================================================
// Image texture cache
// ===========================================================================

namespace {

struct CachedTexture {
	ImTextureID texID = (ImTextureID)0;
	int w = 0, h = 0;
	GLuint glTex = 0;
	SDL_Texture *sdlTex = nullptr;
	bool tried = false;
};

std::vector<CachedTexture> s_imgCache;
std::vector<uint16_t>      s_imgKeys;     // parallel: nameIdx per slot

SDL_Surface *DecodeImage(const uint8_t *data, size_t size) {
#ifndef ALTIRRA_NO_SDL3_IMAGE
	SDL_IOStream *io = SDL_IOFromConstMem(data, size);
	if (!io) return nullptr;
	SDL_Surface *surf = IMG_Load_IO(io, true);
	if (!surf) return nullptr;
	SDL_Surface *conv = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_BGRA32);
	SDL_DestroySurface(surf);
	return conv;
#else
	(void)data; (void)size;
	return nullptr;
#endif
}

bool UploadTexture(CachedTexture &out, SDL_Surface *surf) {
	if (!surf || surf->w <= 0 || surf->h <= 0) return false;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend &&
	             backend->GetType() == DisplayBackendType::OpenGL;

	out.w = surf->w;
	out.h = surf->h;

	if (useGL) {
		out.glTex = GLCreateXRGB8888Texture(surf->w, surf->h, true, nullptr);
		if (!out.glTex) return false;
		std::vector<uint32_t> buf((size_t)surf->w * (size_t)surf->h);
		const uint8_t *src = (const uint8_t *)surf->pixels;
		for (int y = 0; y < surf->h; ++y)
			std::memcpy(&buf[(size_t)y * (size_t)surf->w],
			            src + (size_t)y * (size_t)surf->pitch,
			            (size_t)surf->w * 4);
		glBindTexture(GL_TEXTURE_2D, out.glTex);
		GLUploadXRGB8888(surf->w, surf->h, buf.data(), 0);
		out.texID = (ImTextureID)(intptr_t)out.glTex;
		return true;
	}

	SDL_Renderer *renderer = g_pWindow ? SDL_GetRenderer(g_pWindow) : nullptr;
	if (!renderer) return false;

	out.sdlTex = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_BGRA32,
		SDL_TEXTUREACCESS_STATIC, surf->w, surf->h);
	if (!out.sdlTex) return false;
	if (!SDL_UpdateTexture(out.sdlTex, nullptr, surf->pixels, surf->pitch)) {
		SDL_DestroyTexture(out.sdlTex);
		out.sdlTex = nullptr;
		return false;
	}
	out.texID = (ImTextureID)out.sdlTex;
	return true;
}

} // namespace

ImTextureID GetImageTexture(uint16_t nameIdx, int *outW, int *outH) {
	auto &data = GetHelpData();
	if (!data.EnsureLoaded()) return (ImTextureID)0;
	const ImageRec *img = data.FindImageByNameIdx(nameIdx);
	if (!img) return (ImTextureID)0;

	for (size_t i = 0; i < s_imgKeys.size(); ++i) {
		if (s_imgKeys[i] == nameIdx) {
			auto &c = s_imgCache[i];
			if (outW) *outW = c.w;
			if (outH) *outH = c.h;
			return c.texID;
		}
	}

	SDL_Surface *surf = DecodeImage(img->data, img->size);
	CachedTexture entry;
	entry.tried = true;
	if (surf) {
		UploadTexture(entry, surf);
		SDL_DestroySurface(surf);
	}
	s_imgKeys.push_back(nameIdx);
	s_imgCache.push_back(entry);
	if (outW) *outW = entry.w;
	if (outH) *outH = entry.h;
	return entry.texID;
}

void ReleaseAllImageTextures() {
	for (auto &c : s_imgCache) {
		if (c.glTex) {
			glDeleteTextures(1, &c.glTex);
			c.glTex = 0;
		}
		if (c.sdlTex) {
			SDL_DestroyTexture(c.sdlTex);
			c.sdlTex = nullptr;
		}
	}
	s_imgCache.clear();
	s_imgKeys.clear();
}

// ===========================================================================
// Block-stream renderer
// ===========================================================================
//
// External entry point: ATHelp::RenderTopic(const Topic &t, RenderContext &)
// walks the topic's block stream once per frame, emitting ImGui draws.
//
// State carried across blocks:
//   - we track per-block rendered Y position (top edge, in window-local
//     coords) so the parent can scroll to a specific block index when
//     the user follows an anchor.

namespace {

ImFont *MonoFont() { return ::ATUIGetFontMono(); }
ImFont *UIFont()   { return ::ATUIGetFontUI(); }

// Lowercase ASCII in-place (find-in-page is ASCII-only by design — the
// help corpus is English).
std::string ToLower(const std::string &s) {
	std::string out = s;
	for (auto &c : out) c = (char)std::tolower((unsigned char)c);
	return out;
}

bool TextContainsCI(const char *hay, const std::string &needleLower) {
	if (needleLower.empty()) return false;
	std::string h = ToLower(hay);
	return h.find(needleLower) != std::string::npos;
}

// Forward declaration — defined after RenderRuns.
void RenderRuns(const HelpData &data, const std::vector<Run> &runs,
                RenderContext &ctx, bool boldOverride, ImVec4 baseColor);

// Manual word-wrap renderer for a sequence of styled runs.  Each run
// can have its own color/font/link state; ImGui's TextWrapped is line-
// level so we cannot use it directly.
void RenderRunsImpl(const HelpData &data, const std::vector<Run> &runs,
                    RenderContext &ctx, bool boldOverride, ImVec4 baseColor)
{
	const float wrapWidth = ImGui::GetContentRegionAvail().x;
	if (wrapWidth <= 0) return;

	const float startX = ImGui::GetCursorPosX();
	float lineX = 0.0f;

	auto newLineIfNeeded = [&](float advance) {
		if (lineX > 0 && lineX + advance > wrapWidth) {
			ImGui::NewLine();
			ImGui::SetCursorPosX(startX);
			lineX = 0.0f;
		}
	};

	auto emitToken = [&](const std::string &tok, const Run &r) {
		if (tok.empty()) return;
		bool isMono = (r.flags & kStyleMono) != 0;
		bool isLink = (r.flags & kStyleLink) != 0;
		bool isBold = boldOverride || (r.flags & kStyleBold) != 0;
		bool isItalic = (r.flags & kStyleItalic) != 0;

		ImFont *font = isMono ? MonoFont() : UIFont();
		if (font) ImGui::PushFont(font);

		// Color: link uses ImGuiCol_TextLink; bold = brighter; italic
		// = dim.
		ImVec4 col = baseColor;
		if (isLink) {
			col = ImGui::GetStyleColorVec4(ImGuiCol_TextLink);
		} else if (isBold) {
			// Boost luminance ~15% by mixing toward white.
			col.x = std::min(1.0f, col.x + 0.10f);
			col.y = std::min(1.0f, col.y + 0.10f);
			col.z = std::min(1.0f, col.z + 0.10f);
		} else if (isItalic) {
			col.x *= 0.80f;
			col.y *= 0.80f;
			col.z *= 0.80f;
		}

		ImVec2 sz = ImGui::CalcTextSize(tok.c_str());
		newLineIfNeeded(sz.x);

		if (isLink && r.hrefIdx) {
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			// Unique ID per (href, token) so duplicate link texts in
			// the same topic don't collide.
			ImGui::PushID(&r);
			if (ImGui::TextLink(tok.c_str())) {
				ctx.clickedLink = data.GetString(r.hrefIdx);
			}
			ImGui::PopID();
			ImGui::PopStyleColor();
		} else {
			ImGui::PushStyleColor(ImGuiCol_Text, col);
			ImGui::TextUnformatted(tok.c_str());
			ImGui::PopStyleColor();
		}
		ImGui::SameLine(0.0f, 0.0f);
		lineX += sz.x;

		if (font) ImGui::PopFont();
	};

	// Flush any pending SameLine carry-over from previous Text() call.
	for (const Run &r : runs) {
		const char *txt = data.GetString(r.textIdx);
		// Tokenise on spaces and on \n (force break).  A run's text is
		// already collapsed (from converter), so each token is a word
		// possibly followed by a single space.
		std::string buf;
		buf.reserve(64);
		for (const char *p = txt; *p; ++p) {
			char c = *p;
			if (c == '\n') {
				if (!buf.empty()) { emitToken(buf, r); buf.clear(); }
				ImGui::NewLine();
				ImGui::SetCursorPosX(startX);
				lineX = 0.0f;
				continue;
			}
			if (c == ' ') {
				buf.push_back(' ');
				emitToken(buf, r);
				buf.clear();
				continue;
			}
			buf.push_back(c);
		}
		if (!buf.empty()) emitToken(buf, r);
	}

	// Terminate the line so subsequent ImGui calls start fresh.
	ImGui::NewLine();
}

void RenderRuns(const HelpData &data, const std::vector<Run> &runs,
                RenderContext &ctx, bool boldOverride, ImVec4 baseColor)
{
	if (runs.empty()) return;
	RenderRunsImpl(data, runs, ctx, boldOverride, baseColor);
}

void RenderHeading(const HelpData &data, const std::vector<Run> &runs,
                   RenderContext &ctx, float scale)
{
	ImFont *f = UIFont();
	float oldScale = f ? f->Scale : 1.0f;
	if (f) {
		f->Scale = oldScale * scale;
		ImGui::PushFont(f);
	}
	ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	col.x = std::min(1.0f, col.x + 0.05f);
	col.y = std::min(1.0f, col.y + 0.05f);
	col.z = std::min(1.0f, col.z + 0.05f);
	RenderRuns(data, runs, ctx, /*boldOverride=*/true, col);
	if (f) {
		ImGui::PopFont();
		f->Scale = oldScale;
	}
	ImGui::Spacing();
}

void HighlightMatchUnder(int blockIndex, RenderContext &ctx)
{
	if (ctx.findText.empty()) return;
	bool isCurrent = (blockIndex == ctx.currentMatchBlock);
	ImU32 col = ImGui::GetColorU32(
		isCurrent ? ImVec4(1.0f, 0.85f, 0.0f, 0.40f)
		          : ImVec4(1.0f, 0.85f, 0.0f, 0.18f));
	ImVec2 mn = ImGui::GetItemRectMin();
	ImVec2 mx = ImGui::GetItemRectMax();
	mx.x = mn.x + ImGui::GetContentRegionAvail().x + (mx.x - mn.x);
	ImGui::GetWindowDrawList()->AddRectFilled(mn, mx, col);
}

} // namespace

namespace {

struct BlockHeader {
	uint8_t  op;
	uint8_t  flags;
	uint16_t anchorIdx;       // 0 if no anchor
	uint16_t payloadLen;
	const uint8_t *payload;
	const uint8_t *next;
};

bool ReadHeader(const uint8_t *p, const uint8_t *end, BlockHeader &h) {
	if (p + 2 > end) return false;
	h.op = p[0];
	h.flags = p[1];
	p += 2;
	h.anchorIdx = 0;
	if (h.flags & kBlockHasAnchor) {
		if (p + 2 > end) return false;
		h.anchorIdx = Read16(p);
		p += 2;
	}
	if (p + 2 > end) return false;
	h.payloadLen = Read16(p);
	p += 2;
	if (p + h.payloadLen > end) return false;
	h.payload = p;
	h.next = p + h.payloadLen;
	return true;
}

// Pre-collect headings of a topic so MINI_TOC can list them.
struct HeadingEntry {
	int blockIndex;
	int level;       // 2 / 3 / 4
	std::string text;
};

void CollectHeadings(const HelpData &data, const Topic &topic,
                     std::vector<HeadingEntry> &out)
{
	const uint8_t *p = topic.streamBegin;
	const uint8_t *end = topic.streamEnd;
	int idx = 0;
	while (p < end) {
		BlockHeader h;
		if (!ReadHeader(p, end, h)) break;
		if (h.op == kOpH2 || h.op == kOpH3 || h.op == kOpH4) {
			std::vector<Run> runs;
			DecodeRuns(h.payload, h.payloadLen, runs);
			std::string text;
			for (auto &r : runs) text += data.GetString(r.textIdx);
			HeadingEntry e;
			e.blockIndex = idx;
			e.level = (h.op == kOpH2) ? 2 :
			          (h.op == kOpH3) ? 3 : 4;
			e.text = text;
			out.push_back(std::move(e));
		}
		p = h.next;
		++idx;
	}
}

// Render a mini-TOC at the current cursor position, expanding the
// topic's headings.
void RenderMiniTOC(const HelpData &data, const Topic &topic,
                   RenderContext &ctx)
{
	std::vector<HeadingEntry> headings;
	CollectHeadings(data, topic, headings);
	if (headings.empty()) return;
	ImGui::Indent();
	for (auto &h : headings) {
		float indent = (h.level - 2) * 16.0f;
		if (indent > 0) ImGui::Indent(indent);
		std::string label = "• " + h.text;
		ImGui::PushID(h.blockIndex);
		if (ImGui::TextLink(label.c_str())) {
			// Anchor by block index — synthesise a marker the parent
			// recognises.  "#__block_<idx>" is intercepted in
			// HandleLink and turned into a scroll request.
			char buf[32];
			std::snprintf(buf, sizeof(buf), "#__block_%d", h.blockIndex);
			ctx.clickedLink = buf;
		}
		ImGui::PopID();
		if (indent > 0) ImGui::Unindent(indent);
	}
	ImGui::Unindent();
	ImGui::Spacing();
}

} // namespace

void RenderTopic(const Topic &topic, RenderContext &ctx)
{
	auto &data = GetHelpData();
	ctx.blockY.assign(topic.blockCount, 0.0f);
	ctx.matchBlocks.clear();

	const uint8_t *p = topic.streamBegin;
	const uint8_t *end = topic.streamEnd;
	int idx = 0;

	// Per-block context state.
	int  listDepth = 0;
	bool listOrdered[8] = {false};
	int  listItemIndex[8] = {0};
	bool inNote = false;
	bool inBlockquote = false;

	// Tables: track current row's open state.
	int  tableCol = 0;
	int  tableColCount = 0;

	auto baseTextColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

	std::vector<Run> runs;
	std::string findLower = ctx.findText.empty() ? "" : ToLower(ctx.findText);

	while (p < end) {
		BlockHeader h;
		if (!ReadHeader(p, end, h)) break;

		// Record block top Y in window-local coords (used for scroll-
		// to-anchor next frame).
		if (idx < (int)ctx.blockY.size())
			ctx.blockY[idx] = ImGui::GetCursorPosY();

		switch (h.op) {
		case kOpPara: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			if (!runs.empty()) {
				if (listDepth > 0) ImGui::Bullet();
				ImVec2 startCursor = ImGui::GetCursorScreenPos();
				RenderRuns(data, runs, ctx, false, baseTextColor);
				if (!findLower.empty()) {
					bool match = false;
					for (auto &r : runs)
						if (TextContainsCI(data.GetString(r.textIdx), findLower)) {
							match = true; break;
						}
					if (match) {
						ctx.matchBlocks.push_back(idx);
						ImVec2 endCursor = ImGui::GetCursorScreenPos();
						bool isCurrent = (idx == ctx.currentMatchBlock);
						ImU32 col = ImGui::GetColorU32(
							isCurrent ? ImVec4(1.0f, 0.85f, 0.0f, 0.30f)
							          : ImVec4(1.0f, 0.85f, 0.0f, 0.15f));
						ImGui::GetWindowDrawList()->AddRectFilled(
							startCursor,
							ImVec2(startCursor.x + ImGui::GetContentRegionAvail().x,
							       endCursor.y),
							col);
					}
				}
				ImGui::Spacing();
			}
			break;
		}
		case kOpH2: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			ImGui::Spacing();
			RenderHeading(data, runs, ctx, 1.45f);
			ImGui::Separator();
			break;
		}
		case kOpH3: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			ImGui::Spacing();
			RenderHeading(data, runs, ctx, 1.20f);
			break;
		}
		case kOpH4: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			RenderHeading(data, runs, ctx, 1.05f);
			break;
		}
		case kOpListStart:
			if (listDepth < 8) {
				listOrdered[listDepth] = (h.payloadLen >= 1 &&
				                          h.payload[0] != 0);
				listItemIndex[listDepth] = 0;
				++listDepth;
			}
			ImGui::Indent();
			break;
		case kOpListEnd:
			if (listDepth > 0) --listDepth;
			ImGui::Unindent();
			ImGui::Spacing();
			break;
		case kOpListItem: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			if (listDepth > 0 && listOrdered[listDepth - 1]) {
				++listItemIndex[listDepth - 1];
				char numbuf[16];
				std::snprintf(numbuf, sizeof(numbuf), "%d.",
				              listItemIndex[listDepth - 1]);
				ImGui::TextUnformatted(numbuf);
				ImGui::SameLine();
			} else {
				ImGui::Bullet();
			}
			if (!runs.empty()) {
				RenderRuns(data, runs, ctx, false, baseTextColor);
			} else {
				ImGui::Dummy(ImVec2(1, 0));
			}
			break;
		}
		case kOpNoteStart: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			std::string title;
			for (auto &r : runs) title += data.GetString(r.textIdx);
			if (title.empty()) title = "Note";
			inNote = true;
			ImGui::Spacing();
			ImVec4 noteCol = ImGui::GetStyleColorVec4(ImGuiCol_FrameBg);
			ImGui::PushStyleColor(ImGuiCol_ChildBg, noteCol);
			ImGui::BeginChild(("##note_" + std::to_string(idx)).c_str(),
				ImVec2(0, 0),
				ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
			{
				ImVec4 titleCol = ImGui::GetStyleColorVec4(ImGuiCol_TextLink);
				ImGui::PushStyleColor(ImGuiCol_Text, titleCol);
				ImGui::TextUnformatted(title.c_str());
				ImGui::PopStyleColor();
				ImGui::Separator();
			}
			break;
		}
		case kOpNoteEnd:
			if (inNote) {
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::Spacing();
				inNote = false;
			}
			break;
		case kOpBQStart:
			inBlockquote = true;
			ImGui::Indent(20.0f);
			break;
		case kOpBQEnd:
			if (inBlockquote) {
				ImGui::Unindent(20.0f);
				inBlockquote = false;
			}
			break;
		case kOpTableStart: {
			tableColCount = (h.payloadLen >= 1) ? h.payload[0] : 1;
			if (tableColCount < 1) tableColCount = 1;
			ImGui::Spacing();
			ImGui::BeginTable(("##tbl_" + std::to_string(idx)).c_str(),
				tableColCount,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_SizingStretchProp);
			tableCol = -1;
			break;
		}
		case kOpTableRow:
			ImGui::TableNextRow();
			tableCol = 0;
			break;
		case kOpTableCell:
		case kOpTableHCell: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			if (tableCol >= 0 && tableCol < tableColCount) {
				ImGui::TableSetColumnIndex(tableCol);
				bool isHdr = (h.op == kOpTableHCell);
				if (isHdr) {
					ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_TextLink);
					RenderRuns(data, runs, ctx, true, c);
				} else {
					RenderRuns(data, runs, ctx, false, baseTextColor);
				}
				++tableCol;
			}
			break;
		}
		case kOpTableEnd:
			ImGui::EndTable();
			ImGui::Spacing();
			tableColCount = 0;
			tableCol = 0;
			break;
		case kOpDLStart:
			ImGui::Spacing();
			break;
		case kOpDLEnd:
			ImGui::Spacing();
			break;
		case kOpDT: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			RenderRuns(data, runs, ctx, true, baseTextColor);
			break;
		}
		case kOpDD: {
			DecodeRuns(h.payload, h.payloadLen, runs);
			ImGui::Indent();
			RenderRuns(data, runs, ctx, false, baseTextColor);
			ImGui::Unindent();
			break;
		}
		case kOpPre: {
			if (h.payloadLen >= 2) {
				uint16_t strIdx = Read16(h.payload);
				const char *txt = data.GetString(strIdx);
				ImFont *m = MonoFont();
				if (m) ImGui::PushFont(m);
				ImGui::PushStyleColor(ImGuiCol_ChildBg,
					ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
				ImGui::BeginChild(("##pre_" + std::to_string(idx)).c_str(),
					ImVec2(0, 0),
					ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
					ImGuiWindowFlags_HorizontalScrollbar);
				ImGui::TextUnformatted(txt);
				ImGui::EndChild();
				ImGui::PopStyleColor();
				if (m) ImGui::PopFont();
				ImGui::Spacing();
			}
			break;
		}
		case kOpImage: {
			if (h.payloadLen >= 4) {
				uint16_t imgIdx = Read16(h.payload);
				uint16_t capIdx = Read16(h.payload + 2);
				int w = 0, hpx = 0;
				ImTextureID tex = GetImageTexture(imgIdx, &w, &hpx);
				if (tex && w > 0 && hpx > 0) {
					float maxW = ImGui::GetContentRegionAvail().x;
					float scale = (w > maxW) ? (maxW / (float)w) : 1.0f;
					ImGui::Image(tex,
						ImVec2((float)w * scale, (float)hpx * scale));
					if (capIdx) {
						const char *cap = data.GetString(capIdx);
						ImGui::TextDisabled("%s", cap);
					}
					ImGui::Spacing();
				}
			}
			break;
		}
		case kOpMiniTOC:
			RenderMiniTOC(data, topic, ctx);
			break;
		case kOpHr:
			ImGui::Separator();
			ImGui::Spacing();
			break;
		default:
			// Unknown op — skip silently.
			break;
		}

		p = h.next;
		++idx;
	}

	// Close any leftover scopes (defensive).
	if (inNote) {
		ImGui::EndChild();
		ImGui::PopStyleColor();
	}
	while (listDepth > 0) {
		ImGui::Unindent();
		--listDepth;
	}
}

} // namespace ATHelp
