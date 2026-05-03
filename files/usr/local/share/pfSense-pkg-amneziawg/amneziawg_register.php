#!/usr/local/bin/php
<?php
/*
 * Register AmneziaWG in config.xml and GUI menus after `pkg add`.
 * Plain pkg(8) does not run the same hooks as System > Package Manager.
 */
require_once('/etc/inc/globals.inc');
require_once('/etc/inc/config.inc');
require_once('/etc/inc/pkg-utils.inc');

if (is_package_installed('amneziawg')) {
	exit(0);
}
if (install_package_xml('amneziawg')) {
	exit(0);
}
fwrite(STDERR, "amneziawg_register.php: install_package_xml failed\n");
exit(1);
