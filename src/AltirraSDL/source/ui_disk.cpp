//	AltirraSDL - Disk Drive Manager dialog
//	Mirrors Windows Altirra's Disk Drives dialog (IDD_DISK_DRIVES):
//	per-drive path, write mode, browse/eject/context-menu buttons,
//	Drives 1-8 / 9-15 tabs, emulation level, OK.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <at/atcore/media.h>

#include "ui_main.h"
#include "simulator.h"
#include "diskinterface.h"
#include "disk.h"
#include <at/atio/diskimage.h>

extern ATSimulator g_sim;

// =========================================================================
// Create Disk format types (matches Windows ATNewDiskDialog)
// =========================================================================

struct DiskFormatType {
	uint32 sectorSize;
	uint32 sectorCount;
	const char *label;
};

static const DiskFormatType kDiskFormatTypes[] = {
	{ 0,     0, "Custom" },
	{ 128,  720, "Single density (720 sectors, 128 B/sec)" },
	{ 128, 1040, "Medium density (1040 sectors, 128 B/sec)" },
	{ 256,  720, "Double density (720 sectors, 256 B/sec)" },
	{ 256, 1440, "Double-sided DD (1440 sectors, 256 B/sec)" },
	{ 256, 2880, "DSDD 80 tracks (2880 sectors, 256 B/sec)" },
	{ 128, 2002, "8\" single-sided (2002 sectors, 128 B/sec)" },
	{ 128, 4004, "8\" double-sided (4004 sectors, 128 B/sec)" },
	{ 256, 2002, "8\" single-sided DD (2002 sectors, 256 B/sec)" },
	{ 256, 4004, "8\" double-sided DD (4004 sectors, 256 B/sec)" },
};

static constexpr int kNumDiskFormats = (int)(sizeof(kDiskFormatTypes) / sizeof(kDiskFormatTypes[0]));

static struct {
	bool show = false;
	int targetDrive = 0;
	int formatIndex = 1;     // default: single density
	int sectorCount = 720;
	int bootSectorCount = 3;
	int sectorSize = 128;    // actual byte value: 128, 256, or 512
} g_createDiskState;

static void RenderCreateDiskDialog() {
	if (!g_createDiskState.show)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 260), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Create Disk", &g_createDiskState.show, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	// Format combo
	if (ImGui::Combo("Format", &g_createDiskState.formatIndex,
		[](void *, int idx) -> const char * { return kDiskFormatTypes[idx].label; },
		nullptr, kNumDiskFormats))
	{
		// Update geometry from preset (unless Custom)
		if (g_createDiskState.formatIndex > 0) {
			const auto& fmt = kDiskFormatTypes[g_createDiskState.formatIndex];
			g_createDiskState.sectorCount = (int)fmt.sectorCount;
			g_createDiskState.sectorSize = (int)fmt.sectorSize;
			g_createDiskState.bootSectorCount = 3;
		}
	}

	bool isCustom = (g_createDiskState.formatIndex == 0);

	// Sector count (editable in Custom mode)
	ImGui::BeginDisabled(!isCustom);
	ImGui::InputInt("Sector Count", &g_createDiskState.sectorCount);
	if (g_createDiskState.sectorCount < 1) g_createDiskState.sectorCount = 1;
	if (g_createDiskState.sectorCount > 65535) g_createDiskState.sectorCount = 65535;
	ImGui::EndDisabled();

	// Sector size radio buttons (editable in Custom mode)
	ImGui::BeginDisabled(!isCustom);
	ImGui::Text("Sector Size:");
	ImGui::SameLine();
	int sectorSizeIdx = g_createDiskState.sectorSize == 256 ? 1 :
	                     g_createDiskState.sectorSize == 512 ? 2 : 0;
	if (ImGui::RadioButton("128", sectorSizeIdx == 0)) g_createDiskState.sectorSize = 128;
	ImGui::SameLine();
	if (ImGui::RadioButton("256", sectorSizeIdx == 1)) g_createDiskState.sectorSize = 256;
	ImGui::SameLine();
	if (ImGui::RadioButton("512", sectorSizeIdx == 2)) g_createDiskState.sectorSize = 512;
	ImGui::EndDisabled();

	// Boot sector count
	ImGui::InputInt("Boot Sectors", &g_createDiskState.bootSectorCount);
	if (g_createDiskState.bootSectorCount < 0) g_createDiskState.bootSectorCount = 0;
	if (g_createDiskState.bootSectorCount > 255) g_createDiskState.bootSectorCount = 255;

	ImGui::Separator();

	if (ImGui::Button("Create", ImVec2(80, 0))) {
		ATDiskInterface& di = g_sim.GetDiskInterface(g_createDiskState.targetDrive);
		di.CreateDisk(
			(uint32)g_createDiskState.sectorCount,
			(uint32)g_createDiskState.bootSectorCount,
			(uint32)g_createDiskState.sectorSize);
		g_createDiskState.show = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(80, 0)))
		g_createDiskState.show = false;

	ImGui::End();
}

