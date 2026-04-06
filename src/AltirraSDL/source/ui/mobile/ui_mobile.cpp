//	AltirraSDL - Mobile UI (hamburger menu, settings, file browser)
//	Touch-first UI for Android phones and tablets.
//	Provides hamburger slide-in menu, streamlined settings, and file browser.
//	All sizing uses density-independent pixels (dp) scaled by contentScale.

#include <stdafx.h>
#include <cwctype>
#include <vector>
#include <algorithm>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <vd2/system/registry.h>
#include <vd2/system/error.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "ui_mobile.h"
#include "ui_main.h"
#include "touch_controls.h"
#include "touch_widgets.h"
#include "simulator.h"
#include "gtia.h"
#include <at/ataudio/pokey.h>
#include "diskinterface.h"
#include "disk.h"
#include <at/atio/diskimage.h>
#include "mediamanager.h"
#include "firmwaremanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "constants.h"
#include "display_backend.h"
#include "android_platform.h"
#include <at/ataudio/audiooutput.h>

#include "mobile_internal.h"

extern ATSimulator g_sim;
extern VDStringA ATGetConfigDir();
extern void ATRegistryFlushToDisk();
extern IDisplayBackend *ATUIGetDisplayBackend();

// -------------------------------------------------------------------------
// Persistence (registry-backed) for mobile-only UI settings.
// Desktop settings live in the existing Settings branch; mobile settings
// get their own namespace so they can't collide.
// -------------------------------------------------------------------------

namespace {
constexpr const char *kMobileKey = "Mobile";
}

static void LoadMobileConfig(ATMobileUIState &mobileState) {
	VDRegistryAppKey key(kMobileKey, false);
	if (!key.isReady())
		return;
	int sz = key.getEnumInt("ControlSize", 3, (int)ATTouchControlSize::Medium);
	mobileState.layoutConfig.controlSize = (ATTouchControlSize)sz;
	int opacityPct = key.getInt("ControlOpacity", 50);
	if (opacityPct < 10) opacityPct = 10;
	if (opacityPct > 100) opacityPct = 100;
	mobileState.layoutConfig.controlOpacity = opacityPct / 100.0f;
	mobileState.layoutConfig.hapticEnabled = key.getBool("HapticEnabled", true);
	mobileState.autoSaveOnSuspend   = key.getBool("AutoSaveOnSuspend", true);
	mobileState.autoRestoreOnStart  = key.getBool("AutoRestoreOnStart", true);
	mobileState.fxScanlines         = key.getBool("FxScanlines", false);
	mobileState.fxBloom             = key.getBool("FxBloom", false);
	mobileState.fxDistortion        = key.getBool("FxDistortion", false);
	mobileState.performancePreset   = key.getInt("PerformancePreset", 1);
	if (mobileState.performancePreset < 0 || mobileState.performancePreset > 3)
		mobileState.performancePreset = 1;
	int js = key.getInt("JoystickStyle", (int)ATTouchJoystickStyle::Analog);
	if (js < 0 || js > 2) js = 0;
	mobileState.layoutConfig.joystickStyle = (ATTouchJoystickStyle)js;
}

void SaveMobileConfig(const ATMobileUIState &mobileState) {  // shared via mobile_internal.h
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady())
			return;
		key.setInt("ControlSize", (int)mobileState.layoutConfig.controlSize);
		int pct = (int)(mobileState.layoutConfig.controlOpacity * 100.0f + 0.5f);
		if (pct < 10) pct = 10;
		if (pct > 100) pct = 100;
		key.setInt("ControlOpacity", pct);
		key.setBool("HapticEnabled", mobileState.layoutConfig.hapticEnabled);
		key.setBool("AutoSaveOnSuspend",  mobileState.autoSaveOnSuspend);
		key.setBool("AutoRestoreOnStart", mobileState.autoRestoreOnStart);
		key.setBool("FxScanlines",  mobileState.fxScanlines);
		key.setBool("FxBloom",      mobileState.fxBloom);
		key.setBool("FxDistortion", mobileState.fxDistortion);
		key.setInt("PerformancePreset", mobileState.performancePreset);
		key.setInt("JoystickStyle", (int)mobileState.layoutConfig.joystickStyle);
	}
	// Persist immediately — registry-only writes are lost if the user
	// swipes the app away from recents before it backgrounds properly.
	ATRegistryFlushToDisk();
}

