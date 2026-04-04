//	AltirraSDL - ImGui emulation error dialog
//	Replaces Windows IDD_PROGRAM_ERROR / ATUIDialogEmuError / ATEmuErrorHandler.
//
//	When the emulated CPU encounters an illegal state, the debugger fires
//	OnDebuggerOpen.  This handler intercepts the event, pauses the simulator,
//	and sets a flag so the ImGui dialog renders on the next frame.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/event.h>

#include "ui_emuerror.h"
#include "ui_main.h"
#include "simulator.h"
#include "constants.h"
#include "cpu.h"
#include "options.h"
#include "debugger.h"

// Debugger open/close from ui_debugger.cpp
extern void ATUIDebuggerOpen();

///////////////////////////////////////////////////////////////////////////
// Dialog state
///////////////////////////////////////////////////////////////////////////

static bool g_showEmuError = false;
static bool g_emuErrorNeedOpen = false;
static bool g_emuErrorRequestDebugger = false;

// Checkbox states — set when dialog opens based on hardware config
static bool g_chkHardware = false;
static bool g_chkFirmware = false;
static bool g_chkMemory = false;
static bool g_chkVideo = false;
static bool g_chkBasic = false;
static bool g_chkCPU = false;
static bool g_chkDebugging = false;
static bool g_chkDiskIO = false;

// Whether each checkbox is enabled (relevant given current config)
static bool g_enHardware = false;
static bool g_enFirmware = false;
static bool g_enMemory = false;
static bool g_enVideo = false;
static bool g_enBasic = false;
static bool g_enCPU = false;
static bool g_enDebugging = false;
static bool g_enDiskIO = false;

// Computed target values for changes
static bool g_newPALMode = false;

///////////////////////////////////////////////////////////////////////////
// Populate checkbox enabled states from current hardware config
// (mirrors ATUIDialogEmuError::OnDataExchange in uiemuerror.cpp)
///////////////////////////////////////////////////////////////////////////

static void PopulateCheckboxStates(ATSimulator &sim) {
	// Reset all checkboxes to unchecked
	g_chkHardware = g_chkFirmware = g_chkMemory = g_chkVideo = false;
	g_chkBasic = g_chkCPU = g_chkDebugging = g_chkDiskIO = false;

	ATHardwareMode hw = sim.GetHardwareMode();

	// Hardware mode — enable if not already 800XL or 5200
	switch (hw) {
		case kATHardwareMode_800:
		case kATHardwareMode_1200XL:
		case kATHardwareMode_XEGS:
		case kATHardwareMode_130XE:
		case kATHardwareMode_1400XL:
			g_enHardware = true;
			break;
		default:
			g_enHardware = false;
			break;
	}

	// Firmware — enable if not 5200 and kernel not Default/XL
	if (hw != kATHardwareMode_5200) {
		switch (sim.GetKernelMode()) {
			case kATKernelMode_Default:
			case kATKernelMode_XL:
				g_enFirmware = false;
				break;
			default:
				g_enFirmware = true;
				break;
		}
	} else {
		g_enFirmware = false;
	}

	// Memory — enable if not 5200 and not already 320K
	g_enMemory = (hw != kATHardwareMode_5200) && (sim.GetMemoryMode() != kATMemoryMode_320K);

	// Video — enable if not 5200
	g_enVideo = (hw != kATHardwareMode_5200);
	g_newPALMode = (sim.GetVideoStandard() == kATVideoStandard_NTSC);

	// BASIC
	g_enBasic = sim.IsBASICEnabled();

	// CPU
	ATCPUEmulator &cpu = sim.GetCPU();
	g_enCPU = (cpu.GetCPUMode() != kATCPUMode_6502);

	// Debugging
	g_enDebugging = cpu.IsPathBreakEnabled() || !cpu.AreIllegalInsnsEnabled() || cpu.GetStopOnBRK();

	// Disk I/O
	g_enDiskIO = !sim.IsDiskAccurateTimingEnabled() || sim.IsDiskSIOPatchEnabled();
}

///////////////////////////////////////////////////////////////////////////
// Apply checked changes (mirrors ATUIDialogEmuError::OnOK)
///////////////////////////////////////////////////////////////////////////

static void ApplyChanges(ATSimulator &sim) {
	if (g_chkHardware && g_enHardware) {
		sim.SetHardwareMode(kATHardwareMode_800XL);

		if (kATHardwareModeTraits[kATHardwareMode_800XL].mbRunsXLOS) {
			switch (sim.GetMemoryMode()) {
				case kATMemoryMode_8K:
				case kATMemoryMode_24K:
				case kATMemoryMode_32K:
				case kATMemoryMode_40K:
				case kATMemoryMode_48K:
				case kATMemoryMode_52K:
					sim.SetMemoryMode(kATMemoryMode_64K);
					break;
				default:
					break;
			}
		}
	}

	if (g_chkMemory && g_enMemory)
		sim.SetMemoryMode(kATMemoryMode_320K);

	if (g_chkFirmware && g_enFirmware)
		sim.SetKernel(0);

	if (g_chkVideo && g_enVideo)
		sim.SetVideoStandard(g_newPALMode ? kATVideoStandard_PAL : kATVideoStandard_NTSC);

	if (g_chkBasic && g_enBasic)
		sim.SetBASICEnabled(false);

	ATCPUEmulator &cpu = sim.GetCPU();
	if (g_chkCPU && g_enCPU)
		sim.SetCPUMode(kATCPUMode_6502, 1);

	if (g_chkDebugging && g_enDebugging) {
		cpu.SetIllegalInsnsEnabled(true);
		cpu.SetPathBreakEnabled(false);
		cpu.SetStopOnBRK(false);
	}

	if (g_chkDiskIO && g_enDiskIO) {
		sim.SetDiskSIOPatchEnabled(false);
		sim.SetDiskAccurateTimingEnabled(true);
	}
}

