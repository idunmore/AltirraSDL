//	AltirraSDL - Mobile UI (split from ui_mobile.cpp Phase 3b)
//	Verbatim move; helpers/state shared via mobile_internal.h.

#include <stdafx.h>
#include <cwctype>
#include <vector>
#include <algorithm>
#include <functional>
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

void RenderHamburgerMenu(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();
	float menuW = io.DisplaySize.x * 0.65f;
	float minW = dp(280.0f);
	float maxW = dp(400.0f);
	if (menuW < minW) menuW = minW;
	if (menuW > maxW) menuW = maxW;

	// Dim background
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0), io.DisplaySize,
		IM_COL32(0, 0, 0, 128));

	// Menu panel (slides from right), inset inside safe area so the
	// title bar isn't eaten by the status bar and the last item isn't
	// hidden by the nav bar.
	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - insetR - menuW, insetT));
	ImGui::SetNextWindowSize(ImVec2(menuW, io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##MobileMenu", nullptr, flags)) {
		// Install touch-drag scrolling for the hamburger panel so a
		// short phone or landscape orientation can still reach all
		// the menu items.
		ATTouchDragScroll();

		// Title bar with close button
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + dp(8.0f));
		ImGui::Text("Altirra");
		ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - dp(32.0f));
		if (ImGui::Button("X", ImVec2(dp(32.0f), dp(32.0f))))
			ATMobileUI_CloseMenu(sim, mobileState);

		ImGui::Separator();
		ImGui::Spacing();

		// Menu button height scaled for touch
		float btnH = dp(56.0f);
		ImVec2 btnSize(-1, btnH);

		// Resume
		if (ImGui::Button("Resume", btnSize))
			ATMobileUI_CloseMenu(sim, mobileState);
		ImGui::Spacing();

		// Load Game
		if (ImGui::Button("Load Game", btnSize)) {
			s_romFolderMode = false;
			mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			s_fileBrowserNeedsRefresh = true;
		}
		ImGui::Spacing();

		// Disk Drives — mobile-friendly full-screen manager
		if (ImGui::Button("Disk Drives", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::DiskManager;
		}
		ImGui::Spacing();

		// Audio toggle
		{
			const char *audioLabel = mobileState.audioMuted ? "Audio: OFF" : "Audio: ON";
			if (ImGui::Button(audioLabel, btnSize)) {
				mobileState.audioMuted = !mobileState.audioMuted;
				IATAudioOutput *audioOut = g_sim.GetAudioOutput();
				if (audioOut)
					audioOut->SetMute(mobileState.audioMuted);
			}
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Quick Save State — with confirmation to prevent accidental
		// overwrite of an earlier checkpoint.
		if (ImGui::Button("Quick Save State", btnSize)) {
			ShowConfirmDialog("Quick Save State",
				"Overwrite the current quick save with the "
				"emulator's state right now?",
				[&mobileState]() {
					try {
						VDStringW path = QuickSaveStatePath();
						g_sim.SaveState(path.c_str());
						ShowInfoModal("Saved",
							"Emulator state saved.");
					} catch (const MyError &e) {
						ShowInfoModal("Save Failed", e.c_str());
					}
				});
		}
		ImGui::Spacing();

		// Quick Load State — confirmation, with a distinct info
		// dialog if no save is available.
		if (ImGui::Button("Quick Load State", btnSize)) {
			VDStringW path = QuickSaveStatePath();
			if (!VDDoesPathExist(path.c_str())) {
				ShowInfoModal("No Quick Save",
					"There is no quick save available to load.");
			} else {
				ShowConfirmDialog("Quick Load State",
					"Replace the current emulator state with the "
					"quick save?  Any unsaved progress will be lost.",
					[&sim, &mobileState]() {
						VDStringW p = QuickSaveStatePath();
						try {
							ATImageLoadContext ctx{};
							if (sim.Load(p.c_str(),
								kATMediaWriteMode_RO, &ctx))
							{
								sim.Resume();
								mobileState.gameLoaded = true;
								ShowInfoModal("Loaded",
									"Emulator state restored.");
							}
						} catch (const MyError &e) {
							ShowInfoModal("Load Failed", e.c_str());
						}
					});
			}
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Warm Reset — with confirmation.
		if (ImGui::Button("Warm Reset", btnSize)) {
			ShowConfirmDialog("Warm Reset",
				"Reset the emulator without clearing memory?",
				[&sim, &mobileState]() {
					sim.WarmReset();
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				});
		}
		ImGui::Spacing();

		// Cold Reset — with confirmation.
		if (ImGui::Button("Cold Reset", btnSize)) {
			ShowConfirmDialog("Cold Reset",
				"Power-cycle the emulator?  This clears RAM and "
				"reboots, just like unplugging the machine.",
				[&sim, &mobileState]() {
					sim.ColdReset();
					ATMobileUI_CloseMenu(sim, mobileState);
					sim.Resume();
				});
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// Settings
		if (ImGui::Button("Settings", btnSize)) {
			s_settingsPage = ATMobileSettingsPage::Home;
			mobileState.currentScreen = ATMobileUIScreen::Settings;
		}
		ImGui::Spacing();

		ImGui::Separator();
		ImGui::Spacing();

		// About — mobile-friendly full-screen panel
		if (ImGui::Button("About", btnSize)) {
			mobileState.currentScreen = ATMobileUIScreen::About;
		}
	}
	ImGui::End();

	// Tap outside menu panel to close
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		ImVec2 mousePos = ImGui::GetMousePos();
		if (mousePos.x < io.DisplaySize.x - insetR - menuW)
			ATMobileUI_CloseMenu(sim, mobileState);
	}
}