// =========================================================================
// File dialog callbacks — drive index passed via userdata
// =========================================================================

static void DiskMountCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;

	VDStringW widePath = VDTextU8ToW(filelist[0], -1);
	try {
		g_sim.GetDiskInterface(driveIdx).LoadDisk(widePath.c_str());
		fprintf(stderr, "[AltirraSDL] Mounted D%d: %s\n", driveIdx + 1, filelist[0]);
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Failed to mount D%d: %s\n", driveIdx + 1, filelist[0]);
	}
}

static void DiskSaveAsCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;

	VDStringW widePath = VDTextU8ToW(filelist[0], -1);
	try {
		g_sim.GetDiskInterface(driveIdx).SaveDiskAs(widePath.c_str(), kATDiskImageFormat_ATR);
		fprintf(stderr, "[AltirraSDL] Saved D%d as: %s\n", driveIdx + 1, filelist[0]);
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Failed to save D%d: %s\n", driveIdx + 1, filelist[0]);
	}
}

static const SDL_DialogFileFilter kDiskFilters[] = {
	{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kDiskSaveFilters[] = {
	{ "ATR Disk Image", "atr" },
	{ "XFD Disk Image", "xfd" },
	{ "All Files", "*" },
};

// =========================================================================
// Emulation mode labels (matches Windows dialog order)
// =========================================================================

static const ATDiskEmulationMode kEmuModeValues[] = {
	kATDiskEmulationMode_Generic,
	kATDiskEmulationMode_Generic57600,
	kATDiskEmulationMode_FastestPossible,
	kATDiskEmulationMode_810,
	kATDiskEmulationMode_1050,
	kATDiskEmulationMode_XF551,
	kATDiskEmulationMode_USDoubler,
	kATDiskEmulationMode_Speedy1050,
	kATDiskEmulationMode_IndusGT,
	kATDiskEmulationMode_Happy810,
	kATDiskEmulationMode_Happy1050,
	kATDiskEmulationMode_1050Turbo,
};
static const char *kEmuModeLabels[] = {
	"Generic",
	"Generic + 57600 baud",
	"Fastest Possible",
	"810",
	"1050",
	"XF551",
	"US Doubler",
	"Speedy 1050",
	"Indus GT",
	"Happy 810",
	"Happy 1050",
	"1050 Turbo",
};
static const int kNumEmuModes = 12;

// Write mode labels (matches Windows: Off/R-O/VRWSafe/VRW/R-W)
static const char *kWriteModeLabels[] = {
	"Off", "R/O", "VRWSafe", "VRW", "R/W"
};
static const ATMediaWriteMode kWriteModeValues[] = {
	kATMediaWriteMode_RO,       // placeholder for Off
	kATMediaWriteMode_RO,
	kATMediaWriteMode_VRWSafe,
	kATMediaWriteMode_VRW,
	kATMediaWriteMode_RW,
};

static int GetWriteModeIndex(ATDiskInterface& di) {
	if (!di.IsDiskLoaded()) return 0;
	switch (di.GetWriteMode()) {
	case kATMediaWriteMode_RO:       return 1;
	case kATMediaWriteMode_VRWSafe:  return 2;
	case kATMediaWriteMode_VRW:      return 3;
	case kATMediaWriteMode_RW:       return 4;
	default:                         return 1;
	}
}

// =========================================================================
// Render
// =========================================================================

void ATUIRenderDiskManager(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	ImGui::SetNextWindowSize(ImVec2(720, 460), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Disk drives", &state.showDiskManager, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	// --- Drives 1-8 / 9-15 tab selector (matches Windows radio buttons) ---
	static int driveTab = 0;  // 0 = drives 1-8, 1 = drives 9-15
	if (ImGui::RadioButton("Drives 1-8", driveTab == 0)) driveTab = 0;
	ImGui::SameLine();
	if (ImGui::RadioButton("Drives 9-15", driveTab == 1)) driveTab = 1;

	int baseIdx = driveTab * 8;
	int numDrives = (driveTab == 0) ? 8 : 7;  // Drives 9-15 = 7 drives

	// --- Per-drive table ---
	if (ImGui::BeginTable("##Drives", 6,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Drive", ImGuiTableColumnFlags_WidthFixed, 48);
		ImGui::TableSetupColumn("Image", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Write Mode", ImGuiTableColumnFlags_WidthFixed, 100);
		ImGui::TableSetupColumn("##mount", ImGuiTableColumnFlags_WidthFixed, 32);
		ImGui::TableSetupColumn("##eject", ImGuiTableColumnFlags_WidthFixed, 48);
		ImGui::TableSetupColumn("##more", ImGuiTableColumnFlags_WidthFixed, 32);
		ImGui::TableHeadersRow();

		for (int i = 0; i < numDrives; ++i) {
			int driveIdx = baseIdx + i;
			ImGui::PushID(driveIdx);
			ImGui::TableNextRow();

			ATDiskInterface& di = sim.GetDiskInterface(driveIdx);
			bool loaded = di.IsDiskLoaded();
			bool dirty = loaded && di.IsDirty();

			// Drive label
			ImGui::TableNextColumn();
			if (dirty)
				ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "D%d:", driveIdx + 1);
			else
				ImGui::Text("D%d:", driveIdx + 1);

			// Image name
			ImGui::TableNextColumn();
			if (loaded) {
				const wchar_t *path = di.GetPath();
				if (path && *path) {
					VDStringA u8 = VDTextWToU8(VDStringW(path));
					const char *base = u8.c_str();
					const char *p = strrchr(base, '/');
					if (p) base = p + 1;
					ImGui::TextUnformatted(base);
				} else {
					ImGui::TextDisabled("(loaded)");
				}
			} else {
				ImGui::TextDisabled("(empty)");
			}

			// Write mode combo (matches Windows per-drive IDC_WRITEMODE)
			ImGui::TableNextColumn();
			int wmIdx = GetWriteModeIndex(di);
			ImGui::SetNextItemWidth(-FLT_MIN);
			if (ImGui::Combo("##wm", &wmIdx, kWriteModeLabels, 5)) {
				if (wmIdx == 0) {
					// "Off" = eject
					if (loaded) di.UnloadDisk();
				} else if (loaded) {
					di.SetWriteMode(kWriteModeValues[wmIdx]);
				}
			}

			// Browse button (matches Windows IDC_BROWSE)
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("...")) {
				SDL_ShowOpenFileDialog(DiskMountCallback,
					(void *)(intptr_t)driveIdx, window,
					kDiskFilters, 2, nullptr, false);
			}

			// Eject button (matches Windows IDC_EJECT)
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("Eject") && loaded)
				di.UnloadDisk();

			// More button (context menu — matches Windows IDC_MORE / "+")
			ImGui::TableNextColumn();
			if (ImGui::SmallButton("+")) {
				ImGui::OpenPopup("##DriveCtx");
			}

			if (ImGui::BeginPopup("##DriveCtx")) {
				if (ImGui::MenuItem("New Disk...", nullptr, false, true)) {
					g_createDiskState.show = true;
					g_createDiskState.targetDrive = driveIdx;
				}

				if (ImGui::MenuItem("Save Disk", nullptr, false, loaded && dirty))
					di.SaveDisk();

				if (ImGui::MenuItem("Save Disk As...", nullptr, false, loaded)) {
					SDL_ShowSaveFileDialog(DiskSaveAsCallback,
						(void *)(intptr_t)driveIdx, window,
						kDiskSaveFilters, 3, nullptr);
				}

				if (ImGui::MenuItem("Revert", nullptr, false, loaded && di.CanRevert()))
					di.RevertDisk();

				ImGui::Separator();

				if (ImGui::MenuItem("Eject", nullptr, false, loaded))
					di.UnloadDisk();

				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		ImGui::EndTable();
	}

	ImGui::Separator();

	// --- Emulation mode (global for all drives — matches Windows IDC_EMULATION_LEVEL) ---
	ATDiskEmulationMode curEmu = sim.GetDiskDrive(0).GetEmulationMode();
	int emuIdx = 0;
	for (int i = 0; i < kNumEmuModes; ++i)
		if (kEmuModeValues[i] == curEmu) { emuIdx = i; break; }

	if (ImGui::Combo("Emulation level", &emuIdx, kEmuModeLabels, kNumEmuModes)) {
		for (int i = 0; i < 15; ++i)
			sim.GetDiskDrive(i).SetEmulationMode(kEmuModeValues[emuIdx]);
	}

	ImGui::Separator();

	// OK button (matches Windows DEFPUSHBUTTON "OK")
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showDiskManager = false;

	ImGui::End();

	// Create Disk sub-dialog (opened from context menu)
	RenderCreateDiskDialog();
}
