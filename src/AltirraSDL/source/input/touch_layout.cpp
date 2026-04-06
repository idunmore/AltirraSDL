//	AltirraSDL - Adaptive touch control layout engine
//	Calculates pixel positions for all touch controls based on screen
//	dimensions, orientation, system UI safe-area insets, and
//	user-configured control size.  All sizing uses density-independent
//	pixels (dp) scaled by contentScale.

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
	const ATTouchLayoutConfig &config,
	const ATTouchLayoutInsets &insets)
{
	layout.screenW = screenW;
	layout.screenH = screenH;
	layout.landscape = (screenW > screenH);
	layout.lastControlSize = config.controlSize;
	layout.lastContentScale = config.contentScale;
	layout.insets = insets;
	layout.lastInsets = insets;

	// dp-to-pixel conversion using display content scale
	float cs = config.contentScale;
	if (cs < 1.0f) cs = 1.0f;
	auto dp = [cs](float v) -> float { return v * cs; };

	// Scale factor based on control size setting.
	// Fire A base is 72dp (Medium), so Small=~58dp, Large=~108dp — large
	// enough to be comfortable on a phone held in landscape.
	float sizeMult = 1.0f;
	switch (config.controlSize) {
	case ATTouchControlSize::Small:  sizeMult = 0.8f; break;
	case ATTouchControlSize::Medium: sizeMult = 1.0f; break;
	case ATTouchControlSize::Large:  sizeMult = 1.5f; break;
	}

	// Console button dimensions (dp-based)
	// Width: 96dp, Height: 52dp — bumped from 72/36 on user request so
	// the START/SELECT/OPTION buttons are comfortably finger-sized even
	// at 1.0× (Medium) control size.
	float consoleBtnW = dp(96.0f * sizeMult);
	float consoleBtnH = dp(52.0f * sizeMult);
	float menuBtnSize = dp(56.0f * sizeMult);

	// Fire button dimensions — bumped from 56/48 to 72/64 dp so the
	// default size is comfortable on a high-DPI phone without needing
	// to change the setting.  Fire B is hidden in non-5200 mode
	// (see touch_controls.cpp); the slot is still laid out so the
	// fire zone stays in the same place.
	float fireASize = dp(72.0f * sizeMult);
	float fireBSize = dp(64.0f * sizeMult);

	// Joystick parameters — 72dp max radius (bumped along with fire
	// buttons so the joystick also feels proportional).
	layout.joyMaxRadius = dp(72.0f * sizeMult);
	layout.joyDeadZone  = layout.joyMaxRadius * 0.15f;

	// Top bar height — scaled to accommodate the bigger 52dp console
	// buttons with 8dp top/bottom padding.
	float topBarH = dp(68.0f);

	// Effective drawable area after safe insets.  All button pixel
	// coordinates below are computed inside [insetL, insetR] x
	// [insetT, screenH - insetB].
	float insetT = (float)insets.top;
	float insetB = (float)insets.bottom;
	float insetL = (float)insets.left;
	float insetR = (float)insets.right;

	float usableW = (float)screenW - insetL - insetR;
	if (usableW < 1.0f) usableW = 1.0f;
	float usableH = (float)screenH - insetT - insetB;
	if (usableH < 1.0f) usableH = 1.0f;

	if (layout.landscape) {
		// ---- LANDSCAPE LAYOUT ----
		// Top bar is at the very top of the usable area, below the
		// status bar.  Normalized rects are in full-screen coords,
		// so we convert insetT+topBarH to a normalized y.
		float topBarY0 = insetT;
		float topBarY1 = insetT + topBarH;
		float topBarY0N = topBarY0 / screenH;
		float topBarY1N = topBarY1 / screenH;
		float insetLN = insetL / screenW;
		float insetRN = 1.0f - insetR / screenW;
		float insetBN = 1.0f - insetB / screenH;

		// Normalized zones
		layout.topBar       = { insetLN, topBarY0N, insetRN, topBarY1N };
		layout.joystickZone = { insetLN, topBarY1N, insetLN + 0.30f, insetBN };
		layout.fireZone     = { insetRN - 0.22f, 0.40f, insetRN, insetBN };
		layout.displayArea  = { insetLN + 0.12f, topBarY1N, insetRN - 0.20f, insetBN };

		// Console keys (top-left, evenly spaced) — all y coords include insetT.
		float topCenterY = insetT + topBarH * 0.5f;
		float consoleStartX = insetL + dp(16.0f) + consoleBtnW * 0.5f;
		float consoleSpacing = consoleBtnW + dp(8.0f);

		layout.btnStart  = MakeButton(consoleStartX, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnSelect = MakeButton(consoleStartX + consoleSpacing, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnOption = MakeButton(consoleStartX + consoleSpacing * 2, topCenterY, consoleBtnW, consoleBtnH);

		// Hamburger icon (top-right)
		layout.btnMenu = MakeButton(
			(float)screenW - insetR - dp(16.0f) - menuBtnSize * 0.5f,
			topCenterY, menuBtnSize, menuBtnSize);

		// Fire buttons (right side, stacked vertically)
		float fireCenterX = (float)screenW - insetR - fireASize * 0.5f - dp(24.0f);
		float fireSpacing = dp(16.0f);
		float fireAY = (float)screenH - insetB - fireASize * 0.5f - dp(48.0f);
		float fireBY = fireAY - fireASize * 0.5f - fireSpacing - fireBSize * 0.5f;
		layout.btnFireA = MakeButton(fireCenterX, fireAY, fireASize, fireASize);
		layout.btnFireB = MakeButton(fireCenterX, fireBY, fireBSize, fireBSize);

	} else {
		// ---- PORTRAIT LAYOUT ----
		float topBarY0 = insetT;
		float topBarY1 = insetT + topBarH;
		float topBarY0N = topBarY0 / screenH;
		float topBarY1N = topBarY1 / screenH;
		float insetLN = insetL / screenW;
		float insetRN = 1.0f - insetR / screenW;
		float insetBN = 1.0f - insetB / screenH;

		// Controls occupy the bottom ~32% of the usable area.
		float controlsTopPx = (float)screenH - insetB - usableH * 0.32f;
		float controlsTopN = controlsTopPx / screenH;

		// Normalized zones
		layout.topBar       = { insetLN, topBarY0N, insetRN, topBarY1N };
		layout.joystickZone = { insetLN, controlsTopN, insetLN + 0.50f, insetBN };
		layout.fireZone     = { insetRN - 0.40f, controlsTopN, insetRN, insetBN };
		layout.displayArea  = { insetLN, topBarY1N, insetRN, controlsTopN };

		// Console keys (top, left-aligned)
		float topCenterY = insetT + topBarH * 0.5f;
		float consoleStartX = insetL + dp(12.0f) + consoleBtnW * 0.5f;
		float consoleSpacing = consoleBtnW + dp(6.0f);

		layout.btnStart  = MakeButton(consoleStartX, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnSelect = MakeButton(consoleStartX + consoleSpacing, topCenterY, consoleBtnW, consoleBtnH);
		layout.btnOption = MakeButton(consoleStartX + consoleSpacing * 2, topCenterY, consoleBtnW, consoleBtnH);

		// Hamburger icon (top-right)
		layout.btnMenu = MakeButton(
			(float)screenW - insetR - dp(12.0f) - menuBtnSize * 0.5f,
			topCenterY, menuBtnSize, menuBtnSize);

		// Fire buttons (bottom-right, stacked)
		float fireCenterX = (float)screenW - insetR - fireASize * 0.5f - dp(32.0f);
		float fireSpacing = dp(16.0f);
		float fireAY = (float)screenH - insetB - fireASize * 0.5f - dp(48.0f);
		float fireBY = fireAY - fireASize * 0.5f - fireSpacing - fireBSize * 0.5f;
		layout.btnFireA = MakeButton(fireCenterX, fireAY, fireASize, fireASize);
		layout.btnFireB = MakeButton(fireCenterX, fireBY, fireBSize, fireBSize);
	}
}
