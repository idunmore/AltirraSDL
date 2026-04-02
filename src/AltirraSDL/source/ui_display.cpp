//	AltirraSDL - Display Settings dialog
//	Filter mode, stretch mode, overscan, FPS, indicators.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>

#include "ui_main.h"
#include "simulator.h"
#include "gtia.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include <vd2/system/text.h>

extern ATSimulator g_sim;

static const ATDisplayFilterMode kFilterValues[] = {
	kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
	kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
	kATDisplayFilterMode_AnySuitable,
};
static const char *kFilterLabels[] = {
	"Point (Nearest)", "Bilinear", "Sharp Bilinear",
	"Bicubic", "Default (Any Suitable)",
};

static const ATDisplayStretchMode kStretchValues[] = {
	kATDisplayStretchMode_Unconstrained,
	kATDisplayStretchMode_PreserveAspectRatio,
	kATDisplayStretchMode_SquarePixels,
	kATDisplayStretchMode_Integral,
	kATDisplayStretchMode_IntegralPreserveAspectRatio,
};
static const char *kStretchLabels[] = {
	"Fit to Window", "Preserve Aspect Ratio", "Square Pixels",
	"Integer Scale", "Integer + Aspect Ratio",
};

void ATUIRenderDisplaySettings(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(400, 340), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Display Settings", &state.showDisplaySettings, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showDisplaySettings = false;
		ImGui::End();
		return;
	}

	// Filter mode
	ATDisplayFilterMode curFM = ATUIGetDisplayFilterMode();
	int fmIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kFilterValues[i] == curFM) { fmIdx = i; break; }
	if (ImGui::Combo("Filter Mode", &fmIdx, kFilterLabels, 5))
		ATUISetDisplayFilterMode(kFilterValues[fmIdx]);

	// Stretch mode
	ATDisplayStretchMode curSM = ATUIGetDisplayStretchMode();
	int smIdx = 0;
	for (int i = 0; i < 5; ++i)
		if (kStretchValues[i] == curSM) { smIdx = i; break; }
	if (ImGui::Combo("Stretch Mode", &smIdx, kStretchLabels, 5))
		ATUISetDisplayStretchMode(kStretchValues[smIdx]);

	// Overscan
	ATGTIAEmulator& gtia = sim.GetGTIA();
	static const char *kOverscanLabels[] = {
		"Normal", "Extended", "Full", "OS Screen Only", "Widescreen"
	};
	// Enum order: Normal=0, Extended=1, Full=2, OSScreen=3, Widescreen=4
	int osIdx = (int)gtia.GetOverscanMode();
	if (osIdx < 0 || osIdx >= 5) osIdx = 0;
	if (ImGui::Combo("Overscan Mode", &osIdx, kOverscanLabels, 5))
		gtia.SetOverscanMode((ATGTIAEmulator::OverscanMode)osIdx);

	ImGui::Separator();

	bool showFPS = ATUIGetShowFPS();
	if (ImGui::Checkbox("Show FPS", &showFPS))
		ATUISetShowFPS(showFPS);

	bool indicators = ATUIGetDisplayIndicators();
	if (ImGui::Checkbox("Show Indicators", &indicators))
		ATUISetDisplayIndicators(indicators);

	bool pointerHide = ATUIGetPointerAutoHide();
	if (ImGui::Checkbox("Auto-Hide Mouse Pointer", &pointerHide))
		ATUISetPointerAutoHide(pointerHide);

	ImGui::End();
}

// =========================================================================
// Adjust Colors dialog (matches Windows IDD_ADJUST_COLORS)
// =========================================================================

// Helper: labeled slider with percent-style mapping
static bool SliderPercent(const char *label, float *v, float vmin, float vmax, const char *fmt = "%.0f%%") {
	float pct = *v * 100.0f;
	float pmin = vmin * 100.0f;
	float pmax = vmax * 100.0f;
	if (ImGui::SliderFloat(label, &pct, pmin, pmax, fmt)) {
		*v = pct / 100.0f;
		return true;
	}
	return false;
}

static bool SliderDegrees(const char *label, float *v, float vmin, float vmax) {
	return ImGui::SliderFloat(label, v, vmin, vmax, "%.0f deg");
}

