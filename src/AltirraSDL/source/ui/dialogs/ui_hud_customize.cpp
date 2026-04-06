//	AltirraSDL - HUD customization dialog
//	Replicates Windows Altirra's View > Customize HUD feature.
//	In the Windows version, this opens an interactive overlay where the user
//	can drag HUD widgets around.  The SDL3 ImGui HUD has fixed-position
//	elements, so we present a dialog with visibility toggles and position
//	options matching what the Windows version exposes.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/registry.h>
#include "ui_main.h"
#include "simulator.h"
#include "uirender.h"

extern ATSimulator g_sim;

// =========================================================================
// HUD element visibility flags — stored in registry under "HUD" key
// ATHudSettings struct defined in ui_main.h
// =========================================================================

static ATHudSettings s_hudSettings;
static bool s_hudSettingsLoaded = false;

static void LoadHudSettings() {
	VDRegistryKey key("HUD", false);
	s_hudSettings.showDiskLEDs      = key.getBool("ShowDiskLEDs", true);
	s_hudSettings.showHActivity     = key.getBool("ShowHActivity", true);
	s_hudSettings.showCassette      = key.getBool("ShowCassette", true);
	s_hudSettings.showRecording     = key.getBool("ShowRecording", true);
	s_hudSettings.showFPS           = key.getBool("ShowFPS", true);
	s_hudSettings.showWatches       = key.getBool("ShowWatches", true);
	s_hudSettings.showStatusMessage = key.getBool("ShowStatusMessage", true);
	s_hudSettings.showErrors        = key.getBool("ShowErrors", true);
	s_hudSettings.showPauseOverlay  = key.getBool("ShowPauseOverlay", true);
	s_hudSettings.showHeldButtons   = key.getBool("ShowHeldButtons", true);
	s_hudSettings.showAudioScope    = key.getBool("ShowAudioScope", false);
	s_hudSettingsLoaded = true;
}

static void SaveHudSettings() {
	VDRegistryKey key("HUD", true);
	key.setBool("ShowDiskLEDs",      s_hudSettings.showDiskLEDs);
	key.setBool("ShowHActivity",     s_hudSettings.showHActivity);
	key.setBool("ShowCassette",      s_hudSettings.showCassette);
	key.setBool("ShowRecording",     s_hudSettings.showRecording);
	key.setBool("ShowFPS",           s_hudSettings.showFPS);
	key.setBool("ShowWatches",       s_hudSettings.showWatches);
	key.setBool("ShowStatusMessage", s_hudSettings.showStatusMessage);
	key.setBool("ShowErrors",        s_hudSettings.showErrors);
	key.setBool("ShowPauseOverlay",  s_hudSettings.showPauseOverlay);
	key.setBool("ShowHeldButtons",   s_hudSettings.showHeldButtons);
	key.setBool("ShowAudioScope",    s_hudSettings.showAudioScope);
}

// Public accessor — called by ui_indicators.cpp to check visibility
const ATHudSettings& ATUIGetHudSettings() {
	if (!s_hudSettingsLoaded)
		LoadHudSettings();
	return s_hudSettings;
}

// =========================================================================
// Dialog rendering
// =========================================================================

void ATUIRenderCustomizeHudDialog(ATUIState &state) {
	if (!s_hudSettingsLoaded)
		LoadHudSettings();

	ImGui::SetNextWindowSize(ImVec2(380, 420), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showCustomizeHud;
	if (!ImGui::Begin("Customize HUD", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		state.showCustomizeHud = open;
		return;
	}

	if (ATUICheckEscClose())
		open = false;

	ImGui::TextWrapped("Select which HUD elements are displayed during emulation.");
	ImGui::Separator();
	ImGui::Spacing();

	bool changed = false;

	ImGui::Text("Status Bar");
	ImGui::Indent();
	changed |= ImGui::Checkbox("Disk Drive LEDs", &s_hudSettings.showDiskLEDs);
	changed |= ImGui::Checkbox("H:/IDE/PCLink Activity", &s_hudSettings.showHActivity);
	changed |= ImGui::Checkbox("Cassette Position", &s_hudSettings.showCassette);
	changed |= ImGui::Checkbox("Recording Indicator", &s_hudSettings.showRecording);
	ImGui::Unindent();

	ImGui::Spacing();
	ImGui::Text("Overlay");
	ImGui::Indent();
	changed |= ImGui::Checkbox("FPS Counter", &s_hudSettings.showFPS);
	changed |= ImGui::Checkbox("Watch Values", &s_hudSettings.showWatches);
	changed |= ImGui::Checkbox("Status Messages", &s_hudSettings.showStatusMessage);
	changed |= ImGui::Checkbox("Error Messages", &s_hudSettings.showErrors);
	changed |= ImGui::Checkbox("Pause Overlay", &s_hudSettings.showPauseOverlay);
	changed |= ImGui::Checkbox("Held Keys/Buttons", &s_hudSettings.showHeldButtons);
	ImGui::Unindent();

	ImGui::Spacing();
	ImGui::Text("Audio");
	ImGui::Indent();
	changed |= ImGui::Checkbox("Audio Scope/Waveform", &s_hudSettings.showAudioScope);
	ImGui::Unindent();

	if (changed)
		SaveHudSettings();

	ImGui::Spacing();
	ImGui::Separator();

	float buttonW = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonW - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("Close", ImVec2(buttonW, 0)))
		open = false;

	ImGui::End();
	state.showCustomizeHud = open;
}
