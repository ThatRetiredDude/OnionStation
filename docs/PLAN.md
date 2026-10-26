# OnionStation5 plan: a PS5 Tor node and onion browser for every kind of network

Date: 2026-10-26. Desk study only; nothing run on the console. Separate program from
TokenStation5 and WireStationVPN, with its own repo, title ID and store listing.

This revision replaces the first plan, which was written around one network (Jared's building:
double NAT, no port forwarding). The app has to work for anyone who installs it, so this version
covers every common home and shared network, people who won't or can't open a port, IPv6, CGNAT,
people inside censored countries, onion services hosted from the PS5, and every way to browse
onion sites.

## Short version

- **OnionStation5 is a full Tor node, not a single-mode app.** Every role a home user can safely
  run is first-class: private bridge, public bridge, guard/middle relay, onion services and the
  Tor client, plus two opt-in extras (Snowflake proxy and an expert-only exit relay). No user has
  to pick one; **Auto mode** (the default) runs the strongest non-exit role their network allows
  and moves up or down by itself when the network changes.
- **Snowflake proxy is optional, off by default** (Jared, 2026-10-26). It works on every network
  (any NAT, CGNAT, double NAT, no port forwarding), so the app suggests turning it on when the
  network can't run a bridge or relay. It is the biggest rewrite in the plan, so it comes after
  the core Tor roles in the build order.
- **Bridge and relay modes need inbound connections.** The app finds out by itself whether it can
  get them: it tries automatic port mapping (UPnP IGD, NAT-PMP, PCP) if the user allows it, spots
  CGNAT and double NAT, checks IPv6, and offers a step-by-step manual port-forward guide. Tor's own
  reachability self-test makes the final call. The app never claims a mode works until that test
  passes.
- **Nobody has to understand NAT.** A setup check classifies the network (about 25 situations are
  covered below) and Auto mode acts on it. The user can pin a mode or set a ceiling ("never more
  than a bridge") instead.
- **Exit relay is offered as an expert-only mode** (Jared, 2026-10-26): off by default, never
  chosen by Auto mode, turned on only by hand after a warning that the Tor Project recommends
  against running an exit at home. Reduced exit policy by default.
- **Onion services from the PS5 need no open port at all.** They connect outward, so they work
  behind any NAT. The first use is a private status page the owner can open from their phone
  anywhere, protected by client authorization.
- **Onion browsing has five paths**, from the PS5's own browser through a local proxy to a
  built-in reader and Tor for other devices on the LAN. None of them is Tor Browser, and the app
  says so.
- **People on censored networks** need the PS5's Tor client to use bridges (Snowflake, obfs4,
  WebTunnel). Those transports are Go programs upstream, so they need C/C++ rewrites. The
  Snowflake one shares most code with the Snowflake proxy.
- **Tor itself is C** and ports to the PS5 (libevent, OpenSSL, zlib). Every Go component
  (Snowflake proxy and client, obfs4, WebTunnel) has to be rewritten in C/C++. That is the biggest
  cost in this plan.
- **Proposed ID and name: PPSA99557, "OnionStation5"** (name confirmed by Jared 2026-10-26).

## The node types

| Kind | What it does | Needs inbound? | Works behind CGNAT / no port forward? | Risk to the owner |
|---|---|---|---|---|
| Snowflake proxy | Helps censored users reach Tor over WebRTC | No (dials out, NAT traversal) | **Yes** | Very low. IP not published; only encrypted traffic to Tor's servers leaves. |
| Private bridge (obfs4) | Entry point the owner shares with people they know | Yes, one TCP port | No | Low. Not listed or handed out by Tor. |
| Public bridge (obfs4) | Entry point Tor hands out to censored users | Yes, one TCP port | No | Low. Not in the public relay list, but probed and blocked by censors. |
| Middle / guard relay | Carries Tor traffic between relays | Yes, ORPort (TCP, IPv4 required) | No | Low to medium. IP goes on the public relay list; some sites block all relay IPs. |
| Exit relay (expert only) | Traffic leaves Tor onto the internet from the owner's IP | Yes | No | **High.** Off by default; manual only; Tor Project recommends against it at home. |
| Onion service | Hosts a page or service at a .onion address | **No** | **Yes** | Low for a private status page; depends on what is hosted. |
| Tor client | The PS5 uses Tor (onion browsing, LAN proxy) | No | Yes | Low. It uses Tor, it doesn't help it. |

Modes can run together: Tor client plus an onion service (plus Snowflake, if turned on) is the
normal combination for someone with no open port. A bridge and a relay can't both run on the same Tor
instance, and the app shouldn't try.

## Auto mode: the strongest node each network allows

Auto mode is the default. It treats the roles as a ladder and climbs as far as the network,
the user's consent and the measured bandwidth allow:

| Rung | Role | Needs |
|---|---|---|
| 0 | Tor client only | Tor can bootstrap (with bridges if needed) |
| 1 | Snowflake proxy (only if the user turned it on) | Outbound UDP works and the network isn't censored |
| 2 | Private bridge | A reachable TCP port (self-test passed) |
| 3 | Public bridge | Rung 2, plus several days of mostly-on uptime |
| 4 | Guard/middle relay | Reachable IPv4 ORPort, at least ~16 Mbit/s each way, stable IP, steady uptime, and the user's explicit OK to publish their IP |
| (5) | Exit relay | Never reached by Auto mode. Manual only (see "Exit relay: expert only"). |

How it behaves:

