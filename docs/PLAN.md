# OnionStation5 plan: a PS5 Tor node and onion browser for every kind of network

Date: 2026-10-26. Desk study only; nothing run on the console. Separate program from
TokenStation5 and WireStationVPN, with its own repo, title ID and store listing.

This revision replaces the first plan, which was written around one network (Jared's building:
double NAT, no port forwarding). The app has to work for anyone who installs it, so this version
covers every common home and shared network, people who won't or can't open a port, IPv6, CGNAT,
people inside censored countries, onion services hosted from the PS5, and every way to browse
onion sites.

## Short version

- **Snowflake proxy works on every network** that can reach the internet: any NAT, CGNAT, double
  NAT, IPv6-only, no port forwarding, no router access. It is the default mode and the first thing
  to build.
- **Bridge and relay modes need inbound connections.** The app finds out by itself whether it can
  get them: it tries automatic port mapping (UPnP IGD, NAT-PMP, PCP) if the user allows it, spots
  CGNAT and double NAT, checks IPv6, and offers a step-by-step manual port-forward guide. Tor's own
  reachability self-test makes the final call. The app never claims a mode works until that test
  passes.
- **The app recommends a mode instead of asking people to understand NAT.** A setup check
  classifies the network and suggests: Snowflake only, Snowflake plus private bridge, bridge, or
  relay. The user can always choose a lighter mode than the one suggested.
- **No exit node.** Unchanged from the first plan (see "Risks"). It stays out of the app.
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
| Exit relay | Traffic leaves Tor onto the internet from the owner's IP | Yes | No | **High. Not offered.** |
| Onion service | Hosts a page or service at a .onion address | **No** | **Yes** | Low for a private status page; depends on what is hosted. |
| Tor client | The PS5 uses Tor (onion browsing, LAN proxy) | No | Yes | Low. It uses Tor, it doesn't help it. |

Modes can run together: Snowflake proxy plus Tor client plus an onion service is the normal
combination for someone with no open port. A bridge and a relay can't both run on the same Tor
instance, and the app shouldn't try.

## Every kind of network, and what the app recommends

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
| Shared or institutional network (dorm, workplace, building, café, hotel) | User answers "I don't control this network" | Snowflake, or client only if the network's rules forbid proxies | Nothing inbound. Never relays on networks the user doesn't control. |
| Censored country (Tor blocked) | Tor bootstrap stalls, or the Snowflake broker can't be reached; or user says so | **Client with bridges only.** No proxy, relay or bridge (useless there and riskier) | Onion browsing through bridges |
| Network that only allows outbound 80/443 | Outbound tests on other ports fail | Client with `ReachableAddresses` limited to 80/443, or bridges on 443 | Snowflake usually still works (WebRTC over UDP/TURN is not guaranteed) |

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
6. **Ask the user two questions**, in plain words:
   - "Do you control this network's router?" (yes / no / not sure)
   - "Is your internet capped or metered?" (no / yes, with a monthly limit field)
7. **Recommend.** Show the result as one sentence and one suggested mode, e.g. "Your provider
   shares one internet address between many customers (CGNAT), so other computers can't connect
   in. Snowflake works fine here." Advanced details (addresses, NAT type, which protocol answered)
   sit behind a "Details" button.
8. **Confirm** inbound modes only after Tor's self-test passes (see "Confirming reachability").

If any step fails or times out the app falls back to Snowflake, which needs none of it.

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
- Shared or institutional networks (Jared's building is one): Snowflake only, and only if the
  network's rules allow it. Never relays or bridges on a network the user doesn't control, since
  complaints and blocks land on whoever owns the IP and on every other user of it.

## Confirming reachability

Tor already checks itself, so the app relies on that instead of its own test:

- A bridge or relay builds a circuit back to its own ORPort through other relays. If that works,
  it publishes its descriptor; if not, it logs that it hasn't confirmed reachability and keeps
  trying, without publishing.
- The background part listens on Tor's control port (localhost only, cookie auth) for the
  `STATUS_SERVER` events `CHECKING_REACHABILITY`, `REACHABILITY_SUCCEEDED` and
  `REACHABILITY_FAILED`, and shows the user "Checking (can take up to 20 minutes)", "Reachable",
  or "Not reachable from the internet" with the likely reason from the setup check.
- On failure the app offers: retry, the manual guide, or switch to Snowflake. After repeated
  failures it switches to Snowflake by itself if the user ticked "fall back automatically" (on by
  default).
- Because the test goes out and comes back through other relays, a router without NAT loopback
  ("hairpinning") doesn't give a false failure.
- For bridges, a reachable ORPort isn't enough: the obfs4 port must also be reachable. The bridge
  reachability is visible on Tor's bridge status page (bridges.torproject.org/status, by
  fingerprint) after a while; link to it from the details screen.

