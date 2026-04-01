//	AltirraSDL - System Configuration dialog
//	Mirrors Windows Altirra's Configure System paged dialog
//	(IDD_CONFIGURE with tree sidebar IDC_PAGE_TREE).
//	Hierarchy: Computer, Outputs, Peripherals, Media, Emulator.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include "ui_main.h"
#include "simulator.h"
#include "constants.h"
#include "cpu.h"
#include "firmwaremanager.h"
#include "gtia.h"
#include "cassette.h"
#include "options.h"
#include "uiaccessors.h"
#include <at/atcore/media.h>
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>
#include <at/atio/cassetteimage.h>

extern ATSimulator g_sim;

// =========================================================================
// Category IDs (flat index used by systemConfigCategory in ATUIState)
// Matches Windows tree order from uiconfiguresystem.cpp OnPopulatePages()
// =========================================================================

enum {
	kCat_System,        // Computer > System
	kCat_CPU,           // Computer > CPU
	kCat_Firmware,      // Computer > Firmware
	kCat_Memory,        // Computer > Memory
	kCat_Acceleration,  // Computer > Acceleration
	kCat_Speed,         // Computer > Speed
	kCat_Boot,          // Computer > Boot
	kCat_Video,         // Outputs > Video
	kCat_Audio,         // Outputs > Audio
	kCat_Devices,       // Peripherals > Devices
	kCat_Keyboard,      // Peripherals > Keyboard
	kCat_Disk,          // Media > Disk
	kCat_Cassette,      // Media > Cassette
	kCat_MediaDefaults, // Media > Defaults
	kCat_Display,       // Emulator > Display
	kCat_Input,         // Emulator > Input
	kCat_EaseOfUse,     // Emulator > Ease of Use
	kCat_EnhancedText,  // Emulator > Enhanced Text
	kCat_Caption,       // Emulator > Caption
	kCat_Workarounds,   // Emulator > Workarounds
	kCat_Count
};

// =========================================================================
// System (Hardware)
// =========================================================================

static const ATHardwareMode kHWModeValues[] = {
	kATHardwareMode_800, kATHardwareMode_800XL, kATHardwareMode_1200XL,
	kATHardwareMode_130XE, kATHardwareMode_1400XL, kATHardwareMode_XEGS,
	kATHardwareMode_5200,
};
static const char *kHWModeLabels[] = {
	"Atari 800", "Atari 800XL", "Atari 1200XL",
	"Atari 130XE", "Atari 1400XL", "Atari XEGS",
	"Atari 5200",
};
static const int kHWModeCount = 7;

static const ATVideoStandard kVideoStdValues[] = {
	kATVideoStandard_NTSC, kATVideoStandard_PAL, kATVideoStandard_SECAM,
};
static const char *kVideoStdLabels[] = { "NTSC", "PAL", "SECAM" };

static void RenderSystemCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Hardware type");

	ATHardwareMode curHW = sim.GetHardwareMode();
	int hwIdx = 0;
	for (int i = 0; i < kHWModeCount; ++i)
		if (kHWModeValues[i] == curHW) { hwIdx = i; break; }
	if (ImGui::Combo("##HardwareType", &hwIdx, kHWModeLabels, kHWModeCount))
		sim.SetHardwareMode(kHWModeValues[hwIdx]);

	ImGui::SeparatorText("Video standard");

	ATVideoStandard curVS = sim.GetVideoStandard();
	int vsIdx = 0;
	for (int i = 0; i < 3; ++i)
		if (kVideoStdValues[i] == curVS) { vsIdx = i; break; }
	if (ImGui::Combo("##VideoStandard", &vsIdx, kVideoStdLabels, 3))
		sim.SetVideoStandard(kVideoStdValues[vsIdx]);

	ImGui::SeparatorText("CTIA/GTIA type");

	bool ctia = sim.GetGTIA().IsCTIAMode();
	if (ImGui::Checkbox("CTIA mode", &ctia))
		sim.GetGTIA().SetCTIAMode(ctia);
}

// =========================================================================
// CPU (matches Windows IDD_CONFIGURE_CPU — radio buttons for all modes)
// =========================================================================

struct CPUModeEntry {
	ATCPUMode mode;
	uint32 subCycles;
	const char *label;
};

static const CPUModeEntry kCPUModes[] = {
	{ kATCPUMode_6502,  1, "6502 / 6502C" },
	{ kATCPUMode_65C02, 1, "65C02" },
	{ kATCPUMode_65C816, 1, "65C816 (1.79MHz)" },
	{ kATCPUMode_65C816, 2, "65C816 (3.58MHz)" },
	{ kATCPUMode_65C816, 4, "65C816 (7.14MHz)" },
	{ kATCPUMode_65C816, 6, "65C816 (10.74MHz)" },
	{ kATCPUMode_65C816, 8, "65C816 (14.28MHz)" },
	{ kATCPUMode_65C816, 10, "65C816 (17.90MHz)" },
	{ kATCPUMode_65C816, 12, "65C816 (21.48MHz)" },
	{ kATCPUMode_65C816, 23, "65C816 (41.16MHz)" },
};
static const int kNumCPUModes = 10;

