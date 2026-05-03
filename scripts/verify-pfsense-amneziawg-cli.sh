#!/bin/sh
# Run on pfSense as root. Checks pkg(8), files on disk, and config.xml registration
# (System > Package Manager reads installedpackages/package; VPN menu reads installedpackages/menu).

set +e
PATH=/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin
export PATH

echo "=== 1) pkg(8) database ==="
pkg info -x 'pfSense-pkg-amneziawg' 2>/dev/null || echo "(none matching pfSense-pkg-amneziawg)"
pkg info -x 'pfsense-pkg-amneziawg' 2>/dev/null || true

echo ""
echo "=== 1b) Files recorded for this package (empty = broken/empty .pkg build) ==="
if pkg info pfSense-pkg-amneziawg >/dev/null 2>&1; then
	n=$(pkg info -l pfSense-pkg-amneziawg 2>/dev/null | wc -l)
	echo "pkg info -l line count: $n"
	pkg info -l pfSense-pkg-amneziawg 2>/dev/null | head -25
else
	echo "(pfSense-pkg-amneziawg not installed)"
fi

echo ""
echo "=== 2) Files on disk (package payload) ==="
for p in \
	/usr/local/share/pfSense-pkg-amneziawg/info.xml \
	/usr/local/share/pfSense-pkg-amneziawg/amneziawg_register.php \
	/usr/local/pkg/amneziawg.xml \
	/usr/local/www/pkg/amneziawg/vpn_amneziawg.php \
	/usr/local/www/pkg/amneziawg/amneziawg.inc \
	/usr/local/bin/amneziawg-go; do
	if [ -e "$p" ]; then
		ls -la "$p"
	else
		echo "MISSING: $p"
	fi
done

echo ""
echo "=== 3) Menu <section> in staged package XML (must be exactly VPN) ==="
if [ -f /usr/local/pkg/amneziawg.xml ]; then
	grep -E '<section>|</section>' /usr/local/pkg/amneziawg.xml || true
else
	echo "(no /usr/local/pkg/amneziawg.xml)"
fi

echo ""
echo "=== 4) config.xml registration (what the GUI uses) ==="
/usr/local/bin/php <<'PHPEOF'
<?php
require_once('/etc/inc/globals.inc');
require_once('/etc/inc/config.inc');
require_once('/etc/inc/pkg-utils.inc');

function yn(bool $b): string {
	return $b ? 'yes' : 'no';
}

$cfgpath = g_get('cf_conf_path') . '/config.xml';
echo "config.xml path: {$cfgpath}\n";
echo "config readable: " . (is_readable($cfgpath) ? 'yes' : 'no') . "\n\n";

$packages = config_get_path('installedpackages/package', []);
$found = [];
foreach ($packages as $i => $p) {
	$n = (string)($p['name'] ?? '');
	$in = (string)($p['internal_name'] ?? '');
	if (stripos($n, 'amnezia') !== false || stripos($in, 'amnezia') !== false) {
		$found[] = sprintf(
			'  package[%s] name=%s internal_name=%s configurationfile=%s',
			$i,
			json_encode($n),
			json_encode($in),
			json_encode($p['configurationfile'] ?? '')
		);
	}
}
if ($found === []) {
	echo "installedpackages/package: NO AmneziaWG row — GUI Package Manager will not list it.\n";
} else {
	echo "installedpackages/package:\n" . implode("\n", $found) . "\n";
}

$menus = config_get_path('installedpackages/menu', []);
$mf = [];
foreach ($menus as $i => $m) {
	$u = (string)($m['url'] ?? '');
	if (str_contains($u, '/pkg/amneziawg/')) {
		$mf[] = sprintf(
			'  menu[%s] name=%s section=%s url=%s',
			$i,
			json_encode((string)($m['name'] ?? '')),
			json_encode((string)($m['section'] ?? '')),
			json_encode($u)
		);
	}
}
if ($mf === []) {
	echo "\ninstalledpackages/menu: NO AmneziaWG URL — VPN top menu will not show AmneziaWG.\n";
} else {
	echo "\ninstalledpackages/menu:\n" . implode("\n", $mf) . "\n";
}

echo "\nis_package_installed('amneziawg'): " . yn(is_package_installed('amneziawg')) . "\n";
echo "get_package_id('amneziawg'): " . get_package_id('amneziawg') . "\n";
PHPEOF

echo ""
echo "=== 5) Optional: run register script (exit 0 = install_package_xml ok) ==="
REG=/usr/local/share/pfSense-pkg-amneziawg/amneziawg_register.php
if [ -f "$REG" ]; then
	/usr/local/bin/php -f "$REG"
	echo "amneziawg_register.php exit code: $?"
else
	echo "Skip: $REG not found"
fi

echo ""
echo "=== 6) Raw grep (optional; large file) ==="
CFG=$(/usr/local/bin/php -r 'require_once("/etc/inc/globals.inc"); echo g_get("cf_conf_path") . "/config.xml";' 2>/dev/null)
if [ -n "$CFG" ] && [ -f "$CFG" ]; then
	grep -n 'amneziawg\|AmneziaWG' "$CFG" | head -40 || echo "(no matches)"
else
	echo "Could not resolve config path."
fi

echo ""
echo "Note: AmneziaWG is under VPN -> AmneziaWG, not on the OpenVPN client/server pages."
