//	AltirraSDL - Online Play deep-link helpers
//
//	One-click PLAY support: a URL like
//	  https://ilmenit.github.io/AltirraSDL/?s=<sessionId>
//	is rendered by external sites that consume /v1/public/sessions
//	from the lobby.  When the user clicks PLAY, the WASM page (or a
//	native build invoked with --join-session) reaches this module:
//
//	  - The page / argv parser calls ATNetplayUI::SetPendingDeepLink()
//	    with the session id (and optional entry code).  This may run
//	    very early in startup, before netplay state and lobby workers
//	    exist.
//
//	  - Each frame after the main loop is up, the netplay tick calls
//	    ATNetplayUI::DriveDeepLinkJoin().  It waits for first-run /
//	    setup-wizard / firmware to finish, then issues an async
//	    LobbyClient::GetById, populates State::Session::joinTarget,
//	    and fires the existing StartJoiningAction() so the deep-link
//	    path joins through exactly the same code as a Browser-row
//	    click.
//
//	Failure path: if the lobby returns 404 (session ended), times out,
//	or any other error, the user sees the standard Error screen with
//	a one-line explanation; they don't get stuck on a blank screen.

#pragma once

#include <string>

namespace ATNetplayUI {

// Stash a sessionId for the deep-link joiner to consume on a later
// frame.  Safe to call before any netplay state exists.  Calling
// twice overwrites the prior id (the latest URL wins, matching the
// way browser address bars behave).  Empty value is ignored — use
// ClearPendingDeepLink() to wipe.
void SetPendingDeepLinkSessionId(const std::string& sessionId);

// Optional entry-code for private sessions.  Independent of the
// sessionId setter so `--join-session` and `--join-code` flags can
// arrive in any order without clobbering each other.
void SetPendingDeepLinkCode(const std::string& entryCode);

// Erase any pending sessionId + entry code.  Used after a successful
// join is dispatched so a later refresh of the page (which won't
// have the URL params anymore) doesn't trigger a re-join.
void ClearPendingDeepLink();

// True iff a SetPendingDeepLink() call is still waiting to be acted
// on (used by UI affordances that want to show a "Joining…" hint).
bool HasPendingDeepLink();

// Called from the netplay tick.  Cheap no-op when nothing is pending
// or when the gate (first-run / setup-wizard / firmware / no other
// session in flight) is not yet open.  When the gate opens it kicks
// an async LobbyClient::GetById and, on success, fires the standard
// join action.  On any failure it routes the user to the Error
// screen with a friendly message.
void DriveDeepLinkJoin();

// Forward-declared from the lobby worker to avoid pulling its full
// header here.  (The .cpp #includes the worker header.)
struct LobbyResult;

// Called from ATNetplayUI_Poll's worker drain.  If `r` is the
// response to our own GetById fetch (LobbyOp::GetById with the
// deep-link tag), this consumes it and returns true; otherwise
// returns false so the caller falls through to its other handlers.
bool OnDeepLinkLobbyResult(const LobbyResult& r);

// One-click join UX states.  The Screen::DeepLinkPrep renderer
// switches on this each frame to pick what to draw.  The order
// roughly tracks the state machine; gaps are intentional so a future
// state can slot in without renumbering.
enum class DeepLinkUiState {
	NotPending,        // no deep-link in flight; screen should be Closed
	NeedsNickname,     // first-time visitor; ask for a name and persist
	DownloadingFw,     // WASM auto-fetching ROM bundle from kFirstRunUrls
	FirmwareFailed,    // all firmware mirrors failed; show error + retry
	Looking,           // GET /v1/session/<id> in flight on the lobby
	Joining,           // StartJoiningAction has fired; standard screens
	                   // (JoinConfirm / Waiting / AcceptJoinPrompt) take
	                   // over, but the deep-link UI may still be showing
	                   // until those screens push a new entry.
};

// Snapshot of where the deep-link state machine is.  Cheap; safe
// to call every frame from the renderer.
DeepLinkUiState GetDeepLinkUiState();

// Submit the user's nickname from the deep-link mini-prompt.  Persists
// to the netplay registry, marks first-run complete, and advances the
// state machine.  Empty input is rejected (caller should disable the
// Continue button).  Trims to <=24 chars to match the regular Nickname
// screen's limit; the lobby hard-caps at 32.
void SubmitDeepLinkNickname(const std::string& nick);

// Cancel the in-flight deep-link join (e.g. user closes the prep
// modal).  Wipes the pending session id so a later refresh doesn't
// re-trigger.  Safe to call from any phase.
void CancelDeepLink();

// Re-arm the firmware fetch after a previous all-mirrors-failed.
// Triggered from the FirmwareFailed UI's Retry button.  WASM only;
// no-op on native builds.
void RetryDeepLinkFirmware();

} // namespace ATNetplayUI
