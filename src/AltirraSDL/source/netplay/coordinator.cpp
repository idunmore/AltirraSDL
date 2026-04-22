// Altirra SDL3 netplay - session coordinator (impl)

#include <stdafx.h>

#include "coordinator.h"

#include "protocol.h"

#include <at/atcore/logging.h>

#include <cstring>

// Single shared channel for every netplay subsystem.  Default ON
// (unlike most Altirra log channels) so users can copy diagnostics
// straight from the terminal when a session misbehaves.  Kept
// deliberately sparse: one line per state transition, none per frame.
ATLogChannel g_ATLCNetplay(true, true, "NETPLAY", "Netplay session");

namespace ATNetplay {

namespace {

bool EntryCodeMatches(const uint8_t* a, const uint8_t* b) {
	// Constant-time compare would be nice against malicious peers,
	// but the code isn't a secret — it's a convenience gate, not a
	// crypto token.  Plain memcmp is fine.
	return std::memcmp(a, b, kEntryCodeHashLen) == 0;
}

bool IsZeroEntryCode(const uint8_t* h) {
	for (size_t i = 0; i < kEntryCodeHashLen; ++i)
		if (h[i] != 0) return false;
	return true;
}

} // anonymous

Coordinator::Coordinator() = default;
Coordinator::~Coordinator() {
	if (mPhase == Phase::Lockstepping ||
	    mPhase == Phase::SendingSnapshot ||
	    mPhase == Phase::ReceivingSnapshot ||
	    mPhase == Phase::SnapshotReady) {
		// Best-effort farewell if the app forgot to End() us.
		SendBye(kByeCleanExit);
	}
}

// ---------------------------------------------------------------------------
// Host entrypoint
// ---------------------------------------------------------------------------

bool Coordinator::BeginHost(uint16_t localPort,
                            const char* playerHandle,
                            const char* cartName,
                            uint64_t osRomHash,
                            uint64_t basicRomHash,
                            uint64_t settingsHash,
                            uint16_t inputDelayFrames,
                            const uint8_t* entryCodeHash,
                            const NetBootConfig& bootConfig) {
	mBootConfig = bootConfig;
	if (!mTransport.Listen(localPort)) {
		FailWith("failed to bind UDP socket");
		return false;
	}

	// Record our configuration to populate NetWelcome later.
	HandleFromString(playerHandle, reinterpret_cast<uint8_t*>(mPlayerHandle));
	CartFromString(cartName, reinterpret_cast<uint8_t*>(mCartName));
	mOsRomHash = osRomHash;
	mBasicRomHash = basicRomHash;
	mSettingsHash = settingsHash;
	mInputDelay = inputDelayFrames;
	if (entryCodeHash) {
		std::memcpy(mEntryCodeHash, entryCodeHash, kEntryCodeHashLen);
		mHasEntryCode = !IsZeroEntryCode(mEntryCodeHash);
	} else {
		std::memset(mEntryCodeHash, 0, kEntryCodeHashLen);
		mHasEntryCode = false;
	}

	mRole = Role::Host;
	mPhase = Phase::WaitingForJoiner;
	mLastError = "";
	g_ATLCNetplay("host: listening on UDP port %u (cart=\"%s\" private=%s delay=%u)",
		(unsigned)mTransport.BoundPort(),
		cartName ? cartName : "(none)",
		mHasEntryCode ? "yes" : "no",
		(unsigned)mInputDelay);
	return true;
}

// ---------------------------------------------------------------------------
// Joiner entrypoint
// ---------------------------------------------------------------------------

bool Coordinator::BeginJoin(const char* hostAddress,
                            const char* playerHandle,
                            uint64_t osRomHash,
                            uint64_t basicRomHash,
                            bool acceptTos,
                            const uint8_t* entryCodeHash) {
	// Bind ephemeral local port.
	if (!mTransport.Listen(0)) {
		FailWith("failed to bind UDP socket");
		return false;
	}
	if (!Transport::Resolve(hostAddress, mPeer)) {
		FailWith("failed to resolve host address");
		mTransport.Close();
		return false;
	}
	mPeerKnown = true;

	HandleFromString(playerHandle, reinterpret_cast<uint8_t*>(mPlayerHandle));
	mOsRomHash = osRomHash;
	mBasicRomHash = basicRomHash;
	mAcceptTos = acceptTos;
	if (entryCodeHash) {
		std::memcpy(mEntryCodeHash, entryCodeHash, kEntryCodeHashLen);
		mHasEntryCode = !IsZeroEntryCode(mEntryCodeHash);
	} else {
		std::memset(mEntryCodeHash, 0, kEntryCodeHashLen);
		mHasEntryCode = false;
	}

	mRole = Role::Joiner;
	mPhase = Phase::Handshaking;
	mLastError = "";

	g_ATLCNetplay("joiner: resolved \"%s\", sending Hello", hostAddress);
	SendHello();
	return true;
}

// ---------------------------------------------------------------------------
// Poll
// ---------------------------------------------------------------------------

void Coordinator::Poll(uint64_t nowMs) {
	if (mPhase == Phase::Idle ||
	    mPhase == Phase::Ended ||
	    mPhase == Phase::Failed ||
	    mPhase == Phase::Desynced) {
		return;
	}

	// Drain the socket.
	for (int drained = 0; drained < 64; ++drained) {
		size_t n = 0;
		Endpoint from;
		RecvResult r = mTransport.RecvFrom(mRxBuf, sizeof mRxBuf, n, from);
		if (r == RecvResult::WouldBlock) break;
		if (r == RecvResult::Error)      break;

		uint32_t magic = 0;
		if (!PeekMagic(mRxBuf, n, magic)) continue;

		switch (magic) {
			case kMagicHello: {
				NetHello h;
				if (DecodeHello(mRxBuf, n, h) != DecodeResult::Ok) continue;
				HandleHelloFromJoiner(h, from, nowMs);
				break;
			}
			case kMagicWelcome: {
				NetWelcome w;
				if (DecodeWelcome(mRxBuf, n, w) != DecodeResult::Ok) continue;
				HandleWelcomeFromHost(w, nowMs);
				break;
			}
			case kMagicReject: {
				NetReject rj;
				if (DecodeReject(mRxBuf, n, rj) != DecodeResult::Ok) continue;
				HandleRejectFromHost(rj);
				break;
			}
			case kMagicInput: {
				NetInputPacket ip;
				if (DecodeInputPacket(mRxBuf, n, ip) != DecodeResult::Ok) continue;
				HandleInputPacket(ip, nowMs);
				break;
			}
			case kMagicChunk: {
				NetSnapChunk c;
				if (DecodeSnapChunk(mRxBuf, n, c) != DecodeResult::Ok) continue;
				HandleSnapChunk(c, nowMs);
				break;
			}
			case kMagicAck: {
				NetSnapAck a;
				if (DecodeSnapAck(mRxBuf, n, a) != DecodeResult::Ok) continue;
				HandleSnapAck(a);
				break;
			}
			case kMagicBye: {
				NetBye b;
				if (DecodeBye(mRxBuf, n, b) != DecodeResult::Ok) continue;
				HandleBye(b, from);
				break;
			}
			case kMagicResyncStart: {
				NetResyncStart s;
				if (DecodeResyncStart(mRxBuf, n, s) != DecodeResult::Ok) continue;
				HandleResyncStart(s, nowMs);
				break;
			}
			case kMagicResyncDone: {
				NetResyncDone d;
				if (DecodeResyncDone(mRxBuf, n, d) != DecodeResult::Ok) continue;
				HandleResyncDone(d, nowMs);
				break;
			}
			default:
				// Unknown magic — silently ignore (could be from a
				// different version or a stray packet).
				break;
		}
	}

	// Outgoing pumps.  Snapshot sender is shared between the session-
	// start upload (Phase::SendingSnapshot) and the mid-session resync
	// (Phase::Resyncing) — it doesn't care which phase it's serving.
	if (mPhase == Phase::SendingSnapshot || mPhase == Phase::Resyncing) {
		PumpSnapshotSender(nowMs);
	}
	if (mPhase == Phase::Lockstepping && mWantsOutgoingInput) {
		PumpLockstepSend();
		mWantsOutgoingInput = false;
	}
	// Lockstep: detect desync transition.  Host: initiate a resync by
	// capturing its sim state and streaming it to the joiner.  Joiner:
	// stay in Lockstepping with mLoop.IsDesynced() gating CanAdvance —
	// the sim naturally pauses, and the host will detect within ~1 RTT
	// (via retro-check on the next peer input packet) and drive the
	// resync from its side.
	if (mPhase == Phase::Lockstepping && mLoop.IsDesynced()) {
		if (!mDesyncLogged) {
			g_ATLCNetplay("DESYNC detected at frame %lld (role=%s, delay=%u)",
				(long long)mLoop.DesyncFrame(),
				mRole == Role::Host ? "host" : "joiner",
				(unsigned)mInputDelay);
			mDesyncLogged = true;
		}
		if (mRole == Role::Host) {
			BeginHostResync(nowMs);
		}
		// joiner: nothing to do here; wait for NetResyncStart.
	}

	// Host resync: retransmit NetResyncStart until the joiner acks at
	// least one chunk.  Absorbs a one-packet loss of the initial start
	// without blowing the SnapTx retry budget trying to push chunks the
	// joiner is dropping.
	if (mPhase == Phase::Resyncing && mRole == Role::Host &&
	    mSnapTx.AcknowledgedChunks() == 0 && mPeerKnown &&
	    nowMs - mResyncStartLastSentMs >= kResyncStartRetryMs) {
		NetResyncStart s;
		s.magic        = kMagicResyncStart;
		s.epoch        = mResyncEpoch;
		s.stateBytes   = (uint32_t)mResyncTxBuffer.size();
		s.stateChunks  = mSnapTx.TotalChunks();
		s.resumeFrame  = mResyncResumeFrame;
		s.seedHash     = mResyncSeedHash;
		size_t nn = EncodeResyncStart(s, mTxBuf, sizeof mTxBuf);
		if (nn) {
			mTransport.SendTo(mTxBuf, nn, mPeer);
			mResyncStartLastSentMs = nowMs;
			g_ATLCNetplay("host: re-sent ResyncStart (epoch %u, no chunks acked yet)",
				(unsigned)mResyncEpoch);
		}
	}

	// Lockstep: peer-silence timeout.  With the lockstep design the
	// game naturally pauses on both sides when peer packets stop
	// arriving (our gate won't open), so there's no crash — but if
	// the peer genuinely died or dropped off the network we'd otherwise
	// sit frozen forever.  The UI now shows an interactive "peer
	// unresponsive" prompt at ~3 s; this long backstop only fires if
	// the user dismissed the prompt and then walked away.  Auto-
	// termination without user consent is explicitly unwanted.
	if (mPhase == Phase::Lockstepping) {
		constexpr uint64_t kPeerTimeoutMs = 600000;  // 10 minutes
		if (mLoop.PeerTimedOut(nowMs, kPeerTimeoutMs)) {
			g_ATLCNetplay("%s: peer silent for >%llu ms — timing out "
				"(last packet %llu ms ago)",
				mRole == Role::Host ? "host" : "joiner",
				(unsigned long long)kPeerTimeoutMs,
				(unsigned long long)(nowMs - mLoop.LastPeerRecvMs()));
			SendBye(kByeTimeout);
			mPhase = Phase::Ended;
			mLastError = "peer timed out";
		}
	}
}

// ---------------------------------------------------------------------------
// Host: handle joiner Hello
// ---------------------------------------------------------------------------

void Coordinator::HandleHelloFromJoiner(const NetHello& hello,
                                        const Endpoint& from,
                                        uint64_t nowMs) {
	if (mPhase != Phase::WaitingForJoiner) {
		// Host is already past the listening phase (handshake in
		// progress, snapshot streaming, or in lockstep).  Tell the
		// joiner we're full so they get a clear message instead of
		// hanging in "Connecting…" until their handshake timeout.
		SendReject(kRejectHostFull, from);
		return;
	}

	if (hello.protocolVersion != kProtocolVersion) {
		SendReject(kRejectVersionSkew, from);
		return;
	}
	if (hello.osRomHash != mOsRomHash) {
		SendReject(kRejectOsMismatch, from);
		return;
	}
	if (hello.basicRomHash != mBasicRomHash) {
		SendReject(kRejectBasicMismatch, from);
		return;
	}
	if ((hello.acceptTos & 1u) == 0) {
		SendReject(kRejectTosNotAccept, from);
		return;
	}
	if (mHasEntryCode) {
		if (!EntryCodeMatches(hello.entryCodeHash, mEntryCodeHash)) {
			SendReject(kRejectBadEntryCode, from);
			return;
		}
	}

	// Hello passed validation.  If prompt-accept is enabled, enqueue
	// the joiner and wait for the UI to call AcceptPendingJoiner(i)
	// or RejectPendingJoiner(i).  Duplicate Hellos from the same
	// endpoint (the joiner retransmits every ~500 ms while waiting)
	// refresh the arrival timestamp and nothing else.  We stay in
	// WaitingForJoiner until an Accept picks one entry, so a Reject on
	// any single row can return to listening cleanly.
	if (mPromptAccept) {
		for (auto& e : mPendingDecisions) {
			if (e.peer.Equals(from)) {
				// Refresh arrivedMs (we keep the oldest for the UI
				// "Requested Xs ago" display) — actually keep the
				// first arrival time; retransmits are not new requests.
				return;
			}
		}
		if (mPendingDecisions.size() >= kMaxPendingDecisions) {
			// Queue is full.  Bounce the joiner with HostFull so they
			// get a clear error instead of a silent timeout.
			SendReject(kRejectHostFull, from);
			return;
		}
		PendingDecision pd;
		pd.peer = from;
		std::memset(pd.handle, 0, sizeof pd.handle);
		std::memcpy(pd.handle, hello.playerHandle, kHandleLen);
		pd.arrivedMs = nowMs;
		mPendingDecisions.push_back(pd);
		char ep[32] = {};
		from.Format(ep, sizeof ep);
		g_ATLCNetplay("host: incoming join from \"%s\" at %s — "
			"awaiting host decision (queue depth %zu)",
			pd.handle, ep, mPendingDecisions.size());
		return;
	}

	// Hello accepted.  Lock in the peer.
	mPeer = from;
	mPeerKnown = true;
	{
		char handle[kHandleLen + 1] = {};
		std::memcpy(handle, hello.playerHandle, kHandleLen);
		char ep[32] = {};
		from.Format(ep, sizeof ep);
		g_ATLCNetplay("host: accepted joiner \"%s\" from %s",
			handle, ep);
	}

	if (mSnapTxBuffer.empty()) {
		// The app hasn't uploaded the snapshot yet.  Defer sending
		// Welcome until SubmitSnapshotForUpload() is called.
		mHostHasPendingJoiner = true;
		mPendingJoiner = from;
		// Advance phase so SubmitSnapshotForUpload knows we're
		// expecting it.
		mPhase = Phase::SendingSnapshot;
		return;
	}

	// Snapshot already submitted (app was proactive).  Welcome now.
	mPhase = Phase::SendingSnapshot;
	SendWelcome();
	mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
}

// ---------------------------------------------------------------------------
// Prompt-accept gate
// ---------------------------------------------------------------------------

const char* Coordinator::PendingJoinerHandle(size_t i) const {
	if (i >= mPendingDecisions.size()) return "";
	return mPendingDecisions[i].handle;
}

uint64_t Coordinator::PendingArrivedMs(size_t i) const {
	if (i >= mPendingDecisions.size()) return 0;
	return mPendingDecisions[i].arrivedMs;
}

void Coordinator::AcceptPendingJoiner(size_t i) {
	if (i >= mPendingDecisions.size()) return;
	if (mPhase != Phase::WaitingForJoiner) {
		// Phase moved out from under us (timeout, End, etc.).  Drop
		// the queue silently.
		mPendingDecisions.clear();
		return;
	}

	// Tell every other queued joiner that the host chose someone else
	// so they stop waiting.  Use kRejectHostFull to match the message
	// they would have got if they'd arrived after a silent auto-accept.
	for (size_t k = 0; k < mPendingDecisions.size(); ++k) {
		if (k == i) continue;
		SendReject(kRejectHostFull, mPendingDecisions[k].peer);
	}

	PendingDecision chosen = mPendingDecisions[i];
	mPendingDecisions.clear();

	// Adopt the accepted peer and run the same path the auto-accept
	// branch used to (see HandleHelloFromJoiner).
	mPeer      = chosen.peer;
	mPeerKnown = true;

	char ep[32] = {};
	mPeer.Format(ep, sizeof ep);
	g_ATLCNetplay("host: accepted joiner \"%s\" from %s (manual approval)",
		chosen.handle, ep);

	if (mSnapTxBuffer.empty()) {
		mHostHasPendingJoiner = true;
		mPendingJoiner = mPeer;
		mPhase = Phase::SendingSnapshot;
		return;
	}

	mPhase = Phase::SendingSnapshot;
	SendWelcome();
	mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
}

void Coordinator::RejectPendingJoiner(size_t i) {
	if (i >= mPendingDecisions.size()) return;
	const PendingDecision& pd = mPendingDecisions[i];
	g_ATLCNetplay("host: rejected joiner \"%s\" (manual decline)",
		pd.handle);
	SendReject(kRejectHostRejected, pd.peer);
	mPendingDecisions.erase(mPendingDecisions.begin() + i);
	// Stay in WaitingForJoiner — another entry may still be queued and
	// new peers can still arrive.
}

// ---------------------------------------------------------------------------
// SubmitSnapshotForUpload
// ---------------------------------------------------------------------------

void Coordinator::SubmitSnapshotForUpload(const uint8_t* data, size_t len) {
	if (mRole != Role::Host) return;
	mSnapTxBuffer.assign(data, data + len);

	if (mHostHasPendingJoiner) {
		mHostHasPendingJoiner = false;
		mPhase = Phase::SendingSnapshot;
		SendWelcome();
		mSnapTx.Begin(mSnapTxBuffer.data(), mSnapTxBuffer.size());
	}
	// If no joiner yet, bytes sit in mSnapTxBuffer until one arrives.
}

// ---------------------------------------------------------------------------
// Joiner: handle Welcome
// ---------------------------------------------------------------------------

void Coordinator::HandleWelcomeFromHost(const NetWelcome& w, uint64_t /*nowMs*/) {
	if (mPhase != Phase::Handshaking) return;

	// Adopt host-chosen parameters.
	mInputDelay = w.inputDelayFrames;
	std::memcpy(mCartName, w.cartName, kCartLen);
	mSettingsHash = w.settingsHash;
	mBootConfig = w.boot;

	// Prepare receiver.
	mSnapRx.Begin(w.snapshotChunks, w.snapshotBytes);
	mPhase = Phase::ReceivingSnapshot;
	g_ATLCNetplay("joiner: Welcome accepted (snapshot=%u bytes / %u chunks, delay=%u)",
		(unsigned)w.snapshotBytes, (unsigned)w.snapshotChunks,
		(unsigned)w.inputDelayFrames);

	// If the snapshot is zero-length (edge case: nothing to ship),
	// move straight to SnapshotReady.
	if (w.snapshotChunks == 0) {
		mPhase = Phase::SnapshotReady;
	}
}

void Coordinator::HandleRejectFromHost(const NetReject& r) {
	if (mPhase != Phase::Handshaking) return;
	mPhase = Phase::Failed;
	switch (r.reason) {
		case kRejectOsMismatch:     mLastError = "OS ROM does not match the host's. Install the matching firmware and try again."; break;
		case kRejectBasicMismatch:  mLastError = "BASIC ROM does not match the host's. Install the matching firmware and try again."; break;
		case kRejectVersionSkew:    mLastError = "Protocol version mismatch — your Altirra build is incompatible with the host's."; break;
		case kRejectTosNotAccept:   mLastError = "Host requires terms-of-service acceptance."; break;
		case kRejectHostFull:       mLastError = "The host chose another player, or is handling too many requests right now."; break;
		case kRejectHostNotReady:   mLastError = "Host is not ready to accept joiners yet."; break;
		case kRejectBadEntryCode:   mLastError = "Incorrect join code. Ask the host for the right code and try again."; break;
		case kRejectHostRejected:   mLastError = "The host declined your request to join."; break;
		default:                    mLastError = "Host rejected the connection (unknown reason)."; break;
	}
	g_ATLCNetplay("joiner: %s (reason code %u)", mLastError, (unsigned)r.reason);
}

// ---------------------------------------------------------------------------
// Snapshot chunking (bidirectional)
// ---------------------------------------------------------------------------

void Coordinator::HandleSnapChunk(const NetSnapChunk& c, uint64_t /*nowMs*/) {
	const bool sessionStart = (mPhase == Phase::ReceivingSnapshot);
	const bool midResync    = (mPhase == Phase::Resyncing && mRole == Role::Joiner);
	if (!sessionStart && !midResync) return;

	if (mSnapRx.OnChunk(c)) {
		NetSnapAck ack { kMagicAck, c.chunkIdx };
		size_t n = EncodeSnapAck(ack, mTxBuf, sizeof mTxBuf);
		if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);
	}

