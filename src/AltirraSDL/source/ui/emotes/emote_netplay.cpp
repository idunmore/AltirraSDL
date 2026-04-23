//	AltirraSDL - Online Play emote send/receive glue.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <atomic>

#include <vd2/system/registry.h>
#include <settings.h>

#include "emote_assets.h"
#include "emote_netplay.h"
#include "emote_overlay.h"
#include "netplay/netplay_glue.h"

namespace ATEmoteNetplay {

namespace {

constexpr uint64_t kSameCooldownMs      = 4000; // matches overlay duration
constexpr uint64_t kDifferentCooldownMs = 1500;

bool gSendEnabled    = true;
bool gReceiveEnabled = true;

// Registry-backed persistence via the same settings callback registry
// the rest of the Altirra frontend uses.  We persist under the
// Environment category — these are user-scope preferences, not
// hardware state, and the "Environment" bucket already holds other
// UI-scope bools.
ATSettingsLoadSaveCallback gLoadCallback;
ATSettingsLoadSaveCallback gSaveCallback;
bool gCallbacksRegistered = false;

void LoadCallback(uint32 /*profileId*/, ATSettingsCategory mask, VDRegistryKey& key) {
	if (!(mask & kATSettingsCategory_Environment)) return;
	gSendEnabled    = key.getBool("Netplay: Send emotes",    gSendEnabled);
	gReceiveEnabled = key.getBool("Netplay: Receive emotes", gReceiveEnabled);
}

void SaveCallback(uint32 /*profileId*/, ATSettingsCategory mask, VDRegistryKey& key) {
	if (!(mask & kATSettingsCategory_Environment)) return;
	key.setBool("Netplay: Send emotes",    gSendEnabled);
	key.setBool("Netplay: Receive emotes", gReceiveEnabled);
}

uint64_t gLastSentMs        = 0;
int      gLastSentIconId    = -1;
uint64_t gLastReceivedMs    = 0;
int      gLastReceivedId    = -1;

// Netplay thread -> main thread mailbox.  A single slot is enough:
// under the rate limit the peer can enqueue at most one per 1.5s, so
// dropping a second arrival is the correct behavior anyway.
std::atomic<int> gPendingIconId{-1};

bool PassesRateLimit(uint64_t nowMs, uint64_t &lastMs, int &lastId, int iconId) {
	if (lastId < 0) {
		lastMs = nowMs;
		lastId = iconId;
		return true;
	}
	uint64_t elapsed = nowMs - lastMs;
	uint64_t limit = (iconId == lastId) ? kSameCooldownMs : kDifferentCooldownMs;
	if (elapsed < limit)
		return false;
	lastMs = nowMs;
	lastId = iconId;
	return true;
}

} // namespace

void Initialize() {
	gPendingIconId.store(-1, std::memory_order_relaxed);
	gLastSentMs = 0;
	gLastSentIconId = -1;
	gLastReceivedMs = 0;
	gLastReceivedId = -1;

	if (!gCallbacksRegistered) {
		gLoadCallback = LoadCallback;
		gSaveCallback = SaveCallback;
		ATSettingsRegisterLoadCallback(&gLoadCallback);
		ATSettingsRegisterSaveCallback(&gSaveCallback);
		gCallbacksRegistered = true;
	}
}

void Shutdown() {
	gPendingIconId.store(-1, std::memory_order_relaxed);
	if (gCallbacksRegistered) {
		ATSettingsUnregisterLoadCallback(&gLoadCallback);
		ATSettingsUnregisterSaveCallback(&gSaveCallback);
		gCallbacksRegistered = false;
	}
}

bool Send(int iconId) {
	if (!gSendEnabled) return false;
	if (iconId < 0 || iconId >= ATEmotes::kCount) return false;
	if (!ATNetplayGlue::IsLockstepping()) return false;

	uint64_t nowMs = SDL_GetTicks();
	if (!PassesRateLimit(nowMs, gLastSentMs, gLastSentIconId, iconId))
		return false;

	bool sent = ATNetplayGlue::SendEmote((uint8_t)iconId);

	// Visual confirmation: play the bottom-right "outbound" overlay so
	// the sender sees what they just sent even if the peer's reply
	// doesn't arrive (or their receive toggle is off).  Tied to the
	// actual socket handoff — if the packet couldn't leave the machine
	// at all we don't lie to the user about having sent it.
	if (sent)
		ATEmoteOverlay::ShowOutbound(iconId, nowMs);

	return sent;
}

void Process(uint64_t nowMs) {
	int id = gPendingIconId.exchange(-1, std::memory_order_acq_rel);
	if (id < 0) return;

	if (!gReceiveEnabled) return;
	if (id >= ATEmotes::kCount) return;

	if (!PassesRateLimit(nowMs, gLastReceivedMs, gLastReceivedId, id))
		return;

	ATEmoteOverlay::Show(id, nowMs);
}

void OnReceivedFromPeer(uint8_t iconId) {
	// Called from the netplay socket thread.  Drop if a previous one
	// wasn't consumed yet — receiver-side rate limit would throw it
	// away anyway.
	gPendingIconId.store((int)iconId, std::memory_order_release);
}

bool GetSendEnabled()        { return gSendEnabled; }
void SetSendEnabled(bool v)  { gSendEnabled = v; }
bool GetReceiveEnabled()     { return gReceiveEnabled; }
void SetReceiveEnabled(bool v){ gReceiveEnabled = v; }

} // namespace ATEmoteNetplay
