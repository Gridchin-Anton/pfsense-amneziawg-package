<?php
/*
 * CLI helper for rc.d: apply UAPI and configure TUN address (no web session).
 */
require_once("config.inc");
require_once("util.inc");
require_once("/usr/local/www/pkg/amneziawg/amneziawg.inc");

$op = $argv[1] ?? '';

if ($op === 'apply') {
	exit(amneziawg_rc_apply());
}
if ($op === 'ifconfig') {
	exit(amneziawg_rc_ifconfig());
}

fwrite(STDERR, "usage: amneziawg_rc.php apply|ifconfig\n");
exit(64);
