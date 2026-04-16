//	AltirraSDL - Mobile UI: gamepad navigation
//	See mobile_gamepad.h for the contract.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include "ui_mobile.h"
#include "simulator.h"
#include "mobile_gamepad.h"
#include "mobile_internal.h"

namespace {
	bool s_inited      = false;
	bool s_uiOwning    = false;
}

void ATMobileGamepad_Init() {
	if (s_inited)
		return;
	s_inited = true;

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// NavHighlight colour is now rebound from ATUIApplyTheme() so a
	// runtime theme switch updates the focus ring colour.  Only the
	// config-flag wiring happens here.
}

void ATMobileGamepad_SetUIOwning(bool owning) {
	s_uiOwning = owning;
}

bool ATMobileGamepad_IsUIOwning() {
	return s_uiOwning;
}

bool ATMobileGamepad_IsReservedButton(int sdlGamepadButton) {
	return sdlGamepadButton == SDL_GAMEPAD_BUTTON_START
	    || sdlGamepadButton == SDL_GAMEPAD_BUTTON_BACK;
}

bool ATMobileGamepad_HandleEvent(const SDL_Event &ev,
	ATSimulator &sim, ATMobileUIState &mobileState)
{
	if (ev.type != SDL_EVENT_GAMEPAD_BUTTON_DOWN)
		return false;

	const int btn = ev.gbutton.button;

	if (btn == SDL_GAMEPAD_BUTTON_START) {
		// Start: cold-boot if no game has been loaded yet (gives
		// the user a way to (re)launch the most-recent image
		// without touching the screen); otherwise toggle pause.
		// Matches the behaviour of the on-screen Start button.
		if (!mobileState.gameLoaded) {
			sim.ColdReset();
			sim.Resume();
		} else if (sim.IsPaused()) {
			sim.Resume();
		} else {
			sim.Pause();
		}
		return true;
	}

	if (btn == SDL_GAMEPAD_BUTTON_BACK) {
		if (mobileState.currentScreen == ATMobileUIScreen::None) {
			if (mobileState.gameLoaded) {
				ATMobileUI_OpenMenu(sim, mobileState);
				s_uiOwning = true;
			} else {
				mobileState.currentScreen = ATMobileUIScreen::GameBrowser;
				s_uiOwning = true;
			}
		} else if (mobileState.currentScreen == ATMobileUIScreen::HamburgerMenu) {
			ATMobileUI_CloseMenu(sim, mobileState);
			s_uiOwning = false;
		} else if (mobileState.currentScreen == ATMobileUIScreen::GameBrowser) {
			if (mobileState.gameLoaded) {
				mobileState.currentScreen = ATMobileUIScreen::None;
				sim.Resume();
				s_uiOwning = false;
			}
		} else {
			ATMobileUI_OpenMenu(sim, mobileState);
		}
		return true;
	}

	return false;
}
