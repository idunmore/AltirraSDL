//	AltirraSDL - Mobile UI (hamburger menu, settings, file browser)
//	Streamlined touch-first UI for Android phones and tablets.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include "touch_layout.h"

struct ATUIState;
class ATSimulator;
struct SDL_Window;

enum class ATMobileUIScreen {
	None,           // Normal emulation (controls visible)
	HamburgerMenu,  // Slide-in menu panel
	FileBrowser,    // Full-screen file browser
	Settings,       // Full-screen settings panel
	FirstRunWizard  // First-boot firmware setup
};

struct ATMobileUIState {
	ATMobileUIScreen currentScreen = ATMobileUIScreen::None;

	// Touch layout
	ATTouchLayout layout;
	ATTouchLayoutConfig layoutConfig;

	// File browser state
	VDStringW currentDir;
	int selectedFileIdx = -1;

	// Settings modified flag
	bool settingsDirty = false;

	// Audio muted state (independent of emulator volume)
	bool audioMuted = false;

	// Whether a game is loaded (affects idle screen)
	bool gameLoaded = false;

	// Top bar auto-hide timer (seconds of inactivity)
	float topBarTimer = 0.0f;
	bool topBarVisible = true;
};

// Initialize mobile UI (call once at startup, after ImGui init)
void ATMobileUI_Init();

// Check if this is a first run (no firmware configured)
bool ATMobileUI_IsFirstRun();

// Main render entry point — call from ATUIRenderFrame() when ALTIRRA_MOBILE
void ATMobileUI_Render(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window);

// Process SDL events for mobile UI. Returns true if consumed.
bool ATMobileUI_HandleEvent(const SDL_Event &ev, ATMobileUIState &mobileState);

// Open the hamburger menu (pauses emulation)
void ATMobileUI_OpenMenu(ATSimulator &sim, ATMobileUIState &mobileState);

// Close the hamburger menu (resumes emulation if not explicitly paused)
void ATMobileUI_CloseMenu(ATSimulator &sim, ATMobileUIState &mobileState);

// Open file browser
void ATMobileUI_OpenFileBrowser(ATMobileUIState &mobileState);

// Open settings
void ATMobileUI_OpenSettings(ATMobileUIState &mobileState);
