// Altirra SDL3 netplay - NAT-PMP / PCP port mapping (impl)
//
// NAT-PMP (RFC 6886) wire format — UDP to gateway:5351.
//
//   Request (external address query, 2 bytes):
//     [version=0, op=0]
//   Response (12 bytes):
//     [version=0, op=128, result_be_u16, epoch_be_u32, extIp_be_u32]
//
//   Request (map UDP port, 12 bytes):
//     [version=0, op=1, reserved_be_u16=0,
//      internalPort_be_u16, suggestedExternalPort_be_u16,
//      lifetime_be_u32]
//   Response (16 bytes):
//     [version=0, op=129, result_be_u16, epoch_be_u32,
//      internalPort_be_u16, externalPort_be_u16, lifetime_be_u32]
//
// PCP (RFC 6887) uses version=2 on the same port (5351).  Many
// modern routers support BOTH; we try PCP first (richer semantics),
// fall back to NAT-PMP, fall back to failure.  This file implements
// NAT-PMP; PCP adds an additional ~60 byte request/response but
// the practical difference for our use case (map a UDP port, get
// the external address) is small enough that NAT-PMP alone
// satisfies most routers.  PCP upgrade is a follow-up.

#include <stdafx.h>

#include "port_mapping.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <iphlpapi.h>
   typedef int socklen_t;
#  define PM_LAST_ERR()     ((int)WSAGetLastError())
#  define PM_CLOSE(s)       closesocket((SOCKET)(s))
#  define PM_INVALID_SOCK   ((intptr_t)INVALID_SOCKET)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <poll.h>
#  include <cstdio>
#  define PM_LAST_ERR()     (errno)
#  define PM_CLOSE(s)       ::close((int)(s))
#  define PM_INVALID_SOCK   ((intptr_t)-1)
#endif