	if (mSnapRx.IsComplete()) {
		if (sessionStart) {
			mPhase = Phase::SnapshotReady;
			g_ATLCNetplay("joiner: snapshot download complete, applying…");
		} else {
			// Mid-session resync: flag the app to call ApplySavestate +
			// AcknowledgeResyncApplied.  Phase stays Resyncing until
			// the app acks.
			mNeedsResyncApply = true;
			g_ATLCNetplay("joiner: resync payload complete (epoch %u), awaiting apply",
				(unsigned)mResyncEpoch);
		}
	}
}

void Coordinator::HandleSnapAck(const NetSnapAck& a) {
	const bool sessionStart = (mPhase == Phase::SendingSnapshot);
	const bool midResync    = (mPhase == Phase::Resyncing && mRole == Role::Host);
	if (!sessionStart && !midResync) return;

	mSnapTx.OnAckReceived(a.chunkIdx);
	if (mSnapTx.GetStatus() == SnapshotSender::Status::Done) {
		if (sessionStart) {
			// Host is ready for lockstep immediately; joiner transitions
			// to Lockstepping when it calls AcknowledgeSnapshotApplied.
			mLoop.Begin(Slot::Host, mInputDelay);
			mPhase = Phase::Lockstepping;
			g_ATLCNetplay("host: snapshot delivered, entering Lockstepping (delay=%u frames)",
				(unsigned)mInputDelay);
		} else {
			// Resync: all chunks acknowledged.  Now wait for ResyncDone
			// from the joiner confirming successful apply — only then
			// does the host resume lockstep.
			g_ATLCNetplay("host: resync payload delivered (epoch %u), awaiting ResyncDone",
				(unsigned)mResyncEpoch);
		}
	} else if (mSnapTx.GetStatus() == SnapshotSender::Status::Failed) {
		if (sessionStart) {
			FailWith("snapshot upload failed (retry budget exhausted)");
		} else {
			// Resync transfer exhausted retries — treat as unrecoverable.
			SendBye(kByeDesyncDetected);
			mPhase = Phase::Desynced;
			mLastError = "resync payload upload failed";
			g_ATLCNetplay("host: resync transfer failed — terminating session");
		}
	}
}

