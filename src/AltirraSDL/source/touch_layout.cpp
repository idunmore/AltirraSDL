//	AltirraSDL - Adaptive touch control layout engine
//	Calculates pixel positions for all touch controls based on screen
//	dimensions, orientation, and user-configured control size.

#include <stdafx.h>
#include "touch_layout.h"
#include <algorithm>
#include <cmath>

ATTouchRect ATTouchLayout_ToPixels(const ATTouchRect &norm, int screenW, int screenH) {
	return {
		norm.x0 * screenW,
		norm.y0 * screenH,
		norm.x1 * screenW,
		norm.y1 * screenH
	};
}

// Helper: create a pixel-coordinate button rect centered at (cx, cy) with given size
static ATTouchRect MakeButton(float cx, float cy, float w, float h) {
	return { cx - w * 0.5f, cy - h * 0.5f, cx + w * 0.5f, cy + h * 0.5f };
}

void ATTouchLayout_Update(ATTouchLayout &layout, int screenW, int screenH,
	const ATTouchLayoutConfig &config)
{
	layout.screenW = screenW;
	layout.screenH = screenH;
	layout.landscape = (screenW > screenH);

	float shortDim = (float)std::min(screenW, screenH);

	// Scale factor based on control size setting
	float sizeMult = 1.0f;
	switch (config.controlSize) {
	case ATTouchControlSize::Small:  sizeMult = 0.75f; break;
	case ATTouchControlSize::Medium: sizeMult = 1.0f;  break;
	case ATTouchControlSize::Large:  sizeMult = 1.3f;  break;
	}

	// Console button dimensions
	float consoleBtnW = shortDim * 0.12f * sizeMult;
	float consoleBtnH = shortDim * 0.06f * sizeMult;
	float menuBtnSize = shortDim * 0.07f * sizeMult;

	// Fire button dimensions
	float fireBtnSize = shortDim * 0.14f * sizeMult;

	// Joystick parameters
	layout.joyMaxRadius = shortDim * 0.12f * sizeMult;
	layout.joyDeadZone  = layout.joyMaxRadius * 0.15f;

	if (layout.landscape) {
		// ---- LANDSCAPE LAYOUT ----
		float topH = 0.08f;

		// Normalized zones
		layout.topBar       = { 0.0f, 0.0f, 1.0f, topH };
		layout.joystickZone = { 0.0f, topH, 0.35f, 1.0f };
		layout.fireZone     = { 0.78f, 0.45f, 1.0f, 1.0f };
		layout.displayArea  = { 0.12f, topH, 0.80f, 1.0f };

		// Console keys (top-left, evenly spaced)
		float topCenterY = topH * 0.5f * screenH;
		float consoleStartX = screenW * 0.02f + consoleBtnW * 0.5f;
		float consoleSpacing = consoleBtnW * 1.2f;

		layout.btnStart  = MakeButton(consoleStartX, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnSelect = MakeButton(consoleStartX + consoleSpacing, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnOption = MakeButton(consoleStartX + consoleSpacing * 2, topCenterY, consoleBtnW, consoleBtnH);

		// Hamburger icon (top-right)
		layout.btnMenu = MakeButton(screenW - menuBtnSize * 0.8f, topCenterY, menuBtnSize, menuBtnSize);

		// Fire buttons (right side, stacked vertically)
		float fireCenterX = screenW * 0.90f;
		float fireBSpacing = fireBtnSize * 1.3f;
		layout.btnFireA = MakeButton(fireCenterX, screenH * 0.78f, fireBtnSize, fireBtnSize);
		layout.btnFireB = MakeButton(fireCenterX, screenH * 0.78f - fireBSpacing, fireBtnSize, fireBtnSize);

	} else {
		// ---- PORTRAIT LAYOUT ----
		float topH = 0.06f;
		float controlsTop = 0.70f;

		// Normalized zones
		layout.topBar       = { 0.0f, 0.0f, 1.0f, topH };
		layout.joystickZone = { 0.0f, controlsTop, 0.50f, 1.0f };
		layout.fireZone     = { 0.62f, controlsTop, 1.0f, 1.0f };
		layout.displayArea  = { 0.0f, topH, 1.0f, controlsTop };

		// Console keys (top, centered)
		float topCenterY = topH * 0.5f * screenH;
		float consoleStartX = screenW * 0.02f + consoleBtnW * 0.5f;
		float consoleSpacing = consoleBtnW * 1.15f;

		layout.btnStart  = MakeButton(consoleStartX, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnSelect = MakeButton(consoleStartX + consoleSpacing, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnOption = MakeButton(consoleStartX + consoleSpacing * 2, topCenterY, consoleBtnW, consoleBtnH);

		// Hamburger icon (top-right)
		layout.btnMenu = MakeButton(screenW - menuBtnSize * 0.8f, topCenterY, menuBtnSize, menuBtnSize);

		// Fire buttons (bottom-right, stacked)
		float fireCenterX = screenW * 0.82f;
		float fireBSpacing = fireBtnSize * 1.3f;
		layout.btnFireA = MakeButton(fireCenterX, screenH * 0.88f, fireBtnSize, fireBtnSize);
		layout.btnFireB = MakeButton(fireCenterX, screenH * 0.88f - fireBSpacing, fireBtnSize, fireBtnSize);
	}
}