// Path to the quick save-state file under the config dir.
// Kept as VDStringW because simulator.SaveState takes wchar_t*.
VDStringW QuickSaveStatePath() {
	VDStringA dirU8 = ATGetConfigDir();
	dirU8 += "/quicksave.atstate2";
	return VDTextU8ToW(dirU8);
}

void SaveFileBrowserDir(const VDStringW &dir) {
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady())
			return;
		key.setString("FileBrowserDir", dir.c_str());
	}
	ATRegistryFlushToDisk();
}

static VDStringW LoadFileBrowserDir() {
	VDRegistryAppKey key(kMobileKey, false);
	VDStringW s;
	if (key.isReady())
		key.getString("FileBrowserDir", s);
	return s;
}

bool IsFirstRunComplete() {
	VDRegistryAppKey key(kMobileKey, false);
	if (!key.isReady()) return false;
	return key.getBool("FirstRunComplete", false);
}

void SetFirstRunComplete() {
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady()) return;
		key.setBool("FirstRunComplete", true);
	}
	// Flush IMMEDIATELY.  The user may close the app before it ever
	// gets a proper WILL_ENTER_BACKGROUND event, and without flushing
	// here the flag would be lost and the wizard would re-appear on
	// every launch.
	ATRegistryFlushToDisk();
}

bool IsPermissionAsked() {
	VDRegistryAppKey key(kMobileKey, false);
	if (!key.isReady()) return false;
	return key.getBool("PermissionAsked", false);
}

void SetPermissionAsked() {
	{
		VDRegistryAppKey key(kMobileKey, true);
		if (!key.isReady()) return;
		key.setBool("PermissionAsked", true);
	}
	ATRegistryFlushToDisk();
}

// -------------------------------------------------------------------------
// dp helper / shared state — declarations live in mobile_internal.h
// -------------------------------------------------------------------------

float s_contentScale = 1.0f;

std::vector<FileBrowserEntry> s_fileBrowserEntries;
VDStringW s_fileBrowserDir;
bool s_fileBrowserNeedsRefresh = true;

// ROM folder browser mode — when true, selecting a folder triggers firmware scan
bool s_romFolderMode = false;
VDStringW s_romDir;
int s_romScanResult = -1;  // -1 = no scan yet, 0+ = number of ROMs found

// Disk-mount browser mode — when >= 0, picking a file in the browser
// mounts it into the specified drive index instead of booting.
// -1 means normal Load Game mode.
int s_diskMountTargetDrive = -1;
bool s_mobileShowAllDrives = false;

// Generic modal info popup — every destructive / long-running action
// in the mobile UI should give the user explicit feedback.  This is a
// tiny system: set s_infoModalTitle/Body, the main render pass shows
// the modal, OK dismisses it.
VDStringA s_infoModalTitle;
VDStringA s_infoModalBody;
bool      s_infoModalOpen = false;

void ShowInfoModal(const char *title, const char *body) {
	s_infoModalTitle = title ? title : "";
	s_infoModalBody  = body ? body : "";
	s_infoModalOpen  = true;
}

// Confirm dialog — reuses the same mobile sheet renderer as the
// info modal but shows Cancel + Confirm buttons and fires a
// std::function on confirm.  Used by destructive hamburger actions
// (Cold/Warm Reset, Quick Save/Load).
VDStringA s_confirmTitle;
VDStringA s_confirmBody;
std::function<void()> s_confirmAction;
bool      s_confirmActive = false;