static void RenderCPUCategory(ATSimulator &sim) {
	ImGui::SeparatorText("CPU mode");

	ATCPUMode curMode = sim.GetCPU().GetCPUMode();
	uint32 curSub = sim.GetCPU().GetSubCycles();

	int cpuIdx = 0;
	for (int i = 0; i < kNumCPUModes; ++i)
		if (kCPUModes[i].mode == curMode && kCPUModes[i].subCycles == curSub) { cpuIdx = i; break; }

	for (int i = 0; i < kNumCPUModes; ++i) {
		if (ImGui::RadioButton(kCPUModes[i].label, cpuIdx == i))
			sim.SetCPUMode(kCPUModes[i].mode, kCPUModes[i].subCycles);
	}

	ImGui::SeparatorText("Additional options");

	bool illegals = sim.GetCPU().AreIllegalInsnsEnabled();
	if (ImGui::Checkbox("Enable illegal instructions", &illegals))
		sim.GetCPU().SetIllegalInsnsEnabled(illegals);

	bool nmiBlock = sim.GetCPU().IsNMIBlockingEnabled();
	if (ImGui::Checkbox("Allow BRK/IRQ to block NMI", &nmiBlock))
		sim.GetCPU().SetNMIBlockingEnabled(nmiBlock);

	bool stopBRK = sim.GetCPU().GetStopOnBRK();
	if (ImGui::Checkbox("Stop on BRK instruction", &stopBRK))
		sim.GetCPU().SetStopOnBRK(stopBRK);

	bool history = sim.GetCPU().IsHistoryEnabled();
	if (ImGui::Checkbox("Record instruction history", &history))
		sim.GetCPU().SetHistoryEnabled(history);

	bool paths = sim.GetCPU().IsPathfindingEnabled();
	if (ImGui::Checkbox("Track code paths", &paths))
		sim.GetCPU().SetPathfindingEnabled(paths);

	bool shadowROM = sim.GetShadowROMEnabled();
	if (ImGui::Checkbox("Shadow ROMs in fast RAM", &shadowROM))
		sim.SetShadowROMEnabled(shadowROM);

	bool shadowCart = sim.GetShadowCartridgeEnabled();
	if (ImGui::Checkbox("Shadow cartridges in fast RAM", &shadowCart))
		sim.SetShadowCartridgeEnabled(shadowCart);
}

// =========================================================================
// Firmware
// =========================================================================

static void RenderFirmwareCategory(ATSimulator &sim) {
	ATFirmwareManager *fwm = sim.GetFirmwareManager();
	if (!fwm) {
		ImGui::TextDisabled("Firmware manager not available");
		return;
	}

	ImGui::SeparatorText("Operating system");

	{
		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);
		uint64 curKernel = sim.GetActualKernelId();

		if (ImGui::BeginListBox("##KernelROM", ImVec2(-FLT_MIN, 100))) {
			bool isInternal = (curKernel == 0 || curKernel == 1);
			if (ImGui::Selectable("(Internal - Built-in)", isInternal))
				sim.SetKernel(0);

			for (const auto& fw : fwList) {
				if (fw.mType < kATFirmwareType_Kernel800_OSA || fw.mType > kATFirmwareType_Kernel1200XL)
					continue;
				VDStringA name = VDTextWToU8(fw.mName);
				if (ImGui::Selectable(name.c_str(), fw.mId == curKernel))
					sim.SetKernel(fw.mId);
			}
			ImGui::EndListBox();
		}
	}

	ImGui::SeparatorText("BASIC");

	bool basicEnabled = sim.IsBASICEnabled();
	if (ImGui::Checkbox("Enable internal BASIC (boot without Option pressed)", &basicEnabled))
		sim.SetBASICEnabled(basicEnabled);

	{
		vdvector<ATFirmwareInfo> fwList;
		fwm->GetFirmwareList(fwList);
		uint64 curBasic = sim.GetActualBasicId();

		if (ImGui::BeginListBox("##BasicROM", ImVec2(-FLT_MIN, 80))) {
			bool isInternal = (curBasic == 0 || curBasic == 1);
			if (ImGui::Selectable("(Internal - Built-in)", isInternal))
				sim.SetBasic(0);

			for (const auto& fw : fwList) {
				if (fw.mType != kATFirmwareType_Basic)
					continue;
				VDStringA name = VDTextWToU8(fw.mName);
				if (ImGui::Selectable(name.c_str(), fw.mId == curBasic))
					sim.SetBasic(fw.mId);
			}
			ImGui::EndListBox();
		}
	}

	ImGui::Separator();

	bool autoReload = sim.IsROMAutoReloadEnabled();
	if (ImGui::Checkbox("Auto-Reload ROMs on Cold Reset", &autoReload))
		sim.SetROMAutoReloadEnabled(autoReload);
}

// =========================================================================
// Memory (matches Windows IDD_CONFIGURE_MEMORY)
// =========================================================================

static const ATMemoryMode kMemModeValues[] = {
	kATMemoryMode_8K, kATMemoryMode_16K, kATMemoryMode_24K,
	kATMemoryMode_32K, kATMemoryMode_40K, kATMemoryMode_48K,
	kATMemoryMode_52K, kATMemoryMode_64K, kATMemoryMode_128K,
	kATMemoryMode_256K, kATMemoryMode_320K, kATMemoryMode_576K,
	kATMemoryMode_1088K,
};
static const char *kMemModeLabels[] = {
	"8K", "16K", "24K", "32K", "40K", "48K", "52K", "64K",
	"128K", "256K", "320K (Compy Shop)", "576K (Compy Shop)", "1088K",
};

static const ATMemoryClearMode kMemClearValues[] = {
	kATMemoryClearMode_Zero, kATMemoryClearMode_Random,
	kATMemoryClearMode_DRAM1, kATMemoryClearMode_DRAM2, kATMemoryClearMode_DRAM3,
};
static const char *kMemClearLabels[] = {
	"Zero", "Random", "DRAM Pattern 1", "DRAM Pattern 2", "DRAM Pattern 3",
};

