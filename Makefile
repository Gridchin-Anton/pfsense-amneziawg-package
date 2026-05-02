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
		mkdir -p "${.CURDIR}/files/usr/local/bin"; \
		chmod 755 "${.CURDIR}/scripts/build-amneziawg-go.sh"; \
		sh "${.CURDIR}/scripts/build-amneziawg-go.sh" \
			"${AMNEZIAWG_SRC}" "${.CURDIR}/files/usr/local/bin/amneziawg-go" "$(AMNEZIAWG_PATCH_GOMOD)"; \
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
