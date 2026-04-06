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

void RenderSettings(ATSimulator &sim, ATUIState &uiState,
	ATMobileUIState &mobileState, SDL_Window *window)
{
	ImGuiIO &io = ImGui::GetIO();

	float insetT = (float)mobileState.layout.insets.top;
	float insetB = (float)mobileState.layout.insets.bottom;
	float insetL = (float)mobileState.layout.insets.left;
	float insetR = (float)mobileState.layout.insets.right;
	ImGui::SetNextWindowPos(ImVec2(insetL, insetT));
	ImGui::SetNextWindowSize(ImVec2(
		io.DisplaySize.x - insetL - insetR,
		io.DisplaySize.y - insetT - insetB));

	ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

	if (ImGui::Begin("##MobileSettings", nullptr, flags)) {
		// Header — back arrow, title reflects current sub-page.
		float headerH = dp(48.0f);
		if (ImGui::Button("<", ImVec2(dp(48.0f), headerH))) {
			if (s_settingsPage == ATMobileSettingsPage::Firmware
				&& s_fwPicker != kATFirmwareType_Unknown)
			{
				s_fwPicker = kATFirmwareType_Unknown;
			} else if (s_settingsPage == ATMobileSettingsPage::Home) {
				mobileState.currentScreen = ATMobileUIScreen::HamburgerMenu;
			} else {
				s_settingsPage = ATMobileSettingsPage::Home;
			}
		}
		ImGui::SameLine();
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
		const char *pageTitle = "Settings";
		switch (s_settingsPage) {
		case ATMobileSettingsPage::Home:        pageTitle = "Settings"; break;
		case ATMobileSettingsPage::Machine:     pageTitle = "Machine"; break;
		case ATMobileSettingsPage::Display:     pageTitle = "Display"; break;
		case ATMobileSettingsPage::Performance: pageTitle = "Performance"; break;
		case ATMobileSettingsPage::Controls:    pageTitle = "Controls"; break;
		case ATMobileSettingsPage::SaveState:   pageTitle = "Save State"; break;
		case ATMobileSettingsPage::Firmware:    pageTitle = "Firmware"; break;
		}
		ImGui::Text("%s", pageTitle);

		ImGui::Separator();
		ImGui::Spacing();

		ImGui::BeginChild("SettingsScroll", ImVec2(0, 0), ImGuiChildFlags_None);
		ATTouchDragScroll();

		// --- Settings home: category list with subtitle previews ---
		if (s_settingsPage == ATMobileSettingsPage::Home) {
			auto hwLabel = [&](){
				switch (sim.GetHardwareMode()) {
				case kATHardwareMode_800:   return "400/800";
				case kATHardwareMode_800XL: return "800XL";
				case kATHardwareMode_130XE: return "130XE";
				case kATHardwareMode_5200:  return "5200";
				default: return "?";
				}
			};
			const char *vsLabel = (sim.GetVideoStandard() == kATVideoStandard_PAL) ? "PAL" : "NTSC";
			const char *presetLabel = "Balanced";
			switch (mobileState.performancePreset) {
			case 0: presetLabel = "Efficient"; break;
			case 1: presetLabel = "Balanced"; break;
			case 2: presetLabel = "Quality"; break;
			case 3: presetLabel = "Custom"; break;
			}

			struct CatRow {
				const char *title;
				VDStringA subtitle;
				ATMobileSettingsPage target;
			};
			CatRow cats[7];
			int n = 0;

			cats[n++] = { "Machine",
				VDStringA().sprintf("%s  \xC2\xB7  %s",
					hwLabel(), vsLabel),
				ATMobileSettingsPage::Machine };

			cats[n++] = { "Display",
				VDStringA("Filter, visual effects"),
				ATMobileSettingsPage::Display };

			cats[n++] = { "Performance",
				VDStringA().sprintf("Preset: %s", presetLabel),
				ATMobileSettingsPage::Performance };

			cats[n++] = { "Controls",
				VDStringA().sprintf("Size: %s  \xC2\xB7  Haptic: %s",
					mobileState.layoutConfig.controlSize == ATTouchControlSize::Small  ? "Small"  :
					mobileState.layoutConfig.controlSize == ATTouchControlSize::Large  ? "Large"  : "Medium",
					mobileState.layoutConfig.hapticEnabled ? "on" : "off"),
				ATMobileSettingsPage::Controls };

			cats[n++] = { "Save State",
				VDStringA().sprintf("Auto-save: %s  \xC2\xB7  Restore: %s",
					mobileState.autoSaveOnSuspend ? "on" : "off",
					mobileState.autoRestoreOnStart ? "on" : "off"),
				ATMobileSettingsPage::SaveState };

			cats[n++] = { "Firmware",
				s_romDir.empty()
					? VDStringA("(not set)")
					: VDStringA().sprintf("%s", VDTextWToU8(s_romDir).c_str()),
				ATMobileSettingsPage::Firmware };

			float rowH = dp(76.0f);
			for (int i = 0; i < n; ++i) {
				ImGui::PushID(i);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					IM_COL32(30, 35, 50, 200), dp(10.0f));

				if (ImGui::InvisibleButton("##cat",
					ImVec2(availW, rowH)))
				{
					s_settingsPage = cats[i].target;
				}

				ImVec2 tcur(cursor.x + dp(16.0f), cursor.y + dp(12.0f));
				dl->AddText(tcur, IM_COL32(240, 242, 248, 255),
					cats[i].title);
				ImVec2 scur(cursor.x + dp(16.0f), cursor.y + dp(44.0f));
				dl->AddText(scur, IM_COL32(160, 175, 200, 255),
					cats[i].subtitle.c_str());

				// Right-side chevron
				ImVec2 chev(cursor.x + availW - dp(28.0f),
					cursor.y + rowH * 0.5f - dp(8.0f));
				dl->AddText(chev, IM_COL32(160, 175, 200, 255), ">");

				ImGui::Dummy(ImVec2(0, dp(10.0f)));
				ImGui::PopID();
			}

			ImGui::Dummy(ImVec2(0, dp(16.0f)));
			if (ImGui::Button("About", ImVec2(-1, dp(56.0f)))) {
				mobileState.currentScreen = ATMobileUIScreen::About;
			}

			ImGui::Dummy(ImVec2(0, dp(32.0f)));
			ImGui::EndChild();
			ImGui::End();
			return;
		}

		// --- Sub-page: Machine ---
		if (s_settingsPage == ATMobileSettingsPage::Machine) {
		ATTouchSection("Machine");

		// Hardware type.  All four modes work with the built-in HLE
		// kernel — no user-supplied ROMs required.  Changing the
		// mode triggers a cold reset inside the simulator.
		{
			static const struct {
				const char *label;
				ATHardwareMode mode;
			} kHw[] = {
				{ "400/800",    kATHardwareMode_800    },
				{ "600/800XL",  kATHardwareMode_800XL  },
				{ "130XE",      kATHardwareMode_130XE  },
				{ "5200",       kATHardwareMode_5200   },
			};
			constexpr int kNumHw = (int)(sizeof(kHw) / sizeof(kHw[0]));

			ATHardwareMode curMode = sim.GetHardwareMode();
			int curIdx = 1; // default 800XL
			for (int i = 0; i < kNumHw; ++i)
				if (kHw[i].mode == curMode) { curIdx = i; break; }

			static const char *labels[kNumHw] = {
				kHw[0].label, kHw[1].label, kHw[2].label, kHw[3].label,
			};
			if (ATTouchSegmented("Hardware", &curIdx, labels, kNumHw)) {
				sim.SetHardwareMode(kHw[curIdx].mode);
				sim.ColdReset();
			}
		}

		// Video Standard — PAL / NTSC
		{
			int current = (sim.GetVideoStandard() == kATVideoStandard_PAL) ? 0 : 1;
			static const char *items[] = { "PAL", "NTSC" };
			if (ATTouchSegmented("Video Standard", &current, items, 2))
				sim.SetVideoStandard(current == 0 ? kATVideoStandard_PAL : kATVideoStandard_NTSC);
		}

		// Memory Size
		{
			static const struct {
				const char *label;
				ATMemoryMode mode;
			} kMemModes[] = {
				{ "16K",   kATMemoryMode_16K   },
				{ "48K",   kATMemoryMode_48K   },
				{ "64K",   kATMemoryMode_64K   },
				{ "128K",  kATMemoryMode_128K  },
				{ "320K",  kATMemoryMode_320K  },
				{ "1088K", kATMemoryMode_1088K },
			};
			ATMemoryMode curMode = sim.GetMemoryMode();
			int curIdx = 4; // default 320K
			int count = (int)(sizeof(kMemModes)/sizeof(kMemModes[0]));
			for (int i = 0; i < count; i++) {
				if (kMemModes[i].mode == curMode) { curIdx = i; break; }
			}
			static const char *labels[6] = {
				kMemModes[0].label, kMemModes[1].label, kMemModes[2].label,
				kMemModes[3].label, kMemModes[4].label, kMemModes[5].label,
			};
			if (ATTouchSegmented("Memory Size", &curIdx, labels, count))
				sim.SetMemoryMode(kMemModes[curIdx].mode);
		}

		// BASIC toggle
		{
			bool basicEnabled = sim.IsBASICEnabled();
			if (ATTouchToggle("BASIC Enabled", &basicEnabled))
				sim.SetBASICEnabled(basicEnabled);
		}

		// SIO Patch toggle
		{
			bool sioEnabled = sim.IsSIOPatchEnabled();
			if (ATTouchToggle("SIO Patch", &sioEnabled))
				sim.SetSIOPatchEnabled(sioEnabled);
		}

		// ---- RANDOMIZATION (still on Machine page) ----
		ATTouchSection("Randomization");

		{
			bool randomLaunch = sim.IsRandomProgramLaunchDelayEnabled();
			if (ATTouchToggle("Randomize launch delay", &randomLaunch))
				sim.SetRandomProgramLaunchDelayEnabled(randomLaunch);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Delays program boot by a random number of cycles so "
			"POKEY's RNG seed varies between runs.  Default: on.");
		ImGui::PopStyleColor();

		{
			bool randomFill = sim.IsRandomFillEXEEnabled();
			if (ATTouchToggle("Randomize memory on EXE load", &randomFill))
				sim.SetRandomFillEXEEnabled(randomFill);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Fills uninitialised RAM with random bytes before a .xex "
			"program loads.  Helps flush out games that relied on "
			"specific power-on RAM patterns.  Default: off.");
		ImGui::PopStyleColor();
		} // end Machine page

		// --- Sub-page: Controls ---
		if (s_settingsPage == ATMobileSettingsPage::Controls) {
		ATTouchSection("Controls");

		// Joystick style
		{
			int js = (int)mobileState.layoutConfig.joystickStyle;
			static const char *styles[] = { "Analog", "D-Pad 8", "D-Pad 4" };
			if (ATTouchSegmented("Joystick Style", &js, styles, 3)) {
				mobileState.layoutConfig.joystickStyle = (ATTouchJoystickStyle)js;
				SaveMobileConfig(mobileState);
			}
		}

		// Control size
		{
			int sz = (int)mobileState.layoutConfig.controlSize;
			static const char *sizes[] = { "Small", "Medium", "Large" };
			if (ATTouchSegmented("Control Size", &sz, sizes, 3)) {
				mobileState.layoutConfig.controlSize = (ATTouchControlSize)sz;
				SaveMobileConfig(mobileState);
			}
		}

		// Control opacity — 10%-100%
		{
			int pct = (int)(mobileState.layoutConfig.controlOpacity * 100.0f + 0.5f);
			if (ATTouchSlider("Opacity", &pct, 10, 100, "%d%%")) {
				mobileState.layoutConfig.controlOpacity = pct / 100.0f;
				SaveMobileConfig(mobileState);
			}
		}

		// Haptic feedback
		if (ATTouchToggle("Haptic Feedback", &mobileState.layoutConfig.hapticEnabled)) {
			SaveMobileConfig(mobileState);
			ATTouchControls_SetHapticEnabled(mobileState.layoutConfig.hapticEnabled);
		}
		} // end Controls page

		// --- Sub-page: Save State ---
		if (s_settingsPage == ATMobileSettingsPage::SaveState) {
		ATTouchSection("Save State");

		if (ATTouchToggle("Auto-save on exit / background",
			&mobileState.autoSaveOnSuspend))
		{
			SaveMobileConfig(mobileState);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Snapshots the emulator whenever the app goes to "
			"background or is closed, so a swipe-away or an "
			"incoming call never loses progress.");
		ImGui::PopStyleColor();

		if (ATTouchToggle("Restore on startup",
			&mobileState.autoRestoreOnStart))
		{
			SaveMobileConfig(mobileState);
		}
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"On launch, resume exactly where you left off "
			"(requires Auto-save above).");
		ImGui::PopStyleColor();

		ImGui::Spacing();

		// Manual save / load buttons — always available so the user
		// can checkpoint a run independently of the auto-save setting.
		float halfW = (ImGui::GetContentRegionAvail().x - dp(8.0f)) * 0.5f;
		if (ImGui::Button("Save State Now", ImVec2(halfW, dp(56.0f)))) {
			try {
				VDStringW path = QuickSaveStatePath();
				sim.SaveState(path.c_str());
				ShowInfoModal("Saved", "Emulator state saved.");
			} catch (const MyError &e) {
				ShowInfoModal("Save Failed", e.c_str());
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Load State Now", ImVec2(halfW, dp(56.0f)))) {
			VDStringW path = QuickSaveStatePath();
			if (!VDDoesPathExist(path.c_str())) {
				ShowInfoModal("No State", "No saved state available to load.");
			} else {
				try {
					ATImageLoadContext ctx{};
					if (sim.Load(path.c_str(), kATMediaWriteMode_RO, &ctx)) {
						sim.Resume();
						mobileState.gameLoaded = true;
						ShowInfoModal("Loaded", "Emulator state restored.");
					}
				} catch (const MyError &e) {
					ShowInfoModal("Load Failed", e.c_str());
				}
			}
		}

		} // end Save State page

		// --- Sub-page: Display (Filter + Visual Effects) ---
		if (s_settingsPage == ATMobileSettingsPage::Display) {
		ATTouchSection("Visual Effects");

		// Warn the user up front if the current display backend can't
		// actually render GPU-based effects.  Scanlines still work in
		// software so they're never greyed-out.
		{
			IDisplayBackend *backend = ATUIGetDisplayBackend();
			bool hwSupport = backend && backend->SupportsScreenFX();
			if (!hwSupport) {
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.70f, 0.72f, 0.78f, 1));
				ImGui::TextWrapped(
					"Bloom and CRT distortion need the OpenGL display "
					"backend.  The SDL_Renderer fallback (currently "
					"active on this device) will accept the toggles "
					"but silently ignore those two — scanlines still "
					"work either way.");
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}
		}

		// Manually toggling any visual effect moves the performance
		// preset to Custom so the user can see they've left the
		// bundle.
		auto markCustom = [&](){ mobileState.performancePreset = 3; };

		if (ATTouchToggle("Scanlines", &mobileState.fxScanlines)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		if (ATTouchToggle("Bloom", &mobileState.fxBloom)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		if (ATTouchToggle("CRT Distortion", &mobileState.fxDistortion)) {
			markCustom();
			SaveMobileConfig(mobileState);
			try { ATMobileUI_ApplyVisualEffects(mobileState); } catch (...) {}
		}

		ATTouchSection("Display");

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
			static const char *filters[] = { "Sharp", "Bilinear", "Sharp Bi" };
			if (ATTouchSegmented("Filter Mode", &idx, filters, 3)) {
				static const ATDisplayFilterMode kModes[] = {
					kATDisplayFilterMode_Point,
					kATDisplayFilterMode_Bilinear,
					kATDisplayFilterMode_SharpBilinear,
				};
				ATUISetDisplayFilterMode(kModes[idx]);
				mobileState.performancePreset = 3;  // Custom
				SaveMobileConfig(mobileState);
			}
		}
		} // end Display page

		// --- Sub-page: Performance (bundled preset) ---
		if (s_settingsPage == ATMobileSettingsPage::Performance) {
		ATTouchSection("Performance Preset");

		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
		ImGui::TextWrapped(
			"Choose a preset that bundles visual effects and the "
			"display filter for a consistent trade-off.  Pick "
			"Efficient on older devices, Quality on flagships.");
		ImGui::PopStyleColor();
		ImGui::Spacing();

		{
			// When preset == 3 (Custom) we pass it through unchanged:
			// ATTouchSegmented highlights the matching index or none
			// if out of range, so Custom correctly shows no segment
			// active while the Custom label below explains why.
			int p = mobileState.performancePreset;
			static const char *items[] = { "Efficient", "Balanced", "Quality" };
			if (ATTouchSegmented("Preset", &p, items, 3)) {
				mobileState.performancePreset = p;
				SaveMobileConfig(mobileState);
				ATMobileUI_ApplyPerformancePreset(mobileState);
			}
			if (mobileState.performancePreset == 3) {
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(1.0f, 0.78f, 0.30f, 1));
				ImGui::TextUnformatted(
					"Preset: Custom (you've manually changed a visual "
					"setting — pick a preset above to revert).");
				ImGui::PopStyleColor();
			}
		}
		} // end Performance page

		// --- Sub-page: Firmware ---
		if (s_settingsPage == ATMobileSettingsPage::Firmware) {
		ATFirmwareManager *fwm = g_sim.GetFirmwareManager();

		auto nameForId = [&](uint64 id) -> VDStringA {
			if (!id) return VDStringA("(internal)");
			ATFirmwareInfo info;
			if (fwm->GetFirmwareInfo(id, info))
				return VDTextWToU8(info.mName);
			return VDStringA("(unknown)");
		};

		if (s_fwPicker == kATFirmwareType_Unknown) {
			ATTouchSection("Firmware");

			if (!s_romDir.empty()) {
				VDStringA dirU8 = VDTextWToU8(s_romDir);
				ImGui::TextWrapped("ROM Directory: %s", dirU8.c_str());
			} else {
				ImGui::Text("ROM Directory: (not set)");
			}
			if (s_romScanResult >= 0)
				ImGui::Text("Status: %d ROMs found", s_romScanResult);

			ImGui::Spacing();
			if (ImGui::Button("Select Firmware Folder", ImVec2(-1, dp(56.0f)))) {
				s_romFolderMode = true;
				s_fileBrowserNeedsRefresh = true;
				mobileState.currentScreen = ATMobileUIScreen::FileBrowser;
			}

			ImGui::Dummy(ImVec2(0, dp(16.0f)));
			ATTouchSection("Kernel & BASIC");

			// Tappable card rows for each user-selectable slot.
			// Kept to the kernels + BASIC that mobile users actually
			// care about — the desktop Firmware Manager covers the
			// long tail of device ROMs.
			struct Slot { const char *title; ATFirmwareType type; };
			static const Slot kSlots[] = {
				{ "OS-B (400/800)",      kATFirmwareType_Kernel800_OSB  },
				{ "OS-A (400/800)",      kATFirmwareType_Kernel800_OSA  },
				{ "XL/XE Kernel",        kATFirmwareType_KernelXL       },
				{ "XEGS Kernel",         kATFirmwareType_KernelXEGS     },
				{ "5200 Kernel",         kATFirmwareType_Kernel5200     },
				{ "Atari BASIC",         kATFirmwareType_Basic          },
			};

			float rowH = dp(72.0f);
			for (size_t i = 0; i < sizeof(kSlots)/sizeof(kSlots[0]); ++i) {
				ImGui::PushID((int)i);
				uint64 curId = fwm->GetDefaultFirmware(kSlots[i].type);
				VDStringA curName = nameForId(curId);

				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					IM_COL32(30, 35, 50, 200), dp(10.0f));

				if (ImGui::InvisibleButton("##fwslot", ImVec2(availW, rowH)))
					s_fwPicker = kSlots[i].type;

				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(10.0f)),
					IM_COL32(240, 242, 248, 255), kSlots[i].title);
				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(40.0f)),
					IM_COL32(160, 175, 200, 255), curName.c_str());
				dl->AddText(ImVec2(cursor.x + availW - dp(28.0f),
						cursor.y + rowH * 0.5f - dp(8.0f)),
					IM_COL32(160, 175, 200, 255), ">");

				ImGui::Dummy(ImVec2(0, dp(8.0f)));
				ImGui::PopID();
			}

			ImGui::Dummy(ImVec2(0, dp(12.0f)));
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.70f, 0.78f, 1));
			ImGui::TextWrapped(
				"Tap a slot to choose which ROM to use.  Selections "
				"apply on the next cold reset.  The built-in HLE "
				"kernel is used as a fallback if no ROM is picked.");
			ImGui::PopStyleColor();
		} else {
			// --- Firmware picker ---
			ATFirmwareType picking = s_fwPicker;
			const char *slotTitle = "Firmware";
			switch (picking) {
			case kATFirmwareType_Kernel800_OSA: slotTitle = "OS-A (400/800)"; break;
			case kATFirmwareType_Kernel800_OSB: slotTitle = "OS-B (400/800)"; break;
			case kATFirmwareType_KernelXL:      slotTitle = "XL/XE Kernel"; break;
			case kATFirmwareType_KernelXEGS:    slotTitle = "XEGS Kernel"; break;
			case kATFirmwareType_Kernel5200:    slotTitle = "5200 Kernel"; break;
			case kATFirmwareType_Basic:         slotTitle = "Atari BASIC"; break;
			default: break;
			}
			ATTouchSection(slotTitle);

			if (ImGui::Button("< Back", ImVec2(dp(120.0f), dp(48.0f))))
				s_fwPicker = kATFirmwareType_Unknown;

			ImGui::Dummy(ImVec2(0, dp(8.0f)));

			vdvector<ATFirmwareInfo> fwList;
			fwm->GetFirmwareList(fwList);

			uint64 curId = fwm->GetDefaultFirmware(picking);

			// "Use built-in HLE" row — selecting this clears the
			// default so the simulator falls back to the bundled
			// HLE kernel at next cold reset.
			{
				float rowH = dp(64.0f);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				bool selected = (curId == 0);
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					selected ? IM_COL32(40, 90, 160, 220)
					         : IM_COL32(30, 35, 50, 200),
					dp(10.0f));
				if (ImGui::InvisibleButton("##fwhle", ImVec2(availW, rowH))) {
					fwm->SetDefaultFirmware(picking, 0);
					ATRegistryFlushToDisk();
					g_sim.LoadROMs();
					g_sim.ColdReset();
					s_fwPicker = kATFirmwareType_Unknown;
				}
				dl->AddText(ImVec2(cursor.x + dp(16.0f),
						cursor.y + rowH * 0.5f - dp(8.0f)),
					IM_COL32(240, 242, 248, 255),
					"Built-in HLE (fallback)");
				ImGui::Dummy(ImVec2(0, dp(8.0f)));
			}

			int shown = 0;
			for (const ATFirmwareInfo &info : fwList) {
				if (info.mType != picking)
					continue;
				if (!info.mbVisible)
					continue;
				++shown;

				ImGui::PushID((int)info.mId ^ (int)(info.mId >> 32));
				float rowH = dp(64.0f);
				ImVec2 cursor = ImGui::GetCursorScreenPos();
				float availW = ImGui::GetContentRegionAvail().x;
				ImDrawList *dl = ImGui::GetWindowDrawList();
				bool selected = (curId == info.mId);
				dl->AddRectFilled(cursor,
					ImVec2(cursor.x + availW, cursor.y + rowH),
					selected ? IM_COL32(40, 90, 160, 220)
					         : IM_COL32(30, 35, 50, 200),
					dp(10.0f));
				if (ImGui::InvisibleButton("##fw", ImVec2(availW, rowH))) {
					fwm->SetDefaultFirmware(picking, info.mId);
					ATRegistryFlushToDisk();
					g_sim.LoadROMs();
					g_sim.ColdReset();
					s_fwPicker = kATFirmwareType_Unknown;
				}
				VDStringA nm = VDTextWToU8(info.mName);
				VDStringA ph = VDTextWToU8(info.mPath);
				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(8.0f)),
					IM_COL32(240, 242, 248, 255), nm.c_str());
				dl->AddText(ImVec2(cursor.x + dp(16.0f), cursor.y + dp(36.0f)),
					IM_COL32(160, 175, 200, 255), ph.c_str());
				ImGui::Dummy(ImVec2(0, dp(8.0f)));
				ImGui::PopID();
			}

			if (shown == 0) {
				ImGui::PushStyleColor(ImGuiCol_Text,
					ImVec4(0.65f, 0.70f, 0.78f, 1));
				ImGui::TextWrapped(
					"No ROMs of this type were found in your "
					"firmware folder.  Tap 'Select Firmware "
					"Folder' on the previous screen to scan "
					"a directory containing Atari ROM images.");
				ImGui::PopStyleColor();
			}
		}
		} // end Firmware page

		// Bottom padding so the last row isn't flush against the nav bar
		ImGui::Dummy(ImVec2(0, dp(32.0f)));

		ImGui::EndChild();
	}
	ImGui::End();
}
