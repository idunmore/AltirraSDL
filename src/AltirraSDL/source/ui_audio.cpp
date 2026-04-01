//	AltirraSDL - Audio Settings dialog
//	Mute, POKEY options, dual POKEY, drive sounds.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>

#include "ui_main.h"
#include "simulator.h"
#include "uiaccessors.h"
#include <at/ataudio/pokey.h>
#include <at/ataudio/audiooutput.h>

extern ATSimulator g_sim;

void ATUIRenderAudioSettings(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Audio Settings", &state.showAudioSettings)) {
		ImGui::End();
		return;
	}

	IATAudioOutput *pAudio = sim.GetAudioOutput();
	if (pAudio) {
		bool muted = pAudio->GetMute();
		if (ImGui::Checkbox("Mute", &muted))
			pAudio->SetMute(muted);
	}

	bool dualPokey = sim.IsDualPokeysEnabled();
	if (ImGui::Checkbox("Dual POKEYs (Stereo)", &dualPokey))
		sim.SetDualPokeysEnabled(dualPokey);

	bool driveSounds = ATUIGetDriveSoundsEnabled();
	if (ImGui::Checkbox("Drive Sounds", &driveSounds))
		ATUISetDriveSoundsEnabled(driveSounds);

	ImGui::SeparatorText("POKEY Options");

	ATPokeyEmulator& pokey = sim.GetPokey();

	bool nonlinear = pokey.IsNonlinearMixingEnabled();
	if (ImGui::Checkbox("Nonlinear Mixing", &nonlinear))
		pokey.SetNonlinearMixingEnabled(nonlinear);

	bool serialNoise = pokey.IsSerialNoiseEnabled();
	if (ImGui::Checkbox("Serial Noise", &serialNoise))
		pokey.SetSerialNoiseEnabled(serialNoise);

	ImGui::SeparatorText("POKEY Channels");
	for (int i = 0; i < 4; ++i) {
		char label[32];
		snprintf(label, sizeof(label), "Channel %d", i + 1);
		bool ch = pokey.IsChannelEnabled(i);
		if (ImGui::Checkbox(label, &ch))
			pokey.SetChannelEnabled(i, ch);
	}

	ImGui::End();
}
