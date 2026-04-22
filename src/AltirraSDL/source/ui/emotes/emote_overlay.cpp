//	AltirraSDL - Online Play emote fade overlay.

#include <stdafx.h>
#include <imgui.h>

#include "emote_assets.h"
#include "emote_overlay.h"

namespace ATEmoteOverlay {

namespace {

constexpr uint64_t kFadeInMs  = 300;
constexpr uint64_t kHoldMs    = 3400;
constexpr uint64_t kFadeOutMs = 300;
constexpr uint64_t kTotalMs   = kFadeInMs + kHoldMs + kFadeOutMs; // 4000

int      gIconId   = -1;
uint64_t gStartMs  = 0;

float ComputeAlpha(uint64_t elapsed) {
	if (elapsed < kFadeInMs)
		return (float)elapsed / (float)kFadeInMs;
	if (elapsed < kFadeInMs + kHoldMs)
		return 1.0f;
	if (elapsed < kTotalMs) {
		uint64_t t = elapsed - (kFadeInMs + kHoldMs);
		return 1.0f - (float)t / (float)kFadeOutMs;
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

	ImGuiViewport *vp = ImGui::GetMainViewport();
	float scale = ImGui::GetIO().FontGlobalScale;
	if (scale <= 0.0f) scale = 1.0f;

	// Target draw size: keep aspect, cap display height at ~15% of
	// viewport height so it doesn't dominate the emulated screen.
	float displayH = vp->Size.y * 0.15f;
	if (displayH < 96.0f) displayH = 96.0f;
	if (displayH > 192.0f) displayH = 192.0f;
	float displayW = displayH * ((float)srcW / (float)srcH);

	float margin = 16.0f;
	ImVec2 pos(
		vp->Pos.x + vp->Size.x - displayW - margin,
		vp->Pos.y + margin);

	float alpha = ComputeAlpha(elapsed);
	if (alpha < 0.0f) alpha = 0.0f;
	if (alpha > 1.0f) alpha = 1.0f;

	ImU32 tint = IM_COL32(255, 255, 255, (int)(alpha * 255.0f));

	ImDrawList *dl = ImGui::GetForegroundDrawList(vp);
	// Soft shadow disc behind the icon for legibility against busy
	// emulated backgrounds.
	ImU32 shadow = IM_COL32(0, 0, 0, (int)(alpha * 120.0f));
	ImVec2 center(pos.x + displayW * 0.5f, pos.y + displayH * 0.5f);
	float radius = (displayW > displayH ? displayW : displayH) * 0.55f;
	dl->AddCircleFilled(center, radius, shadow, 32);

	dl->AddImage(tex, pos, ImVec2(pos.x + displayW, pos.y + displayH),
		ImVec2(0, 0), ImVec2(1, 1), tint);
	(void)scale;
}

} // namespace ATEmoteOverlay
