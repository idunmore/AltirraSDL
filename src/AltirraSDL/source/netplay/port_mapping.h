// Altirra SDL3 netplay - router-assisted UDP port mapping
//
// Asks the local router to install a temporary external → internal
// UDP port-forward, using NAT-PMP (RFC 6886) with a PCP (RFC 6887)
// upgrade probe.  This is the "how does my torrent client Just Work"
// technique: routers that support NAT-PMP/PCP accept an API call
// from the LAN and carve out a hole in their NAT table for inbound
// traffic, eliminating the need for hole-punching and making even
// restrictive cone-NAT hosts reachable.
//
// Coverage (real-world, from public protocol surveys):
//   - NAT-PMP / PCP: Apple AirPort, most OpenWrt / DD-WRT builds,
//     pfSense, OPNsense, many TP-Link / ASUS firmwares, iOS/macOS.
//   - UPnP-IGD: broader Windows-consumer coverage (Netgear / Linksys
//     with default config).  Not implemented here — adding SSDP
//     discovery + SOAP/XML is a separate, larger module.  Most
//     modern routers support BOTH, so NAT-PMP alone covers most.
//
// Scope:
//   - IPv4 only.  NAT-PMP is IPv4; PCP supports v6 but we don't need
//     it yet — if the host has a public v6 address, no mapping is
//     needed.
//   - Non-blocking on the calling thread: all network I/O uses a
//     short timeout (<1.5 s total across retries).  The lobby
//     worker thread calls this before posting Create.
//   - Best-effort.  On failure we log and move on; the reflector
//     probe remains the fallback.
//
// Thread safety:
//   - RequestMapping() owns its socket for the duration of the call;
//     safe to invoke concurrently from multiple threads.  The
//     returned mapping must be released via ReleaseMapping() before
//     process exit, or left to expire naturally (NAT-PMP leases
//     default to 1 hour).

#pragma once

#include <cstdint>
#include <string>

namespace ATNetplay {

struct PortMapping {
	// Protocol that granted this mapping, for logging.  "" on failure.
	std::string protocol;        // "NAT-PMP" | "PCP"
	// The router's public IPv4 (as seen from the WAN side).  Empty
	// when PCP didn't report it; callers can fall back to the
	// reflector probe.
	std::string externalIp;
	// The external port actually granted — routers may pick a port
	// different from the one you asked for if the requested one is
	// already in use.
	uint16_t    externalPort = 0;
	// Lease lifetime in seconds (0 = release).
	uint32_t    lifetimeSec  = 0;
	// Opaque handle needed to release the mapping.  Currently just
	// the internal port we asked for (NAT-PMP / PCP keyed by that).
	uint16_t    internalPort = 0;
};

// Request an external UDP port mapping for (internalPort → same
// external port preferred).  Returns true on success with `out`
// filled in; false on any failure path (no gateway discoverable,
// router doesn't speak NAT-PMP/PCP, refusal, timeout).  On
// failure, `err` holds a short reason.
//
// `lifetimeSec` is a hint — most routers clamp it to their own
// limit (typically 24h max).  Short leases (60-600 s) are best
// for ad-hoc netplay sessions; a refresh call before expiry keeps
// the mapping alive without long-term squatting on a port.
bool RequestUdpPortMapping(uint16_t internalPort,
                           uint32_t lifetimeSec,
                           PortMapping& out,
                           std::string& err);

// Release a mapping acquired by RequestUdpPortMapping.  Best-effort;
// ignores errors because if the router is unreachable, the mapping
// will expire naturally.
void ReleaseUdpPortMapping(const PortMapping& m);

} // namespace ATNetplay
