//	AltirraSDL - Configure System > Emulator > Online Play page.
//
//	SDL3-only page (no Windows equivalent yet).  Exposes the two
//	communication-icon toggles plus a short help line.  Persistence is
//	owned by emote_netplay.cpp via the settings load/save callback
//	registry, so nothing needs to be re-saved from here.

#include <stdafx.h>
#include <imgui.h>

#include "simulator.h"
#include "ui_system_internal.h"
#include "ui/emotes/emote_netplay.h"
#include "settings.h"

void RenderOnlinePlayCategory(ATSimulator & /*sim*/) {
	ImGui::TextWrapped("Settings that apply to 2-player Online Play sessions.");
	ImGui::Spacing();

	ImGui::SeparatorText("Communication Icons");

	ImGui::TextWrapped(
		"Send quick reactions to the other player during a match. "
		"Press F1 (keyboard) or R3 / right stick click (gamepad) "
		"during an active session to open the picker.  F1 is safe "
		"to use for this because warp-speed is automatically blocked "
		"during Online Play.");

	ImGui::Spacing();

	bool sendEnabled = ATEmoteNetplay::GetSendEnabled();
	if (ImGui::Checkbox("Send communication icons to the other player",
			&sendEnabled))
	{
		ATEmoteNetplay::SetSendEnabled(sendEnabled);
		ATSaveSettings(kATSettingsCategory_Environment);
	}

	bool recvEnabled = ATEmoteNetplay::GetReceiveEnabled();
	if (ImGui::Checkbox("Receive communication icons from the other player",
			&recvEnabled))
	{
		ATEmoteNetplay::SetReceiveEnabled(recvEnabled);
		ATSaveSettings(kATSettingsCategory_Environment);
	}

	ImGui::Spacing();
	ImGui::TextDisabled(
		"Both toggles default to on.  Turning either off disables the "
		"corresponding direction silently; the other player is not "
		"notified.");
}
