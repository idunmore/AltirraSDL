//	Altirra SDL3 frontend - main entry point
//	Integrates SDL3 window, emulator core, and Dear ImGui UI.
//
//	Frame pacing:
//	  The Windows version uses a waitable timer + error accumulator to
//	  sleep between frames and hit the exact Atari frame rate (~59.92 Hz
//	  NTSC, ~49.86 Hz PAL).  We replicate this with SDL_DelayPrecise()
//	  and SDL_GetPerformanceCounter().  Vsync is still enabled but only
//	  as a secondary backstop; the primary rate limiter is our own timer.

#include <stdafx.h>
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED
#include <SDL3/SDL_main.h>
#include <stdio.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include <at/atio/image.h>

#include "display_sdl3_impl.h"
#include "input_sdl3.h"
#include "ui_main.h"

#include "simulator.h"
#include <at/ataudio/audiooutput.h>
#include "uiaccessors.h"
#include "inputmanager.h"
#include "inputdefs.h"
#include "gtia.h"
#include "joystick.h"
#include "joystick_sdl3.h"
#include "firmwaremanager.h"
#include "settings.h"

ATSimulator g_sim;
static VDVideoDisplaySDL3 *g_pDisplay = nullptr;
static SDL_Window   *g_pWindow   = nullptr;
static SDL_Renderer *g_pRenderer = nullptr;
static IATJoystickManager *g_pJoystickMgr = nullptr;
static bool g_running = true;
static bool g_winActive = true;
static ATUIState g_uiState;

// =========================================================================
// Frame pacing — matches Windows main.cpp timing architecture
// =========================================================================

// Atari frame rates (from main.cpp ATUIUpdateSpeedTiming):
//   NTSC:  262 scanlines * 114 clocks @ 1.7897725 MHz = ~59.9227 Hz
//   PAL:   312 scanlines * 114 clocks @ 1.773447  MHz = ~49.8607 Hz
//   SECAM: 312 scanlines * 114 clocks @ 1.7815    MHz = ~50.0818 Hz
static constexpr double kFrameRate_NTSC  = 59.9227;
static constexpr double kFrameRate_PAL   = 49.8607;
static constexpr double kFrameRate_SECAM = 50.0818;

struct FramePacer {
	uint64_t perfFreq;          // SDL_GetPerformanceFrequency()
	uint64_t lastFrameTime;     // perf counter at last frame presentation
	double   targetSecsPerFrame;// seconds per emulated frame
	int64_t  errorAccum;        // timing error in perf counter ticks (positive = ahead)

	void Init() {
		perfFreq = SDL_GetPerformanceFrequency();
		lastFrameTime = SDL_GetPerformanceCounter();
		errorAccum = 0;
		UpdateRate(kFrameRate_NTSC);
	}

	void UpdateRate(double fps) {
		targetSecsPerFrame = 1.0 / fps;
	}

	// Called after a frame is complete.  Sleeps to maintain correct rate.
	void WaitForNextFrame() {
		uint64_t now = SDL_GetPerformanceCounter();
		int64_t elapsed = (int64_t)(now - lastFrameTime);
		int64_t targetTicks = (int64_t)(targetSecsPerFrame * (double)perfFreq);

		// Error accumulator: positive = we finished early, need to wait.
		// Mirrors the Windows "error += lastFrameDuration - g_frameTicks"
		// logic, but with inverted sign (they track lateness, we track
		// earliness).
		errorAccum += targetTicks - elapsed;

		// Clamp error to ±2 frames to prevent runaway drift (matches the
		// Windows g_frameErrorBound = 2 * g_frameTicks).
		int64_t errorBound = 2 * targetTicks;
		if (errorAccum > errorBound || errorAccum < -errorBound)
			errorAccum = 0;

		// If we're ahead of schedule, sleep.
		if (errorAccum > 0) {
			uint64_t waitNs = (uint64_t)((double)errorAccum / (double)perfFreq * 1e9);
			if (waitNs > 1000000) // only bother sleeping > 1ms
				SDL_DelayPrecise(waitNs);
		}

		lastFrameTime = SDL_GetPerformanceCounter();
	}
};