void Coordinator::PumpSnapshotSender(uint64_t nowMs) {
	if (!mPeerKnown) return;
	NetSnapChunk c;
	while (mSnapTx.NextOutgoing(c, nowMs)) {
		size_t n = EncodeSnapChunk(c, mTxBuf, sizeof mTxBuf);
		if (n == 0) break;
		mTransport.SendTo(mTxBuf, n, mPeer);
		// Emit ONE packet per Poll() to avoid flooding — the retry
		// timer (500 ms) paces subsequent re-sends naturally.  We
		// need to break after the first emission to match that
		// pacing intent; without this, NextOutgoing would re-fire
		// for the same chunk on the very next iteration if nowMs
		// increments within the loop's view (it doesn't, but the
		// intent is clearer with an explicit break).
		break;
	}
}

// ---------------------------------------------------------------------------
// AcknowledgeSnapshotApplied (joiner)
// ---------------------------------------------------------------------------

void Coordinator::AcknowledgeSnapshotApplied() {
	if (mPhase != Phase::SnapshotReady) return;
	mLoop.Begin(Slot::Joiner, mInputDelay);
	mPhase = Phase::Lockstepping;
	g_ATLCNetplay("joiner: snapshot applied, entering Lockstepping (delay=%u frames)",
		(unsigned)mInputDelay);
}

