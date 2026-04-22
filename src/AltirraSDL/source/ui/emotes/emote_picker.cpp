//	AltirraSDL - Online Play emote picker.
//
//	Modal popup: 4×4 grid + big Cancel button.  Sized responsively so it
//	fits a 360 px phone portrait up to a desktop fullscreen.  Works with
//	mouse, touch, keyboard (arrows + Enter + Esc), and gamepad (D-pad +
//	A + B) in both Desktop UI and Gaming Mode.  Desktop's ImGui nav
//	flags are off by default — we enable them while the picker is open
//	and restore on close.

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
constexpr const char *kPopupId = "Emote##emote_picker";

enum class OpenRequest { None, Open, Close };

OpenRequest gRequest = OpenRequest::None;
bool        gPopupOpen = false;  // tracks ImGui popup state across frames
bool        gNavFlagsPushed = false;
ImGuiConfigFlags gPrevNavFlags = 0;

bool IsGamingMode() {
	// FontGlobalScale is bumped ≥ ~1.25 when Gaming Mode is active;
	// keeps this module decoupled from the mobile UI module.
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

} // namespace

void Open() {
	if (!ATNetplayGlue::IsLockstepping()) return;
	if (!ATEmoteNetplay::GetSendEnabled()) return;
	if (!ATEmotes::IsReady()) return;
	// Defer to Render() — OpenPopup() must be called from inside the
	// ImGui frame, not from an event handler that may run before
	// NewFrame().  Also makes Open() idempotent within a single frame.
	gRequest = OpenRequest::Open;
}

void Close() {
	gRequest = OpenRequest::Close;
}

bool IsOpen() {
	return gPopupOpen;
}

void Render() {
	// --- 1. Drive the open/close request -------------------------------
	if (gRequest == OpenRequest::Open && !gPopupOpen) {
		ImGui::OpenPopup(kPopupId);
		gPopupOpen = true;
		PushNavFlags();
	}
	gRequest = OpenRequest::None;

	if (!gPopupOpen)
		return;

	// --- 2. Force-close on state change --------------------------------
	// Netplay session ended or send-emotes toggled off while we were
	// open: drop the popup without going through BeginPopupModal so the
	// user never sees a flicker.
	bool mustClose =
		!ATNetplayGlue::IsLockstepping() ||
		!ATEmoteNetplay::GetSendEnabled();

	// --- 3. Responsive sizing ------------------------------------------
	ImGuiViewport *vp = ImGui::GetMainViewport();
	float vw = vp->Size.x;
	float vh = vp->Size.y;
	const bool gaming = IsGamingMode();

	// Leave 5% margin in each dimension so the popup never touches the
	// viewport edge even on small phone portrait screens.
	float maxW = vw * 0.90f;
	float maxH = vh * 0.85f;

	float pad     = gaming ? 12.0f : 8.0f;
	float titleH  = ImGui::GetFrameHeightWithSpacing();
	float cancelH = gaming ? 56.0f : 40.0f;  // touch-friendly height
	float frame   = ImGui::GetStyle().FramePadding.y * 2.0f;

	// Total non-grid height: title + padding + cancel button + paddings.
	float overhead = titleH + pad * 3.0f + cancelH + frame;

	float tileFromW = (maxW - (kCols + 1) * pad) / (float)kCols;
	float tileFromH = (maxH - overhead - (kRows + 1) * pad) / (float)kRows;
	float tileSize = std::min(tileFromW, tileFromH);

	// Clamp: don't let it shrink below a hit-target floor, don't let it
	// balloon past a comfortable max.
	float minTile = gaming ? 56.0f : 40.0f;
	float maxTile = gaming ? 128.0f : 96.0f;
	tileSize = std::max(minTile, std::min(maxTile, tileSize));

	float gridW = (float)kCols * tileSize + (kCols + 1) * pad;
	float gridH = (float)kRows * tileSize + (kRows + 1) * pad;
	float winW  = gridW;
	float winH  = gridH + overhead;

	ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Always);
	ImGui::SetNextWindowPos(
		ImVec2(vp->Pos.x + vw * 0.5f, vp->Pos.y + vh * 0.5f),
		ImGuiCond_Always, ImVec2(0.5f, 0.5f));

	ImGuiWindowFlags flags =
		  ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoDocking;

	// Modal popup: dims + blocks background interaction.  Esc is
	// handled automatically by ImGui.  Click-outside does NOT close
	// (modal behavior), so we rely on the explicit Cancel button +
	// Esc + gamepad B + the netplay-ended force-close above.
	if (!ImGui::BeginPopupModal(kPopupId, nullptr, flags)) {
		// Popup closed behind our back (e.g. user hit Esc, ImGui
		// already tore it down).  Reset local state and restore nav
		// flags.
		gPopupOpen = false;
		PopNavFlags();
		return;
	}

	if (mustClose) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		gPopupOpen = false;
		PopNavFlags();
		return;
	}

	// --- 4. Grid of 4×4 image buttons ----------------------------------
	int chosen = -1;
	// Push item spacing so ImGui::SameLine's default gap is replaced by
	// our `pad` pixels horizontally and a vertical gap between rows.
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(pad, pad));

	for (int row = 0; row < kRows; ++row) {
		for (int col = 0; col < kCols; ++col) {
			int idx = row * kCols + col;
			if (col > 0)
				ImGui::SameLine();

			ImGui::PushID(idx);

			ImTextureID tex = ATEmotes::GetTexture(idx);
			bool clicked;
			if (tex) {
				clicked = ImGui::ImageButton("##tile", tex,
					ImVec2(tileSize, tileSize),
					ImVec2(0, 0), ImVec2(1, 1),
					ImVec4(0, 0, 0, 0),    // bg (transparent)
					ImVec4(1, 1, 1, 1));   // tint
			} else {
				clicked = ImGui::Button("?", ImVec2(tileSize, tileSize));
			}
			// SetItemDefaultFocus must run AFTER the item it focuses.
			// Gives keyboard arrows / D-pad a starting tile on open.
			if (idx == 0)
				ImGui::SetItemDefaultFocus();
			if (clicked)
				chosen = idx;
			ImGui::PopID();
		}
	}

	ImGui::PopStyleVar();

	// --- 5. Cancel button (full-width, touch-sized) --------------------
	ImGui::Spacing();
	// -FLT_MIN tells ImGui::Button to fill the available horizontal
	// space, which is the content region (grid width minus the window's
	// inner padding).  Hard-coding gridW would overflow the padding and
	// clip the button's right edge.
	if (ImGui::Button("Cancel", ImVec2(-FLT_MIN, cancelH))) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		gPopupOpen = false;
		PopNavFlags();
		return;
	}

	// --- 6. Gamepad B / controller-cancel -----------------------------
	// BeginPopupModal handles Esc automatically, but NavCancel on the
	// gamepad (B / FaceRight) does NOT close the popup on its own when
	// we're on an ImageButton — we forward it explicitly.
	if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		gPopupOpen = false;
		PopNavFlags();
		return;
	}

	// --- 7. Selection --------------------------------------------------
	if (chosen >= 0) {
		ATEmoteNetplay::Send(chosen);
		ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
		gPopupOpen = false;
		PopNavFlags();
		return;
	}

	ImGui::EndPopup();
}

} // namespace ATEmotePicker
