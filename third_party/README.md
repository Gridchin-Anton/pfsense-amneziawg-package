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

`scripts/build-amneziawg-go.sh` detects `third_party/amneziawg-go/vendor` and runs **`go build -mod=vendor`**, which avoids the module proxy download path that triggers the crash on your kernel.