// ---------------------------------------------------------------------------
// Lockstep hot path
// ---------------------------------------------------------------------------

void Coordinator::HandleInputPacket(const NetInputPacket& pkt, uint64_t nowMs) {
	if (mPhase != Phase::Lockstepping) return;
	mLoop.OnPeerInputPacket(pkt, nowMs);
}

void Coordinator::OnFrameAdvanced(uint32_t simStateHash) {
	if (mPhase != Phase::Lockstepping) return;
	mLoop.OnFrameAdvanced(simStateHash);
	// Send the outgoing packet IMMEDIATELY rather than deferring it
	// to the next Poll().  The peer is likely already stalled waiting
	// for this frame's input — deferring would cost them a full render
	// + vsync cycle (~16 ms) before our packet is transmitted, which
	// halves effective lockstep throughput on localhost and high-RTT
	// links alike.  PumpLockstepSend is cheap (one encode + sendto).
	PumpLockstepSend();
	mWantsOutgoingInput = false;
}

void Coordinator::PumpLockstepSend() {
	if (!mPeerKnown) return;
	NetInputPacket pkt;
	mLoop.BuildOutgoingInputPacket(pkt);
	size_t n = EncodeInputPacket(pkt, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, mPeer);
}

// ---------------------------------------------------------------------------
// Bye / termination
// ---------------------------------------------------------------------------