///////////////////////////////////////////////////////////////////////////
// Error handler — hooks into debugger OnDebuggerOpen event
///////////////////////////////////////////////////////////////////////////

class ATEmuErrorHandlerSDL3 {
public:
	void Init(ATSimulator *sim);
	void Shutdown();

private:
	void OnDebuggerOpen(IATDebugger *dbg, ATDebuggerOpenEvent *event);

	ATSimulator *mpSim = nullptr;
	VDDelegate mDelDebuggerOpen;
};

void ATEmuErrorHandlerSDL3::Init(ATSimulator *sim) {
	mpSim = sim;
	ATGetDebugger()->OnDebuggerOpen() += mDelDebuggerOpen.Bind(this, &ATEmuErrorHandlerSDL3::OnDebuggerOpen);
}

void ATEmuErrorHandlerSDL3::Shutdown() {
	ATGetDebugger()->OnDebuggerOpen() -= mDelDebuggerOpen;
	mpSim = nullptr;
}

void ATEmuErrorHandlerSDL3::OnDebuggerOpen(IATDebugger *, ATDebuggerOpenEvent *event) {
	if (!mpSim)
		return;

	extern ATOptions g_ATOptions;

	switch (g_ATOptions.mErrorMode) {
		case kATErrorMode_Dialog:
			break;

		case kATErrorMode_Debug:
			// Allow debugger to open
			return;

		case kATErrorMode_Pause:
			mpSim->Pause();
			event->mbAllowOpen = false;
			return;

		case kATErrorMode_ColdReset:
			mpSim->ColdReset();
			mpSim->Resume();
			event->mbAllowOpen = false;
			return;
	}

	// Show the ImGui error dialog.  Since we're inside sim.Advance(),
	// we can't render ImGui here — just set state and return.
	// The dialog will be drawn on the next frame.
	mpSim->Pause();
	PopulateCheckboxStates(*mpSim);
	g_showEmuError = true;
	g_emuErrorNeedOpen = true;
	event->mbAllowOpen = false;
}

static ATEmuErrorHandlerSDL3 g_emuErrorHandler;

void ATInitEmuErrorHandlerSDL3(ATSimulator *sim) {
	g_emuErrorHandler.Init(sim);
}

void ATShutdownEmuErrorHandlerSDL3() {
	g_emuErrorHandler.Shutdown();
}

///////////////////////////////////////////////////////////////////////////
// ImGui dialog render (called each frame from ATUIRenderFrame)
///////////////////////////////////////////////////////////////////////////

void ATUIRenderEmuErrorDialog(ATSimulator &sim) {
	// Check if debug action was requested last frame
	if (g_emuErrorRequestDebugger) {
		g_emuErrorRequestDebugger = false;
		ATUIDebuggerOpen();
	}

	if (!g_showEmuError)
		return;

	if (g_emuErrorNeedOpen) {
		ImGui::OpenPopup("Program Error");
		g_emuErrorNeedOpen = false;
	}

	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Program Error", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextWrapped(
			"The emulated computer has stopped due to a program error.\n"
			"You can try changing one of these options and restarting:");
		ImGui::Spacing();

		// Hardware
		ImGui::BeginDisabled(!g_enHardware);
		ImGui::Checkbox("Change hardware mode to XL/XE", &g_chkHardware);
		ImGui::EndDisabled();

		// Firmware
		ImGui::BeginDisabled(!g_enFirmware);
		ImGui::Checkbox("Change firmware to Default", &g_chkFirmware);
		ImGui::EndDisabled();

		// Memory
		ImGui::BeginDisabled(!g_enMemory);
		ImGui::Checkbox("Change memory to 320K", &g_chkMemory);
		ImGui::EndDisabled();

		// Video
		ImGui::BeginDisabled(!g_enVideo);
		if (g_newPALMode)
			ImGui::Checkbox("Change video standard to PAL", &g_chkVideo);
		else
			ImGui::Checkbox("Change video standard to NTSC", &g_chkVideo);
		ImGui::EndDisabled();

		// BASIC
		ImGui::BeginDisabled(!g_enBasic);
		ImGui::Checkbox("Disable BASIC", &g_chkBasic);
		ImGui::EndDisabled();

		// CPU
		ImGui::BeginDisabled(!g_enCPU);
		ImGui::Checkbox("Disable CPU diagnostics (use 6502)", &g_chkCPU);
		ImGui::EndDisabled();

		// Debugging
		ImGui::BeginDisabled(!g_enDebugging);
		ImGui::Checkbox("Disable debugging options", &g_chkDebugging);
		ImGui::EndDisabled();

		// Disk I/O
		ImGui::BeginDisabled(!g_enDiskIO);
		ImGui::Checkbox("Enable accurate floppy disk timing", &g_chkDiskIO);
		ImGui::EndDisabled();

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Spacing();

		// Buttons — Debug | Warm Reset | Restart (default) | Pause
		if (ImGui::Button("Debug", ImVec2(100, 0))) {
			g_showEmuError = false;
			g_emuErrorRequestDebugger = true;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Warm Reset", ImVec2(100, 0))) {
			g_showEmuError = false;
			sim.WarmReset();
			sim.Resume();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Restart", ImVec2(100, 0))) {
			g_showEmuError = false;
			ApplyChanges(sim);
			sim.ColdReset();
			sim.Resume();
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Pause", ImVec2(100, 0))) {
			g_showEmuError = false;
			// Sim is already paused
			ImGui::CloseCurrentPopup();
		}

		// ESC closes as Pause
		if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
			g_showEmuError = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}
}