static FramePacer g_pacer;

// =========================================================================
// Event handling
// =========================================================================

static bool g_prevImGuiCapture = false;
static bool g_prevImGuiMouseCapture = false;

static void HandleEvents() {
	// Detect when ImGui starts capturing keyboard (e.g. menu opened)
	// and release all held emulator keys to prevent stuck input.
	bool imguiCapture = ATUIWantCaptureKeyboard();
	if (imguiCapture && !g_prevImGuiCapture)
		ATInputSDL3_ReleaseAllKeys();
	g_prevImGuiCapture = imguiCapture;

	// Release mouse capture when ImGui wants the mouse (menu/dialog open)
	bool imguiMouseCapture = ATUIWantCaptureMouse();
	if (imguiMouseCapture && !g_prevImGuiMouseCapture)
		ATUIReleaseMouse();
	g_prevImGuiMouseCapture = imguiMouseCapture;

	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		ATUIProcessEvent(&ev);

		switch (ev.type) {
		case SDL_EVENT_QUIT:
		case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
			g_running = false;
			break;

		case SDL_EVENT_KEY_DOWN:
			// Right-Alt releases mouse capture (matches Windows behavior)
			if (ATUIIsMouseCaptured() && ev.key.scancode == SDL_SCANCODE_RALT) {
				ATUIReleaseMouse();
				break;
			}
			if (!ATUIWantCaptureKeyboard()) {
				if (ev.key.key == SDLK_F5 && (ev.key.mod & SDL_KMOD_SHIFT)) {
					g_sim.ColdReset();
					g_sim.Resume();
				} else if (ev.key.key == SDLK_F5 && !(ev.key.mod & SDL_KMOD_SHIFT)) {
					g_sim.WarmReset();
					g_sim.Resume();
				} else if (ev.key.key == SDLK_F9) {
					if (g_sim.IsPaused())
						g_sim.Resume();
					else
						g_sim.Pause();
				} else if (ev.key.key == SDLK_F7)
					ATUIQuickLoadState();
				else if (ev.key.key == SDLK_F8)
					ATUIQuickSaveState();
				else if (ev.key.key == SDLK_RETURN &&
					(ev.key.mod & SDL_KMOD_ALT)) {
					bool fs = (SDL_GetWindowFlags(g_pWindow) & SDL_WINDOW_FULLSCREEN) != 0;
					SDL_SetWindowFullscreen(g_pWindow, !fs);
				} else
					ATInputSDL3_HandleKeyDown(ev.key);
			}
			break;

		case SDL_EVENT_KEY_UP:
			if (!ATUIWantCaptureKeyboard()) {
				// Don't forward key-up for keys consumed as hotkeys
				// (F5, Shift+F5, F7, F8, Alt+Enter) to avoid spurious
				// ATInputManager release events.
				if (ev.key.key != SDLK_F5 && ev.key.key != SDLK_F7 &&
					ev.key.key != SDLK_F8 && ev.key.key != SDLK_F9)
					ATInputSDL3_HandleKeyUp(ev.key);
			}
			break;

		case SDL_EVENT_MOUSE_BUTTON_DOWN:
			if (!ATUIWantCaptureMouse()) {
				// Middle-click releases mouse capture when MMB isn't mapped
				// as an input (matches Windows behavior).
				if (ev.button.button == SDL_BUTTON_MIDDLE &&
					ATUIIsMouseCaptured()) {
					ATInputManager *im = g_sim.GetInputManager();
					if (!im || !im->IsInputMapped(0, kATInputCode_MouseMMB))
						ATUIReleaseMouse();
					break;
				}

				// Auto-capture: on left click, capture the mouse if auto-capture
				// is enabled and mouse is mapped.  The click is consumed (not
				// forwarded to input manager) — matches Windows behavior.
				if (ev.button.button == SDL_BUTTON_LEFT &&
					ATUIGetMouseAutoCapture() &&
					!ATUIIsMouseCaptured()) {
					ATUICaptureMouse();
				}
			}
			break;

		case SDL_EVENT_GAMEPAD_ADDED:
			if (g_pJoystickMgr)
				g_pJoystickMgr->RescanForDevices();
			break;

		case SDL_EVENT_GAMEPAD_REMOVED:
			// RescanForDevices only adds new gamepads.  For removal,
			// the SDL3-specific manager exposes CloseGamepad().
			if (g_pJoystickMgr)
				static_cast<ATJoystickManagerSDL3 *>(g_pJoystickMgr)->CloseGamepad(ev.gdevice.which);
			break;

		case SDL_EVENT_DROP_FILE: {
			const char *file = ev.drop.data;
			if (file) {
				VDStringW widePath = VDTextU8ToW(file, -1);
				ATImageLoadContext ctx {};
				if (g_sim.Load(widePath.c_str(), kATMediaWriteMode_VRWSafe, &ctx)) {
					ATAddMRU(widePath.c_str());
					g_sim.ColdReset();
					g_sim.Resume();
				} else
					fprintf(stderr, "Warning: Could not load dropped file '%s'\n", file);
			}
			break;
		}

		case SDL_EVENT_WINDOW_FOCUS_GAINED:
			g_winActive = true;
			break;

		case SDL_EVENT_WINDOW_FOCUS_LOST:
			g_winActive = false;
			// Release all held keys/buttons to prevent stuck input
			ATInputSDL3_ReleaseAllKeys();
			// Release mouse capture on focus loss
			ATUIReleaseMouse();
			break;

		default:
			break;
		}
	}
}