static void RenderMemoryCategory(ATSimulator &sim) {
	ATMemoryMode curMM = sim.GetMemoryMode();
	int mmIdx = 0;
	for (int i = 0; i < 13; ++i)
		if (kMemModeValues[i] == curMM) { mmIdx = i; break; }
	if (ImGui::Combo("Memory Size", &mmIdx, kMemModeLabels, 13))
		sim.SetMemoryMode(kMemModeValues[mmIdx]);

	ATMemoryClearMode curMC = sim.GetMemoryClearMode();
	int mcIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kMemClearValues[i] == curMC) { mcIdx = i; break; }
	if (ImGui::Combo("Memory Clear Pattern", &mcIdx, kMemClearLabels, 5))
		sim.SetMemoryClearMode(kMemClearValues[mcIdx]);

	ImGui::Separator();

	bool mapRAM = sim.IsMapRAMEnabled();
	if (ImGui::Checkbox("Enable MapRAM (XL/XE only)", &mapRAM))
		sim.SetMapRAMEnabled(mapRAM);

	bool u1mb = sim.IsUltimate1MBEnabled();
	if (ImGui::Checkbox("Enable Ultimate1MB", &u1mb))
		sim.SetUltimate1MBEnabled(u1mb);

	bool axlonAlias = sim.GetAxlonAliasingEnabled();
	if (ImGui::Checkbox("Enable bank register aliasing", &axlonAlias))
		sim.SetAxlonAliasingEnabled(axlonAlias);

	bool floatingIO = sim.IsFloatingIoBusEnabled();
	if (ImGui::Checkbox("Enable floating I/O bus (800 only)", &floatingIO))
		sim.SetFloatingIoBusEnabled(floatingIO);

	bool preserveExt = sim.IsPreserveExtRAMEnabled();
	if (ImGui::Checkbox("Preserve extended memory on cold reset", &preserveExt))
		sim.SetPreserveExtRAMEnabled(preserveExt);
}

// =========================================================================
// Acceleration (matches Windows IDD_CONFIGURE_ACCELERATION)
// =========================================================================

static void RenderAccelerationCategory(ATSimulator &sim) {
	ImGui::SeparatorText("OS acceleration");

	bool fastBoot = sim.IsFastBootEnabled();
	if (ImGui::Checkbox("Fast boot", &fastBoot))
		sim.SetFastBootEnabled(fastBoot);

	bool fastFP = sim.IsFPPatchEnabled();
	if (ImGui::Checkbox("Fast floating-point math", &fastFP))
		sim.SetFPPatchEnabled(fastFP);

	ImGui::SeparatorText("SIO device patches");

	bool sioPatch = sim.IsSIOPatchEnabled();
	if (ImGui::Checkbox("SIO Patch", &sioPatch))
		sim.SetSIOPatchEnabled(sioPatch);

	bool diskSioPatch = sim.IsDiskSIOPatchEnabled();
	if (ImGui::Checkbox("D: patch (Disk SIO)", &diskSioPatch))
		sim.SetDiskSIOPatchEnabled(diskSioPatch);

	bool casSioPatch = sim.IsCassetteSIOPatchEnabled();
	if (ImGui::Checkbox("C: patch (Cassette SIO)", &casSioPatch))
		sim.SetCassetteSIOPatchEnabled(casSioPatch);

	bool deviceSioPatch = sim.GetDeviceSIOPatchEnabled();
	if (ImGui::Checkbox("PRT: patch (Other SIO)", &deviceSioPatch))
		sim.SetDeviceSIOPatchEnabled(deviceSioPatch);

	ImGui::SeparatorText("CIO device patches");

	bool cioH = sim.GetCIOPatchEnabled('H');
	if (ImGui::Checkbox("H: (Host device CIO)", &cioH))
		sim.SetCIOPatchEnabled('H', cioH);

	bool cioP = sim.GetCIOPatchEnabled('P');
	if (ImGui::Checkbox("P: (Printer CIO)", &cioP))
		sim.SetCIOPatchEnabled('P', cioP);

	bool cioR = sim.GetCIOPatchEnabled('R');
	if (ImGui::Checkbox("R: (RS-232 CIO)", &cioR))
		sim.SetCIOPatchEnabled('R', cioR);

	bool cioT = sim.GetCIOPatchEnabled('T');
	if (ImGui::Checkbox("T: (1030 Serial CIO)", &cioT))
		sim.SetCIOPatchEnabled('T', cioT);

	ImGui::SeparatorText("Burst transfers");

	bool diskBurst = sim.GetDiskBurstTransfersEnabled();
	if (ImGui::Checkbox("D: burst I/O", &diskBurst))
		sim.SetDiskBurstTransfersEnabled(diskBurst);

	bool devSioBurst = sim.GetDeviceSIOBurstTransfersEnabled();
	if (ImGui::Checkbox("PRT: burst I/O", &devSioBurst))
		sim.SetDeviceSIOBurstTransfersEnabled(devSioBurst);

	bool devCioBurst = sim.GetDeviceCIOBurstTransfersEnabled();
	if (ImGui::Checkbox("CIO burst transfers", &devCioBurst))
		sim.SetDeviceCIOBurstTransfersEnabled(devCioBurst);

	ImGui::Separator();

	bool sioOverride = sim.IsDiskSIOOverrideDetectEnabled();
	if (ImGui::Checkbox("SIO override detection", &sioOverride))
		sim.SetDiskSIOOverrideDetectEnabled(sioOverride);
}

// =========================================================================
// Speed (matches Windows IDD_CONFIGURE_SPEED)
// =========================================================================

