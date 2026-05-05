//	AltirraSDL - Help → Contents browser (header)
//	Internal interface used by ui_help_contents.cpp and ui_help_render.cpp.
//	Public entry points are declared in ui_main_internal.h.

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <string>

#include <imgui.h>

namespace ATHelp {

// Style flag bits used by run encoding.  Must match bake_help.py.
constexpr uint8_t kStyleBold   = 0x01;
constexpr uint8_t kStyleItalic = 0x02;
constexpr uint8_t kStyleMono   = 0x04;
constexpr uint8_t kStyleLink   = 0x08;

// Block opcodes — must match bake_help.py.
enum BlockOp : uint8_t {
	kOpPara        = 1,
	kOpH2          = 2,
	kOpH3          = 3,
	kOpH4          = 4,
	kOpListStart   = 5,
	kOpListEnd     = 6,
	kOpListItem    = 7,
	kOpNoteStart   = 8,
	kOpNoteEnd     = 9,
	kOpBQStart     = 10,
	kOpBQEnd       = 11,
	kOpTableStart  = 12,
	kOpTableRow    = 13,
	kOpTableCell   = 14,
	kOpTableHCell  = 15,
	kOpTableEnd    = 16,
	kOpDLStart     = 17,
	kOpDLEnd       = 18,
	kOpDT          = 19,
	kOpDD          = 20,
	kOpPre         = 21,
	kOpImage       = 22,
	kOpMiniTOC     = 23,
	kOpHr          = 24,
};

constexpr uint8_t kBlockHasAnchor = 0x01;

// Topic record (from baked topic table).
struct Topic {
	uint16_t  filenameIdx;
	uint16_t  titleIdx;
	const uint8_t *streamBegin;
	const uint8_t *streamEnd;
	const uint8_t *anchorTable;     // (u16 anchorStrIdx, u32 blockIdx)[anchorCount]
	uint32_t  anchorCount;
	uint32_t  blockCount;
};

struct ImageRec {
	uint16_t  nameIdx;
	const uint8_t *data;
	uint32_t  size;
};

struct TocNode {
	uint16_t nameIdx;
	uint16_t hrefIdx;
	uint16_t hasChildren;     // 0 = leaf, 1 = first child at offset+8
	uint16_t nextSiblingOff;  // byte offset of next sibling, 0xFFFF = end
};

constexpr uint16_t kTocNoSibling = 0xFFFFu;

class HelpData {
public:
	HelpData() = default;

	// Decompresses the baked blob and builds index tables.  Idempotent;
	// safe to call multiple times.  Returns false if the blob is missing
	// or corrupt.
	bool EnsureLoaded();

	// Total number of topics.
	size_t TopicCount() const { return mTopics.size(); }

	// Lookup a topic by filename (e.g. "colors.html").  Returns -1 if
	// not found.  Also accepts the .xml form ("colors.xml" or "colors").
	int FindTopicByFilename(const char *filename) const;

	// Indexed access.
	const Topic &GetTopic(size_t i) const { return mTopics[i]; }
	const std::vector<Topic> &Topics() const { return mTopics; }

	// String pool — returns pointer to UTF-8 NUL-terminated string for
	// the given index (0 returns empty string).
	const char *GetString(uint16_t idx) const;

	// Image lookup by name index.
	const ImageRec *FindImageByNameIdx(uint16_t nameIdx) const;

	// TOC: returns pointer to the flat array of nodes (preorder).  The
	// first root sibling is at TocFirstRootOff() bytes into the array;
	// follow nextSiblingOff to walk siblings, or recurse into children
	// at offset + 8.
	const TocNode *TocNodes() const { return mTocNodes; }
	uint16_t TocFirstRootOff() const { return mTocFirstRootOff; }

	// Anchor lookup within a topic.  Returns the block index (or -1
	// if not found).
	int FindAnchor(const Topic &t, const char *anchorName) const;

private:
	bool mLoaded = false;
	bool mFailed = false;

	// Pointers / sizes into the (uncompressed) payload region:
	std::vector<Topic> mTopics;
	std::vector<ImageRec> mImages;
	const uint8_t *mStringPoolOffsets = nullptr;
	uint32_t mStringCount = 0;
	const uint8_t *mStringPoolData = nullptr;
	uint32_t mStringPoolDataSize = 0;
	const TocNode *mTocNodes = nullptr;
	uint16_t mTocFirstRootOff = kTocNoSibling;

	// Inflated payload (used when the blob is gzip-compressed).
	std::vector<uint8_t> mInflated;
};

// Accessor — owns the singleton.
HelpData &GetHelpData();

// Image texture cache: lazy-decode + upload on first call, then cached.
// Returns 0 if decode failed or image not present.
ImTextureID GetImageTexture(uint16_t nameIdx, int *outW, int *outH);
void ReleaseAllImageTextures();

// ----- Run iteration -----
//
// Each text-bearing block payload begins with a uint16 runCount,
// followed by per-run records: u8 flags, u16 textIdx,
// (if kStyleLink) u16 hrefIdx.

struct Run {
	uint8_t   flags;
	uint16_t  textIdx;
	uint16_t  hrefIdx;   // 0 if no link
};

// Decode runs from a payload buffer.  Returns the number of runs
// read; out_runs is populated.  Returns 0 on malformed input.
size_t DecodeRuns(const uint8_t *payload, size_t len, std::vector<Run> &out);

// Renderer context — populated each frame by RenderTopic(), consumed
// by the help window for navigation/scroll/find.
struct RenderContext {
	std::vector<float> blockY;       // per-block top Y in window coords
	std::string findText;            // empty = no find
	int currentMatchBlock = -1;
	std::vector<int> matchBlocks;    // populated during render
	std::string clickedLink;         // populated if a link was clicked
};

void RenderTopic(const Topic &topic, RenderContext &ctx);

} // namespace ATHelp
