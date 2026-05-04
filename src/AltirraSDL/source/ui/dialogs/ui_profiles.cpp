//	AltirraSDL - Profile Management dialog
//	Matches Windows Profiles dialog (uiprofiles.cpp)

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/vdstl.h>
#include <cstring>

#include "ui_main.h"
#include "simulator.h"
#include "settings.h"
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "netplay/netplay_glue.h"
#endif

extern ATSimulator g_sim;

void ATUIRenderProfiles(ATSimulator &sim, ATUIState &state) {
#ifdef ALTIRRA_NETPLAY_ENABLED
	// Defensive: if the dialog was already open when an Online Play
	// session began (the menu gate only blocks new opens, not
	// pre-existing windows), close it.  Editing / switching profiles
	// while the canonical Online Play profile is active would break
	// the session.
	// IsSessionEngaged() — auto-close only once a peer is engaged.
	// Merely hosting (WaitingForJoiner) shouldn't block profile edits.
	if (ATNetplayGlue::IsSessionEngaged()) {
		state.showProfiles = false;
		return;
	}
#endif
	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Profiles", &state.showProfiles, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showProfiles = false;
		ImGui::End();
		return;
	}

	static char renameBuffer[128] = {};
	static uint32 renamingId = kATProfileId_Invalid;

	uint32 currentId = ATSettingsGetCurrentProfileId();

	// Enumerate profiles
	vdfastvector<uint32> profileIds;
	ATSettingsProfileEnum(profileIds);

	// Default profile names
	static const char *kDefaultProfileNames[] = {
		"400/800", "1200XL", "XL/XE", "XEGS", "5200"
	};

	// Show current profile
	VDStringW curName = ATSettingsProfileGetName(currentId);
	VDStringA curNameU8 = VDTextWToU8(curName);
	ImGui::Text("Current profile: %s", curNameU8.c_str());

	bool temporary = ATSettingsGetTemporaryProfileMode();
	if (ImGui::Checkbox("Temporary Profile (don't save changes)", &temporary))
		ATSettingsSetTemporaryProfileMode(temporary);
	ImGui::SetItemTooltip("When enabled, changes are not saved to the profile on exit.");

	ImGui::Separator();

	// Default profiles section
	ImGui::SeparatorText("Default Profiles");
	ImGui::TextWrapped("Default profiles are used as presets for different hardware types.");

	if (ImGui::BeginTable("##DefaultProfiles", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
		ImGui::TableSetupColumn("Hardware", ImGuiTableColumnFlags_WidthFixed, 80.0f);
		ImGui::TableSetupColumn("Profile", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
		ImGui::TableHeadersRow();

		for (int i = 0; i < kATDefaultProfileCount; ++i) {
			uint32 defId = ATGetDefaultProfileId((ATDefaultProfile)i);
			VDStringW defName = ATSettingsProfileGetName(defId);
			VDStringA defNameU8 = VDTextWToU8(defName);

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(kDefaultProfileNames[i]);
			ImGui::TableNextColumn();
			ImGui::Text("%s", defNameU8.c_str());
			ImGui::TableNextColumn();

			ImGui::PushID(i);
			if (ImGui::SmallButton("Switch")) {
				ATSettingsSwitchProfile(defId);
				sim.Resume();
			}
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	// User profiles section
	ImGui::SeparatorText("All Profiles");

	float listHeight = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2;

	if (ImGui::BeginChild("##ProfileList", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
		// Global profile (ID 0) — cannot be renamed or deleted
		{
			VDStringW globalName = ATSettingsProfileGetName(0);
			VDStringA globalNameU8 = VDTextWToU8(globalName);
			bool isActive = (currentId == 0);

			ImGui::PushID(0);
			if (ImGui::Selectable(globalNameU8.c_str(), isActive)) {
				if (!isActive) {
					ATSettingsSwitchProfile(0);
					sim.Resume();
				}
			}
			if (isActive) {
				ImGui::SameLine();
				ImGui::TextDisabled("(active)");
			}
			ImGui::PopID();
		}

		// Other profiles
		for (uint32 id : profileIds) {
			VDStringW name = ATSettingsProfileGetName(id);
			VDStringA nameU8 = VDTextWToU8(name);
			bool isActive = (currentId == id);
			bool visible = ATSettingsProfileGetVisible(id);

			ImGui::PushID((int)id);

			if (!visible)
				ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);

			if (renamingId == id) {
				ImGui::SetNextItemWidth(-100);
				if (ImGui::InputText("##rename", renameBuffer, sizeof(renameBuffer),
					ImGuiInputTextFlags_EnterReturnsTrue)) {
					ATSettingsProfileSetName(id, VDTextU8ToW(VDStringA(renameBuffer)).c_str());
					renamingId = kATProfileId_Invalid;
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Done"))
					renamingId = kATProfileId_Invalid;
			} else {
				if (ImGui::Selectable(nameU8.c_str(), isActive, ImGuiSelectableFlags_AllowDoubleClick)) {
					if (ImGui::IsMouseDoubleClicked(0)) {
						renamingId = id;
						strncpy(renameBuffer, nameU8.c_str(), sizeof(renameBuffer) - 1);
					} else if (!isActive) {
						ATSettingsSwitchProfile(id);
						sim.Resume();
					}
				}
			}

			if (isActive) {
				ImGui::SameLine();
				ImGui::TextDisabled("(active)");
			}

			// Context menu
			if (ImGui::BeginPopupContextItem()) {
				if (ImGui::MenuItem("Switch To")) {
					ATSettingsSwitchProfile(id);
					sim.Resume();
				}
				if (ImGui::MenuItem("Rename")) {
					renamingId = id;
					strncpy(renameBuffer, nameU8.c_str(), sizeof(renameBuffer) - 1);
				}

				bool vis = ATSettingsProfileGetVisible(id);
				if (ImGui::MenuItem("Visible", nullptr, vis))
					ATSettingsProfileSetVisible(id, !vis);

				ImGui::Separator();
				if (ImGui::MenuItem("Delete", nullptr, false, !isActive)) {
					ATSettingsProfileDelete(id);
				}
				ImGui::EndPopup();
			}

			if (!visible)
				ImGui::PopStyleVar();

			ImGui::PopID();
		}
	}
	ImGui::EndChild();

	// Bottom buttons
	if (ImGui::Button("Add Profile")) {
		uint32 newId = ATSettingsGenerateProfileId();
		ATSettingsProfileSetParent(newId, currentId);
		ATSettingsProfileSetName(newId, L"New Profile");
		ATSettingsProfileSetCategoryMask(newId, kATSettingsCategory_None);
		ATSettingsProfileSetVisible(newId, true);
		// Start renaming immediately
		renamingId = newId;
		strncpy(renameBuffer, "New Profile", sizeof(renameBuffer) - 1);
	}

	ImGui::SameLine();
	if (ImGui::Button("OK"))
		state.showProfiles = false;

	ImGui::End();
}
