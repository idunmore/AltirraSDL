//	AltirraSDL - Dear ImGui UI layer
//	Implements menu bar, status overlay, and render orchestration.
//	Dialog windows are in separate files (ui_system.cpp, ui_disk.cpp, etc.)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include <at/atcore/device.h>
#include <at/atio/image.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cartridgetypes.h>

#include <vd2/system/file.h>
#include <at/atio/cassetteimage.h>

#include <at/atcore/serializable.h>

#include "ui_main.h"
#include "display_sdl3_impl.h"
#include "simulator.h"
#include "gtia.h"
#include "cartridge.h"
#include "cassette.h"
#include "diskinterface.h"
#include "constants.h"
#include "audiowriter.h"
#include "sapwriter.h"
#include "simeventmanager.h"
#include <at/ataudio/pokey.h>
#include "uiaccessors.h"
#include "uitypes.h"
#include <vd2/system/strutil.h>
#include "inputmanager.h"
#include "inputmap.h"

extern ATSimulator g_sim;

// =========================================================================
// Deferred action queue — thread-safe handoff from file dialog callbacks
//
// SDL3 file dialog callbacks may run on a background thread (platform-
// dependent).  All simulator mutations must happen on the main thread.
// Callbacks push VDStringW paths here; the main loop drains the queue
// each frame via ATUIPollDeferredActions().
// =========================================================================

#include <mutex>
#include <vector>

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
};

struct ATDeferredAction {
	ATDeferredActionType type;
	VDStringW path;
	int mInt = 0;
};

static std::mutex g_deferredMutex;
static std::vector<ATDeferredAction> g_deferredActions;

static void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra = 0) {
	ATDeferredAction action;
	action.type = type;
	action.path = VDTextU8ToW(utf8path, -1);
	action.mInt = extra;

	std::lock_guard<std::mutex> lock(g_deferredMutex);
	g_deferredActions.push_back(std::move(action));
}

// Called from main loop each frame — processes deferred file dialog results.
void ATUIPollDeferredActions();

// =========================================================================
// MRU (Most Recently Used) list — same registry format as Windows
// =========================================================================

void ATAddMRU(const wchar_t *path) {
	VDRegistryAppKey key("MRU List", true);

	VDStringW order;
	key.getString("Order", order);

	// Check if already in list — if so, promote to front
	VDStringW existing;
	for (size_t i = 0; i < order.size(); ++i) {
		char kn[2] = { (char)order[i], 0 };
		key.getString(kn, existing);
		if (existing.comparei(path) == 0) {
			// Promote: move to front
			wchar_t c = order[i];
			order.erase(i, 1);
			order.insert(order.begin(), c);
			key.setString("Order", order.c_str());
			return;
		}
	}

	// Not found — add new entry (recycle oldest if at 10)
	int slot = 0;
	if (order.size() >= 10) {
		wchar_t c = order.back();
		if (c >= L'A' && c < L'A' + 10)
			slot = c - L'A';
		order.resize(9);
	} else {
		slot = (int)order.size();
	}

	order.insert(order.begin(), L'A' + slot);
	char kn[2] = { (char)('A' + slot), 0 };
	key.setString(kn, path);
	key.setString("Order", order.c_str());
}

static VDStringW ATGetMRU(uint32 index) {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	VDStringW s;
	if (index < order.size()) {
		char kn[2] = { (char)order[index], 0 };
		key.getString(kn, s);
	}
	return s;
}

static uint32 ATGetMRUCount() {
	VDRegistryAppKey key("MRU List", false);
	VDStringW order;
	key.getString("Order", order);
	return (uint32)order.size();
}

static void ATClearMRU() {
	VDRegistryAppKey key("MRU List", true);
	key.removeValue("Order");
}

// =========================================================================
// File dialog callbacks (SDL3 async)
// =========================================================================

static void BootImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_BootImage, filelist[0]);
}

static void OpenImageCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_OpenImage, filelist[0]);
}

static void CartridgeAttachCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_AttachCartridge, filelist[0]);
}

// Per-drive attach callback — drive index in userdata
static void AttachDiskCallback(void *userdata, const char * const *filelist, int) {
	int driveIdx = (int)(intptr_t)userdata;
	if (!filelist || !filelist[0] || driveIdx < 0 || driveIdx >= 15) return;
	ATUIPushDeferred(kATDeferred_AttachDisk, filelist[0], driveIdx);
}

static void CassetteSaveCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveCassette, filelist[0]);
}

// =========================================================================
// State save/load
// =========================================================================

static vdrefptr<IATSerializable> g_pQuickSaveState;

static void SaveStateCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_SaveState, filelist[0]);
}

static void LoadStateCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	ATUIPushDeferred(kATDeferred_LoadState, filelist[0]);
}

void ATUIQuickSaveState() {
	try {
		vdrefptr<IATSerializable> info;
		g_sim.CreateSnapshot(~g_pQuickSaveState, ~info);
		fprintf(stderr, "[AltirraSDL] Quick save state created\n");
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Quick save failed\n");
	}
}

void ATUIQuickLoadState() {
	if (!g_pQuickSaveState)
		return;
	try {
		g_sim.ApplySnapshot(*g_pQuickSaveState, nullptr);
		g_sim.Resume();
		fprintf(stderr, "[AltirraSDL] Quick load state applied\n");
	} catch (...) {
		fprintf(stderr, "[AltirraSDL] Quick load failed\n");
	}
}

// =========================================================================
// Audio recording state
// =========================================================================

static ATAudioWriter *g_pAudioWriter = nullptr;
static IATSAPWriter *g_pSAPWriter = nullptr;

static bool ATUIIsRecording() {
	return g_pAudioWriter || g_pSAPWriter;
}

static void ATUIStartAudioRecording(const wchar_t *path, bool raw) {
	if (ATUIIsRecording()) return;

	bool stereo = g_sim.IsDualPokeysEnabled();
	bool pal = (g_sim.GetVideoStandard() == kATVideoStandard_PAL);

	try {
		g_pAudioWriter = new ATAudioWriter(path, raw, stereo, pal, nullptr);
		g_sim.GetAudioOutput()->SetAudioTap(g_pAudioWriter);
		fprintf(stderr, "[AltirraSDL] Audio recording started (%s, %s)\n",
			raw ? "raw" : "WAV", stereo ? "stereo" : "mono");
	} catch (...) {
		delete g_pAudioWriter;
		g_pAudioWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start audio recording\n");
	}
}

static void ATUIStartSAPRecording(const wchar_t *path) {
	if (ATUIIsRecording()) return;

	bool pal = (g_sim.GetVideoStandard() == kATVideoStandard_PAL);

	try {
		g_pSAPWriter = ATCreateSAPWriter();
		g_pSAPWriter->Init(g_sim.GetEventManager(), &g_sim.GetPokey(), nullptr, path, pal);
		fprintf(stderr, "[AltirraSDL] SAP recording started\n");
	} catch (...) {
		delete g_pSAPWriter;
		g_pSAPWriter = nullptr;
		fprintf(stderr, "[AltirraSDL] Failed to start SAP recording\n");
	}
}

