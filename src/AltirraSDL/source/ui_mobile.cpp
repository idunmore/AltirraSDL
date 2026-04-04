//	AltirraSDL - Mobile UI (hamburger menu, settings, file browser)
//	Touch-first UI for Android phones and tablets.
//	Provides hamburger slide-in menu, streamlined settings, and file browser.

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
#include <at/atcore/media.h>

#include "ui_mobile.h"
#include "ui_main.h"
#include "touch_controls.h"
#include "simulator.h"
#include "gtia.h"
#include "mediamanager.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "constants.h"

extern ATSimulator g_sim;

// -------------------------------------------------------------------------
// File browser state
// -------------------------------------------------------------------------

struct FileBrowserEntry {
	VDStringW name;
	VDStringW fullPath;
	bool isDirectory;
	// For sorting: directories first, then alphabetical
	bool operator<(const FileBrowserEntry &o) const {
		if (isDirectory != o.isDirectory) return isDirectory > o.isDirectory;
		return name < o.name;
	}
};

static std::vector<FileBrowserEntry> s_fileBrowserEntries;
static VDStringW s_fileBrowserDir;
static bool s_fileBrowserNeedsRefresh = true;

// Supported file extensions for Atari images
static bool IsSupportedExtension(const wchar_t *name) {
	const wchar_t *ext = wcsrchr(name, L'.');
	if (!ext) return false;
	ext++;

	// Convert to lowercase for comparison
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

static void RefreshFileBrowser(const VDStringW &dir) {
	s_fileBrowserEntries.clear();
	s_fileBrowserDir = dir;

	VDStringW pattern = dir;
	if (!pattern.empty() && pattern.back() != L'/')
		pattern += L'/';
	pattern += L'*';

	VDStringA dirU8 = VDTextWToU8(VDStringW(dir));

	// Use SDL3 directory enumeration
	SDL_EnumerationResult enumCb(void *, const char *, const char *);

	struct EnumCtx {
		VDStringW baseDir;
		std::vector<FileBrowserEntry> *entries;
	};

	EnumCtx ctx;
	ctx.baseDir = dir;
	if (!ctx.baseDir.empty() && ctx.baseDir.back() != L'/')
		ctx.baseDir += L'/';
	ctx.entries = &s_fileBrowserEntries;

	auto callback = [](void *userdata, const char *dirname, const char *fname) -> SDL_EnumerationResult {
		EnumCtx *ctx = (EnumCtx *)userdata;

		// Skip hidden files
		if (fname[0] == '.')
			return SDL_ENUM_CONTINUE;

		VDStringW wname = VDTextU8ToW(VDStringA(fname));
		VDStringW fullPath = ctx->baseDir + wname;
		VDStringA fullPathU8 = VDTextWToU8(fullPath);

		FileBrowserEntry entry;
		entry.name = std::move(wname);
		entry.fullPath = std::move(fullPath);

		// Check if directory using SDL3
		SDL_PathInfo info;
		if (SDL_GetPathInfo(fullPathU8.c_str(), &info)) {
			entry.isDirectory = (info.type == SDL_PATHTYPE_DIRECTORY);
		} else {
			entry.isDirectory = false;
		}

		// Only show directories and supported file types
		if (entry.isDirectory || IsSupportedExtension(entry.name.c_str()))
			ctx->entries->push_back(std::move(entry));

		return SDL_ENUM_CONTINUE;
	};

	SDL_EnumerateDirectory(dirU8.c_str(), callback, &ctx);

	// Sort: directories first, then alphabetical
	std::sort(s_fileBrowserEntries.begin(), s_fileBrowserEntries.end());

	s_fileBrowserNeedsRefresh = false;
}

// -------------------------------------------------------------------------
// Init
// -------------------------------------------------------------------------

void ATMobileUI_Init() {
	// Set default file browser directory
#ifdef __ANDROID__
	s_fileBrowserDir = VDTextU8ToW(VDStringA(SDL_GetUserFolder(SDL_FOLDER_DOWNLOADS)));
#else
	const char *home = SDL_GetUserFolder(SDL_FOLDER_HOME);
	if (home)
		s_fileBrowserDir = VDTextU8ToW(VDStringA(home));
	else
		s_fileBrowserDir = L"/";
#endif
	s_fileBrowserNeedsRefresh = true;
}

bool ATMobileUI_IsFirstRun() {
	// Check if any firmware is configured
	// For now, simple heuristic: check if the built-in kernel is the only option
	// This will be refined when firmware manager integration is complete
	return false;
}

// -------------------------------------------------------------------------
// Menu button polling (called from event handler)
// -------------------------------------------------------------------------

extern bool s_menuTapped;  // Defined in touch_controls.cpp

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

static void RenderHamburgerMenu(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();
	float menuW = io.DisplaySize.x * 0.60f;
	if (menuW < 280.0f) menuW = 280.0f;
	if (menuW > 400.0f) menuW = 400.0f;

	// Dim background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize,
		IM_COL32(0, 0, 0, 120));

	// Menu panel (slides from right)
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - menuW, 0));
	ImGui::SetNextWindowSize(ImVec2(menuW, io.DisplaySize.y));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##MobileMenu", nullptr, flags)) {
		ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); // Default font

		// Title
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8.0f);
		ImGui::Text("Altirra");
		ImGui::Separator();
		ImGui::Spacing();

		float btnH = 48.0f;
		ImVec2 btnSize(-1, btnH);

		// Resume / Pause
		if (ImGui::Button(s_wasPausedBeforeMenu ? "Resume" : "Resume", btnSize)) {
			ATMobileUI_CloseMenu(sim, mobileState);
		}
		ImGui::Spacing();

		// Load Game
		if (ImGui::Button("Load Game", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			s_fileBrowserNeedsRefresh = true;
		}
		ImGui::Spacing();

		// Disk Drives
		if (ImGui::Button("Disk Drives", btnSize)) {
			ATMobileUI_CloseMenu(sim, mobileState);
			uiState.showDiskManager = true;
		}
		ImGui::Spacing();

		// Audio toggle
		{
			const char *audioLabel = mobileState.audioMuted ? "Audio: OFF" : "Audio: ON";
			if (ImGui::Button(audioLabel, btnSize)) {
				mobileState.audioMuted = !mobileState.audioMuted;
				// TODO: Wire to actual audio mute via ATSimulator or SDL audio
			}
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Warm Reset
		if (ImGui::Button("Warm Reset", btnSize)) {
			sim.WarmReset();
			ATMobileUI_CloseMenu(sim, mobileState);
			sim.Resume();
		}
		ImGui::Spacing();

		// Cold Reset
		if (ImGui::Button("Cold Reset", btnSize)) {
			sim.ColdReset();
			ATMobileUI_CloseMenu(sim, mobileState);
			sim.Resume();
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Virtual Keyboard (placeholder)
		if (ImGui::Button("Virtual Keyboard", btnSize)) {
			// TODO: Phase 2 — show Atari keyboard overlay
		}
		ImGui::Spacing();

		// Settings
		if (ImGui::Button("Settings", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::Settings;
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// About
		if (ImGui::Button("About", btnSize)) {
			ATMobileUI_CloseMenu(sim, mobileState);
			uiState.showAboutDialog = true;
		}

		ImGui::PopFont();
	}
	ImGui::End();

	// Tap outside menu to close
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		ImVec2 mousePos = ImGui::GetMousePos();
		if (mousePos.x < io.DisplaySize.x - menuW) {
			ATMobileUI_CloseMenu(sim, mobileState);
		}
	}
}

// -------------------------------------------------------------------------
// File browser
// -------------------------------------------------------------------------

static void RenderFileBrowser(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	if (s_fileBrowserNeedsRefresh)
		RefreshFileBrowser(s_fileBrowserDir);

	ImGuiIO &io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##FileBrowser", nullptr, flags)) {
		// Header
		if (ImGui::Button("< Back")) {
			ATMobileUI_CloseMenu(sim, mobileState);
		}
		ImGui::SameLine();
		ImGui::Text("Load Game");

		ImGui::Separator();

		// Current directory display
		VDStringA dirU8 = VDTextWToU8(s_fileBrowserDir);
		ImGui::TextWrapped("Directory: %s", dirU8.c_str());

		// Go up button
		if (ImGui::Button(".. (Parent Directory)")) {
			VDStringW parent = s_fileBrowserDir;
			// Strip trailing slash
			while (!parent.empty() && parent.back() == L'/')
				parent.pop_back();
			// Find last separator by scanning backwards
			size_t lastSlash = 0;
			bool found = false;
			for (size_t i = parent.size(); i > 0; --i) {
				if (parent[i - 1] == L'/') {
					lastSlash = i - 1;
					found = true;
					break;
				}
			}
			if (found && lastSlash > 0) {
				parent.resize(lastSlash);
				s_fileBrowserDir = parent;
				s_fileBrowserNeedsRefresh = true;
			}
		}

		ImGui::Separator();

		// File list
		float itemH = 44.0f;
		ImGui::BeginChild("FileList", ImVec2(0, 0), ImGuiChildFlags_None);

		for (size_t i = 0; i < s_fileBrowserEntries.size(); i++) {
			const FileBrowserEntry &entry = s_fileBrowserEntries[i];
			VDStringA nameU8 = VDTextWToU8(VDStringW(entry.name));

			char label[512];
			if (entry.isDirectory)
				snprintf(label, sizeof(label), "[DIR] %s", nameU8.c_str());
			else
				snprintf(label, sizeof(label), "      %s", nameU8.c_str());

			ImGui::PushID((int)i);
			if (ImGui::Selectable(label, (int)i == mobileState.selectedFileIdx,
				0, ImVec2(0, itemH)))
			{
				if (entry.isDirectory) {
					s_fileBrowserDir = entry.fullPath;
					s_fileBrowserNeedsRefresh = true;
					mobileState.selectedFileIdx = -1;
				} else {
					mobileState.selectedFileIdx = (int)i;
					// Boot the image
					VDStringA pathU8 = VDTextWToU8(VDStringW(entry.fullPath));
					ATUIPushDeferred(kATDeferred_BootImage, pathU8.c_str());
					mobileState.gameLoaded = true;
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				}
			}
			ImGui::PopID();
		}

		ImGui::EndChild();
	}
	ImGui::End();
}

// -------------------------------------------------------------------------
// Settings
// -------------------------------------------------------------------------

static void RenderSettings(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##MobileSettings", nullptr, flags)) {
		if (ImGui::Button("< Back")) {
			mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
		}
		ImGui::SameLine();
		ImGui::Text("Settings");

		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), ImGuiChildFlags_None);

		// ---- SYSTEM ----
		ImGui::SeparatorText("System");

		// Video Standard
		{
			int current = (sim.GetVideoStandard() == kATVideoStandard_PAL) ? 1 : 0;
			const char *items[] = { "NTSC", "PAL" };
			if (ImGui::Combo("Video Standard", &current, items, 2)) {
				sim.SetVideoStandard(current ? kATVideoStandard_PAL : kATVideoStandard_NTSC);
			}
		}

		// Memory Size
		{
			static const struct {
				const char *label;
				ATMemoryMode mode;
			} kMemModes[] = {
				{ "16K", kATMemoryMode_16K },
				{ "48K", kATMemoryMode_48K },
				{ "64K", kATMemoryMode_64K },
				{ "128K", kATMemoryMode_128K },
				{ "320K (Compy Shop)", kATMemoryMode_320K },
				{ "1088K", kATMemoryMode_1088K },
			};

			ATMemoryMode curMode = sim.GetMemoryMode();
			int curIdx = 2; // default 64K
			for (int i = 0; i < (int)(sizeof(kMemModes)/sizeof(kMemModes[0])); i++) {
				if (kMemModes[i].mode == curMode) { curIdx = i; break; }
			}

			const char *labels[16];
			int count = (int)(sizeof(kMemModes)/sizeof(kMemModes[0]));
			for (int i = 0; i < count; i++) labels[i] = kMemModes[i].label;

			if (ImGui::Combo("Memory Size", &curIdx, labels, count)) {
				sim.SetMemoryMode(kMemModes[curIdx].mode);
			}
		}

		// BASIC toggle
		{
			bool basicEnabled = sim.IsBASICEnabled();
			if (ImGui::Checkbox("BASIC Enabled", &basicEnabled))
				sim.SetBASICEnabled(basicEnabled);
		}

		ImGui::Spacing();

		// ---- CONTROLS ----
		ImGui::SeparatorText("Controls");

		// Control size
		{
			int sz = (int)mobileState.layoutConfig.controlSize;
			const char *sizes[] = { "Small", "Medium", "Large" };
			if (ImGui::Combo("Control Size", &sz, sizes, 3))
				mobileState.layoutConfig.controlSize = (ATTouchControlSize)sz;
		}

		// Control opacity
		ImGui::SliderFloat("Opacity", &mobileState.layoutConfig.controlOpacity, 0.1f, 1.0f, "%.0f%%");

		// Haptic feedback
		ImGui::Checkbox("Haptic Feedback", &mobileState.layoutConfig.hapticEnabled);

		ImGui::Spacing();

		// ---- DISPLAY ----
		ImGui::SeparatorText("Display");

		// Filter mode
		{
			ATDisplayFilterMode curFM = ATUIGetDisplayFilterMode();
			int idx = 0;
			switch (curFM) {
			case kATDisplayFilterMode_Point:        idx = 0; break;
			case kATDisplayFilterMode_Bilinear:     idx = 1; break;
			case kATDisplayFilterMode_SharpBilinear:idx = 2; break;
			default: idx = 1; break;
			}
			const char *filters[] = { "Sharp (Nearest)", "Bilinear", "Sharp Bilinear" };
			if (ImGui::Combo("Filter Mode", &idx, filters, 3)) {
				static const ATDisplayFilterMode kModes[] = {
					kATDisplayFilterMode_Point,
					kATDisplayFilterMode_Bilinear,
					kATDisplayFilterMode_SharpBilinear,
				};
				ATUISetDisplayFilterMode(kModes[idx]);
			}
		}

		ImGui::Spacing();

		// ---- FIRMWARE ----
		ImGui::SeparatorText("Firmware");

		{
			VDStringA dirU8 = VDTextWToU8(s_fileBrowserDir);
			ImGui::Text("ROM Directory: %s", dirU8.c_str());
			if (ImGui::Button("Change ROM Directory")) {
				// Open file browser in directory-selection mode
				// For now, reuses the standard file browser
			}
		}

		ImGui::EndChild();
	}
	ImGui::End();
}