static void RenderSpeedCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Speed control");

	bool warp = ATUIGetTurbo();
	if (ImGui::Checkbox("Run as fast as possible (warp)", &warp))
		ATUISetTurbo(warp);

	bool slowmo = ATUIGetSlowMotion();
	if (ImGui::Checkbox("Slow Motion", &slowmo))
		ATUISetSlowMotion(slowmo);

	ImGui::SeparatorText("Speed adjustment");

	float spd = ATUIGetSpeedModifier();
	static const float kSpdValues[] = { 0.0f, 1.0f, 3.0f, 7.0f };
	static const char *kSpdLabels[] = { "1x (Normal)", "2x", "4x", "8x" };
	int spdIdx = 0;
	for (int i = 0; i < 4; ++i)
		if (kSpdValues[i] == spd) { spdIdx = i; break; }
	if (ImGui::Combo("Speed", &spdIdx, kSpdLabels, 4))
		ATUISetSpeedModifier(kSpdValues[spdIdx]);

	ImGui::SeparatorText("Frame rate");

	static const char *kFrameRateLabels[] = { "Hardware", "Broadcast", "Integral" };
	ATFrameRateMode frMode = ATUIGetFrameRateMode();
	int frIdx = (int)frMode;
	if (frIdx < 0 || frIdx >= 3) frIdx = 0;
	if (ImGui::Combo("Base frame rate", &frIdx, kFrameRateLabels, 3))
		ATUISetFrameRateMode((ATFrameRateMode)frIdx);

	bool vsyncAdaptive = ATUIGetFrameRateVSyncAdaptive();
	if (ImGui::Checkbox("Lock speed to display refresh rate", &vsyncAdaptive))
		ATUISetFrameRateVSyncAdaptive(vsyncAdaptive);

	ImGui::Separator();

	bool pauseInactive = ATUIGetPauseWhenInactive();
	if (ImGui::Checkbox("Pause when emulator window is inactive", &pauseInactive))
		ATUISetPauseWhenInactive(pauseInactive);
}

// =========================================================================
// Boot (matches Windows IDD_CONFIGURE_BOOT)
// =========================================================================

static void RenderBootCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Program load mode");

	static const char *kLoadModes[] = {
		"Default", "Type 3 Poll", "Deferred", "Disk Boot"
	};
	int loadMode = (int)sim.GetHLEProgramLoadMode();
	if (loadMode < 0 || loadMode > 3) loadMode = 0;
	if (ImGui::Combo("##ProgramLoadMode", &loadMode, kLoadModes, 4))
		sim.SetHLEProgramLoadMode((ATHLEProgramLoadMode)loadMode);

	ImGui::SeparatorText("Randomization");

	bool randomFillEXE = sim.IsRandomFillEXEEnabled();
	if (ImGui::Checkbox("Randomize Memory on EXE Load", &randomFillEXE))
		sim.SetRandomFillEXEEnabled(randomFillEXE);

	bool randomLaunch = sim.IsRandomProgramLaunchDelayEnabled();
	if (ImGui::Checkbox("Randomize program load timing", &randomLaunch))
		sim.SetRandomProgramLaunchDelayEnabled(randomLaunch);

	ImGui::SeparatorText("Unload on boot image");

	uint32 bootMask = ATUIGetBootUnloadStorageMask();

	bool unloadCarts = (bootMask & kATStorageTypeMask_Cartridge) != 0;
	if (ImGui::Checkbox("Unload cartridges when booting new image", &unloadCarts))
		ATUISetBootUnloadStorageMask(unloadCarts
			? (bootMask | kATStorageTypeMask_Cartridge)
			: (bootMask & ~kATStorageTypeMask_Cartridge));

	bool unloadDisks = (bootMask & kATStorageTypeMask_Disk) != 0;
	if (ImGui::Checkbox("Unload disks when booting new image", &unloadDisks))
		ATUISetBootUnloadStorageMask(unloadDisks
			? (bootMask | kATStorageTypeMask_Disk)
			: (bootMask & ~kATStorageTypeMask_Disk));

	bool unloadTapes = (bootMask & kATStorageTypeMask_Tape) != 0;
	if (ImGui::Checkbox("Unload tapes when booting new image", &unloadTapes))
		ATUISetBootUnloadStorageMask(unloadTapes
			? (bootMask | kATStorageTypeMask_Tape)
			: (bootMask & ~kATStorageTypeMask_Tape));
}

// =========================================================================
// Video (matches Windows IDD_CONFIGURE_VIDEO)
// =========================================================================

static void RenderVideoCategory(ATSimulator &sim) {
	ATGTIAEmulator& gtia = sim.GetGTIA();

	ImGui::SeparatorText("Video effects");

	static const char *kArtifactLabels[] = {
		"None", "NTSC", "PAL", "NTSC High", "PAL High", "Auto", "Auto High"
	};
	int artifact = (int)gtia.GetArtifactingMode();
	if (artifact < 0 || artifact >= 7) artifact = 0;
	if (ImGui::Combo("Artifacting", &artifact, kArtifactLabels, 7))
		gtia.SetArtifactingMode((ATArtifactMode)artifact);

	static const char *kMonitorLabels[] = {
		"Color", "Peritel", "Green Mono", "Amber Mono", "Blue-White Mono", "White Mono"
	};
	int monitor = (int)gtia.GetMonitorMode();
	if (monitor < 0 || monitor >= 6) monitor = 0;
	if (ImGui::Combo("Monitor Mode", &monitor, kMonitorLabels, 6))
		gtia.SetMonitorMode((ATMonitorMode)monitor);

	ImGui::Separator();

	bool blend = gtia.IsBlendModeEnabled();
	if (ImGui::Checkbox("Frame Blending", &blend))
		gtia.SetBlendModeEnabled(blend);

	bool interlace = gtia.IsInterlaceEnabled();
	if (ImGui::Checkbox("Interlace", &interlace))
		gtia.SetInterlaceEnabled(interlace);

	bool scanlines = gtia.AreScanlinesEnabled();
	if (ImGui::Checkbox("Scanlines", &scanlines))
		gtia.SetScanlinesEnabled(scanlines);

	ImGui::Separator();

	bool palExt = gtia.IsOverscanPALExtended();
	if (ImGui::Checkbox("Extended PAL Height", &palExt))
		gtia.SetOverscanPALExtended(palExt);
}

