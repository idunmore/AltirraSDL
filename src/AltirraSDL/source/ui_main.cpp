//	AltirraSDL - Dear ImGui UI layer
//	Implements menu bar, status overlay, and render orchestration.
//	Dialog windows are in separate files (ui_system.cpp, ui_disk.cpp, etc.)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include <at/atcore/device.h>
#include <at/atio/image.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cartridgetypes.h>

#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <at/atio/cassetteimage.h>

#include <at/atcore/serializable.h>

#include "ui_main.h"
#include "display_sdl3_impl.h"
#include "simulator.h"
#include "mediamanager.h"
#include "gtia.h"
#include "cartridge.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include "constants.h"
#include "debugger.h"
#include <algorithm>
#include "audiowriter.h"
#include "videowriter.h"
#include "sapwriter.h"
#include "simeventmanager.h"
#include <at/ataudio/pokey.h>
#include "uiaccessors.h"
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include <vd2/system/strutil.h>
#include "inputmanager.h"
#include "inputmap.h"
#include <at/ataudio/audiooutput.h>
#include <at/atcore/audiomixer.h>
#include <vd2/system/math.h>
#include "settings.h"
#include "options.h"
#include "sapconverter.h"
#include "firmwaremanager.h"
#include "oshelper.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// =========================================================================
// Deferred action queue — thread-safe handoff from file dialog callbacks
//
// SDL3 file dialog callbacks may run on a background thread (platform-
// dependent).  All simulator mutations must happen on the main thread.
// Callbacks push VDStringW paths here; the main loop drains the queue
// each frame via ATUIPollDeferredActions().
// =========================================================================

#include <mutex>
#include <vector>

// ATDeferredActionType enum is now in ui_main.h

struct ATDeferredAction {
	ATDeferredActionType type;
	VDStringW path;
	VDStringW path2;   // second path for two-file operations (SAP->EXE, tape analysis)
	int mInt = 0;
};

static std::mutex g_deferredMutex;
static std::vector<ATDeferredAction> g_deferredActions;

// Tools result popup state
static bool g_showToolsResult = false;
static VDStringA g_toolsResultMessage;

// Export ROM Set confirmation state
static bool g_showExportROMOverwrite = false;
static VDStringW g_exportROMPath;

// Forward declarations for tools
static void ATUIDoExportROMSet(const VDStringW &targetDir);

void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra) {
	ATDeferredAction action;
	action.type = type;
	action.path = VDTextU8ToW(utf8path, -1);
	action.mInt = extra;

	std::lock_guard<std::mutex> lock(g_deferredMutex);
	g_deferredActions.push_back(std::move(action));
}

static void ATUIPushDeferred2(ATDeferredActionType type, const char *utf8path1, const char *utf8path2) {
	ATDeferredAction action;
	action.type = type;
	action.path = VDTextU8ToW(utf8path1, -1);
	action.path2 = VDTextU8ToW(utf8path2, -1);

	std::lock_guard<std::mutex> lock(g_deferredMutex);
	g_deferredActions.push_back(std::move(action));
}

// Export ROM Set implementation — writes internal ROMs to a user-selected folder.
static void ATUIDoExportROMSet(const VDStringW &targetDir) {
	static const struct {
		ATFirmwareId mId;
		const wchar_t *mpFilename;
	} kOutputs[] = {
		{ kATFirmwareId_Basic_ATBasic, L"atbasic.rom" },
		{ kATFirmwareId_Kernel_LLE,    L"altirraos-800.rom" },
		{ kATFirmwareId_Kernel_LLEXL,  L"altirraos-xl.rom" },
		{ kATFirmwareId_Kernel_816,    L"altirraos-816.rom" },
		{ kATFirmwareId_5200_LLE,      L"altirraos-5200.rom" },
	};

	vdfastvector<uint8> buf;

	try {
		for (auto &&out : kOutputs) {
			ATLoadInternalFirmware(out.mId, nullptr, 0, 0, nullptr, nullptr, &buf);

			VDFile f(VDMakePath(targetDir, VDStringSpanW(out.mpFilename)).c_str(),
				nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
			f.write(buf.data(), (long)buf.size());
		}

		g_toolsResultMessage = "ROM set successfully exported.";
		g_showToolsResult = true;
	} catch (const MyError &e) {
		g_toolsResultMessage = VDStringA("Export failed: ") + e.c_str();
		g_showToolsResult = true;
	}
}

// Called from main loop each frame — processes deferred file dialog results.
void ATUIPollDeferredActions();

// g_cartMapperPending and cartridge mapper dialog are in ui_cartmapper.cpp
static bool g_copyFrameRequested = false;

// =========================================================================
// Compatibility Warning — SDL3 replacement for Windows IDD_COMPATIBILITY
// =========================================================================

#include "compatengine.h"
#include "compatdb.h"
#include "uicompat.h"

// Pending compat check flag — set after boot, consumed by render loop
static bool g_compatCheckPending = false;

// Compat warning dialog state
static struct {
	const ATCompatDBTitle *pTitle = nullptr;
	vdfastvector<ATCompatKnownTag> tags;
	bool ignoreThistitle = false;
	bool ignoreAll = false;
} g_compatWarningState;

// SDL3 implementation of ATUICompatGetKnownTagDisplayName
// (Windows version is in uicompat.cpp which is excluded from SDL3 build)
const wchar_t *ATUICompatGetKnownTagDisplayName(ATCompatKnownTag knownTag) {
	static constexpr const wchar_t *kKnownTagNames[] = {
		L"Requires BASIC",
		L"Requires Atari BASIC revision A",
		L"Requires Atari BASIC revision B",
		L"Requires Atari BASIC revision C",
		L"Requires BASIC disabled",
		L"Requires OS-A",
		L"Requires OS-B",
		L"Requires XL/XE OS",
		L"Requires accurate disk timing",
		L"Requires no additional CIO devices",
		L"Requires no expanded memory",
		L"Requires CTIA",
		L"Incompatible with Ultimate1MB",
		L"Requires 6502 undocumented opcodes",
		L"Incompatible with 65C816 24-bit addressing",
		L"Requires writable disk",
		L"Incompatible with floating data bus",
		L"Cart: Use 5200 8K mapper",
		L"Cart: Use 5200 one-chip 16K mapper",
		L"Cart: Use 5200 two-chip 16K mapper",
		L"Cart: Use 5200 32K mapper",
		L"Requires 60Hz (NTSC ANTIC)",
		L"Requires 50Hz (PAL ANTIC)",
	};

	const size_t index = (size_t)knownTag - 1;
	if (index < sizeof(kKnownTagNames) / sizeof(kKnownTagNames[0]))
		return kKnownTagNames[index];

	return L"<Unknown tag>";
}

// SDL3 linker symbol for ATUIShowDialogCompatWarning.
// In the SDL3 build, compat checking is done directly in ATUIPollDeferredActions
// rather than through this function, but we provide it as a symbol since
// uicompat.h declares it and compatengine.cpp may reference it.
ATUICompatAction ATUIShowDialogCompatWarning(VDGUIHandle, const ATCompatDBTitle *title,
	const ATCompatKnownTag *tags, size_t numTags)
{
	g_compatWarningState.pTitle = title;
	g_compatWarningState.tags.assign(tags, tags + numTags);
	g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(title);
	g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
	g_compatCheckPending = true;
	return kATUICompatAction_Ignore;
}

void ATUICheckCompatibility(ATSimulator &, ATUIState &state) {
	// Compat check already happened in ATUIPollDeferredActions.
	// This just picks up the pending flag and shows the dialog.
	state.showCompatWarning = true;
}

void ATUIRenderCompatWarning(ATSimulator &sim, ATUIState &state) {
	if (!state.showCompatWarning)
		return;

	// Helper to apply mute settings from checkboxes
	auto applyMuteSettings = [&]() {
		auto &s = g_compatWarningState;
		if (s.ignoreAll)
			ATCompatSetAllMuted(true);
		else if (s.ignoreThistitle)
			ATCompatSetTitleMuted(s.pTitle, true);
	};

	ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool wasOpen = state.showCompatWarning;
	if (!ImGui::Begin("Compatibility Warning", &state.showCompatWarning,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		// Window collapsed — if user closed via X, resume emulation
		if (wasOpen && !state.showCompatWarning) {
			applyMuteSettings();
			sim.Resume();
		}
		ImGui::End();
		return;
	}

	// Detect X-button or ESC close (showCompatWarning toggled to false by ImGui)
	if (!state.showCompatWarning || ATUICheckEscClose()) {
		state.showCompatWarning = false;
		applyMuteSettings();
		sim.Resume();
		ImGui::End();
		return;
	}

	auto &s = g_compatWarningState;

	// Title text — mName is a UTF-8 byte string from the compat DB
	if (s.pTitle) {
		ImGui::TextWrapped("The title \"%s\" being booted has compatibility issues "
			"with current settings:", s.pTitle->mName.c_str());
	}

	ImGui::Spacing();

	// List of issues
	for (size_t i = 0; i < s.tags.size(); ++i) {
		VDStringA tagName = VDTextWToU8(VDStringW(ATUICompatGetKnownTagDisplayName(s.tags[i])));
		ImGui::BulletText("%s", tagName.c_str());
	}

	ImGui::Spacing();
	ImGui::TextWrapped("Do you want to automatically adjust emulation settings "
		"for better compatibility?");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Action buttons — emulation is paused while this dialog is open
	if (ImGui::Button("Auto-adjust settings and reboot", ImVec2(-1, 0))) {
		applyMuteSettings();
		ATCompatAdjust(nullptr, s.tags.data(), s.tags.size());
		sim.ColdReset();
		sim.Resume();
		state.showCompatWarning = false;
	}

	if (ImGui::Button("Pause emulation to adjust manually", ImVec2(-1, 0))) {
		applyMuteSettings();
		// Leave paused — user wants to manually adjust settings
		state.showCompatWarning = false;
	}

	if (ImGui::Button("Boot anyway", ImVec2(-1, 0))) {
		applyMuteSettings();
		sim.Resume();
		state.showCompatWarning = false;
	}

	ImGui::Spacing();

	// Mute options
	ImGui::Checkbox("Turn off compatibility checks for this title", &s.ignoreThistitle);
	ImGui::Checkbox("Turn off all compatibility warnings", &s.ignoreAll);

	ImGui::End();
}

// =========================================================================
// Save Frame state — file path set by dialog callback, consumed by render
// =========================================================================
static std::mutex g_saveFrameMutex;
static VDStringA g_saveFramePath;

// =========================================================================
// MRU (Most Recently Used) list — same registry format as Windows
// =========================================================================

void ATAddMRU(const wchar_t *path) {
	VDRegistryAppKey key("MRU List", true);

	VDStringW order;
	key.getString("Order", order);

	// Check if already in list — if so, promote to front
	VDStringW existing;
	for (size_t i = 0; i < order.size(); ++i) {
		char kn[2] = { (char)order[i], 0 };
		key.getString(kn, existing);
		if (existing.comparei(path) == 0) {
			// Promote: move to front
			wchar_t c = order[i];
			order.erase(i, 1);
			order.insert(order.begin(), c);
			key.setString("Order", order.c_str());
			return;
		}
	}

	// Not found — add new entry (recycle oldest if at 10)
	int slot = 0;
	if (order.size() >= 10) {
		wchar_t c = order.back();
		if (c >= L'A' && c < L'A' + 10)
			slot = c - L'A';
		order.resize(9);
	} else {
		slot = (int)order.size();
	}

	order.insert(order.begin(), L'A' + slot);
	char kn[2] = { (char)('A' + slot), 0 };
	key.setString(kn, path);
	key.setString("Order", order.c_str());
}

static VDStringW ATGetMRU(uint32 index) {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	VDStringW s;
	if (index < order.size()) {
		char kn[2] = { (char)order[index], 0 };
		key.getString(kn, s);
	}
	return s;
}

static uint32 ATGetMRUCount() {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	return (uint32)order.size();
}

static void ATClearMRU() {
	VDRegistryAppKey key("MRU List", true);
	key.removeValue("Order");
}

// =========================================================================
// File dialog callbacks (SDL3 async)
// =========================================================================

static void BootImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_BootImage, filelist[0]);
}

static void OpenImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_OpenImage, filelist[0]);
}

static void CartridgeAttachCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_AttachCartridge, filelist[0]);
}

// Per-drive attach callback — drive index in userdata
static void AttachDiskCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;
	ATUIPushDeferred(kATDeferred_AttachDisk, filelist[0], driveIdx);
}

static void CassetteSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveCassette, filelist[0]);
}

static void SaveFrameCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_saveFrameMutex);
	g_saveFramePath = filelist[0];
}

// =========================================================================
// State save/load
// =========================================================================

static vdrefptr<IATSerializable> g_pQuickSaveState;