static void ATUIStopRecording() {
	bool wasRecording = ATUIIsRecording();

	if (g_pAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		try {
			g_pAudioWriter->Finalize();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing audio recording\n");
		}
		delete g_pAudioWriter;
		g_pAudioWriter = nullptr;
	}

	if (g_pSAPWriter) {
		try {
			g_pSAPWriter->Shutdown();
		} catch (...) {
			fprintf(stderr, "[AltirraSDL] Error finalizing SAP recording\n");
		}
		delete g_pSAPWriter;
		g_pSAPWriter = nullptr;
	}

	if (wasRecording)
		fprintf(stderr, "[AltirraSDL] Recording stopped\n");
}

// =========================================================================
// Deferred action execution (main thread only)
// =========================================================================

void ATUIPollDeferredActions() {
	std::vector<ATDeferredAction> actions;
	{
		std::lock_guard<std::mutex> lock(g_deferredMutex);
		actions.swap(g_deferredActions);
	}

	for (auto& a : actions) {
		try {
			switch (a.type) {
			case kATDeferred_BootImage: {
				ATImageLoadContext ctx {};
				if (g_sim.Load(a.path.c_str(), kATMediaWriteMode_RO, &ctx)) {
					ATAddMRU(a.path.c_str());
					g_sim.ColdReset();
					g_sim.Resume();
				}
				break;
			}
			case kATDeferred_OpenImage: {
				ATImageLoadContext ctx {};
				if (g_sim.Load(a.path.c_str(), kATMediaWriteMode_RO, &ctx)) {
					ATAddMRU(a.path.c_str());
					g_sim.Resume();
				}
				break;
			}
			case kATDeferred_AttachCartridge: {
				ATCartLoadContext ctx {};
				if (g_sim.LoadCartridge(0, a.path.c_str(), &ctx)) {
					g_sim.ColdReset();
					g_sim.Resume();
				}
				break;
			}
			case kATDeferred_AttachSecondaryCartridge: {
				ATCartLoadContext ctx {};
				if (g_sim.LoadCartridge(1, a.path.c_str(), &ctx)) {
					g_sim.ColdReset();
					g_sim.Resume();
				}
				break;
			}
			case kATDeferred_AttachDisk: {
				int idx = a.mInt;
				if (idx >= 0 && idx < 15)
					g_sim.GetDiskInterface(idx).LoadDisk(a.path.c_str());
				break;
			}
			case kATDeferred_LoadState: {
				ATImageLoadContext ctx {};
				if (g_sim.Load(a.path.c_str(), kATMediaWriteMode_RO, &ctx))
					g_sim.Resume();
				break;
			}
			case kATDeferred_SaveState:
				g_sim.SaveState(a.path.c_str());
				break;
			case kATDeferred_SaveCassette: {
				IATCassetteImage *image = g_sim.GetCassette().GetImage();
				if (image) {
					VDFileStream fs(a.path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					ATSaveCassetteImageCAS(fs, image);
					g_sim.GetCassette().SetImagePersistent(a.path.c_str());
					g_sim.GetCassette().SetImageClean();
				}
				break;
			}
			case kATDeferred_ExportCassetteAudio: {
				IATCassetteImage *image = g_sim.GetCassette().GetImage();
				if (image) {
					VDFileStream f(a.path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					ATSaveCassetteImageWAV(f, image);
					g_sim.GetCassette().SetImageClean();
				}
				break;
			}
			case kATDeferred_SaveCartridge: {
				ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
				if (cart && cart->GetMode()) {
					VDStringA u8 = VDTextWToU8(a.path);
					const char *ext = strrchr(u8.c_str(), '.');
					bool includeHeader = true;
					if (ext && (strcasecmp(ext, ".bin") == 0 || strcasecmp(ext, ".rom") == 0))
						includeHeader = false;
					cart->Save(a.path.c_str(), includeHeader);
				}
				break;
			}
			case kATDeferred_SaveFirmware:
				g_sim.SaveStorage((ATStorageId)(kATStorageId_Firmware + a.mInt), a.path.c_str());
				break;
			case kATDeferred_LoadCassette:
				g_sim.GetCassette().Load(a.path.c_str());
				break;
			case kATDeferred_StartRecordRaw:
				ATUIStartAudioRecording(a.path.c_str(), true);
				break;
			case kATDeferred_StartRecordWAV:
				ATUIStartAudioRecording(a.path.c_str(), false);
				break;
			case kATDeferred_StartRecordSAP:
				ATUIStartSAPRecording(a.path.c_str());
				break;
			}
		} catch (...) {
			VDStringA u8 = VDTextWToU8(a.path);
			fprintf(stderr, "[AltirraSDL] Deferred action %d failed for: %s\n", a.type, u8.c_str());
		}
	}
}

// =========================================================================
// Init / Shutdown
// =========================================================================

bool ATUIInit(SDL_Window *window, SDL_Renderer *renderer) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.IniFilename = "altirrasdl_imgui.ini";

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.FrameRounding = 2.0f;
	style.WindowRounding = 4.0f;
	style.GrabRounding = 2.0f;

	if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
		fprintf(stderr, "[AltirraSDL] ImGui SDL3 init failed\n");
		return false;
	}
	if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
		fprintf(stderr, "[AltirraSDL] ImGui SDLRenderer3 init failed\n");
		return false;
	}

	fprintf(stderr, "[AltirraSDL] ImGui initialized (docking enabled)\n");
	return true;
}

