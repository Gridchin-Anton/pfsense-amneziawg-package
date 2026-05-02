# Build pfsense-pkg-amneziawg: amneziawg-go (FreeBSD/amd64) + pkg txz
# Run on FreeBSD amd64 with Go 1.22+ and pkg installed.

VERSION=	0.1
PKGNAME=	pfsense-pkg-amneziawg
STAGEDIR=	${.CURDIR}/work/stage
DISTDIR=	${.CURDIR}/work/dist
REPODIR=	${.CURDIR}/work/repo

# amneziawg-go source (override tag or point AMNEZIAWG_SRC to a checkout)
AMNEZIAWG_REPO?=	https://github.com/amnezia-vpn/amneziawg-go
AMNEZIAWG_TAG?=	master
AMNEZIAWG_SRC?=	${.CURDIR}/work/amneziawg-go

# Go binary (override if not in PATH), e.g. GO=/usr/local/bin/go
GO?=		go
SKIP_GO?=	0
# Upstream go.mod may require a newer Go than pfSense ships; patch the `go` line
# to this host's version and use GOTOOLCHAIN=local to avoid auto-downloading a
# toolchain (which can SIGSEGV on some 15-CURRENT/pfSense setups). Set to 0 to disable.
AMNEZIAWG_PATCH_GOMOD?=	1

.PHONY: all clean fetch build-go stage pkg pkg-repo install

all: pkg

fetch:
	@if [ ! -d "${AMNEZIAWG_SRC}/.git" ]; then \
		mkdir -p "${.CURDIR}/work"; \
		git clone --depth 1 --branch "${AMNEZIAWG_TAG}" "${AMNEZIAWG_REPO}" "${AMNEZIAWG_SRC}" 2>/dev/null || \
		( rm -rf "${AMNEZIAWG_SRC}" && git clone --depth 1 "${AMNEZIAWG_REPO}" "${AMNEZIAWG_SRC}" && \
		  git -C "${AMNEZIAWG_SRC}" fetch --depth 1 origin "refs/tags/${AMNEZIAWG_TAG}:refs/tags/${AMNEZIAWG_TAG}" 2>/dev/null || true; \
		  git -C "${AMNEZIAWG_SRC}" checkout "${AMNEZIAWG_TAG}" 2>/dev/null || true ); \
	fi

build-go: fetch
	@if [ "${SKIP_GO}" = "1" ]; then \
		mkdir -p "${.CURDIR}/files/usr/local/bin"; \
		printf '%s\n' '#!/bin/sh' 'echo "amneziawg-go was not built (SKIP_GO=1); run make build-go" >&2' 'exit 1' \
			> "${.CURDIR}/files/usr/local/bin/amneziawg-go"; \
		chmod 0555 "${.CURDIR}/files/usr/local/bin/amneziawg-go"; \
	else \
		PATH="/usr/local/bin:/usr/local/sbin:/usr/bin:/bin:/sbin:$$PATH"; \
		export PATH; \
		REALGO=""; \
		GROOT=$$(ls -d /usr/local/go[0-9]* 2>/dev/null | sort -V | tail -1); \
		if [ -n "$$GROOT" ] && [ -x "$$GROOT/bin/go" ]; then \
			REALGO="$$GROOT/bin/go"; \
			export GOROOT="$$GROOT"; \
		fi; \
		if [ -z "$$REALGO" ]; then \
			for G in /usr/local/bin/go /usr/local/sbin/go /usr/local/go/bin/go; do \
				if [ -x "$$G" ]; then REALGO="$$G"; break; fi; \
			done; \
		fi; \
		if [ -z "$$REALGO" ] && command -v "${GO}" >/dev/null 2>&1; then \
			REALGO=$$(command -v "${GO}"); \
		fi; \
		if [ -n "$$REALGO" ] && [ -z "$$GOROOT" ]; then \
			GROOT=$$(ls -d /usr/local/go[0-9]* 2>/dev/null | sort -V | tail -1); \
			if [ -n "$$GROOT" ] && [ -d "$$GROOT/src" ]; then export GOROOT="$$GROOT"; fi; \
		fi; \
		if [ -z "$$REALGO" ]; then \
			echo ""; \
			echo "===> Go not found (needed to compile amneziawg-go)."; \
			echo "     On pfSense install a versioned package, e.g.:"; \
			echo "       pkg search -x '^go[0-9]'   # then: pkg install -y go123"; \
			echo "     Go lives under /usr/local/go123/bin/go (not /usr/local/bin/go)."; \
			echo "     Or: make GO=/usr/local/go123/bin/go clean pkg"; \
			echo "     Or: make SKIP_GO=1 clean pkg   (stub binary)"; \
			echo ""; \
			exit 1; \
		fi; \
		mkdir -p "${.CURDIR}/files/usr/local/bin"; \
		echo "===> Building amneziawg-go with $$REALGO (GOROOT=$$GOROOT)"; \
		cd "${AMNEZIAWG_SRC}" || exit 1; \
		if [ "$(AMNEZIAWG_PATCH_GOMOD)" != "0" ]; then \
			NUMVER=$$("$$REALGO" env GOVERSION | sed 's/^go//'); \
			if [ -n "$$NUMVER" ]; then \
				echo "===> Aligning go.mod language version to host: go $$NUMVER (was newer upstream)"; \
				cp -f go.mod go.mod.pfsense.bak; \
				sed -i '' "s/^go .*/go $$NUMVER/" go.mod; \
			fi; \
		fi; \
		export GOTOOLCHAIN=local; \
		env CGO_ENABLED=0 GOOS=freebsd GOARCH=amd64 \
			"$$REALGO" build -trimpath -ldflags="-s -w" \
			-o "${.CURDIR}/files/usr/local/bin/amneziawg-go" . ; \
	fi

clean:
	rm -rf "${STAGEDIR}" "${DISTDIR}" "${REPODIR}"

stage: build-go
	rm -rf "${STAGEDIR}"
	mkdir -p "${STAGEDIR}"
	rsync -a "${.CURDIR}/files/" "${STAGEDIR}/"
	cp "${.CURDIR}/pkg/+MANIFEST" "${STAGEDIR}/+MANIFEST"
	cp "${.CURDIR}/pkg/+INSTALL" "${STAGEDIR}/+INSTALL"
	cp "${.CURDIR}/pkg/+DEINSTALL" "${STAGEDIR}/+DEINSTALL"

pkg: stage
	mkdir -p "${DISTDIR}"
	cd "${STAGEDIR}" && pkg create -r . -m ./+MANIFEST -o "${DISTDIR}"
	@echo "Package: $$(ls -1t "${DISTDIR}"/*.txz 2>/dev/null | head -1)"

pkg-repo: pkg
	mkdir -p "${REPODIR}/All" "${REPODIR}/Latest"
	cp "${DISTDIR}/"*.txz "${REPODIR}/All/" 2>/dev/null || true
	cd "${REPODIR}" && pkg repo .
	@echo "Repository metadata in ${REPODIR} (publish All/, Latest/, packagesite.*)"

install: stage
	rsync -a "${STAGEDIR}/" /