static void SaveStateCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveState, filelist[0]);
}

static void LoadStateCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_LoadState, filelist[0]);
}

void ATUIQuickSaveState() {
	try {
		vdrefptr<IATSerializable> info;
		g_sim.CreateSnapshot(~g_pQuickSaveState, ~info);
		fprintf(stderr, "[AltirraSDL] Quick save state created\n");
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Quick save failed\n");
	}
}

void ATUIQuickLoadState() {
	if (!g_pQuickSaveState)
		return;
	try {
		g_sim.ApplySnapshot(*g_pQuickSaveState, nullptr);
		g_sim.Resume();
		fprintf(stderr, "[AltirraSDL] Quick load state applied\n");
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Quick load failed\n");
	}
}

// =========================================================================
// Audio recording state
// =========================================================================

static ATAudioWriter *g_pAudioWriter = nullptr;
static IATSAPWriter *g_pSAPWriter = nullptr;
static IATVideoWriter *g_pVideoWriter = nullptr;

// Video recording settings dialog state
static bool g_showVideoRecordingDialog = false;
static ATVideoEncoding g_videoRecEncoding = kATVideoEncoding_ZMBV;
static ATVideoRecordingFrameRate g_videoRecFrameRate = kATVideoRecordingFrameRate_Normal;
static ATVideoRecordingResamplingMode g_videoRecResamplingMode = ATVideoRecordingResamplingMode::Nearest;
static ATVideoRecordingAspectRatioMode g_videoRecAspectRatioMode = ATVideoRecordingAspectRatioMode::IntegerOnly;
static ATVideoRecordingScalingMode g_videoRecScalingMode = ATVideoRecordingScalingMode::None;
static bool g_videoRecHalfRate = false;
static bool g_videoRecEncodeAll = false;

static bool ATUIIsRecording() {
	return g_pAudioWriter || g_pSAPWriter || g_pVideoWriter;
}

static void ATUIStartAudioRecording(const wchar_t *path, bool raw) {
	if (ATUIIsRecording()) return;

	bool stereo = g_sim.IsDualPokeysEnabled();
	bool pal = (g_sim.GetVideoStandard() == kATVideoStandard_PAL);

	try {
		g_pAudioWriter = new ATAudioWriter(path, raw, stereo, pal, nullptr);
		g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		fprintf(stderr, "[AltirraSDL] Audio recording started (%s, %s)\n",
			raw ? "raw" : "WAV", stereo ? "stereo" : "mono");
	} catch (...) {
		delete g_pAudioWriter;
		g_pAudioWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start audio recording\n");
	}
}

static void ATUIStartSAPRecording(const wchar_t *path) {
	if (ATUIIsRecording()) return;

	bool pal = (g_sim.GetVideoStandard() == kATVideoStandard_PAL);

	try {
		g_pSAPWriter = ATCreateSAPWriter();
		g_pSAPWriter->Init(g_sim.GetEventManager(), &g_sim.GetPokey(), nullptr, path, pal);
		fprintf(stderr, "[AltirraSDL] SAP recording started\n");
	} catch (...) {
		delete g_pSAPWriter;
		g_pSAPWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start SAP recording\n");
	}
}

static void ATUIStartVideoRecording(const wchar_t *path, ATVideoEncoding encoding) {
	if (ATUIIsRecording()) return;

	try {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();

		ATCreateVideoWriter(&g_pVideoWriter);

		int w;
		int h;
		bool rgb32;
		gtia.GetRawFrameFormat(w, h, rgb32);

		uint32 palette[256];
		if (!rgb32)
			gtia.GetPalette(palette);

		const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;
		VDFraction frameRate = hz50 ? VDFraction(1773447, 114*312) : VDFraction(3579545, 2*114*262);
		double samplingRate = hz50 ? 1773447.0 / 28.0 : 3579545.0 / 56.0;

		switch(g_videoRecFrameRate) {
			case kATVideoRecordingFrameRate_NTSCRatio:
				if (hz50) {
					samplingRate = samplingRate * (50000.0 / 1001.0) / frameRate.asDouble();
					frameRate = VDFraction(50000, 1001);
				} else {
					samplingRate = samplingRate * (60000.0 / 1001.0) / frameRate.asDouble();
					frameRate = VDFraction(60000, 1001);
				}
				break;

			case kATVideoRecordingFrameRate_Integral:
				if (hz50) {
					samplingRate = samplingRate * 50.0 / frameRate.asDouble();
					frameRate = VDFraction(50, 1);
				} else {
					samplingRate = samplingRate * 60.0 / frameRate.asDouble();
					frameRate = VDFraction(60, 1);
				}
				break;

			default:
				break;
		}

		double par = 1.0;
		if (g_videoRecAspectRatioMode != ATVideoRecordingAspectRatioMode::None) {
			if (g_videoRecAspectRatioMode == ATVideoRecordingAspectRatioMode::FullCorrection) {
				par = gtia.GetPixelAspectRatio();
			} else {
				int px = 2, py = 2;
				gtia.GetPixelAspectMultiple(px, py);
				par = (double)py / (double)px;
			}
		}

		g_pVideoWriter->Init(path,
			encoding,
			0,	// videoBitRate (not used for AVI encodings)
			0,	// audioBitRate (not used for AVI encodings)
			w, h,
			frameRate,
			par,
			g_videoRecResamplingMode,
			g_videoRecScalingMode,
			rgb32 ? NULL : palette,
			samplingRate,
			g_sim.IsDualPokeysEnabled(),
			hz50 ? 1773447.0f : 1789772.5f,
			g_videoRecHalfRate,
			g_videoRecEncodeAll,
			nullptr);

		g_sim.GetAudioOutput()->SetAudioTap(g_pVideoWriter->AsAudioTap());
		gtia.AddVideoTap(g_pVideoWriter->AsVideoTap());

		fprintf(stderr, "[AltirraSDL] Video recording started\n");
	} catch (const MyError& e) {
		if (g_pVideoWriter) {
			ATGTIAEmulator& gtia2 = g_sim.GetGTIA();
			gtia2.RemoveVideoTap(g_pVideoWriter->AsVideoTap());
			g_sim.GetAudioOutput()->SetAudioTap(nullptr);
			try { g_pVideoWriter->Shutdown(); } catch (...) {}
			delete g_pVideoWriter;
			g_pVideoWriter = nullptr;
		}
		fprintf(stderr, "[AltirraSDL] Failed to start video recording: %s\n", e.c_str());
	} catch (...) {
		if (g_pVideoWriter) {
			ATGTIAEmulator& gtia2 = g_sim.GetGTIA();
			gtia2.RemoveVideoTap(g_pVideoWriter->AsVideoTap());
			g_sim.GetAudioOutput()->SetAudioTap(nullptr);
			try { g_pVideoWriter->Shutdown(); } catch (...) {}
			delete g_pVideoWriter;
			g_pVideoWriter = nullptr;
		}
		fprintf(stderr, "[AltirraSDL] Failed to start video recording\n");
	}
}

static void ATUIStopRecording() {
	bool wasRecording = ATUIIsRecording();

	if (g_pVideoWriter) {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();
		gtia.RemoveVideoTap(g_pVideoWriter->AsVideoTap());
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);

		try {
			g_pVideoWriter->Shutdown();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing video recording\n");
		}
		delete g_pVideoWriter;
		g_pVideoWriter = nullptr;
	}

	if (g_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		try {
			g_pAudioWriter->Finalize();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing audio recording\n");
		}
		delete g_pAudioWriter;
		g_pAudioWriter = nullptr;
	}

	if (g_pSAPWriter) {
		try {
			g_pSAPWriter->Shutdown();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing SAP recording\n");
		}
		delete g_pSAPWriter;
		g_pSAPWriter = nullptr;
	}

	if (wasRecording)
		fprintf(stderr, "[AltirraSDL] Recording stopped\n");
}

// =========================================================================
// Deferred action execution (main thread only)
// =========================================================================