void ATUIShutdown() {
	ATUIStopRecording();
	ImGui_ImplSDLRenderer3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

bool ATUIProcessEvent(const SDL_Event *event) {
	return ImGui_ImplSDL3_ProcessEvent(event);
}

bool ATUIWantCaptureKeyboard() { return ImGui::GetIO().WantCaptureKeyboard; }
bool ATUIWantCaptureMouse() { return ImGui::GetIO().WantCaptureMouse; }

// =========================================================================
// File dialog filters
// =========================================================================

static const SDL_DialogFileFilter kImageFilters[] = {
	{ "Atari Images", "atr;xfd;dcm;xex;obx;com;bin;rom;car;cas;wav;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kCartFilters[] = {
	{ "Cartridge Images", "car;rom;bin" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kDiskAttachFilters[] = {
	{ "Disk Images", "atr;xfd;dcm;pro;atx;gz;zip;atz" },
	{ "All Files", "*" },
};

static const SDL_DialogFileFilter kCasSaveFilters[] = {
	{ "Cassette Images", "cas" },
	{ "All Files", "*" },
};

// =========================================================================
// File menu
// =========================================================================

static void RenderFileMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window) {
	if (ImGui::MenuItem("Boot Image...", "Ctrl+O"))
		SDL_ShowOpenFileDialog(BootImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);

	if (ImGui::MenuItem("Open Image..."))
		SDL_ShowOpenFileDialog(OpenImageCallback, nullptr, window, kImageFilters, 2, nullptr, false);

	// Recently Booted (MRU list)
	uint32 mruCount = ATGetMRUCount();
	if (ImGui::BeginMenu("Recently Booted", mruCount > 0)) {
		for (uint32 i = 0; i < mruCount; ++i) {
			VDStringW wpath = ATGetMRU(i);
			if (wpath.empty()) continue;
			VDStringA u8 = VDTextWToU8(wpath);
			const char *base = strrchr(u8.c_str(), '/');
			if (!base) base = strrchr(u8.c_str(), '\\');
			const char *label = base ? base + 1 : u8.c_str();

			char menuLabel[280];
			snprintf(menuLabel, sizeof(menuLabel), "%d. %s", i + 1, label);

			if (ImGui::MenuItem(menuLabel)) {
				ATImageLoadContext ctx {};
				if (g_sim.Load(wpath.c_str(), kATMediaWriteMode_RO, &ctx)) {
					ATAddMRU(wpath.c_str());
					g_sim.ColdReset();
					g_sim.Resume();
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", u8.c_str());
		}

		ImGui::Separator();
		if (ImGui::MenuItem("Clear List"))
			ATClearMRU();

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Disk Drives..."))
		state.showDiskManager = true;

	// Attach Disk submenu (matches Windows: Rotate + D1-D8)
	if (ImGui::BeginMenu("Attach Disk")) {
		if (ImGui::MenuItem("Rotate Down"))
			sim.RotateDrives(8, 1);
		if (ImGui::MenuItem("Rotate Up"))
			sim.RotateDrives(8, -1);

		ImGui::Separator();

		for (int i = 0; i < 8; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "Drive %d...", i + 1);

			ATDiskInterface& di = sim.GetDiskInterface(i);
			bool loaded = di.IsDiskLoaded();

			// Show current image in menu item if loaded
			if (loaded) {
				const wchar_t *path = di.GetPath();
				VDStringA u8;
				if (path && *path) {
					u8 = VDTextWToU8(VDStringW(path));
					const char *base = strrchr(u8.c_str(), '/');
					if (base) u8 = VDStringA(base + 1);
				}
				char fullLabel[256];
				snprintf(fullLabel, sizeof(fullLabel), "Drive %d [%s]...",
					i + 1, u8.empty() ? "loaded" : u8.c_str());
				if (ImGui::MenuItem(fullLabel))
					SDL_ShowOpenFileDialog(AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, nullptr, false);
			} else {
				if (ImGui::MenuItem(label))
					SDL_ShowOpenFileDialog(AttachDiskCallback,
						(void *)(intptr_t)i, window,
						kDiskAttachFilters, 2, nullptr, false);
			}
		}
		ImGui::EndMenu();
	}

	// Detach Disk submenu (matches Windows: All + D1-D8)
	if (ImGui::BeginMenu("Detach Disk")) {
		if (ImGui::MenuItem("All")) {
			for (int i = 0; i < 15; ++i)
				if (sim.GetDiskInterface(i).IsDiskLoaded())
					sim.GetDiskInterface(i).UnloadDisk();
		}

		ImGui::Separator();

		for (int i = 0; i < 8; ++i) {
			char label[32];
			snprintf(label, sizeof(label), "Drive %d", i + 1);
			bool loaded = sim.GetDiskInterface(i).IsDiskLoaded();
			if (ImGui::MenuItem(label, nullptr, false, loaded))
				sim.GetDiskInterface(i).UnloadDisk();
		}
		ImGui::EndMenu();
	}

	// Cassette submenu (matches Windows: Tape Control, Tape Editor, New/Load/Unload/Save/Export)
	if (ImGui::BeginMenu("Cassette")) {
		ATCassetteEmulator& cas = sim.GetCassette();
		bool loaded = cas.IsLoaded();

		if (ImGui::MenuItem("Tape Control..."))
			state.showCassetteControl = true;

		ImGui::MenuItem("Tape Editor...", nullptr, false, false);  // placeholder

		ImGui::Separator();

		if (ImGui::MenuItem("New Tape"))
			cas.LoadNew();

		if (ImGui::MenuItem("Load...")) {
			static const SDL_DialogFileFilter casFilters[] = {
				{ "Cassette Images", "cas;wav" }, { "All Files", "*" },
			};
			SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_LoadCassette, fl[0]);
			}, nullptr, window, casFilters, 2, nullptr, false);
		}

		if (ImGui::MenuItem("Unload", nullptr, false, loaded))
			cas.Unload();

		if (ImGui::MenuItem("Save...", nullptr, false, loaded)) {
			SDL_ShowSaveFileDialog(CassetteSaveCallback, nullptr, window,
				kCasSaveFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Export Audio Tape...", nullptr, false, loaded)) {
			static const SDL_DialogFileFilter wavFilters[] = {
				{ "WAV Audio", "wav" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_ExportCassetteAudio, fl[0]);
			}, nullptr, window, wavFilters, 1, nullptr);
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// State save/load
	{
		static const SDL_DialogFileFilter stateFilters[] = {
			{ "Save States", "atstate2;atstate" }, { "All Files", "*" },
		};

		if (ImGui::MenuItem("Load State..."))
			SDL_ShowOpenFileDialog(LoadStateCallback, nullptr, window, stateFilters, 2, nullptr, false);

		if (ImGui::MenuItem("Save State..."))
			SDL_ShowSaveFileDialog(SaveStateCallback, nullptr, window, stateFilters, 1, nullptr);

		if (ImGui::MenuItem("Quick Load State", "F7", false, g_pQuickSaveState != nullptr))
			ATUIQuickLoadState();

		if (ImGui::MenuItem("Quick Save State", "F8"))
			ATUIQuickSaveState();
	}

	ImGui::Separator();

	// Attach Special Cartridge submenu (matches Windows exactly)
	if (ImGui::BeginMenu("Attach Special Cartridge")) {
		static const struct { const char *label; int mode; } kSpecialCarts[] = {
			{ "SuperCharger3D",                                     kATCartridgeMode_SuperCharger3D },
			{ "Empty 128K (1Mbit) MaxFlash cartridge",              kATCartridgeMode_MaxFlash_128K },
			{ "Empty 128K (1Mbit) MaxFlash cartridge (MyIDE banking)", kATCartridgeMode_MaxFlash_128K_MyIDE },
			{ "Empty 1M (8Mbit) MaxFlash cartridge (older - bank 127)", kATCartridgeMode_MaxFlash_1024K },
			{ "Empty 1M (8Mbit) MaxFlash cartridge (newer - bank 0)", kATCartridgeMode_MaxFlash_1024K_Bank0 },
			{ "Empty 128K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_128K },
			{ "Empty 256K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_256K },
			{ "Empty 512K J(Atari)Cart",                            kATCartridgeMode_JAtariCart_512K },
			{ "Empty 1024K J(Atari)Cart",                           kATCartridgeMode_JAtariCart_1024K },
			{ "Empty DCart",                                        kATCartridgeMode_DCart },
			{ "Empty SIC! 512K flash cartridge",                    kATCartridgeMode_SIC_512K },
			{ "Empty SIC! 256K flash cartridge",                    kATCartridgeMode_SIC_256K },
			{ "Empty SIC! 128K flash cartridge",                    kATCartridgeMode_SIC_128K },
			{ "Empty SIC+ flash cartridge",                         kATCartridgeMode_SICPlus },
			{ "Empty 512K MegaCart flash cartridge",                kATCartridgeMode_MegaCart_512K },
			{ "Empty 4MB MegaCart flash cartridge",                 kATCartridgeMode_MegaCart_4M_3 },
			{ "Empty The!Cart 32MB flash cartridge",                kATCartridgeMode_TheCart_32M },
			{ "Empty The!Cart 64MB flash cartridge",                kATCartridgeMode_TheCart_64M },
			{ "Empty The!Cart 128MB flash cartridge",               kATCartridgeMode_TheCart_128M },
		};

		for (auto& sc : kSpecialCarts) {
			if (ImGui::MenuItem(sc.label)) {
				sim.LoadNewCartridge(sc.mode);
				sim.ColdReset();
				sim.Resume();
			}
		}

		ImGui::Separator();

		if (ImGui::MenuItem("BASIC")) {
			sim.LoadCartridgeBASIC();
			sim.ColdReset();
			sim.Resume();
		}

		ImGui::EndMenu();
	}

	// Secondary Cartridge submenu
	if (ImGui::BeginMenu("Secondary Cartridge")) {
		if (ImGui::MenuItem("Attach..."))
			SDL_ShowOpenFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_AttachSecondaryCartridge, fl[0]);
			}, nullptr, window, kCartFilters, 2, nullptr, false);

		if (ImGui::MenuItem("Detach", nullptr, false, sim.IsCartridgeAttached(1))) {
			sim.UnloadCartridge(1);
			sim.ColdReset();
			sim.Resume();
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Attach Cartridge..."))
		SDL_ShowOpenFileDialog(CartridgeAttachCallback, nullptr, window, kCartFilters, 2, nullptr, false);

	if (ImGui::MenuItem("Detach Cartridge", nullptr, false, sim.IsCartridgeAttached(0))) {
		sim.UnloadCartridge(0);
		sim.ColdReset();
		sim.Resume();
	}

	// Save Firmware submenu
	if (ImGui::BeginMenu("Save Firmware")) {
		if (ImGui::MenuItem("Save Cartridge...", nullptr, false, sim.IsCartridgeAttached(0))) {
			static const SDL_DialogFileFilter cartSaveFilters[] = {
				{ "Cartridge image with header (*.car)", "car" },
				{ "Raw cartridge image (*.bin, *.rom)", "bin;rom" },
				{ "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveCartridge, fl[0]);
			}, nullptr, window, cartSaveFilters, 3, nullptr);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE / SIDE / MyIDE II Main Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)kATStorageId_Firmware))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)0, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save KMK/JZ IDE SDX Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 1)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)1, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save Ultimate1MB Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 2)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)2, window, fwFilters, 2, nullptr);
		}

		if (ImGui::MenuItem("Save Rapidus Flash...", nullptr, false,
				sim.IsStoragePresent((ATStorageId)(kATStorageId_Firmware + 3)))) {
			static const SDL_DialogFileFilter fwFilters[] = {
				{ "Firmware Images", "bin;rom" }, { "All Files", "*" },
			};
			SDL_ShowSaveFileDialog([](void *ud, const char * const *fl, int) {
				if (fl && fl[0])
					ATUIPushDeferred(kATDeferred_SaveFirmware, fl[0], (int)(intptr_t)ud);
			}, (void *)(intptr_t)3, window, fwFilters, 2, nullptr);
		}

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Exit")) {
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
	}
}