void ShowConfirmDialog(const char *title, const char *body,
	std::function<void()> onConfirm)
{
	s_confirmTitle  = title ? title : "";
	s_confirmBody   = body  ? body  : "";
	s_confirmAction = std::move(onConfirm);
	s_confirmActive = true;
}

// Public forwarders — give non-mobile translation units (ui_main.cpp
// tool/confirm popups) a way to drive the mobile sheet without
// touching the file-local statics.
void ATMobileUI_ShowInfoModal(const char *title, const char *body) {
	ShowInfoModal(title, body);
}
void ATMobileUI_ShowConfirmDialog(const char *title, const char *body,
	std::function<void()> onConfirm)
{
	ShowConfirmDialog(title, body, std::move(onConfirm));
}

// Hierarchical settings — definition lives in mobile_internal.h.
ATMobileSettingsPage s_settingsPage = ATMobileSettingsPage::Home;

// Firmware slot currently being picked within the Firmware sub-page.
// File scope so the header back button can close the picker.
ATFirmwareType s_fwPicker = kATFirmwareType_Unknown;

// Supported file extensions for Atari images
static bool IsSupportedExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	VDStringW extLower;
	for (const wchar_t *p = ext; *p; ++p)
		extLower += (wchar_t)towlower(*p);

	static const wchar_t *kExtensions[] = {
		L"xex", L"atr", L"car", L"bin", L"rom", L"cas",
		L"dcm", L"atz", L"zip", L"gz", L"xfd", L"atx",
		L"obx", L"com", L"exe",
		nullptr
	};

	for (const wchar_t **p = kExtensions; *p; ++p) {
		if (extLower == *p)
			return true;
	}
	return false;
}

void RefreshFileBrowser(const VDStringW &dir) {
	s_fileBrowserEntries.clear();
	s_fileBrowserDir = dir;

	VDStringA dirU8 = VDTextWToU8(VDStringW(dir));

	struct EnumCtx {
		VDStringW baseDir;
		std::vector<FileBrowserEntry> *entries;
		bool romMode;
	};

	EnumCtx ctx;
	ctx.baseDir = dir;
	if (!ctx.baseDir.empty() && ctx.baseDir.back() != L'/')
		ctx.baseDir += L'/';
	ctx.entries = &s_fileBrowserEntries;
	ctx.romMode = s_romFolderMode;

	auto callback = [](void *userdata, const char *dirname, const char *fname) -> SDL_EnumerationResult {
		EnumCtx *ctx = (EnumCtx *)userdata;

		if (fname[0] == '.')
			return SDL_ENUM_CONTINUE;

		VDStringW wname = VDTextU8ToW(VDStringA(fname));
		VDStringW fullPath = ctx->baseDir + wname;
		VDStringA fullPathU8 = VDTextWToU8(fullPath);

		FileBrowserEntry entry;
		entry.name = std::move(wname);
		entry.fullPath = std::move(fullPath);

		SDL_PathInfo info;
		if (SDL_GetPathInfo(fullPathU8.c_str(), &info)) {
			entry.isDirectory = (info.type == SDL_PATHTYPE_DIRECTORY);
		} else {
			entry.isDirectory = false;
		}

		// In ROM folder mode, only show directories
		if (ctx->romMode) {
			if (entry.isDirectory)
				ctx->entries->push_back(std::move(entry));
		} else {
			if (entry.isDirectory || IsSupportedExtension(entry.name.c_str()))
				ctx->entries->push_back(std::move(entry));
		}

		return SDL_ENUM_CONTINUE;
	};

	SDL_EnumerateDirectory(dirU8.c_str(), callback, &ctx);
	std::sort(s_fileBrowserEntries.begin(), s_fileBrowserEntries.end());
	s_fileBrowserNeedsRefresh = false;
}

// -------------------------------------------------------------------------
// Init
// -------------------------------------------------------------------------

