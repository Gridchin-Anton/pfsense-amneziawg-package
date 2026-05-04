# AmneziaWG on pfSense — end-to-end test runbook

This document describes how to install, verify, and **prove** that `pfSense-pkg-amneziawg` behaves as intended: daemon up, UAPI applied, handshake, optional policy routing, and (if you need it) a nested WireGuard client test.

**Assumptions:** pfSense 2.8.x amd64, root SSH or console, a valid Amnezia profile (keys, endpoint, obfuscation fields if any). Replace example IPs, interface names, and keys with yours.

**Shell note:** Default root shell on pfSense is **tcsh**. Bash-style `${VAR:-default}`, `!$`, and unescaped `!` in double-quoted strings break. Prefer **`/bin/sh -c '...'`** for portable snippets, or run commands from **Diagnostics → Command Prompt** (PHP) where noted.

---

## Phase 0 — Prerequisites

1. **Fresh or known-good config** — reduces stale `tunnel` vs `awgcfg` confusion and broken XML from older package builds.
2. **Package build/install** — from this repo: `make clean pkg`, copy `work/dist/pfSense-pkg-amneziawg-*.pkg` to the firewall, then:
   ```sh
   pkg add --force ./pfSense-pkg-amneziawg-*.pkg
   ```
3. **GUI registration** (if VPN menu missing after `pkg add`):
   ```sh
   /usr/local/bin/php -f /usr/local/share/pfSense-pkg-amneziawg/amneziawg_register.php
   /etc/rc.reload_all
   ```
4. **Remove wrong package name** if you ever installed lowercase `pfsense-pkg-amneziawg`:
   ```sh
   pkg delete -y pfsense-pkg-amneziawg 2>/dev/null; true
   ```

---

## Phase 1 — Package integrity (automated)

Copy `scripts/verify-pfsense-amneziawg-cli.sh` to the firewall and run:

```sh
sh /path/to/verify-pfsense-amneziawg-cli.sh
```

**Pass criteria:**

- `pkg info` shows `pfSense-pkg-amneziawg`.
- Files exist: `/usr/local/bin/amneziawg-go`, `/usr/local/www/pkg/amneziawg/amneziawg.inc`, `/usr/local/pkg/amneziawg.xml`, menu section is **`VPN`** (capitalized).
- PHP block reports `is_package_installed('amneziawg'): yes` and a menu row pointing at `/pkg/amneziawg/`.

---

## Phase 2 — Enable service and local flags

```sh
sysrc amneziawg_enable=YES
printf '1\n' > /usr/local/etc/amneziawg/enabled
printf 'amnezia0\n' > /usr/local/etc/amneziawg/interface.txt
```

Optional log verbosity (rc.d exports this when starting the daemon):

```sh
printf 'error\n' > /usr/local/etc/amneziawg/log_level
```

**About the “first class support for AmneziaWG” banner:** `amneziawg-go` prints an informational banner on FreeBSD; userspace is still required. It is harmless noise. To reduce foreground noise when testing interactively: `export WG_PROCESS_FOREGROUND=1` before running the binary by hand (not required for normal rc.d use).

---

## Phase 3 — Configuration paths (what must be true)

| Concern | Intended behavior |
|--------|---------------------|
| **config.xml storage** | Tunnel data lives under **`amneziawg/awgcfg`** (not a bare `tunnel` key that collides with pfSense listtags). XML-unsafe fields (e.g. obfuscation `I1` with `<`…`>`) are stored as **`b64:`** + base64 in current package code. |
| **UAPI file** | `/usr/local/etc/amneziawg/uapi.conf` must be **Unix LF** lines, **no UTF-8 BOM**. CRLF/BOM corrupts the first key (symptom: `invalid UAPI device key: ce_peers`). |
| **ifconfig order (FreeBSD)** | Address family before MTU: `ifconfig amnezia0 inet <addr>/32 mtu 1420 up` — not `mtu` before `inet`. The package’s apply path should generate the correct order. |
| **Listen port** | If you also run kernel WireGuard on 51820, set AmneziaWG **`listenport` to `0`** (ephemeral) in the profile to avoid UDP conflicts. |

---

## Phase 4 — GUI-first configuration (preferred)