// =========================================================================
// View menu
// =========================================================================

static void RenderViewMenu(ATSimulator &sim, ATUIState &state, SDL_Window *window, SDL_Renderer *renderer) {
	bool isFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
	if (ImGui::MenuItem("Full Screen", "Alt+Enter", isFullscreen))
		SDL_SetWindowFullscreen(window, !isFullscreen);

	ImGui::Separator();

	if (ImGui::BeginMenu("Filter Mode")) {
		ATDisplayFilterMode fm = ATUIGetDisplayFilterMode();
		if (ImGui::MenuItem("Point", nullptr, fm == kATDisplayFilterMode_Point))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Point);
		if (ImGui::MenuItem("Bilinear", nullptr, fm == kATDisplayFilterMode_Bilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bilinear);
		if (ImGui::MenuItem("Sharp Bilinear", nullptr, fm == kATDisplayFilterMode_SharpBilinear))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_SharpBilinear);
		if (ImGui::MenuItem("Bicubic", nullptr, fm == kATDisplayFilterMode_Bicubic))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_Bicubic);
		if (ImGui::MenuItem("Default (Any Suitable)", nullptr, fm == kATDisplayFilterMode_AnySuitable))
			ATUISetDisplayFilterMode(kATDisplayFilterMode_AnySuitable);
		ImGui::Separator();
		if (ImGui::MenuItem("Next Mode")) {
			// Cycle through modes
			static const ATDisplayFilterMode kModes[] = {
				kATDisplayFilterMode_Point, kATDisplayFilterMode_Bilinear,
				kATDisplayFilterMode_SharpBilinear, kATDisplayFilterMode_Bicubic,
				kATDisplayFilterMode_AnySuitable,
			};
			int cur = 0;
			for (int i = 0; i < 5; ++i)
				if (kModes[i] == fm) { cur = i; break; }
			ATUISetDisplayFilterMode(kModes[(cur + 1) % 5]);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Filter Sharpness")) {
		int sharpness = ATUIGetViewFilterSharpness();
		if (ImGui::MenuItem("Softer", nullptr, sharpness == -2))
			ATUISetViewFilterSharpness(-2);
		if (ImGui::MenuItem("Soft", nullptr, sharpness == -1))
			ATUISetViewFilterSharpness(-1);
		if (ImGui::MenuItem("Normal", nullptr, sharpness == 0))
			ATUISetViewFilterSharpness(0);
		if (ImGui::MenuItem("Sharp", nullptr, sharpness == 1))
			ATUISetViewFilterSharpness(1);
		if (ImGui::MenuItem("Sharper", nullptr, sharpness == 2))
			ATUISetViewFilterSharpness(2);
		ImGui::EndMenu();
	}

	// Video Frame submenu (matches Windows "Video Frame" menu)
	if (ImGui::BeginMenu("Video Frame")) {
		ATDisplayStretchMode sm = ATUIGetDisplayStretchMode();
		if (ImGui::MenuItem("Fit to Window", nullptr, sm == kATDisplayStretchMode_Unconstrained))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Unconstrained);
		if (ImGui::MenuItem("Preserve Aspect Ratio", nullptr, sm == kATDisplayStretchMode_PreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_PreserveAspectRatio);
		if (ImGui::MenuItem("Preserve Aspect Ratio (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_IntegralPreserveAspectRatio))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_IntegralPreserveAspectRatio);
		if (ImGui::MenuItem("Square Pixels", nullptr, sm == kATDisplayStretchMode_SquarePixels))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_SquarePixels);
		if (ImGui::MenuItem("Square Pixels (fixed multiples only)", nullptr, sm == kATDisplayStretchMode_Integral))
			ATUISetDisplayStretchMode(kATDisplayStretchMode_Integral);

		ImGui::Separator();

		ImGui::MenuItem("Pan/Zoom Tool", nullptr, false, false);  // placeholder
		if (ImGui::MenuItem("Reset Pan and Zoom")) {
			ATUISetDisplayZoom(1.0f);
			ATUISetDisplayPanOffset({0, 0});
		}
		if (ImGui::MenuItem("Reset Panning"))
			ATUISetDisplayPanOffset({0, 0});
		if (ImGui::MenuItem("Reset Zoom"))
			ATUISetDisplayZoom(1.0f);

		ImGui::EndMenu();
	}

	// Overscan Mode submenu (matches Windows exactly, with sub-submenus)
	if (ImGui::BeginMenu("Overscan Mode")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto om = gtia.GetOverscanMode();
		if (ImGui::MenuItem("OS Screen Only", nullptr, om == ATGTIAEmulator::kOverscanOSScreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanOSScreen);
		if (ImGui::MenuItem("Normal", nullptr, om == ATGTIAEmulator::kOverscanNormal))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanNormal);
		if (ImGui::MenuItem("Widescreen", nullptr, om == ATGTIAEmulator::kOverscanWidescreen))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanWidescreen);
		if (ImGui::MenuItem("Extended", nullptr, om == ATGTIAEmulator::kOverscanExtended))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanExtended);
		if (ImGui::MenuItem("Full (With Blanking)", nullptr, om == ATGTIAEmulator::kOverscanFull))
			gtia.SetOverscanMode(ATGTIAEmulator::kOverscanFull);

		ImGui::Separator();

		// Vertical Override sub-submenu
		if (ImGui::BeginMenu("Vertical Override")) {
			auto vom = gtia.GetVerticalOverscanMode();
			if (ImGui::MenuItem("Off", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Default))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Default);
			if (ImGui::MenuItem("OS Screen Only", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_OSScreen))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_OSScreen);
			if (ImGui::MenuItem("Normal", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Normal))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Normal);
			if (ImGui::MenuItem("Extended", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Extended))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Extended);
			if (ImGui::MenuItem("Full (With Blanking)", nullptr, vom == ATGTIAEmulator::kVerticalOverscan_Full))
				gtia.SetVerticalOverscanMode(ATGTIAEmulator::kVerticalOverscan_Full);
			ImGui::EndMenu();
		}

		bool palExt = gtia.IsOverscanPALExtended();
		if (ImGui::MenuItem("Extended PAL Height", nullptr, palExt))
			gtia.SetOverscanPALExtended(!palExt);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	// VSync toggle — query current state from renderer
	{
		int vsync = 0;
		SDL_GetRenderVSync(renderer, &vsync);
		bool vsyncOn = (vsync != 0);
		if (ImGui::MenuItem("Vertical Sync", nullptr, vsyncOn))
			SDL_SetRenderVSync(renderer, vsyncOn ? 0 : 1);
	}

	bool showFPS = ATUIGetShowFPS();
	if (ImGui::MenuItem("Show FPS", nullptr, showFPS))
		ATUISetShowFPS(!showFPS);

	// Video Outputs submenu
	if (ImGui::BeginMenu("Video Outputs")) {
		bool altView = ATUIGetAltViewEnabled();
		if (ImGui::MenuItem("1 Computer Output", nullptr, !altView))
			ATUISetAltViewEnabled(false);

		if (ATUIIsAltOutputAvailable()) {
			if (ImGui::MenuItem("Next Output"))
				ATUISelectNextAltOutput();
		}

		bool autoSwitch = ATUIGetAltViewAutoswitchingEnabled();
		if (ImGui::MenuItem("Auto-Switch Video Output", nullptr, autoSwitch))
			ATUISetAltViewAutoswitchingEnabled(!autoSwitch);

		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::MenuItem("Adjust Colors..."))
		state.showAdjustColors = true;

	ImGui::MenuItem("Adjust Screen Effects...", nullptr, false, false);  // placeholder — needs shader support

	ImGui::Separator();

	// Copy/Save Frame (placeholders — require framebuffer readback)
	ImGui::MenuItem("Copy Frame to Clipboard", nullptr, false, false);
	ImGui::MenuItem("Save Frame...", nullptr, false, false);
}

