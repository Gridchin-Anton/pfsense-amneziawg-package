# Offline Go modules (optional)

Some pfSense / FreeBSD **15-CURRENT** systems crash (`SIGSEGV` in `runtime.netpoll`) while **`go build`** downloads modules from the network, even after the `go.mod` toolchain workaround.

If `make clean pkg` still dies during `go: downloading …`, populate **`vendor/`** here on **any machine with a working Go** (same module versions as your `AMNEZIAWG_TAG` checkout), then rebuild on the firewall **without** internet module access:

```sh
# On a host with Go 1.23+ and git:
VER=master   # or a tag matching AMNEZIAWG_TAG in the top Makefile
git clone --depth 1 --branch "$VER" https://github.com/amnezia-vpn/amneziawg-go.git /tmp/amneziawg-go
cd /tmp/amneziawg-go
go mod vendor
mkdir -p /path/to/pfsense-amneziawg-package/third_party/amneziawg-go
rm -rf /path/to/pfsense-amneziawg-package/third_party/amneziawg-go/vendor
cp -a vendor /path/to/pfsense-amneziawg-package/third_party/amneziawg-go/
```

Copy the updated `pfsense-amneziawg-package` tree to pfSense (or commit `third_party/amneziawg-go/vendor` to your fork), then:

```sh
make clean pkg
```

`scripts/build-amneziawg-go.sh` detects `third_party/amneziawg-go/vendor` and runs **`go build -mod=vendor`** (no module proxy). For vendored builds it **does not** rewrite the main **`go`** line in **`go.mod`** to the host **1.23.x** (that prevented **`GOTOOLCHAIN=auto`** from selecting **1.24**). It strips **`toolchain`** lines only, then uses **`GOTOOLCHAIN=auto`** so a **go123** driver can fetch a **1.24+** toolchain while modules still come from **`vendor/`**. If that download fails or crashes, install **`go124`** (or unpack **`go*.freebsd-amd64.tar.gz`** under **`/usr/local/go124/`**) so the newest **`/usr/local/go*`** is already **1.24+**, or set **`AMNEZIAWG_GOTOOLCHAIN=local`** only when you have a matching system Go.