// =========================================================================
// Rendering
// =========================================================================

static void RenderAndPresent() {
	SDL_SetRenderDrawColor(g_pRenderer, 0, 0, 0, 255);
	SDL_RenderClear(g_pRenderer);

	// Draw emulator frame texture with blending disabled.
	// ImGui's SDLRenderer3 backend leaves SDL_BLENDMODE_BLEND active.
	SDL_Texture *emuTex = g_pDisplay->GetTexture();
	if (emuTex) {
		SDL_SetTextureBlendMode(emuTex, SDL_BLENDMODE_NONE);
		SDL_RenderTexture(g_pRenderer, emuTex, nullptr, nullptr);
	}

	// Draw ImGui UI on top
	ATUIRenderFrame(g_sim, *g_pDisplay, g_pRenderer, g_uiState);

	SDL_RenderPresent(g_pRenderer);
}

// =========================================================================
// Update frame pacer rate from current video standard
// =========================================================================

static double GetBaseFrameRate() {
	switch (g_sim.GetVideoStandard()) {
	case kATVideoStandard_PAL:
	case kATVideoStandard_NTSC50:	// NTSC color at 50Hz → PAL timing
		return kFrameRate_PAL;
	case kATVideoStandard_SECAM:
		return kFrameRate_SECAM;
	case kATVideoStandard_PAL60:	// PAL color at 60Hz → NTSC timing
	default:
		return kFrameRate_NTSC;
	}
}

static void UpdatePacerRate() {
	double rate = GetBaseFrameRate();

	// Apply speed modifier: 0 = 1x, 1 = 2x, 3 = 4x, 7 = 8x
	float spd = ATUIGetSpeedModifier();
	double speedFactor = (double)spd + 1.0;
	if (ATUIGetSlowMotion())
		speedFactor *= 0.25;

	if (speedFactor < 0.01) speedFactor = 0.01;
	if (speedFactor > 100.0) speedFactor = 100.0;

	g_pacer.UpdateRate(rate * speedFactor);

	// Update audio output resampling rate to match speed.
	// This mirrors Windows main.cpp line 537.
	IATAudioOutput *audio = g_sim.GetAudioOutput();
	if (audio) {
		double cyclesPerSecond = g_sim.GetScheduler()->GetRate().asDouble();
		if (cyclesPerSecond <= 0)
			cyclesPerSecond = 1789772.5;

		audio->SetCyclesPerSecond(cyclesPerSecond, 1.0 / speedFactor);
	}
}

// =========================================================================
// Main
// =========================================================================

