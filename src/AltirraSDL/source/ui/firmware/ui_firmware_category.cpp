//	AltirraSDL - Firmware category page
//	The "Firmware" page inside the System Configuration dialog: OS/BASIC
//	firmware selection and the button that opens the full Firmware Manager.
//	Split out of ui_firmware.cpp (Phase 2a).

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstl.h>

#include "ui_main.h"
#include "ui_firmware_internal.h"
#include "simulator.h"
#include "firmwaremanager.h"
#include "uiaccessors.h"
#include "uiconfirm.h"

#include <algorithm>
#include <vector>
#include <cstring>

// =========================================================================
// Firmware category (System Configuration page)
// =========================================================================

// Helper: switch kernel with hardware mode compatibility checks
// (matches Windows ATUISwitchKernel from main.cpp:1041)
static void SwitchKernel(ATSimulator &sim, uint64 kernelId, const ATFirmwareInfo *pFw) {
	if (sim.GetKernelId() == kernelId)
		return;

	// Check kernel/hardware compatibility and auto-switch if needed
	if (pFw) {
		const auto hwmode = sim.GetHardwareMode();
		const bool canUseXLOS = kATHardwareModeTraits[hwmode].mbRunsXLOS;

		switch (pFw->mType) {
			case kATFirmwareType_Kernel1200XL:
				if (!canUseXLOS)
					ATUISwitchHardwareMode(nullptr, kATHardwareMode_1200XL, true);
				break;
			case kATFirmwareType_KernelXL:
				if (!canUseXLOS)
					ATUISwitchHardwareMode(nullptr, kATHardwareMode_800XL, true);
				break;
			case kATFirmwareType_KernelXEGS:
				if (!canUseXLOS)
					ATUISwitchHardwareMode(nullptr, kATHardwareMode_XEGS, true);
				break;
			case kATFirmwareType_Kernel800_OSA:
			case kATFirmwareType_Kernel800_OSB:
				if (hwmode == kATHardwareMode_5200)
					ATUISwitchHardwareMode(nullptr, kATHardwareMode_800, true);
				break;
			case kATFirmwareType_Kernel5200:
				if (hwmode != kATHardwareMode_5200)
					ATUISwitchHardwareMode(nullptr, kATHardwareMode_5200, true);
				break;
			default:
				break;
		}

		// Adjust memory for XL/1200XL kernels
		switch (pFw->mType) {
			case kATFirmwareType_KernelXL:
			case kATFirmwareType_Kernel1200XL:
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
				break;
			default:
				break;
		}
	}

	sim.SetKernel(kernelId);
	sim.ColdReset();
}