void ATUIPollDeferredActions() {
	std::vector<ATDeferredAction> actions;
	{
		std::lock_guard<std::mutex> lock(g_deferredMutex);
		actions.swap(g_deferredActions);
	}

	for (auto& a : actions) {
		try {
			switch (a.type) {
			case kATDeferred_BootImage:
			case kATDeferred_OpenImage: {
				// Matches Windows DoLoadStream retry loop (main.cpp:1186-1369):
				// 1. Unload storage before boot (per user-configured mask)
				// 2. Load with retry loop for hardware mode switching, BASIC conflicts
				// 3. Cold reset (boot only)
				// 4. Check compatibility
				// 5. Resume

				extern ATOptions g_ATOptions;
				if (a.type == kATDeferred_BootImage)
					g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());

				vdfastvector<uint8> captureBuffer;
				ATCartLoadContext cartCtx {};
				cartCtx.mbReturnOnUnknownMapper = true;
				cartCtx.mpCaptureBuffer = &captureBuffer;

				if (a.mInt > 0) {
					// Re-load with user-selected mapper (from mapper dialog)
					cartCtx.mbReturnOnUnknownMapper = false;
					cartCtx.mCartMapper = a.mInt;
					cartCtx.mpCaptureBuffer = nullptr;
				}

				ATStateLoadContext stateCtx {};
				ATImageLoadContext ctx {};
				ctx.mpCartLoadContext = &cartCtx;
				ctx.mpStateLoadContext = &stateCtx;

				// Build full ATMediaLoadContext with stop flags (matches Windows)
				ATMediaLoadContext mctx;
				mctx.mOriginalPath = a.path;
				mctx.mImageName = a.path;
				mctx.mpStream = nullptr;
				mctx.mWriteMode = g_ATOptions.mDefaultWriteMode;
				mctx.mbStopOnModeIncompatibility = true;
				mctx.mbStopAfterImageLoaded = true;
				mctx.mbStopOnMemoryConflictBasic = true;
				mctx.mbStopOnIncompatibleDiskFormat = true;
				mctx.mpImageLoadContext = &ctx;

				bool loadSuccess = false;
				bool suppressColdReset = false;

				// Retry loop matching Windows DoLoadStream (up to 10 retries)
				int safetyCounter = 10;
				for (;;) {
					if (g_sim.Load(mctx)) {
						loadSuccess = true;
						break;
					}

					if (!--safetyCounter)
						break;

					if (mctx.mbStopAfterImageLoaded)
						mctx.mbStopAfterImageLoaded = false;

					if (mctx.mbMode5200Required) {
						// Auto-switch to 5200 mode (matches Windows ATUISwitchHardwareMode5200)
						if (g_sim.GetHardwareMode() == kATHardwareMode_5200)
							break; // already in 5200
						if (!ATUISwitchHardwareMode(nullptr, kATHardwareMode_5200, true))
							break;
						continue;
					} else if (mctx.mbModeComputerRequired) {
						// Auto-switch to computer mode (matches Windows ATUISwitchHardwareModeComputer)
						if (g_sim.GetHardwareMode() != kATHardwareMode_5200)
							break; // already in computer mode
						if (!ATUISwitchHardwareMode(nullptr, kATHardwareMode_800XL, true))
							break;
						continue;
					} else if (mctx.mbMemoryConflictBasic) {
						// Auto-disable BASIC on memory conflict (SDL3 can't show
						// modal dialog, so auto-disable matching common user choice)
						mctx.mbStopOnMemoryConflictBasic = false;
						g_sim.SetBASICEnabled(false);
						continue;
					} else if (mctx.mbIncompatibleDiskFormat) {
						// Allow incompatible disk format (SDL3 auto-accepts)
						mctx.mbIncompatibleDiskFormat = false;
						mctx.mbStopOnIncompatibleDiskFormat = false;
						continue;
					}

					// Unknown cart mapper — show selection dialog
					if (ctx.mLoadType == kATImageType_Cartridge) {
						ATUIOpenCartridgeMapperDialog(a.type, a.path, 0,
							(a.type == kATDeferred_BootImage),
							captureBuffer, cartCtx.mCartSize);
						break;  // mapper dialog will re-push deferred action
					}

					break;  // unknown failure
				}

				if (loadSuccess) {
					ATAddMRU(a.path.c_str());

					// Save state loads suppress cold reset (matches Windows)
					if (ctx.mLoadType == kATImageType_SaveState || ctx.mLoadType == kATImageType_SaveState2)
						suppressColdReset = true;

					if (a.type == kATDeferred_BootImage && !suppressColdReset)
						g_sim.ColdReset();

					// Check compatibility before resuming (matches Windows
					// modal dialog behavior — emulation stays paused while
					// the user decides what to do).
					bool compatIssue = false;
					try {
						vdfastvector<ATCompatKnownTag> compatTags;
						auto *compatTitle = ATCompatCheck(compatTags);
						if (compatTitle) {
							g_compatWarningState.pTitle = compatTitle;
							g_compatWarningState.tags = std::move(compatTags);
							g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(compatTitle);
							g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
							g_compatCheckPending = true;
							compatIssue = true;
						}
					} catch (...) {
						// Compat check failure should not block boot
					}
					// Only resume for Boot Image (matches Windows — Open Image
					// does not resume; user may be paused to inspect media)
					if (!compatIssue && a.type == kATDeferred_BootImage)
						g_sim.Resume();
				}
				break;
			}
			case kATDeferred_AttachCartridge:
			case kATDeferred_AttachSecondaryCartridge: {
				int slot = (a.type == kATDeferred_AttachSecondaryCartridge) ? 1 : 0;
				vdfastvector<uint8> captureBuffer;
				ATCartLoadContext ctx {};
				ctx.mbReturnOnUnknownMapper = true;
				ctx.mpCaptureBuffer = &captureBuffer;

				if (a.mInt > 0) {
					// Re-load with user-selected mapper (from mapper dialog)
					ctx.mbReturnOnUnknownMapper = false;
					ctx.mCartMapper = a.mInt;
					ctx.mpCaptureBuffer = nullptr;
				}

				if (g_sim.LoadCartridge(slot, a.path.c_str(), &ctx)) {
					if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
						g_sim.ColdReset();

					// Check compatibility before resuming
					bool compatIssue = false;
					try {
						vdfastvector<ATCompatKnownTag> compatTags;
						auto *compatTitle = ATCompatCheck(compatTags);
						if (compatTitle) {
							g_compatWarningState.pTitle = compatTitle;
							g_compatWarningState.tags = std::move(compatTags);
							g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(compatTitle);
							g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
							g_compatCheckPending = true;
							compatIssue = true;
						}
					} catch (...) {
					}
					if (!compatIssue)
						g_sim.Resume();
				} else if (ctx.mLoadStatus == kATCartLoadStatus_UnknownMapper) {
					// Unknown mapper — show selection dialog
					ATUIOpenCartridgeMapperDialog(a.type, a.path, slot, true,
						captureBuffer, ctx.mCartSize);
				}
				break;
			}
			case kATDeferred_AttachDisk: {
				int idx = a.mInt;
				if (idx >= 0 && idx < 15)
					g_sim.GetDiskInterface(idx).LoadDisk(a.path.c_str());
				break;
			}
			case kATDeferred_LoadState: {
				ATImageLoadContext ctx {};
				if (g_sim.Load(a.path.c_str(), kATMediaWriteMode_RO, &ctx))
					g_sim.Resume();
				break;
			}
			case kATDeferred_SaveState:
				g_sim.SaveState(a.path.c_str());
				break;
			case kATDeferred_SaveCassette: {
				IATCassetteImage *image = g_sim.GetCassette().GetImage();
				if (image) {
					VDFileStream fs(a.path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					ATSaveCassetteImageCAS(fs, image);
					g_sim.GetCassette().SetImagePersistent(a.path.c_str());
					g_sim.GetCassette().SetImageClean();
				}
				break;
			}
			case kATDeferred_ExportCassetteAudio: {
				IATCassetteImage *image = g_sim.GetCassette().GetImage();
				if (image) {
					VDFileStream f(a.path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					ATSaveCassetteImageWAV(f, image);
					g_sim.GetCassette().SetImageClean();
				}
				break;
			}
			case kATDeferred_SaveCartridge: {
				ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
				if (cart && cart->GetMode()) {
					VDStringA u8 = VDTextWToU8(a.path);
					const char *ext = strrchr(u8.c_str(), '.');
					bool includeHeader = true;
					if (ext && (strcasecmp(ext, ".bin") == 0 || strcasecmp(ext, ".rom") == 0))
						includeHeader = false;
					cart->Save(a.path.c_str(), includeHeader);
				}
				break;
			}
			case kATDeferred_SaveFirmware:
				g_sim.SaveStorage((ATStorageId)(kATStorageId_Firmware + a.mInt), a.path.c_str());
				break;
			case kATDeferred_LoadCassette:
				g_sim.GetCassette().Load(a.path.c_str());
				g_sim.GetCassette().Play();
				break;
			case kATDeferred_StartRecordRaw:
				ATUIStartAudioRecording(a.path.c_str(), true);
				break;
			case kATDeferred_StartRecordWAV:
				ATUIStartAudioRecording(a.path.c_str(), false);
				break;
			case kATDeferred_StartRecordSAP:
				ATUIStartSAPRecording(a.path.c_str());
				break;
			case kATDeferred_StartRecordVideo:
				ATUIStartVideoRecording(a.path.c_str(), (ATVideoEncoding)a.mInt);
				break;
			case kATDeferred_SetCompatDBPath: {
				extern ATOptions g_ATOptions;
				ATOptions prev(g_ATOptions);
				g_ATOptions.mCompatExternalDBPath = a.path;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
				}
				break;
			}
			case kATDeferred_ConvertSAPToEXE:
				ATConvertSAPToPlayer(a.path2.c_str(), a.path.c_str());
				g_toolsResultMessage = "SAP file successfully converted to executable.";
				g_showToolsResult = true;
				break;
			case kATDeferred_ExportROMSet: {
				// Check if any target files exist — show overwrite confirm if so
				static const wchar_t *kROMNames[] = {
					L"atbasic.rom", L"altirraos-800.rom", L"altirraos-xl.rom",
					L"altirraos-816.rom", L"altirraos-5200.rom",
				};
				bool needConfirm = false;
				for (auto *name : kROMNames) {
					if (VDDoesPathExist(VDMakePath(a.path, VDStringSpanW(name)).c_str())) {
						needConfirm = true;
						break;
					}
				}
				if (needConfirm) {
					g_exportROMPath = a.path;
					g_showExportROMOverwrite = true;
				} else {
					ATUIDoExportROMSet(a.path);
				}
				break;
			}
			case kATDeferred_AnalyzeTapeDecode: {
				if (VDFileIsPathEqual(a.path.c_str(), a.path2.c_str()))
					throw MyError("The analysis file needs to be different from the source tape file.");

				VDFileStream f2(a.path2.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kSequential | nsVDFile::kCreateAlways);
				ATCassetteLoadContext ctx;
				g_sim.GetCassette().GetLoadOptions(ctx);
				(void)ATLoadCassetteImage(a.path.c_str(), &f2, ctx);
				g_toolsResultMessage = "Tape analysis complete.";
				g_showToolsResult = true;
				break;
			}
			}
		} catch (const MyError& e) {
			g_toolsResultMessage = VDStringA("Error: ") + e.c_str();
			g_showToolsResult = true;
		} catch (...) {
			VDStringA u8 = VDTextWToU8(a.path);
			fprintf(stderr, "[AltirraSDL] Deferred action %d failed for: %s\n", a.type, u8.c_str());
		}
	}
}

// =========================================================================
// Init / Shutdown
// =========================================================================

bool ATUIInit(SDL_Window *window, SDL_Renderer *renderer) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.IniFilename = "altirrasdl_imgui.ini";

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.FrameRounding = 2.0f;
	style.WindowRounding = 4.0f;
	style.GrabRounding = 2.0f;

	if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
		fprintf(stderr, "[AltirraSDL] ImGui SDL3 init failed\n");
		return false;
	}
	if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
		fprintf(stderr, "[AltirraSDL] ImGui SDLRenderer3 init failed\n");
		return false;
	}

	fprintf(stderr, "[AltirraSDL] ImGui initialized (docking enabled)\n");
	return true;
}

