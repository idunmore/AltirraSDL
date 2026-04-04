//	AltirraSDL - Calibration screen dialog
//	Replicates ATUICalibrationScreen from Windows Altirra using ImGui.
//	Panels: Black/White Levels, Gradient.
//	(HDR panels omitted — SDL3 backend does not support HDR output.)

#include <stdafx.h>
#include <imgui.h>
#include <cmath>
#include "ui_main.h"

// Panel IDs
enum CalibrationPanel {
	kCalPanel_BlackWhiteLevels = 0,
	kCalPanel_Gradient,
	kCalPanel_Count
};

static const char *kPanelNames[] = {
	"Black/white levels",
	"Gradient",
};

static int s_selectedPanel = 0;
static float s_blinkTimer = 0.0f;
static bool s_blinkOn = true;

// =========================================================================
// Panel: Black/White Levels
// Matches ATUICalibrationPanelBlackWhiteLevels from uicalibrationscreen.cpp
// =========================================================================

static void RenderBlackWhiteLevels(ImDrawList *dl, ImVec2 origin, float w, float h) {
	// Update blink timer (0.5s toggle)
	ImGuiIO &io = ImGui::GetIO();
	s_blinkTimer += io.DeltaTime;
	if (s_blinkTimer >= 0.5f) {
		s_blinkTimer -= 0.5f;
		s_blinkOn = !s_blinkOn;
	}

	const int numBlocks = 16;
	const float blockSpacing = 20.0f;
	float blockSize = std::min(h * 0.2f, (w - blockSpacing * (numBlocks - 1)) / numBlocks);
	if (blockSize < 8.0f) blockSize = 8.0f;
	float inset = blockSize / 4.0f;

	float totalW = numBlocks * blockSize + (numBlocks - 1) * blockSpacing;
	float startX = origin.x + (w - totalW) * 0.5f;

	// Black levels strip (top 25%)
	{
		float stripY = origin.y + h * 0.08f;
		for (int i = 0; i < numBlocks; i++) {
			float bx = startX + i * (blockSize + blockSpacing);

			// Outer block: level i (0..15)
			uint8_t v = (uint8_t)i;
			ImU32 outerCol = IM_COL32(v, v, v, 255);
			dl->AddRectFilled(ImVec2(bx, stripY), ImVec2(bx + blockSize, stripY + blockSize), outerCol);

			// Blinking inner square at level 0 (pure black) — visible only if
			// display black level is correct
			if (s_blinkOn) {
				dl->AddRectFilled(
					ImVec2(bx + inset, stripY + inset),
					ImVec2(bx + blockSize - inset, stripY + blockSize - inset),
					IM_COL32(0, 0, 0, 255));
			}

			// Level number below block
			char label[8];
			snprintf(label, sizeof(label), "%d", i);
			ImVec2 tsz = ImGui::CalcTextSize(label);
			dl->AddText(ImVec2(bx + (blockSize - tsz.x) * 0.5f, stripY + blockSize + 4),
				IM_COL32(200, 200, 200, 255), label);
		}
	}

	// Instructions (center)
	{
		const char *text1 = "Adjust your display's black level (brightness) until the inner squares";
		const char *text2 = "in the top strip are just barely visible. Differences at levels 0-1 are very faint.";
		const char *text3 = "Adjust contrast until the inner squares in the bottom strip are visible.";
		const char *text4 = "Differences at levels 254-255 are very faint.";

		float ty = origin.y + h * 0.38f;
		float lineH = ImGui::GetTextLineHeightWithSpacing();

		auto centerText = [&](const char *t, float y) {
			ImVec2 sz = ImGui::CalcTextSize(t);
			dl->AddText(ImVec2(origin.x + (w - sz.x) * 0.5f, y), IM_COL32(255, 255, 255, 255), t);
		};

		centerText(text1, ty);
		centerText(text2, ty + lineH);
		centerText(text3, ty + lineH * 3);
		centerText(text4, ty + lineH * 4);
	}

	// White levels strip (bottom 25%)
	{
		float stripY = origin.y + h * 0.68f;
		for (int i = 0; i < numBlocks; i++) {
			float bx = startX + i * (blockSize + blockSpacing);

			// Outer block: level 239+i (239..254)
			uint8_t v = (uint8_t)(239 + i);
			ImU32 outerCol = IM_COL32(v, v, v, 255);
			dl->AddRectFilled(ImVec2(bx, stripY), ImVec2(bx + blockSize, stripY + blockSize), outerCol);

			// Blinking inner square at level 255 (pure white)
			if (s_blinkOn) {
				dl->AddRectFilled(
					ImVec2(bx + inset, stripY + inset),
					ImVec2(bx + blockSize - inset, stripY + blockSize - inset),
					IM_COL32(255, 255, 255, 255));
			}

			// Level number below block
			char label[8];
			snprintf(label, sizeof(label), "%d", 239 + i);
			ImVec2 tsz = ImGui::CalcTextSize(label);
			dl->AddText(ImVec2(bx + (blockSize - tsz.x) * 0.5f, stripY + blockSize + 4),
				IM_COL32(200, 200, 200, 255), label);
		}
	}
}

