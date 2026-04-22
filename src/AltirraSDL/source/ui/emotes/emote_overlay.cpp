//	AltirraSDL - Online Play emote fade overlay.
//
//	Anchored top-left of the work area (below the menu bar if it's
//	visible, inside the viewport otherwise, never over the top-right
//	Session HUD).  Animated entry/exit combine three effects driven by
//	a single "visibility" parameter p in [0, 1]:
//
//	    * horizontal slide: x = lerp(offscreen_left, final_x, p)
//	    * scale:            s = 0.25 + 0.75 * p   (25% -> 100%)
//	    * fade:             a = p                  (transparent -> opaque)
//
//	Timeline (total 4.0 s):
//	    0.00 - 0.40 s   entry  (p: 0 -> 1, smoothstep)
//	    0.40 - 3.60 s   hold   (p = 1)
//	    3.60 - 4.00 s   exit   (p: 1 -> 0, smoothstep)
//
//	The icon reappearing during an active display replaces the current
//	one and restarts the state machine at entry, which feels more
//	responsive than queueing.

#include <stdafx.h>
#include <imgui.h>

#include "emote_assets.h"
#include "emote_overlay.h"

namespace ATEmoteOverlay {

namespace {

constexpr uint64_t kEntryMs = 400;
constexpr uint64_t kHoldMs  = 3200;
constexpr uint64_t kExitMs  = 400;
constexpr uint64_t kTotalMs = kEntryMs + kHoldMs + kExitMs; // 4000

int      gIconId  = -1;
uint64_t gStartMs = 0;

// Cubic Hermite smoothstep: 3t^2 - 2t^3.  Smooth velocity at 0 and 1,
// non-zero acceleration in the middle — gives a nicer feel than a
// linear ramp for both the slide and the scale.
float Smoothstep(float t) {
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

// Visibility progress p: 0 (hidden) ... 1 (fully on-screen, full size).
float ComputeP(uint64_t elapsed) {
	if (elapsed < kEntryMs) {
		return Smoothstep((float)elapsed / (float)kEntryMs);
	}
	if (elapsed < kEntryMs + kHoldMs) {
		return 1.0f;
	}
	if (elapsed < kTotalMs) {
		float t = (float)(elapsed - kEntryMs - kHoldMs) / (float)kExitMs;
		return 1.0f - Smoothstep(t);
	}
	return 0.0f;
}

} // namespace

void Show(int iconId, uint64_t nowMs) {
	gIconId = iconId;
	gStartMs = nowMs;
}

void Clear() {
	gIconId = -1;
}

void Render(uint64_t nowMs) {
	if (gIconId < 0)
		return;

	uint64_t elapsed = nowMs - gStartMs;
	if (elapsed >= kTotalMs) {
		gIconId = -1;
		return;
	}

	int srcW = 0, srcH = 0;
	ImTextureID tex = ATEmotes::GetTexture(gIconId, &srcW, &srcH);
	if (!tex || srcW <= 0 || srcH <= 0)
		return;

	const ImGuiViewport *vp = ImGui::GetMainViewport();

	// WorkPos/WorkSize exclude the menu bar — so when the desktop menu
	// is visible, the overlay sits just below it; when the menu is
	// auto-hidden (gaming mode, fullscreen), it sits at the viewport
	// top.  Either way it never clips into the Session HUD's top-right
	// area.
	const ImVec2 workPos  = vp->WorkPos;
	const ImVec2 workSize = vp->WorkSize;

	// Display height: ~15% of work area, bounded for phone & 4K.
	float displayH = workSize.y * 0.15f;
	if (displayH < 96.0f) displayH = 96.0f;
	if (displayH > 192.0f) displayH = 192.0f;
	float displayW = displayH * ((float)srcW / (float)srcH);

	const float margin = 16.0f;
	const float finalX = workPos.x + margin;
	const float finalY = workPos.y + margin;

	// Offscreen-left: fully past the viewport's left edge (vp->Pos.x,
	// NOT WorkPos.x — the emote should slide in from beyond the actual
	// window edge, not from below the menu bar boundary).  Add a small
	// gutter so anti-aliasing can't clip back into view.
	const float offscreenX = vp->Pos.x - displayW - 8.0f;

	const float p = ComputeP(elapsed);

	// Slide: lerp offscreen -> final as p: 0 -> 1.
	const float x = offscreenX + (finalX - offscreenX) * p;
	const float y = finalY;

	// Scale: 25% -> 100%.  Anchor the scale box to the top-left of the
	// final rect so the icon grows to the right / downward from the
	// screen edge rather than shrinking into the middle of empty space.
	const float scale = 0.25f + 0.75f * p;
	const float scaledW = displayW * scale;
	const float scaledH = displayH * scale;

	// Alpha.
	const float alpha = p;

	ImDrawList *dl = ImGui::GetForegroundDrawList(
		const_cast<ImGuiViewport *>(vp));

	// Soft shadow disc behind the icon for legibility on busy
	// emulated backgrounds.  Dimmer than the icon so it fades faster
	// on exit.
	const ImVec2 center(x + scaledW * 0.5f, y + scaledH * 0.5f);
	const float  radius = (scaledW > scaledH ? scaledW : scaledH) * 0.55f;
	const ImU32  shadow = IM_COL32(0, 0, 0, (int)(alpha * 110.0f));
	dl->AddCircleFilled(center, radius, shadow, 32);

	const ImU32 tint = IM_COL32(255, 255, 255, (int)(alpha * 255.0f));
	dl->AddImage(tex,
		ImVec2(x, y),
		ImVec2(x + scaledW, y + scaledH),
		ImVec2(0, 0), ImVec2(1, 1),
		tint);
}

} // namespace ATEmoteOverlay
