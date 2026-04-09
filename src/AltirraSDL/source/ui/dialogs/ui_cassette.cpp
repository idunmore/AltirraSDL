//	AltirraSDL - Tape Control dialog
//	Mirrors Windows Altirra's Tape Control dialog (IDD_TAPE_CONTROL).
//	File menu (New/Load/Save/Save As/Unload), resizable window,
//	transport controls (Stop/Pause/Play/SeekStart/SeekEnd/Record) and
//	position slider.  Turbo defaults live in Configure System > Cassette.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include "ui_file_dialog_sdl3.h"
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include <SDL3/SDL_dialog.h>

#include "ui_main.h"
#include "simulator.h"
#include "cassette.h"

extern ATSimulator g_sim;

// Shared cassette save file filter (mirrors Windows File > Save As).
static const SDL_DialogFileFilter kTapeCtrlSaveFilters[] = {
	{ "Cassette Audio (*.wav)", "wav" },
	{ "Cassette Tape (*.cas)", "cas" },
};

static const SDL_DialogFileFilter kTapeCtrlLoadFilters[] = {
	{ "Cassette Images", "cas;wav" },
	{ "All Files", "*" },
};

static void TapeCtrlSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveCassette, filelist[0]);
}

static void TapeCtrlLoadCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_LoadCassette, filelist[0]);
}

// Format time as M:SS.d or H:MM:SS.d (matching Windows uitapecontrol.cpp)
static void FormatTapeTime(char *buf, size_t bufSize, float seconds) {
	if (seconds < 0.0f) seconds = 0.0f;
	int tenths = (int)(seconds * 10.0f) % 10;
	int totalSec = (int)seconds;
	int sec = totalSec % 60;
	int totalMin = totalSec / 60;
	if (totalMin >= 60) {
		int hrs = totalMin / 60;
		int min = totalMin % 60;
		snprintf(buf, bufSize, "%d:%02d:%02d.%d", hrs, min, sec, tenths);
	} else {
		snprintf(buf, bufSize, "%d:%02d.%d", totalMin, sec, tenths);
	}
}

void ATUIRenderCassetteControl(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	ImGui::SetNextWindowSize(ImVec2(460, 170), ImGuiCond_Appearing);
	ImGui::SetNextWindowSizeConstraints(ImVec2(360, 150), ImVec2(FLT_MAX, FLT_MAX));
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	const ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
	if (!ImGui::Begin("Cassette Tape Control", &state.showCassetteControl, kFlags)) {
		ImGui::End();
		return;
	}

	ATCassetteEmulator& cas = sim.GetCassette();
	bool loaded = cas.IsLoaded();

	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("File")) {
			if (ImGui::MenuItem("New"))
				cas.LoadNew();

			if (ImGui::MenuItem("Load...")) {
				ATUIShowOpenFileDialog('cass', TapeCtrlLoadCallback, nullptr, window,
					kTapeCtrlLoadFilters, 2, false);
			}

			if (ImGui::MenuItem("Save", nullptr, false, loaded)) {
				// Mirror Windows File > Save: if we have a path, overwrite; otherwise prompt.
				ATUIShowSaveFileDialog('cass', TapeCtrlSaveCallback, nullptr, window,
					kTapeCtrlSaveFilters, 2);
			}

			if (ImGui::MenuItem("Save As...", nullptr, false, loaded)) {
				ATUIShowSaveFileDialog('cass', TapeCtrlSaveCallback, nullptr, window,
					kTapeCtrlSaveFilters, 2);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Unload", nullptr, false, loaded)) {
				cas.Unload();
				loaded = false;
			}

			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	if (ATUICheckEscClose()) {
		state.showCassetteControl = false;
		ImGui::End();
		return;
	}

	// Status text (matches Windows IDC_PEAK_IMAGE placeholder)
	if (!loaded) {
		ImGui::TextDisabled("There is no tape in the cassette tape drive.");
	}

	// Position slider
	if (loaded) {
		float pos = cas.GetPosition();
		float len = cas.GetLength();
		if (len <= 0.0f) len = 1.0f;

		// Position label: "M:SS.d / M:SS.d" (matching Windows format)
		char posStr[32], lenStr[32], label[80];
		FormatTapeTime(posStr, sizeof(posStr), pos);
		FormatTapeTime(lenStr, sizeof(lenStr), len);
		snprintf(label, sizeof(label), "%s / %s", posStr, lenStr);

		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::SliderFloat("##TapePos", &pos, 0.0f, len, label))
			cas.SeekToTime(pos);
	}

	// Transport controls (matching Windows: Stop, Pause, Play, SeekStart, SeekEnd, Record)
	// Windows uses Webdings font icons; we use text labels with color for active state.
	bool playing = loaded && cas.IsPlayEnabled();
	bool recording = loaded && cas.IsRecordEnabled();
	bool paused = loaded && cas.IsPaused();
	bool stopped = !loaded || cas.IsStopped();

	float btnW = 50.0f;
	ImVec4 activeColor(0.2f, 0.7f, 0.2f, 1.0f);  // green tint for active

	// Stop
	if (stopped) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
	if (ImGui::Button("Stop", ImVec2(btnW, 0)) && loaded && !stopped)
		cas.Stop();
	if (stopped) ImGui::PopStyleColor();
	ImGui::SameLine();

	// Pause
	if (paused) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
	if (ImGui::Button("Pause", ImVec2(btnW, 0)) && loaded)
		cas.SetPaused(!paused);
	if (paused) ImGui::PopStyleColor();
	ImGui::SameLine();

	// Play
	if (playing && !paused) ImGui::PushStyleColor(ImGuiCol_Button, activeColor);
	if (ImGui::Button("Play", ImVec2(btnW, 0)) && loaded && !playing)
		cas.Play();
	if (playing && !paused) ImGui::PopStyleColor();
	ImGui::SameLine();

	// Seek Start (rewind to beginning)
	if (ImGui::Button("|<", ImVec2(34.0f, 0)) && loaded)
		cas.SeekToTime(0.0f);
	ImGui::SameLine();

	// Seek End
	if (ImGui::Button(">|", ImVec2(34.0f, 0)) && loaded)
		cas.SeekToTime(cas.GetLength());
	ImGui::SameLine();

	// Record
	if (recording) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
	if (ImGui::Button("Rec", ImVec2(btnW, 0)) && loaded)
		cas.Record();
	if (recording) ImGui::PopStyleColor();

	ImGui::End();
}