void ATUIShutdown() {
	ATUIStopRecording();
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

bool ATUIProcessEvent(const SDL_Event *event) {
	return ImGui_ImplSDL3_ProcessEvent(event);
}

bool ATUIWantCaptureKeyboard() { return ImGui::GetIO().WantCaptureKeyboard; }
bool ATUIWantCaptureMouse() { return ImGui::GetIO().WantCaptureMouse; }

// =========================================================================
// File dialog filters
// =========================================================================

static const SDL_DialogFileFilter kImageFilters[] = {
	{ "Atari Images", "atr;xfd;dcm;xex;obx;com;bin;rom;car;cas;wav;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kCartFilters[] = {
	{ "Cartridge Images", "car;rom;bin" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kDiskAttachFilters[] = {
	{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kCasSaveFilters[] = {
	{ "Cassette Images", "cas" },
	{ "All Files", "*" },
};

// =========================================================================
// File menu
// =========================================================================

static void RenderFileMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	if (ImGui::MenuItem("Boot Image...", "Ctrl+O"))
		SDL_ShowOpenFileDialog(BootImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);

	if (ImGui::MenuItem("Open Image..."))
		SDL_ShowOpenFileDialog(OpenImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);

	// Recently Booted (MRU list)
	uint32 mruCount = ATGetMRUCount();
	if (ImGui::BeginMenu("Recently Booted", mruCount > 0)) {
		for (uint32 i = 0; i < mruCount; ++i) {
			VDStringW wpath = ATGetMRU(i);
			if (wpath.empty()) continue;
			VDStringA u8 = VDTextWToU8(wpath);
			const char *base = strrchr(u8.c_str(), '/');
			if (!base) base = strrchr(u8.c_str(), '\\');
			const char *label = base ? base + 1 : u8.c_str();

			char menuLabel[280];
			snprintf(menuLabel, sizeof(menuLabel), "%d. %s", i + 1, label);

			if (ImGui::MenuItem(menuLabel)) {
				ATImageLoadContext ctx {};
				if (g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx)) {
					ATAddMRU(wpath.c_str());
					g_sim.ColdReset();
					g_sim.Resume();
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", u8.c_str());
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Clear List"))
			ATClearMRU();

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Disk Drives..."))
		state.showDiskManager = true;

	// Attach Disk submenu (matches Windows: Rotate + D1-D8)
	if (ImGui::BeginMenu("Attach Disk")) {
		if (ImGui::MenuItem("Rotate Down"))
			sim.RotateDrives(8, 1);
		if (ImGui::MenuItem("Rotate Up"))
			sim.RotateDrives(8, -1);

		ImGui::Separator();

		for (int i = 0; i < 8; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "Drive %d...", i + 1);

			ATDiskInterface& di = sim.GetDiskInterface(i);
			bool loaded = di.IsDiskLoaded();

			// Show current image in menu item if loaded
			if (loaded) {
				const wchar_t *path = di.GetPath();
				VDStringA u8;
				if (path && *path) {
					u8 = VDTextWToU8(VDStringW(path));
					const char *base = strrchr(u8.c_str(), '/');
					if (base) u8 = VDStringA(base + 1);
				}
				char fullLabel[256];
				snprintf(fullLabel, sizeof(fullLabel), "Drive %d [%s]...",
					i + 1, u8.empty() ? "loaded" : u8.c_str());
				if (ImGui::MenuItem(fullLabel))
					SDL_ShowOpenFileDialog(AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, nullptr, false);
			} else {
				if (ImGui::MenuItem(label))
					SDL_ShowOpenFileDialog(AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, nullptr, false);
			}
		}
		ImGui::EndMenu();
	}

	// Detach Disk submenu (matches Windows: All + D1-D8)
	if (ImGui::BeginMenu("Detach Disk")) {
		if (ImGui::MenuItem("All")) {
			for (int i = 0; i < 15; ++i) {
				sim.GetDiskInterface(i).UnloadDisk();
				sim.GetDiskDrive(i).SetEnabled(false);
			}
		}

		ImGui::Separator();

		for (int i = 0; i < 8; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "Drive %d", i + 1);
			bool loaded = sim.GetDiskInterface(i).IsDiskLoaded();
			if (ImGui::MenuItem(label, nullptr, false, loaded)) {
				sim.GetDiskInterface(i).UnloadDisk();
				sim.GetDiskDrive(i).SetEnabled(false);
			}
		}
		ImGui::EndMenu();
	}

	// Cassette submenu (matches Windows: Tape Control, Tape Editor, New/Load/Unload/Save/Export)
	if (ImGui::BeginMenu("Cassette")) {
		ATCassetteEmulator& cas = sim.GetCassette();
		bool loaded = cas.IsLoaded();

		if (ImGui::MenuItem("Tape Control..."))
			state.showCassetteControl = true;

		ImGui::MenuItem("Tape Editor...", nullptr, false, false);  // placeholder

		ImGui::Separator();

		if (ImGui::MenuItem("New Tape"))
			cas.LoadNew();

		if (ImGui::MenuItem("Load...")) {
			static const SDL_DialogFileFilter casFilters[] = {
				{ "Cassette Images", "cas;wav" }, { "All Files", "*" },
			};
			SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_LoadCassette, fl[0]);
			}, nullptr, window, casFilters, 2, nullptr, false);
		}

		if (ImGui::MenuItem("Unload", nullptr, false, loaded))
			cas.Unload();

		if (ImGui::MenuItem("Save...", nullptr, false, loaded)) {
			SDL_ShowSaveFileDialog(CassetteSaveCallback, nullptr, window,
				kCasSaveFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Export Audio Tape...", nullptr, false, loaded)) {
			static const SDL_DialogFileFilter wavFilters[] = {
				{ "WAV Audio", "wav" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_ExportCassetteAudio, fl[0]);
			}, nullptr, window, wavFilters, 1, nullptr);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// State save/load
	{
		static const SDL_DialogFileFilter stateFilters[] = {
			{ "Save States", "atstate2;atstate" }, { "All Files", "*" },
		};

		if (ImGui::MenuItem("Load State..."))
			SDL_ShowOpenFileDialog(LoadStateCallback, nullptr, window, stateFilters, 2, nullptr, false);

		if (ImGui::MenuItem("Save State..."))
			SDL_ShowSaveFileDialog(SaveStateCallback, nullptr, window, stateFilters, 1, nullptr);

		if (ImGui::MenuItem("Quick Load State", "F7", false, g_pQuickSaveState != nullptr))
			ATUIQuickLoadState();

		if (ImGui::MenuItem("Quick Save State", "F8"))
			ATUIQuickSaveState();
	}

	ImGui::Separator();

	// Attach Special Cartridge submenu (matches Windows exactly)
	if (ImGui::BeginMenu("Attach Special Cartridge")) {
		static const struct { const char *label; int mode; } kSpecialCarts[] = {
			{ "SuperCharger3D",                                     kATCartridgeMode_SuperCharger3D },
			{ "Empty 128K (1Mbit) MaxFlash cartridge",              kATCartridgeMode_MaxFlash_128K },
			{ "Empty 128K (1Mbit) MaxFlash cartridge (MyIDE banking)", kATCartridgeMode_MaxFlash_128K_MyIDE },
			{ "Empty 1M (8Mbit) MaxFlash cartridge (older - bank 127)", kATCartridgeMode_MaxFlash_1024K },
			{ "Empty 1M (8Mbit) MaxFlash cartridge (newer - bank 0)", kATCartridgeMode_MaxFlash_1024K_Bank0 },
			{ "Empty 128K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_128K },
			{ "Empty 256K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_256K },
			{ "Empty 512K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_512K },
			{ "Empty 1024K J(Atari)Cart",                           kATCartridgeMode_JAtariCart_1024K },
			{ "Empty DCart",                                        kATCartridgeMode_DCart },
			{ "Empty SIC! 512K flash cartridge",                    kATCartridgeMode_SIC_512K },
			{ "Empty SIC! 256K flash cartridge",                    kATCartridgeMode_SIC_256K },
			{ "Empty SIC! 128K flash cartridge",                    kATCartridgeMode_SIC_128K },
			{ "Empty SIC+ flash cartridge",                         kATCartridgeMode_SICPlus },
			{ "Empty 512K MegaCart flash cartridge",                kATCartridgeMode_MegaCart_512K_3 },
			{ "Empty 4MB MegaCart flash cartridge",                 kATCartridgeMode_MegaCart_4M_3 },
			{ "Empty The!Cart 32MB flash cartridge",                kATCartridgeMode_TheCart_32M },
			{ "Empty The!Cart 64MB flash cartridge",                kATCartridgeMode_TheCart_64M },
			{ "Empty The!Cart 128MB flash cartridge",               kATCartridgeMode_TheCart_128M },
		};

		for (auto& sc : kSpecialCarts) {
			if (ImGui::MenuItem(sc.label)) {
				sim.LoadNewCartridge(sc.mode);
				if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
					sim.ColdReset();
				sim.Resume();
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("BASIC")) {
			sim.LoadCartridgeBASIC();
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				sim.ColdReset();
			sim.Resume();
		}

		ImGui::EndMenu();
	}

	// Secondary Cartridge submenu
	if (ImGui::BeginMenu("Secondary Cartridge")) {
		if (ImGui::MenuItem("Attach..."))
			SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_AttachSecondaryCartridge, fl[0]);
			}, nullptr, window, kCartFilters, 2, nullptr, false);

		if (ImGui::MenuItem("Detach", nullptr, false, sim.IsCartridgeAttached(1))) {
			sim.UnloadCartridge(1);
			if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
				sim.ColdReset();
			sim.Resume();
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Attach Cartridge..."))
		SDL_ShowOpenFileDialog(CartridgeAttachCallback, nullptr, window, kCartFilters, 2, nullptr, false);

	if (ImGui::MenuItem("Detach Cartridge", nullptr, false, sim.IsCartridgeAttached(0))) {
		if (sim.GetHardwareMode() == kATHardwareMode_5200)
			sim.LoadCartridge5200Default();
		else
			sim.UnloadCartridge(0);
		if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
			sim.ColdReset();
		sim.Resume();
	}

	// Save Firmware submenu
	if (ImGui::BeginMenu("Save Firmware")) {
		if (ImGui::MenuItem("Save Cartridge...", nullptr, false, sim.IsCartridgeAttached(0))) {
			static const SDL_DialogFileFilter cartSaveFilters[] = {
				{ "Cartridge image with header (*.car)", "car" },
				{ "Raw cartridge image (*.bin, *.rom)", "bin;rom" },
				{ "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveCartridge, fl[0]);
			}, nullptr, window, cartSaveFilters, 3, nullptr);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE / SIDE / MyIDE II Main Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)kATStorageId_Firmware))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)0, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE SDX Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 1)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)1, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save Ultimate1MB Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 2)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)2, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save Rapidus Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 3)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)3, window, fwFilters, 2, nullptr);
		}

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Exit")) {
		if (!state.showExitConfirm)
			state.showExitConfirm = true;
	}
}

// =========================================================================
// View menu
// =========================================================================

