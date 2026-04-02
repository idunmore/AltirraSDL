//	AltirraSDL - Device Configuration Dialogs
//	Implements per-device settings dialogs matching Windows Altirra's
//	uiconfdev*.cpp dialogs using Dear ImGui.
//
//	Each device type identified by its configTag gets a specific dialog
//	with the same controls as the Windows version. Devices without a
//	specific dialog get a generic property editor via EnumProperties().

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/device.h>
#include "devicemanager.h"
#include "ui_main.h"

// =========================================================================
// Helpers
// =========================================================================

// Convert wchar_t property string to UTF-8 for ImGui display
static VDStringA WToU8(const wchar_t *s) {
	return s ? VDTextWToU8(VDStringW(s)) : VDStringA();
}

// Convert UTF-8 ImGui string to wchar_t for property storage
static VDStringW U8ToW(const char *s) {
	return VDTextU8ToW(VDStringA(s));
}

// =========================================================================
// Dialog state — persistent across frames while dialog is open
// =========================================================================

struct ATDeviceConfigState {
	bool open = false;
	bool justOpened = false;
	IATDevice *pDev = nullptr;
	ATPropertySet props;
	VDStringA configTag;
	VDStringA deviceName;

	// Scratch buffers for text inputs
	char pathBuf[1024] = {};
	char addrBuf[256] = {};
	char svcBuf[256] = {};
	char portBuf[32] = {};
	char baudBuf[32] = {};
	char mappingBuf[64] = {};
	// Extra path buffers for HostFS (4 paths need >32 bytes each)
	char extraPaths[3][512] = {};

	// Combo selections
	int combo[16] = {};
	// Checkbox states
	bool check[16] = {};
	// Int values
	int intVal[8] = {};

	void Reset() {
		open = false;
		justOpened = false;
		pDev = nullptr;
		props.Clear();
		configTag.clear();
		deviceName.clear();
		memset(pathBuf, 0, sizeof(pathBuf));
		memset(addrBuf, 0, sizeof(addrBuf));
		memset(svcBuf, 0, sizeof(svcBuf));
		memset(portBuf, 0, sizeof(portBuf));
		memset(baudBuf, 0, sizeof(baudBuf));
		memset(mappingBuf, 0, sizeof(mappingBuf));
		memset(extraPaths, 0, sizeof(extraPaths));
		memset(combo, 0, sizeof(combo));
		memset(check, 0, sizeof(check));
		memset(intVal, 0, sizeof(intVal));
	}
};

static ATDeviceConfigState g_devCfg;

// Forward declaration for generic editor cleanup (defined later in file)
static void CleanupGenericEntries();

// =========================================================================
// Forward declarations for per-device dialog renderers
// Each returns true if user clicked OK (apply settings)
// =========================================================================