// =========================================================================
// Audio (matches Windows IDD_CONFIGURE_AUDIO)
// =========================================================================

static void RenderAudioCategory(ATSimulator &sim) {
	ImGui::SeparatorText("Audio setup");

	IATAudioOutput *pAudio = sim.GetAudioOutput();
	if (pAudio) {
		bool muted = pAudio->GetMute();
		if (ImGui::Checkbox("Mute All", &muted))
			pAudio->SetMute(muted);
	}

	bool dualPokey = sim.IsDualPokeysEnabled();
	if (ImGui::Checkbox("Stereo", &dualPokey))
		sim.SetDualPokeysEnabled(dualPokey);

	ATPokeyEmulator& pokey = sim.GetPokey();

	bool stereoMono = pokey.IsStereoAsMonoEnabled();
	if (ImGui::Checkbox("Downmix stereo to mono", &stereoMono))
		pokey.SetStereoAsMonoEnabled(stereoMono);

	bool nonlinear = pokey.IsNonlinearMixingEnabled();
	if (ImGui::Checkbox("Non-linear mixing", &nonlinear))
		pokey.SetNonlinearMixingEnabled(nonlinear);

	bool serialNoise = pokey.IsSerialNoiseEnabled();
	if (ImGui::Checkbox("Serial noise", &serialNoise))
		pokey.SetSerialNoiseEnabled(serialNoise);

	bool speaker = pokey.IsSpeakerFilterEnabled();
	if (ImGui::Checkbox("Simulate console speaker", &speaker))
		pokey.SetSpeakerFilterEnabled(speaker);

	ImGui::SeparatorText("Enabled channels");

	// Primary POKEY channels 1-4
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "%d", i + 1);
		bool ch = pokey.IsChannelEnabled(i);
		if (ImGui::Checkbox(label, &ch))
			pokey.SetChannelEnabled(i, ch);
		if (i < 3) ImGui::SameLine();
	}

	// Secondary POKEY channels (if stereo enabled)
	if (dualPokey) {
		for (int i = 0; i < 4; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "%dR", i + 1);
			bool ch = pokey.IsSecondaryChannelEnabled(i);
			if (ImGui::Checkbox(label, &ch))
				pokey.SetSecondaryChannelEnabled(i, ch);
			if (i < 3) ImGui::SameLine();
		}
	}

	ImGui::Separator();

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Drive Sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);

	bool audioMonitor = sim.IsAudioMonitorEnabled();
	if (ImGui::Checkbox("Audio monitor", &audioMonitor))
		sim.SetAudioMonitorEnabled(audioMonitor);

	bool audioScope = sim.IsAudioScopeEnabled();
	if (ImGui::Checkbox("Audio scope", &audioScope))
		sim.SetAudioScopeEnabled(audioScope);
}

// =========================================================================
// Keyboard (matches Windows IDD_CONFIGURE_KEYBOARD)
// =========================================================================

static void RenderKeyboardCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_KEYBOARD
	extern ATUIKeyboardOptions g_kbdOpts;

	ImGui::SeparatorText("Arrow key mode");

	static const char *kArrowModes[] = {
		"Invert Ctrl", "Auto Ctrl", "Default Ctrl"
	};
	int akm = (int)g_kbdOpts.mArrowKeyMode;
	if (akm < 0 || akm >= 3) akm = 0;
	if (ImGui::Combo("##ArrowKeyMode", &akm, kArrowModes, 3))
		g_kbdOpts.mArrowKeyMode = (ATUIKeyboardOptions::ArrowKeyMode)akm;

	ImGui::SeparatorText("Key press mode");

	static const char *kLayoutModes[] = {
		"Natural", "Raw", "Custom"
	};
	int lm = (int)g_kbdOpts.mLayoutMode;
	if (lm < 0 || lm >= 3) lm = 0;
	if (ImGui::Combo("##LayoutMode", &lm, kLayoutModes, 3))
		g_kbdOpts.mLayoutMode = (ATUIKeyboardOptions::LayoutMode)lm;

	ImGui::Separator();

	if (ImGui::Checkbox("Allow SHIFT key to be detected on cold reset", &g_kbdOpts.mbAllowShiftOnColdReset))
		ATUIInitVirtualKeyMap(g_kbdOpts);

	if (ImGui::Checkbox("Enable F1-F4 as 1200XL function keys", &g_kbdOpts.mbEnableFunctionKeys))
		ATUIInitVirtualKeyMap(g_kbdOpts);

	if (ImGui::Checkbox("Share modifier host keys between keyboard and input maps", &g_kbdOpts.mbAllowInputMapModifierOverlap))
		ATUIInitVirtualKeyMap(g_kbdOpts);

	if (ImGui::Checkbox("Share non-modifier host keys between keyboard and input maps", &g_kbdOpts.mbAllowInputMapOverlap))
		ATUIInitVirtualKeyMap(g_kbdOpts);
}

// =========================================================================
// Disk (matches Windows IDD_CONFIGURE_DISK)
// =========================================================================