static void RenderViewMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window, SDL_Renderer *renderer) {
	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (ImGui::MenuItem("Full Screen", "Alt+Enter", isFullscreen))
		SDL_SetWindowFullscreen(window, !isFullscreen);

	ImGui::Separator();

	if (ImGui::BeginMenu("Filter Mode")) {
		ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
		if (ImGui::MenuItem("Next Mode")) {
			static const ATDisplayFilterMode kModes[] = {
				kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
				kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
				kATDisplayFilterMode_AnySuitable,
			};
			int cur = 0;
			for (int i = 0; i < 5; ++i)
				if (kModes[i] == fm) { cur = i; break; }
			ATUISetDisplayFilterMode(kModes[(cur + 1) % 5]);
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Point", nullptr, fm == kATDisplayFilterMode_Point))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Point);
		if (ImGui::MenuItem("Bilinear", nullptr, fm == kATDisplayFilterMode_Bilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
		if (ImGui::MenuItem("Sharp Bilinear", nullptr, fm == kATDisplayFilterMode_SharpBilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
		if (ImGui::MenuItem("Bicubic", nullptr, fm == kATDisplayFilterMode_Bicubic))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic);
		if (ImGui::MenuItem("Default (Any Suitable)", nullptr, fm == kATDisplayFilterMode_AnySuitable))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Filter Sharpness")) {
		int sharpness = ATUIGetViewFilterSharpness();
		if (ImGui::MenuItem("Softer", nullptr, sharpness == -2))
			ATUISetViewFilterSharpness(-2);
		if (ImGui::MenuItem("Soft", nullptr, sharpness == -1))
			ATUISetViewFilterSharpness(-1);
		if (ImGui::MenuItem("Normal", nullptr, sharpness == 0))
			ATUISetViewFilterSharpness(0);
		if (ImGui::MenuItem("Sharp", nullptr, sharpness == 1))
			ATUISetViewFilterSharpness(1);
		if (ImGui::MenuItem("Sharper", nullptr, sharpness == 2))
			ATUISetViewFilterSharpness(2);
		ImGui::EndMenu();
	}

	// Video Frame submenu (matches Windows "Video Frame" menu)
	if (ImGui::BeginMenu("Video Frame")) {
		ATDisplayStretchMode sm = ATUIGetDisplayStretchMode();
		if (ImGui::MenuItem("Fit to Window", nullptr, sm == kATDisplayStretchMode_Unconstrained))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained);
		if (ImGui::MenuItem("Preserve Aspect Ratio", nullptr, sm == kATDisplayStretchMode_PreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio);
		if (ImGui::MenuItem("Preserve Aspect Ratio (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_IntegralPreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio);
		if (ImGui::MenuItem("Square Pixels", nullptr, sm == kATDisplayStretchMode_SquarePixels))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels);
		if (ImGui::MenuItem("Square Pixels (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_Integral))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral);

		ImGui::Separator();

		ImGui::MenuItem("Pan/Zoom Tool", nullptr, false, false);  // placeholder
		if (ImGui::MenuItem("Reset Pan and Zoom")) {
			ATUISetDisplayZoom(1.0f);
			ATUISetDisplayPanOffset({0, 0});
		}
		if (ImGui::MenuItem("Reset Panning"))
			ATUISetDisplayPanOffset({0, 0});
		if (ImGui::MenuItem("Reset Zoom"))
			ATUISetDisplayZoom(1.0f);

		ImGui::EndMenu();
	}

	// Overscan Mode submenu (matches Windows exactly, with sub-submenus)
	if (ImGui::BeginMenu("Overscan Mode")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto om = gtia.GetOverscanMode();
		if (ImGui::MenuItem("OS Screen Only", nullptr, om == ATGTIAEmulator::kOverscanOSScreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
		if (ImGui::MenuItem("Normal", nullptr, om == ATGTIAEmulator::kOverscanNormal))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
		if (ImGui::MenuItem("Widescreen", nullptr, om == ATGTIAEmulator::kOverscanWidescreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanWidescreen);
		if (ImGui::MenuItem("Extended", nullptr, om == ATGTIAEmulator::kOverscanExtended))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
		if (ImGui::MenuItem("Full (With Blanking)", nullptr, om == ATGTIAEmulator::kOverscanFull))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanFull);

		ImGui::Separator();

		// Vertical Override sub-submenu
		if (ImGui::BeginMenu("Vertical Override")) {
			auto vom = gtia.GetVerticalOverscanMode();
			if (ImGui::MenuItem("Off", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Default))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Default);
			if (ImGui::MenuItem("OS Screen Only", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_OSScreen))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_OSScreen);
			if (ImGui::MenuItem("Normal", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Normal))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Normal);
			if (ImGui::MenuItem("Extended", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Extended))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Extended);
			if (ImGui::MenuItem("Full (With Blanking)", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Full))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Full);
			ImGui::EndMenu();
		}

		bool palExt = gtia.IsOverscanPALExtended();
		if (ImGui::MenuItem("Extended PAL Height", nullptr, palExt))
			gtia.SetOverscanPALExtended(!palExt);

		bool indicatorMargin = ATUIGetDisplayPadIndicators();
		if (ImGui::MenuItem("Indicator Margin", nullptr, indicatorMargin))
			ATUISetDisplayPadIndicators(!indicatorMargin);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	// VSync toggle — query current state from renderer
	{
		int vsync = 0;
		SDL_GetRenderVSync(renderer, &vsync);
		bool vsyncOn = (vsync != 0);
		if (ImGui::MenuItem("Vertical Sync", nullptr, vsyncOn))
			SDL_SetRenderVSync(renderer, vsyncOn ? 0 : 1);
	}

	bool showFPS = ATUIGetShowFPS();
	if (ImGui::MenuItem("Show FPS", nullptr, showFPS))
		ATUISetShowFPS(!showFPS);

	// Video Outputs submenu
	if (ImGui::BeginMenu("Video Outputs")) {
		bool altView = ATUIGetAltViewEnabled();
		if (ImGui::MenuItem("1 Computer Output", nullptr, !altView))
			ATUISetAltViewEnabled(false);

		if (ATUIIsAltOutputAvailable()) {
			if (ImGui::MenuItem("Next Output"))
				ATUISelectNextAltOutput();
		}

		bool autoSwitch = ATUIGetAltViewAutoswitchingEnabled();
		if (ImGui::MenuItem("Auto-Switch Video Output", nullptr, autoSwitch))
			ATUISetAltViewAutoswitchingEnabled(!autoSwitch);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Adjust Colors..."))
		state.showAdjustColors = true;

	ImGui::MenuItem("Adjust Screen Effects...", nullptr, false, false);  // placeholder — needs shader support
	ImGui::MenuItem("Customize HUD...", nullptr, false, false);          // placeholder
	ImGui::MenuItem("Calibrate...", nullptr, false, false);              // placeholder

	ImGui::Separator();

	ImGui::MenuItem("Display", nullptr, false, false);                   // placeholder — dockable pane
	ImGui::MenuItem("Printer Output", nullptr, false, false);            // placeholder — dockable pane

	ImGui::Separator();

	// Copy/Save Frame
	if (ImGui::MenuItem("Copy Frame to Clipboard"))
		g_copyFrameRequested = true;
	ImGui::MenuItem("Copy Frame to Clipboard (True Aspect)", nullptr, false, false);  // placeholder

	if (ImGui::MenuItem("Save Frame...")) {
		static const SDL_DialogFileFilter filters[] = {
			{ "BMP Images", "bmp" },
		};
		SDL_ShowSaveFileDialog(SaveFrameCallback, nullptr, window, filters, 1, nullptr);
	}
	ImGui::MenuItem("Save Frame (True Aspect)...", nullptr, false, false);  // placeholder

	// Text Selection submenu
	if (ImGui::BeginMenu("Text Selection")) {
		ImGui::MenuItem("Copy Text", nullptr, false, false);          // placeholder
		ImGui::MenuItem("Copy Escaped Text", nullptr, false, false);  // placeholder
		ImGui::MenuItem("Copy Hex", nullptr, false, false);           // placeholder
		ImGui::MenuItem("Copy Unicode", nullptr, false, false);       // placeholder
		ImGui::MenuItem("Paste Text", nullptr, false, false);         // placeholder
		ImGui::Separator();
		ImGui::MenuItem("Select All", nullptr, false, false);         // placeholder
		ImGui::MenuItem("Deselect", nullptr, false, false);           // placeholder
		ImGui::EndMenu();
	}
}

// =========================================================================
// System menu
// =========================================================================

static void RenderSystemMenu(ATSimulator &sim, ATUIState &state) {
	// Profiles submenu (matches Windows System > Profiles)
	if (ImGui::BeginMenu("Profiles")) {
		if (ImGui::MenuItem("Edit Profiles..."))
			state.showProfiles = true;

		bool temporary = ATSettingsGetTemporaryProfileMode();
		if (ImGui::MenuItem("Temporary Profile", nullptr, temporary))
			ATSettingsSetTemporaryProfileMode(!temporary);

		ImGui::Separator();

		// Quick profile switching — list visible profiles
		uint32 currentId = ATSettingsGetCurrentProfileId();

		// Global profile
		{
			VDStringW name = ATSettingsProfileGetName(0);
			VDStringA nameU8 = VDTextWToU8(name);
			if (ImGui::MenuItem(nameU8.c_str(), nullptr, currentId == 0)) {
				if (currentId != 0) {
					ATSettingsSwitchProfile(0);
					sim.Resume();
				}
			}
		}

		// Enumerated profiles (visible only)
		vdfastvector<uint32> profileIds;
		ATSettingsProfileEnum(profileIds);
		for (uint32 id : profileIds) {
			if (!ATSettingsProfileGetVisible(id))
				continue;
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA nameU8 = VDTextWToU8(name);
			if (ImGui::MenuItem(nameU8.c_str(), nullptr, currentId == id)) {
				if (currentId != id) {
					ATSettingsSwitchProfile(id);
					sim.Resume();
				}
			}
		}

		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Configure System..."))
		state.showSystemConfig = true;

	ImGui::Separator();

	if (ImGui::MenuItem("Warm Reset", "F5")) {
		sim.WarmReset();
		sim.Resume();
	}
	if (ImGui::MenuItem("Cold Reset", "Shift+F5")) {
		sim.ColdReset();
		sim.Resume();
		if (!g_kbdOpts.mbAllowShiftOnColdReset)
			sim.GetPokey().SetShiftKeyState(false, true);
	}
	if (ImGui::MenuItem("Cold Reset (Computer Only)")) {
		sim.ColdResetComputerOnly();
		sim.Resume();
		if (!g_kbdOpts.mbAllowShiftOnColdReset)
			sim.GetPokey().SetShiftKeyState(false, true);
	}

	bool paused = sim.IsPaused();
	if (ImGui::MenuItem("Pause", "F9", paused)) {
		if (paused) sim.Resume(); else sim.Pause();
	}

	ImGui::Separator();

	bool turbo = ATUIGetTurbo();
	if (ImGui::MenuItem("Warp Speed", nullptr, turbo))
		ATUISetTurbo(!turbo);

	bool pauseInactive = ATUIGetPauseWhenInactive();
	if (ImGui::MenuItem("Pause When Inactive", nullptr, pauseInactive))
		ATUISetPauseWhenInactive(!pauseInactive);

	// Rewind submenu
	if (ImGui::BeginMenu("Rewind")) {
		ImGui::MenuItem("Quick Rewind", nullptr, false, false);  // placeholder
		ImGui::MenuItem("Rewind...", nullptr, false, false);      // placeholder
		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("Power-On Delay")) {
		int delay = sim.GetPowerOnDelay();
		if (ImGui::MenuItem("Auto", nullptr, delay < 0))
			sim.SetPowerOnDelay(-1);
		if (ImGui::MenuItem("None", nullptr, delay == 0))
			sim.SetPowerOnDelay(0);
		if (ImGui::MenuItem("1 Second", nullptr, delay == 10))
			sim.SetPowerOnDelay(10);
		if (ImGui::MenuItem("2 Seconds", nullptr, delay == 20))
			sim.SetPowerOnDelay(20);
		if (ImGui::MenuItem("3 Seconds", nullptr, delay == 30))
			sim.SetPowerOnDelay(30);
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Hold Keys For Reset"))
		ATUIToggleHoldKeys();

	bool basic = sim.IsBASICEnabled();
	if (ImGui::MenuItem("Internal BASIC (Boot Without Option Key)", nullptr, basic)) {
		sim.SetBASICEnabled(!basic);
		if (ATUIIsResetNeeded(kATUIResetFlag_BasicChange))
			sim.ColdReset();
	}

	bool casAutoBoot = sim.IsCassetteAutoBootEnabled();
	if (ImGui::MenuItem("Auto-Boot Tape (Hold Start)", nullptr, casAutoBoot))
		sim.SetCassetteAutoBootEnabled(!casAutoBoot);

	ImGui::Separator();

	// Console Switches submenu (matches Windows menu_default.txt exactly)
	if (ImGui::BeginMenu("Console Switches")) {
		bool kbdPresent = sim.IsKeyboardPresent();
		if (ImGui::MenuItem("Keyboard Present (XEGS)", nullptr, kbdPresent))
			sim.SetKeyboardPresent(!kbdPresent);

		bool selfTest = sim.IsForcedSelfTest();
		if (ImGui::MenuItem("Force Self-Test", nullptr, selfTest))
			sim.SetForcedSelfTest(!selfTest);

		// "Activate Cart Menu Button" — momentary action (matches Windows Cart.ActivateMenuButton)
		if (ImGui::MenuItem("Activate Cart Menu Button"))
			ATUIActivateDeviceButton(kATDeviceButton_CartridgeResetBank, true);

		// "Enable Cart Switch" — toggle (matches Windows Cart.ToggleSwitch)
		bool cartSwitch = sim.GetCartridgeSwitch();
		if (ImGui::MenuItem("Enable Cart Switch", nullptr, cartSwitch))
			sim.SetCartridgeSwitch(!cartSwitch);

		// Device buttons (shown only when the device is present)
		static const struct { ATDeviceButton btn; const char *label; } kDevButtons[] = {
			{ kATDeviceButton_BlackBoxDumpScreen, "BlackBox: Dump Screen" },
			{ kATDeviceButton_BlackBoxMenu, "BlackBox: Menu" },
			{ kATDeviceButton_IDEPlus2SwitchDisks, "IDE Plus 2.0: Switch Disks" },
			{ kATDeviceButton_IDEPlus2WriteProtect, "IDE Plus 2.0: Write Protect" },
			{ kATDeviceButton_IDEPlus2SDX, "IDE Plus 2.0: SDX Enable" },
			{ kATDeviceButton_IndusGTError, "Indus GT: Error Button" },
			{ kATDeviceButton_IndusGTTrack, "Indus GT: Track Button" },
			{ kATDeviceButton_IndusGTId, "Indus GT: Drive Type Button" },
			{ kATDeviceButton_IndusGTBootCPM, "Indus GT: Boot CP/M" },
			{ kATDeviceButton_IndusGTChangeDensity, "Indus GT: Change Density" },
			{ kATDeviceButton_HappySlow, "Happy: Slow Switch" },
			{ kATDeviceButton_HappyWPEnable, "Happy 1050: Write protect disk" },
			{ kATDeviceButton_HappyWPDisable, "Happy 1050: Write enable disk" },
			{ kATDeviceButton_ATR8000Reset, "ATR8000: Reset" },
			{ kATDeviceButton_XELCFSwap, "XEL-CF3: Swap" },
		};

		bool anyDevBtn = false;
		for (auto& db : kDevButtons) {
			if (ATUIGetDeviceButtonSupported((uint32)db.btn)) {
				if (!anyDevBtn) {
					ImGui::Separator();
					anyDevBtn = true;
				}
				bool dep = ATUIGetDeviceButtonDepressed((uint32)db.btn);
				if (ImGui::MenuItem(db.label, nullptr, dep))
					ATUIActivateDeviceButton((uint32)db.btn, !dep);
			}
		}
		ImGui::EndMenu();
	}

}

// =========================================================================
// Input menu
// =========================================================================

// Render a single Port submenu.  Queries ATInputManager for all input maps
// that touch the given physical port, presents them as radio items, and
// toggles activation.  Mirrors Windows uiportmenus.cpp behavior exactly.
static void RenderPortSubmenu(ATInputManager &im, int portIdx) {
	// Collect input maps that use this physical port
	struct MapEntry {
		ATInputMap *map;
		VDStringA  name;   // UTF-8 for ImGui
		bool       active;
	};
	std::vector<MapEntry> entries;

	uint32 mapCount = im.GetInputMapCount();
	for (uint32 i = 0; i < mapCount; ++i) {
		vdrefptr<ATInputMap> imap;
		if (im.GetInputMapByIndex(i, ~imap)) {
			if (imap->UsesPhysicalPort(portIdx)) {
				MapEntry e;
				e.map = imap;
				e.name = VDTextWToU8(imap->GetName(), -1);
				e.active = im.IsInputMapEnabled(imap);
				entries.push_back(std::move(e));
			}
		}
	}

	// Sort alphabetically (case-insensitive), matching Windows
	std::sort(entries.begin(), entries.end(),
		[](const MapEntry &a, const MapEntry &b) {
			return vdwcsicmp(a.map->GetName(), b.map->GetName()) < 0;
		});

	// "None" item — selected when no maps are active for this port
	bool anyActive = false;
	for (const auto &e : entries)
		if (e.active) { anyActive = true; break; }

	if (ImGui::MenuItem("None", nullptr, !anyActive)) {
		// Deactivate all maps for this port
		for (const auto &e : entries)
			if (e.active)
				im.ActivateInputMap(e.map, false);
	}

	// One radio item per input map — strict radio behavior matching Windows:
	// clicking any item activates it and deactivates all others for this port.
	// Clicking the already-active item is a no-op (it stays selected).
	for (const auto &e : entries) {
		if (ImGui::MenuItem(e.name.c_str(), nullptr, e.active)) {
			for (const auto &other : entries)
				im.ActivateInputMap(other.map, &other == &e);
		}
	}
}

static void RenderInputMenu(ATSimulator &sim, ATUIState &state) {
	ATInputManager *pIM = sim.GetInputManager();

	if (ImGui::MenuItem("Input Mappings..."))
		state.showInputMappings = true;
	if (ImGui::MenuItem("Input Setup..."))
		state.showInputSetup = true;

	// Cycle Quick Maps — cycles through maps marked as quick-cycle
	if (ImGui::MenuItem("Cycle Quick Maps")) {
		if (pIM) {
			ATInputMap *pMap = pIM->CycleQuickMaps();
			if (pMap) {
				VDStringA msg;
				msg = "Quick map: ";
				msg += VDTextWToU8(pMap->GetName(), -1);
				fprintf(stderr, "[AltirraSDL] %s\n", msg.c_str());
			} else {
				fprintf(stderr, "[AltirraSDL] Quick maps disabled\n");
			}
		}
	}

	ImGui::Separator();

	// Capture Mouse — only enabled when mouse is mapped in an input map
	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool captured = ATUIIsMouseCaptured();
		if (ImGui::MenuItem("Capture Mouse", nullptr, captured, mouseMapped)) {
			if (captured)
				ATUIReleaseMouse();
			else
				ATUICaptureMouse();
		}
	}

	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool mouseAutoCapture = ATUIGetMouseAutoCapture();
		if (ImGui::MenuItem("Auto-Capture Mouse", nullptr, mouseAutoCapture, mouseMapped))
			ATUISetMouseAutoCapture(!mouseAutoCapture);
	}

	ImGui::Separator();

	ImGui::MenuItem("Light Pen/Gun...", nullptr, false, false);          // placeholder
	ImGui::MenuItem("Recalibrate Light Pen/Gun", nullptr, false, false); // placeholder

	ImGui::Separator();

	// Port 1-4 submenus — dynamic input map assignment per port
	if (pIM) {
		for (int i = 0; i < 4; ++i) {
			char label[16];
			snprintf(label, sizeof(label), "Port %d", i + 1);
			if (ImGui::BeginMenu(label)) {
				RenderPortSubmenu(*pIM, i);
				ImGui::EndMenu();
			}
		}
	}
}

// =========================================================================
// Cheat menu
// =========================================================================

static void RenderCheatMenu(ATSimulator &sim) {
	ImGui::MenuItem("Cheater...", nullptr, false, false);
	ImGui::Separator();

	ATGTIAEmulator& gtia = sim.GetGTIA();

	bool pmColl = gtia.ArePMCollisionsEnabled();
	if (ImGui::MenuItem("Disable P/M Collisions", nullptr, !pmColl))
		gtia.SetPMCollisionsEnabled(!pmColl);

	bool pfColl = gtia.ArePFCollisionsEnabled();
	if (ImGui::MenuItem("Disable Playfield Collisions", nullptr, !pfColl))
		gtia.SetPFCollisionsEnabled(!pfColl);
}

// =========================================================================
// Debug menu
// =========================================================================

static void RenderDebugMenu(ATSimulator &sim) {
	ImGui::MenuItem("Enable Debugger", nullptr, false, false);           // placeholder
	ImGui::MenuItem("Open Source File...", nullptr, false, false);       // placeholder
	ImGui::MenuItem("Source File List...", nullptr, false, false);       // placeholder

	if (ImGui::BeginMenu("Window")) {
		ImGui::MenuItem("Console", nullptr, false, false);
		ImGui::MenuItem("Registers", nullptr, false, false);
		ImGui::MenuItem("Disassembly", nullptr, false, false);
		ImGui::MenuItem("Call Stack", nullptr, false, false);
		ImGui::MenuItem("History", nullptr, false, false);
		if (ImGui::BeginMenu("Memory")) {
			ImGui::MenuItem("Memory 1", nullptr, false, false);
			ImGui::MenuItem("Memory 2", nullptr, false, false);
			ImGui::MenuItem("Memory 3", nullptr, false, false);
			ImGui::MenuItem("Memory 4", nullptr, false, false);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Watch")) {
			ImGui::MenuItem("Watch 1", nullptr, false, false);
			ImGui::MenuItem("Watch 2", nullptr, false, false);
			ImGui::MenuItem("Watch 3", nullptr, false, false);
			ImGui::MenuItem("Watch 4", nullptr, false, false);
			ImGui::EndMenu();
		}
		ImGui::MenuItem("Breakpoints", nullptr, false, false);
		ImGui::MenuItem("Targets", nullptr, false, false);
		ImGui::MenuItem("Debug Display", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Visualization")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto am = gtia.GetAnalysisMode();

		if (ImGui::MenuItem("Cycle GTIA Visualization")) {
			int next = ((int)am + 1) % ATGTIAEmulator::kAnalyzeCount;
			gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)next);
		}
		ImGui::MenuItem("Cycle ANTIC Visualization", nullptr, false, false);  // placeholder

		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Options")) {
		ImGui::MenuItem("Auto-Reload ROMs on Cold Reset", nullptr, false, false);
		ImGui::MenuItem("Randomize Memory On EXE Load", nullptr, false, false);
		ImGui::MenuItem("Break at EXE Run Address", nullptr, false, false);
		ImGui::Separator();
		ImGui::MenuItem("Change Font...", nullptr, false, false);
		ImGui::EndMenu();
	}

	ImGui::Separator();

	ImGui::MenuItem("Run/Break", nullptr, false, false);
	ImGui::MenuItem("Break", nullptr, false, false);

	ImGui::Separator();

	ImGui::MenuItem("Step Into", nullptr, false, false);
	ImGui::MenuItem("Step Over", nullptr, false, false);
	ImGui::MenuItem("Step Out", nullptr, false, false);

	ImGui::Separator();

	if (ImGui::BeginMenu("Profile")) {
		ImGui::MenuItem("Profile View", nullptr, false, false);
		ImGui::EndMenu();
	}
	ImGui::MenuItem("Verifier...", nullptr, false, false);
	ImGui::MenuItem("Performance Analyzer...", nullptr, false, false);
}

// =========================================================================
// Record menu
// =========================================================================

static void RenderRecordMenu(ATSimulator &sim, SDL_Window *window) {
	bool recording = ATUIIsRecording();

	if (ImGui::MenuItem("Record Raw Audio...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter rawFilters[] = {
			{ "Raw PCM Audio", "pcm" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordRaw, fl[0]);
		}, nullptr, window, rawFilters, 1, nullptr);
	}

	if (ImGui::MenuItem("Record Audio...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter wavFilters[] = {
			{ "WAV Audio", "wav" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordWAV, fl[0]);
		}, nullptr, window, wavFilters, 1, nullptr);
	}

	if (ImGui::MenuItem("Record Video...", nullptr, g_pVideoWriter != nullptr, !recording))
		g_showVideoRecordingDialog = true;

	if (ImGui::MenuItem("Record SAP Type R...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter sapFilters[] = {
			{ "SAP Files", "sap" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordSAP, fl[0]);
		}, nullptr, window, sapFilters, 1, nullptr);
	}

	ImGui::MenuItem("Record VGM...", nullptr, false, false);  // placeholder — excluded from build (wchar_t size)

	ImGui::Separator();

	if (ImGui::MenuItem("Stop Recording", nullptr, false, recording))
		ATUIStopRecording();

	bool videoRecording = (g_pVideoWriter != nullptr);
	if (ImGui::MenuItem("Pause/Resume Recording", nullptr, false, videoRecording)) {
		if (g_pVideoWriter) {
			if (g_pVideoWriter->IsPaused())
				g_pVideoWriter->Resume();
			else
				g_pVideoWriter->Pause();
		}
	}
}

// =========================================================================
// Video Recording Settings dialog (ImGui replacement for IDD_VIDEO_RECORDING)
// =========================================================================

static void RenderVideoRecordingDialog(SDL_Window *window) {
	if (!g_showVideoRecordingDialog)
		return;

	ImGui::SetNextWindowSize(ImVec2(460, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Record Video", &g_showVideoRecordingDialog, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		g_showVideoRecordingDialog = false;
		ImGui::End();
		return;
	}

	// Load saved settings from registry
	static bool settingsLoaded = false;
	if (!settingsLoaded) {
		VDRegistryAppKey key("Settings");
		g_videoRecEncoding = (ATVideoEncoding)key.getEnumInt("Video Recording: Compression Mode", kATVideoEncodingCount, kATVideoEncoding_ZMBV);
		// Clamp to AVI-only encodings for SDL3 build
		if (g_videoRecEncoding > kATVideoEncoding_ZMBV)
			g_videoRecEncoding = kATVideoEncoding_ZMBV;
		g_videoRecFrameRate = (ATVideoRecordingFrameRate)key.getEnumInt("Video Recording: Frame Rate", kATVideoRecordingFrameRateCount, kATVideoRecordingFrameRate_Normal);
		g_videoRecHalfRate = key.getBool("Video Recording: Half Rate", false);
		g_videoRecEncodeAll = key.getBool("Video Recording: Encode All Frames", false);
		g_videoRecAspectRatioMode = (ATVideoRecordingAspectRatioMode)key.getEnumInt("Video Recording: Aspect Ratio Mode", (int)ATVideoRecordingAspectRatioMode::Count, (int)ATVideoRecordingAspectRatioMode::IntegerOnly);
		g_videoRecResamplingMode = (ATVideoRecordingResamplingMode)key.getEnumInt("Video Recording: Resampling Mode", (int)ATVideoRecordingResamplingMode::Count, (int)ATVideoRecordingResamplingMode::Nearest);
		g_videoRecScalingMode = (ATVideoRecordingScalingMode)key.getEnumInt("Video Recording: Frame Size Mode", (int)ATVideoRecordingScalingMode::Count, (int)ATVideoRecordingScalingMode::None);
		settingsLoaded = true;
	}

	// Video codec
	static const char *kCodecNames[] = {
		"Uncompressed (AVI)",
		"Run-Length Encoding (AVI)",
		"Zipped Motion Block Vector (AVI)",
	};
	int codecIdx = (int)g_videoRecEncoding;
	if (codecIdx > 2) codecIdx = 2;
	if (ImGui::Combo("Video Codec", &codecIdx, kCodecNames, 3))
		g_videoRecEncoding = (ATVideoEncoding)codecIdx;

	// Codec description
	static const char *kCodecDescs[] = {
		"Uncompressed RGB. Largest files, most compatible.",
		"Lossless RLE compression. Smaller than raw, 8-bit video only.",
		"Lossless ZMBV (DOSBox). Excellent compression for retro video.\nNeeds ffmpeg/ffdshow to play.",
	};
	ImGui::TextWrapped("%s", kCodecDescs[codecIdx]);
	ImGui::Separator();

	// Frame rate
	const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;
	double halfMul = g_videoRecHalfRate ? 0.5 : 1.0;

	static const double kFrameRates[][2]={
		{ 3579545.0 / (2.0*114.0*262.0), 1773447.0 / (114.0*312.0) },
		{ 60000.0/1001.0, 50000.0/1001.0 },
		{ 60.0, 50.0 },
	};

	char frlabel0[64], frlabel1[64], frlabel2[64];
	snprintf(frlabel0, sizeof frlabel0, "Accurate (%.3f fps)", kFrameRates[0][hz50] * halfMul);
	snprintf(frlabel1, sizeof frlabel1, "Broadcast (%.3f fps)", kFrameRates[1][hz50] * halfMul);
	snprintf(frlabel2, sizeof frlabel2, "Integral (%.3f fps)", kFrameRates[2][hz50] * halfMul);

	int frIdx = (int)g_videoRecFrameRate;
	ImGui::Text("Frame Rate:");
	ImGui::RadioButton(frlabel0, &frIdx, 0);
	ImGui::RadioButton(frlabel1, &frIdx, 1);
	ImGui::RadioButton(frlabel2, &frIdx, 2);
	g_videoRecFrameRate = (ATVideoRecordingFrameRate)frIdx;

	ImGui::Checkbox("Record at half frame rate", &g_videoRecHalfRate);
	ImGui::Checkbox("Encode duplicate frames as full frames", &g_videoRecEncodeAll);
	ImGui::Separator();

	// Scaling
	static const char *kScalingModes[] = {
		"No scaling",
		"Scale to 640x480 (4:3)",
		"Scale to 854x480 (16:9)",
		"Scale to 960x720 (4:3)",
		"Scale to 1280x720 (16:9)",
	};
	int scalingIdx = (int)g_videoRecScalingMode;
	ImGui::Combo("Scaling", &scalingIdx, kScalingModes, 5);
	g_videoRecScalingMode = (ATVideoRecordingScalingMode)scalingIdx;

	// Aspect ratio
	static const char *kAspectModes[] = {
		"None - raw pixels",
		"Pixel double - 1x/2x only",
		"Full - correct pixel aspect ratio",
	};
	int arIdx = (int)g_videoRecAspectRatioMode;
	ImGui::Combo("Aspect Ratio", &arIdx, kAspectModes, 3);
	g_videoRecAspectRatioMode = (ATVideoRecordingAspectRatioMode)arIdx;

	// Resampling
	static const char *kResamplingModes[] = {
		"Nearest - sharpest",
		"Sharp Bilinear",
		"Bilinear - smoothest",
	};
	int rsIdx = (int)g_videoRecResamplingMode;
	ImGui::Combo("Resampling", &rsIdx, kResamplingModes, 3);
	g_videoRecResamplingMode = (ATVideoRecordingResamplingMode)rsIdx;

	ImGui::Separator();

	// OK/Cancel
	if (ImGui::Button("Record", ImVec2(120, 0))) {
		// Save settings
		VDRegistryAppKey key("Settings");
		key.setInt("Video Recording: Compression Mode", (int)g_videoRecEncoding);
		key.setInt("Video Recording: Frame Rate", (int)g_videoRecFrameRate);
		key.setBool("Video Recording: Half Rate", g_videoRecHalfRate);
		key.setBool("Video Recording: Encode All Frames", g_videoRecEncodeAll);
		key.setInt("Video Recording: Aspect Ratio Mode", (int)g_videoRecAspectRatioMode);
		key.setInt("Video Recording: Resampling Mode", (int)g_videoRecResamplingMode);
		key.setInt("Video Recording: Frame Size Mode", (int)g_videoRecScalingMode);

		g_showVideoRecordingDialog = false;

		// Show save file dialog
		static const SDL_DialogFileFilter aviFilters[] = {
			{ "AVI Video", "avi" }, { "All Files", "*" },
		};
		// Capture encoding to pass through the callback
		static ATVideoEncoding s_pendingEncoding;
		s_pendingEncoding = g_videoRecEncoding;
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordVideo, fl[0], (int)s_pendingEncoding);
		}, nullptr, window, aviFilters, 1, nullptr);
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(120, 0)))
		g_showVideoRecordingDialog = false;

	ImGui::End();
}