void ATMobileUI_Init() {
	// 1) Restore last-used browser dir from registry, if any.
	VDStringW saved = LoadFileBrowserDir();
	if (!saved.empty()) {
		s_fileBrowserDir = saved;
	} else {
#ifdef __ANDROID__
		// Prefer the public Downloads dir via Environment — this is the
		// same path users see when they drop files onto the phone via
		// ADB, Files app, or a file manager.  Chain through
		// SDL_GetUserFolder as an API-33+ fallback (SAF URIs), then
		// the app-private external dir, then config.
		const char *dl = ATAndroid_GetPublicDownloadsDir();
		if (dl && *dl) {
			s_fileBrowserDir = VDTextU8ToW(VDStringA(dl));
		} else {
			const char *dl2 = SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS);
			if (dl2 && *dl2) {
				s_fileBrowserDir = VDTextU8ToW(VDStringA(dl2));
			} else {
				const char *ext = SDL_GetAndroidExternalStoragePath();
				if (ext && *ext)
					s_fileBrowserDir = VDTextU8ToW(VDStringA(ext));
				else
					s_fileBrowserDir = VDTextU8ToW(ATGetConfigDir());
			}
		}
#else
		const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
		if (home)
			s_fileBrowserDir = VDTextU8ToW(VDStringA(home));
		else
			s_fileBrowserDir = L"/";
#endif
	}
	s_fileBrowserNeedsRefresh = true;
}

bool ATMobileUI_IsFirstRun() {
	return !IsFirstRunComplete();
}

// Load persisted mobile config into a state object.  Exposed for
// main_sdl3.cpp to call right after creating g_mobileState so the
// settings are in place before the first layout computation.
void ATMobileUI_LoadConfig(ATMobileUIState &mobileState) {
	LoadMobileConfig(mobileState);
}

// Save persisted mobile config.  Called from the Settings panel on
// change and on explicit shutdown.
void ATMobileUI_SaveConfig(const ATMobileUIState &mobileState) {
	SaveMobileConfig(mobileState);
}

// Push the three visual-effect toggles into the GTIA's
// ATArtifactingParams + scanlines flag.  Safe to call on any
// display backend — if the backend doesn't support GPU screen FX,
// the params are still stored but SyncScreenFXToBackend in
// main_sdl3.cpp skips the push (see main_sdl3.cpp:975).  Scanlines
// work in both the CPU and GL paths.
void ATMobileUI_ApplyVisualEffects(const ATMobileUIState &mobileState) {
	ATGTIAEmulator &gtia = g_sim.GetGTIA();

	// Scanlines toggle: GPU-accelerated in GL, CPU fallback otherwise.
	gtia.SetScanlinesEnabled(mobileState.fxScanlines);

	// Read current params, tweak the three fields we care about,
	// leave everything else at the user's/default values.
	ATArtifactingParams params = gtia.GetArtifactingParams();

	// Bloom: pushed hard enough that the effect is obvious on a
	// phone LCD.  The default-constructed params leave radius/
	// intensity at zero so mbEnableBloom alone does nothing visible.
	params.mbEnableBloom = mobileState.fxBloom;
	if (mobileState.fxBloom) {
		params.mBloomRadius            = 8.0f;
		params.mBloomDirectIntensity   = 1.00f;
		params.mBloomIndirectIntensity = 0.80f;
	} else {
		params.mBloomRadius            = 0.0f;
		params.mBloomDirectIntensity   = 0.0f;
		params.mBloomIndirectIntensity = 0.0f;
	}

	if (mobileState.fxDistortion) {
		// Noticeable barrel distortion — larger than the previous
		// "subtle" value so the curvature is actually visible on a
		// 6" phone screen without looking cartoonish.
		params.mDistortionViewAngleX = 85.0f;
		params.mDistortionYRatio     = 0.50f;
	} else {
		params.mDistortionViewAngleX = 0.0f;
		params.mDistortionYRatio     = 0.0f;
	}

	gtia.SetArtifactingParams(params);
}

