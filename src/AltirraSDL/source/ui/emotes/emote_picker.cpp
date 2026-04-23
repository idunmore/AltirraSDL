//	AltirraSDL - Online Play emote picker.
//
//	Modal popup: 4x4 grid + big Cancel button.  Sized responsively so it
//	fits a 360 px phone portrait up to a desktop fullscreen.
//
//	Each tile carries a letter shortcut (A..P) rendered in the top-left
//	corner on Desktop.  After F1 opens the picker, pressing that letter
//	sends the icon and closes the picker immediately — "F1, A" fires the
//	first emote in two keypresses without looking at the screen.
//
//	Inputs supported:
//	  Mouse    : hover + click, Cancel button, title-bar X.
//	  Touch    : tap a tile, tap Cancel (40/56 px tall).
//	  Keyboard : arrows move focus, Enter selects, Esc cancels, A..P
//	             jump-select any tile.
//	  Gamepad  : D-pad / left stick move focus, A selects, B cancels.
//
//	Desktop's ImGui nav flags are off by default; we enable them while
//	the picker is open and restore prior state on close.

#include <stdafx.h>
#include <algorithm>
#include <imgui.h>
#include <imgui_internal.h>

#include "emote_assets.h"
#include "emote_netplay.h"
#include "emote_picker.h"
#include "netplay/netplay_glue.h"

namespace ATEmotePicker {

namespace {

constexpr int kCols = 4;
constexpr int kRows = 4;
constexpr int kTotalTiles = kCols * kRows;
constexpr const char *kPopupId = "Emote##emote_picker";

enum class OpenRequest { None, Open, Close };

OpenRequest gRequest = OpenRequest::None;
bool        gPopupOpen = false;
bool        gNavFlagsPushed = false;
ImGuiConfigFlags gPrevNavFlags = 0;

bool IsGamingMode() {
	return ImGui::GetIO().FontGlobalScale >= 1.25f;
}

void PushNavFlags() {
	if (gNavFlagsPushed) return;
	ImGuiIO &io = ImGui::GetIO();
	gPrevNavFlags = io.ConfigFlags &
		(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
	io.ConfigFlags |=
		ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
	gNavFlagsPushed = true;
}

void PopNavFlags() {
	if (!gNavFlagsPushed) return;
	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags &=
		~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
	io.ConfigFlags |= gPrevNavFlags;
	gNavFlagsPushed = false;
}

// Close helpers — we end up doing the same dance from four exit paths
// (Cancel click, gamepad B, letter-key send, tile click), so wrap it.
void CloseSequence() {
	ImGui::CloseCurrentPopup();
	ImGui::EndPopup();
	gPopupOpen = false;
	PopNavFlags();
}

} // namespace

void Open() {
	if (!ATNetplayGlue::IsLockstepping()) return;
	if (!ATEmoteNetplay::GetSendEnabled()) return;
	if (!ATEmotes::IsReady()) return;
	gRequest = OpenRequest::Open;
}

void Close() {
	gRequest = OpenRequest::Close;
}

bool IsOpen() {
	return gPopupOpen;
}

void Render() {
	// --- 1. Drive open/close requests ---------------------------------
	if (gRequest == OpenRequest::Open && !gPopupOpen) {
		ImGui::OpenPopup(kPopupId);
		gPopupOpen = true;
		PushNavFlags();
	}
	gRequest = OpenRequest::None;

	if (!gPopupOpen)
		return;

	// --- 2. Force-close on state change -------------------------------
	bool mustClose =
		!ATNetplayGlue::IsLockstepping() ||
		!ATEmoteNetplay::GetSendEnabled();

	// --- 3. Sizing (accounts for FramePadding + WindowPadding + ItemSpacing) ---
	ImGuiStyle &style = ImGui::GetStyle();
	ImGuiViewport *vp = ImGui::GetMainViewport();
	const float vw = vp->Size.x;
	const float vh = vp->Size.y;
	const bool gaming = IsGamingMode();

	const float pad       = gaming ? 12.0f : 8.0f;   // item spacing (also grid pad)
	const float cancelH   = gaming ? 56.0f : 40.0f;  // touch-friendly cancel
	const float framePadX = style.FramePadding.x;
	const float framePadY = style.FramePadding.y;
	const float winPadX   = style.WindowPadding.x;
	const float winPadY   = style.WindowPadding.y;
	const float titleH    = ImGui::GetFrameHeight() + style.ItemSpacing.y;

	// Window chrome overhead: title bar + top/bottom window padding +
	// spacing row between grid and cancel + cancel button height.
	const float vOverhead =
		titleH + 2.0f * winPadY + style.ItemSpacing.y + cancelH;
	const float hOverhead = 2.0f * winPadX;

	// How much space a tile actually costs: image_size + 2*FramePadding
	// for the ImageButton frame around it.  Rows also have ItemSpacing
	// between them which I account for as (kCols-1)*pad for X and
	// (kRows-1)*pad for Y (we set ItemSpacing = (pad, pad) below).
	//
	// Solving for tileSize from the available window size:
	//   winW >= hOverhead + kCols*(tile + 2*framePadX) + (kCols-1)*pad
	//   winH >= vOverhead + kRows*(tile + 2*framePadY) + (kRows-1)*pad
	const float maxW = vw * 0.92f;
	const float maxH = vh * 0.88f;

	float tileFromW = (maxW - hOverhead - (kCols - 1) * pad) / (float)kCols
	                  - 2.0f * framePadX;
	float tileFromH = (maxH - vOverhead - (kRows - 1) * pad) / (float)kRows
	                  - 2.0f * framePadY;
	float tileSize = std::min(tileFromW, tileFromH);

	// Clamp so taps stay finger-sized and tiles don't balloon past a
	// comfortable max on 4K monitors.
	const float minTile = gaming ? 64.0f : 56.0f;
	const float maxTile = gaming ? 144.0f : 112.0f;
	tileSize = std::max(minTile, std::min(maxTile, tileSize));

	const float buttonW = tileSize + 2.0f * framePadX;
	const float buttonH = tileSize + 2.0f * framePadY;

	const float winW = hOverhead + kCols * buttonW + (kCols - 1) * pad;
	const float winH = vOverhead + kRows * buttonH + (kRows - 1) * pad;

	ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
	ImGui::SetNextWindowPos(
		ImVec2(vp->Pos.x + vw * 0.5f, vp->Pos.y + vh * 0.5f),
		ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags =
		  ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse;

	if (!ImGui::BeginPopupModal(kPopupId, nullptr, flags)) {
		gPopupOpen = false;
		PopNavFlags();
		return;
	}

	if (mustClose) {
		CloseSequence();
		return;
	}

	// --- 4. Keyboard letter shortcuts A..P ----------------------------
	// Checked first so they beat ImGui's item-level key handling in
	// edge cases.
	int letterChosen = -1;
	for (int i = 0; i < kTotalTiles; ++i) {
		ImGuiKey k = (ImGuiKey)(ImGuiKey_A + i);
		if (ImGui::IsKeyPressed(k, false)) {
			letterChosen = i;
			break;
		}
	}

	// --- 5. 4x4 grid --------------------------------------------------
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad, pad));