static void RenderDiskCategory(ATSimulator &sim) {
	bool accurateTiming = sim.IsDiskAccurateTimingEnabled();
	if (ImGui::Checkbox("Accurate sector timing", &accurateTiming))
		sim.SetDiskAccurateTimingEnabled(accurateTiming);

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Play drive sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);

	bool sectorCounter = sim.IsDiskSectorCounterEnabled();
	if (ImGui::Checkbox("Show sector counter", &sectorCounter))
		sim.SetDiskSectorCounterEnabled(sectorCounter);
}

// =========================================================================
// Cassette (matches Windows IDD_CONFIGURE_CASSETTE)
// =========================================================================

static void RenderCassetteCategory(ATSimulator &sim) {
	ATCassetteEmulator& cas = sim.GetCassette();

	ImGui::SeparatorText("Tape setup");

	bool autoBoot = sim.IsCassetteAutoBootEnabled();
	if (ImGui::Checkbox("Auto-boot on startup", &autoBoot))
		sim.SetCassetteAutoBootEnabled(autoBoot);

	bool autoBasicBoot = sim.IsCassetteAutoBasicBootEnabled();
	if (ImGui::Checkbox("Auto-boot BASIC on startup", &autoBasicBoot))
		sim.SetCassetteAutoBasicBootEnabled(autoBasicBoot);

	bool autoRewind = sim.IsCassetteAutoRewindEnabled();
	if (ImGui::Checkbox("Auto-rewind on startup", &autoRewind))
		sim.SetCassetteAutoRewindEnabled(autoRewind);

	bool loadDataAsAudio = cas.IsLoadDataAsAudioEnabled();
	if (ImGui::Checkbox("Load data as audio", &loadDataAsAudio))
		cas.SetLoadDataAsAudioEnable(loadDataAsAudio);

	bool randomStart = sim.IsCassetteRandomizedStartEnabled();
	if (ImGui::Checkbox("Randomize starting position", &randomStart))
		sim.SetCassetteRandomizedStartEnabled(randomStart);

	ImGui::SeparatorText("Turbo support");

	static const char *kTurboModes[] = {
		"None", "Command Control", "Proceed Sense",
		"Interrupt Sense", "KSO Turbo 2000", "Turbo D",
		"Data Control", "Always"
	};
	int turbo = (int)cas.GetTurboMode();
	if (turbo < 0 || turbo >= 8) turbo = 0;
	if (ImGui::Combo("Turbo mode", &turbo, kTurboModes, 8))
		cas.SetTurboMode((ATCassetteTurboMode)turbo);

	static const char *kTurboDecoders[] = {
		"Slope (No Filter)", "Slope (Filter)",
		"Peak (Filter)", "Peak (Balance Lo-Hi)", "Peak (Balance Hi-Lo)"
	};
	int decoder = (int)cas.GetTurboDecodeAlgorithm();
	if (decoder < 0 || decoder >= 5) decoder = 0;
	if (ImGui::Combo("Turbo decoder", &decoder, kTurboDecoders, 5))
		cas.SetTurboDecodeAlgorithm((ATCassetteTurboDecodeAlgorithm)decoder);

	bool invertTurbo = cas.GetPolarityMode() == kATCassettePolarityMode_Inverted;
	if (ImGui::Checkbox("Invert turbo data", &invertTurbo))
		cas.SetPolarityMode(invertTurbo
			? kATCassettePolarityMode_Inverted
			: kATCassettePolarityMode_Normal);

	ImGui::SeparatorText("Direct read filter");

	static const char *kDirectSenseModes[] = {
		"Normal", "Low Speed", "High Speed", "Max Speed"
	};
	int dsm = (int)cas.GetDirectSenseMode();
	if (dsm < 0 || dsm >= 4) dsm = 0;
	if (ImGui::Combo("##DirectReadFilter", &dsm, kDirectSenseModes, 4))
		cas.SetDirectSenseMode((ATCassetteDirectSenseMode)dsm);

	ImGui::SeparatorText("Workarounds");

	bool vbiAvoid = cas.IsVBIAvoidanceEnabled();
	if (ImGui::Checkbox("Avoid OS C: random VBI-related errors", &vbiAvoid))
		cas.SetVBIAvoidanceEnabled(vbiAvoid);

	ImGui::SeparatorText("Pre-filtering");

	bool fskComp = cas.GetFSKSpeedCompensationEnabled();
	if (ImGui::Checkbox("Enable FSK speed compensation", &fskComp))
		cas.SetFSKSpeedCompensationEnabled(fskComp);

	bool crosstalk = cas.GetCrosstalkReductionEnabled();
	if (ImGui::Checkbox("Enable crosstalk reduction", &crosstalk))
		cas.SetCrosstalkReductionEnabled(crosstalk);
}

// =========================================================================
// Display (matches Windows IDD_CONFIGURE_DISPLAY)
// =========================================================================

static void RenderDisplayCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_DISPLAY — pointer/indicator settings
	// Note: Filter mode, stretch mode, overscan are View menu items, not here.

	bool autoHide = ATUIGetPointerAutoHide();
	if (ImGui::Checkbox("Auto-hide mouse pointer after short delay", &autoHide))
		ATUISetPointerAutoHide(autoHide);

	bool constrainFS = ATUIGetConstrainMouseFullScreen();
	if (ImGui::Checkbox("Constrain mouse pointer in full-screen mode", &constrainFS))
		ATUISetConstrainMouseFullScreen(constrainFS);

	bool hideTarget = !ATUIGetTargetPointerVisible();
	if (ImGui::Checkbox("Hide target pointer for absolute mouse input (light pen/gun/tablet)", &hideTarget))
		ATUISetTargetPointerVisible(!hideTarget);

	ImGui::Separator();

	bool indicators = ATUIGetDisplayIndicators();
	if (ImGui::Checkbox("Show indicators", &indicators))
		ATUISetDisplayIndicators(indicators);

	bool padIndicators = ATUIGetDisplayPadIndicators();
	if (ImGui::Checkbox("Pad bottom margin to reserve space for indicators", &padIndicators))
		ATUISetDisplayPadIndicators(padIndicators);

	bool padBounds = ATUIGetDrawPadBoundsEnabled();
	if (ImGui::Checkbox("Show tablet/pad bounds", &padBounds))
		ATUISetDrawPadBoundsEnabled(padBounds);

	bool padPointers = ATUIGetDrawPadPointersEnabled();
	if (ImGui::Checkbox("Show tablet/pad pointers", &padPointers))
		ATUISetDrawPadPointersEnabled(padPointers);
}

