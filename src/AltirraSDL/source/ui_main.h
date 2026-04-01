//	AltirraSDL - Dear ImGui UI layer
//	Top-level UI state and rendering interface.

#pragma once

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
class ATSimulator;
class VDVideoDisplaySDL3;

struct ATUIState {
	bool requestExit = false;
	bool fileDialogPending = false;

	// Dialog windows
	bool showSystemConfig = false;
	bool showDiskManager = false;
	bool showCassetteControl = false;
	bool showAboutDialog = false;
	bool showAdjustColors = false;
	bool showDisplaySettings = false;

	// System config sidebar selection
	int systemConfigCategory = 0;
};

bool ATUIInit(SDL_Window *window, SDL_Renderer *renderer);
void ATUIShutdown();
bool ATUIProcessEvent(const SDL_Event *event);
bool ATUIWantCaptureKeyboard();
bool ATUIWantCaptureMouse();

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	SDL_Renderer *renderer, ATUIState &state);

// MRU list (shared with main loop for file drop)
void ATAddMRU(const wchar_t *path);

// Quick save state (F7 load, F8 save)
void ATUIQuickSaveState();
void ATUIQuickLoadState();

// Dialog render functions (each in its own .cpp file)
void ATUIRenderSystemConfig(ATSimulator &sim, ATUIState &state);
void ATUIRenderDiskManager(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderCassetteControl(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderAdjustColors(ATSimulator &sim, ATUIState &state);
void ATUIRenderDisplaySettings(ATSimulator &sim, ATUIState &state);