void Coordinator::HandleBye(const NetBye& b, const Endpoint& from) {
	// Host-side special case: a Bye from a joiner that's only *queued*
	// (not yet the locked-in peer) means that joiner cancelled before
	// we got around to accepting them.  Just drop them from the queue —
	// do not end the listening phase.  Without this the host's whole
	// offer would collapse the moment any queued peer gave up waiting.
	if (mRole == Role::Host && mPhase == Phase::WaitingForJoiner) {
		for (size_t k = 0; k < mPendingDecisions.size(); ++k) {
			if (mPendingDecisions[k].peer.Equals(from)) {
				char ep[32] = {};
				from.Format(ep, sizeof ep);
				g_ATLCNetplay("host: queued joiner \"%s\" at %s cancelled "
					"before decision",
					mPendingDecisions[k].handle, ep);
				mPendingDecisions.erase(mPendingDecisions.begin() + k);
				return;
			}
		}
		// Bye from a stranger while listening — ignore.
		return;
	}

	// Peer said goodbye; treat as clean exit unless they flagged
	// desync.
	const char *reasonStr = "clean";
	switch (b.reason) {
		case kByeCleanExit:       reasonStr = "clean";         break;
		case kByeDesyncDetected:  reasonStr = "desync";        break;
		case kByeTimeout:         reasonStr = "timeout";       break;
		case kByeVersionMismatch: reasonStr = "ver-mismatch";  break;
		default:                  reasonStr = "unknown";       break;
	}
	g_ATLCNetplay("%s: peer said Bye (%s)",
		mRole == Role::Host ? "host" : "joiner", reasonStr);
	// A desync Bye from the peer is terminal regardless of our current
	// phase — Lockstepping (their apply-time check fired before ours)
	// or Resyncing (they hit their flap limit / apply error mid-
	// recovery).  Without handling the Resyncing case, the UI would
	// show "clean exit" for an unrecoverable desync.
	if (b.reason == kByeDesyncDetected &&
	    (mPhase == Phase::Lockstepping || mPhase == Phase::Resyncing)) {
		mPhase = Phase::Desynced;
		mLastError = "peer reported desync";
	} else {
		mPhase = Phase::Ended;
	}
}

