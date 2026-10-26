#!/usr/bin/env bash
# Builds Tor as a PS5 payload (build step 1 in docs/PLAN.md):
#
#   build/ps5/onionstation-tor.elf     send it to the console's ELF loader (payload/send.sh)
#
# or, with "host", the same Tor and start code for this Linux machine, which CI
# runs to check that the settings bootstrap:
#
#   build/host/onionstation-tor
#
# Everything is fetched at pinned versions and checked against its hash:
# ps5-payload-sdk (built from source), zlib, OpenSSL, libevent and Tor, the
# same versions and hashes ps5-payload-dev/pacbrew-repo uses for the first three.
#
#   payload/build.sh [ps5|host]
#
# Needs clang 18 and lld 18 (llvm-config-18), make, autoconf, automake,
# libtool, pkg-config, perl, python3, wget, git.
set -euo pipefail

target=${1:-ps5}
case $target in ps5|host) ;; *) echo "usage: $0 [ps5|host]" >&2; exit 2 ;; esac

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
deps="${ONIONSTATION_DEPS:-$root/.deps}"
dl="$deps/dl"
src="$deps/src-$target"
prefix="$deps/$target"
out="$root/build/$target"
jobs=${JOBS:-$(nproc)}
mkdir -p "$dl" "$src" "$prefix" "$out"

sdk_rev=a7685ce8e97ce1f8a4f38253a41d54241c9124f1
zlib_ver=1.3.2
zlib_sha=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
openssl_ver=3.5.2
openssl_sha=c53a47e5e441c930c3928cf7bf6fb00e5d129b630e0aa873b08258656e7345ec
libevent_ver=2.1.12
libevent_sha=92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb
tor_ver=0.4.9.14
tor_sha=182449ac1c8ff43278da27b69b02040e11d03d0327db6d9126e4190ec6235e4a

# A release tarball, downloaded once and checked against its SHA-256.
fetch() {
    local url=$1 sha=$2 file="$dl/$(basename "$1")"
    if [ ! -f "$file" ]; then
        echo "build: downloading $(basename "$file")"
        wget -q -O "$file.part" "$url"
        mv "$file.part" "$file"
    fi
    echo "$sha  $file" | sha256sum --check --strict --quiet
}

# Unpacks a tarball into $src, fresh each time its stamp is missing.
unpack() {
    local file="$dl/$1" dir="$src/$2"
    if [ ! -f "$dir/.unpacked" ]; then
        rm -rf "$dir"
        tar -xzf "$file" -C "$src"
        touch "$dir/.unpacked"
    fi
}

# The payload SDK, built from source at a pinned commit (its releases are a
# binary build of the same tree).
sdk="$deps/ps5-payload-sdk"
if [ "$target" = ps5 ] && [ ! -f "$sdk/.built-$sdk_rev" ]; then
    sdk_src="$deps/sdk-src"
    if [ ! -d "$sdk_src/.git" ]; then
        git init -q "$sdk_src"
        git -C "$sdk_src" remote add origin https://github.com/ps5-payload-dev/sdk.git
    fi
    git -C "$sdk_src" fetch -q --depth 1 origin "$sdk_rev"
    git -C "$sdk_src" checkout -q --force FETCH_HEAD
    rm -rf "$sdk"
    make -C "$sdk_src" -j1 DESTDIR="$sdk" install > "$deps/sdk-build.log" 2>&1 ||
        { tail -40 "$deps/sdk-build.log"; exit 1; }
    touch "$sdk/.built-$sdk_rev"
fi

# The compilers and the configure --host for each target. For the PS5 the SDK's
# prospero-clang wraps clang with the PS5 target, sysroot, CRT and libraries.
if [ "$target" = ps5 ]; then
    # shellcheck source=/dev/null
    source "$sdk/toolchain/prospero.sh"
    # prospero.sh points installs at the SDK's sysroot; ours go to $prefix.
    unset DESTDIR PREFIX
    host=(--host=x86_64-pc-freebsd)
    openssl_target=BSD-x86_64
else
    export CC=${CC:-clang} CXX=${CXX:-clang++} AR=${AR:-ar} RANLIB=${RANLIB:-ranlib}
    host=()
    openssl_target=linux-x86_64
fi

fetch "https://github.com/madler/zlib/releases/download/v$zlib_ver/zlib-$zlib_ver.tar.gz" "$zlib_sha"
fetch "https://github.com/openssl/openssl/releases/download/openssl-$openssl_ver/openssl-$openssl_ver.tar.gz" "$openssl_sha"
fetch "https://github.com/libevent/libevent/releases/download/release-$libevent_ver-stable/libevent-$libevent_ver-stable.tar.gz" "$libevent_sha"
fetch "https://dist.torproject.org/tor-$tor_ver.tar.gz" "$tor_sha"