// =========================================================================
// Input (matches Windows IDD_CONFIGURE_INPUT)
// =========================================================================

static void RenderInputCategory(ATSimulator &sim) {
	ATPokeyEmulator& pokey = sim.GetPokey();

	bool potNoise = sim.GetPotNoiseEnabled();
	if (ImGui::Checkbox("Enable paddle potentiometer noise", &potNoise))
		sim.SetPotNoiseEnabled(potNoise);

	bool immPots = pokey.IsImmediatePotUpdateEnabled();
	if (ImGui::Checkbox("Use immediate analog updates", &immPots))
		pokey.SetImmediatePotUpdateEnabled(immPots);
}

// =========================================================================
// Ease of Use (matches Windows IDD_CONFIGURE_EASEOFUSE)
// =========================================================================

static void RenderEaseOfUseCategory(ATSimulator &) {
	// Matches Windows IDD_CONFIGURE_EASEOFUSE
	uint32 flags = ATUIGetResetFlags();

	bool resetCart = (flags & kATUIResetFlag_CartridgeChange) != 0;
	if (ImGui::Checkbox("Reset when changing cartridges", &resetCart))
		ATUIModifyResetFlag(kATUIResetFlag_CartridgeChange, resetCart);

	bool resetVS = (flags & kATUIResetFlag_VideoStandardChange) != 0;
	if (ImGui::Checkbox("Reset when changing video standard", &resetVS))
		ATUIModifyResetFlag(kATUIResetFlag_VideoStandardChange, resetVS);

	bool resetBasic = (flags & kATUIResetFlag_BasicChange) != 0;
	if (ImGui::Checkbox("Reset when toggling internal BASIC", &resetBasic))
		ATUIModifyResetFlag(kATUIResetFlag_BasicChange, resetBasic);
}

// =========================================================================
// Devices (matches Windows IDD_CONFIGURE_DEVICES — placeholder)
// =========================================================================

static void RenderDevicesCategory(ATSimulator &sim) {
	ImGui::TextDisabled("Device management is not yet available in the SDL3 frontend.");
	ImGui::TextDisabled("Use Configure System > Acceleration for SIO/CIO patches.");
}

// =========================================================================
// Media Defaults (matches Windows IDD_CONFIGURE_MEDIADEFAULTS)
// =========================================================================

static void RenderMediaDefaultsCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Default write mode");
	ImGui::TextWrapped("Controls the default write mode used when mounting new disk or tape images.");

	static const char *kWriteModeLabels[] = {
		"Read Only", "Virtual R/W (Safe)", "Virtual R/W", "Read/Write"
	};
	static const ATMediaWriteMode kWriteValues[] = {
		kATMediaWriteMode_RO, kATMediaWriteMode_VRWSafe,
		kATMediaWriteMode_VRW, kATMediaWriteMode_RW,
	};
	int wmIdx = 0;
	for (int i = 0; i < 4; ++i)
		if (kWriteValues[i] == g_ATOptions.mDefaultWriteMode) { wmIdx = i; break; }
	if (ImGui::Combo("Write mode", &wmIdx, kWriteModeLabels, 4))
		g_ATOptions.mDefaultWriteMode = kWriteValues[wmIdx];
}

// =========================================================================
// Enhanced Text (matches Windows IDD_CONFIGURE_ENHANCEDTEXT)
// =========================================================================

static void RenderEnhancedTextCategory(ATSimulator &) {
	ImGui::SeparatorText("Enhanced text output");

	static const char *kModes[] = { "None", "Hardware", "Software" };
	ATUIEnhancedTextMode mode = ATUIGetEnhancedTextMode();
	int modeIdx = (int)mode;
	if (modeIdx < 0 || modeIdx >= 3) modeIdx = 0;
	if (ImGui::Combo("Mode", &modeIdx, kModes, 3))
		ATUISetEnhancedTextMode((ATUIEnhancedTextMode)modeIdx);

	ImGui::TextWrapped(
		"Hardware mode uses the video display for text rendering.\n"
		"Software mode renders text independently of video output.");
}

// =========================================================================
// Caption (matches Windows IDD_CONFIGURE_CAPTION)
// =========================================================================

static void RenderCaptionCategory(ATSimulator &) {
	ImGui::SeparatorText("Window caption template");

	ImGui::TextWrapped(
		"Customize the window title bar. Available variables:\n"
		"  $(profile) - current profile name\n"
		"  $(hardware) - hardware mode\n"
		"  $(video) - video standard\n"
		"  $(speed) - speed setting\n"
		"  $(fps) - frames per second");

	// Sync buffer from accessor on first render and when not actively editing
	static char captionBuf[256] = {};
	static bool editing = false;
	if (!editing) {
		const char *tmpl = ATUIGetWindowCaptionTemplate();
		if (tmpl) {
			strncpy(captionBuf, tmpl, sizeof(captionBuf) - 1);
			captionBuf[sizeof(captionBuf) - 1] = 0;
		}
	}

	if (ImGui::InputText("Template", captionBuf, sizeof(captionBuf))) {
		ATUISetWindowCaptionTemplate(captionBuf);
		editing = true;
	}
	if (!ImGui::IsItemActive())
		editing = false;

	if (ImGui::Button("Reset to Default")) {
		captionBuf[0] = 0;
		ATUISetWindowCaptionTemplate("");
		editing = false;
	}
}