1. **VPN → AmneziaWG → Settings:** paste **Amnezia native** / **vpn://** import or fill fields; enable tunnel; set address/CIDR, peer, keys, obfuscation, endpoint, keepalive.
2. **Save** — should **not** produce `XML_ERR_NAME_REQUIRED`. If it does, your installed `amneziawg.inc` may be older than the `awgcfg` + `b64:` fix; upgrade the package.
3. **Interfaces → Assignments:** assign **`amnezia0`** to an interface (often LAN or an OPT). Enable the assignment and set a **static IPv4** on that assignment if you will use **Outbound NAT → Interface address** or need a stable “parent” for rules/NAT.
4. **VPN → AmneziaWG:** enable **Auto gateway** when you want **System → Routing → Gateways** to show **`AMNEZIAWG_VPNV4`**. The sync logic expects **`enable`** and **`auto_gateway`** both on where applicable.
5. **Restart service:**
   ```sh
   /usr/local/etc/rc.d/amneziawg restart
   ```

---

## Phase 5 — CLI verification (no GUI)

Use this when validating protocol support before trusting the GUI, or after hand-editing files.

### 5a. Ensure JSON + UAPI exist (normally written by GUI/apply)

Paths under `/usr/local/etc/amneziawg/`:

- `config.json` — human-readable mirror (daemon does not read it at startup; UAPI does).
- `uapi.conf` — `set` payload for the socket.
- `interface.txt` — interface name (default `amnezia0`).
- `enabled` — `1` or `0`.

Normalize line endings once if you edited on Windows:

```sh
perl -pi -e 's/\r\n/\n/g; s/\r/\n/g' /usr/local/etc/amneziawg/uapi.conf
```

Lock down permissions:

```sh
chmod 600 /usr/local/etc/amneziawg/config.json /usr/local/etc/amneziawg/uapi.conf
```

### 5b. Start / status

```sh
/usr/local/etc/rc.d/amneziawg restart
/bin/sh -c 'IF=$(tr -d "\r\n" < /usr/local/etc/amneziawg/interface.txt 2>/dev/null); IF=${IF:-amnezia0}; /usr/local/etc/rc.d/amneziawg status; ls -la /var/run/amneziawg/${IF}.sock /var/run/amneziawg/${IF}.pid'
```

### 5c. UAPI dump (handshake and counters)

Run under **`/bin/sh -c`** so tcsh does not mangle quoting:

```sh
/bin/sh -c '/usr/local/bin/php -r '"'"'require_once("/etc/inc/config.inc"); require_once("/etc/inc/util.inc"); require_once("/usr/local/www/pkg/amneziawg/amneziawg.inc"); $ifn = trim((string)@file_get_contents("/usr/local/etc/amneziawg/interface.txt")) ?: "amnezia0"; $r = amneziawg_uapi_get($ifn, 10); if ($r["ok"] === false) { fwrite(STDERR, $r["err"]."\n"); exit(1);} echo $r["data"];'"'"''
```

**Pass criteria:**

- `last_handshake_time_sec` is **non-zero** after a short wait (and advances on activity).
- `tx_bytes` / `rx_bytes` increase when you generate traffic through the tunnel.

### 5d. Interface address

```sh
/bin/sh -c 'IF=$(tr -d "\r\n" < /usr/local/etc/amneziawg/interface.txt 2>/dev/null); IF=${IF:-amnezia0}; /sbin/ifconfig "$IF"'
```

You should see `inet` with your tunnel IP and `mtu` (commonly 1420). If `inet` is missing after restart, apply manually once to confirm path:

```sh
/sbin/ifconfig amnezia0 inet 10.8.1.6/32 mtu 1420 up
```

Then fix package/rc apply if your build still lacked the `inet`-before-`mtu` fix.

### 5e. Traffic sourced from tunnel IP (on the firewall itself)

Replace `10.8.1.6` with your tunnel address:

```sh
curl -4 --interface 10.8.1.6 -sS --max-time 15 https://api.ipify.org; echo
```

Compare to WAN-sourced check:

```sh
curl -4 -sS --max-time 15 https://api.ipify.org; echo
```

If handshake works but `--interface` curl fails, suspect **pfSense routing/NAT** for that source address, not AmneziaWG crypto.

### 5f. Re-apply UAPI and ifconfig from PHP (matches rc.d)

```sh
/usr/local/bin/php -f /usr/local/www/pkg/amneziawg/amneziawg_rc.php apply
/usr/local/bin/php -f /usr/local/www/pkg/amneziawg/amneziawg_rc.php ifconfig
```

