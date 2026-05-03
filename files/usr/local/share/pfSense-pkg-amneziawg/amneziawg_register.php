#!/usr/local/bin/php
<?php
/*
 * Register AmneziaWG in config.xml and GUI menus after `pkg add`.
 * Plain pkg(8) does not run the same hooks as System > Package Manager.
 */
require_once('/etc/inc/globals.inc');
require_once('/etc/inc/config.inc');
require_once('/etc/inc/pkg-utils.inc');

/*
 * head.inc only attaches package menus where $menu['section'] == "VPN" (capitalized).
 * Drop stale rows (e.g. section "vpn") so install_package_xml can register a visible item.
 */
$menus = config_get_path('installedpackages/menu', []);
$filtered = [];
$stripped = false;
foreach ($menus as $m) {
	if (isset($m['name'], $m['url']) && trim($m['name']) === 'AmneziaWG' &&
	    str_contains($m['url'], '/pkg/amneziawg/') &&
	    (($m['section'] ?? '') !== 'VPN')) {
		$stripped = true;
		continue;
	}
	$filtered[] = $m;
}
if ($stripped) {
	config_set_path('installedpackages/menu', $filtered);
	write_config('AmneziaWG: removed stale package menu entry (wrong section case)');
}

if (install_package_xml('amneziawg')) {
	exit(0);
}
fwrite(STDERR, "amneziawg_register.php: install_package_xml failed\n");
exit(1);
