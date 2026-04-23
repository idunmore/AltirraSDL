# NAT traversal for AltirraSDL netplay

## TL;DR

If you're getting **"Asking the host to let you in…" forever** when
trying to join a host across the internet, the problem is almost
always that the host's router isn't letting UDP packets reach the
emulator. This doc explains why and what to do.

## How netplay connects

The lobby server (`lobby.altirra.example:8080`) is only a **directory**
— it never sees the game traffic. Once the joiner picks a session,
the two emulators exchange UDP packets **directly** peer-to-peer.

For that to work, both peers' routers must agree to let UDP through
in both directions. The client uses the same combination of
techniques that BitTorrent / WebRTC / Skype rely on:

1. **Router-assisted port forwarding via NAT-PMP / PCP** (RFC 6886 /
   RFC 6887). The host politely asks its router to install a
   temporary external→internal UDP forwarding rule, exactly the
   way a BitTorrent client does. When this succeeds (most Apple
   AirPort, OpenWrt, pfSense, many ASUS / TP-Link / Netgear
   routers out of the box), the host's public endpoint is rock-
   solid — no hole-punching, no retries, works regardless of NAT
   type.
2. **Advertising multiple candidate endpoints per host.** If NAT-PMP
   is unavailable, the host still publishes every endpoint a
   joiner might reach it at: router-mapped (from step 1), LAN IP
   (`192.168.x.x`), server-reflexive (public IP as observed by
   the lobby's UDP reflector), and loopback.
3. **Spraying `NetHello` to every candidate in parallel.** The joiner
   doesn't pre-guess which endpoint will work — it tries all of
   them for up to 15 s and locks onto the first host that responds.
4. **Probing the lobby's UDP reflector (STUN-lite)** so the host
   learns what its NAT maps its UDP port to, and publishes that as
   a candidate. This is the fallback when NAT-PMP isn't available.

Coverage in practice:

| Scenario | Works? | Which candidate wins? |
|----------|--------|------------------------|
| Both peers on the same LAN | Yes | LAN |
| Both peers on the same machine | Yes | Loopback |
| Host's router supports NAT-PMP/PCP | Yes (broad) | router-mapped |
| Host has manually port-forwarded UDP | Yes | srflx or mapped |
| Host behind full-cone NAT, no NAT-PMP | Usually yes | srflx (public) |
| Host behind symmetric NAT / CGNAT, no NAT-PMP | No | — |

NAT-PMP is enabled by default on Apple / OpenWrt-based routers and
can be toggled on in most modern consumer router admin UIs (look for
"NAT-PMP", "PCP", or sometimes "UPnP / NAT-PMP"). If it's on, the
host doesn't need to configure anything at all.

## If cross-internet join fails

### Step 1 — Check which candidate was even tried

On the joiner, open the netplay log channel. Look for lines like:

```
joiner candidate: 192.168.0.5:26100 -> 192.168.0.5:26100
joiner candidate: 203.0.113.5:26100 -> 203.0.113.5:26100
joiner candidate: 127.0.0.1:26100   -> 127.0.0.1:26100
joiner: multi-candidate (3), spraying Hello
...
joiner: handshake timeout after 15000ms, no host responded
```

If all three candidates were tried and none responded, the host's
router is dropping your packets.

### Step 2 — Enable NAT-PMP / PCP on the host's router

If your router admin UI has a NAT-PMP, PCP, or "UPnP / NAT-PMP"
toggle, turn it on. The client will detect it on the next host
session and the router will automatically install the port-forward
rule — no manual configuration needed. This is the BitTorrent-style
auto-forward and works on the widest variety of home routers.

### Step 3 — Have the host port-forward manually

If NAT-PMP isn't available on your router, you can set up the
forward by hand. This is the single most reliable fix. Steps:

1. **Pick a fixed UDP port on the host.** Any port >= 1024 works;
   26100 is the AltirraSDL default.
2. **Assign the host machine a static LAN IP** (or a DHCP
   reservation) so the router rule stays valid across reboots.
3. **Add the rule in the router admin UI.** The exact wording
   varies, but it looks like:
   - *Service*: Custom
   - *Protocol*: UDP
   - *External port*: 26100
   - *Internal IP*: the host machine's LAN IP
   - *Internal port*: 26100
4. **Verify from outside** using any UDP port-open tester, or just
   try a join from a mobile device on cellular (off-LAN).

After port forwarding, cross-internet joins should succeed
immediately via the srflx candidate.

### Step 4 — Symmetric NAT / CGNAT

If port-forwarding isn't possible (e.g. you're on a carrier-grade
NAT where the router you control isn't actually the NAT boundary),
the practical options are:

- **Host on a machine that has a public IP** (a VPS, a home
  machine on unfiltered fibre, etc.).
- **Use a VPN with port-forwarding support** (some VPN providers
  offer this) so the VPN's exit node acts as your router.
- **Have the joiner and host both use the same LAN** (e.g. via
  Tailscale, ZeroTier, or similar overlay networks). The LAN
  candidate will then work.

AltirraSDL does **not** currently relay UDP through the lobby.
That would bankrupt the free-tier lobby server at game rates
(~6 KB/s per direction per game × hundreds of concurrent sessions).

## For lobby operators

The UDP reflector listens on `UDP_REFLECTOR_PORT` (default 8081).
Open it in the security list / firewall alongside the TCP lobby
port. Wire protocol:

- **Request** (8 bytes): `'A' 'S' 'D' 'R'` magic + 4-byte little-
  endian transaction ID.
- **Response** (24 bytes): magic + txid + family(=4) + pad + port
  (big-endian uint16) + IPv4 (big-endian uint32) + 8 reserved
  bytes.

Bandwidth cost is negligible — ~30 bytes per host session, one
probe at session-create time.
