# Build pfsense-pkg-amneziawg: amneziawg-go (FreeBSD/amd64) + pkg txz
# Run on FreeBSD amd64 with Go 1.22+ and pkg installed.
# Homelab pfSense: Go 1.26.2 from go.dev tarball → /usr/local/go124/ (see TOOLCHAIN-PIN.txt).

VERSION=	0.1
PKGNAME=	pfsense-pkg-amneziawg
# GNU Make sets $(CURDIR) and treats $(.CURDIR) as empty, so ${.CURDIR}/foo becomes /foo.
# BSD Make sets ${.CURDIR} and leaves $(CURDIR) empty. Concatenation yields the repo root on both.
TOP=	$(CURDIR)${.CURDIR}
STAGEDIR=	${TOP}/work/stage
PKGMETA=	${TOP}/work/pkgmeta
DISTDIR=	${TOP}/work/dist
REPODIR=	${TOP}/work/repo

# amneziawg-go source (override tag or point AMNEZIAWG_SRC to a checkout)
AMNEZIAWG_REPO?=	https://github.com/amnezia-vpn/amneziawg-go
AMNEZIAWG_TAG?=	master
AMNEZIAWG_SRC?=	${TOP}/work/amneziawg-go

SKIP_GO?=	0
# Upstream go.mod may require a newer Go than pfSense ships; patch the `go` line
# to this host's version and use GOTOOLCHAIN=local to avoid auto-downloading a
# toolchain (which can SIGSEGV on some 15-CURRENT/pfSense setups). Set to 0 to disable.
AMNEZIAWG_PATCH_GOMOD?=	1

.PHONY: all clean fetch build-go vendor-export stage pkg pkg-repo install

all: pkg

fetch:
	@if [ ! -d "${AMNEZIAWG_SRC}/.git" ]; then \
		mkdir -p "${TOP}/work"; \
		git clone --depth 1 --branch "${AMNEZIAWG_TAG}" "${AMNEZIAWG_REPO}" "${AMNEZIAWG_SRC}" 2>/dev/null || \
		( rm -rf "${AMNEZIAWG_SRC}" && git clone --depth 1 "${AMNEZIAWG_REPO}" "${AMNEZIAWG_SRC}" && \
		  git -C "${AMNEZIAWG_SRC}" fetch --depth 1 origin "refs/tags/${AMNEZIAWG_TAG}:refs/tags/${AMNEZIAWG_TAG}" 2>/dev/null || true; \
		  git -C "${AMNEZIAWG_SRC}" checkout "${AMNEZIAWG_TAG}" 2>/dev/null || true ); \
	fi

build-go: fetch
	@if [ "${SKIP_GO}" = "1" ]; then \
		mkdir -p "${TOP}/files/usr/local/bin"; \
		printf '%s\n' '#!/bin/sh' 'echo "amneziawg-go was not built (SKIP_GO=1); run make build-go" >&2' 'exit 1' \
			> "${TOP}/files/usr/local/bin/amneziawg-go"; \
		chmod 0555 "${TOP}/files/usr/local/bin/amneziawg-go"; \
	else \
		mkdir -p "${TOP}/files/usr/local/bin"; \
		chmod 755 "${TOP}/scripts/build-amneziawg-go.sh"; \
		sh "${TOP}/scripts/build-amneziawg-go.sh" \
			"${AMNEZIAWG_SRC}" "${TOP}/files/usr/local/bin/amneziawg-go" "$(AMNEZIAWG_PATCH_GOMOD)"; \
	fi

# Run on a machine with a working Go (Linux/macOS/FreeBSD): copies vendored deps
# into third_party/amneziawg-go/vendor for offline pfSense builds (see third_party/README.md).
vendor-export: fetch
	cd "${AMNEZIAWG_SRC}" && go mod vendor
	mkdir -p "${TOP}/third_party/amneziawg-go"
	rm -rf "${TOP}/third_party/amneziawg-go/vendor"
	cp -a "${AMNEZIAWG_SRC}/vendor" "${TOP}/third_party/amneziawg-go/vendor"
	rm -rf "${AMNEZIAWG_SRC}/vendor"
	@echo "===> third_party/amneziawg-go/vendor ready (copy repo to pfSense or commit)"

clean:
	rm -rf "${STAGEDIR}" "${PKGMETA}" "${DISTDIR}" "${REPODIR}"

stage: build-go
	rm -rf "${STAGEDIR}" "${PKGMETA}"
	mkdir -p "${STAGEDIR}" "${PKGMETA}"
	cp -a "${TOP}/files/." "${STAGEDIR}/"
	cp "${TOP}/pkg/+MANIFEST" "${PKGMETA}/+MANIFEST"
	cp "${TOP}/pkg/+POST_INSTALL" "${PKGMETA}/+POST_INSTALL"
	cp "${TOP}/pkg/+POST_DEINSTALL" "${PKGMETA}/+POST_DEINSTALL"

pkg: stage
	mkdir -p "${DISTDIR}"
	pkg create -r "${STAGEDIR}" -m "${PKGMETA}" -o "${DISTDIR}"
	@echo "Package: `ls -1t ${DISTDIR}/pfsense-pkg-*.* 2>/dev/null | head -1`"

pkg-repo: pkg
	mkdir -p "${REPODIR}/All" "${REPODIR}/Latest"
	cp "${DISTDIR}"/pfsense-pkg-*.pkg "${REPODIR}/All/" 2>/dev/null || true
	cp "${DISTDIR}"/pfsense-pkg-*.txz "${REPODIR}/All/" 2>/dev/null || true
	cd "${REPODIR}" && pkg repo .
	@echo "Repository metadata in ${REPODIR} (publish All/, Latest/, packagesite.*)"

install: stage
	cp -a "${STAGEDIR}/." /
