//	AltirraSDL - Online Play deep-link state machine.
//
//	See ui_netplay_deeplink.h for the contract.  This file owns:
//	  - the pending sessionId / entry code stash (set by the WASM URL
//	    bridge or the native --join-session flag, before any netplay
//	    worker exists)
//	  - the per-frame state machine that consumes the stash once the
//	    rest of the app is ready, by issuing GET /v1/session/<id> via
//	    the lobby worker and then calling the standard
//	    StartJoiningAction.
//
//	Threading: the setters can be invoked from any startup thread.
//	DriveDeepLinkJoin() and OnDeepLinkLobbyResult() run on the main
//	thread from the netplay tick.  The async lobby fetch is enqueued on
//	the existing lobby worker; its result comes back through
//	ATNetplayUI_Poll's drain on the main thread the same way Browser
//	List() results do.

#include <stdafx.h>

#include "ui_netplay_deeplink.h"
#include "ui_netplay_state.h"
#include "ui_netplay_actions.h"
#include "ui_netplay_lobby_worker.h"
#include "ui_netplay.h"

#include "netplay/netplay_glue.h"

#include <vd2/system/registry.h>

#include <mutex>

namespace ATNetplayUI {

// Defined in ui_netplay.cpp.  Forward-declared here (not in the
// public header) to keep the worker singleton an implementation
// detail of the netplay UI module.
LobbyWorker& GetWorker();

namespace {

// Module-local stash + state machine bookkeeping.
//
// The setters (Set*DeepLink*) can fire arbitrarily early — for native
// builds, before main() has even reached the SDL init pump; for WASM,
// before the emscripten runtime has booted.  Only a mutex-guarded
// pair of strings is touched at that point.
//
// The state machine fields below (g_phase, g_fetchTag) are only
// touched on the main thread, from DriveDeepLinkJoin and
// OnDeepLinkLobbyResult.  They don't need the mutex.
std::mutex     g_deepLinkMu;
std::string    g_pendingSessionId;     // protected by g_deepLinkMu
std::string    g_pendingEntryCode;     // protected by g_deepLinkMu

enum class Phase {
	Idle,           // nothing pending or last attempt finished
	FetchInFlight,  // GetById posted, waiting for OnDeepLinkLobbyResult
	Done,           // terminal — error already routed or join fired
};
Phase    g_phase    = Phase::Idle;
uint32_t g_fetchTag = 0;

// Distinct tag for our GetById posts so the result handler can
// recognise its own work and ignore other ops that happen to share
// the worker queue.  The high bit avoids collision with the offer-
// hash tags that ReconcileHostedGames uses for per-game Create.
constexpr uint32_t kDeepLinkTagBase = 0x5DEA1ED0u;

// Read the same registry key the Gaming Mode setup wizard uses to
// signal "user has been through first-run".  We don't link against
// ui_mobile.cpp's IsFirstRunComplete() to keep this module's
// dependency surface narrow (and to compile on builds that don't
// even include the mobile UI sources).
bool IsFirstRunComplete() {
	VDRegistryAppKey key("Mobile", false);
	if (!key.isReady()) return false;
	return key.getBool("FirstRunComplete", false);
}

void RouteToError(const std::string& msg) {
	State& st = GetState();
	st.session.lastError = msg;
	Navigate(Screen::Error);
}

} // namespace anonymous

void SetPendingDeepLinkSessionId(const std::string& sessionId) {
	if (sessionId.empty()) return;
	std::lock_guard<std::mutex> lk(g_deepLinkMu);
	g_pendingSessionId = sessionId;
}

void SetPendingDeepLinkCode(const std::string& entryCode) {
	std::lock_guard<std::mutex> lk(g_deepLinkMu);
	g_pendingEntryCode = entryCode;
}

void ClearPendingDeepLink() {
	std::lock_guard<std::mutex> lk(g_deepLinkMu);
	g_pendingSessionId.clear();
	g_pendingEntryCode.clear();
}

bool HasPendingDeepLink() {
	std::lock_guard<std::mutex> lk(g_deepLinkMu);
	return !g_pendingSessionId.empty();
}

void DriveDeepLinkJoin() {
	// Cheap early-out: nothing to do unless the user actually arrived
	// via a deep-link URL.
	std::string sessionId, entryCode;
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		sessionId = g_pendingSessionId;
		entryCode = g_pendingEntryCode;
	}
	if (sessionId.empty()) return;