// =========================================================================
// System menu
// =========================================================================

static void RenderSystemMenu(ATSimulator &sim, ATUIState &state) {
	// Profiles submenu (placeholder — profile system not yet wired)
	if (ImGui::BeginMenu("Profiles")) {
		ImGui::MenuItem("Edit Profiles...", nullptr, false, false);
		ImGui::MenuItem("Temporary Profile", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Configure System..."))
		state.showSystemConfig = true;

	ImGui::Separator();

	if (ImGui::MenuItem("Warm Reset", "F5")) {
		sim.WarmReset();
		sim.Resume();
	}
	if (ImGui::MenuItem("Cold Reset", "Shift+F5")) {
		sim.ColdReset();
		sim.Resume();
	}
	if (ImGui::MenuItem("Cold Reset (Computer Only)")) {
		sim.ColdResetComputerOnly();
		sim.Resume();
	}

	bool paused = sim.IsPaused();
	if (ImGui::MenuItem("Pause", "F9", paused)) {
		if (paused) sim.Resume(); else sim.Pause();
	}

	ImGui::Separator();

	bool turbo = ATUIGetTurbo();
	if (ImGui::MenuItem("Warp Speed", nullptr, turbo))
		ATUISetTurbo(!turbo);

	bool pauseInactive = ATUIGetPauseWhenInactive();
	if (ImGui::MenuItem("Pause When Inactive", nullptr, pauseInactive))
		ATUISetPauseWhenInactive(!pauseInactive);

	// Rewind submenu
	if (ImGui::BeginMenu("Rewind")) {
		ImGui::MenuItem("Quick Rewind", nullptr, false, false);  // placeholder
		ImGui::MenuItem("Rewind...", nullptr, false, false);      // placeholder
		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("Power-On Delay")) {
		int delay = sim.GetPowerOnDelay();
		if (ImGui::MenuItem("Auto", nullptr, delay < 0))
			sim.SetPowerOnDelay(-1);
		if (ImGui::MenuItem("None", nullptr, delay == 0))
			sim.SetPowerOnDelay(0);
		if (ImGui::MenuItem("1 Second", nullptr, delay == 10))
			sim.SetPowerOnDelay(10);
		if (ImGui::MenuItem("2 Seconds", nullptr, delay == 20))
			sim.SetPowerOnDelay(20);
		if (ImGui::MenuItem("3 Seconds", nullptr, delay == 30))
			sim.SetPowerOnDelay(30);
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Hold Keys For Reset"))
		ATUIToggleHoldKeys();

	bool basic = sim.IsBASICEnabled();
	if (ImGui::MenuItem("Internal BASIC (Boot Without Option Key)", nullptr, basic))
		sim.SetBASICEnabled(!basic);

	bool casAutoBoot = sim.IsCassetteAutoBootEnabled();
	if (ImGui::MenuItem("Auto-Boot Tape (Hold Start)", nullptr, casAutoBoot))
		sim.SetCassetteAutoBootEnabled(!casAutoBoot);

	ImGui::Separator();

	// Console Switches submenu (matches Windows menu)
	if (ImGui::BeginMenu("Console Switches")) {
		bool kbdPresent = sim.IsKeyboardPresent();
		if (ImGui::MenuItem("Keyboard Present (XEGS)", nullptr, kbdPresent))
			sim.SetKeyboardPresent(!kbdPresent);

		bool selfTest = sim.IsForcedSelfTest();
		if (ImGui::MenuItem("Force Self-Test", nullptr, selfTest))
			sim.SetForcedSelfTest(!selfTest);

		bool cartSwitch = sim.GetCartridgeSwitch();
		if (ImGui::MenuItem("Cartridge Switch", nullptr, cartSwitch))
			sim.SetCartridgeSwitch(!cartSwitch);

		// Device buttons (shown only when supported)
		static const struct { ATDeviceButton btn; const char *label; } kDevButtons[] = {
			{ kATDeviceButton_BlackBoxDumpScreen, "BlackBox: Dump Screen" },
			{ kATDeviceButton_BlackBoxMenu, "BlackBox: Menu" },
			{ kATDeviceButton_CartridgeResetBank, "Cartridge: Reset Bank" },
			{ kATDeviceButton_IDEPlus2SwitchDisks, "IDE Plus 2.0: Switch Disks" },
			{ kATDeviceButton_IDEPlus2WriteProtect, "IDE Plus 2.0: Write Protect" },
			{ kATDeviceButton_IDEPlus2SDX, "IDE Plus 2.0: SDX" },
			{ kATDeviceButton_IndusGTTrack, "Indus GT: Track" },
			{ kATDeviceButton_IndusGTId, "Indus GT: Id" },
			{ kATDeviceButton_IndusGTError, "Indus GT: Error" },
			{ kATDeviceButton_IndusGTBootCPM, "Indus GT: Boot CP/M" },
			{ kATDeviceButton_IndusGTChangeDensity, "Indus GT: Change Density" },
			{ kATDeviceButton_HappySlow, "Happy: Slow" },
			{ kATDeviceButton_HappyWPEnable, "Happy: Write Protect Enable" },
			{ kATDeviceButton_HappyWPDisable, "Happy: Write Protect Disable" },
			{ kATDeviceButton_ATR8000Reset, "ATR8000: Reset" },
			{ kATDeviceButton_XELCFSwap, "XEL-CF: Swap" },
		};

		bool anyDevBtn = false;
		for (auto& db : kDevButtons) {
			if (ATUIGetDeviceButtonSupported((uint32)db.btn)) {
				if (!anyDevBtn) {
					ImGui::Separator();
					anyDevBtn = true;
				}
				bool dep = ATUIGetDeviceButtonDepressed((uint32)db.btn);
				if (ImGui::MenuItem(db.label, nullptr, dep))
					ATUIActivateDeviceButton((uint32)db.btn, !dep);
			}
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	// Quick-access submenus (also in Configure System dialog)
	if (ImGui::BeginMenu("Video Standard")) {
		ATVideoStandard vs = sim.GetVideoStandard();
		if (ImGui::MenuItem("NTSC", nullptr, vs == kATVideoStandard_NTSC))
			sim.SetVideoStandard(kATVideoStandard_NTSC);
		if (ImGui::MenuItem("PAL", nullptr, vs == kATVideoStandard_PAL))
			sim.SetVideoStandard(kATVideoStandard_PAL);
		if (ImGui::MenuItem("SECAM", nullptr, vs == kATVideoStandard_SECAM))
			sim.SetVideoStandard(kATVideoStandard_SECAM);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Hardware Mode")) {
		ATHardwareMode hw = sim.GetHardwareMode();
		static const struct { const char *label; ATHardwareMode mode; } kHWModes[] = {
			{ "800",    kATHardwareMode_800 },
			{ "800XL",  kATHardwareMode_800XL },
			{ "1200XL", kATHardwareMode_1200XL },
			{ "130XE",  kATHardwareMode_130XE },
			{ "1400XL", kATHardwareMode_1400XL },
			{ "XEGS",   kATHardwareMode_XEGS },
			{ "5200",   kATHardwareMode_5200 },
		};
		for (auto& m : kHWModes) {
			if (ImGui::MenuItem(m.label, nullptr, hw == m.mode))
				sim.SetHardwareMode(m.mode);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Memory")) {
		ATMemoryMode mm = sim.GetMemoryMode();
		static const struct { const char *label; ATMemoryMode mode; } kMemModes[] = {
			{ "8K",                  kATMemoryMode_8K },
			{ "16K",                 kATMemoryMode_16K },
			{ "24K",                 kATMemoryMode_24K },
			{ "32K",                 kATMemoryMode_32K },
			{ "40K",                 kATMemoryMode_40K },
			{ "48K",                 kATMemoryMode_48K },
			{ "52K",                 kATMemoryMode_52K },
			{ "64K",                 kATMemoryMode_64K },
			{ "128K",                kATMemoryMode_128K },
			{ "256K",                kATMemoryMode_256K },
			{ "320K (Rambo)",        kATMemoryMode_320K },
			{ "320K (Compy Shop)",   kATMemoryMode_320K_Compy },
			{ "576K (Rambo)",        kATMemoryMode_576K },
			{ "576K (Compy Shop)",   kATMemoryMode_576K_Compy },
			{ "1088K",               kATMemoryMode_1088K },
		};
		for (auto& m : kMemModes) {
			if (ImGui::MenuItem(m.label, nullptr, mm == m.mode))
				sim.SetMemoryMode(m.mode);
		}
		ImGui::EndMenu();
	}
}

// =========================================================================
// Input menu
// =========================================================================

// Render a single Port submenu.  Queries ATInputManager for all input maps
// that touch the given physical port, presents them as radio items, and
// toggles activation.  Mirrors Windows uiportmenus.cpp behavior exactly.
static void RenderPortSubmenu(ATInputManager &im, int portIdx) {
	// Collect input maps that use this physical port
	struct MapEntry {
		ATInputMap *map;
		VDStringA  name;   // UTF-8 for ImGui
		bool       active;
	};
	std::vector<MapEntry> entries;

	uint32 mapCount = im.GetInputMapCount();
	for (uint32 i = 0; i < mapCount; ++i) {
		vdrefptr<ATInputMap> imap;
		if (im.GetInputMapByIndex(i, ~imap)) {
			if (imap->UsesPhysicalPort(portIdx)) {
				MapEntry e;
				e.map = imap;
				e.name = VDTextWToU8(imap->GetName(), -1);
				e.active = im.IsInputMapEnabled(imap);
				entries.push_back(std::move(e));
			}
		}
	}

	// Sort alphabetically (case-insensitive), matching Windows
	std::sort(entries.begin(), entries.end(),
		[](const MapEntry &a, const MapEntry &b) {
			return vdwcsicmp(a.map->GetName(), b.map->GetName()) < 0;
		});

	// "None" item — selected when no maps are active for this port
	bool anyActive = false;
	for (const auto &e : entries)
		if (e.active) { anyActive = true; break; }

	if (ImGui::MenuItem("None", nullptr, !anyActive)) {
		// Deactivate all maps for this port
		for (const auto &e : entries)
			if (e.active)
				im.ActivateInputMap(e.map, false);
	}

	// One radio item per input map — strict radio behavior matching Windows:
	// clicking any item activates it and deactivates all others for this port.
	// Clicking the already-active item is a no-op (it stays selected).
	for (const auto &e : entries) {
		if (ImGui::MenuItem(e.name.c_str(), nullptr, e.active)) {
			for (const auto &other : entries)
				im.ActivateInputMap(other.map, &other == &e);
		}
	}
}

static void RenderInputMenu(ATSimulator &sim) {
	ATInputManager *pIM = sim.GetInputManager();

	ImGui::MenuItem("Input Mappings...", nullptr, false, false);  // placeholder
	ImGui::MenuItem("Input Setup...", nullptr, false, false);     // placeholder

	// Cycle Quick Maps — cycles through maps marked as quick-cycle
	if (ImGui::MenuItem("Cycle Quick Maps")) {
		if (pIM) {
			ATInputMap *pMap = pIM->CycleQuickMaps();
			if (pMap) {
				VDStringA msg;
				msg = "Quick map: ";
				msg += VDTextWToU8(pMap->GetName(), -1);
				fprintf(stderr, "[AltirraSDL] %s\n", msg.c_str());
			} else {
				fprintf(stderr, "[AltirraSDL] Quick maps disabled\n");
			}
		}
	}

	ImGui::Separator();

	// Capture Mouse — only enabled when mouse is mapped in an input map
	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool captured = ATUIIsMouseCaptured();
		if (ImGui::MenuItem("Capture Mouse", nullptr, captured, mouseMapped)) {
			if (captured)
				ATUIReleaseMouse();
			else
				ATUICaptureMouse();
		}
	}

	{
		bool mouseMapped = pIM && pIM->IsMouseMapped();
		bool mouseAutoCapture = ATUIGetMouseAutoCapture();
		if (ImGui::MenuItem("Auto-Capture Mouse", nullptr, mouseAutoCapture, mouseMapped))
			ATUISetMouseAutoCapture(!mouseAutoCapture);
	}

	ImGui::Separator();

	ImGui::MenuItem("Light Pen/Gun...", nullptr, false, false);          // placeholder
	ImGui::MenuItem("Recalibrate Light Pen/Gun", nullptr, false, false); // placeholder

	ImGui::Separator();

	// Port 1-4 submenus — dynamic input map assignment per port
	if (pIM) {
		for (int i = 0; i < 4; ++i) {
			char label[16];
			snprintf(label, sizeof(label), "Port %d", i + 1);
			if (ImGui::BeginMenu(label)) {
				RenderPortSubmenu(*pIM, i);
				ImGui::EndMenu();
			}
		}
	}
}

// =========================================================================
// Cheat menu
// =========================================================================

static void RenderCheatMenu(ATSimulator &sim) {
	ImGui::MenuItem("Cheater...", nullptr, false, false);
	ImGui::Separator();

	ATGTIAEmulator& gtia = sim.GetGTIA();

	bool pmColl = gtia.ArePMCollisionsEnabled();
	if (ImGui::MenuItem("Disable P/M Collisions", nullptr, !pmColl))
		gtia.SetPMCollisionsEnabled(!pmColl);

	bool pfColl = gtia.ArePFCollisionsEnabled();
	if (ImGui::MenuItem("Disable Playfield Collisions", nullptr, !pfColl))
		gtia.SetPFCollisionsEnabled(!pfColl);
}

// =========================================================================
// Debug menu
// =========================================================================

static void RenderDebugMenu(ATSimulator &sim) {
	ImGui::MenuItem("Enable Debugger", nullptr, false, false);
	ImGui::Separator();

	ImGui::MenuItem("Open Source File...", nullptr, false, false);

	if (ImGui::BeginMenu("Window")) {
		ImGui::MenuItem("Console", nullptr, false, false);
		ImGui::MenuItem("Registers", nullptr, false, false);
		ImGui::MenuItem("Disassembly", nullptr, false, false);
		ImGui::MenuItem("Memory", nullptr, false, false);
		ImGui::MenuItem("Call Stack", nullptr, false, false);
		ImGui::MenuItem("History", nullptr, false, false);
		ImGui::Separator();
		ImGui::MenuItem("Watch 1", nullptr, false, false);
		ImGui::MenuItem("Watch 2", nullptr, false, false);
		ImGui::MenuItem("Watch 3", nullptr, false, false);
		ImGui::MenuItem("Watch 4", nullptr, false, false);
		ImGui::Separator();
		ImGui::MenuItem("Memory 1", nullptr, false, false);
		ImGui::MenuItem("Memory 2", nullptr, false, false);
		ImGui::MenuItem("Memory 3", nullptr, false, false);
		ImGui::MenuItem("Memory 4", nullptr, false, false);
		ImGui::Separator();
		ImGui::MenuItem("Breakpoints", nullptr, false, false);
		ImGui::MenuItem("Targets", nullptr, false, false);
		ImGui::MenuItem("Debug Display", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Visualization")) {
		ATGTIAEmulator& gtia = sim.GetGTIA();
		auto am = gtia.GetAnalysisMode();
		if (ImGui::MenuItem("None", nullptr, am == ATGTIAEmulator::kAnalyzeNone))
			gtia.SetAnalysisMode(ATGTIAEmulator::kAnalyzeNone);
		if (ImGui::MenuItem("Layers", nullptr, am == ATGTIAEmulator::kAnalyzeLayers))
			gtia.SetAnalysisMode(ATGTIAEmulator::kAnalyzeLayers);
		if (ImGui::MenuItem("Colors", nullptr, am == ATGTIAEmulator::kAnalyzeColors))
			gtia.SetAnalysisMode(ATGTIAEmulator::kAnalyzeColors);
		if (ImGui::MenuItem("Display List", nullptr, am == ATGTIAEmulator::kAnalyzeDList))
			gtia.SetAnalysisMode(ATGTIAEmulator::kAnalyzeDList);

		ImGui::Separator();
		if (ImGui::MenuItem("Cycle GTIA Visualization")) {
			int next = ((int)am + 1) % ATGTIAEmulator::kAnalyzeCount;
			gtia.SetAnalysisMode((ATGTIAEmulator::AnalysisMode)next);
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Options")) {
		ImGui::MenuItem("Auto-Reload ROMs on Cold Reset", nullptr, false, false);
		ImGui::MenuItem("Randomize Memory on Cold Reset", nullptr, false, false);
		ImGui::MenuItem("Break at EXE Run Address", nullptr, false, false);
		ImGui::MenuItem("Change Font...", nullptr, false, false);
		ImGui::EndMenu();
	}

	ImGui::Separator();

	ImGui::MenuItem("Run/Break", "F8", false, false);
	ImGui::MenuItem("Break", nullptr, false, false);
	ImGui::MenuItem("Step Into", "F11", false, false);
	ImGui::MenuItem("Step Over", "F10", false, false);
	ImGui::MenuItem("Step Out", "Shift+F11", false, false);

	ImGui::Separator();

	ImGui::MenuItem("Profile View...", nullptr, false, false);
	ImGui::MenuItem("Verifier...", nullptr, false, false);
	ImGui::MenuItem("Performance Analyzer...", nullptr, false, false);
}

// =========================================================================
// Record menu
// =========================================================================

static void RenderRecordMenu(ATSimulator &sim, SDL_Window *window) {
	bool recording = ATUIIsRecording();

	if (ImGui::MenuItem("Record Raw Audio...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter rawFilters[] = {
			{ "Raw PCM Audio", "pcm" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordRaw, fl[0]);
		}, nullptr, window, rawFilters, 1, nullptr);
	}

	if (ImGui::MenuItem("Record Audio...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter wavFilters[] = {
			{ "WAV Audio", "wav" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordWAV, fl[0]);
		}, nullptr, window, wavFilters, 1, nullptr);
	}

	ImGui::MenuItem("Record Video...", nullptr, false, false);  // placeholder — needs video encoder

	if (ImGui::MenuItem("Record SAP Type R...", nullptr, false, !recording)) {
		static const SDL_DialogFileFilter sapFilters[] = {
			{ "SAP Files", "sap" }, { "All Files", "*" },
		};
		SDL_ShowSaveFileDialog([](void *, const char * const *fl, int) {
			if (fl && fl[0])
				ATUIPushDeferred(kATDeferred_StartRecordSAP, fl[0]);
		}, nullptr, window, sapFilters, 1, nullptr);
	}

	ImGui::MenuItem("Record VGM...", nullptr, false, false);  // placeholder — excluded from build (wchar_t size)

	ImGui::Separator();

	if (ImGui::MenuItem("Stop Recording", nullptr, false, recording))
		ATUIStopRecording();
}

// =========================================================================
// Tools menu
// =========================================================================

static void RenderToolsMenu() {
	ImGui::MenuItem("Disk Explorer...", nullptr, false, false);         // placeholder
	ImGui::MenuItem("Convert SAP to EXE...", nullptr, false, false);   // placeholder
	ImGui::MenuItem("Export ROM set...", nullptr, false, false);        // placeholder
	ImGui::MenuItem("Analyze tape decoding...", nullptr, false, false); // placeholder
	ImGui::Separator();
	ImGui::MenuItem("First Time Setup...", nullptr, false, false);     // placeholder
	ImGui::Separator();
	ImGui::MenuItem("Keyboard Shortcuts...", nullptr, false, false);   // placeholder
	ImGui::MenuItem("Compatibility Database...", nullptr, false, false); // placeholder
	ImGui::MenuItem("Advanced Configuration...", nullptr, false, false); // placeholder
}

// =========================================================================
// Window menu
// =========================================================================

static void RenderWindowMenu(SDL_Window *window) {
	if (ImGui::BeginMenu("Adjust Window Size")) {
		static const struct { const char *label; int w; int h; } kSizes[] = {
			{ "1x (336x240)", 336, 240 },
			{ "2x (672x480)", 672, 480 },
			{ "3x (1008x720)", 1008, 720 },
			{ "4x (1344x960)", 1344, 960 },
		};
		for (auto& sz : kSizes) {
			if (ImGui::MenuItem(sz.label))
				SDL_SetWindowSize(window, sz.w, sz.h);
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Reset Window Layout")) {
		// Reset to default 2x size
		SDL_SetWindowSize(window, 672, 480);
		SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	}
}

// =========================================================================
// Help menu
// =========================================================================

static void RenderHelpMenu(ATUIState &state) {
	ImGui::MenuItem("Contents", nullptr, false, false);  // placeholder

	if (ImGui::MenuItem("About"))
		state.showAboutDialog = true;

	ImGui::MenuItem("Change Log", nullptr, false, false);       // placeholder
	ImGui::MenuItem("Command-Line Help", nullptr, false, false); // placeholder
}

// =========================================================================
// About dialog
// =========================================================================

static void RenderAboutDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(420, 220), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("About AltirraSDL", &state.showAboutDialog, ImGuiWindowFlags_NoCollapse)) {
		ImGui::End();
		return;
	}

	ImGui::Text("AltirraSDL");
	ImGui::Separator();
	ImGui::TextWrapped(
		"Atari 800/800XL/5200 emulator\n"
		"Based on Altirra by Avery Lee\n"
		"SDL3 + Dear ImGui cross-platform frontend\n\n"
		"Licensed under GNU GPL v2+");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showAboutDialog = false;

	ImGui::End();
}

// =========================================================================
// Main menu bar
// =========================================================================

static void RenderMainMenu(ATSimulator &sim, SDL_Window *window, SDL_Renderer *renderer, ATUIState &state) {
	if (!ImGui::BeginMainMenuBar())
		return;

	if (ImGui::BeginMenu("File")) { RenderFileMenu(sim, state, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("View")) { RenderViewMenu(sim, state, window, renderer); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("System")) { RenderSystemMenu(sim, state); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Input")) { RenderInputMenu(sim); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Cheat")) { RenderCheatMenu(sim); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Debug")) { RenderDebugMenu(sim); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Record")) { RenderRecordMenu(sim, window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Tools")) { RenderToolsMenu(); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Window")) { RenderWindowMenu(window); ImGui::EndMenu(); }
	if (ImGui::BeginMenu("Help")) { RenderHelpMenu(state); ImGui::EndMenu(); }

	ImGui::EndMainMenuBar();
}

// =========================================================================
// Status overlay
// =========================================================================

static void RenderStatusOverlay(ATSimulator &sim) {
	bool showFPS = ATUIGetShowFPS();
	bool paused = sim.IsPaused();
	bool recording = ATUIIsRecording();

	if (!showFPS && !paused && !recording)
		return;

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x - 10.0f, io.DisplaySize.y - 10.0f),
		ImGuiCond_Always, ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.5f);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

	if (ImGui::Begin("##StatusOverlay", nullptr, flags)) {
		if (showFPS)
			ImGui::Text("%.0f FPS", io.Framerate);
		if (paused) {
			if (showFPS) ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "PAUSED");
		}
		if (recording) {
			if (showFPS || paused) ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC");
		}
	}
	ImGui::End();
}

// =========================================================================
// Top-level frame render
// =========================================================================

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	SDL_Renderer *renderer, ATUIState &state)
{
	ImGui_ImplSDLRenderer3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	SDL_Window *window = SDL_GetRenderWindow(renderer);

	RenderMainMenu(sim, window, renderer, state);
	RenderStatusOverlay(sim);

	if (state.showSystemConfig)    ATUIRenderSystemConfig(sim, state);
	if (state.showDiskManager)     ATUIRenderDiskManager(sim, state, window);
	if (state.showCassetteControl) ATUIRenderCassetteControl(sim, state, window);
	if (state.showAdjustColors)    ATUIRenderAdjustColors(sim, state);
	if (state.showDisplaySettings) ATUIRenderDisplaySettings(sim, state);
	if (state.showAboutDialog)     RenderAboutDialog(state);

	ImGui::Render();
	ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}