static bool RenderCovoxConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool Render850Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool Render850FullConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderModemConfig(ATPropertySet& props, ATDeviceConfigState& st, bool fullEmu, bool is835);
static bool RenderSX212Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderPocketModemConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderHardDiskConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderBlackBoxConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderVBXEConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderSoundBoardConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderDiskDriveFullConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderSIDE3Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderXEP80Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderCorvusConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderVeronicaConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderDongleConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderKMKJZIDEConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderKMKJZIDE2Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderMyIDE2Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderHDVirtFATConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderPCLinkConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderHostFSConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderCustomDeviceConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderComputerEyesConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderVideoStillImageConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderNetSerialConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderPipeSerialConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderPrinterConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderPrinterHLEConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderPercomConfig(ATPropertySet& props, ATDeviceConfigState& st, bool atMode, bool atSPDMode);
static bool RenderAMDCConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderBlackBoxFloppyConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderHappy810Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool Render815Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderATR8000Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool Render1020Config(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderMultiplexerConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderParFileWriterConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderKarinMaxiDriveConfig(ATPropertySet& props, ATDeviceConfigState& st);
static bool RenderGenericConfig(ATPropertySet& props, ATDeviceConfigState& st);

// =========================================================================
// Public API
// =========================================================================

void ATUIOpenDeviceConfig(IATDevice *dev, ATDeviceManager *devMgr) {
	if (!dev || !devMgr)
		return;

	ATDeviceInfo info;
	dev->GetDeviceInfo(info);

	if (!info.mpDef->mpConfigTag)
		return;

	g_devCfg.Reset();
	g_devCfg.open = true;
	g_devCfg.justOpened = true;
	g_devCfg.pDev = dev;
	g_devCfg.configTag = info.mpDef->mpConfigTag;
	g_devCfg.deviceName = WToU8(info.mpDef->mpName);
	dev->GetSettings(g_devCfg.props);
}

bool ATUIIsDeviceConfigOpen() {
	return g_devCfg.open;
}

void ATUICloseDeviceConfigFor(IATDevice *dev) {
	if (g_devCfg.open && g_devCfg.pDev == dev) {
		CleanupGenericEntries();
		g_devCfg.Reset();
	}
}

// Dispatch to the appropriate per-device dialog renderer
static bool DispatchDeviceDialog(const char *tag, ATPropertySet& props, ATDeviceConfigState& st) {
	// Covox
	if (!strcmp(tag, "covox")) return RenderCovoxConfig(props, st);

	// 850 interface
	if (!strcmp(tag, "850")) return Render850Config(props, st);
	if (!strcmp(tag, "850full")) return Render850FullConfig(props, st);

	// Modems
	if (!strcmp(tag, "modem")) return RenderModemConfig(props, st, false, false);
	if (!strcmp(tag, "1030")) return RenderModemConfig(props, st, false, false);
	if (!strcmp(tag, "1030full")) return RenderModemConfig(props, st, true, false);
	if (!strcmp(tag, "835")) return RenderModemConfig(props, st, false, true);
	if (!strcmp(tag, "835full")) return RenderModemConfig(props, st, true, true);
	// 1400XL is a simpler modem: has unthrottled but no connect_rate/check_rate/emulevel
	if (!strcmp(tag, "1400xl")) return RenderModemConfig(props, st, false, true);
	if (!strcmp(tag, "sx212")) return RenderSX212Config(props, st);
	if (!strcmp(tag, "pocketmodem")) return RenderPocketModemConfig(props, st);

	// Hard disk
	if (!strcmp(tag, "harddisk")) return RenderHardDiskConfig(props, st);

	// Black Box
	if (!strcmp(tag, "blackbox")) return RenderBlackBoxConfig(props, st);
	if (!strcmp(tag, "blackboxfloppy")) return RenderBlackBoxFloppyConfig(props, st);

	// Video/display
	if (!strcmp(tag, "vbxe")) return RenderVBXEConfig(props, st);
	if (!strcmp(tag, "xep80")) return RenderXEP80Config(props, st);
	if (!strcmp(tag, "computereyes")) return RenderComputerEyesConfig(props, st);
	if (!strcmp(tag, "videostillimage")) return RenderVideoStillImageConfig(props, st);

	// Sound
	if (!strcmp(tag, "soundboard")) return RenderSoundBoardConfig(props, st);

	// Disk drives
	if (!strcmp(tag, "diskdriveatr8000")) return RenderATR8000Config(props, st);
	if (!strcmp(tag, "diskdrivepercom")) return RenderPercomConfig(props, st, false, false);
	if (!strcmp(tag, "diskdrivepercomat")) return RenderPercomConfig(props, st, true, false);
	if (!strcmp(tag, "diskdrivepercomatspd")) return RenderPercomConfig(props, st, true, true);
	if (!strcmp(tag, "diskdriveamdc")) return RenderAMDCConfig(props, st);
	if (!strcmp(tag, "diskdrivehappy810")) return RenderHappy810Config(props, st);
	if (!strcmp(tag, "diskdrive815")) return Render815Config(props, st);

	// Full emulation disk drives (all share same dialog)
	if (!strncmp(tag, "diskdrive", 9)) return RenderDiskDriveFullConfig(props, st);

	// Cartridges / expansion
	if (!strcmp(tag, "side3")) return RenderSIDE3Config(props, st);
	if (!strcmp(tag, "veronica")) return RenderVeronicaConfig(props, st);
	if (!strcmp(tag, "corvus")) return RenderCorvusConfig(props, st);
	if (!strcmp(tag, "dongle")) return RenderDongleConfig(props, st);
	if (!strcmp(tag, "karinmaxidrive")) return RenderKarinMaxiDriveConfig(props, st);

	// IDE
	if (!strcmp(tag, "kmkjzide")) return RenderKMKJZIDEConfig(props, st);
	if (!strcmp(tag, "kmkjzide2")) return RenderKMKJZIDE2Config(props, st);
	if (!strcmp(tag, "myide2")) return RenderMyIDE2Config(props, st);

	// Host filesystem
	if (!strcmp(tag, "hdvirtfat16") || !strcmp(tag, "hdvirtfat32") || !strcmp(tag, "hdvirtsdfs"))
		return RenderHDVirtFATConfig(props, st);
	if (!strcmp(tag, "pclink")) return RenderPCLinkConfig(props, st);
	if (!strcmp(tag, "hostfs")) return RenderHostFSConfig(props, st);

	// Custom device
	if (!strcmp(tag, "custom")) return RenderCustomDeviceConfig(props, st);

	// Serial/network
	if (!strcmp(tag, "netserial")) return RenderNetSerialConfig(props, st);
	if (!strcmp(tag, "pipeserial")) return RenderPipeSerialConfig(props, st);
	if (!strcmp(tag, "dragoncart")) return RenderGenericConfig(props, st);

	// Printers
	if (!strcmp(tag, "820") || !strcmp(tag, "1025") || !strcmp(tag, "1029"))
		return RenderPrinterConfig(props, st);
	if (!strcmp(tag, "printer")) return RenderPrinterHLEConfig(props, st);
	if (!strcmp(tag, "1020")) return Render1020Config(props, st);

	// Multiplexer
	if (!strcmp(tag, "multiplexer")) return RenderMultiplexerConfig(props, st);

	// Parallel port
	if (!strcmp(tag, "parfilewriter")) return RenderParFileWriterConfig(props, st);

	// Fallback: generic property editor
	return RenderGenericConfig(props, st);
}

void ATUIRenderDeviceConfig(ATDeviceManager *devMgr) {
	if (!g_devCfg.open)
		return;

	VDStringA title;
	title.sprintf("%s Settings###DeviceConfig", g_devCfg.deviceName.c_str());

	ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (g_devCfg.justOpened) {
		ImGui::SetNextWindowFocus();
	}

	bool windowOpen = true;
	if (ImGui::Begin(title.c_str(), &windowOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		bool applied = DispatchDeviceDialog(g_devCfg.configTag.c_str(), g_devCfg.props, g_devCfg);

		if (applied && g_devCfg.pDev && devMgr) {
			try {
				devMgr->ReconfigureDevice(*g_devCfg.pDev, g_devCfg.props);
			} catch (...) {
				fprintf(stderr, "[AltirraSDL] Failed to reconfigure device\n");
			}
			g_devCfg.Reset();
		}
	}
	// Clear justOpened AFTER dispatch so per-device renderers can see it
	g_devCfg.justOpened = false;
	ImGui::End();

	if (!windowOpen) {
		CleanupGenericEntries(); // clean up generic editor if it was active
		g_devCfg.Reset();
	}
}

// =========================================================================
// Covox — base address + channels
// =========================================================================

static bool RenderCovoxConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	// Must match Windows kRanges[] in uiconfdevcovox.cpp exactly
	static const char *kAddressLabels[] = {
		"$D100-D1FF", "$D280-D2FF", "$D500-D5FF",
		"$D600-D63F", "$D600-D6FF", "$D700-D7FF"
	};
	static const uint32 kAddressBase[] = { 0xD100, 0xD280, 0xD500, 0xD600, 0xD600, 0xD700 };
	static const uint32 kAddressSize[] = { 0x100, 0x80, 0x100, 0x40, 0x100, 0x100 };

	static const char *kChannelLabels[] = { "1 channel (mono)", "4 channels (stereo)" };

	if (st.justOpened) {
		uint32 base = props.GetUint32("base", 0xD600);
		uint32 size = props.GetUint32("size", 0);
		uint32 maxSize = size ? size : 0x100;
		// Find largest range with matching base address within specified size
		// (matches Windows "find largest range" algorithm)
		st.combo[0] = 4; // default: index 4 = $D600-D6FF
		uint32 bestSize = 0;
		for (int i = 0; i < 6; ++i) {
			if (kAddressBase[i] == base && kAddressSize[i] <= maxSize && kAddressSize[i] > bestSize) {
				st.combo[0] = i;
				bestSize = kAddressSize[i];
			}
		}
		st.combo[1] = props.GetUint32("channels", 4) > 1 ? 1 : 0;
	}

	ImGui::Combo("Address range", &st.combo[0], kAddressLabels, 6);
	ImGui::Combo("Channels", &st.combo[1], kChannelLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		int sel = st.combo[0];
		if (sel >= 0 && sel < 6) {
			props.SetUint32("base", kAddressBase[sel]);
			props.SetUint32("size", kAddressSize[sel]);
		}
		props.SetUint32("channels", st.combo[1] > 0 ? 4 : 1);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 850 Interface — SIO emulation level + checkboxes
// =========================================================================

static bool Render850Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kSIOLabels[] = { "None", "Minimal", "Full" };

	if (st.justOpened) {
		uint32 level = props.GetUint32("emulevel", 0);
		st.combo[0] = (level < 3) ? (int)level : 0;
		st.check[0] = props.GetBool("unthrottled", false);
		st.check[1] = props.GetBool("baudex", false);
	}

	ImGui::Combo("SIO emulation level", &st.combo[0], kSIOLabels, 3);
	ImGui::Checkbox("Disable throttling", &st.check[0]);
	ImGui::Checkbox("Extended baud rates", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.combo[0] > 0)
			props.SetUint32("emulevel", (uint32)st.combo[0]);
		if (st.check[0]) props.SetBool("unthrottled", true);
		if (st.check[1]) props.SetBool("baudex", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 850 Full Emulation — 4 per-port baud rates
// (matches Windows ATUIConfDev850Full using generic config with serbaud1-4)
// =========================================================================

static bool Render850FullConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	// Labels and values must match Windows AddChoice order exactly
	static const char *kBaudLabels[] = {
		"Auto", "45.5 baud", "50 baud", "56.875 baud",
		"75 baud", "110 baud", "134.5 baud", "150 baud",
		"300 baud", "600 baud", "1200 baud", "1800 baud",
		"2400 baud", "4800 baud", "9600 baud"
	};
	static const int kBaudValues[] = {
		0, 2, 3, 4, 5, 6, 7, 8, 1, 10, 11, 12, 13, 14, 15
	};
	static const int kNumBaud = 15;

	if (st.justOpened) {
		for (int p = 0; p < 4; ++p) {
			char key[16];
			snprintf(key, sizeof(key), "serbaud%d", p + 1);
			uint32 val = props.GetUint32(key, 0);
			st.combo[p] = 0; // default Auto
			for (int i = 0; i < kNumBaud; ++i) {
				if ((uint32)kBaudValues[i] == val) { st.combo[p] = i; break; }
			}
		}
	}

	for (int p = 0; p < 4; ++p) {
		char label[32];
		snprintf(label, sizeof(label), "Port %d baud rate", p + 1);
		ImGui::Combo(label, &st.combo[p], kBaudLabels, kNumBaud);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int p = 0; p < 4; ++p) {
			char key[16];
			snprintf(key, sizeof(key), "serbaud%d", p + 1);
			int idx = st.combo[p];
			if (idx >= 0 && idx < kNumBaud)
				props.SetUint32(key, (uint32)kBaudValues[idx]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Modem — full modem config (1030, 835, 1400xl, generic modem)
// Note: SX212 and PocketModem have their own dialogs below
// =========================================================================

static bool RenderModemConfig(ATPropertySet& props, ATDeviceConfigState& st, bool fullEmu, bool is835) {
	// Determine if this is the generic modem (has check_rate + full speed combo)
	// vs 1030/835 (no check_rate, different controls)
	// The dispatch sends modem/1400xl with fullEmu=false,is835=false
	// and 1030/835 with their respective flags
	static const char *kTermTypes[] = {
		"(none)", "ansi", "dec-vt52", "dec-vt100", "vt52", "vt100", "vt102", "vt320"
	};
	static const char *kNetModes[] = {
		"Disabled", "Minimal (simulate dialing)", "Full (dialing + handshaking)"
	};
	static const char *kNetModeValues[] = { "none", "minimal", "full" };
	static const char *kSIOLabels[] = { "None", "Minimal", "Full" };

	static const uint32 kConnSpeeds[] = {
		300, 600, 1200, 2400, 4800, 7200, 9600, 12000, 14400, 19200, 38400, 57600, 115200, 230400
	};
	static const char *kConnSpeedLabels[] = {
		"300", "600", "1200", "2400", "4800", "7200", "9600", "12000",
		"14400", "19200", "38400", "57600", "115200", "230400"
	};

	if (st.justOpened) {
		uint32 port = props.GetUint32("port", 0);
		st.check[0] = (port > 0); // accept connections
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", port > 0 ? port : 9000);
		st.check[1] = props.GetBool("outbound", true);
		st.check[2] = props.GetBool("telnet", true);
		st.check[3] = props.GetBool("telnetlf", true);
		st.check[4] = props.GetBool("ipv6", true);
		st.check[5] = props.GetBool("unthrottled", false);
		st.check[6] = props.GetBool("check_rate", false);

		// Terminal type
		const wchar_t *tt = props.GetString("termtype");
		st.combo[0] = 0;
		if (tt) {
			VDStringA ttU8 = WToU8(tt);
			for (int i = 1; i < 8; ++i) {
				if (!strcmp(kTermTypes[i], ttU8.c_str())) {
					st.combo[0] = i;
					break;
				}
			}
		}

		// Network mode
		const wchar_t *nm = props.GetString("netmode", L"full");
		st.combo[1] = 2; // default full
		if (nm) {
			VDStringA nmU8 = WToU8(nm);
			for (int i = 0; i < 3; ++i) {
				if (!strcmp(kNetModeValues[i], nmU8.c_str())) {
					st.combo[1] = i;
					break;
				}
			}
		}

		// SIO level
		st.combo[2] = (int)props.GetUint32("emulevel", 0);
		if (st.combo[2] > 2) st.combo[2] = 0;

		// Connect speed
		uint32 speed = props.GetUint32("connect_rate", 9600);
		st.combo[3] = 6; // 9600 default
		for (int i = 0; i < 14; ++i) {
			if (kConnSpeeds[i] >= speed) { st.combo[3] = i; break; }
		}

		// Dial address/service
		const wchar_t *da = props.GetString("dialaddr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(da).c_str());
		const wchar_t *ds = props.GetString("dialsvc", L"");
		snprintf(st.svcBuf, sizeof(st.svcBuf), "%s", WToU8(ds).c_str());
	}

	// Incoming connections
	ImGui::SeparatorText("Incoming");
	ImGui::Checkbox("Accept connections", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Listen port", st.portBuf, sizeof(st.portBuf));
	ImGui::Checkbox("Accept IPv6", &st.check[4]);
	ImGui::EndDisabled();

	// Outgoing connections
	ImGui::SeparatorText("Outgoing");
	ImGui::Checkbox("Allow outbound", &st.check[1]);
	ImGui::BeginDisabled(!st.check[1]);
	ImGui::InputText("Dial address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Dial service", st.svcBuf, sizeof(st.svcBuf));
	ImGui::Combo("Terminal type", &st.combo[0], kTermTypes, 8);
	ImGui::EndDisabled();

	// Protocol
	ImGui::SeparatorText("Protocol");
	ImGui::Checkbox("Telnet protocol", &st.check[2]);
	ImGui::BeginDisabled(!st.check[2]);
	ImGui::Checkbox("Telnet LF conversion", &st.check[3]);
	ImGui::EndDisabled();

	ImGui::Combo("Network mode", &st.combo[1], kNetModes, 3);

	// Connection speed, SIO level, check_rate only for generic modem (not 1030/835/1400xl)
	if (!fullEmu && !is835) {
		ImGui::Combo("Connection speed", &st.combo[3], kConnSpeedLabels, 14);
	}

	if (!fullEmu) {
		ImGui::Checkbox("Disable throttling", &st.check[5]);
	}

	if (!fullEmu && !is835) {
		ImGui::Combo("SIO emulation level", &st.combo[2], kSIOLabels, 3);
		ImGui::Checkbox("Require matched DTE rate", &st.check[6]);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) {
			unsigned p = 0;
			sscanf(st.portBuf, "%u", &p);
			if (p >= 1 && p <= 65535)
				props.SetUint32("port", p);
		}
		props.SetBool("outbound", st.check[1]);
		props.SetBool("telnet", st.check[2]);
		props.SetBool("telnetlf", st.check[3]);
		props.SetBool("ipv6", st.check[4]);
		if (!fullEmu)
			props.SetBool("unthrottled", st.check[5]);
		// check_rate only exists on generic modem (not 1030/835)
		if (!fullEmu && !is835)
			props.SetBool("check_rate", st.check[6]);

		// connect_rate only for generic modem (not 1030/835/1400xl)
		if (!fullEmu && !is835 && st.combo[3] >= 0 && st.combo[3] < 14)
			props.SetUint32("connect_rate", kConnSpeeds[st.combo[3]]);

		if (st.combo[0] > 0)
			props.SetString("termtype", U8ToW(kTermTypes[st.combo[0]]).c_str());

		props.SetString("netmode", U8ToW(kNetModeValues[st.combo[1]]).c_str());

		if (!fullEmu && !is835 && st.combo[2] > 0)
			props.SetUint32("emulevel", (uint32)st.combo[2]);

		if (st.addrBuf[0])
			props.SetString("dialaddr", U8ToW(st.addrBuf).c_str());
		if (st.svcBuf[0])
			props.SetString("dialsvc", U8ToW(st.svcBuf).c_str());

		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// SX212 Modem — 300/1200 baud radio + SIO level (None/Full only)
// =========================================================================

static bool RenderSX212Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kTermTypes[] = {
		"(none)", "ansi", "dec-vt52", "dec-vt100", "vt52", "vt100", "vt102", "vt320"
	};
	static const char *kNetModes[] = {
		"Disabled", "Minimal (simulate dialing)", "Full (dialing + handshaking)"
	};
	static const char *kNetModeValues[] = { "none", "minimal", "full" };

	if (st.justOpened) {
		uint32 port = props.GetUint32("port", 0);
		st.check[0] = (port > 0);
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", port > 0 ? port : 9000);
		st.check[1] = props.GetBool("outbound", true);
		st.check[2] = props.GetBool("telnet", true);
		st.check[3] = props.GetBool("telnetlf", true);
		st.check[4] = props.GetBool("ipv6", true);
		st.check[5] = props.GetBool("unthrottled", false);

		// Connect rate: 300 or 1200 (radio buttons)
		st.combo[0] = (props.GetUint32("connect_rate", 1200) <= 300) ? 0 : 1;

		// SIO level: None or Full only
		st.combo[1] = (props.GetUint32("emulevel", 0) > 0) ? 1 : 0;

		// Terminal type
		const wchar_t *tt = props.GetString("termtype");
		st.combo[2] = 0;
		if (tt) {
			VDStringA ttU8 = WToU8(tt);
			for (int i = 1; i < 8; ++i)
				if (!strcmp(kTermTypes[i], ttU8.c_str())) { st.combo[2] = i; break; }
		}

		// Network mode
		const wchar_t *nm = props.GetString("netmode", L"full");
		st.combo[3] = 2;
		if (nm) {
			VDStringA nmU8 = WToU8(nm);
			for (int i = 0; i < 3; ++i)
				if (!strcmp(kNetModeValues[i], nmU8.c_str())) { st.combo[3] = i; break; }
		}

		const wchar_t *da = props.GetString("dialaddr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(da).c_str());
		const wchar_t *ds = props.GetString("dialsvc", L"");
		snprintf(st.svcBuf, sizeof(st.svcBuf), "%s", WToU8(ds).c_str());
	}

	// Incoming
	ImGui::SeparatorText("Incoming");
	ImGui::Checkbox("Accept connections", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Listen port", st.portBuf, sizeof(st.portBuf));
	ImGui::Checkbox("Accept IPv6", &st.check[4]);
	ImGui::EndDisabled();

	// Outgoing
	ImGui::SeparatorText("Outgoing");
	ImGui::Checkbox("Allow outbound", &st.check[1]);
	ImGui::BeginDisabled(!st.check[1]);
	ImGui::InputText("Dial address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Dial service", st.svcBuf, sizeof(st.svcBuf));
	ImGui::Combo("Terminal type", &st.combo[2], kTermTypes, 8);
	ImGui::EndDisabled();

	// Protocol
	ImGui::SeparatorText("Protocol");
	ImGui::Checkbox("Telnet protocol", &st.check[2]);
	ImGui::BeginDisabled(!st.check[2]);
	ImGui::Checkbox("Telnet LF conversion", &st.check[3]);
	ImGui::EndDisabled();

	ImGui::Combo("Network mode", &st.combo[3], kNetModes, 3);

	// Connection speed: 300/1200 radio buttons (not full combo)
	ImGui::SeparatorText("Connection speed");
	ImGui::RadioButton("300 baud", &st.combo[0], 0);
	ImGui::SameLine();
	ImGui::RadioButton("1200 baud", &st.combo[0], 1);

	// SIO emulation: None/Full only (not 3-level)
	static const char *kSIOLabels[] = { "None", "Full" };
	ImGui::Combo("SIO emulation level", &st.combo[1], kSIOLabels, 2);

	ImGui::Checkbox("Disable throttling", &st.check[5]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) {
			unsigned p = 0;
			sscanf(st.portBuf, "%u", &p);
			if (p >= 1 && p <= 65535)
				props.SetUint32("port", p);
		}
		props.SetBool("outbound", st.check[1]);
		props.SetBool("telnet", st.check[2]);
		props.SetBool("telnetlf", st.check[3]);
		props.SetBool("ipv6", st.check[4]);
		props.SetBool("unthrottled", st.check[5]);
		props.SetUint32("connect_rate", st.combo[0] == 0 ? 300 : 1200);
		// SIO level: 0=None, kAT850SIOEmulationLevel_Full for Full
		// Windows uses kAT850SIOEmulationLevel_Full which is typically 2
		props.SetUint32("emulevel", st.combo[1] > 0 ? 2 : 0);

		if (st.combo[2] > 0)
			props.SetString("termtype", U8ToW(kTermTypes[st.combo[2]]).c_str());
		props.SetString("netmode", U8ToW(kNetModeValues[st.combo[3]]).c_str());

		if (st.addrBuf[0])
			props.SetString("dialaddr", U8ToW(st.addrBuf).c_str());
		if (st.svcBuf[0])
			props.SetString("dialsvc", U8ToW(st.svcBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Pocket Modem — minimal modem config (no speed/SIO/throttle/check_rate)
// =========================================================================

static bool RenderPocketModemConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kTermTypes[] = {
		"(none)", "ansi", "dec-vt52", "dec-vt100", "vt52", "vt100", "vt102", "vt320"
	};

	if (st.justOpened) {
		uint32 port = props.GetUint32("port", 0);
		st.check[0] = (port > 0);
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", port > 0 ? port : 9000);
		st.check[1] = props.GetBool("outbound", true);
		st.check[2] = props.GetBool("telnet", true);
		st.check[3] = props.GetBool("telnetlf", true);
		st.check[4] = props.GetBool("ipv6", true);

		// Terminal type
		const wchar_t *tt = props.GetString("termtype");
		st.combo[0] = 0;
		if (tt) {
			VDStringA ttU8 = WToU8(tt);
			for (int i = 1; i < 8; ++i)
				if (!strcmp(kTermTypes[i], ttU8.c_str())) { st.combo[0] = i; break; }
		}

		const wchar_t *da = props.GetString("dialaddr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(da).c_str());
		const wchar_t *ds = props.GetString("dialsvc", L"");
		snprintf(st.svcBuf, sizeof(st.svcBuf), "%s", WToU8(ds).c_str());
	}

	// Incoming
	ImGui::SeparatorText("Incoming");
	ImGui::Checkbox("Accept connections", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::InputText("Listen port", st.portBuf, sizeof(st.portBuf));
	ImGui::Checkbox("Accept IPv6", &st.check[4]);
	ImGui::EndDisabled();

	// Outgoing
	ImGui::SeparatorText("Outgoing");
	ImGui::Checkbox("Allow outbound", &st.check[1]);
	ImGui::BeginDisabled(!st.check[1]);
	ImGui::InputText("Dial address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Dial service", st.svcBuf, sizeof(st.svcBuf));
	ImGui::Combo("Terminal type", &st.combo[0], kTermTypes, 8);
	ImGui::EndDisabled();

	// Protocol
	ImGui::SeparatorText("Protocol");
	ImGui::Checkbox("Telnet protocol", &st.check[2]);
	ImGui::BeginDisabled(!st.check[2]);
	ImGui::Checkbox("Telnet LF conversion", &st.check[3]);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) {
			unsigned p = 0;
			sscanf(st.portBuf, "%u", &p);
			if (p >= 1 && p <= 65535)
				props.SetUint32("port", p);
		}
		props.SetBool("outbound", st.check[1]);
		if (st.combo[0] > 0)
			props.SetString("termtype", U8ToW(kTermTypes[st.combo[0]]).c_str());
		props.SetBool("telnet", st.check[2]);
		props.SetBool("telnetlf", st.check[3]);
		props.SetBool("ipv6", st.check[4]);

		if (st.addrBuf[0])
			props.SetString("dialaddr", U8ToW(st.addrBuf).c_str());
		if (st.svcBuf[0])
			props.SetString("dialsvc", U8ToW(st.svcBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Hard Disk — path + CHS geometry + options
// =========================================================================

static bool RenderHardDiskConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.intVal[0] = (int)props.GetUint32("cylinders", 0);
		st.intVal[1] = (int)props.GetUint32("heads", 0);
		st.intVal[2] = (int)props.GetUint32("sectors_per_track", 0);
		st.check[0] = !props.GetBool("write_enabled", false); // inverted: readonly
		st.check[1] = props.GetBool("solid_state", false);
	}

	ImGui::InputText("Image path", st.pathBuf, sizeof(st.pathBuf));

	ImGui::SeparatorText("CHS Geometry (0 = auto-detect)");
	ImGui::InputInt("Cylinders", &st.intVal[0]);
	ImGui::InputInt("Heads", &st.intVal[1]);
	ImGui::InputInt("Sectors/track", &st.intVal[2]);

	ImGui::SeparatorText("Options");
	ImGui::Checkbox("Read only", &st.check[0]);
	ImGui::Checkbox("Solid state (SSD)", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (st.pathBuf[0] == 0) {
			// Path required
			return false;
		}
		props.Clear();
		props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.intVal[0] > 0)
			props.SetUint32("cylinders", (uint32)std::clamp(st.intVal[0], 0, 16777216));
		if (st.intVal[1] > 0)
			props.SetUint32("heads", (uint32)std::clamp(st.intVal[1], 0, 16));
		if (st.intVal[2] > 0)
			props.SetUint32("sectors_per_track", (uint32)std::clamp(st.intVal[2], 0, 255));
		props.SetBool("write_enabled", !st.check[0]);
		if (st.check[1]) props.SetBool("solid_state", true);
		// Compute total sector count from CHS geometry (matches Windows)
		if (st.intVal[0] > 0 && st.intVal[1] > 0 && st.intVal[2] > 0) {
			uint32 sectors = (uint32)st.intVal[0] * (uint32)st.intVal[1] * (uint32)st.intVal[2];
			props.SetUint32("sectors", sectors);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Black Box — DIP switches + sector size + RAM
// =========================================================================

static bool RenderBlackBoxConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kSwitchLabels[] = {
		"1: Ignore printer fault line",
		"2: Enable HD and high speed floppy SIO",
		"3: Enable printer port",
		"4: Enable RS232 port",
		"5: Enable printer linefeeds",
		"6: ProWriter printer mode",
		"7: MIO compatibility mode",
		"8: Unused"
	};
	static const char *kRAMLabels[] = { "8K", "32K", "64K" };
	static const uint32 kRAMValues[] = { 8192, 32768, 65536 };

	if (st.justOpened) {
		uint32 dipsw = props.GetUint32("dipsw", 0x0F);
		for (int i = 0; i < 8; ++i)
			st.check[i] = (dipsw & (1 << i)) != 0;

		uint32 blksize = props.GetUint32("blksize", 512);
		st.combo[0] = (blksize == 256) ? 0 : 1;

		uint32 ramsize = props.GetUint32("ramsize", 8192);
		st.combo[1] = 0;
		for (int i = 0; i < 3; ++i)
			if (kRAMValues[i] == ramsize) st.combo[1] = i;
	}

	ImGui::SeparatorText("DIP Switches");
	for (int i = 0; i < 8; ++i)
		ImGui::Checkbox(kSwitchLabels[i], &st.check[i]);

	ImGui::SeparatorText("Hard Disk");
	static const char *kBlkLabels[] = { "256 bytes/sector", "512 bytes/sector" };
	ImGui::Combo("Sector size", &st.combo[0], kBlkLabels, 2);

	ImGui::Combo("SRAM size", &st.combo[1], kRAMLabels, 3);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		uint32 dipsw = 0;
		for (int i = 0; i < 8; ++i)
			if (st.check[i]) dipsw |= (1 << i);
		props.SetUint32("dipsw", dipsw);
		props.SetUint32("blksize", st.combo[0] == 0 ? 256 : 512);
		props.SetUint32("ramsize", kRAMValues[st.combo[1]]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// VBXE — version + base address + shared memory
// =========================================================================

static bool RenderVBXEConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "FX 1.20", "FX 1.24", "FX 1.26" };
	static const uint32 kVersionValues[] = { 120, 124, 126 };

	if (st.justOpened) {
		uint32 ver = props.GetUint32("version", 126);
		st.combo[0] = 2;
		for (int i = 0; i < 3; ++i)
			if (kVersionValues[i] == ver) st.combo[0] = i;

		st.combo[1] = props.GetBool("alt_page", false) ? 1 : 0;
		st.check[0] = props.GetBool("shared_mem", false);
	}

	ImGui::Combo("Core version", &st.combo[0], kVersionLabels, 3);

	static const char *kBaseLabels[] = { "$D600 (standard)", "$D700 (alternate)" };
	ImGui::Combo("Base address", &st.combo[1], kBaseLabels, 2);
	ImGui::Checkbox("Enable shared memory", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("version", kVersionValues[st.combo[0]]);
		if (st.combo[1] > 0) props.SetBool("alt_page", true);
		if (st.check[0]) props.SetBool("shared_mem", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Sound Board — version + base address
// =========================================================================

static bool RenderSoundBoardConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "1.1 (VBXE-based)", "1.2 (with multiplier)", "2.0 Preview" };
	static const uint32 kVersionValues[] = { 110, 120, 200 };
	static const char *kBaseLabels[] = { "$D280", "$D2C0", "$D600", "$D700" };
	static const uint32 kBaseValues[] = { 0xD280, 0xD2C0, 0xD600, 0xD700 };

	if (st.justOpened) {
		uint32 ver = props.GetUint32("version", 120);
		st.combo[0] = 1;
		for (int i = 0; i < 3; ++i)
			if (kVersionValues[i] == ver) st.combo[0] = i;

		uint32 base = props.GetUint32("base", 0xD2C0);
		st.combo[1] = 1;
		for (int i = 0; i < 4; ++i)
			if (kBaseValues[i] == base) st.combo[1] = i;
	}

	ImGui::Combo("Version", &st.combo[0], kVersionLabels, 3);
	ImGui::Combo("Base address", &st.combo[1], kBaseLabels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("version", kVersionValues[st.combo[0]]);
		props.SetUint32("base", kBaseValues[st.combo[1]]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Disk Drive Full Emulation — drive select
// =========================================================================

static bool RenderDiskDriveFullConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveLabels[] = {
		"Drive 1 (D1:)", "Drive 2 (D2:)", "Drive 3 (D3:)", "Drive 4 (D4:)"
	};

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("id", 0);
		if (st.combo[0] > 3) st.combo[0] = 0;
	}

	ImGui::Combo("Drive select", &st.combo[0], kDriveLabels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("id", (uint32)st.combo[0]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// SIDE 3 — version + LED + recovery
// =========================================================================

static bool RenderSIDE3Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = {
		"SIDE 3 (JED 1.1: 2MB RAM)",
		"SIDE 3.1 (JED 1.4: 8MB RAM, enhanced DMA)"
	};

	if (st.justOpened) {
		st.check[0] = props.GetBool("led_enable", true);
		st.check[1] = props.GetBool("recovery", false);
		st.combo[0] = props.GetUint32("version", 10) > 10 ? 1 : 0;
	}

	ImGui::Combo("Version", &st.combo[0], kVersionLabels, 2);
	ImGui::Checkbox("Enable activity LED", &st.check[0]);
	ImGui::Checkbox("Recovery mode", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetBool("led_enable", st.check[0]);
		props.SetBool("recovery", st.check[1]);
		props.SetUint32("version", st.combo[0] > 0 ? 14 : 10);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// XEP-80 — joystick port
// =========================================================================

static bool RenderXEP80Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kPortLabels[] = { "Port 1", "Port 2", "Port 3", "Port 4" };

	if (st.justOpened) {
		// Windows stores as 1-based (1-4), default 2
		uint32 port = props.GetUint32("port", 2);
		st.combo[0] = (int)std::clamp<uint32>(port - 1, 0, 3);
	}

	ImGui::Combo("Joystick port", &st.combo[0], kPortLabels, 4);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Store as 1-based (matching Windows)
		props.SetUint32("port", (uint32)(st.combo[0] & 3) + 1);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Corvus — port selection
// =========================================================================

static bool RenderCorvusConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kPortLabels[] = { "Ports 3+4", "Ports 1+2" };

	if (st.justOpened) {
		st.combo[0] = props.GetBool("altports", false) ? 1 : 0;
	}

	ImGui::Combo("Controller ports", &st.combo[0], kPortLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.combo[0] > 0) props.SetBool("altports", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Veronica — version
// =========================================================================

static bool RenderVeronicaConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "V1 (three RAM chips)", "V2 (single RAM chip)" };

	if (st.justOpened) {
		st.combo[0] = props.GetBool("version1", false) ? 0 : 1;
	}

	ImGui::Combo("Version", &st.combo[0], kVersionLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.combo[0] == 0) props.SetBool("version1", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Dongle — port + mapping
// =========================================================================

static bool RenderDongleConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kPortLabels[] = { "Port 1", "Port 2", "Port 3", "Port 4" };

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("port", 0);
		if (st.combo[0] > 3) st.combo[0] = 0;
		const wchar_t *m = props.GetString("mapping", L"FFFFFFFFFFFFFFFF");
		snprintf(st.mappingBuf, sizeof(st.mappingBuf), "%s", WToU8(m).c_str());
	}

	ImGui::Combo("Joystick port", &st.combo[0], kPortLabels, 4);
	ImGui::InputText("Mapping (hex)", st.mappingBuf, sizeof(st.mappingBuf));
	ImGui::SetItemTooltip("16-character hex string mapping input bits to output bits");

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("port", (uint32)st.combo[0]);
		if (st.mappingBuf[0])
			props.SetString("mapping", U8ToW(st.mappingBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// KMK/JZ IDE — device ID + SDX
// =========================================================================

static bool RenderKMKJZIDEConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kIDLabels[] = { "ID 0", "ID 1", "ID 2", "ID 3", "ID 4", "ID 5", "ID 6", "ID 7" };

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("id", 0);
		if (st.combo[0] > 7) st.combo[0] = 0;
		st.check[0] = props.GetBool("enablesdx", true);
	}

	ImGui::Combo("Device ID", &st.combo[0], kIDLabels, 8);
	ImGui::Checkbox("Enable SDX", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("id", (uint32)st.combo[0]);
		if (st.check[0]) props.SetBool("enablesdx", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// KMK/JZ IDE v2 — revision + ID + SDX + write protect + NVRAM guard
// =========================================================================

static bool RenderKMKJZIDE2Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kRevLabels[] = { "Rev C", "Rev D", "Rev Ds/S", "Rev E" };
	static const char *kRevValues[] = { "c", "d", "s", "e" };
	static const char *kIDLabels[] = { "ID 0", "ID 1", "ID 2", "ID 3", "ID 4", "ID 5", "ID 6", "ID 7" };

	if (st.justOpened) {
		st.combo[0] = 1; // default Rev D
		const wchar_t *rev = props.GetString("revision", L"d");
		if (rev) {
			VDStringA revU8 = WToU8(rev);
			for (int i = 0; i < 4; ++i)
				if (!strcmp(kRevValues[i], revU8.c_str())) { st.combo[0] = i; break; }
		}
		st.combo[1] = (int)props.GetUint32("id", 0);
		if (st.combo[1] > 7) st.combo[1] = 0;
		st.check[0] = props.GetBool("enablesdx", false);
		st.check[1] = props.GetBool("writeprotect", false);
		st.check[2] = props.GetBool("nvramguard", true);
	}

	ImGui::Combo("Revision", &st.combo[0], kRevLabels, 4);
	ImGui::Combo("Device ID", &st.combo[1], kIDLabels, 8);
	ImGui::Checkbox("Enable SDX", &st.check[0]);
	ImGui::Checkbox("Write protect", &st.check[1]);
	ImGui::Checkbox("NVRAM guard", &st.check[2]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetString("revision", U8ToW(kRevValues[st.combo[0]]).c_str());
		props.SetUint32("id", (uint32)st.combo[1]);
		if (st.check[0]) props.SetBool("enablesdx", true);
		if (st.check[1]) props.SetBool("writeprotect", true);
		if (st.check[2]) props.SetBool("nvramguard", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// MyIDE-II — CPLD version
// =========================================================================

static bool RenderMyIDE2Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kVersionLabels[] = { "Original", "Updated (video playback)" };

	if (st.justOpened) {
		// Windows: cpldver >= 2 means v2; combo index 0 = original, 1 = updated
		st.combo[0] = props.GetUint32("cpldver", 0) >= 2 ? 1 : 0;
	}

	ImGui::Combo("CPLD version", &st.combo[0], kVersionLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Windows writes cpldver=2 for v2, nothing for v1 (defaults to 0)
		if (st.combo[0] == 1)
			props.SetUint32("cpldver", 2);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Virtual FAT HD — directory path
// =========================================================================

static bool RenderHDVirtFATConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
	}

	ImGui::InputText("Directory path", st.pathBuf, sizeof(st.pathBuf));
	ImGui::SetItemTooltip("Path to host directory to map as virtual disk");

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		if (st.pathBuf[0] == 0)
			return false;
		props.Clear();
		props.SetString("path", U8ToW(st.pathBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// PCLink — path + write + timestamps
// =========================================================================

static bool RenderPCLinkConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.check[0] = props.GetBool("write", false);
		st.check[1] = props.GetBool("set_timestamps", false);
	}

	ImGui::InputText("Base directory", st.pathBuf, sizeof(st.pathBuf));
	ImGui::Checkbox("Allow writes", &st.check[0]);
	ImGui::Checkbox("Set file timestamps", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.check[0]) props.SetBool("write", true);
		if (st.check[1]) props.SetBool("set_timestamps", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Host FS — 4 paths + LFN mode + readonly + lowercase + fakedisk
// =========================================================================

static bool RenderHostFSConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kLFNLabels[] = {
		"8.3 only, truncate long names",
		"8.3 only, encode long names",
		"Use long file names"
	};

	// Use pathBuf for path1, extraPaths[0..2] for path2-4
	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "path%d", i + 1);
			const wchar_t *p = props.GetString(key, L"");
			char *dest = (i == 0) ? st.pathBuf : st.extraPaths[i - 1];
			int maxLen = (i == 0) ? (int)sizeof(st.pathBuf) : (int)sizeof(st.extraPaths[0]);
			snprintf(dest, maxLen, "%s", WToU8(p).c_str());
		}
		st.check[0] = props.GetBool("readonly", true);
		st.check[1] = props.GetBool("lowercase", true);
		st.check[2] = props.GetBool("fakedisk", false);

		bool encodeLFN = props.GetBool("encodelfn", true);
		bool enableLFN = props.GetBool("longfilenames", false);
		st.combo[0] = enableLFN ? 2 : encodeLFN ? 1 : 0;
	}

	ImGui::InputText("Path 1 (H1:)", st.pathBuf, sizeof(st.pathBuf));
	ImGui::InputText("Path 2 (H2:)", st.extraPaths[0], sizeof(st.extraPaths[0]));
	ImGui::InputText("Path 3 (H3:)", st.extraPaths[1], sizeof(st.extraPaths[1]));
	ImGui::InputText("Path 4 (H4:)", st.extraPaths[2], sizeof(st.extraPaths[2]));

	ImGui::SeparatorText("Options");
	ImGui::Checkbox("Read only", &st.check[0]);
	ImGui::Checkbox("Lowercase filenames", &st.check[1]);
	ImGui::Checkbox("Install as disk", &st.check[2]);
	ImGui::Combo("Long filename mode", &st.combo[0], kLFNLabels, 3);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (!st.check[0]) props.SetBool("readonly", false);
		if (!st.check[1]) props.SetBool("lowercase", false);
		if (st.check[2]) props.SetBool("fakedisk", true);
		props.SetBool("encodelfn", st.combo[0] >= 1);
		props.SetBool("longfilenames", st.combo[0] >= 2);

		const char *bufs[] = { st.pathBuf, st.extraPaths[0], st.extraPaths[1], st.extraPaths[2] };
		for (int i = 0; i < 4; ++i) {
			if (bufs[i][0]) {
				char key[16];
				snprintf(key, sizeof(key), "path%d", i + 1);
				props.SetString(key, U8ToW(bufs[i]).c_str());
			}
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Custom Device — script path + options
// =========================================================================

static bool RenderCustomDeviceConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.check[0] = props.GetBool("hotreload", false);
		st.check[1] = props.GetBool("allowunsafe", false);
	}

	ImGui::InputText("Device descriptor path", st.pathBuf, sizeof(st.pathBuf));
	ImGui::Checkbox("Hot reload on change", &st.check[0]);
	ImGui::Checkbox("Allow unsafe operations", &st.check[1]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.check[0]) props.SetBool("hotreload", true);
		if (st.check[1]) props.SetBool("allowunsafe", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// ComputerEyes — brightness
// =========================================================================

static bool RenderComputerEyesConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		st.intVal[0] = (int)props.GetUint32("brightness", 50);
	}

	ImGui::SliderInt("Brightness", &st.intVal[0], 0, 100, "%d");
	ImGui::SetItemTooltip("Displayed as offset: %d", st.intVal[0] - 50);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("brightness", (uint32)std::clamp(st.intVal[0], 0, 100));
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Video Still Image — image path
// =========================================================================

static bool RenderVideoStillImageConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
	}

	ImGui::InputText("Image file path", st.pathBuf, sizeof(st.pathBuf));

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Network Serial — address + port + baud + listen mode
// =========================================================================

static bool RenderNetSerialConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *addr = props.GetString("connect_addr", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(addr).c_str());
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", props.GetUint32("port", 9000));
		snprintf(st.baudBuf, sizeof(st.baudBuf), "%u", props.GetUint32("baud_rate", 31250));
		st.combo[0] = props.GetBool("listen", false) ? 1 : 0;
	}

	static const char *kModeLabels[] = { "Connect (outbound)", "Listen (inbound)" };
	ImGui::Combo("Mode", &st.combo[0], kModeLabels, 2);

	ImGui::InputText("Address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Port", st.portBuf, sizeof(st.portBuf));
	ImGui::InputText("Baud rate", st.baudBuf, sizeof(st.baudBuf));

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		unsigned port = 0, baud = 0;
		sscanf(st.portBuf, "%u", &port);
		sscanf(st.baudBuf, "%u", &baud);
		if (port < 1 || port > 65535) return false;
		if (baud < 1 || baud > 1000000) return false;
		props.SetUint32("port", port);
		props.SetUint32("baud_rate", baud);
		if (st.addrBuf[0])
			props.SetString("connect_addr", U8ToW(st.addrBuf).c_str());
		if (st.combo[0] > 0) props.SetBool("listen", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Pipe Serial — pipe name + baud rate
// =========================================================================

static bool RenderPipeSerialConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *name = props.GetString("pipe_name", L"AltirraSerial");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(name).c_str());
		snprintf(st.baudBuf, sizeof(st.baudBuf), "%u", props.GetUint32("baud_rate", 9600));
	}

	ImGui::InputText("Pipe name", st.addrBuf, sizeof(st.addrBuf));
	ImGui::InputText("Baud rate", st.baudBuf, sizeof(st.baudBuf));

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.addrBuf[0])
			props.SetString("pipe_name", U8ToW(st.addrBuf).c_str());
		unsigned baud = 0;
		sscanf(st.baudBuf, "%u", &baud);
		if (baud >= 1 && baud <= 1000000)
			props.SetUint32("baud_rate", baud);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Printer (820/1025/1029) — graphics + timing + sound
// =========================================================================

static bool RenderPrinterConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		st.check[0] = props.GetBool("graphics", false);
		st.check[1] = props.GetBool("accurate_timing", false);
		st.check[2] = props.GetBool("sound", false);
	}

	ImGui::Checkbox("Enable graphical output", &st.check[0]);

	ImGui::BeginDisabled(!st.check[0]);
	ImGui::Checkbox("Enable accurate timing", &st.check[1]);
	ImGui::EndDisabled();

	ImGui::BeginDisabled(!st.check[0] || !st.check[1]);
	ImGui::Checkbox("Enable sound", &st.check[2]);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.check[0]) props.SetBool("graphics", true);
		if (st.check[1]) props.SetBool("accurate_timing", true);
		if (st.check[2]) props.SetBool("sound", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Printer HLE (P:) — translation mode
// =========================================================================

static bool RenderPrinterHLEConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kModeLabels[] = {
		"Default: Translate EOL -> CR",
		"Raw: No translation",
		"ATASCII to UTF-8"
	};
	// Enum string values matching ATPrinterPortTranslationMode
	static const char *kModeValues[] = { "default", "raw", "atasciitoutf8" };

	if (st.justOpened) {
		// Read as enum string
		const wchar_t *modeStr = props.GetString("translation_mode");
		st.combo[0] = 0; // default
		if (modeStr) {
			VDStringA modeU8 = WToU8(modeStr);
			for (int i = 0; i < 3; ++i)
				if (!strcmp(kModeValues[i], modeU8.c_str())) { st.combo[0] = i; break; }
		}
	}

	ImGui::Combo("Port translation", &st.combo[0], kModeLabels, 3);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Store as enum string (matching AT_DEFINE_ENUM_TABLE)
		props.SetString("translation_mode", U8ToW(kModeValues[st.combo[0]]).c_str());
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Percom disk controller — FDC type + drive types + drive ID
// =========================================================================

static bool RenderPercomConfig(ATPropertySet& props, ATDeviceConfigState& st, bool atMode, bool atSPDMode) {
	static const char *kDriveTypeLabels[] = {
		"None", "5.25\" (40 track)", "5.25\" (80 track)"
	};

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "drivetype%d", i);
			st.combo[4 + i] = (int)props.GetUint32(key, i == 0 ? 1 : 0);
			if (st.combo[4 + i] > 2) st.combo[4 + i] = 0;
		}

		if (atMode) {
			bool use1795 = props.GetBool("use1795", false);
			if (atSPDMode) {
				st.combo[0] = use1795 ? 1 : 0;
			} else {
				bool ddcapable = props.GetBool("ddcapable", true);
				if (use1795) st.combo[0] = 1;
				else if (!ddcapable) st.combo[0] = 2;
				else st.combo[0] = 0;
			}
		} else {
			st.combo[0] = (int)props.GetUint32("id", 0);
			if (st.combo[0] > 7) st.combo[0] = 0;
		}
	}

	if (atMode) {
		if (atSPDMode) {
			static const char *kFDCLabels[] = {
				"1791 (side compare optional)", "1795 (side compare always on)"
			};
			ImGui::Combo("FDC type", &st.combo[0], kFDCLabels, 2);
		} else {
			static const char *kFDCLabels[] = {
				"1771+1791 (double density, side compare optional)",
				"1771+1795 (double density, side compare always on)",
				"1771 (single density only)"
			};
			ImGui::Combo("FDC type", &st.combo[0], kFDCLabels, 3);
		}
	} else {
		static const char *kIDLabels[] = {
			"D1:", "D2:", "D3:", "D4:", "D5:", "D6:", "D7:", "D8:"
		};
		ImGui::Combo("Drive select", &st.combo[0], kIDLabels, 8);
	}

	ImGui::SeparatorText("Drive types");
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "Drive %d", i + 1);
		ImGui::Combo(label, &st.combo[4 + i], kDriveTypeLabels, 3);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (atMode) {
			if (atSPDMode) {
				props.SetBool("use1795", st.combo[0] != 0);
			} else {
				props.SetBool("use1795", st.combo[0] == 1);
				props.SetBool("ddcapable", st.combo[0] != 2);
			}
		} else {
			props.SetUint32("id", (uint32)st.combo[0]);
		}
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "drivetype%d", i);
			props.SetUint32(key, (uint32)st.combo[4 + i]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// AMDC — drive select + switches + drive types
// =========================================================================

static bool RenderAMDCConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveSelectLabels[] = {
		"Drive 1 (D1:)", "Drive 2 (D2:)", "Drive 3 (D3:)", "Drive 4 (D4:)",
		"Drive 5 (D5:)", "Drive 6 (D6:)", "Drive 7 (D7:)", "Drive 8 (D8:)"
	};
	static const char *kDriveTypeLabels[] = {
		"None", "3\"/5.25\" (40 track)", "3\"/5.25\" (80 track)"
	};
	static const char *kSwitchLabels[] = {
		"SW1", "SW2", "SW3", "SW4", "SW7", "SW8", "Jumper"
	};
	static const uint32 kSwitchBits[] = { 0x01, 0x02, 0x04, 0x08, 0x40, 0x80, 0x100 };

	if (st.justOpened) {
		uint32 sw = props.GetUint32("switches", 0x40);
		st.combo[0] = (int)((sw >> 4) & 3);
		for (int i = 0; i < 7; ++i)
			st.check[i] = (sw & kSwitchBits[i]) != 0;
		st.check[7] = props.GetBool("drive2", false);

		for (int i = 0; i < 2; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "extdrive%d", i);
			st.combo[1 + i] = (int)props.GetUint32(key, 0);
			if (st.combo[1 + i] > 2) st.combo[1 + i] = 0;
		}
	}

	ImGui::Combo("Drive select", &st.combo[0], kDriveSelectLabels, 8);

	ImGui::SeparatorText("DIP Switches");
	for (int i = 0; i < 7; ++i)
		ImGui::Checkbox(kSwitchLabels[i], &st.check[i]);

	ImGui::SeparatorText("External drives");
	ImGui::Checkbox("Drive 2 present", &st.check[7]);
	for (int i = 0; i < 2; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "External drive %d", i + 1);
		ImGui::Combo(label, &st.combo[1 + i], kDriveTypeLabels, 3);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		uint32 sw = ((uint32)st.combo[0] & 3) << 4;
		for (int i = 0; i < 7; ++i)
			if (st.check[i]) sw |= kSwitchBits[i];
		props.SetUint32("switches", sw);
		props.SetBool("drive2", st.check[7]);
		for (int i = 0; i < 2; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "extdrive%d", i);
			props.SetUint32(key, (uint32)st.combo[1 + i]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Black Box Floppy — 4 slots with drive ID + type + mapping
// =========================================================================

static bool RenderBlackBoxFloppyConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveSlotLabels[] = {
		"Not connected", "D1:", "D2:", "D3:", "D4:", "D5:", "D6:", "D7:",
		"D8:", "D9:", "D10:", "D11:", "D12:", "D13:", "D14:"
	};
	static const char *kDriveTypeLabels[] = {
		"180K 5.25\" 40 track, single-sided",
		"360K 5.25\" 40 track, double-sided",
		"1.2M 5.25\" 80 track, double-sided HD",
		"360K 3.5\" 80 track, single-sided",
		"720K 3.5\" 80 track, double-sided",
		"1.4M 3.5\" 80 track, double-sided HD",
		"1M 8\" 77 track, double-sided HD"
	};
	// Enum string values matching ATBlackBoxFloppyType
	static const char *kDriveTypeValues[] = {
		"fiveinch180K", "fiveinch360K", "fiveinch12M",
		"threeinch360K", "threeinch720K", "threeinch144M", "eightinch1M"
	};
	static const char *kMappingLabels[] = {
		"Map double-sided as XF551",
		"Map double-sided as ATR8000",
		"Map double-sided as PERCOM"
	};
	// Enum string values matching ATBlackBoxFloppyMappingType
	static const char *kMappingValues[] = { "xf551", "atr8000", "percom" };

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[32];
			// Drive slot is stored as int (not enum)
			snprintf(key, sizeof(key), "driveslot%d", i);
			st.combo[i * 3] = (int)props.GetUint32(key, 0);
			if (st.combo[i * 3] > 14) st.combo[i * 3] = 0;

			// Drive type is stored as enum string
			snprintf(key, sizeof(key), "drivetype%d", i);
			const wchar_t *dtStr = props.GetString(key);
			st.combo[i * 3 + 1] = 0; // default FiveInch180K
			if (dtStr) {
				VDStringA dtU8 = WToU8(dtStr);
				for (int j = 0; j < 7; ++j)
					if (!strcmp(kDriveTypeValues[j], dtU8.c_str())) { st.combo[i * 3 + 1] = j; break; }
			}

			// Mapping type is stored as enum string
			snprintf(key, sizeof(key), "drivemapping%d", i);
			const wchar_t *dmStr = props.GetString(key);
			st.combo[i * 3 + 2] = 0; // default XF551
			if (dmStr) {
				VDStringA dmU8 = WToU8(dmStr);
				for (int j = 0; j < 3; ++j)
					if (!strcmp(kMappingValues[j], dmU8.c_str())) { st.combo[i * 3 + 2] = j; break; }
			}
		}
	}

	for (int i = 0; i < 4; ++i) {
		ImGui::PushID(i);
		char header[32];
		snprintf(header, sizeof(header), "PBI Floppy %d", i);
		ImGui::SeparatorText(header);

		ImGui::Combo("Drive slot", &st.combo[i * 3], kDriveSlotLabels, 15);
		ImGui::Combo("Drive type", &st.combo[i * 3 + 1], kDriveTypeLabels, 7);
		ImGui::Combo("Mapping", &st.combo[i * 3 + 2], kMappingLabels, 3);
		ImGui::PopID();
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int i = 0; i < 4; ++i) {
			char key[32];
			snprintf(key, sizeof(key), "driveslot%d", i);
			props.SetUint32(key, (uint32)st.combo[i * 3]);
			// Store enum types as string values (matching AT_DEFINE_ENUM_TABLE)
			snprintf(key, sizeof(key), "drivetype%d", i);
			props.SetString(key, U8ToW(kDriveTypeValues[st.combo[i * 3 + 1]]).c_str());
			snprintf(key, sizeof(key), "drivemapping%d", i);
			props.SetString(key, U8ToW(kMappingValues[st.combo[i * 3 + 2]]).c_str());
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Happy 810 — drive ID + auto-speed + speed rate
// =========================================================================

static bool RenderHappy810Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveLabels[] = {
		"Drive 1 (D1:)", "Drive 2 (D2:)", "Drive 3 (D3:)", "Drive 4 (D4:)"
	};

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("id", 0);
		if (st.combo[0] > 3) st.combo[0] = 0;
		st.check[0] = props.GetBool("autospeed", false);
		// Store speed as int * 10 for slider (200-400 range = 2000-4000)
		float speed = props.GetFloat("autospeedrate", 266.0f);
		st.intVal[0] = (int)(speed + 0.5f);
		if (st.intVal[0] < 200) st.intVal[0] = 200;
		if (st.intVal[0] > 400) st.intVal[0] = 400;
	}

	ImGui::Combo("Drive select", &st.combo[0], kDriveLabels, 4);
	ImGui::Checkbox("Auto-speed", &st.check[0]);
	ImGui::BeginDisabled(!st.check[0]);
	ImGui::SliderInt("RPM", &st.intVal[0], 200, 400);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("id", (uint32)st.combo[0]);
		if (st.check[0]) props.SetBool("autospeed", true);
		props.SetFloat("autospeedrate", (float)st.intVal[0]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// 815 Dual Drive — ID + invert mode
// =========================================================================

static bool Render815Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDriveLabels[] = {
		"Drives 1-2 (D1-D2:)", "Drives 3-4 (D3-D4:)",
		"Drives 5-6 (D5-D6:)", "Drives 7-8 (D7-D8:)"
	};

	if (st.justOpened) {
		// Windows stores id as selection << 1 (bit-shifted)
		st.combo[0] = (int)(props.GetUint32("id", 0) >> 1);
		if (st.combo[0] > 3) st.combo[0] = 0;
		st.check[0] = props.GetBool("accurate_invert", false);
	}

	ImGui::Combo("Drive pair", &st.combo[0], kDriveLabels, 4);
	ImGui::Checkbox("Accurate data inversion", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		// Windows stores id as selection << 1 (bit-shifted)
		props.SetUint32("id", (uint32)st.combo[0] << 1);
		if (st.check[0]) props.SetBool("accurate_invert", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// ATR8000 — same interface as Percom but without FDC/AT modes
// =========================================================================

static bool RenderATR8000Config(ATPropertySet& props, ATDeviceConfigState& st) {
	return RenderPercomConfig(props, st, false, false);
}

// =========================================================================
// 1020 Plotter — pen colors
// =========================================================================

static bool Render1020Config(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kColorLabels[] = { "Black", "Blue", "Green", "Red" };

	if (st.justOpened) {
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "pencolor%d", i);
			st.combo[i] = (int)props.GetUint32(key, (uint32)i);
			if (st.combo[i] > 3) st.combo[i] = i;
		}
	}

	ImGui::SeparatorText("Pen Colors");
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "Pen %d", i + 1);
		ImGui::Combo(label, &st.combo[i], kColorLabels, 4);
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (int i = 0; i < 4; ++i) {
			char key[16];
			snprintf(key, sizeof(key), "pencolor%d", i);
			props.SetUint32(key, (uint32)st.combo[i]);
		}
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Multiplexer — device ID + host address + port + external
// =========================================================================

static bool RenderMultiplexerConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kDeviceLabels[] = {
		"Host", "Client (ID 1)", "Client (ID 2)", "Client (ID 3)",
		"Client (ID 4)", "Client (ID 5)", "Client (ID 6)",
		"Client (ID 7)", "Client (ID 8)"
	};
	// Maps: combo index 0 = -1 (host), 1 = 0 (client 1), etc.

	if (st.justOpened) {
		sint32 id = props.GetInt32("device_id", -1);
		st.combo[0] = id + 1;
		if (st.combo[0] < 0 || st.combo[0] > 8) st.combo[0] = 0;

		const wchar_t *addr = props.GetString("host_address", L"");
		snprintf(st.addrBuf, sizeof(st.addrBuf), "%s", WToU8(addr).c_str());
		snprintf(st.portBuf, sizeof(st.portBuf), "%u", props.GetUint32("port", 6522));
		st.check[0] = props.GetBool("allow_external", false);
	}

	ImGui::Combo("Device ID", &st.combo[0], kDeviceLabels, 9);

	bool isHost = (st.combo[0] == 0);
	bool isClient = !isHost;

	ImGui::BeginDisabled(!isClient);
	ImGui::InputText("Host address", st.addrBuf, sizeof(st.addrBuf));
	ImGui::EndDisabled();

	ImGui::InputText("TCP port", st.portBuf, sizeof(st.portBuf));

	ImGui::BeginDisabled(!isHost);
	ImGui::Checkbox("Allow external connections", &st.check[0]);
	ImGui::EndDisabled();

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetInt32("device_id", st.combo[0] - 1);
		if (st.addrBuf[0])
			props.SetString("host_address", U8ToW(st.addrBuf).c_str());
		unsigned port = 0;
		sscanf(st.portBuf, "%u", &port);
		if (port >= 1 && port <= 65535)
			props.SetUint32("port", port);
		if (st.check[0]) props.SetBool("allow_external", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Parallel File Writer — output path + text mode
// =========================================================================

static bool RenderParFileWriterConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened) {
		const wchar_t *path = props.GetString("path", L"");
		snprintf(st.pathBuf, sizeof(st.pathBuf), "%s", WToU8(path).c_str());
		st.check[0] = props.GetBool("text_mode", false);
	}

	ImGui::InputText("Output file path", st.pathBuf, sizeof(st.pathBuf));
	ImGui::Checkbox("Text mode", &st.check[0]);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		if (st.pathBuf[0])
			props.SetString("path", U8ToW(st.pathBuf).c_str());
		if (st.check[0]) props.SetBool("text_mode", true);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Karin Maxi Drive — hardware version + drives + switches
// =========================================================================

static bool RenderKarinMaxiDriveConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	static const char *kHWLabels[] = { "Original", "PBI selection fix" };
	static const char *kDriveTypeLabels[] = {
		"None", "5.25\" Drive (40 tracks)", "5.25\" Drive (80 tracks)", "3.5\" Drive"
	};
	static const char *kSW1Labels[] = { "D1: and D2:", "D2: and D3:" };
	static const char *kSW2Labels[] = { "Automatic", "Manual (SW3-SW6)" };
	static const char *kStepLabels[] = { "3ms", "6ms" };

	if (st.justOpened) {
		st.combo[0] = (int)props.GetUint32("hwversion", 0);
		if (st.combo[0] > 1) st.combo[0] = 0;
		st.combo[1] = (int)props.GetUint32("drivetype1", 1);
		if (st.combo[1] > 3) st.combo[1] = 1;
		st.combo[2] = (int)props.GetUint32("drivetype2", 0);
		if (st.combo[2] > 3) st.combo[2] = 0;
		st.combo[3] = (int)props.GetUint32("sw1", 0);
		st.combo[4] = (int)props.GetUint32("sw2", 0);
		st.combo[5] = (int)props.GetUint32("sw3", 0);
		st.combo[6] = (int)props.GetUint32("sw4", 0);
	}

	ImGui::Combo("Hardware version", &st.combo[0], kHWLabels, 2);

	ImGui::SeparatorText("Drives");
	ImGui::Combo("Drive 1", &st.combo[1], kDriveTypeLabels, 4);
	ImGui::Combo("Drive 2", &st.combo[2], kDriveTypeLabels, 4);

	ImGui::SeparatorText("Switches");
	ImGui::Combo("SW1 Drive IDs", &st.combo[3], kSW1Labels, 2);
	ImGui::Combo("SW2 Config", &st.combo[4], kSW2Labels, 2);
	ImGui::Combo("SW3 Drive 2 step", &st.combo[5], kStepLabels, 2);
	ImGui::Combo("SW4 Drive 1 step", &st.combo[6], kStepLabels, 2);

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		props.SetUint32("hwversion", (uint32)st.combo[0]);
		props.SetUint32("drivetype1", (uint32)st.combo[1]);
		props.SetUint32("drivetype2", (uint32)st.combo[2]);
		props.SetUint32("sw1", (uint32)st.combo[3]);
		props.SetUint32("sw2", (uint32)st.combo[4]);
		props.SetUint32("sw3", (uint32)st.combo[5]);
		props.SetUint32("sw4", (uint32)st.combo[6]);
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_devCfg.Reset();

	return false;
}

// =========================================================================
// Generic Property Editor — fallback for unknown devices
// Uses EnumProperties() to discover and edit all properties
// =========================================================================

// Generic property editor state — separate from ATDeviceConfigState to
// hold dynamically-sized per-property string buffers
struct GenericPropEntry {
	VDStringA name;
	ATPropertyType type;
	bool boolVal;
	sint32 i32Val;
	uint32 u32Val;
	float fVal;
	double dVal;
	char strBuf[512]; // per-entry string buffer (avoids shared-static-buffer bug)
};
static vdvector<GenericPropEntry> g_genericEntries;
static bool g_genericNeedsInit = true;

static void CleanupGenericEntries() {
	g_genericEntries.clear();
	g_genericNeedsInit = true;
}

static bool RenderGenericConfig(ATPropertySet& props, ATDeviceConfigState& st) {
	if (st.justOpened || g_genericNeedsInit) {
		g_genericEntries.clear();
		props.EnumProperties([](const char *name, const ATPropertyValue& val) {
			GenericPropEntry e;
			e.name = name;
			e.type = val.mType;
			e.boolVal = false;
			e.i32Val = 0;
			e.u32Val = 0;
			e.fVal = 0;
			e.dVal = 0;
			memset(e.strBuf, 0, sizeof(e.strBuf));
			switch (val.mType) {
				case kATPropertyType_Bool: e.boolVal = val.mValBool; break;
				case kATPropertyType_Int32: e.i32Val = val.mValI32; break;
				case kATPropertyType_Uint32: e.u32Val = val.mValU32; break;
				case kATPropertyType_Float: e.fVal = val.mValF; break;
				case kATPropertyType_Double: e.dVal = val.mValD; break;
				case kATPropertyType_String16:
					if (val.mValStr16) {
						VDStringA u8 = VDTextWToU8(VDStringW(val.mValStr16));
						strncpy(e.strBuf, u8.c_str(), sizeof(e.strBuf) - 1);
					}
					break;
				default: break;
			}
			g_genericEntries.push_back(std::move(e));
		});
		g_genericNeedsInit = false;
	}

	if (g_genericEntries.empty()) {
		ImGui::TextDisabled("This device has no configurable properties.");
	} else {
		for (int i = 0; i < (int)g_genericEntries.size(); ++i) {
			auto &e = g_genericEntries[i];
			ImGui::PushID(i);
			switch (e.type) {
				case kATPropertyType_Bool:
					ImGui::Checkbox(e.name.c_str(), &e.boolVal);
					break;
				case kATPropertyType_Int32:
					ImGui::InputInt(e.name.c_str(), &e.i32Val);
					break;
				case kATPropertyType_Uint32: {
					int v = (int)e.u32Val;
					if (ImGui::InputInt(e.name.c_str(), &v))
						e.u32Val = (uint32)v;
					break;
				}
				case kATPropertyType_Float:
					ImGui::InputFloat(e.name.c_str(), &e.fVal);
					break;
				case kATPropertyType_Double: {
					float f = (float)e.dVal;
					if (ImGui::InputFloat(e.name.c_str(), &f))
						e.dVal = (double)f;
					break;
				}
				case kATPropertyType_String16:
					ImGui::InputText(e.name.c_str(), e.strBuf, sizeof(e.strBuf));
					break;
				default:
					ImGui::TextDisabled("%s: (unknown type)", e.name.c_str());
					break;
			}
			ImGui::PopID();
		}
	}

	ImGui::Separator();
	if (ImGui::Button("OK", ImVec2(120, 0))) {
		props.Clear();
		for (auto &e : g_genericEntries) {
			switch (e.type) {
				case kATPropertyType_Bool:
					props.SetBool(e.name.c_str(), e.boolVal);
					break;
				case kATPropertyType_Int32:
					props.SetInt32(e.name.c_str(), e.i32Val);
					break;
				case kATPropertyType_Uint32:
					props.SetUint32(e.name.c_str(), e.u32Val);
					break;
				case kATPropertyType_Float:
					props.SetFloat(e.name.c_str(), e.fVal);
					break;
				case kATPropertyType_Double:
					props.SetDouble(e.name.c_str(), e.dVal);
					break;
				case kATPropertyType_String16:
					if (e.strBuf[0])
						props.SetString(e.name.c_str(), U8ToW(e.strBuf).c_str());
					break;
				default:
					break;
			}
		}
		CleanupGenericEntries();
		return true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0))) {
		CleanupGenericEntries();
		g_devCfg.Reset();
	}

	return false;
}