// Apply a bundled performance preset.  Efficient turns everything
// off and picks the cheapest filter.  Balanced keeps effects off
// but uses a nicer filter.  Quality enables all three CRT effects.
// Custom (3) is a no-op so the user's manual tweaks stay put.
void ATMobileUI_ApplyPerformancePreset(ATMobileUIState &mobileState) {
	int p = mobileState.performancePreset;
	if (p < 0 || p >= 3) return;  // Custom or out of range

	ATDisplayFilterMode filter = kATDisplayFilterMode_Bilinear;
	bool fastBoot       = true;   // always on — near-free speed win
	bool interlace      = false;  // extra frame work; off except Quality
	bool nonlinearMix   = true;   // POKEY quality
	bool audioMonitor   = false;  // profiler overhead, off everywhere
	bool driveSounds    = false;  // extra audio mixing; off except Quality

	switch (p) {
	case 0: // Efficient
		mobileState.fxScanlines  = false;
		mobileState.fxBloom      = false;
		mobileState.fxDistortion = false;
		filter        = kATDisplayFilterMode_Point;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = false;   // cheaper linear mix
		driveSounds   = false;
		break;
	case 1: // Balanced
		mobileState.fxScanlines  = false;
		mobileState.fxBloom      = false;
		mobileState.fxDistortion = false;
		filter        = kATDisplayFilterMode_Bilinear;
		fastBoot      = true;
		interlace     = false;
		nonlinearMix  = true;
		driveSounds   = false;
		break;
	case 2: // Quality
		mobileState.fxScanlines  = true;
		mobileState.fxBloom      = true;
		mobileState.fxDistortion = true;
		filter        = kATDisplayFilterMode_SharpBilinear;
		fastBoot      = false;   // authentic boot timing
		interlace     = true;    // high-res interlace for games that use it
		nonlinearMix  = true;
		driveSounds   = true;
		break;
	}

	ATUISetDisplayFilterMode(filter);
	ATMobileUI_ApplyVisualEffects(mobileState);

	// Simulator-side knobs — match the Windows defaults where they
	// exist and fall back to the cheapest option elsewhere.
	g_sim.SetFastBootEnabled(fastBoot);
	g_sim.GetGTIA().SetInterlaceEnabled(interlace);
	g_sim.GetPokey().SetNonlinearMixingEnabled(nonlinearMix);
	g_sim.SetAudioMonitorEnabled(audioMonitor);
	ATUISetDriveSoundsEnabled(driveSounds);
}

// Force the file browser to re-enumerate next frame.  Used after
// returning from the Android Settings app so any newly-granted
// "All files access" permission is reflected immediately.
void ATMobileUI_InvalidateFileBrowser() {
	s_fileBrowserNeedsRefresh = true;
}

// -------------------------------------------------------------------------
// Suspend save-state
//
// Android can terminate a backgrounded app at any time.  To make the
// emulator feel like a native console handheld (flip open, keep
// playing), we snapshot the simulator to disk whenever the app goes
// to background or is about to terminate, and restore it on next
// launch.  Both halves of the feature are user-toggleable under the
// mobile Settings panel.
// -------------------------------------------------------------------------

void ATMobileUI_SaveSuspendState(ATSimulator &sim,
	const ATMobileUIState &mobileState)
{
	if (!mobileState.autoSaveOnSuspend)
		return;
	if (!mobileState.gameLoaded) {
		// Nothing worth saving — remove any stale file so a later
		// restore doesn't load a session from a different game.
		ATMobileUI_ClearSuspendState();
		return;
	}
	VDStringW path = QuickSaveStatePath();
	try {
		sim.SaveState(path.c_str());
	} catch (const MyError &e) {
		// Non-fatal — just log.  We don't want suspend to fail because
		// a save-state write had a disk error.
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] SaveState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
	} catch (...) {
		fprintf(stderr, "[mobile] SaveState threw unknown exception\n");
	}
}

