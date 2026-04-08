// AltirraBridge - Phase 2 state-read commands
//
// All commands here are pure read operations: they snapshot CPU
// registers, memory, and chip state without modifying anything and
// without requiring the simulator to be paused. Each function takes
// a tokenised command line (verb + args) and returns a JSON response
// string with the trailing newline.
//
// These functions never call sim.Pause() / sim.Resume() / Advance().
// The frame gate (in bridge_server.cpp) is what coordinates "stop the
// world before reading state", and Phase 1 already wires that.

#pragma once

#include <string>
#include <vector>

class ATSimulator;

namespace ATBridge {

// REGS — CPU registers, status flags, cycle counter, mode.
std::string CmdRegs(ATSimulator& sim, const std::vector<std::string>& tokens);

// PEEK addr [length]
//   addr   16-bit (or 24-bit) address, hex/decimal/$hex
//   length optional byte count, default 1, capped at 16384
//
// Returns hex-encoded bytes in the "data" field. Uses the same
// debug-safe read path as the memory pane: I/O register reads do NOT
// trigger ANTIC/GTIA/POKEY side effects.
std::string CmdPeek(ATSimulator& sim, const std::vector<std::string>& tokens);

// PEEK16 addr
//   Returns one little-endian 16-bit word in the "value" field.
std::string CmdPeek16(ATSimulator& sim, const std::vector<std::string>& tokens);

// ANTIC — full ANTIC register state plus beam position and current
// display list pointer.
std::string CmdAntic(ATSimulator& sim, const std::vector<std::string>& tokens);

// GTIA — full GTIA register dump (32 bytes), console switches.
std::string CmdGtia(ATSimulator& sim, const std::vector<std::string>& tokens);

// POKEY — full POKEY register dump (32 bytes).
std::string CmdPokey(ATSimulator& sim, const std::vector<std::string>& tokens);

// PIA — six PIA registers (PORTA/B output, DDRA/B, CRA/B).
std::string CmdPia(ATSimulator& sim, const std::vector<std::string>& tokens);

// DLIST — current display list, parsed from ANTIC's DL history cache.
// Each entry: address, instruction byte, decoded mode, LMS target (if
// LMS), DLI flag, scroll bits, character base, DMA control, current
// playfield address.
std::string CmdDlist(ATSimulator& sim, const std::vector<std::string>& tokens);

// HWSTATE — combined snapshot: CPU + ANTIC + GTIA + POKEY + PIA, all
// in one response. Useful for "tell me everything" diagnostic dumps
// without N round trips.
std::string CmdHwstate(ATSimulator& sim, const std::vector<std::string>& tokens);

// PALETTE — 256-entry RGB color table from GTIA's analysis palette.
// Each entry is the 24-bit RGB value for a GTIA color index 0..255.
std::string CmdPalette(ATSimulator& sim, const std::vector<std::string>& tokens);

}  // namespace ATBridge
