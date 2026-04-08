// AltirraBridge - line-delimited JSON protocol helpers
//
// Same hand-rolled style as source/ui/testmode/ui_testmode.cpp: we have
// no JSON dependency, just escape-and-concatenate. The bridge keeps its
// own copy of these helpers (rather than reusing testmode's) so the
// module is fully self-contained and can be deleted in one shot.
//
// Wire format:
//   - Commands and responses are UTF-8, newline-terminated ('\n').
//   - Commands are either a bare verb ("PING\n") or a verb followed by
//     space-separated tokens ("FRAME 60\n", "POKE 600 ff\n").
//   - Responses are always single-line JSON objects with at least an
//     "ok" field, terminated by '\n'.
//   - Errors: {"ok":false,"error":"message"}.
//   - Success without payload: {"ok":true}.
//   - Success with payload: {"ok":true,"frames":60} etc.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ATBridge {

// Escape an arbitrary UTF-8 string into a JSON string literal body
// (without the surrounding quotes). Handles backslash, double quote,
// control characters (\n, \r, \t, \b, \f), and other low bytes via
// \u00XX. Does not validate UTF-8 — bad bytes pass through unmodified.
std::string JsonEscape(const std::string& s);

// Build a {"ok":true} response with optional extra payload appended
// before the closing brace. The extra payload, if non-empty, must be
// valid JSON object body fragments WITHOUT a leading comma — JsonOk
// inserts the comma. Examples:
//   JsonOk()                          -> {"ok":true}\n
//   JsonOk("\"frames\":60")           -> {"ok":true,"frames":60}\n
//   JsonOk("\"port\":54321,\"ip\":\"127.0.0.1\"")
//                                     -> {"ok":true,"port":54321,...}\n
// The trailing newline is always included.
std::string JsonOk();
std::string JsonOk(const std::string& extraPayload);

// Build an error response. msg is the human-readable error string;
// it is JSON-escaped automatically. The trailing newline is included.
std::string JsonError(const std::string& msg);

// Tokenise a command line at whitespace. Empty tokens are dropped.
// Quoted-string tokens are NOT supported in v1 — Phase 1 commands take
// only bare argument tokens. Whitespace inside an argument requires
// inline-base64 encoding or a path: argument (added in later phases).
std::vector<std::string> TokenizeCommand(const std::string& line);

// Parse a hex or decimal integer (e.g. "60", "0x3c", "$3c"). Returns
// false on parse failure. Accepts uppercase and lowercase. The Atari
// convention "$XX" for hex is supported in addition to "0xXX".
bool ParseUint(const std::string& s, uint32_t& out);

// Standard RFC 4648 base64 encoding (no line breaks). Used by Phase
// 3+ commands that ship binary payloads inline (MEMDUMP, MEMLOAD,
// STATE_SAVE/LOAD inline mode, RENDER_FRAME).
std::string Base64Encode(const uint8_t* data, size_t len);

// Standard base64 decoding. Tolerates whitespace and trailing '='
// padding. Returns false on invalid characters or malformed length.
// Output vector is cleared on entry.
bool Base64Decode(const std::string& s, std::vector<uint8_t>& out);

}  // namespace ATBridge