bool ATMobileUI_RestoreSuspendState(ATSimulator &sim,
	ATMobileUIState &mobileState)
{
	if (!mobileState.autoRestoreOnStart)
		return false;
	VDStringW path = QuickSaveStatePath();
	if (!VDDoesPathExist(path.c_str()))
		return false;

	try {
		ATImageLoadContext ctx{};
		if (sim.Load(path.c_str(), kATMediaWriteMode_RO, &ctx)) {
			// Match Windows behaviour: a save-state load suppresses
			// the cold reset that Load() would otherwise perform.
			sim.Resume();
			mobileState.gameLoaded = true;
			return true;
		}
	} catch (const MyError &e) {
		VDStringA u8 = VDTextWToU8(path);
		fprintf(stderr, "[mobile] LoadState(%s) failed: %s\n",
			u8.c_str(), e.c_str());
		// Corrupt snapshot — remove it so we don't keep crashing.
		ATMobileUI_ClearSuspendState();
	} catch (...) {
		fprintf(stderr, "[mobile] LoadState threw unknown exception\n");
		ATMobileUI_ClearSuspendState();
	}
	return false;
}

void ATMobileUI_ClearSuspendState() {
	VDStringW path = QuickSaveStatePath();
	if (VDDoesPathExist(path.c_str()))
		VDRemoveFile(path.c_str());
}

// -------------------------------------------------------------------------
// Menu button polling
// -------------------------------------------------------------------------

extern bool s_menuTapped;

static bool ConsumeMenuTap() {
	if (s_menuTapped) {
		s_menuTapped = false;
		return true;
	}
	return false;
}

// -------------------------------------------------------------------------
// Hamburger menu
// -------------------------------------------------------------------------

static bool s_wasPausedBeforeMenu = false;

void ATMobileUI_OpenMenu(ATSimulator &sim, ATMobileUIState &mobileState) {
	s_wasPausedBeforeMenu = sim.IsPaused();
	sim.Pause();
	ATTouchControls_ReleaseAll();
	mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
}

void ATMobileUI_CloseMenu(ATSimulator &sim, ATMobileUIState &mobileState) {
	mobileState.currentScreen = ATMobileUIScreen::None;
	if (!s_wasPausedBeforeMenu)
		sim.Resume();
}


// -------------------------------------------------------------------------
// File browser
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Settings
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Mobile Disk Drive Manager — full-screen touch-first replacement for
// the desktop ATUIRenderDiskManager dialog.  Large 96dp rows per
// drive with clear Mount/Eject buttons, shows D1:-D4: by default and
// reveals D5:-D15: behind a disclosure.
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Mobile About panel — full-screen replacement for the desktop
// `About AltirraSDL` dialog (which is sized 420×220 px and looks
// tiny on a phone).  Centered title/subtitle, scrollable credits,
// and a big Close button that returns to the hamburger menu.
// -------------------------------------------------------------------------


// -------------------------------------------------------------------------
// Main render entry point
// -------------------------------------------------------------------------