void RenderFirmwareCategory(ATSimulator &sim) {
	ATFirmwareManager *fwm = sim.GetFirmwareManager();
	if (!fwm) {
		ImGui::TextDisabled("Firmware manager not available");
		return;
	}

	// --- Operating system firmware (combo, matches Windows IDC_OS) ---
	ImGui::SeparatorText("Operating system");

	{
		// Build filtered, sorted firmware list matching Windows ATUIGetKernelFirmwareList
		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);
		const ATHardwareMode hwmode = sim.GetHardwareMode();

		struct FwEntry { uint64 id; VDStringA name; const ATFirmwareInfo *pInfo; };
		std::vector<FwEntry> osEntries;
		{ FwEntry e; e.id = 0; e.name = "[Autoselect]"; e.pInfo = nullptr; osEntries.push_back(std::move(e)); }

		for (const auto& fw : fwList) {
			if (!fw.mbVisible) continue;
			switch (fw.mType) {
				case kATFirmwareType_Kernel800_OSA:
				case kATFirmwareType_Kernel800_OSB:
					if (hwmode == kATHardwareMode_5200) continue;
					break;
				case kATFirmwareType_KernelXL:
				case kATFirmwareType_KernelXEGS:
				case kATFirmwareType_Kernel1200XL:
					if (!kATHardwareModeTraits[hwmode].mbRunsXLOS) continue;
					break;
				case kATFirmwareType_Kernel5200:
					if (hwmode != kATHardwareMode_5200) continue;
					break;
				default:
					continue;
			}
			{ FwEntry e; e.id = fw.mId; e.name = VDTextWToU8(fw.mName); e.pInfo = &fw; osEntries.push_back(std::move(e)); }
		}

		// Sort entries after [Autoselect] alphabetically
		std::sort(osEntries.begin() + 1, osEntries.end(),
			[](const FwEntry &a, const FwEntry &b) {
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});

		uint64 curKernel = sim.GetActualKernelId();
		int osIdx = 0;
		for (int i = 0; i < (int)osEntries.size(); ++i)
			if (osEntries[i].id == curKernel) { osIdx = i; break; }

		const char *preview = (osIdx < (int)osEntries.size()) ? osEntries[osIdx].name.c_str() : "[Autoselect]";
		if (ImGui::BeginCombo("##OS", preview)) {
			for (int i = 0; i < (int)osEntries.size(); ++i) {
				ImGui::PushID(i);
				if (ImGui::Selectable(osEntries[i].name.c_str(), osIdx == i))
					SwitchKernel(sim, osEntries[i].id, osEntries[i].pInfo);
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip("Select the firmware ROM image used for the operating system.");
	}

	// --- BASIC firmware (combo, matches Windows IDC_BASIC) ---
	ImGui::SeparatorText("BASIC");

	bool basicEnabled = sim.IsBASICEnabled();
	if (ImGui::Checkbox("Enable internal BASIC (boot without Option pressed)", &basicEnabled)) {
		sim.SetBASICEnabled(basicEnabled);
		if (ATUIIsResetNeeded(kATUIResetFlag_BasicChange))
			sim.ColdReset();
	}
	ImGui::SetItemTooltip(
		"Controls whether internal BASIC is enabled on boot. If enabled, BASIC starts "
		"after disk boot completes. If disabled, the Option button is automatically "
		"held down during boot to suppress internal BASIC.");

	{
		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);

		struct FwEntry { uint64 id; VDStringA name; };
		std::vector<FwEntry> basicEntries;
		{ FwEntry e; e.id = 0; e.name = "[Autoselect]"; basicEntries.push_back(std::move(e)); }

		for (const auto& fw : fwList) {
			if (fw.mbVisible && fw.mType == kATFirmwareType_Basic) {
				FwEntry e; e.id = fw.mId; e.name = VDTextWToU8(fw.mName);
				basicEntries.push_back(std::move(e));
			}
		}

		std::sort(basicEntries.begin() + 1, basicEntries.end(),
			[](const FwEntry &a, const FwEntry &b) {
				return strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
			});

		uint64 curBasic = sim.GetActualBasicId();
		int basicIdx = 0;
		for (int i = 0; i < (int)basicEntries.size(); ++i)
			if (basicEntries[i].id == curBasic) { basicIdx = i; break; }

		const char *preview = (basicIdx < (int)basicEntries.size()) ? basicEntries[basicIdx].name.c_str() : "[Autoselect]";
		if (ImGui::BeginCombo("##BASIC", preview)) {
			for (int i = 0; i < (int)basicEntries.size(); ++i) {
				ImGui::PushID(i);
				if (ImGui::Selectable(basicEntries[i].name.c_str(), basicIdx == i)) {
					sim.SetBasic(basicEntries[i].id);
					sim.ColdReset();
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		ImGui::SetItemTooltip(
			"Select the firmware ROM image used for BASIC. For computer models "
			"that have built-in BASIC, this determines the BASIC used when the "
			"computer is booted without holding down the Option button.");
	}

	ImGui::Separator();

	bool autoReload = sim.IsROMAutoReloadEnabled();
	if (ImGui::Checkbox("Auto-Reload ROMs on Cold Reset", &autoReload))
		sim.SetROMAutoReloadEnabled(autoReload);
	ImGui::SetItemTooltip("If enabled, firmware ROM images are reloaded from disk on every cold reset.");

	ImGui::Separator();

	// Firmware Manager button (matches Windows IDC_FIRMWAREMANAGER)
	if (ImGui::Button("Firmware Manager..."))
		g_showFirmwareManager = true;
	ImGui::SetItemTooltip("Open the firmware manager to add, remove, or configure ROM images.");

	if (g_showFirmwareManager)
		RenderFirmwareManager(sim, g_showFirmwareManager);
}