void Coordinator::End(uint32_t byeReason) {
	if (mPhase == Phase::Idle || mPhase == Phase::Ended || mPhase == Phase::Failed)
		return;
	// Tell every still-queued joiner that we're not coming — without
	// this they sit in "Connecting…" until their own handshake timeout
	// fires.  Use kRejectHostFull; the session is ending from under
	// them, not a per-peer rejection.
	for (const auto& pd : mPendingDecisions) {
		SendReject(kRejectHostFull, pd.peer);
	}
	mPendingDecisions.clear();
	SendBye(byeReason);
	mPhase = Phase::Ended;
}

void Coordinator::SendBye(uint32_t reason) {
	if (!mPeerKnown) return;
	NetBye b { kMagicBye, reason };
	size_t n = EncodeBye(b, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, mPeer);
}

// ---------------------------------------------------------------------------
// Packet senders
// ---------------------------------------------------------------------------

void Coordinator::SendHello() {
	NetHello h;
	h.magic = kMagicHello;
	h.protocolVersion = kProtocolVersion;
	h.flags = 0;
	// sessionNonce: all zero for v1.0 — the field is reserved for
	// future control-packet auth.  Leaving it zero costs nothing now
	// and mirrors the Go PoC.
	h.osRomHash = mOsRomHash;
	h.basicRomHash = mBasicRomHash;
	std::memcpy(h.playerHandle, mPlayerHandle, kHandleLen);
	h.acceptTos = mAcceptTos ? 1u : 0u;
	std::memcpy(h.entryCodeHash, mEntryCodeHash, kEntryCodeHashLen);

	size_t n = EncodeHello(h, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);
}