void ATMobileUI_Render(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	// Cache content scale for dp() helper
	s_contentScale = mobileState.layoutConfig.contentScale;

	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	// Query Android safe-area insets (status bar, nav bar, cutout).
	// Zero on desktop.  Cached — only re-queried when the window was
	// resized (insets cache is invalidated from main_sdl3.cpp).
#ifdef __ANDROID__
	ATSafeInsets androidInsets = ATAndroid_GetSafeInsets();
	ATTouchLayoutInsets insets;
	insets.top    = androidInsets.top;
	insets.bottom = androidInsets.bottom;
	insets.left   = androidInsets.left;
	insets.right  = androidInsets.right;
#else
	ATTouchLayoutInsets insets;
#endif

	// Update layout if screen size, config, or insets changed
	if (w != mobileState.layout.screenW || h != mobileState.layout.screenH
		|| mobileState.layoutConfig.controlSize != mobileState.layout.lastControlSize
		|| mobileState.layoutConfig.contentScale != mobileState.layout.lastContentScale
		|| insets.top    != mobileState.layout.lastInsets.top
		|| insets.bottom != mobileState.layout.lastInsets.bottom
		|| insets.left   != mobileState.layout.lastInsets.left
		|| insets.right  != mobileState.layout.lastInsets.right)
	{
		ATTouchLayout_Update(mobileState.layout, w, h, mobileState.layoutConfig, insets);
	}

	// First run: show welcome wizard on top of everything until the user
	// picks ROMs or skips.  The wizard sets the flag itself.
	if (mobileState.currentScreen == ATMobileUIScreen::None
		&& !IsFirstRunComplete())
	{
		mobileState.currentScreen = ATMobileUIScreen::FirstRunWizard;
	}

	// Check for menu button tap
	if (ConsumeMenuTap() && mobileState.currentScreen == ATMobileUIScreen::None)
		ATMobileUI_OpenMenu(sim, mobileState);

	switch (mobileState.currentScreen) {
	case ATMobileUIScreen::None:
		// Render touch controls overlay
		ATTouchControls_Render(mobileState.layout, mobileState.layoutConfig);

		// If no game loaded, show a styled centered "Load Game" button
		if (!mobileState.gameLoaded)
			RenderLoadGamePrompt(sim, uiState, mobileState);
		break;

	case ATMobileUIScreen::HamburgerMenu:
		RenderHamburgerMenu(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::FileBrowser:
		RenderFileBrowser(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::Settings:
		RenderSettings(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::FirstRunWizard:
		RenderFirstRunWizard(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::About:
		RenderMobileAbout(sim, uiState, mobileState, window);
		break;

	case ATMobileUIScreen::DiskManager:
		RenderMobileDiskManager(sim, uiState, mobileState, window);
		break;
	}

	// Global mobile dialog sheet — serves both info popups
	// (ShowInfoModal, single OK button) and confirmation popups
	// (ShowConfirmDialog, Cancel + Confirm buttons).  Card-style
	// sheet sized to the phone display, centered in the safe area.
	const bool haveInfo    = s_infoModalOpen;
	const bool haveConfirm = s_confirmActive;
	if (haveInfo || haveConfirm) {
		// Full-screen dim backdrop.  Use the BACKGROUND draw list so
		// the rectangle renders *beneath* every ImGui window this
		// frame — otherwise the foreground list paints over the sheet
		// card and visibly darkens it.
		ImGui::GetBackgroundDrawList()->AddRectFilled(
			ImVec2(0, 0), ImGui::GetIO().DisplaySize,
			IM_COL32(0, 0, 0, 160));

		float insetL = (float)mobileState.layout.insets.left;
		float insetR = (float)mobileState.layout.insets.right;
		float insetT = (float)mobileState.layout.insets.top;
		float insetB = (float)mobileState.layout.insets.bottom;
		float availW = ImGui::GetIO().DisplaySize.x - insetL - insetR - dp(32.0f);
		float sheetW = availW < dp(520.0f) ? availW : dp(520.0f);
		if (sheetW < dp(260.0f)) sheetW = dp(260.0f);
		float sheetH = dp(260.0f);
		float sheetX = (ImGui::GetIO().DisplaySize.x - sheetW) * 0.5f;
		float areaTop = insetT;
		float areaH = ImGui::GetIO().DisplaySize.y - insetT - insetB;
		float sheetY = areaTop + (areaH - sheetH) * 0.5f;
		if (sheetY < insetT + dp(16.0f)) sheetY = insetT + dp(16.0f);

		ImGui::SetNextWindowPos(ImVec2(sheetX, sheetY));
		ImGui::SetNextWindowSize(ImVec2(sheetW, 0));

		ImGuiStyle &style = ImGui::GetStyle();
		float prevR = style.WindowRounding;
		float prevB = style.WindowBorderSize;
		style.WindowRounding = dp(14.0f);
		style.WindowBorderSize = dp(1.0f);
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.12f, 0.18f, 0.98f));
		ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.27f, 0.51f, 0.82f, 1.0f));

		const char *winId = haveConfirm ? "##MobileConfirm" : "##MobileInfo";
		ImGui::Begin(winId, nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_AlwaysAutoResize);

		const char *title = haveConfirm
			? s_confirmTitle.c_str() : s_infoModalTitle.c_str();
		const char *body  = haveConfirm
			? s_confirmBody.c_str()  : s_infoModalBody.c_str();

		ImGui::Dummy(ImVec2(0, dp(8.0f)));
		if (title && *title) {
			ImGui::SetWindowFontScale(1.25f);
			ImGui::PushStyleColor(ImGuiCol_Text,
				ImVec4(0.40f, 0.70f, 1.00f, 1.0f));
			ImGui::TextUnformatted(title);
			ImGui::PopStyleColor();
			ImGui::SetWindowFontScale(1.0f);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
		}
		ImGui::PushTextWrapPos(sheetW - dp(24.0f));
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.92f, 0.96f, 1));
		ImGui::TextUnformatted(body);
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
		ImGui::Dummy(ImVec2(0, dp(16.0f)));
		ImGui::Separator();
		ImGui::Dummy(ImVec2(0, dp(8.0f)));

		float btnH = dp(56.0f);
		float rowW = ImGui::GetContentRegionAvail().x;
		if (haveConfirm) {
			float gap = dp(12.0f);
			float halfW = (rowW - gap) * 0.5f;

			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.30f, 0.32f, 0.38f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.38f, 0.40f, 0.48f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.22f, 0.24f, 0.30f, 1));
			if (ImGui::Button("Cancel", ImVec2(halfW, btnH))) {
				s_confirmActive = false;
				s_confirmAction = nullptr;
			}
			ImGui::PopStyleColor(3);

			ImGui::SameLine(0.0f, gap);

			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.25f, 0.55f, 0.90f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.30f, 0.62f, 0.95f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.20f, 0.48f, 0.85f, 1));
			if (ImGui::Button("Confirm", ImVec2(halfW, btnH))) {
				auto act = s_confirmAction;
				s_confirmActive = false;
				s_confirmAction = nullptr;
				if (act) act();
			}
			ImGui::PopStyleColor(3);
		} else {
			ImGui::PushStyleColor(ImGuiCol_Button,
				ImVec4(0.25f, 0.55f, 0.90f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
				ImVec4(0.30f, 0.62f, 0.95f, 1));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive,
				ImVec4(0.20f, 0.48f, 0.85f, 1));
			if (ImGui::Button("OK", ImVec2(-1, btnH))) {
				s_infoModalOpen = false;
			}
			ImGui::PopStyleColor(3);
		}

		ImGui::End();
		ImGui::PopStyleColor(2);
		style.WindowRounding = prevR;
		style.WindowBorderSize = prevB;
	}
}