void ATUIRenderAdjustColors(ATSimulator &sim, ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(480, 600), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Adjust Colors", &state.showAdjustColors, ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAdjustColors = false;
		ImGui::End();
		return;
	}

	ATGTIAEmulator& gtia = sim.GetGTIA();
	ATColorSettings settings = gtia.GetColorSettings();

	// Select active params based on current video mode
	bool isPAL = gtia.IsPALMode();
	ATNamedColorParams *params = isPAL ? &settings.mPALParams : &settings.mNTSCParams;

	bool changed = false;

	// Preset selection
	{
		uint32 presetCount = ATGetColorPresetCount();
		sint32 curPresetIdx = -1;
		if (!params->mPresetTag.empty())
			curPresetIdx = ATGetColorPresetIndexByTag(params->mPresetTag.c_str());

		// Build display: "(Custom)" + all presets
		int comboIdx = (curPresetIdx >= 0) ? (curPresetIdx + 1) : 0;
		VDStringA curLabel = (comboIdx == 0) ? VDStringA("(Custom)")
			: VDTextWToU8(VDStringW(ATGetColorPresetNameByIndex(curPresetIdx)));
		if (ImGui::BeginCombo("Preset", curLabel.c_str())) {
			if (ImGui::Selectable("(Custom)", comboIdx == 0))
				comboIdx = 0;
			for (uint32 i = 0; i < presetCount; ++i) {
				VDStringA name = VDTextWToU8(VDStringW(ATGetColorPresetNameByIndex(i)));
				bool selected = ((int)i + 1 == comboIdx);
				if (ImGui::Selectable(name.c_str(), selected)) {
					ATColorParams preset = ATGetColorPresetByIndex(i);
					params->mPresetTag = ATGetColorPresetTagByIndex(i);
					// Copy preset values to current params, preserving non-preset fields
					(ATColorParams &)*params = preset;
					changed = true;
				}
			}
			ImGui::EndCombo();
		}
	}

	// Shared NTSC/PAL toggle
	{
		bool shared = !settings.mbUsePALParams;
		if (ImGui::Checkbox("Share NTSC/PAL settings", &shared)) {
			settings.mbUsePALParams = !shared;
			changed = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(%s active)", isPAL ? "PAL" : "NTSC");
	}

	ImGui::Separator();

	// Luma ramp mode
	{
		static const char *kLumaLabels[] = { "Linear", "XL" };
		int lumaIdx = (int)params->mLumaRampMode;
		if (lumaIdx < 0 || lumaIdx >= 2) lumaIdx = 0;
		if (ImGui::Combo("Luma Ramp", &lumaIdx, kLumaLabels, 2)) {
			params->mLumaRampMode = (ATLumaRampMode)lumaIdx;
			changed = true;
		}
	}

	// Color matching mode
	{
		static const char *kMatchLabels[] = { "None", "sRGB", "Adobe RGB", "Gamma 2.2", "Gamma 2.4" };
		int matchIdx = (int)params->mColorMatchingMode;
		if (matchIdx < 0 || matchIdx >= 5) matchIdx = 0;
		if (ImGui::Combo("Color Matching", &matchIdx, kMatchLabels, 5)) {
			params->mColorMatchingMode = (ATColorMatchingMode)matchIdx;
			changed = true;
		}
	}

	// PAL quirks
	if (ImGui::Checkbox("PAL quirks", &params->mbUsePALQuirks))
		changed = true;

	ImGui::Separator();

	// Hue controls
	if (ImGui::CollapsingHeader("Hue", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (SliderDegrees("Hue Start", &params->mHueStart, -120.0f, 360.0f))
			changed = true;
		if (SliderDegrees("Hue Range", &params->mHueRange, 0.0f, 540.0f))
			changed = true;
	}

	// Brightness / Contrast
	if (ImGui::CollapsingHeader("Brightness / Contrast", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (SliderPercent("Brightness", &params->mBrightness, -0.50f, 0.50f))
			changed = true;
		if (SliderPercent("Contrast", &params->mContrast, 0.0f, 2.0f))
			changed = true;
		if (SliderPercent("Saturation", &params->mSaturation, 0.0f, 1.0f))
			changed = true;
		if (ImGui::SliderFloat("Gamma", &params->mGammaCorrect, 0.50f, 2.60f, "%.2f"))
			changed = true;
		if (SliderPercent("Intensity Scale", &params->mIntensityScale, 0.50f, 2.0f))
			changed = true;
	}

	// Artifacting
	if (ImGui::CollapsingHeader("Artifacting")) {
		if (SliderDegrees("Phase", &params->mArtifactHue, -60.0f, 360.0f))
			changed = true;
		if (SliderPercent("Saturation##Art", &params->mArtifactSat, 0.0f, 4.0f))
			changed = true;
		if (SliderPercent("Sharpness", &params->mArtifactSharpness, 0.0f, 1.0f))
			changed = true;
	}

	// Color matrix adjustments
	if (ImGui::CollapsingHeader("Color Matrix")) {
		if (ImGui::SliderFloat("R-Y Shift", &params->mRedShift, -22.5f, 22.5f, "%.1f deg"))
			changed = true;
		if (SliderPercent("R-Y Scale", &params->mRedScale, 0.0f, 4.0f))
			changed = true;
		if (ImGui::SliderFloat("G-Y Shift", &params->mGrnShift, -22.5f, 22.5f, "%.1f deg"))
			changed = true;
		if (SliderPercent("G-Y Scale", &params->mGrnScale, 0.0f, 4.0f))
			changed = true;
		if (ImGui::SliderFloat("B-Y Shift", &params->mBluShift, -22.5f, 22.5f, "%.1f deg"))
			changed = true;
		if (SliderPercent("B-Y Scale", &params->mBluScale, 0.0f, 4.0f))
			changed = true;
	}

	// Apply changes live
	if (changed) {
		// Clear preset tag since user has customized values
		params->mPresetTag.clear();

		if (!settings.mbUsePALParams) {
			// When shared, copy active to both
			if (isPAL)
				settings.mNTSCParams = *params;
			else
				settings.mPALParams = *params;
		}
		gtia.SetColorSettings(settings);
	}

	ImGui::Separator();

	if (ImGui::Button("Reset to Defaults")) {
		ATColorSettings defaults = gtia.GetDefaultColorSettings();
		gtia.SetColorSettings(defaults);
	}

	ImGui::End();
}