namespace ATNetplay {

namespace {

constexpr uint16_t kNatPmpPort = 5351;

void EnsureNetSubsystem() {
#if defined(_WIN32)
	static bool ok = false;
	if (ok) return;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) ok = true;
#endif
}

uint64_t MonoMillis() {
	using namespace std::chrono;
	return (uint64_t)duration_cast<milliseconds>(
		steady_clock::now().time_since_epoch()).count();
}

// -------------------------------------------------------------------
// Default-gateway discovery.
//
// The router that owns our NAT mapping is the default gateway — the
// next-hop for 0.0.0.0.  Each platform answers this query
// differently; none of these calls is expensive.  Returns the
// gateway IP in network byte order (htonl form) or 0 on failure.
// -------------------------------------------------------------------

#if defined(_WIN32)

uint32_t DiscoverDefaultGateway() {
	// GetBestRoute to 8.8.8.8: Windows returns the interface + next
	// hop that would serve that destination.
	MIB_IPFORWARDROW row = {};
	DWORD rc = ::GetBestRoute(
		htonl(0x08080808),   // 8.8.8.8
		0,
		&row);
	if (rc != NO_ERROR) return 0;
	return (uint32_t)row.dwForwardNextHop;   // already network-byte-order
}

#elif defined(__linux__) || defined(__ANDROID__)

uint32_t DiscoverDefaultGateway() {
	// /proc/net/route is the simplest cross-distro way.  Columns
	// (whitespace-separated): Iface Destination Gateway Flags ...
	// — all hex, little-endian ASCII (!).  Pick the first row whose
	// Destination is 00000000 (default route).
	std::FILE* f = std::fopen("/proc/net/route", "r");
	if (!f) return 0;
	char buf[512];
	// Skip header.
	if (!std::fgets(buf, sizeof buf, f)) { std::fclose(f); return 0; }
	uint32_t gw = 0;
	while (std::fgets(buf, sizeof buf, f)) {
		char iface[64];
		unsigned long dest = 0, gateway = 0, flags = 0;
		if (std::sscanf(buf, "%63s %lx %lx %lx",
		                iface, &dest, &gateway, &flags) < 4) continue;
		if (dest == 0) {
			// /proc/net/route stores addresses in HOST byte order on
			// little-endian (the kernel just writes the bytes of the
			// __be32 as hex).  htonl would double-swap; the raw
			// uint32_t *is* already network-order.
			gw = (uint32_t)gateway;
			break;
		}
	}
	std::fclose(f);
	return gw;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

#include <sys/sysctl.h>
#include <net/route.h>
#include <net/if.h>
#include <net/if_dl.h>

uint32_t DiscoverDefaultGateway() {
	int mib[7] = { CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_FLAGS,
	               RTF_GATEWAY, 0 };
	size_t len = 0;
	if (::sysctl(mib, 6, nullptr, &len, nullptr, 0) < 0) return 0;
	if (len == 0) return 0;
	std::vector<char> tbl(len);
	if (::sysctl(mib, 6, tbl.data(), &len, nullptr, 0) < 0) return 0;

	for (char* p = tbl.data(); p < tbl.data() + len; ) {
		rt_msghdr* rtm = reinterpret_cast<rt_msghdr*>(p);
		sockaddr* sa = reinterpret_cast<sockaddr*>(rtm + 1);
		sockaddr* gw = nullptr;
		sockaddr* dst = nullptr;
		for (int i = 0; i < RTAX_MAX; ++i) {
			if (rtm->rtm_addrs & (1 << i)) {
				if (i == RTAX_DST) dst = sa;
				else if (i == RTAX_GATEWAY) gw = sa;
				sa = (sockaddr*)((char*)sa +
					((sa->sa_len + sizeof(long) - 1) &
					 ~(sizeof(long) - 1)));
			}
		}
		if (dst && dst->sa_family == AF_INET &&
		    reinterpret_cast<sockaddr_in*>(dst)->sin_addr.s_addr == 0 &&
		    gw && gw->sa_family == AF_INET) {
			uint32_t addr = reinterpret_cast<sockaddr_in*>(gw)
				->sin_addr.s_addr;
			return addr;
		}
		p += rtm->rtm_msglen;
	}
	return 0;
}

#else

uint32_t DiscoverDefaultGateway() { return 0; }

#endif

bool WaitReadable(intptr_t s, uint32_t timeoutMs) {
#if defined(_WIN32)
	fd_set rf; FD_ZERO(&rf); FD_SET((SOCKET)s, &rf);
	timeval tv;
	tv.tv_sec  = (long)(timeoutMs / 1000);
	tv.tv_usec = (long)((timeoutMs % 1000) * 1000);
	return ::select(0, &rf, nullptr, nullptr, &tv) > 0;
#else
	pollfd pfd{};
	pfd.fd = (int)s;
	pfd.events = POLLIN;
	int rc = ::poll(&pfd, 1, (int)timeoutMs);
	return rc > 0 && (pfd.revents & POLLIN);
#endif
}

// Return raw uint32 (network byte order) → dotted quad.
std::string NetworkOrderToDotted(uint32_t be) {
	in_addr ia;
	ia.s_addr = be;
	char buf[INET_ADDRSTRLEN] = {};
#if defined(_WIN32)
	InetNtopA(AF_INET, &ia, buf, sizeof buf);
#else
	inet_ntop(AF_INET, &ia, buf, sizeof buf);
#endif
	return buf;
}

uint16_t ReadBE16(const uint8_t* p) { return (uint16_t)((p[0]<<8)|p[1]); }
uint32_t ReadBE32(const uint8_t* p) {
	return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16)
	     | ((uint32_t)p[2]<<8)  | ((uint32_t)p[3]);
}
void WriteBE16(uint8_t* p, uint16_t v) {
	p[0] = (uint8_t)(v>>8); p[1] = (uint8_t)(v);
}
void WriteBE32(uint8_t* p, uint32_t v) {
	p[0] = (uint8_t)(v>>24); p[1] = (uint8_t)(v>>16);
	p[2] = (uint8_t)(v>>8);  p[3] = (uint8_t)(v);
}

// -------------------------------------------------------------------
// NAT-PMP exchange.
// -------------------------------------------------------------------
//
// Per RFC 6886 §3.1 the client retransmits at 250/500/1000/2000/4000
// ms if no reply arrives.  We use a shorter, 3-try budget — if the
// router doesn't answer in ~1.5 s it almost certainly doesn't speak
// NAT-PMP, and we want to move on to the reflector probe.

bool DoNatPmp(uint32_t gatewayBE, uint16_t internalPort,
              uint32_t lifetimeSec, PortMapping& out,
              std::string& err) {
	EnsureNetSubsystem();

#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) { err = "socket() failed"; return false; }
	intptr_t s = (intptr_t)raw;
#endif

	sockaddr_in gw {};
	gw.sin_family = AF_INET;
	gw.sin_port   = htons(kNatPmpPort);
	gw.sin_addr.s_addr = gatewayBE;

	// ---- Query 1: external address (op=0).  Gives us the WAN IP. --
	uint8_t addrReq[2] = { 0, 0 };
	uint8_t resp[16] = {};
	std::string extIp;

	const uint32_t kTriesMs[] = { 250, 500, 750 };
	bool gotAddr = false;
	for (uint32_t tm : kTriesMs) {
		int n = ::sendto((int)s, (const char*)addrReq, (int)sizeof addrReq,
			0, (const sockaddr*)&gw, sizeof gw);
		if (n != (int)sizeof addrReq) { err = "send failed"; break; }
		if (!WaitReadable(s, tm)) continue;

		sockaddr_in from {};
		socklen_t flen = sizeof from;
		n = ::recvfrom((int)s, (char*)resp, (int)sizeof resp, 0,
			(sockaddr*)&from, &flen);
		if (n < 12) continue;
		if (from.sin_addr.s_addr != gatewayBE) continue;
		if (resp[0] != 0 || resp[1] != 128) continue;   // version/op
		uint16_t result = ReadBE16(resp + 2);
		if (result != 0) { err = "NAT-PMP refused addr query"; break; }
		uint32_t extBE = *reinterpret_cast<const uint32_t*>(resp + 8);
		extIp = NetworkOrderToDotted(extBE);
		gotAddr = true;
		break;
	}

	if (!gotAddr) {
		if (err.empty()) err = "NAT-PMP gateway did not respond";
		PM_CLOSE(s);
		return false;
	}

	// ---- Query 2: map UDP port (op=1) with lifetime hint. ---------
	uint8_t mapReq[12] = {};
	mapReq[0] = 0;     // version
	mapReq[1] = 1;     // op = map UDP
	// reserved (2 bytes) = 0
	WriteBE16(mapReq + 4, internalPort);
	WriteBE16(mapReq + 6, internalPort);   // suggested external
	WriteBE32(mapReq + 8, lifetimeSec);

	bool gotMap = false;
	for (uint32_t tm : kTriesMs) {
		int n = ::sendto((int)s, (const char*)mapReq, (int)sizeof mapReq,
			0, (const sockaddr*)&gw, sizeof gw);
		if (n != (int)sizeof mapReq) { err = "send failed"; break; }
		if (!WaitReadable(s, tm)) continue;

		sockaddr_in from {};
		socklen_t flen = sizeof from;
		n = ::recvfrom((int)s, (char*)resp, (int)sizeof resp, 0,
			(sockaddr*)&from, &flen);
		if (n < 16) continue;
		if (from.sin_addr.s_addr != gatewayBE) continue;
		if (resp[0] != 0 || resp[1] != 129) continue;
		uint16_t result = ReadBE16(resp + 2);
		if (result != 0) { err = "NAT-PMP refused mapping"; break; }
		uint16_t internal = ReadBE16(resp + 8);
		uint16_t external = ReadBE16(resp + 10);
		uint32_t lease    = ReadBE32(resp + 12);
		(void)internal;

		out.protocol     = "NAT-PMP";
		out.externalIp   = extIp;
		out.externalPort = external;
		out.lifetimeSec  = lease;
		out.internalPort = internalPort;
		gotMap = true;
		break;
	}

	PM_CLOSE(s);
	if (!gotMap && err.empty()) err = "NAT-PMP mapping timeout";
	return gotMap;
}

} // anon

