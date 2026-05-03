# pfSense package: AmneziaWG (`pfSense-pkg-amneziawg`)

This repository builds an installable **FreeBSD pkg** for pfSense 2.8.x that runs the userspace daemon [**amneziawg-go**](https://github.com/amnezia-vpn/amneziawg-go), exposes a **VPN → AmneziaWG** web UI (settings, status, logs), and can add a **dynamic IPv4 gateway** once the tunnel interface is assigned under **Interfaces → Assignments**.

## Important technical notes

- **Homelab Go version:** this tree documents a pinned pfSense toolchain in **`TOOLCHAIN-PIN.txt`** (currently **Go 1.26.2** as **`go1.26.2.freebsd-amd64.tar.gz`** under **`/usr/local/go124/`**). Update that file when you change versions.
- **Configuration:** `amneziawg-go` does **not** read a JSON file at startup. It only accepts the **interface name** on the command line (`amneziawg-go -f amnezia0`) and is configured through the **WireGuard UAPI** on a Unix socket (`/var/run/amneziawg/<ifname>.sock`). This package stores your settings in **config.xml**, mirrors them to **`/usr/local/etc/amneziawg/config.json`** (documentation / interchange), generates a **UAPI `set` payload** in **`/usr/local/etc/amneziawg/uapi.conf`**, and applies it after the daemon starts (PHP over the Unix socket).
- **Obfuscation:** `Jc`, `Jmin`, `Jmax`, `S1`–`S4`, `H1`–`H4`, `I1`–`I5` are **interface-wide** in `amneziawg-go` (shared by all peers on that device).
- **Private keys:** Stored in **plain text in config.xml**, same convention as the built-in WireGuard package.

## Prerequisites

- **Make:** The `Makefile` uses **`TOP=$(CURDIR)${.CURDIR}`** so it works with **GNU Make** (Linux: `make`) and **BSD Make** (pfSense/FreeBSD: `make`). Plain **`${.CURDIR}`** alone breaks on GNU Make because **`$(.CURDIR)` is empty**, which turned paths like `${.CURDIR}/work` into **`/work`**.
- **Build host:** FreeBSD **amd64** (pfSense 2.8 / FreeBSD 15-CURRENT is fine) with:
  - **Go** (1.22+ recommended). On pfSense, Go is not installed by default. The **FreeBSD `lang/go` metaport is often missing** from pfSense package feeds; use the **versioned** package instead (name changes over time):
    ```sh
    pkg search -x '^go[0-9]'
    pkg install -y go123
    ```
    The **go123** package installs the toolchain under **`/usr/local/go123/`** (compiler is **`/usr/local/go123/bin/go`**). There is often **no** `/usr/local/bin/go` unless you add a symlink yourself. **`scripts/build-amneziawg-go.sh`** picks the newest **`/usr/local/go*/bin/go`**, sets **`GOROOT`**, and prepends **`/usr/local/bin`** to **`PATH`** for the build.

    **Toolchain / SIGSEGV:** upstream `amneziawg-go` may declare **`go 1.24.x`** in `go.mod`. Older `go123` (1.23.x) then tries to **download** `go1.24.x`, which can crash on some pfSense/15-CURRENT kernels. The helper **`scripts/build-amneziawg-go.sh`** drops **`toolchain`** lines from **`go.mod`**. For builds **without** vendored deps it also lowers the **`go`** line to the host version and uses **`GOTOOLCHAIN=local`**. When **`third_party/amneziawg-go/vendor`** exists it **does not** lower the **`go`** line (so **`GOTOOLCHAIN=auto`** can still install a **1.24+** toolchain while **`go build -mod=vendor`** avoids the module proxy). It sets **`GOWORK=off`**, **`GOMAXPROCS=1`**, **`GODEBUG=asyncpreemptoff=1`**, **`go build -p 1`**, and picks the **newest** **`/usr/local/go*/bin/go`**. Override with **`AMNEZIAWG_GOTOOLCHAIN=local`** if toolchain download crashes. To skip patching: `make AMNEZIAWG_PATCH_GOMOD=0 clean pkg`.

    **Newer Go on pfSense (optional):** if you prefer not to rely on **`GOTOOLCHAIN=auto`** downloads from **`go.dev`**, install another versioned package, e.g. **`pkg install -y go124`** (run **`pkg search -x '^go[0-9]'`** on the firewall; names track FreeBSD). That typically installs **`/usr/local/go124/bin/go`**. The build script uses the **highest-version** **`/usr/local/go*`** tree. Enabling the [FreeBSD package repo](https://docs.netgate.com/pfsense/en/latest/recipes/freebsd-pkg-repo.html) may be required if **`go124`** is not in the default Netgate feed. Alternatively fetch a concrete tarball from [https://go.dev/dl/](https://go.dev/dl/) (example this homelab: **`go1.26.2.freebsd-amd64.tar.gz`** → **`tar -C /usr/local -xzf go1.26.2.freebsd-amd64.tar.gz`** then **`mv /usr/local/go /usr/local/go124`**). Use the **exact** filename you downloaded, not a placeholder. The canonical pin for this tree is **`TOOLCHAIN-PIN.txt`** in the repo root.

    **SIGSEGV while downloading modules** (after `go.mod` is aligned): some hosts still crash in **`runtime.netpoll`** during **`go build`**. Use an **offline vendor tree**: on any machine with working Go, from this repo run **`make vendor-export`** (or follow **`third_party/README.md`**), copy the repo to pfSense, then **`make clean pkg`**. The build script uses **`go build -mod=vendor`** (no module proxy). Alternatively **`make SKIP_GO=1 clean pkg`** and install a **`amneziawg-go`** binary built elsewhere under **`/usr/local/bin/`** before staging.
  - **`git`**, **`pkg`** (staging uses **`cp -a`**, not **`rsync`**, so pfSense minimal images work)
- **Runtime:** pfSense with PHP (as shipped), optional **`wg`** from **wireguard-tools** for the **Generate** private-key button (`wg genkey`).

## Build the package

**`pkg create` payload:** when using **`-m`** (metadata dir) and **`-r`** (staging root), **`pkg(8)`** only packs staged files if you also pass **`-p`** with a plist, or list each path in **`+MANIFEST`** as `file <sha256> <path>`. This **`Makefile`** writes **`work/pkgmeta/+PLIST`** from the staged tree, then runs **`pkg create … -p …`**. Builds that skipped **`-p`** produced a package **`pkg add`** registered in the database but **installed no files** (for example **`ls /usr/local/share/pfSense-pkg-amneziawg/`** missing). After pulling the fix, run **`make clean pkg`** and reinstall.

After **`git pull`**, always **`make clean pkg`** on the firewall before **`pkg add`**. Otherwise **`work/dist/`** can still hold an **older** archive (e.g. lowercase **`pfsense-pkg-*`**) while **`pkg add`** for **`pfSense-pkg-*`** fails with “No such file or directory” — and installing the stale file skips GUI registration.

```sh
cd pfsense-amneziawg-package
# Optional: pin a tag
# make AMNEZIAWG_TAG=v0.x.y
make clean pkg
ls work/dist/
```

Output: **`work/dist/pfSense-pkg-amneziawg-0.2.pkg`** (or **`.txz`**) — version tracks **`Makefile` `VERSION`** and **`pkg/+MANIFEST`**. Only install the **`pfSense-pkg-`** file **`ls`** shows after this build.

To build **without** compiling Go (placeholder binary that exits with an error):

```sh
make SKIP_GO=1 clean pkg
```

### Custom repository (GitHub Pages / static host)

```sh
make pkg-repo
```

Upload the contents of `work/repo/` (including `packagesite.yaml` / `packagesite.txz` generated by `pkg repo`).

On pfSense, add your repo URL under **System → Package Manager** (or `pkg` configuration) per Netgate documentation for third-party repositories.

## Install on pfSense

Copy the package file (`.pkg` or `.txz`) to the firewall, then:

```sh
ls work/dist/
pkg add --force ./work/dist/pfSense-pkg-amneziawg-0.2.pkg
```

**Why `pkg add` alone used to hide the VPN menu:** pfSense only adds **VPN / Package Manager** entries when **`install_package_xml()`** updates **`config.xml`**. Plain **`pkg add`** installs files but skips that step. This package’s **`+POST_INSTALL`** runs **`amneziawg_register.php`** (via **`fcgicli`** or **`php`**) so the **AmneziaWG** item appears under **VPN** like a GUI-installed package. The menu **`<section>`** in **`amneziawg.xml`** must match the capitalized name **`VPN`** (see **`return_ext_menu("VPN")`** in **`head.inc`**); a lowercase **`vpn`** entry is stored in **`config.xml`** but never shown in the sidebar.

If you installed an **older build** named **`pfsense-pkg-amneziawg`** (lowercase), remove it before installing this one: **`pkg delete -y pfsense-pkg-amneziawg`**, then **`pkg add`** the new **`.pkg`**. On an already-installed tree without re-running post-install, run once: **`/usr/local/bin/php -f /usr/local/share/pfSense-pkg-amneziawg/amneziawg_register.php`**.

Reload the GUI if the **VPN → AmneziaWG** menu does not appear immediately (some images need a refresh or **`/etc/rc.reload_all`**).

## Operation

1. **VPN → AmneziaWG → Settings:** enter keys, peers, optional obfuscation fields, tunnel CIDR, and enable the tunnel.
2. **Interfaces → Assignments:** assign the TUN interface (e.g. `amnezia0`) to an OPT interface after the service has started once so the interface exists.
3. **System → Routing → Gateways:** if you enabled **Auto gateway**, a dynamic IPv4 gateway appears after assignment (monitor IP optional).
4. Use **policy routing** (firewall rules / aliases / pfBlockerNG) to send selected traffic out that gateway.

**Service control:** `/usr/local/etc/rc.d/amneziawg {start|stop|restart|status|reload}`  
Boot integration uses `amneziawg_enable` in `rc.conf` (set by the **`+POST_INSTALL`** script) and a local **`/usr/local/etc/amneziawg/enabled`** flag (`1`/`0`) written from the GUI so the tunnel **Enable** switch does not fight `sysrc` on every change.

## Files installed

| Path | Role |
|------|------|
| `/usr/local/bin/amneziawg-go` | Daemon binary |
| `/usr/local/etc/rc.d/amneziawg` | rc script |
| `/usr/local/etc/amneziawg/` | `config.json`, `uapi.conf`, `enabled`, `interface.txt`, `log_level`, sample |
| `/usr/local/share/pfSense-pkg-amneziawg/` | `info.xml`, `amneziawg.xml` (menu / service) |
| `/usr/local/www/pkg/amneziawg/` | PHP UI + `amneziawg.inc` |

## License

PHP and packaging scripts in this repository are provided under **Apache-2.0** unless you choose another license for your fork. `amneziawg-go` remains under its upstream license.
