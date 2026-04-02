//	AltirraSDL - Dear ImGui UI layer
//	Top-level UI state and rendering interface.

#pragma once

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

struct SDL_Window;
struct SDL_Renderer;
union SDL_Event;
class ATSimulator;
class VDVideoDisplaySDL3;

struct ATUIState {
	bool requestExit = false;
	bool fileDialogPending = false;
	bool showExitConfirm = false;       // Exit confirmation popup pending
	bool exitConfirmed = false;          // User confirmed exit — proceed with quit

	// Dialog windows
	bool showSystemConfig = false;
	bool showDiskManager = false;
	bool showCassetteControl = false;
	bool showAboutDialog = false;
	bool showAdjustColors = false;
	bool showDisplaySettings = false;
	bool showCartridgeMapper = false;
	bool showAudioOptions = false;
	bool showInputMappings = false;
	bool showInputSetup = false;
	bool showProfiles = false;
	bool showCommandLineHelp = false;
	bool showChangeLog = false;
	bool showCompatWarning = false;

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

// Process deferred file dialog results on main thread (call each frame)
void ATUIPollDeferredActions();

// MRU list (shared with main loop for file drop)
void ATAddMRU(const wchar_t *path);

// Quick save state (F7 load, F8 save)
void ATUIQuickSaveState();
void ATUIQuickLoadState();

// Mouse capture — SDL3 implementation (defined in uiaccessors_stubs.cpp)
void ATUISetMouseCaptureWindow(SDL_Window *window);
void ATUICaptureMouse();
void ATUIReleaseMouse();

// Deferred action types — shared between ui_main.cpp and ui_cartmapper.cpp
enum ATDeferredActionType {
	kATDeferred_BootImage,
	kATDeferred_OpenImage,
	kATDeferred_AttachCartridge,
	kATDeferred_AttachSecondaryCartridge,
	kATDeferred_AttachDisk,        // uses mInt for drive index
	kATDeferred_LoadState,
	kATDeferred_SaveState,
	kATDeferred_SaveCassette,
	kATDeferred_ExportCassetteAudio,
	kATDeferred_SaveCartridge,
	kATDeferred_SaveFirmware,      // uses mInt for firmware index
	kATDeferred_LoadCassette,
	kATDeferred_StartRecordRaw,
	kATDeferred_StartRecordWAV,
	kATDeferred_StartRecordSAP,
	kATDeferred_StartRecordVideo,  // uses mInt for ATVideoEncoding
	kATDeferred_SetCompatDBPath,
};

// Push a deferred action (thread-safe — may be called from file dialog callbacks)
void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra = 0);

// Cartridge mapper dialog (ui_cartmapper.cpp)
extern bool g_cartMapperPending;
void ATUIOpenCartridgeMapperDialog(ATDeferredActionType origAction,
	const VDStringW &path, int slot, bool coldReset,
	const vdfastvector<uint8> &capturedData, uint32 cartSize);
void ATUIRenderCartridgeMapper(ATUIState &state);

// Firmware Manager — global visibility flag and drop handler (ui_system.cpp)
extern bool g_showFirmwareManager;
bool ATUIFirmwareManagerHandleDrop(const char *utf8path);

// Dialog render functions (each in its own .cpp file)
void ATUIRenderSystemConfig(ATSimulator &sim, ATUIState &state);
void ATUIRenderDiskManager(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderCassetteControl(ATSimulator &sim, ATUIState &state, SDL_Window *window);
void ATUIRenderAdjustColors(ATSimulator &sim, ATUIState &state);
void ATUIRenderDisplaySettings(ATSimulator &sim, ATUIState &state);
void ATUIRenderInputMappings(ATSimulator &sim, ATUIState &state);
void ATUIRenderInputSetup(ATSimulator &sim, ATUIState &state);
void ATUIRenderProfiles(ATSimulator &sim, ATUIState &state);

// Device configuration dialog (ui_devconfig.cpp)
class IATDevice;
class ATDeviceManager;
void ATUIOpenDeviceConfig(IATDevice *dev, ATDeviceManager *devMgr);
bool ATUIIsDeviceConfigOpen();
void ATUICloseDeviceConfigFor(IATDevice *dev); // close if open for this device
void ATUIRenderDeviceConfig(ATDeviceManager *devMgr);

// Compatibility warning — SDL3/ImGui replacement for Windows IDD_COMPATIBILITY
void ATUICheckCompatibility(ATSimulator &sim, ATUIState &state);
void ATUIRenderCompatWarning(ATSimulator &sim, ATUIState &state);

// Exit confirmation — checks for dirty storage and shows discard dialog
// Returns true if exit should proceed immediately (nothing dirty).
// Returns false if confirmation popup was opened (check state.exitConfirmed later).
bool ATUIRequestExit(ATSimulator &sim, ATUIState &state);
void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state);