	// Once we've terminated (error routed or join fired), don't try
	// again — even if the stash still has a leftover value.  A fresh
	// SetPendingDeepLinkSessionId() would be needed (e.g. a future
	// in-app "open this URL" flow).
	if (g_phase == Phase::Done) return;
	if (g_phase == Phase::FetchInFlight) return;  // wait for response

	// Gates — every one of these must be open before we commit:
	//
	//   - GetWorker().IsRunning(): the worker thread / fetch helpers
	//     must be alive, otherwise Post() drops the request.
	//   - !ATNetplayGlue::IsActive(): we must not already be in a
	//     session (the user could have manually joined something else
	//     between the URL click and this tick).
	//   - IsFirstRunComplete(): the Gaming Mode setup wizard must be
	//     done (firmware installed, nickname picked).  Without this
	//     the join would fail with "missing kernel" and confuse the
	//     user.  Desktop UI doesn't run the wizard but writes the
	//     same flag, so the check is correct in both modes.
	if (!GetWorker().IsRunning())   return;
	if (ATNetplayGlue::IsActive())  return;
	if (!IsFirstRunComplete())      return;

	// All gates open — issue the lobby fetch.  We pick the first
	// enabled HTTP lobby; if the user has multiple federated lobbies
	// configured, the deep-link assumes the one that minted the URL
	// is the same one configured first (which is the default —
	// lobby.atari.org.pl).  A failed fetch falls through to the
	// Error screen, so a wrong-lobby URL surfaces as "session not
	// found" rather than silently retrying forever.
	auto lobbies = AllEnabledHttpLobbies();
	if (lobbies.empty()) {
		RouteToError(
			"Online Play isn't configured.  Open Online Play → Settings "
			"to add a lobby, then try the link again.");
		ClearPendingDeepLink();
		g_phase = Phase::Done;
		return;
	}

	LobbyRequest req;
	req.op        = LobbyOp::GetById;
	req.endpoint  = lobbies.front().endpoint;
	req.sessionId = sessionId;
	req.tag       = kDeepLinkTagBase;
	g_fetchTag    = req.tag;

	if (!GetWorker().Post(std::move(req), lobbies.front().section)) {
		RouteToError("Couldn't reach the lobby — try again in a moment.");
		ClearPendingDeepLink();
		g_phase = Phase::Done;
		return;
	}
	g_phase = Phase::FetchInFlight;
}

bool OnDeepLinkLobbyResult(const LobbyResult& r) {
	if (r.op != LobbyOp::GetById)   return false;
	if (r.tag != g_fetchTag)        return false;

	g_phase = Phase::Idle;  // clear in-flight; next branch sets terminal

	State& st = GetState();
	if (!r.ok || r.sessions.empty()) {
		// Friendly translation by HTTP status.
		if (r.httpStatus == 404) {
			RouteToError(
				"That game is no longer being hosted.  The host may have "
				"ended the session or restarted.");
		} else if (r.httpStatus == 0) {
			RouteToError(
				"Couldn't reach the lobby.  Check your connection and "
				"try the link again.");
		} else {
			std::string msg = "Couldn't load the deep-link session";
			if (!r.error.empty()) {
				msg += " — ";
				msg += r.error;
			} else if (r.httpStatus > 0) {
				char buf[64];
				std::snprintf(buf, sizeof buf, " (HTTP %d)", r.httpStatus);
				msg += buf;
			}
			msg += ".";
			RouteToError(msg);
		}
		ClearPendingDeepLink();
		g_phase = Phase::Done;
		return true;
	}

	// Populate the standard join target so StartJoiningAction can run
	// the same code path a Browser-row click does.
	st.session.joinTarget = r.sessions.front();
	st.session.joinTarget.sourceLobby = r.sourceLobby;

	// Apply any entry-code captured by --join-code / ?code=…
	{
		std::lock_guard<std::mutex> lk(g_deepLinkMu);
		st.session.joinEntryCode = g_pendingEntryCode;
	}

	// Wipe the stash before we hand off — a successful join should
	// not re-trigger if the user later navigates back and forth.
	ClearPendingDeepLink();
	g_phase = Phase::Done;

	StartJoiningAction();
	return true;
}

} // namespace ATNetplayUI