// -------------------------------------------------------------------------
// Event handling
// -------------------------------------------------------------------------

bool ATMobileUI_HandleEvent(const SDL_Event &ev, ATMobileUIState &mobileState) {
	// If a menu/dialog is open, let ImGui handle everything
	if (mobileState.currentScreen != ATMobileUIScreen::None)
		return false;

	// Route touch events to touch controls
	if (ev.type == SDL_EVENT_FINGER_DOWN ||
		ev.type == SDL_EVENT_FINGER_MOTION ||
		ev.type == SDL_EVENT_FINGER_UP)
	{
		return ATTouchControls_HandleEvent(ev, mobileState.layout, mobileState.layoutConfig);
	}

	return false;
}

void ATMobileUI_OpenFileBrowser(ATMobileUIState &mobileState) {
	s_romFolderMode = false;
	mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
	s_fileBrowserNeedsRefresh = true;
#ifdef __ANDROID__
	// Lazy permission request — only the first time the user actually
	// needs storage access.
	if (!IsPermissionAsked()) {
		ATAndroid_RequestStoragePermission();
		SetPermissionAsked();
	}
#endif
}

void ATMobileUI_OpenSettings(ATMobileUIState &mobileState) {
	mobileState.currentScreen = ATMobileUIScreen::Settings;
}