// =========================================================================
// Workarounds (matches Windows IDD_CONFIGURE_WORKAROUNDS)
// =========================================================================

static void RenderWorkaroundsCategory(ATSimulator &) {
	extern ATOptions g_ATOptions;

	ImGui::SeparatorText("Directory polling");

	bool poll = g_ATOptions.mbPollDirectories;
	if (ImGui::Checkbox("Poll directories for changes (H: device)", &poll))
		g_ATOptions.mbPollDirectories = poll;

	ImGui::TextWrapped(
		"When enabled, the H: device will periodically check for external "
		"changes to host directories. Disable if experiencing performance "
		"issues with large directories.");
}

// =========================================================================
// System Configuration window — paged dialog with hierarchical sidebar
// Matches Windows tree: Computer, Outputs, Peripherals, Media, Emulator
// =========================================================================

struct TreeEntry {
	const char *label;
	int catId;       // -1 = header only (non-selectable)
	int indent;      // 0 = top-level header, 1 = leaf
};

static const TreeEntry kTreeEntries[] = {
	// Computer
	{ "Computer",       -1,             0 },
	{ "System",         kCat_System,    1 },
	{ "CPU",            kCat_CPU,       1 },
	{ "Firmware",       kCat_Firmware,  1 },
	{ "Memory",         kCat_Memory,    1 },
	{ "Acceleration",   kCat_Acceleration, 1 },
	{ "Speed",          kCat_Speed,     1 },
	{ "Boot",           kCat_Boot,      1 },
	// Outputs
	{ "Outputs",        -1,             0 },
	{ "Video",          kCat_Video,     1 },
	{ "Audio",          kCat_Audio,     1 },
	// Peripherals
	{ "Peripherals",    -1,             0 },
	{ "Devices",        kCat_Devices,   1 },
	{ "Keyboard",       kCat_Keyboard,  1 },
	// Media
	{ "Media",          -1,             0 },
	{ "Defaults",       kCat_MediaDefaults, 1 },
	{ "Disk",           kCat_Disk,      1 },
	{ "Cassette",       kCat_Cassette,  1 },
	// Emulator
	{ "Emulator",       -1,             0 },
	{ "Display",        kCat_Display,   1 },
	{ "Input",          kCat_Input,     1 },
	{ "Ease of Use",    kCat_EaseOfUse, 1 },
	{ "Enhanced Text",  kCat_EnhancedText, 1 },
	{ "Caption",        kCat_Caption,   1 },
	{ "Workarounds",    kCat_Workarounds, 1 },
};
static const int kNumTreeEntries = sizeof(kTreeEntries) / sizeof(kTreeEntries[0]);

void ATUIRenderSystemConfig(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Configure System", &state.showSystemConfig)) {
		ImGui::End();
		return;
	}

	// Reserve space at bottom for OK button
	float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

	// Left sidebar — tree hierarchy
	ImGui::BeginChild("##SysCfgTree", ImVec2(150, -footerHeight), ImGuiChildFlags_Borders);
	for (int i = 0; i < kNumTreeEntries; ++i) {
		const TreeEntry& te = kTreeEntries[i];
		if (te.indent == 0) {
			// Category header (non-selectable)
			ImGui::Spacing();
			ImGui::TextDisabled("%s", te.label);
		} else {
			// Leaf item (selectable)
			ImGui::Indent(8.0f);
			if (ImGui::Selectable(te.label, state.systemConfigCategory == te.catId))
				state.systemConfigCategory = te.catId;
			ImGui::Unindent(8.0f);
		}
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Right content
	ImGui::BeginChild("##SysCfgContent", ImVec2(0, -footerHeight));
	switch (state.systemConfigCategory) {
	case kCat_System:        RenderSystemCategory(sim); break;
	case kCat_CPU:           RenderCPUCategory(sim); break;
	case kCat_Firmware:      RenderFirmwareCategory(sim); break;
	case kCat_Memory:        RenderMemoryCategory(sim); break;
	case kCat_Acceleration:  RenderAccelerationCategory(sim); break;
	case kCat_Speed:         RenderSpeedCategory(sim); break;
	case kCat_Boot:          RenderBootCategory(sim); break;
	case kCat_Video:         RenderVideoCategory(sim); break;
	case kCat_Audio:         RenderAudioCategory(sim); break;
	case kCat_Devices:       RenderDevicesCategory(sim); break;
	case kCat_Keyboard:      RenderKeyboardCategory(sim); break;
	case kCat_Disk:          RenderDiskCategory(sim); break;
	case kCat_Cassette:      RenderCassetteCategory(sim); break;
	case kCat_MediaDefaults: RenderMediaDefaultsCategory(sim); break;
	case kCat_Display:       RenderDisplayCategory(sim); break;
	case kCat_Input:         RenderInputCategory(sim); break;
	case kCat_EaseOfUse:     RenderEaseOfUseCategory(sim); break;
	case kCat_EnhancedText:  RenderEnhancedTextCategory(sim); break;
	case kCat_Caption:       RenderCaptionCategory(sim); break;
	case kCat_Workarounds:   RenderWorkaroundsCategory(sim); break;
	}
	ImGui::EndChild();

	// OK button — matches Windows DEFPUSHBUTTON "OK"
	ImGui::Separator();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showSystemConfig = false;

	ImGui::End();
}