int main(int argc, char *argv[]) {
	fprintf(stderr, "[AltirraSDL] Starting...\n");

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return 1;
	}

	VDRegistryAppKey::setDefaultKey("AltirraSDL");

	// Load persisted settings from ~/.config/altirra/settings.ini
	extern void ATRegistryLoadFromDisk();
	ATRegistryLoadFromDisk();

	const int kScale = 2;
	g_pWindow = SDL_CreateWindow("AltirraSDL", 384*kScale, 240*kScale, SDL_WINDOW_RESIZABLE);
	if (!g_pWindow) { fprintf(stderr, "CreateWindow: %s\n", SDL_GetError()); SDL_Quit(); return 1; }

	g_pRenderer = SDL_CreateRenderer(g_pWindow, nullptr);
	if (!g_pRenderer) { fprintf(stderr, "CreateRenderer: %s\n", SDL_GetError()); SDL_DestroyWindow(g_pWindow); SDL_Quit(); return 1; }

	SDL_SetRenderVSync(g_pRenderer, 1);

	if (!ATUIInit(g_pWindow, g_pRenderer)) {
		SDL_DestroyRenderer(g_pRenderer);
		SDL_DestroyWindow(g_pWindow);
		SDL_Quit();
		return 1;
	}

	// Give the mouse capture system access to the window
	ATUISetMouseCaptureWindow(g_pWindow);

	g_pDisplay = new VDVideoDisplaySDL3(g_pRenderer, 384*kScale, 240*kScale);

	fprintf(stderr, "[AltirraSDL] Initializing simulator...\n");
	g_sim.Init();
	g_sim.LoadROMs();

	g_sim.GetGTIA().SetVideoOutput(g_pDisplay);

	// Initialize joystick manager before loading settings — the Input
	// settings category needs GetJoystickManager() to read transforms.
	g_pJoystickMgr = ATCreateJoystickManager();
	if (g_pJoystickMgr->Init(nullptr, g_sim.GetInputManager()))
		g_sim.SetJoystickManager(g_pJoystickMgr);
	else {
		delete g_pJoystickMgr;
		g_pJoystickMgr = nullptr;
	}

	ATInputSDL3_Init(&g_sim.GetPokey(), g_sim.GetInputManager(), &g_sim.GetGTIA());

	// Load emulator settings using the same code path as Windows.
	// On first run (no config file), VDRegistryKey returns defaults for all
	// keys, which matches a fresh Windows install.
	ATLoadSettings((ATSettingsCategory)(
		kATSettingsCategory_Hardware
		| kATSettingsCategory_Firmware
		| kATSettingsCategory_Acceleration
		| kATSettingsCategory_Debugging
		| kATSettingsCategory_View
		| kATSettingsCategory_Color
		| kATSettingsCategory_Sound
		| kATSettingsCategory_Boot
		| kATSettingsCategory_Environment
		| kATSettingsCategory_Speed
		| kATSettingsCategory_StartupConfig
		| kATSettingsCategory_FullScreen
		| kATSettingsCategory_Input
		| kATSettingsCategory_InputMaps
	));

	// Create the native audio device now that settings have been loaded
	// (SetApi, SetLatency, etc. may have been called during ATLoadSettings).
	g_sim.GetAudioOutput()->InitNativeAudio();

	if (argc > 1) {
		VDStringW widePath = VDTextU8ToW(argv[1], -1);
		ATImageLoadContext ctx {};
		if (!g_sim.Load(widePath.c_str(), kATMediaWriteMode_RO, &ctx))
			fprintf(stderr, "Warning: Could not load '%s'\n", argv[1]);
	}

	g_sim.ColdReset();
	g_sim.Resume();

	g_pacer.Init();
	UpdatePacerRate();

	// Present once immediately so compositors show the window.
	RenderAndPresent();

	fprintf(stderr, "[AltirraSDL] Entering main loop\n");

	// Main loop.  Mirrors the Windows idle handler structure:
	//
	// 1. Advance() runs emulation for up to g_ATSimScanlinesPerAdvance
	//    scanlines (default 32).  A full NTSC frame is 262 scanlines,
	//    so ~9 Advance() calls per frame.
	//
	// 2. When Advance() returns kAdvanceResult_WaitingForFrame, GTIA
	//    has completed a frame.  We upload it, present, and then SLEEP
	//    until it's time for the next frame (frame pacing).
	//
	// 3. The Windows version sleeps via MsgWaitForMultipleObjects or a
	//    waitable timer.  We use SDL_DelayPrecise() for equivalent
	//    nanosecond-precision sleep.
	//
	// Without this sleep, the emulator runs Advance() as fast as the
	// CPU allows, producing frames far faster than 60 Hz.

	while (g_running) {
		HandleEvents();
		if (!g_running) break;

		// Process deferred file dialog results on main thread
		ATUIPollDeferredActions();

		// Pause emulation when window loses focus (if enabled).
		const bool pauseInactive = ATUIGetPauseWhenInactive() && !g_winActive;

		if (pauseInactive) {
			// Window inactive — render for UI responsiveness, sleep.
			RenderAndPresent();
			SDL_Delay(16);
			continue;
		}

		ATSimulator::AdvanceResult result = g_sim.Advance(false);

		// Check if a new frame arrived (GTIA called PostBuffer).
		// We must present whenever a frame is ready, regardless of
		// Advance() return value.  GTIA's PostBuffer and BeginFrame
		// can both happen inside a single Advance() call — the next
		// frame may already be in progress when Advance() returns
		// kAdvanceResult_Running.  The original main loop called
		// Present() on every Advance() result for this reason.
		bool hadFrame = g_pDisplay->IsFramePending();
		g_pDisplay->PrepareFrame();

		const bool turbo = g_sim.IsTurboModeEnabled();

		if (hadFrame) {
			// A frame was uploaded — present it and pace.
			RenderAndPresent();

			// Sync pacer rate and audio rate with current speed settings.
			// Cheap — just reads a few values and updates if changed.
			UpdatePacerRate();

			// In turbo mode, skip frame pacing to run as fast as possible.
			if (!turbo)
				g_pacer.WaitForNextFrame();
		} else if (result == ATSimulator::kAdvanceResult_WaitingForFrame) {
			// GTIA is blocked but we had no frame to show.
			// Present anyway to keep UI responsive.
			RenderAndPresent();
			if (!turbo)
				g_pacer.WaitForNextFrame();
		} else if (result == ATSimulator::kAdvanceResult_Stopped) {
			// Paused/stopped — render for UI, sleep to avoid busy-wait.
			RenderAndPresent();
			SDL_Delay(16);
		}
		// kAdvanceResult_Running with no frame: loop immediately.
	}

	g_sim.GetGTIA().SetVideoOutput(nullptr);

	// Detach and destroy joystick manager before simulator shutdown
	if (g_pJoystickMgr) {
		g_sim.SetJoystickManager(nullptr);
		g_pJoystickMgr->Shutdown();
		delete g_pJoystickMgr;
		g_pJoystickMgr = nullptr;
	}

	// Release mouse capture before shutdown
	ATUIReleaseMouse();

	// Save settings before shutdown
	extern void ATRegistryFlushToDisk();
	try {
		ATSaveSettings((ATSettingsCategory)(
			kATSettingsCategory_Hardware
			| kATSettingsCategory_Firmware
			| kATSettingsCategory_Acceleration
			| kATSettingsCategory_Debugging
			| kATSettingsCategory_View
			| kATSettingsCategory_Color
			| kATSettingsCategory_Sound
			| kATSettingsCategory_Boot
			| kATSettingsCategory_Environment
			| kATSettingsCategory_Speed
			| kATSettingsCategory_StartupConfig
			| kATSettingsCategory_FullScreen
			| kATSettingsCategory_Input
			| kATSettingsCategory_InputMaps
		));
		ATRegistryFlushToDisk();
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Warning: failed to save settings on exit\n");
	}

	g_sim.Shutdown();

	ATUIShutdown();

	delete g_pDisplay;
	SDL_DestroyRenderer(g_pRenderer);
	SDL_DestroyWindow(g_pWindow);
	SDL_Quit();
	return 0;
}