bool RequestUdpPortMapping(uint16_t internalPort,
                           uint32_t lifetimeSec,
                           PortMapping& out,
                           std::string& err) {
	out = {};
	err.clear();

	if (internalPort == 0) { err = "internal port is 0"; return false; }

	uint32_t gw = DiscoverDefaultGateway();
	if (gw == 0) { err = "could not discover gateway"; return false; }

	// First try NAT-PMP.  PCP upgrade is a planned follow-up — PCP's
	// MAP opcode produces a strict superset of what NAT-PMP gives us,
	// but requires a PCP-aware router and the slightly larger wire
	// format.  Most consumer routers that support PCP also support
	// NAT-PMP for backward compatibility, so NAT-PMP alone covers
	// nearly all of the coverage population.
	if (DoNatPmp(gw, internalPort, lifetimeSec, out, err)) {
		return true;
	}
	return false;
}

void ReleaseUdpPortMapping(const PortMapping& m) {
	if (m.internalPort == 0 || m.protocol.empty()) return;

	uint32_t gw = DiscoverDefaultGateway();
	if (gw == 0) return;

	EnsureNetSubsystem();

#if defined(_WIN32)
	SOCKET raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (raw == INVALID_SOCKET) return;
	intptr_t s = (intptr_t)raw;
#else
	int raw = ::socket(AF_INET, SOCK_DGRAM, 0);
	if (raw < 0) return;
	intptr_t s = (intptr_t)raw;
#endif

	sockaddr_in dst {};
	dst.sin_family = AF_INET;
	dst.sin_port   = htons(kNatPmpPort);
	dst.sin_addr.s_addr = gw;

	// NAT-PMP release: same map request with lifetime=0.
	uint8_t req[12] = {};
	req[0] = 0;
	req[1] = 1;
	WriteBE16(req + 4, m.internalPort);
	WriteBE16(req + 6, m.externalPort);
	WriteBE32(req + 8, 0);   // lifetime = release
	::sendto((int)s, (const char*)req, (int)sizeof req, 0,
		(const sockaddr*)&dst, sizeof dst);
	// Best-effort: don't wait for a reply.  If the router is gone,
	// the mapping expires naturally.
	PM_CLOSE(s);
}

} // namespace ATNetplay
