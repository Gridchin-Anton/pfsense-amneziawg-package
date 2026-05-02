#!/bin/sh
# Build amneziawg-go for packaging. Invoked from Makefile (avoids make $(…) parsing).
set -e
PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:${PATH}"
export PATH

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC="$1"
OUT="$2"
PATCH_GOMOD="${3:-1}"

if [ ! -d "$SRC" ] || [ -z "$OUT" ]; then
	echo "usage: $0 <amneziawg-go-src-dir> <output-binary> [patch_gomod 0|1]" >&2
	exit 2
fi

REALGO=""
GROOT=""
for d in /usr/local/go[0-9]*; do
	[ -x "$d/bin/go" ] && GROOT="$d"
done
if [ -n "$GROOT" ]; then
	REALGO="$GROOT/bin/go"
	export GOROOT="$GROOT"
fi
if [ -z "$REALGO" ]; then
	for G in /usr/local/bin/go /usr/local/sbin/go /usr/local/go/bin/go; do
		if [ -x "$G" ]; then REALGO="$G"; break; fi
	done
fi
if [ -z "$REALGO" ]; then
	echo "ERROR: no Go compiler found (try: pkg install -y go123)" >&2
	exit 1
fi
if [ -z "$GOROOT" ]; then
	GROOT=$(ls -d /usr/local/go[0-9]* 2>/dev/null | sort -V | tail -1)
	if [ -n "$GROOT" ] && [ -d "$GROOT/src" ]; then
		export GOROOT="$GROOT"
	fi
fi

export GOWORK=off
# Some FreeBSD 15-CURRENT / pfSense hosts hit SIGSEGV in netpoll during parallel
# module fetches; keep the toolchain calmer for cmd/go network I/O.
export GOMAXPROCS=1
export GODEBUG=asyncpreemptoff=1

cd "$SRC"

if [ "$PATCH_GOMOD" != "0" ]; then
	NUMVER=$("$REALGO" env GOVERSION 2>/dev/null | sed -e 's/^go//' | tr -d '\r\n')
	if [ -z "$NUMVER" ]; then
		NUMVER=$("$REALGO" version 2>/dev/null | awk '{print $3}' | sed -e 's/^go//')
	fi
	if [ -n "$NUMVER" ]; then
		echo "===> Aligning go.mod to host toolchain: go $NUMVER"
		cp -f go.mod go.mod.pfsense.bak
		sed -i.bak -e '/^toolchain[[:space:]]/d' \
			-e "s/^go[[:space:]].*/go $NUMVER/" go.mod
		echo "===> go.mod head now:"
		head -n 5 go.mod
	else
		echo "===> WARNING: could not read Go version; go.mod not patched" >&2
	fi
fi

VFLAG=""
if [ -d "$ROOT/third_party/amneziawg-go/vendor" ]; then
	echo "===> Using vendored modules (third_party/amneziawg-go/vendor) — no module proxy fetch"
	rm -rf vendor
	cp -a "$ROOT/third_party/amneziawg-go/vendor" .
	VFLAG="-mod=vendor"
fi

# Without vendor: keep GOTOOLCHAIN=local so cmd/go does not download a newer toolchain
# (can SIGSEGV on some pfSense kernels during downloads).
# With -mod=vendor: vendored deps may record go >= 1.24 while the host is go123 (1.23.x).
# GOTOOLCHAIN=auto then only upgrades the *compiler*; modules still come from vendor/.
GOTOOLCHAIN="${AMNEZIAWG_GOTOOLCHAIN:-}"
if [ -z "$GOTOOLCHAIN" ]; then
	if [ -n "$VFLAG" ]; then
		GOTOOLCHAIN=auto
	else
		GOTOOLCHAIN=local
	fi
fi

echo "===> go build with $REALGO (GOROOT=${GOROOT:-}) GOTOOLCHAIN=$GOTOOLCHAIN GOMAXPROCS=$GOMAXPROCS GODEBUG=$GODEBUG"
exec env GOTOOLCHAIN="$GOTOOLCHAIN" GOWORK=off GOMAXPROCS=1 GODEBUG=asyncpreemptoff=1 \
	CGO_ENABLED=0 GOOS=freebsd GOARCH=amd64 \
	"$REALGO" build -p 1 -trimpath $VFLAG -ldflags="-s -w" -o "$OUT" .
