// Altirra SDL3 netplay - per-frame simulator-state hash
//
// Deterministic lockstep assumes both peers reach bit-identical sim
// state from identical inputs.  Hashing inputs alone (as the original
// rolling hash did) can't detect sim-state divergence caused by an
// incomplete snapshot restore or a non-serialized device field — the
// input hashes agree while sprite positions silently drift apart.
//
// We hash a canonical slice of sim state once per emu frame and put
// the low 32 bits on the wire (re-using NetInputPacket.stateHashLow32).
// First-seen-wins on the peer-hash ring + existing apply-time and
// retro-check plumbing in lockstep.cpp do the detection.
//
// Scope of the hash:
//
//   - 6502 register file (PC, A, X, Y, S, P)
//   - Full 64 KB of ATSimulator::GetRawMemory()
//
// This is intentionally narrow for v1: it's the state 99% of games
// drive from, and the buckets are cheap enough (FNV-1a 32 over 64 KiB
// + 8 bytes ≈ sub-100 µs on any modern CPU) to run every frame.
// Device registers (POKEY/ANTIC/GTIA) are hashed separately only when
// the cheap hash has already flagged a desync, via
// ComputeSimStateHashBreakdown — letting us localize the first-
// diverging subsystem without paying the cost per frame.

#pragma once

#include <cstddef>
#include <cstdint>

#include <vd2/system/vdstl.h>

class ATSimulator;

namespace ATNetplay {

struct SimHashBreakdown {
	uint32_t total    = 0;    // matches ComputeSimStateHash() exactly
	uint32_t cpuRegs  = 0;    // 6502 PC/A/X/Y/S/P
	uint32_t ramBank0 = 0;    // $0000-$3FFF
	uint32_t ramBank1 = 0;    // $4000-$7FFF
	uint32_t ramBank2 = 0;    // $8000-$BFFF
	uint32_t ramBank3 = 0;    // $C000-$FFFF
	uint32_t gtiaRegs = 0;    // HPOS/PAL, player data latches
	uint32_t anticRegs = 0;   // DLIST, VCOUNT, NMIEN, DMACTL
	uint32_t pokeyRegs = 0;   // AUDF/AUDC/AUDCTL + poly-counter phase
	uint32_t schedTick = 0;   // low 32 bits of mScheduler.GetTick64()
};

// Fast path — called every frame.  ~100 µs per call.
uint32_t ComputeSimStateHash(ATSimulator& sim);

// Expensive path — called only on first desync detection.
void ComputeSimStateHashBreakdown(ATSimulator& sim, SimHashBreakdown& out);

} // namespace ATNetplay