	int chosen = -1;
	for (int row = 0; row < kRows; ++row) {
		for (int col = 0; col < kCols; ++col) {
			const int idx = row * kCols + col;
			if (col > 0)
				ImGui::SameLine();

			ImGui::PushID(idx);

			// Record the frame-rect minimum BEFORE the button so we can
			// overlay the shortcut letter in its top-left corner.
			const ImVec2 itemMin = ImGui::GetCursorScreenPos();

			ImTextureID tex = ATEmotes::GetTexture(idx);
			bool clicked;
			if (tex) {
				clicked = ImGui::ImageButton("##tile", tex,
					ImVec2(tileSize, tileSize),
					ImVec2(0, 0), ImVec2(1, 1),
					ImVec4(0, 0, 0, 0),
					ImVec4(1, 1, 1, 1));
			} else {
				clicked = ImGui::Button("?", ImVec2(tileSize, tileSize));
			}
			if (idx == 0)
				ImGui::SetItemDefaultFocus();
			if (clicked)
				chosen = idx;

			// Shortcut letter overlay — draw AFTER the button so it
			// appears on top of the image.  Hidden in Gaming Mode
			// (touch users don't have a keyboard; adding the letter
			// just clutters the tile).
			if (!gaming) {
				char letter[2] = { (char)('A' + idx), 0 };
				ImDrawList *dl = ImGui::GetWindowDrawList();
				const ImVec2 textPos(itemMin.x + framePadX + 4.0f,
				                     itemMin.y + framePadY + 2.0f);
				// Dark backdrop for legibility on bright icons.
				ImVec2 textSize = ImGui::CalcTextSize(letter);
				dl->AddRectFilled(
					ImVec2(textPos.x - 2.0f, textPos.y - 1.0f),
					ImVec2(textPos.x + textSize.x + 2.0f,
					       textPos.y + textSize.y + 1.0f),
					IM_COL32(0, 0, 0, 170), 3.0f);
				dl->AddText(textPos, IM_COL32(255, 255, 255, 230), letter);
			}

			ImGui::PopID();
		}
	}

	ImGui::PopStyleVar();

	// --- 6. Cancel button --------------------------------------------
	ImGui::Spacing();
	if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, cancelH))) {
		CloseSequence();
		return;
	}

	// --- 7. Gamepad B / NavCancel ------------------------------------
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
		CloseSequence();
		return;
	}

	// --- 8. Selection --------------------------------------------------
	if (letterChosen >= 0) {
		ATEmoteNetplay::Send(letterChosen);
		CloseSequence();
		return;
	}
	if (chosen >= 0) {
		ATEmoteNetplay::Send(chosen);
		CloseSequence();
		return;
	}

	ImGui::EndPopup();
}

} // namespace ATEmotePicker
