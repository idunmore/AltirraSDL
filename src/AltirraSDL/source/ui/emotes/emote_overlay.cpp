//	AltirraSDL - Online Play emote overlays.
//
//	Two overlay slots with identical animation but mirrored anchors /
//	slide directions:
//
//	   Inbound (peer -> us):  top-left, slides in from offscreen left.
//	   Outbound (us -> peer): bottom-right, slides in from offscreen right.
//
//	A single visibility parameter p in [0, 1] drives three effects at
//	once: horizontal slide, uniform scale (25% -> 100%), and alpha
//	(transparent -> opaque).  Timeline (4.0 s total):
//
//	      0.00 - 0.40 s   entry  (p: 0 -> 1, smoothstep)
//	      0.40 - 3.60 s   hold   (p = 1)
//	      3.60 - 4.00 s   exit   (p: 1 -> 0, smoothstep)
//
//	Both sides run independently so seeing an incoming emote never
//	hides a simultaneous send confirmation.

#include <stdafx.h>
#include <imgui.h>

#include "emote_assets.h"
#include "emote_overlay.h"

namespace ATEmoteOverlay {

namespace {

constexpr uint64_t kEntryMs = 400;
constexpr uint64_t kHoldMs  = 3200;
constexpr uint64_t kExitMs  = 400;
constexpr uint64_t kTotalMs = kEntryMs + kHoldMs + kExitMs;

enum class Side { InboundTopLeft, OutboundBottomRight };

struct Slot {
	int      iconId  = -1;
	uint64_t startMs = 0;
};

Slot gInbound;
Slot gOutbound;

// Per-frame safe-area override.  Cleared at the end of each Render
// pass so a caller that forgets to set it on the next frame falls
// back to the viewport work area.
SafeArea gSafeArea;

float Smoothstep(float t) {
	if (t <= 0.0f) return 0.0f;
	if (t >= 1.0f) return 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

// 0 when hidden, 1 at full presence.
float ComputeP(uint64_t elapsed) {
	if (elapsed < kEntryMs)
		return Smoothstep((float)elapsed / (float)kEntryMs);
	if (elapsed < kEntryMs + kHoldMs)
		return 1.0f;
	if (elapsed < kTotalMs) {
		float t = (float)(elapsed - kEntryMs - kHoldMs) / (float)kExitMs;
		return 1.0f - Smoothstep(t);
	}
	return 0.0f;
}

void RenderSlot(Slot &slot, Side side, uint64_t nowMs) {
	if (slot.iconId < 0)
		return;

	const uint64_t elapsed = nowMs - slot.startMs;
	if (elapsed >= kTotalMs) {
		slot.iconId = -1;
		return;
	}

	int srcW = 0, srcH = 0;
	ImTextureID tex = ATEmotes::GetTexture(slot.iconId, &srcW, &srcH);
	if (!tex || srcW <= 0 || srcH <= 0)
		return;

	const ImGuiViewport *vp = ImGui::GetMainViewport();
	// Apply the per-frame safe-area override to the viewport work
	// area.  Mobile pushes the inbound icon below its custom top bar
	// (console buttons) and the outbound icon above its bottom touch
	// controls; Desktop leaves the override at zero so we anchor flush
	// against the menu-bar / window edge as before.
	const float extraTop    = gSafeArea.topPx    > 0.0f ? gSafeArea.topPx    : 0.0f;
	const float extraBottom = gSafeArea.bottomPx > 0.0f ? gSafeArea.bottomPx : 0.0f;
	const float extraLeft   = gSafeArea.leftPx   > 0.0f ? gSafeArea.leftPx   : 0.0f;
	const float extraRight  = gSafeArea.rightPx  > 0.0f ? gSafeArea.rightPx  : 0.0f;
	const ImVec2 workPos(vp->WorkPos.x + extraLeft,
	                     vp->WorkPos.y + extraTop);
	const ImVec2 workSize(vp->WorkSize.x - extraLeft - extraRight,
	                      vp->WorkSize.y - extraTop  - extraBottom);

	// Height ~15% of the longer work-area axis, clamped for phone +
	// 4K.  Driving from the longer axis (rather than workSize.y)
	// keeps the icon the same size in landscape and portrait — on a
	// 1080×1920 phone, workSize.y is 1920 in portrait but only 1080
	// in landscape, so the old workSize.y * 0.15f shrunk the icon by
	// ~30% the moment the user rotated the device.  Using the major
	// axis means the result is identical in both orientations on the
	// same device, and on landscape phones with bottom touch
	// controls + top status-bar safe-area insets (which subtract
	// further from workSize.y) the icon no longer collapses below
	// the 96px minimum either.
	const float majorAxis = workSize.x > workSize.y ? workSize.x : workSize.y;
	float displayH = majorAxis * 0.15f;
	if (displayH < 96.0f)  displayH = 96.0f;
	if (displayH > 192.0f) displayH = 192.0f;
	const float displayW = displayH * ((float)srcW / (float)srcH);
	const float margin   = 16.0f;

	// Anchor-specific final position + slide origin.  "final" is the
	// top-left of the icon rect at rest; "offscreenX" is where the
	// top-left sits fully offscreen on the entry edge.
	float finalX, finalY, offscreenX;
	switch (side) {
		case Side::InboundTopLeft:
			finalX     = workPos.x + margin;
			finalY     = workPos.y + margin;
			// Offscreen to the LEFT of the actual viewport edge
			// (Pos.x, not WorkPos.x — we want the icon to come from
			// behind the window chrome, not from below the menu bar).
			offscreenX = vp->Pos.x - displayW - 8.0f;
			break;
		case Side::OutboundBottomRight:
		default:
			finalX     = workPos.x + workSize.x - displayW - margin;
			finalY     = workPos.y + workSize.y - displayH - margin;
			// Offscreen to the RIGHT of the viewport edge.
			offscreenX = vp->Pos.x + vp->Size.x + 8.0f;
			break;
	}

	const float p     = ComputeP(elapsed);
	const float x     = offscreenX + (finalX - offscreenX) * p;
	const float y     = finalY;
	const float alpha = p;
	const float scale = 0.25f + 0.75f * p;

	// Scale centered on the slid rect so the icon "blooms" in place as
	// it slides — a top-left scale anchor looked fine for inbound but
	// awkward when mirrored for outbound, and center scale looks
	// natural for both sides.
	const float scaledW = displayW * scale;
	const float scaledH = displayH * scale;
	const float cx = x + displayW * 0.5f;
	const float cy = y + displayH * 0.5f;
	const ImVec2 drawMin(cx - scaledW * 0.5f, cy - scaledH * 0.5f);
	const ImVec2 drawMax(cx + scaledW * 0.5f, cy + scaledH * 0.5f);

	ImDrawList *dl = ImGui::GetForegroundDrawList(
		const_cast<ImGuiViewport *>(vp));

	// Soft shadow disc for legibility on busy backgrounds; dimmer alpha
	// so it fades faster on exit.
	const ImVec2 center(cx, cy);
	const float  radius = (scaledW > scaledH ? scaledW : scaledH) * 0.55f;
	dl->AddCircleFilled(center, radius,
		IM_COL32(0, 0, 0, (int)(alpha * 110.0f)), 32);

	const ImU32 tint = IM_COL32(255, 255, 255, (int)(alpha * 255.0f));
	dl->AddImage(tex, drawMin, drawMax,
		ImVec2(0, 0), ImVec2(1, 1), tint);
}

} // namespace

void Show(int iconId, uint64_t nowMs) {
	gInbound.iconId  = iconId;
	gInbound.startMs = nowMs;
}

void ShowOutbound(int iconId, uint64_t nowMs) {
	gOutbound.iconId  = iconId;
	gOutbound.startMs = nowMs;
}

void Clear() {
	gInbound.iconId  = -1;
	gOutbound.iconId = -1;
}

void SetSafeArea(const SafeArea &area) {
	gSafeArea = area;
}

void Render(uint64_t nowMs) {
	RenderSlot(gInbound,  Side::InboundTopLeft,     nowMs);
	RenderSlot(gOutbound, Side::OutboundBottomRight, nowMs);
	// Reset for the next frame so a caller that only wants to set
	// the safe area conditionally (e.g. only on mobile) doesn't keep
	// its values around when the condition stops holding.
	gSafeArea = SafeArea{};
}

} // namespace ATEmoteOverlay