---

## Phase 6 — Policy routing (optional but “full product” test)

1. Assign **`amnezia0`** to an interface; set **static IPv4** on that assignment if you use NAT “interface address”.
2. Enable **Auto gateway**; confirm **`AMNEZIAWG_VPNV4`** under **System → Routing → Gateways**. Set **Disable gateway monitoring** (empty monitor) if the UI flaps.
3. **Firewall → Rules** on the source interface (e.g. LAN or WireGuard OPT): pass traffic, **Advanced → Gateway** = `AMNEZIAWG_VPNV4`.
4. **Firewall → NAT → Outbound:** switch to **Hybrid**; add **Manual** rule: source = test subnet, interface = the assignment that hosts `amnezia0`, translation = **Interface IP** (tunnel IP).

Without outbound NAT, client subnets behind pfSense often **cannot** egress Amnezia’s remote side correctly.

---

## Phase 7 — Remote WireGuard client test (optional lab)

Use when the firewall has no LAN hosts: laptop → UDP **51820** (or your WG port) → pfSense WireGuard → policy route → Amnezia gateway.

1. **WAN rule:** allow **UDP** to `tun_wg0` listen port (e.g. 51820).
2. **WireGuard tunnel** on pfSense: server keys, **Allowed IPs** for peer include client `/32`.
3. **Firewall rule** on WG interface: source must match **real** WG subnet (e.g. `10.66.66.0/24`), gateway `AMNEZIAWG_VPNV4`.
4. **Hybrid Outbound NAT** for that WG subnet out the interface where `amnezia0` lives.

**Client (Linux) sketch:**

```sh
sudo wg-quick up ./wg0-client.conf
curl -4 -sS --max-time 20 https://api.ipify.org; echo
sudo wg-quick down ./wg0-client.conf
```

**Pass:** `wg show` shows received bytes increasing; `curl` returns Amnezia egress IP (or expected provider IP), not only the pfSense WAN.

---

## Phase 8 — `config.xml` spot checks (after save / restore)

```sh
grep -n 'amneziawg\|awgcfg\|AMNEZIAWG_VPNV4' /cf/conf/config.xml | head -60
```

Confirm:

- **`amneziawg/awgcfg`** holds tunnel fields; dangerous strings appear as **`b64:`** if applicable.
- No raw `<` inside XML text nodes for obfuscation blobs.
- Gateway block references **`AMNEZIAWG_VPNV4`** only after assignment + sync.

---

## Success criteria (checklist)

- [ ] `verify-pfsense-amneziawg-cli.sh` passes; VPN menu present.
- [ ] `amneziawg` rc.d **running**; `.sock` exists.
- [ ] UAPI shows **non-zero** `last_handshake_time_sec` and moving byte counters.
- [ ] `ifconfig` shows tunnel **`inet`** and correct **MTU**.
- [ ] Optional: `curl --interface <tunnel-ip>` works from the firewall.
- [ ] Optional: test host or WG client egresses via Amnezia with **firewall + hybrid NAT** correct.
- [ ] GUI save does **not** corrupt `config.xml` (no `XML_ERR_NAME_REQUIRED`).

---

## Troubleshooting quick map

| Symptom | Likely cause |
|--------|----------------|
| `XML_ERR_NAME_REQUIRED` | XML-unsafe characters in config, or legacy `tunnel` listtag collision — upgrade package; use `awgcfg` + `b64:` build. |
| `invalid UAPI device key: ce_peers` | BOM or CRLF on first line of `uapi.conf` — normalize to LF, strip BOM. |
| `ifconfig: inet: bad value` | **`mtu` before `inet`** — reorder command. |
| Handshake stays 0 | Wrong keys; blocked UDP to endpoint; listen port conflict; WAN firewall. |
| WG client handshake OK but no web | Wrong rule source subnet; missing **hybrid NAT**; parent interface has **no IPv4**; gateway not actually used on rule. |
| `curl --interface` timeout from firewall | Policy/NAT for that source IP on pfSense, or upstream blocking — isolate with `tcpdump` on correct WAN interface name (`ifconfig` / **Interfaces** page). |

---

## Security

If real **PrivateKey**, **PresharedKey**, or live **endpoint** were pasted into tickets, chats, or committed XML, **rotate** them with your VPN provider and treat the old material as compromised.