- **Snowflake is the user's switch, not Auto mode's.** Auto mode never turns it on. When the
  best rung the network allows is 0 (no inbound possible), the status screen shows the hint from
  "When to turn Snowflake on". If the user has turned it on, it runs from the first minute while
  inbound probing happens in the background, and keeps running alongside a bridge or relay if
  upload allows (separate process, different users, bandwidth limits split between them).
- **Promote only on proof.** A rung is reached only when Tor's own self-test confirms it (and
  for rung 4, after the bandwidth check and an uptime history the app keeps itself).
- **Consent at the point it matters.** Opening a router port (rung 2) and publishing the IP as a
  relay (rung 4) are each asked once, in plain words, the first time Auto mode wants them. A "no"
  is remembered and becomes the ceiling.
- **Demote on any change.** Lost port mapping, a new IP that turns out to be CGNAT, failed
  reachability, the monthly cap reached, the network switched (Wi-Fi to wired, different router),
  or signs of censorship: drop to the highest rung that still holds, say why on the status screen,
  and keep checking.
- **No flapping.** Relays that come and go hurt the Tor network and never earn flags. Auto mode
  waits (hours, not minutes) before promoting again after a demotion, and moves up to relay only
  after a stretch of stable bridge-level uptime.
- **Re-check triggers:** app start, interface address change, Tor reporting a new external
  address, lease renewal failure, and a periodic check (every few hours).