// =========================================================================
// Tools menu
// =========================================================================

// SAP-to-EXE: two-step file dialog (open SAP, then save XEX)
static std::string g_sapSourcePath;

static void SAPSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0] || g_sapSourcePath.empty())
		return;
	ATUIPushDeferred2(kATDeferred_ConvertSAPToEXE, g_sapSourcePath.c_str(), filelist[0]);
	g_sapSourcePath.clear();
}

static void SAPOpenCallback(void *userdata, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	g_sapSourcePath = filelist[0];
	static const SDL_DialogFileFilter kXEXFilters[] = {
		{ "Atari Executable", "xex;obx;com" },
		{ "All Files", "*" },
	};
	SDL_ShowSaveFileDialog(SAPSaveCallback, nullptr, (SDL_Window *)userdata, kXEXFilters, 2, nullptr);
}

// Export ROM Set: folder dialog callback
static void ExportROMSetCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	ATUIPushDeferred(kATDeferred_ExportROMSet, filelist[0]);
}

// Analyze Tape: two-step file dialog (open WAV, then save WAV)
static std::string g_tapeSourcePath;

static void TapeAnalysisSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0] || g_tapeSourcePath.empty())
		return;
	ATUIPushDeferred2(kATDeferred_AnalyzeTapeDecode, g_tapeSourcePath.c_str(), filelist[0]);
	g_tapeSourcePath.clear();
}

