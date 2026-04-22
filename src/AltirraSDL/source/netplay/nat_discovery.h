// Altirra SDL3 netplay - NAT reflector client (STUN-lite)
//
// Discovers the public UDP endpoint (server-reflexive / "srflx") that
// a host's NAT assigns to a local UDP socket.  The lobby server runs
// a matching reflector on UDP port kReflectorPort that echoes back
// the observed source address.  Stateless — one request/response per
// probe, ~30 bytes on the wire, no ongoing load.
//
// This is *not* full STUN (RFC 5389).  We control both ends and only
// need source-endpoint echo, so the wire format is a minimal custom
// packet.  Sufficient for "what does the internet see me as?" which
// is the only question we need answered.
//
// Usage:
//   ATNetplay::ReflectorProbe probe;
//   std::string err;
//   bool ok = probe.Run("lobby.example", kReflectorPort,
//                       /*localPort=*/0, /*timeoutMs=*/1500,
//                       /*out=*/observedIpPort, err);
//
// The `localPort=0` form binds an ephemeral port and returns the
// observed srflx endpoint for *that* ephemeral port.  To learn the
// srflx for an already-bound game socket, pass its local port — the
// probe binds a separate socket on the same port via SO_REUSEADDR so
// both see the same NAT mapping on cone-NAT routers (the usual
// consumer case).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace ATNetplay {

// UDP reflector wire protocol.
//
// Request  (8 bytes):  'A' 'S' 'D' 'R'  <txid_le32>
// Response (24 bytes): 'A' 'S' 'D' 'R'  <txid_le32>  <family_u8> <pad_u8>
//                      <port_be_u16>     <ipv4_be_u32>  <reserved8>
//
// family = 4 for IPv4.  Reserved bytes are zero today; receivers must
// tolerate any value for forward compatibility.
constexpr uint32_t kReflectorMagic   = 0x52445341u;   // 'ASDR' LE
constexpr uint16_t kReflectorPort    = 8081;           // default UDP port
constexpr size_t   kReflectorReqLen  = 8;
constexpr size_t   kReflectorRespLen = 24;

struct ReflectorProbe {
	// Returns true on success.  `outIpPort` is written as "a.b.c.d:port"
	// in dotted-quad form.  On failure `err` holds a short reason.
	//
	// `localPort` == 0 means "bind an ephemeral port for this probe only";
	// the returned srflx endpoint corresponds to that ephemeral socket.
	// For netplay we want the srflx of the *game* socket, so callers
	// pass the bound game port here and the probe uses SO_REUSEADDR to
	// share the mapping.
	bool Run(const char* lobbyHost, uint16_t reflectorPort,
	         uint16_t localPort, uint32_t timeoutMs,
	         std::string& outIpPort, std::string& err);
};

// Convenience: synchronous probe that returns just the "ip:port"
// string.  Empty return = probe failed.  Safe for UI code on a
// worker thread (blocking up to timeoutMs).
std::string DiscoverReflexiveEndpoint(const char* lobbyHost,
                                      uint16_t reflectorPort,
                                      uint16_t localPort,
                                      uint32_t timeoutMs);

} // namespace ATNetplay