## Snowflake proxy

Unchanged in substance from the first plan, plus a few additions for other networks.

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
- `ExitRelay 0` and `ExitPolicy reject *:*` are hard-coded; the user can't change them.
- Bandwidth: `RelayBandwidthRate` / `RelayBandwidthBurst`, and `AccountingMax` with
  `AccountingStart` for monthly caps.
- Several PS5s behind one IP: Tor limits how many relays share an address. The app doesn't need
  to handle this beyond a note.
- Family: an owner running more than one relay should declare them as a family; mention it in
  the docs, not the UI.

## Exit node: not offered

Unchanged. Abuse reports and police requests go to whoever owns the IP; home and building
connections usually forbid it; equipment seizures have happened; and the IP is the user's own.
The Tor Project's advice is to run exits only on a host that knows and agrees, ideally not at
home. Hard-coded off, not a hidden setting.

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
  others use it, is dangerous. The app should say this plainly, keep all relay/bridge/proxy modes
  off in client-with-bridges mode, and not show "help others" prompts there.

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

**Exit node:** see above. Not offered.

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

- Works everywhere: Snowflake proxy, onion services (status page), Tor client, onion browsing,
  home screen and during games.
- Needs a port open to the internet: private bridge, public bridge, relay. The app checks for you.
- Doesn't work behind CGNAT or double NAT: bridge and relay modes.
- Not offered: exit node.
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

## Build order

0. Repo exists (ThatRetiredDude/OnionStation). Open the catalog reservation for PPSA99557.
1. Tor client as a payload started by hand over 9021, SOCKS on the LAN; bootstrap to 100%.
   Answers unknown 1.
2. Snowflake proxy in C++; broker hands it clients. Answers unknown 2. **First useful release
   candidate for everyone.**
3. App front end, background launch, keep-alive, status, limits, About page.
4. Setup check (addresses, STUN, Snowflake NAT check, CGNAT/double-NAT detection) and the
   recommendation screen. Answers unknowns 6, 7 and 9.
5. Onion browsing path 1 and the HTTP shim; path 2 if path 1 fails. LAN page (path 5).
6. Onion service status page with client authorization.
7. First store release: Snowflake, client, onion browsing, onion status page.
8. Port mapping (PCP, NAT-PMP, UPnP) with consent, leases and cleanup; manual guide; reachability
   via control-port events. Tested with the chutney test network and a spare router.
9. obfs4 in C++ (server and client); private bridge with bridge line and QR code; then public
   bridge. Interop-tested against lyrebird.
10. Relay mode (off by default, with the uptime and blocking warnings).
11. Censored-network client: Snowflake client, then obfs4 client, then WebTunnel client;
    bootstrap watching and Connection Assist.
12. Optional: Snowflake NAT improvement with a mapped UDP range; IPv6 bridge; personal static
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
  Jared's own logo.
- **Store description draft (under 200):** "Help people reach Tor from your PS5, on any home
  network. Runs a Snowflake proxy in the background, even during games. Open onion sites too.
  Needs a jailbroken PS5."

## Decisions still open

1. Confirm no exit node (recommended: confirm).
2. Whether public bridge and relay modes are in scope at all, or only private bridges (recommended:
   private bridge first, public bridge next, relay last, since a PS5's uptime suits relays least).
3. Whether censored-network client transports (step 11) are in scope; they are a large rewrite
   for a smaller audience of PS5 owners inside censored countries.
4. Repo license.