static void TapeAnalysisOpenCallback(void *userdata, const char * const *filelist, int) {
	if (!filelist || !filelist[0])
		return;
	g_tapeSourcePath = filelist[0];
	static const SDL_DialogFileFilter kWAVFilters[] = {
		{ "WAV Audio", "wav" },
		{ "All Files", "*" },
	};
	SDL_ShowSaveFileDialog(TapeAnalysisSaveCallback, nullptr, (SDL_Window *)userdata, kWAVFilters, 2, nullptr);
}

static void RenderToolsMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	if (ImGui::MenuItem("Disk Explorer..."))
		state.showDiskExplorer = true;

	if (ImGui::MenuItem("Convert SAP to EXE...")) {
		static const SDL_DialogFileFilter kSAPFilters[] = {
			{ "SAP Music Files", "sap" },
			{ "All Files", "*" },
		};
		SDL_ShowOpenFileDialog(SAPOpenCallback, window, window, kSAPFilters, 2, nullptr, false);
	}

	if (ImGui::MenuItem("Export ROM set..."))
		SDL_ShowOpenFolderDialog(ExportROMSetCallback, nullptr, window, nullptr, false);

	if (ImGui::MenuItem("Analyze tape decoding...")) {
		static const SDL_DialogFileFilter kTapeFilters[] = {
			{ "Audio Files", "wav;flac" },
			{ "All Files", "*" },
		};
		SDL_ShowOpenFileDialog(TapeAnalysisOpenCallback, window, window, kTapeFilters, 2, nullptr, false);
	}

	ImGui::Separator();

	if (ImGui::MenuItem("First Time Setup..."))
		state.showSetupWizard = true;

	ImGui::Separator();

	if (ImGui::MenuItem("Keyboard Shortcuts..."))
		state.showKeyboardShortcuts = true;

	if (ImGui::MenuItem("Compatibility Database..."))
		state.showCompatDB = true;

	if (ImGui::MenuItem("Advanced Configuration..."))
		state.showAdvancedConfig = true;
}

// =========================================================================
// Window menu
// =========================================================================

static void RenderWindowMenu(SDL_Window *window) {
	ImGui::MenuItem("Close", nullptr, false, false);            // placeholder — dockable pane
	ImGui::MenuItem("Undock", nullptr, false, false);           // placeholder — dockable pane
	ImGui::MenuItem("Next Pane", nullptr, false, false);        // placeholder — dockable pane
	ImGui::MenuItem("Previous Pane", nullptr, false, false);    // placeholder — dockable pane
	ImGui::Separator();

	if (ImGui::BeginMenu("Adjust Window Size")) {
		static const struct { const char *label; int w; int h; } kSizes[] = {
			{ "1x (336x240)", 336, 240 },
			{ "2x (672x480)", 672, 480 },
			{ "3x (1008x720)", 1008, 720 },
			{ "4x (1344x960)", 1344, 960 },
		};
		for (auto& sz : kSizes) {
			if (ImGui::MenuItem(sz.label))
				SDL_SetWindowSize(window, sz.w, sz.h);
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Reset Window Layout")) {
		// Reset to default 2x size
		SDL_SetWindowSize(window, 672, 480);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
}

// =========================================================================
// Help menu
// =========================================================================

static void RenderHelpMenu(ATUIState &state) {
	ImGui::MenuItem("Contents", nullptr, false, false);  // placeholder — needs help system

	if (ImGui::MenuItem("About"))
		state.showAboutDialog = true;

	if (ImGui::MenuItem("Change Log"))
		state.showChangeLog = true;

	if (ImGui::MenuItem("Command-Line Help"))
		state.showCommandLineHelp = true;

	ImGui::MenuItem("Export Debugger Help...", nullptr, false, false);  // placeholder
	ImGui::MenuItem("Check For Updates", nullptr, false, false);       // placeholder — N/A on Linux
	ImGui::MenuItem("Altirra Home...", nullptr, false, false);         // placeholder
}

// =========================================================================
// Audio Options dialog
// Matches Windows IDD_AUDIO_OPTIONS (uiaudiooptions.cpp)
// =========================================================================

static void RenderAudioOptionsDialog(ATUIState &state) {
	if (!state.showAudioOptions)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 320), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Audio Options", &state.showAudioOptions, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAudioOptions = false;
		ImGui::End();
		return;
	}

	IATAudioOutput *audioOut = g_sim.GetAudioOutput();
	if (!audioOut) {
		ImGui::Text("No audio output available.");
		ImGui::End();
		return;
	}

	// Volume slider (0 to 200 ticks = -20dB to 0dB)
	float volume = audioOut->GetVolume();
	int volTick = 200 + VDRoundToInt(100.0f * log10f(std::max(volume, 0.01f)));
	volTick = std::clamp(volTick, 0, 200);
	if (ImGui::SliderInt("Volume", &volTick, 0, 200)) {
		audioOut->SetVolume(powf(10.0f, (volTick - 200) * 0.01f));
	}
	ImGui::SameLine();
	ImGui::Text("%.1fdB", 0.1f * (volTick - 200));

	// Drive volume slider
	float driveVol = audioOut->GetMixLevel(kATAudioMix_Drive);
	int driveVolTick = 200 + VDRoundToInt(100.0f * log10f(std::max(driveVol, 0.01f)));
	driveVolTick = std::clamp(driveVolTick, 0, 200);
	if (ImGui::SliderInt("Drive volume", &driveVolTick, 0, 200)) {
		audioOut->SetMixLevel(kATAudioMix_Drive, powf(10.0f, (driveVolTick - 200) * 0.01f));
	}
	ImGui::SameLine();
	ImGui::Text("%.1fdB", 0.1f * (driveVolTick - 200));

	// Covox volume slider
	float covoxVol = audioOut->GetMixLevel(kATAudioMix_Covox);
	int covoxVolTick = 200 + VDRoundToInt(100.0f * log10f(std::max(covoxVol, 0.01f)));
	covoxVolTick = std::clamp(covoxVolTick, 0, 200);
	if (ImGui::SliderInt("Covox volume", &covoxVolTick, 0, 200)) {
		audioOut->SetMixLevel(kATAudioMix_Covox, powf(10.0f, (covoxVolTick - 200) * 0.01f));
	}
	ImGui::SameLine();
	ImGui::Text("%.1fdB", 0.1f * (covoxVolTick - 200));

	ImGui::Separator();

	// Latency slider (10ms to 500ms)
	int latency = audioOut->GetLatency();
	int latencyTick = (latency + 5) / 10;
	latencyTick = std::clamp(latencyTick, 1, 50);
	if (ImGui::SliderInt("Latency", &latencyTick, 1, 50)) {
		audioOut->SetLatency(latencyTick * 10);
	}
	ImGui::SameLine();
	ImGui::Text("%d ms", latencyTick * 10);

	// Extra buffer slider (20ms to 500ms)
	int extraBuf = audioOut->GetExtraBuffer();
	int extraBufTick = (extraBuf + 5) / 10;
	extraBufTick = std::clamp(extraBufTick, 2, 50);
	if (ImGui::SliderInt("Extra buffer", &extraBufTick, 2, 50)) {
		audioOut->SetExtraBuffer(extraBufTick * 10);
	}
	ImGui::SameLine();
	ImGui::Text("%d ms", extraBufTick * 10);

	ImGui::Separator();

	bool debug = g_sim.IsAudioStatusEnabled();
	if (ImGui::Checkbox("Show debug info", &debug))
		g_sim.SetAudioStatusEnabled(debug);

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showAudioOptions = false;

	ImGui::End();
}

// =========================================================================
// Copy Frame to Clipboard (SDL3)
// =========================================================================

// Clipboard callbacks receive the SDL_Surface* via userdata, so each
// clipboard operation owns its own surface independently.  When SDL
// replaces the clipboard (e.g. a second Copy Frame), it calls the OLD
// cleanup callback with the OLD userdata, correctly freeing only the
// old surface.

static const void *ClipboardDataCallback(void *userdata, const char *mime_type, size_t *size) {
	SDL_Surface *surface = static_cast<SDL_Surface *>(userdata);
	if (!surface || strcmp(mime_type, "image/bmp") != 0) {
		*size = 0;
		return nullptr;
	}

	// Encode the surface as BMP into a dynamic memory stream
	SDL_IOStream *mem = SDL_IOFromDynamicMem();
	if (!mem) {
		*size = 0;
		return nullptr;
	}

	if (!SDL_SaveBMP_IO(surface, mem, false)) {
		SDL_CloseIO(mem);
		*size = 0;
		return nullptr;
	}

	Sint64 bmpSize = SDL_TellIO(mem);
	if (bmpSize <= 0) {
		SDL_CloseIO(mem);
		*size = 0;
		return nullptr;
	}

	SDL_SeekIO(mem, 0, SDL_IO_SEEK_SET);
	void *bmpData = SDL_malloc((size_t)bmpSize);
	if (!bmpData) {
		SDL_CloseIO(mem);
		*size = 0;
		return nullptr;
	}

	SDL_ReadIO(mem, bmpData, (size_t)bmpSize);
	SDL_CloseIO(mem);
	*size = (size_t)bmpSize;
	return bmpData;
}

static void ClipboardCleanupCallback(void *userdata) {
	SDL_Surface *surface = static_cast<SDL_Surface *>(userdata);
	if (surface)
		SDL_DestroySurface(surface);
}

static void CopyFrameToClipboard(SDL_Renderer *renderer) {
	SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
	if (!surface) {
		fprintf(stderr, "[AltirraSDL] Copy frame: failed to read pixels: %s\n", SDL_GetError());
		return;
	}

	// Pass surface as userdata — SDL owns its lifetime via the cleanup callback.
	// When SDL_SetClipboardData is called again, SDL calls the OLD cleanup
	// with the OLD userdata (old surface), then installs the new callbacks.
	const char *mimeTypes[] = { "image/bmp" };
	if (SDL_SetClipboardData(ClipboardDataCallback, ClipboardCleanupCallback, surface, mimeTypes, 1))
		fprintf(stderr, "[AltirraSDL] Frame copied to clipboard\n");
	else
		fprintf(stderr, "[AltirraSDL] Failed to copy frame to clipboard: %s\n", SDL_GetError());
}

// =========================================================================
// Command-Line Help dialog
// =========================================================================

static void RenderCommandLineHelpDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Command-Line Help", &state.showCommandLineHelp, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showCommandLineHelp = false;
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Usage: AltirraSDL [options] [image-file]");
	ImGui::Separator();

	ImGui::TextWrapped(
		"Positional arguments:\n"
		"  image-file            Load and boot the given disk/cartridge/tape image\n\n"
		"The emulator accepts ATR, XEX, BIN, ROM, CAR, CAS, WAV, and ATX files.\n"
		"Drag-and-drop onto the window also loads an image.\n\n"
		"Settings are stored in ~/.config/altirra/settings.ini");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showCommandLineHelp = false;

	ImGui::End();
}