// -------------------------------------------------------------------------
// Main render entry point
// -------------------------------------------------------------------------

void ATMobileUI_Render(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	int w, h;
	SDL_GetWindowSize(window, &w, &h);

	// Update layout if screen size changed
	if (w != mobileState.layout.screenW || h != mobileState.layout.screenH)
		ATTouchLayout_Update(mobileState.layout, w, h, mobileState.layoutConfig);

	// Check for menu button tap
	if (ConsumeMenuTap() && mobileState.currentScreen == ATMobileUIScreen::None) {
		ATMobileUI_OpenMenu(sim, mobileState);
	}

	switch (mobileState.currentScreen) {
	case ATMobileUIScreen::None:
		// Render touch controls overlay
		ATTouchControls_Render(mobileState.layout, mobileState.layoutConfig);

		// If no game loaded, show centered "Load Game" button
		if (!mobileState.gameLoaded) {
			ImGuiIO &io = ImGui::GetIO();
			ImVec2 center = io.DisplaySize;
			center.x *= 0.5f;
			center.y *= 0.5f;
			ImVec2 btnSize(200, 60);
			ImGui::SetNextWindowPos(center, 0, ImVec2(0.5f, 0.5f));
			ImGui::SetNextWindowSize(ImVec2(btnSize.x + 40, btnSize.y + 40));
			ImGui::Begin("##LoadPrompt", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
				| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
				| ImGuiWindowFlags_NoBackground);
			if (ImGui::Button("Load Game", btnSize)) {
				ATMobileUI_OpenMenu(sim, mobileState);
				mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
				s_fileBrowserNeedsRefresh = true;
			}
			ImGui::End();
		}
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
		// TODO: Phase 2 — first-boot firmware wizard
		break;
	}
}

// -------------------------------------------------------------------------
// Event handling
// -------------------------------------------------------------------------

bool ATMobileUI_HandleEvent(const SDL_Event &ev, ATMobileUIState &mobileState) {
	// If a menu/dialog is open, let ImGui handle everything
	if (mobileState.currentScreen != ATMobileUIScreen::None)
		return false;  // ImGui handles it via normal processing

	// Route touch events to touch controls
	if (ev.type == SDL_EVENT_FINGER_DOWN ||
		ev.type == SDL_EVENT_FINGER_MOTION ||
		ev.type == SDL_EVENT_FINGER_UP)
	{
		return ATTouchControls_HandleEvent(ev, mobileState.layout);
	}

	return false;
}

void ATMobileUI_OpenFileBrowser(ATMobileUIState &mobileState) {
	mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
	s_fileBrowserNeedsRefresh = true;
}

void ATMobileUI_OpenSettings(ATMobileUIState &mobileState) {
	mobileState.currentScreen = ATMobileUIScreen::Settings;
}