if [ ! -f "$prefix/lib/libz.a" ]; then
    unpack "zlib-$zlib_ver.tar.gz" "zlib-$zlib_ver"
    (cd "$src/zlib-$zlib_ver" && ./configure --prefix="$prefix" --static && make -j"$jobs" && make install) > "$src/zlib.log" 2>&1 ||
        { tail -40 "$src/zlib.log"; exit 1; }
fi

if [ ! -f "$prefix/lib/libssl.a" ]; then
    unpack "openssl-$openssl_ver.tar.gz" "openssl-$openssl_ver"
    (cd "$src/openssl-$openssl_ver" &&
        ./Configure "$openssl_target" no-tests no-apps no-docs no-shared no-module --prefix="$prefix" --libdir=lib &&
        make -j"$jobs" build_sw && make install_sw) > "$src/openssl.log" 2>&1 ||
        { tail -40 "$src/openssl.log"; exit 1; }
fi

if [ ! -f "$prefix/lib/libevent.a" ]; then
    unpack "libevent-$libevent_ver-stable.tar.gz" "libevent-$libevent_ver-stable"
    (cd "$src/libevent-$libevent_ver-stable" &&
        ./configure "${host[@]}" --prefix="$prefix" --enable-static --disable-shared \
            --disable-openssl --disable-samples --disable-libevent-regress &&
        make -j"$jobs" && make install) > "$src/libevent.log" 2>&1 ||
        { tail -40 "$src/libevent.log"; exit 1; }
fi

# Our start code: the settings, the folders and, on the PS5, the socket
# workarounds and notifications (payload/src). It wraps Tor's main(), so Tor
# itself builds unmodified.
start_flags=(-std=c11 -O2 -Wall -Wextra "-DONIONSTATION_TOR_VERSION=\"$tor_ver\""
    "-DONIONSTATION_BUILD_TIME=$(date -u +%s)")
wrap=(-Wl,--wrap=main)
[ "$target" = host ] && wrap+=(-fuse-ld=lld)
sources=("$root/payload/src/start.c")
if [ "$target" = ps5 ]; then
    start_flags+=(-DONIONSTATION_PS5=1)
    sources+=("$root/payload/src/ps5_compat.c")
    wrap+=(-Wl,--wrap=fcntl -Wl,--wrap=socket -Wl,--wrap=socketpair)
fi
rm -f "$out/libonionstation.a"
objs=()
for s in "${sources[@]}"; do
    o="$out/$(basename "$s" .c).o"
    $CC "${start_flags[@]}" -c "$s" -o "$o"
    objs+=("$o")
done
$AR rcs "$out/libonionstation.a" "${objs[@]}"

# Tor. Hardening flags are off because the PS5 target has no stack protector
# runtime or PIE linking; the payload loader relocates it instead. Functions
# the PS5 kernel handles differently are kept out (accept4, pipe2, eventfd:
# Tor falls back to accept and pipe, which then go through the wrapped fcntl).
tor_dir="$src/tor-$tor_ver"
unpack "tor-$tor_ver.tar.gz" "tor-$tor_ver"
if [ ! -f "$tor_dir/.configured" ]; then
    tor_cache=()
    [ "$target" = ps5 ] && tor_cache=(ac_cv_func_accept4=no ac_cv_func_pipe2=no ac_cv_func_eventfd=no)
    (cd "$tor_dir" && ./configure "${host[@]}" "${tor_cache[@]}" \
        --disable-asciidoc --disable-manpage --disable-html-manual --disable-unittests \
        --disable-gcc-hardening --disable-linker-hardening --disable-seccomp --disable-libscrypt \
        --disable-systemd --disable-lzma --disable-zstd --disable-module-dirauth \
        --disable-tool-name-check \
        --enable-static-openssl --with-openssl-dir="$prefix" \
        --enable-static-libevent --with-libevent-dir="$prefix" \
        --enable-static-zlib --with-zlib-dir="$prefix") > "$src/tor-configure.log" 2>&1 ||
        { tail -60 "$src/tor-configure.log"; tail -80 "$tor_dir/config.log" 2>/dev/null; exit 1; }
    touch "$tor_dir/.configured"
fi
# lld takes archives in any order, so ours can ride in LDFLAGS.
(cd "$tor_dir" && make -j"$jobs" src/app/tor \
    LDFLAGS="${wrap[*]} $out/libonionstation.a") > "$src/tor-build.log" 2>&1 ||
    { grep -E "error|undefined" "$src/tor-build.log" | head -80; tail -30 "$src/tor-build.log"; exit 1; }

if [ "$target" = ps5 ]; then
    cp "$tor_dir/src/app/tor" "$out/onionstation-tor.elf"
    "$STRIP" "$out/onionstation-tor.elf" 2>/dev/null || true
    echo "build: $out/onionstation-tor.elf ($(stat -c %s "$out/onionstation-tor.elf") bytes, Tor $tor_ver)"
else
    cp "$tor_dir/src/app/tor" "$out/onionstation-tor"
    echo "build: $out/onionstation-tor (Tor $tor_ver)"
fi
