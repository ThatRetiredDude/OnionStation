# The Tor payload (build step 1)

`onionstation-tor.elf` is Tor 0.4.9.14, unmodified, built as a PS5 payload with a small start
code around it. It is step 1 of the build order in `PLAN.md`: a Tor client started by hand
through the jailbreak's ELF loader, with SOCKS on the home network. The app, Auto mode, bridges,
relays and Snowflake come later.

## Getting it

- **From CI:** every push and pull request runs `.github/workflows/payload.yml`. The `ps5` job
  uploads `onionstation-tor.elf` as the `onionstation-tor-ps5` artifact on the run's page.
- **Building it yourself** (Ubuntu 24.04): install `clang-18 lld-18 llvm-18 autoconf automake
  libtool pkg-config wget python3`, put `/usr/lib/llvm-18/bin` first on `PATH`, then run
  `payload/build.sh`. The first build fetches and builds the payload SDK, zlib, OpenSSL,
  libevent and Tor (each pinned and hash-checked) into `.deps/`, and writes
  `build/ps5/onionstation-tor.elf`. `payload/build.sh host` builds the same thing for Linux.

## What it does when it starts

1. Makes `/data/onionstation/` and `/data/onionstation/tor/` (Tor's data).
2. Writes `/data/onionstation/torrc` with the defaults below, only if it doesn't exist yet. Edit
   it over FTP to change settings; delete it to get the defaults back.
3. Reads optional `KEY=VALUE` lines from `/data/onionstation/env` (see "Console tests").
4. Warns (notification) if the console clock is earlier than the build date. Tor can't connect
   with a wrong clock.
5. Starts Tor with that torrc, logging to `/data/onionstation/tor.log`, and shows notifications
   when Tor starts, at 50% and at 100% bootstrap ("Tor is connected").

Default settings:

| Port | What | Who can use it |
|---|---|---|
| 9050 | SOCKS5 proxy | This console and private LAN addresses (`SocksPolicy`) |
| 9080 | HTTP CONNECT proxy, for the PS5 browser | This console only (127.0.0.1) |
| 9051 | Control port, cookie authentication | This console only (127.0.0.1) |

To stop it, restart the console (or, later, the app sends Tor a shutdown over the control port).

## PS5 workarounds in the start code

`payload/src/ps5_compat.c` is linked around Tor, libevent and OpenSSL with `--wrap`, so none of
them is patched:

- `fcntl(F_SETFL, O_NONBLOCK)` also sets `ioctl(FIONBIO)`, the call TokenStation5 found works on
  the PS5.
- `socket()` and `socketpair()` with `SOCK_NONBLOCK` / `SOCK_CLOEXEC` apply those flags after
  the socket is made.
- Tor is configured without `accept4`, `pipe2` and `eventfd`, so it uses the plain calls that
  the wrappers cover.

## Console tests (for whoever runs the console)

Each test answers one of the console unknowns in `PLAN.md`. Nothing here changes anything outside
`/data/onionstation`.

1. **Does it start and bootstrap?** (unknowns 1 and 4)
   `payload/send.sh <PS5 IP>`. Expect "starting Tor", then "halfway", then "Tor is connected"
   within a few minutes. Fetch `/data/onionstation/tor.log` over FTP either way.
2. **If it stalls before 100%:** the event backend may not report ready sockets. Try each in
   turn by putting one line in `/data/onionstation/env`, restarting the console, and sending
   the payload again:
   - `EVENT_NOKQUEUE=1` (libevent then tries poll, then select)
   - `EVENT_NOKQUEUE=1` and `EVENT_NOPOLL=1` on two lines (select only)
   Note which one bootstraps. `EVENT_SHOW_METHOD=1` makes libevent log the backend it picked.
3. **SOCKS from the LAN:** on a computer on the same network,
   `curl --socks5-hostname <PS5 IP>:9050 https://check.torproject.org/api/ip` should print
   `"IsTor":true`.
4. **PS5 browser** (unknown 3): Settings > Network > Settings > Set Up Internet Connection >
   your connection > Advanced Settings > Proxy Server: Use, address `127.0.0.1`, port `9080`.
   Open an https site, then an https .onion site. Note whether the store and PSN still work with
   the proxy on, then set Proxy Server back to "Do Not Use". Plain-http sites won't load through
   this port yet (the HTTP shim is build step 4).
5. **Survives a game** (unknown 4): start a game for a few minutes, then repeat test 3.

## License of the built payload

The OnionStation5 source is BSD-3-Clause. The payload SDK it links against (its start code and
libc additions) is GPL-3.0, so the built `onionstation-tor.elf` as a whole is distributed under
GPL-3.0. Tor and libevent (BSD), OpenSSL (Apache-2.0) and zlib (zlib) all allow this.