void Coordinator::SendWelcome() {
	NetWelcome w;
	w.magic = kMagicWelcome;
	w.inputDelayFrames = mInputDelay;
	w.playerSlot = 2;    // first joiner is always P2
	std::memcpy(w.cartName, mCartName, kCartLen);
	w.snapshotBytes = (uint32_t)mSnapTxBuffer.size();
	w.snapshotChunks =
		(uint32_t)((mSnapTxBuffer.size() + kSnapshotChunkSize - 1)
			/ kSnapshotChunkSize);
	w.settingsHash = mSettingsHash;
	w.boot = mBootConfig;

	size_t n = EncodeWelcome(w, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);
}

void Coordinator::SendReject(uint32_t reason, const Endpoint& to) {
	NetReject r { kMagicReject, reason };
	size_t n = EncodeReject(r, mTxBuf, sizeof mTxBuf);
	if (n) mTransport.SendTo(mTxBuf, n, to);
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Resync
// ---------------------------------------------------------------------------

void Coordinator::BeginHostResync(uint64_t nowMs) {
	if (mRole != Role::Host) return;
	if (mPhase != Phase::Lockstepping) return;

	// Flap limiter.  A resync that itself leads to another immediate
	// desync signals a deterministic bug no amount of state-copying
	// will fix; fall back to the legacy "Bye + Desynced" behaviour.
	if (mResyncCountInWindow == 0 ||
	    nowMs - mResyncWindowStartMs > kResyncFlapWindowMs) {
		mResyncWindowStartMs = nowMs;
		mResyncCountInWindow = 1;
	} else {
		++mResyncCountInWindow;
	}
	if (mResyncCountInWindow > kMaxResyncsPerWindow) {
		g_ATLCNetplay("host: resync flap limit reached (%d in %llu ms) — "
			"declaring unrecoverable desync",
			mResyncCountInWindow,
			(unsigned long long)(nowMs - mResyncWindowStartMs));
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
		mLastError = "repeated desync — session unrecoverable";
		return;
	}

	// resumeFrame = next-frame-to-apply.  The savestate the glue layer
	// is about to capture represents the state AFTER applying
	// (resumeFrame - 1) and BEFORE frame resumeFrame.  The seed hash
	// we seed into the joiner's lockstep is our local hash at
	// (resumeFrame - 1) — that's the hash the app layer computed on
	// the very state it's about to serialise.
	++mResyncEpoch;
	mResyncResumeFrame = mLoop.CurrentFrame();
	// Seed hash = our local hash of the last-applied frame, which is
	// exactly the hash the joiner will compute post-apply if the
	// savestate round-tripped cleanly.  Falls back to 0 if the slot
	// has already been recycled (shouldn't happen in practice — ring
	// is 256 frames, the last-applied slot is always fresh).  A zero
	// seed is treated as "no hash yet" by OnPeerInputPacket, which
	// just suppresses the retro-check for one frame — safe either way.
	mResyncSeedHash = 0;
	if (mResyncResumeFrame > 0) {
		mLoop.GetLocalHashAt(mResyncResumeFrame - 1, mResyncSeedHash);
	}

	// Clear the lockstep desync flag so CanAdvance() unblocks once we
	// transition back to Lockstepping.  The ResumeAt() call on resume
	// will wipe rings; we could clear it there, but clearing here is
	// harmless and keeps the invariant "mPhase != Lockstepping implies
	// CanAdvance is irrelevant".
	mLoop.ClearDesync();

	mSnapTxBuffer.clear();
	mResyncTxBuffer.clear();
	mNeedsResyncCapture = true;
	mPhase = Phase::Resyncing;

	g_ATLCNetplay("host: initiating resync epoch=%u resumeFrame=%u",
		(unsigned)mResyncEpoch, (unsigned)mResyncResumeFrame);
}

void Coordinator::SubmitResyncCapture(const uint8_t* data, size_t len) {
	if (mPhase != Phase::Resyncing) return;
	if (mRole != Role::Host) return;
	if (!mNeedsResyncCapture) return;
	mNeedsResyncCapture = false;

	if (!data || len == 0) {
		SendBye(kByeDesyncDetected);
		mPhase = Phase::Desynced;
		mLastError = "host failed to capture savestate";
		g_ATLCNetplay("host: SubmitResyncCapture got empty payload");
		return;
	}

	// Keep host's copy alive for the duration of the SnapTx transfer.
	mResyncTxBuffer.assign(data, data + len);

	const uint32_t chunks = (uint32_t)((mResyncTxBuffer.size() +
		kSnapshotChunkSize - 1) / kSnapshotChunkSize);

	// Send NetResyncStart — the joiner needs this BEFORE chunks so it
	// can arm its receiver for this epoch's chunk count.
	NetResyncStart s;
	s.magic        = kMagicResyncStart;
	s.epoch        = mResyncEpoch;
	s.stateBytes   = (uint32_t)mResyncTxBuffer.size();
	s.stateChunks  = chunks;
	s.resumeFrame  = mResyncResumeFrame;
	s.seedHash     = mResyncSeedHash;
	size_t n = EncodeResyncStart(s, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) {
		mTransport.SendTo(mTxBuf, n, mPeer);
		// Record the send so the retransmit watchdog in Poll() doesn't
		// fire for the next kResyncStartRetryMs.
		// (nowMs would be cleaner but SubmitResyncCapture isn't given
		// a clock; leave it at 0 — the watchdog treats that as "long
		// ago" and will issue one extra retransmit on the next tick,
		// which is harmless.)
	}

	// Kick off chunk transfer (uses existing SnapTx machinery).
	mSnapTx.Begin(mResyncTxBuffer.data(), mResyncTxBuffer.size());
	g_ATLCNetplay("host: resync upload starting (%u bytes / %u chunks, epoch=%u)",
		(unsigned)mResyncTxBuffer.size(), (unsigned)chunks, (unsigned)mResyncEpoch);
}

void Coordinator::HandleResyncStart(const NetResyncStart& s, uint64_t nowMs) {
	if (mRole != Role::Joiner) return;
	// Accept from Lockstepping (normal trigger) or a duplicate while
	// already Resyncing (host retransmit before we ACKed first chunk).
	if (mPhase != Phase::Lockstepping && mPhase != Phase::Resyncing) return;

	if (mPhase == Phase::Resyncing && s.epoch == mResyncEpoch) {
		// Retransmit of the same start — nothing to do, receiver is
		// already armed.
		return;
	}

	BeginJoinerResync(s, nowMs);
}

void Coordinator::BeginJoinerResync(const NetResyncStart& s, uint64_t /*nowMs*/) {
	mResyncEpoch       = s.epoch;
	mResyncResumeFrame = s.resumeFrame;
	mResyncSeedHash    = s.seedHash;

	// Clear pre-resync lockstep state so the (now meaningless) input
	// queue can't race with the new timeline.
	mLoop.ClearDesync();

	mSnapRx.Begin(s.stateChunks, s.stateBytes);
	mNeedsResyncApply = false;
	mPhase = Phase::Resyncing;

	g_ATLCNetplay("joiner: resync incoming (epoch=%u, %u bytes / %u chunks, "
		"resumeFrame=%u)",
		(unsigned)s.epoch, (unsigned)s.stateBytes,
		(unsigned)s.stateChunks, (unsigned)s.resumeFrame);

	// Edge case: zero-byte payload (host captured nothing) — treat as
	// immediate apply-ready so the app can fast-fail the session.
	if (s.stateChunks == 0) mNeedsResyncApply = true;
}

void Coordinator::AcknowledgeResyncApplied() {
	if (mPhase != Phase::Resyncing) return;
	if (mRole != Role::Joiner) return;
	if (!mNeedsResyncApply) return;
	mNeedsResyncApply = false;

	// Resume LockstepLoop at the agreed frame with ring state wiped.
	// Host will do the same on its side when ResyncDone arrives.
	mLoop.ResumeAt(mResyncResumeFrame, mResyncSeedHash);
	mDesyncLogged = false;

	NetResyncDone d;
	d.magic       = kMagicResyncDone;
	d.epoch       = mResyncEpoch;
	d.resumeFrame = mResyncResumeFrame;
	size_t n = EncodeResyncDone(d, mTxBuf, sizeof mTxBuf);
	if (n && mPeerKnown) mTransport.SendTo(mTxBuf, n, mPeer);

	mPhase = Phase::Lockstepping;
	g_ATLCNetplay("joiner: resync applied, resumed at frame %u (epoch %u)",
		(unsigned)mResyncResumeFrame, (unsigned)mResyncEpoch);
}

void Coordinator::HandleResyncDone(const NetResyncDone& d, uint64_t /*nowMs*/) {
	if (mRole != Role::Host) return;
	if (mPhase != Phase::Resyncing) return;
	if (d.epoch != mResyncEpoch) {
		// Stale ack for a previous resync — ignore.
		return;
	}

	// Host now resumes its own lockstep at the same frame the joiner
	// jumped to.  Ring state wiped; hash ring re-seeded from our local
	// post-capture hash.
	mLoop.ResumeAt(mResyncResumeFrame, mResyncSeedHash);
	mDesyncLogged = false;
	mPhase = Phase::Lockstepping;
	mResyncTxBuffer.clear();    // free memory
	g_ATLCNetplay("host: resync complete, resumed at frame %u (epoch %u)",
		(unsigned)mResyncResumeFrame, (unsigned)mResyncEpoch);
}

void Coordinator::FailWith(const char* msg) {
	mLastError = msg;
	mPhase = Phase::Failed;
	g_ATLCNetplay("%s session failed: %s",
		mRole == Role::Host ? "host" :
		mRole == Role::Joiner ? "joiner" : "idle",
		msg ? msg : "(no reason)");
}

} // namespace ATNetplay