// =========================================================================
// Change Log dialog
// =========================================================================

static void RenderChangeLogDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Change Log", &state.showChangeLog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showChangeLog = false;
		ImGui::End();
		return;
	}

	ImGui::TextWrapped(
		"AltirraSDL - Cross-Platform Frontend\n\n"
		"This is the SDL3 + Dear ImGui cross-platform port of Altirra.\n"
		"It aims for full feature parity with the Windows version.\n\n"
		"Current status:\n"
		"  - Full emulation core (CPU, ANTIC, GTIA, POKEY, PIA)\n"
		"  - All hardware modes (800/800XL/1200XL/130XE/1400XL/XEGS/5200)\n"
		"  - Disk, cassette, and cartridge support\n"
		"  - Keyboard, gamepad, and mouse input\n"
		"  - Audio output via SDL3\n"
		"  - Settings persistence\n"
		"  - Profile management\n"
		"  - State save/load\n\n"
		"For the full Altirra change log, see the Windows version documentation.");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showChangeLog = false;

	ImGui::End();
}

// =========================================================================
// About dialog
// =========================================================================

static void RenderAboutDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(420, 220), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("About AltirraSDL", &state.showAboutDialog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAboutDialog = false;
		ImGui::End();
		return;
	}

	ImGui::Text("AltirraSDL");
	ImGui::Separator();
	ImGui::TextWrapped(
		"Atari 800/800XL/5200 emulator\n"
		"Based on Altirra by Avery Lee\n"
		"SDL3 + Dear ImGui cross-platform frontend\n\n"
		"Licensed under GNU GPL v2+");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showAboutDialog = false;

	ImGui::End();
}

// =========================================================================
// Main menu bar
// =========================================================================

static void RenderMainMenu(ATSimulator &sim, SDL_Window *window, SDL_Renderer *renderer, ATUIState &state) {
	if (!ImGui::BeginMainMenuBar())
		return;

	if (ImGui::BeginMenu("File")) { RenderFileMenu(sim, state, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("View")) { RenderViewMenu(sim, state, window, renderer); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("System")) { RenderSystemMenu(sim, state); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Input")) { RenderInputMenu(sim, state); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Cheat")) { RenderCheatMenu(sim); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Debug")) { RenderDebugMenu(sim); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Record")) { RenderRecordMenu(sim, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Tools")) { RenderToolsMenu(sim, state, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Window")) { RenderWindowMenu(window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Help")) { RenderHelpMenu(state); ImGui::EndMenu(); }

	ImGui::EndMainMenuBar();
}

// =========================================================================
// Status overlay
// =========================================================================

static void RenderStatusOverlay(ATSimulator &sim) {
	bool showFPS = ATUIGetShowFPS();
	bool showIndicators = ATUIGetDisplayIndicators();
	bool paused = sim.IsPaused();
	bool recording = ATUIIsRecording();

	// Check for any active drive or cassette activity
	bool hasDriveActivity = false;
	bool hasCassetteActivity = false;
	if (showIndicators) {
		for (int i = 0; i < 15; ++i) {
			auto& di = sim.GetDiskInterface(i);
			if (di.GetClientCount() > 0 && di.IsDiskLoaded()) {
				hasDriveActivity = true;
				break;
			}
		}
		hasCassetteActivity = (sim.GetCassette().GetImage() != nullptr);
	}

	if (!showFPS && !paused && !recording && !hasDriveActivity && !hasCassetteActivity)
		return;

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x - 10.0f, io.DisplaySize.y - 10.0f),
		ImGuiCond_Always, ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.5f);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

	if (ImGui::Begin("##StatusOverlay", nullptr, flags)) {
		bool needSep = false;

		if (showFPS) {
			ImGui::Text("%.0f FPS", io.Framerate);
			needSep = true;
		}
		if (paused) {
			if (needSep) ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "PAUSED");
			needSep = true;
		}
		if (recording) {
			if (needSep) ImGui::SameLine();
			bool recPaused = g_pVideoWriter && g_pVideoWriter->IsPaused();
			if (recPaused)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "REC PAUSED");
			else
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC");
			needSep = true;
		}

		// Drive and cassette indicators
		if (showIndicators) {
			for (int i = 0; i < 15; ++i) {
				auto& di = sim.GetDiskInterface(i);
				if (di.GetClientCount() == 0 || !di.IsDiskLoaded())
					continue;

				if (needSep) ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "D%d", i + 1);
				needSep = true;
			}

			// Cassette position indicator
			ATCassetteEmulator& cas = sim.GetCassette();
			if (cas.GetImage()) {
				if (needSep) ImGui::SameLine();

				float posSec = cas.GetPosition();
				int posMin = (int)(posSec / 60.0f);
				int posFrac = (int)fmodf(posSec, 60.0f);

				bool casActive = cas.IsPlayEnabled() || cas.IsRecordEnabled();
				ImVec4 casColor = casActive
					? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
					: ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

				ImGui::TextColored(casColor, "C: %d:%02d", posMin, posFrac);
				needSep = true;
			}
		}
	}
	ImGui::End();
}

// =========================================================================
// Exit confirmation dialog
// =========================================================================

// Persistent message string — built when the dialog opens, displayed until closed.
static VDStringA g_exitConfirmMsgUtf8;

// Build the dirty-storage message, matching Windows ATUIConfirmDiscardAllStorageGetMessage().
// Returns UTF-8 string ready for ImGui, using "  " indent instead of "\t" (tabs
// don't render as indentation in ImGui).
static VDStringA BuildDirtyStorageMessage(ATSimulator &sim) {
	vdfastvector<ATStorageId> dirtyIds;
	sim.GetDirtyStorage(dirtyIds, ~(uint32)0);

	vdfastvector<ATDebuggerStorageId> dbgDirtyIds;
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->GetDirtyStorage(dbgDirtyIds);

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return VDStringA();

	std::sort(dirtyIds.begin(), dirtyIds.end());
	std::sort(dbgDirtyIds.begin(), dbgDirtyIds.end());

	VDStringA msg;
	msg = "The following modified items have not been saved:\n\n";
	msg += "  Contents of emulation memory\n";

	for (const ATStorageId id : dirtyIds) {
		const uint32 type = id & kATStorageId_TypeMask;
		const uint32 unit = id & kATStorageId_UnitMask;

		switch (type) {
			case kATStorageId_Cartridge:
				msg += "  Cartridge";
				if (unit)
					msg.append_sprintf(" %u", unit + 1);
				break;

			case kATStorageId_Disk:
				msg.append_sprintf("  Disk (D%u:)", unit + 1);
				break;

			case kATStorageId_Tape:
				msg += "  Tape";
				break;

			case kATStorageId_Firmware:
				switch (unit) {
					case 0: msg += "  IDE main firmware"; break;
					case 1: msg += "  IDE SDX firmware"; break;
					case 2: msg += "  Ultimate1MB firmware"; break;
					case 3: msg += "  Rapidus flash firmware"; break;
					case 4: msg += "  Rapidus PBI firmware"; break;
				}
				break;
		}
		msg += '\n';
	}

	for (const ATDebuggerStorageId id : dbgDirtyIds) {
		switch (id) {
			case kATDebuggerStorageId_CustomSymbols:
				msg += "  Debugger: Custom Symbols\n";
				break;
			default:
				break;
		}
	}

	msg += "\nAre you sure you want to exit?";
	return msg;
}

void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state) {
	// First frame: build the message.
	if (g_exitConfirmMsgUtf8.empty()) {
		g_exitConfirmMsgUtf8 = BuildDirtyStorageMessage(sim);

		// Windows Altirra always confirms on exit:
		// - If dirty storage: lists dirty items + memory warning
		// - If nothing dirty: still warns about emulation memory loss
		if (g_exitConfirmMsgUtf8.empty())
			g_exitConfirmMsgUtf8 = "Any unsaved work in emulation memory will be lost.\n\nAre you sure you want to exit?";
	}

	ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showExitConfirm;
	if (!ImGui::Begin("Confirm Exit", &open,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!open) {
			state.showExitConfirm = false;
			g_exitConfirmMsgUtf8.clear();
		}
		ImGui::End();
		return;
	}

	// User closed via title bar X button or ESC
	if (!open || ATUICheckEscClose()) {
		state.showExitConfirm = false;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", g_exitConfirmMsgUtf8.c_str());
	ImGui::Separator();

	float buttonWidth = 120.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float totalWidth = buttonWidth * 2 + spacing;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

	if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
		state.showExitConfirm = false;
		state.exitConfirmed = true;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();

		// Push quit event so the main loop exits.
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
		state.showExitConfirm = false;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();
		return;
	}

	ImGui::End();
}

// =========================================================================
// Top-level frame render
// =========================================================================

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	SDL_Renderer *renderer, ATUIState &state)
{
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	SDL_Window *window = SDL_GetRenderWindow(renderer);

	RenderMainMenu(sim, window, renderer, state);
	RenderStatusOverlay(sim);

	// Pick up pending cartridge mapper dialog from deferred actions
	if (g_cartMapperPending) {
		state.showCartridgeMapper = true;
		g_cartMapperPending = false;
	}

	// Pick up pending compatibility check from boot
	if (g_compatCheckPending) {
		g_compatCheckPending = false;
		ATUICheckCompatibility(sim, state);
	}

	if (state.showSystemConfig)      ATUIRenderSystemConfig(sim, state);
	if (state.showDiskManager)       ATUIRenderDiskManager(sim, state, window);
	if (state.showCassetteControl)   ATUIRenderCassetteControl(sim, state, window);
	if (state.showAdjustColors)      ATUIRenderAdjustColors(sim, state);
	if (state.showDisplaySettings)   ATUIRenderDisplaySettings(sim, state);
	if (state.showCartridgeMapper)   ATUIRenderCartridgeMapper(state);
	if (state.showAudioOptions)      RenderAudioOptionsDialog(state);
	if (state.showInputMappings)     ATUIRenderInputMappings(sim, state);
	if (state.showInputSetup)        ATUIRenderInputSetup(sim, state);
	if (state.showAboutDialog)       RenderAboutDialog(state);
	if (state.showProfiles)          ATUIRenderProfiles(sim, state);
	if (state.showCommandLineHelp)   RenderCommandLineHelpDialog(state);
	if (state.showChangeLog)         RenderChangeLogDialog(state);
	if (state.showCompatWarning)     ATUIRenderCompatWarning(sim, state);
	if (state.showExitConfirm)       ATUIRenderExitConfirm(sim, state);
	if (state.showDiskExplorer)      ATUIRenderDiskExplorer(sim, state, window);
	if (state.showSetupWizard)       ATUIRenderSetupWizard(sim, state, window);
	if (state.showKeyboardShortcuts) ATUIRenderKeyboardShortcuts(state);
	if (state.showCompatDB)          ATUIRenderCompatDB(sim, state);
	if (state.showAdvancedConfig)    ATUIRenderAdvancedConfig(state);
	RenderVideoRecordingDialog(window);

	// Tools result popup (success/error messages from deferred tool actions)
	if (g_showToolsResult) {
		ImGui::OpenPopup("Tool Result");
		g_showToolsResult = false;
	}
	if (ImGui::BeginPopupModal("Tool Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted(g_toolsResultMessage.c_str());
		ImGui::Spacing();
		if (ImGui::Button("OK", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	// Export ROM overwrite confirmation popup
	if (g_showExportROMOverwrite) {
		ImGui::OpenPopup("Overwrite Existing Files?");
		g_showExportROMOverwrite = false;
	}
	if (ImGui::BeginPopupModal("Overwrite Existing Files?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("There are existing files with the same names that will be overwritten.\nAre you sure?");
		ImGui::Spacing();
		if (ImGui::Button("Yes", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
			ATUIDoExportROMSet(g_exportROMPath);
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::Render();
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

	// Save Frame / Copy Frame — readback after rendering, before present
	{
		VDStringA savePath;
		{
			std::lock_guard<std::mutex> lock(g_saveFrameMutex);
			savePath.swap(g_saveFramePath);
		}
		if (!savePath.empty()) {
			SDL_Surface *surface = SDL_RenderReadPixels(renderer, nullptr);
			if (surface) {
				if (SDL_SaveBMP(surface, savePath.c_str()))
					fprintf(stderr, "[AltirraSDL] Frame saved to %s\n", savePath.c_str());
				else
					fprintf(stderr, "[AltirraSDL] Failed to save frame: %s\n", SDL_GetError());
				SDL_DestroySurface(surface);
			} else {
				fprintf(stderr, "[AltirraSDL] Failed to read pixels: %s\n", SDL_GetError());
			}
		}

		if (g_copyFrameRequested) {
			g_copyFrameRequested = false;
			CopyFrameToClipboard(renderer);
		}
	}
}