- **Separate identities per role.** A bridge must never reuse keys that were once a public relay
  (that relay's IP and fingerprint are public, so the bridge would be known from day one). Each
  role keeps its own DataDirectory under /download0; switching roles switches directories.
- **Censorship flips the ladder.** If the network looks censored, Auto mode drops all
  help-others roles and becomes a client with bridges (see "Censored networks").

Manual control stays available: pin any rung, set a ceiling, or turn individual roles off.

## Every kind of network, and what the app recommends

Where this table recommends Snowflake, the app shows the "turn on Snowflake" hint; it doesn't
switch Snowflake on by itself. Exit relays appear nowhere in the table because Auto mode never
recommends them.

| Network | How the app recognizes it | Recommended mode | Also possible |
|---|---|---|---|
| Public IPv4 on the PS5 (rare: modem in bridge mode, some fiber) | The PS5's own address is public | Relay or public bridge | Snowflake |
| Home router, UPnP/NAT-PMP/PCP on, user allows auto mapping | Mapping succeeds and the router reports a public WAN address | Public bridge (relay if bandwidth and uptime allow) | Private bridge, Snowflake |
| Home router, mapping off or refused, user willing to forward | No mapping protocol answers; WAN address unknown or public | Guided manual port forward, then bridge | Snowflake meanwhile |
| Home router, user unwilling to touch ports | User says no at the port step | Snowflake | Onion service, client |
| Double NAT (ISP router plus own router, or a building like Jared's) | Mapping succeeds but the reported WAN address is private (10/8, 172.16/12, 192.168/16) | Snowflake | Bridge only if the user can also forward on the outer router or put it in bridge mode |
| CGNAT (common on mobile, satellite, fiber and cable ISPs) | WAN address in 100.64.0.0/10, or STUN's public address differs from the router's WAN address | Snowflake | Bridge or relay over IPv6 only where it applies (see IPv6) |
| IPv6 with a global address | Interface has a 2000::/3 address and an outbound IPv6 test works | Snowflake; IPv6 adds nothing a relay can use by itself | Bridge on IPv6 if the router firewall opens (pinhole), with a caveat |
| Mobile hotspot / tethering | CGNAT plus usually a metered link | Snowflake with a low data cap, or nothing | Client only |
| Shared or institutional network (dorm, workplace, building, café, hotel) | User answers "I don't control this network" | Snowflake, or client only if the network's rules forbid proxies | Auto mode won't go inbound here; the user can, after a warning that complaints and blocks hit everyone on that IP. |
| Censored country (Tor blocked) | Tor bootstrap stalls, or the Snowflake broker can't be reached; or user says so | **Client with bridges only.** No proxy, relay or bridge (useless there and riskier) | Onion browsing through bridges |
| Network that only allows outbound 80/443 | Outbound tests on other ports fail | Client with `ReachableAddresses` limited to 80/443, or bridges on 443 | Snowflake usually still works (WebRTC over UDP/TURN is not guaranteed) |
| ISP blocks some inbound ports (often 25, 80, 443, or everything below 1024) | Mapping works but the self-test fails on a low port | Bridge or relay on a random high port | Snowflake |
| ISP terms forbid "servers" | Can't be detected; the app mentions it once before rung 2 | User's call; Snowflake is client-like and generally fine | Bridge/relay at the user's risk |
| UDP blocked outbound | STUN and the Snowflake NAT check time out | Bridge or relay if inbound TCP works; otherwise client only | Nothing UDP-based |
| A proxy is required to reach the internet (some schools, workplaces) | Direct connections fail; user enters a proxy | Client through Tor's `HTTPSProxy` / `Socks5Proxy` options | Nothing that helps others (and the network's owner likely forbids it) |
| IPv6-only with NAT64/DNS64 (some mobile carriers) | No IPv4 address; a 64:ff9b::/96 prefix appears in DNS answers | Client over IPv6 (`ClientUseIPv6`, `ClientPreferIPv6ORPort`); Snowflake if the broker and STUN answer over IPv6 | No relay (relays need IPv4) |
| Starlink and similar satellite | CGNAT on IPv4; often a public, routed IPv6 prefix | Snowflake | IPv6-only bridge (advanced, low value) |
| 5G / LTE "home internet" boxes | CGNAT, usually IPv6, sometimes metered | Snowflake with a cap | Client |
| ISP router the user can't log in to | No mapping protocol answers and the user says they can't change settings | Snowflake | Ask the ISP for a public IP or bridge mode |
| Mesh Wi-Fi or several routers | Double-NAT check; mesh nodes in router mode add a NAT layer | Same as double NAT unless the mesh is in access-point mode | The guide says how to tell |
| Whole-home VPN on the router, or a VPN app on the network | Public address belongs to a VPN provider (user confirms) | Snowflake (works, but check the VPN's terms) | Bridge only if the VPN provider offers port forwarding and allows it |
| Captive portal (hotel, café, guest Wi-Fi) | HTTP requests get redirected to a login page; Tor can't bootstrap | Tell the user to log in through the PS5 browser first, then retry | Client afterwards |
| IP changes often (daily DHCP leases, PPPoE reconnects) | Tor reports a new address more than once a day | Snowflake or private bridge (the app shows the new bridge line; friends must update theirs) | Public bridge; relay not recommended |
| PS5 on Wi-Fi instead of wired | Interface type | Same as wired, but suggest wired for bridge and relay roles (drops hurt uptime) | — |
| Slow or high-latency links (old DSL, geostationary satellite) | Bandwidth check, round-trip times | Snowflake with a low cap | Bridge; no relay |
| Another Tor node or PS5 running OnionStation5 behind the same IP | Port already mapped, or Tor reports the address already in use by a relay | Use another port; only one relay per household in Auto mode | Snowflake on both |
| PS5 clock wrong or NTP blocked | Tor refuses the consensus as out of date | Warn and show how to set the clock; nothing works until fixed | — |

## Setup check: how the app classifies the network

Runs on first start and again from a "Check my network" button. Each step is local or uses
infrastructure Tor already relies on; there is no third-party "is my port open" service by
default (that would tell an outside site the user runs Tor).

1. **Local addresses.** Read the PS5's interfaces (getifaddrs or the PS5 equivalent): private or
   public IPv4, any global IPv6, the default gateway.
2. **Ask the router.** Try, in this order, each with a short timeout:
   - PCP (RFC 6887) to the gateway on UDP 5351, ANNOUNCE/MAP;
   - NAT-PMP (RFC 6886) to the gateway on UDP 5351, external address request;
   - UPnP IGD: SSDP M-SEARCH to 239.255.255.250:1900, then GetExternalIPAddress over SOAP.
   Whatever answers gives the router's WAN address. Nothing answers means mapping is off,
   unsupported, or blocked.
3. **Find the public address.** STUN binding request to the same public STUN servers the Snowflake
   proxy already uses. Compare with the router's WAN address:
   - WAN address private: double NAT.
   - WAN address in 100.64.0.0/10, or STUN address differs from WAN address: CGNAT.
   - Same: the router has the public address; port mapping on it is enough.
4. **NAT behaviour** (for Snowflake): run the Snowflake NAT check (the probe the official proxy
   does) to learn "unrestricted" or "restricted".
5. **IPv6 outbound.** STUN over IPv6, or a TCP connect to a Tor directory authority's IPv6
   address. Records whether IPv6 works at all.
6. **Other checks:** captive portal (a plain HTTP request that comes back redirected), UDP
   blocked (STUN times out but TCP works), a required proxy (direct TCP fails everywhere; the user
   can enter one), NAT64 (no IPv4 address, IPv6 works), and the clock.
7. **Ask the user two questions**, in plain words:
   - "Do you control this network's router?" (yes / no / not sure)
   - "Is your internet capped or metered?" (no / yes, with a monthly limit field)
8. **Recommend.** Show the result as one sentence and one suggested mode, e.g. "Your provider
   shares one internet address between many customers (CGNAT), so other computers can't connect
   in. Snowflake works fine here." Advanced details (addresses, NAT type, which protocol answered)
   sit behind a "Details" button.
9. **Confirm** inbound modes only after Tor's self-test passes (see "Confirming reachability").

If any step fails or times out the app falls back to client mode and shows the Snowflake hint,
since Snowflake needs none of these checks.

## Automatic port mapping (UPnP IGD, NAT-PMP, PCP)

Tor has no built-in port mapping any more (the old tor-fw-helper was removed years ago), so the
background part does it.

- **Only with consent.** Off until the user picks a bridge or relay mode and agrees to "Let
  OnionStation5 open a port on your router". Some people keep UPnP off on purpose; the app says
  that's fine and offers the manual guide.
- **Libraries:** miniupnpc and libnatpmp (both BSD-3). PCP's MAP request is small enough to write
  directly (one UDP packet each way) and also handles IPv6 pinholes.
- **One TCP port** for the ORPort (relay) or the obfs4 port (bridge). Default 9001 for relays and
  a random high port for bridges (fixed ports like 443 are easy for censors to guess; a random
  port is better for bridges). Relays may prefer 443 to help clients behind strict firewalls if
  the PS5 can bind it (console unknown).
- **Leases.** Ask for a limited lease (e.g. 1 hour for NAT-PMP/PCP, a lease time for UPnP where
  the router supports it) and renew it while running. If the app crashes or the PS5 turns off,
  the mapping expires on its own instead of staying open forever. Remove it explicitly on stop.
- **Conflicts.** If the port is already mapped to another device (UPnP error 718, PCP/NAT-PMP
  returning a different port), pick another port and tell Tor; never take over someone else's
  mapping.
- **Double NAT.** Mapping on the inner router "succeeds" but doesn't help. Step 3 of the setup
  check catches this, and the app says so instead of reporting success.
- **Address changes.** Re-check the WAN address when Tor reports a new address or the lease
  renews; residential IPs change.

## Manual port forwarding guide

For users with a router that has no mapping protocol, or who prefer doing it themselves.

- Shows exactly what to enter: the PS5's LAN IP, the port, protocol TCP, and the same port on the
  outside.
- Tells the user to give the PS5 a fixed LAN address first (DHCP reservation in the router, or
  manual IP in PS5 network settings), or the forward breaks after a router reboot.
- Links to general guidance rather than per-router screenshots (there are thousands of routers).
- Ends with "Test it now", which starts Tor in the chosen mode and waits for the self-test.
- Never asks the user to put the PS5 in the router's DMZ. That exposes every port, including the
  jailbreak's ELF loader on 9021.

## IPv6

- Most IPv6 homes have no NAT, but the router's firewall usually blocks unsolicited inbound IPv6
  by default, so "has IPv6" doesn't mean "reachable". PCP or UPnP IGDv2 (WANIPv6FirewallControl)
  can open a pinhole if the router supports it; otherwise the user opens one by hand.
- **Relays need IPv4.** Tor relays must have a reachable IPv4 ORPort; IPv6 is an extra address,
  not a replacement. So a CGNAT user with good IPv6 still can't run a relay.
- **Bridges over IPv6 only:** technically possible, but few censored clients have IPv6, so the
  value is low. Offer it as "advanced" with that caveat, or skip it in the first releases.
- **Snowflake** works over IPv4 or IPv6 (ICE tries both); nothing special needed.
- **Client:** Tor uses IPv6 for outbound connections where it helps (`ClientUseIPv6`); leave
  Tor's defaults.
- Tor's self-test checks IPv6 ORPort reachability separately from IPv4 in current versions. If
  IPv6 fails, the app drops the IPv6 ORPort rather than leaving a broken address in the
  descriptor.

## CGNAT, double NAT and shared networks

- No inbound mode can work behind CGNAT without the ISP's help. Some ISPs give a public IPv4 on
  request or for a fee; the app can mention that once, in the details, and then stop suggesting
  it.
- Double NAT: works only if the user can also forward on the outer router, or switch one router
  to bridge/modem mode. The guide says which one is which (the outer one's LAN address is the inner
  one's WAN address).
- Shared or institutional networks (Jared's building is one): Auto mode stops at Snowflake (if
  turned on). A user can still pick a bridge or relay by hand if the network lets them, after a
  warning that complaints and blocks land on whoever owns the IP and on every other user of it.

## Confirming reachability

Tor already checks itself, so the app relies on that instead of its own test:

- A bridge or relay builds a circuit back to its own ORPort through other relays. If that works,
  it publishes its descriptor; if not, it logs that it hasn't confirmed reachability and keeps
  trying, without publishing.
- The background part listens on Tor's control port (localhost only, cookie auth) for the
  `STATUS_SERVER` events `CHECKING_REACHABILITY`, `REACHABILITY_SUCCEEDED` and
  `REACHABILITY_FAILED`, and shows the user "Checking (can take up to 20 minutes)", "Reachable",
  or "Not reachable from the internet" with the likely reason from the setup check.
- On failure the app offers: retry, the manual guide, or turn on Snowflake. After repeated
  failures Auto mode drops back to the highest rung that still holds (client, or Snowflake if the
  user has it on).
- Because the test goes out and comes back through other relays, a router without NAT loopback
  ("hairpinning") doesn't give a false failure.
- For bridges, a reachable ORPort isn't enough: the obfs4 port must also be reachable. The bridge
  reachability is visible on Tor's bridge status page (bridges.torproject.org/status, by
  fingerprint) after a while; link to it from the details screen.

## Snowflake proxy (optional, off by default)

A setting the user turns on. Off by default because it is not needed by Tor itself, and because
the people who can run a bridge or relay help more that way.

### When to turn Snowflake on (text for the app)

"Turn on Snowflake if you'd like to help people in censored countries reach Tor but your PS5 can't
be a bridge or relay. That's the case if:
- your internet provider shares one address between customers (CGNAT; common with mobile, 5G home
  internet and satellite),
- you're behind two routers, or on a building, dorm or work network,
- you can't or don't want to open a port on your router.
If your PS5 is already a bridge or relay, Snowflake is optional extra help. Don't turn it on if
Tor is blocked where you live."

The setup check decides when to show this hint: whenever the best rung it finds is 0.

Rewritten in C++ on libdatachannel (Go has no PS5 build, and the PS5 kernel blocks or renumbers
syscalls Go calls directly):

1. Poll the broker over HTTPS (snowflake-broker.torproject.net) for a client offer.
2. Answer it over WebRTC with libdatachannel (libjuice for ICE/STUN, usrsctp for data channels,
   OpenSSL for DTLS), using the proxy's default STUN servers.
3. Relay bytes between the data channel and a WebSocket to the Snowflake relay
   (snowflake.torproject.net).
4. Run the NAT-type check so the broker knows whether to pair it with any client ("unrestricted")
   or only easy ones ("restricted").

Additions:

- **Capacity and bandwidth limits** (the official proxy has a capacity setting): max simultaneous
  clients, and an optional total-per-month cap for metered users.
- **Better NAT type with a mapping (optional).** If the user allowed port mapping, pin libjuice to
  a small UDP port range and map it. That may move a "restricted" proxy to "unrestricted" and get
  it more clients. Worth trying after the basic proxy works; not needed for launch.
- **Don't run as a proxy inside a censored country.** If the broker can't be reached or the
  setup check says the network is censored, say so and offer client mode instead.
- Snowflake doesn't need Tor running on the PS5, so this mode stays light (no Tor process at all
  unless the user also wants the client or an onion service).

## Bridges

- **obfs4 is the transport to ship.** It is the most used bridge transport. The server side is
  lyrebird (Go upstream), so it needs a C/C++ implementation: the obfs4 handshake (ntor-based, with
  Elligator2-encoded keys), framing and padding. The spec and Go source (BSD) are public; a C++
  version shared between server (bridge) and client (censored users) halves the work. Test it
  against stock lyrebird clients and servers on a PC before trusting it.
- **Private bridge for friends** (`BridgeDistribution none`): the bridge isn't handed out by Tor;
  the app shows the bridge line and a QR code so the owner can give it to people they know. This
  is the most useful bridge for someone with family in a censored country, and it is the safest
  bridge setting (censors can't learn it from Tor's distributors).
- **Public bridge** (`BridgeDistribution any`): Tor hands it out. Needs steady uptime to be useful.
- **WebTunnel:** needs a domain name, a TLS certificate and a web server on 443 in front of it.
  Not practical for home users on a PS5; leave out as a server mode. Do support it as a client
  transport (see "Censored networks").
- **Vanilla bridge (no transport):** easy to block and identify; not offered.
- **Snowflake** isn't a bridge the user runs; the proxy mode is how a PS5 helps Snowflake users.
- Bridge requirements to tell users: steady uptime, an open port, and an IP that doesn't change
  often. A bridge needs a `ContactInfo` (Tor uses it to reach operators); the app asks for an
  email or "none" and explains it is published only for relays, not bridges.

## Relays (middle / guard)

- Needs: a reachable IPv4 ORPort, decent bandwidth (the Tor Project's relay guide asks for at
  least 16 Mbit/s each way; slower relays get little traffic), and days of steady uptime before
  it earns the flags that bring real traffic. A PS5 that loses its jailbreak on every restart
  rarely meets the uptime part; the app says so before the user picks relay mode.
- Set `ContactInfo` (published) and `Nickname`; explain both.
- Relay mode is always non-exit (`ExitRelay 0`, `ExitPolicy reject *:*`). Exit is a separate,
  manual mode (below), never a side effect of relay settings.
- Bandwidth: `RelayBandwidthRate` / `RelayBandwidthBurst`, and `AccountingMax` with
  `AccountingStart` for monthly caps.
- Several PS5s behind one IP: Tor limits how many relays share an address. The app doesn't need
  to handle this beyond a note.
- Family: an owner running more than one relay should declare them as a family; mention it in
  the docs, not the UI.

## Exit relay: expert only

Added at Jared's request (2026-10-26). Off by default and never chosen by Auto mode.

**Principle (Jared, 2026-10-26): warn clearly, then let the user decide.** Laws and ISP terms
differ a lot between countries (running an exit is ordinary in places like Switzerland or
Germany), so the app informs instead of refusing. The only hard limits are technical ones.

**The warning the app shows before turning it on** (once, with one confirm button; it stays
readable under "About exit relays"):

"The Tor Project recommends against running an exit relay from home. An exit sends other people's
internet traffic out from your IP address. Abuse complaints, copyright notices and police
requests about that traffic will come to you and your internet provider. Most home internet plans
forbid it, your provider may cut you off, and in some countries operators' equipment has been
seized during investigations. Websites, games and online services (possibly including PlayStation
Network) may block or flag your IP. Only continue if your internet provider knows and agrees."

Links: the Tor Project's relay operator guide and its exit-relay guidelines, and the EFF's Tor
legal FAQ.

**Technical requirements** (the only things that block it):
- A reachable IPv4 ORPort, confirmed by Tor's self-test. Without it an exit can't work at all.

**Things the app warns about but doesn't block** (shown with the warning if they apply):
- The setup check found a shared or institutional network, a VPN, or a metered link.
- No `ContactInfo`: abuse reports and the Tor Project can't reach the operator, and exits without
  contact details are more likely to be flagged by the directory authorities. The app strongly
  suggests an email but accepts "none".
- Low upload bandwidth or short uptime: the exit will get little use.

**Settings:**
- `ExitRelay 1` with the Tor Project's reduced exit policy (web, mail retrieval, chat and similar
  ports; no SMTP 25 and no common abuse/file-sharing ports). Tor has no built-in name for it, so
  the app writes out the port list. A "custom policy" field is an advanced option.