// =========================================================================
// Panel: Gradient
// Matches ATUICalibrationPanelGradient from uicalibrationscreen.cpp
// =========================================================================

static void RenderGradient(ImDrawList *dl, ImVec2 origin, float w, float h) {
	// Four gradient bands: Red, Green, Blue, Grayscale
	struct GradientBand {
		uint8_t rBase, gBase, bBase;
		const char *label;
	};
	static const GradientBand bands[] = {
		{1, 0, 0, "Red"},
		{0, 1, 0, "Green"},
		{0, 0, 1, "Blue"},
		{1, 1, 1, "Grayscale"},
	};

	float bandH = h * 0.4f / 4.0f;
	float bandGap = 4.0f;
	float startY = origin.y + 20.0f;

	float xstep = std::max(1.0f, w / 256.0f);

	for (int b = 0; b < 4; b++) {
		float by = startY + b * (bandH + bandGap);

		// Label
		dl->AddText(ImVec2(origin.x, by - 16), IM_COL32(200, 200, 200, 255), bands[b].label);

		// Draw 256 color steps
		for (int i = 0; i < 256; i++) {
			float x0 = origin.x + i * xstep;
			float x1 = x0 + xstep;

			uint8_t r = (uint8_t)(bands[b].rBase * i);
			uint8_t g = (uint8_t)(bands[b].gBase * i);
			uint8_t bv = (uint8_t)(bands[b].bBase * i);

			ImU32 col = IM_COL32(r, g, bv, 255);

			// Alternating height pattern (top/bottom halves) to show banding
			float halfH = bandH * 0.5f;
			if (i & 1) {
				dl->AddRectFilled(ImVec2(x0, by), ImVec2(x1, by + halfH), col);
			} else {
				dl->AddRectFilled(ImVec2(x0, by), ImVec2(x1, by + bandH), col);
			}
		}
	}

	// Instructions below gradients
	float ty = startY + 4 * (bandH + bandGap) + 20;
	const char *text1 = "Each gradient should show a smooth ramp from black to full brightness.";
	const char *text2 = "Visible banding or steps indicate reduced color bit depth (6-bit panel).";
	const char *text3 = "On an 8-bit panel, the alternating-height pattern should be barely visible.";

	float lineH = ImGui::GetTextLineHeightWithSpacing();
	auto centerText = [&](const char *t, float y) {
		ImVec2 sz = ImGui::CalcTextSize(t);
		dl->AddText(ImVec2(origin.x + (w - sz.x) * 0.5f, y), IM_COL32(255, 255, 255, 255), t);
	};

	centerText(text1, ty);
	centerText(text2, ty + lineH);
	centerText(text3, ty + lineH * 2);
}

// =========================================================================
// Main calibration dialog
// =========================================================================

void ATUIRenderCalibrationDialog(ATUIState &state) {
	ImGuiIO &io = ImGui::GetIO();
	float winW = io.DisplaySize.x * 0.85f;
	float winH = io.DisplaySize.y * 0.85f;

	ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showCalibrate;
	if (!ImGui::Begin("Calibrate", &open,
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		state.showCalibrate = open;
		return;
	}

	if (ATUICheckEscClose())
		open = false;

	// Left panel: list of calibration pages
	float listW = 180.0f;
	ImGui::BeginChild("CalPanelList", ImVec2(listW, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
	for (int i = 0; i < kCalPanel_Count; i++) {
		if (ImGui::Selectable(kPanelNames[i], s_selectedPanel == i))
			s_selectedPanel = i;
	}
	ImGui::EndChild();

	ImGui::SameLine();

	// Right panel: calibration pattern drawing area
	ImGui::BeginChild("CalPanel", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_Borders);
	{
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImDrawList *dl = ImGui::GetWindowDrawList();

		// Black background for the pattern area
		dl->AddRectFilled(pos, ImVec2(pos.x + avail.x, pos.y + avail.y), IM_COL32(0, 0, 0, 255));

		switch (s_selectedPanel) {
			case kCalPanel_BlackWhiteLevels:
				RenderBlackWhiteLevels(dl, pos, avail.x, avail.y);
				break;
			case kCalPanel_Gradient:
				RenderGradient(dl, pos, avail.x, avail.y);
				break;
		}
	}
	ImGui::EndChild();

	// OK button at bottom-right
	float buttonW = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonW - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonW, 0)))
		open = false;

	ImGui::End();
	state.showCalibrate = open;
}