- `IPv6Exit` only if IPv6 is confirmed reachable.
- DNS: exits resolve names for their users, and Tor recommends a local caching resolver rather
  than the ISP's or a big public one. Whether a resolver can run inside the payload is a console
  unknown; until then the system resolver is used and the app notes it.
- An exit notice page served on port 80 (Tor's `DirPortFrontPage`) where the network allows,
  explaining to anyone looking up the IP that it is a Tor exit.
- Separate identity from the non-exit relay is not needed (the relay is already public), but
  turning exit off later keeps the same relay and just sets `ExitRelay 0`.

**Behaviour:** Auto mode never turns exit on by itself. If reachability is lost or the cap is
reached, the exit pauses and resumes on its own when things recover, since the user already chose
it. A network change (different router, new location) pauses it and asks the user once whether to
keep exiting on the new network.

## Bandwidth, data caps and gaming

A PS5 is a gaming machine, so the node must not ruin the owner's games.

- **Defaults are conservative:** Snowflake capped at a few clients; bridge/relay rate limited to a
  modest share of measured upload.
- **Measured upload:** a short speed check (upload to the Snowflake relay or a directory mirror, no
  third-party speed test) sets suggested limits.
- **Monthly cap** for metered users: `AccountingMax` for Tor modes, a byte counter for Snowflake.
  When the cap is reached the node stops until the next period and says why.
- **Game-friendly mode:** filling the upload causes lag (bufferbloat). If the background part can
  tell a game is running (console unknown), it drops to a lower limit; if it can't, the user sets
  a "while playing" limit by hand or schedules the node for off hours.
- Status screen shows traffic today, this month, and clients helped.

## Censored networks: the PS5 as a Tor client

For users in countries that block Tor, the client needs pluggable transports. All of these are Go
upstream, so each needs a C/C++ version:

- **Snowflake client:** shares libdatachannel and most protocol code with the Snowflake proxy.
  Reaching the broker needs domain fronting or AMP cache rendezvous, as the official client does.
  First choice, because it works in most censored countries.
- **obfs4 client:** the same C++ obfs4 code as the bridge server. Needs bridge lines, which the
  user gets from bridges.torproject.org, Tor's Telegram bot, email, or a friend's private bridge.
  Typing a bridge line on a controller is painful, so the LAN web page ("Onion browsing" item 5)
  lets the user paste it from a phone or computer.
- **WebTunnel client:** HTTPS plus a WebSocket-like upgrade; the simplest of the three to write.
- **Connection Assist:** Tor Browser asks a Tor Project service (the moat circumvention API at
  bridges.torproject.org) which transport works in the user's country. The app can do the same,
  reached over a fronted channel; check what Tor Browser currently uses before building it.
- **Strict firewalls** (only 80/443 out): `ReachableAddresses *:80,*:443` or bridges on 443.
- **Bootstrap watching:** the app reads Tor's bootstrap progress from the control port. Stuck at
  the same step for a few minutes means Tor is probably blocked; the app suggests bridges.
- **Risk note for this audience:** in some countries using Tor, or running anything that helps
  others use it, is dangerous. The app should say this plainly, and Auto mode keeps all
  help-others roles off and shows no "help others" prompts there. A user who knows their
  situation can still turn them on by hand after the warning.

## Onion services hosted from the PS5

Onion services work behind any NAT, CGNAT or firewall, because the PS5 connects out to Tor's
introduction points and rendezvous points; nothing connects in. This is the one way to reach the
PS5 from outside the home with no open port.

- **Status page (first use):** a tiny read-only page served by the background part over a v3
  onion service, showing mode, traffic, uptime and reachability. Lets the owner check on the node
  from their phone (Tor Browser or Orbot) anywhere.
- **Client authorization on by default.** Only devices with the key the app shows (as text and on
  the LAN web page) can even find the service. Without the key the address is useless.
- **Optional personal static site:** serve a folder from /data. Off by default; the app warns that
  the owner is responsible for what they host.
- **Never expose the jailbreak's ELF loader (9021), FTP or any debug port as an onion service.**
  Anyone who reached it would own the console. Hard-coded refusal.
- Keys live in /download0 next to Tor's data so the .onion address survives store updates.
- Tor's built-in defences apply (vanguards-lite in current Tor). The proof-of-work DoS defence may
  need a build option; check when building and enable it if it doesn't force license changes.
- Status page controls (start/stop the node from the phone) are a later step; read-only first.

## Onion browsing: every path

All paths except 1b need the Tor client running in the background part.

1. **PS5 browser through a local proxy (try first).** PS5 network settings have a manual proxy
   field. Point it at Tor's `HTTPTunnelPort` (HTTP CONNECT) on 127.0.0.1 or the PS5's LAN IP.
   HTTPS and .onion work; plain-http sites need a small HTTP-to-SOCKS shim in the payload, which
   also handles http:// .onion sites (most onion sites are plain http). Unknowns: whether the field
   accepts localhost, and whether it sends all console traffic (store, PSN, game updates) through
   Tor. If it does, the app warns that PSN may break and offers to turn the proxy off again.
   - 1b. **The same, through another device's Tor.** If a PC or phone on the LAN already runs Tor
     with an HTTP proxy, the PS5 browser can use that instead; the app just explains how.
2. **Built-in reader in the app.** Fetch pages through Tor's SOCKS port and render with a small
   HTML engine (e.g. litehtml), driven by the controller. Fully under our control, no
   JavaScript, works even if path 1 fails. Fine for text sites, forums and search.
3. **Tor for the LAN.** SOCKS5 and HTTP proxy ports on the LAN so other devices (smart TVs, other
   consoles, old computers that can't run Tor) can use it. Off by default; `SocksPolicy` accepts
   only private LAN ranges; the app warns that anyone on the LAN can use it. A PAC file served on
   the LAN makes browser setup one URL.
4. **The PS5's hidden web browser** (the one jailbreaks already use) follows the same system proxy
   setting as path 1, so it's the same path, not a separate one. No separate work.
5. **LAN web page.** The background part serves a small page on the LAN (status, settings, paste a
   bridge line, show the onion service key). Useful because typing long bridge lines and onion
   addresses on a controller is painful. It isn't a browsing path but it makes paths 1 to 3
   usable. Bound to the LAN only, with a PIN shown on the TV.

Not possible: Tor Browser itself (Firefox doesn't build for the PS5), and transparent proxying of
the PS5's own traffic (the payload can't redirect the system's packets).

Whichever path is used, the app says: **this is not Tor Browser.** The PS5's browser keeps its own
cookies and storage and has a recognizable fingerprint, so sites can still tell visitors apart.
It reaches .onion sites; it doesn't make you anonymous the way Tor Browser does. Use Tor Browser
on a computer or phone when anonymity matters.

## Risks

**Exit relay:** see "Exit relay: expert only". The highest-risk role by far; manual, warned, and
off by default.

**Relays and bridges at home:** a listed relay's IP is public, and some sites, games and
streaming services block every Tor relay IP, not only exits. That includes possibly PSN-adjacent
services and games with anti-cheat that block Tor IPs; the app should say this before relay mode
starts. Behind a shared IP it hits everyone else on it. Bridges aren't listed, so they avoid most
of this.

**Port mapping:** opening a port exposes only Tor's port, but the app must never map anything
else, and must clean up its mappings.

**Snowflake proxy:** the people connecting through you are hidden from you and you from them;
your IP isn't published; what leaves your network is encrypted traffic to Tor's own servers.

**Onion services:** private by default; hosting public content makes the owner responsible for it.

**Censored countries:** using or helping Tor can be dangerous; the app keeps help-others modes off
there and says why.

**Onion browser:** visiting .onion sites is legal in most countries, but the PS5's browser isn't
built to hide who you are.

**The name.** "Tor" is a trademark of The Tor Project, and its policy asks that apps not use "Tor"
in their name without permission. "Onion" is fine, hence OnionStation5. The About page says
"Uses Tor" and links to the Tor Project.

## How it's put together (same shape as WireStationVPN)

1. **The app** (a normal title): setup check and recommendation, mode picker, port-mapping
   consent, manual guide, status (clients helped, traffic, uptime, reachability), bridge line and
   QR code, onion service key, limits, About page.
2. **The background part** (a payload): "Run in background" hands it to the jailbreak's ELF loader
   at 127.0.0.1:9021 and closes the app. It runs Tor and/or the Snowflake proxy, the port-mapping
   client, the control-port watcher, the HTTP shim and the LAN page. Tor needs no GPU and roughly
   50 to 300 MB of memory.
3. **Keep-alive on by default:** restarts Tor or the proxy if it stalls; the app restarts the
   background part if it has died; port-mapping leases are renewed by the background part and
   expire if it dies.
4. **Data in /download0** from the first release (Tor's DataDirectory, relay/bridge keys, onion
   service keys, settings), so a store update doesn't wipe identities. Deleting the app still
   wipes them; the About page says so.
5. **Control port** on 127.0.0.1 only, cookie authentication; the app and the background part talk
   over localhost.

## Porting Tor and the other pieces

Tor's needs: libevent 2.1, OpenSSL 3, zlib (zstd and xz optional, left out). Static payload with
the same SDK TokenStation5 uses.

Known PS5 socket gotchas from TokenStation5's WireGuard work:
- **fcntl O_NONBLOCK doesn't take.** Patch libevent's `evutil_make_socket_nonblocking` and Tor's
  `set_socket_nonblocking` to `ioctl(FIONBIO)`. Same for libjuice, miniupnpc and libnatpmp.
- **poll doesn't report ready sockets.** Try kqueue, then select; fall back to a short timed loop.
- **Some ports can't be bound** from a title (UDP 51820 and 40000 failed, 9030 worked). Check what
  a payload can bind, especially TCP 9001, 443 and random high ports.
- Tor needs a correct clock; check it on start and warn if off.

New pieces in this revision:

| Piece | Upstream | Plan | License |
|---|---|---|---|
| Tor | C | Port | BSD-3 |
| Snowflake proxy | Go | Rewrite in C++ on libdatachannel | BSD-3 upstream; ours as chosen |
| Snowflake client | Go | Rewrite, sharing proxy code | same |
| obfs4 server + client | Go (lyrebird) | Rewrite in C++ | BSD-2/3 upstream |
| WebTunnel client | Go | Rewrite in C++ | BSD upstream |
| UPnP IGD | miniupnpc (C) | Port | BSD-3 |
| NAT-PMP | libnatpmp (C) | Port | BSD-3 |
| PCP | (none needed) | Write directly | ours |
| STUN check | libjuice (C) | Already in via libdatachannel | MPL-2.0 |
| HTML reader | litehtml (C++) | Port | BSD-3 |

Arti (Rust) is out: no Rust target for the PS5. None of these forces GPL; reusing TokenStation5's
GPL payload code would make the repo GPL-3.0 like WireStationVPN.

## Testing inbound modes without an open port

Jared's own network can't test bridge or relay modes from outside, so:

- **A private Tor test network on the LAN** (Tor's `chutney` tool on a PC, `TestingTorNetwork`):
  the PS5 joins a test network whose authorities and relays all run on the PC. Tests relay and
  bridge code paths, the ORPort, and the control-port events without the internet.
- **obfs4 interop:** stock lyrebird client on the PC against the PS5's obfs4 server on the LAN, and
  the other way round.
- **Port mapping:** a spare consumer router (or a PC running miniupnpd) on the LAN behind Jared's
  network gives UPnP/NAT-PMP/PCP to test against; the double-NAT detection gets tested for free.
- **Real internet test:** a tester on a home network with a forwardable port, before release.

## What works and what doesn't (for the About page)

- Works everywhere: Tor client, onion browsing, onion services (status page), and the optional
  Snowflake proxy; home screen and during games.
- Needs a port open to the internet: private bridge, public bridge, relay, exit. The app checks
  for you.
- Doesn't work behind CGNAT or double NAT: bridge, relay and exit modes.
- Exit relay: expert only, off by default. The Tor Project recommends against running one at
  home.
- Stops: in rest mode (to check), after a PS5 restart until you re-jailbreak and reopen the app.
- Not Tor Browser: no full Tor Browser privacy in the PS5 browser.

## Unknowns that need the console (MASTER CHAT owns it; nothing here touches it)

1. Which event backend (kqueue, select, poll) reports ready sockets inside a payload?
2. Do WebRTC's UDP sockets (random high ports) bind and receive in a payload?
3. Does the PS5 proxy setting accept the payload's port, and what traffic goes through it?
4. The same three as WireStationVPN: 9021 launch then app close, surviving games, rest mode.
5. Can a payload bind TCP 9001, 443 and random high ports, on IPv4 and IPv6?
6. Can a payload send SSDP multicast (239.255.255.250:1900) and receive the replies? (UPnP)
7. Can a payload read the default gateway and interface addresses, including IPv6? (NAT-PMP, PCP,
   setup check)
8. Can a payload tell whether a game is running? (game-friendly bandwidth mode)
9. Does the PS5 get and use a global IPv6 address on IPv6 networks, and does it accept inbound
   IPv6?
10. Can a payload notice network changes (address change, Wi-Fi to wired) without polling, or does
    Auto mode poll the interface list?

## Build order

0. Repo exists (ThatRetiredDude/OnionStation). Open the catalog reservation for PPSA99557.
1. Tor client as a payload started by hand over 9021, SOCKS on the LAN; bootstrap to 100%.
   Answers unknown 1.
2. App front end, background launch, keep-alive, status, limits, About page.
3. Setup check (addresses, STUN, CGNAT/double-NAT detection) and the recommendation screen.
   Answers unknowns 6, 7 and 9.
4. Onion browsing path 1 and the HTTP shim; path 2 if path 1 fails. LAN page (path 5).
5. Onion service status page with client authorization.
6. First store release: client, onion browsing, onion status page.
7. Port mapping (PCP, NAT-PMP, UPnP) with consent, leases and cleanup; manual guide; reachability
   via control-port events. Tested with the chutney test network and a spare router.
8. obfs4 in C++ (server and client); private bridge with bridge line and QR code; then public
   bridge. Interop-tested against lyrebird.
9. Relay mode (consent-gated, with the uptime and blocking warnings), then the expert-only exit
   mode with its warning, reduced exit policy and exit notice page.
10. Auto mode ladder over everything above: promotion on proof, demotion on change, no
    flapping, separate identities per role. Until this step, the app recommends and the user
    picks.
11. Snowflake proxy in C++ (optional setting) with the NAT check and the "when to turn it on"
    hint. Answers unknown 2. Until this lands, users with no inbound can use Tor but can't help
    the network, Jared's own PS5 included.
12. Censored-network client: Snowflake client, then obfs4 client, then WebTunnel client;
    bootstrap watching and Connection Assist.
13. Optional: Snowflake NAT improvement with a mapped UDP range; IPv6 bridge; personal static
    onion site; phone controls on the status page.

## Prospero Store, title ID and name

- **Title ID PPSA99557.** Checked against darkxex/ps5-homebrew-catalog (latest commit
  2026-10-26 09:21): no apps/PPSA99557.json or PPSA99557-*.json. PPSA99555 is TokenStation5 and
  PPSA99556 is held for WireStationVPN. Re-check right before reserving, and check open PRs too.
- **Name "OnionStation5"** (confirmed by Jared 2026-10-26). No catalog name contains "onion",
  "tor", "relay", "snowflake" or "proxy" today. Names must be unique ignoring case.
- param.json: titleId PPSA99557, conceptId 99557, contentId UP9000-PPSA99557_00-ONIONSTATION5000,
  titleName "OnionStation5".
- **Reserve early** from the ThatRetiredDude account (max 5 per account; touch every 180 days).
  With TokenStation5 and WireStationVPN this would be the third.
- Same store lessons as WireStationVPN: homebrew range only; never change titleId, conceptId or
  contentId between releases; downloadDataSize no higher than 256; ZIP with PPSA99557/ at its
  root; bump contentVersion and tag the same string; never replace a listed release ZIP; catalog
  PRs change one file; description up to 200 characters; author "ThatRetiredDude"; icon from
  Jared's own logo (added 2026-10-26: `assets/logo-original.jpg`, `sce_sys/icon0.png` 512×512,
  `sce_sys/pic0.png` 1920×1080).
- **Logo trademark check (open):** the supplied logo spells "Tor" and draws the "o" as the
  PlayStation "PS" mark. Both are trademarks (The Tor Project; Sony). The Tor Project's policy
  asks for permission before using "Tor" this way, and Sony's mark on a jailbreak-only app invites
  a takedown. Options: ask the Tor Project, or change the word (e.g. "Onion") and the "o".
- **Store description draft (under 200):** "Turn your PS5 into a Tor bridge or relay that adapts
  to your network, or help through Snowflake. Runs in the background, even during games. Opens
  onion sites. Needs a jailbroken PS5."

## Decisions still open

1. Decided 2026-10-26: Snowflake is an optional setting, off by default; exit relay is offered as
   an expert-only manual mode with the Tor Project's at-home warning. Approach: warn clearly,
   then let users decide; only technical limits block a mode.
2. Whether public bridge and relay modes are in scope at all, or only private bridges (recommended:
   private bridge first, public bridge next, relay last, since a PS5's uptime suits relays least).
3. Whether censored-network client transports (step 11) are in scope; they are a large rewrite
   for a smaller audience of PS5 owners inside censored countries.
4. Repo license.
