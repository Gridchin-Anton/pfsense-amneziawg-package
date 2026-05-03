<?php
/*
 * amneziawg.inc — AmneziaWG pfSense package: config, JSON/UAPI, service, gateways
 * SPDX-License-Identifier: Apache-2.0
 */

require_once("config.inc");
require_once("util.inc");

define('AMNEZIAWG_CONFIG_PATH', '/usr/local/etc/amneziawg/config.json');
define('AMNEZIAWG_UAPI_PATH', '/usr/local/etc/amneziawg/uapi.conf');
define('AMNEZIAWG_RUN_DIR', '/var/run/amneziawg');
define('AMNEZIAWG_BINARY', '/usr/local/bin/amneziawg-go');
/*
 * Under config path amneziawg/, the tunnel block must NOT use the key name "tunnel":
 * pfSense xmlparse listtags() treats "tunnel" as a repeating list tag, so dump_xml_config
 * corrupts a normal associative tunnel array and config.xml round-trip loses settings.
 */
if (!defined('AMNEZIAWG_CFG_STORAGEKEY')) {
	define('AMNEZIAWG_CFG_STORAGEKEY', 'awgcfg');
}

/**
 * True if a string must not be written raw into config.xml (angle brackets, etc.).
 */
function amneziawg_storage_field_needs_xml_escape(string $v): bool
{
	return (bool)preg_match('/[<>&\x00-\x08\x0B\x0C\x0E-\x1F]/u', $v);
}

/**
 * Encode tunnel fields for XML storage (pfSense config.xml cannot hold raw `<` in text nodes).
 * Uses prefix `b64:` + base64 (UTF-8). I1–I5 from Amnezia are always encoded when non-empty.
 */
function amneziawg_storage_encode_tunnel(array $t): array
{
	$alwaysB64 = ['i1', 'i2', 'i3', 'i4', 'i5'];
	foreach ($alwaysB64 as $k) {
		if (!isset($t[$k]) || $t[$k] === '' || $t[$k] === null) {
			continue;
		}
		$v = (string)$t[$k];
		if (strncmp($v, 'b64:', 4) === 0) {
			continue;
		}
		$t[$k] = 'b64:' . base64_encode($v);
	}
	foreach (['h1', 'h2', 'h3', 'h4'] as $k) {
		if (!isset($t[$k]) || $t[$k] === '' || $t[$k] === null) {
			continue;
		}
		$v = (string)$t[$k];
		if (strncmp($v, 'b64:', 4) === 0) {
			continue;
		}
		if (amneziawg_storage_field_needs_xml_escape($v)) {
			$t[$k] = 'b64:' . base64_encode($v);
		}
	}
	if (isset($t['descr']) && $t['descr'] !== '' && $t['descr'] !== null) {
		$v = (string)$t['descr'];
		if (strncmp($v, 'b64:', 4) !== 0 && amneziawg_storage_field_needs_xml_escape($v)) {
			$t['descr'] = 'b64:' . base64_encode($v);
		}
	}
	return $t;
}

/**
 * Decode tunnel fields after reading from config.xml.
 */
function amneziawg_storage_decode_tunnel(array $t): array
{
	foreach (['i1', 'i2', 'i3', 'i4', 'i5', 'h1', 'h2', 'h3', 'h4', 'descr'] as $k) {
		if (!isset($t[$k]) || $t[$k] === '' || $t[$k] === null) {
			continue;
		}
		$v = (string)$t[$k];
		if (strncmp($v, 'b64:', 4) === 0) {
			$d = base64_decode(substr($v, 4), true);
			if ($d !== false) {
				$t[$k] = $d;
			}
		}
	}
	return $t;
}

/**
 * Return package configuration array (tunnel + peers + globals).
 * Always exposes `tunnel` and `globals` keys for callers; reads from `awgcfg` in config.xml
 * or legacy `tunnel` if present.
 */
function amneziawg_get_config(): array
{
	if (function_exists('config_get_path')) {
		$c = config_get_path('amneziawg', []);
	} else {
		global $config;
		$c = $config['amneziawg'] ?? [];
	}
	$c = is_array($c) ? $c : [];
	$sk = AMNEZIAWG_CFG_STORAGEKEY;
	$raw = null;
	if (!empty($c[$sk]) && is_array($c[$sk])) {
		$raw = $c[$sk];
	} elseif (!empty($c['tunnel']) && is_array($c['tunnel'])) {
		$raw = $c['tunnel'];
	}
	$tunnel = [];
	if (is_array($raw)) {
		$tunnel = amneziawg_storage_decode_tunnel($raw);
	}
	$globals = (isset($c['globals']) && is_array($c['globals'])) ? $c['globals'] : [];
	return ['tunnel' => $tunnel, 'globals' => $globals];
}

/**
 * Persist full AmneziaWG package config.
 * Writes tunnel data under amneziawg/awgcfg (not "tunnel") so pfSense XML export stays valid.
 */
function amneziawg_set_config(array $cfg): void
{
	$cur = [];
	if (function_exists('config_get_path')) {
		$cur = config_get_path('amneziawg', []);
	} else {
		global $config;
		$cur = $config['amneziawg'] ?? [];
	}
	$cur = is_array($cur) ? $cur : [];
	unset($cur['tunnel']);
	$out = $cur;
	if (isset($cfg['globals']) && is_array($cfg['globals'])) {
		$out['globals'] = $cfg['globals'];
	}
	if (isset($cfg['tunnel']) && is_array($cfg['tunnel'])) {
		$out[AMNEZIAWG_CFG_STORAGEKEY] = amneziawg_storage_encode_tunnel($cfg['tunnel']);
	}
	if (function_exists('config_set_path')) {
		config_set_path('amneziawg', $out);
		return;
	}
	global $config;
	$config['amneziawg'] = $out;
}

/**
 * Default tunnel shape (single tunnel, extensible to array later).
 */
function amneziawg_default_tunnel(): array
{
	return [
		'enable' => 'off',
		'ifname' => 'amnezia0',
		'descr' => 'AmneziaWG',
		'privkey' => '',
		'listenport' => '51820',
		'address' => '',
		'mtu' => '1420',
		'dns' => '',
		/* Obfuscation (interface-wide; shared by all peers per amneziawg-go) */
		'jc' => '',
		'jmin' => '',
		'jmax' => '',
		's1' => '', 's2' => '', 's3' => '', 's4' => '',
		'h1' => '', 'h2' => '', 'h3' => '', 'h4' => '',
		'i1' => '', 'i2' => '', 'i3' => '', 'i4' => '', 'i5' => '',
		/* Gateway integration */
		'auto_gateway' => 'off',
		'gateway_name' => 'AMNEZIAWG_VPNV4',
		'gateway_monitor' => '',
		'peers' => [],
	];
}

function amneziawg_default_peer(): array
{
	return [
		'name' => '',
		'pubkey' => '',
		'psk' => '',
		'endpoint' => '',
		'allowed_ips' => '0.0.0.0/0, ::/0',
		'keepalive' => '25',
		'server_peer' => 'off',
	];
}

function amneziawg_default_globals(): array
{
	return [
		'log_enable' => 'on',
		'log_level' => 'error',
	];
}

/**
 * Base64 WireGuard key -> lowercase hex for UAPI.
 */
function amneziawg_key_b64_to_hex(string $b64): ?string
{
	$b64 = trim($b64);
	if ($b64 === '') {
		return null;
	}
	$bin = base64_decode($b64, true);
	if ($bin === false || strlen($bin) !== 32) {
		return null;
	}
	return bin2hex($bin);
}

/**
 * Validate a WireGuard-style base64 key (32 bytes).
 */
function amneziawg_valid_key_b64(string $b64): bool
{
	return amneziawg_key_b64_to_hex($b64) !== null;
}

/**
 * Build JSON file consumed by helper scripts / documentation (logical model).
 * amneziawg-go is configured via UAPI; this JSON mirrors the same fields.
 */
function amneziawg_tunnel_to_json(array $tunnel): array
{
	$peers = [];
	foreach ($tunnel['peers'] ?? [] as $p) {
		if (!is_array($p)) {
			continue;
		}
		$peers[] = [
			'name' => $p['name'] ?? '',
			'public_key' => $p['pubkey'] ?? '',
			'preshared_key' => $p['psk'] ?? '',
			'endpoint' => $p['endpoint'] ?? '',
			'allowed_ips' => $p['allowed_ips'] ?? '',
			'persistent_keepalive' => (int)($p['keepalive'] ?? 0),
			'server_peer' => ($p['server_peer'] ?? 'off') === 'on',
		];
	}
	return [
		'interface' => [
			'name' => $tunnel['ifname'] ?? 'amnezia0',
			'private_key' => $tunnel['privkey'] ?? '',
			'listen_port' => (int)($tunnel['listenport'] ?? 51820),
			'address' => $tunnel['address'] ?? '',
			'mtu' => (int)($tunnel['mtu'] ?? 1420),
			'dns' => $tunnel['dns'] ?? '',
			'jc' => $tunnel['jc'] !== '' ? (int)$tunnel['jc'] : null,
			'jmin' => $tunnel['jmin'] !== '' ? (int)$tunnel['jmin'] : null,
			'jmax' => $tunnel['jmax'] !== '' ? (int)$tunnel['jmax'] : null,
			's1' => $tunnel['s1'] !== '' ? (int)$tunnel['s1'] : null,
			's2' => $tunnel['s2'] !== '' ? (int)$tunnel['s2'] : null,
			's3' => $tunnel['s3'] !== '' ? (int)$tunnel['s3'] : null,
			's4' => $tunnel['s4'] !== '' ? (int)$tunnel['s4'] : null,
			'h1' => $tunnel['h1'] !== '' ? $tunnel['h1'] : null,
			'h2' => $tunnel['h2'] !== '' ? $tunnel['h2'] : null,
			'h3' => $tunnel['h3'] !== '' ? $tunnel['h3'] : null,
			'h4' => $tunnel['h4'] !== '' ? $tunnel['h4'] : null,
			'i1' => $tunnel['i1'] !== '' ? $tunnel['i1'] : null,
			'i2' => $tunnel['i2'] !== '' ? $tunnel['i2'] : null,
			'i3' => $tunnel['i3'] !== '' ? $tunnel['i3'] : null,
			'i4' => $tunnel['i4'] !== '' ? $tunnel['i4'] : null,
			'i5' => $tunnel['i5'] !== '' ? $tunnel['i5'] : null,
		],
		'peers' => $peers,
	];
}

/**
 * Generate UAPI "set" body (without the leading set=1 and trailing blank line).
 */
function amneziawg_build_uapi_body(array $tunnel): string
{
	$lines = [];
	$lines[] = 'replace_peers=true';

	$sk = amneziawg_key_b64_to_hex($tunnel['privkey'] ?? '');
	if ($sk === null || $sk === str_repeat('0', 64)) {
		throw new Exception(gettext('Invalid or missing interface private key.'));
	}
	$lines[] = 'private_key=' . $sk;

	$lp = (int)($tunnel['listenport'] ?? 0);
	if ($lp > 0) {
		$lines[] = 'listen_port=' . $lp;
	}

	$mapInt = ['jc' => 'jc', 'jmin' => 'jmin', 'jmax' => 'jmax',
		's1' => 's1', 's2' => 's2', 's3' => 's3', 's4' => 's4'];
	foreach ($mapInt as $tk => $uk) {
		if (isset($tunnel[$tk]) && $tunnel[$tk] !== '' && (int)$tunnel[$tk] > 0) {
			$lines[] = $uk . '=' . (int)$tunnel[$tk];
		}
	}
	foreach (['h1', 'h2', 'h3', 'h4'] as $k) {
		if (!empty($tunnel[$k])) {
			$lines[] = $k . '=' . $tunnel[$k];
		}
	}
	foreach (['i1', 'i2', 'i3', 'i4', 'i5'] as $k) {
		if (!empty($tunnel[$k])) {
			$lines[] = $k . '=' . $tunnel[$k];
		}
	}

	foreach ($tunnel['peers'] ?? [] as $peer) {
		if (!is_array($peer)) {
			continue;
		}
		$pk = amneziawg_key_b64_to_hex($peer['pubkey'] ?? '');
		if ($pk === null) {
			continue;
		}
		$lines[] = 'public_key=' . $pk;
		$lines[] = 'protocol_version=1';

		$psk = amneziawg_key_b64_to_hex($peer['psk'] ?? '');
		if ($psk !== null && $psk !== str_repeat('0', 64)) {
			$lines[] = 'preshared_key=' . $psk;
		}

		$server = ($peer['server_peer'] ?? 'off') === 'on';
		if (!$server && !empty(trim($peer['endpoint'] ?? ''))) {
			$lines[] = 'endpoint=' . trim($peer['endpoint']);
		}

		$ka = (int)($peer['keepalive'] ?? 0);
		if ($ka > 0) {
			$lines[] = 'persistent_keepalive_interval=' . $ka;
		}

		$lines[] = 'replace_allowed_ips=true';
		$allowed = $peer['allowed_ips'] ?? '';
		$parts = array_filter(array_map('trim', explode(',', $allowed)));
		if ($parts === []) {
			$parts = ['0.0.0.0/0'];
		}
		foreach ($parts as $cidr) {
			$lines[] = 'allowed_ip=' . $cidr;
		}
	}

	return implode("\n", $lines) . "\n";
}

/**
 * Write JSON + UAPI files and return path list.
 */
function amneziawg_write_daemon_files(array $tunnel): void
{
	if (!is_dir('/usr/local/etc/amneziawg')) {
		mkdir('/usr/local/etc/amneziawg', 0750, true);
	}
	$json = amneziawg_tunnel_to_json($tunnel);
	file_put_contents(
		AMNEZIAWG_CONFIG_PATH,
		json_encode($json, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . "\n",
		LOCK_EX
	);
	$uapi = amneziawg_build_uapi_body($tunnel);
	file_put_contents(AMNEZIAWG_UAPI_PATH, $uapi, LOCK_EX);
	chmod(AMNEZIAWG_UAPI_PATH, 0600);
	chmod(AMNEZIAWG_CONFIG_PATH, 0600);
	$ifn = $tunnel['ifname'] ?? 'amnezia0';
	@file_put_contents('/usr/local/etc/amneziawg/interface.txt', $ifn . "\n", LOCK_EX);
	$g = amneziawg_get_config()['globals'] ?? [];
	$g = array_merge(amneziawg_default_globals(), is_array($g) ? $g : []);
	$lvl = in_array($g['log_level'] ?? '', ['verbose', 'debug', 'error', 'silent'], true)
		? $g['log_level'] : 'error';
	if (($g['log_enable'] ?? 'on') !== 'on') {
		$lvl = 'silent';
	}
	@file_put_contents('/usr/local/etc/amneziawg/log_level', $lvl . "\n", LOCK_EX);
}

/**
 * Apply UAPI to a running daemon (unix socket).
 */
function amneziawg_uapi_apply(string $ifname, string $uapiBody, int $timeout = 8): array
{
	$sockPath = AMNEZIAWG_RUN_DIR . '/' . $ifname . '.sock';
	$deadline = time() + $timeout;
	while (!file_exists($sockPath) && time() < $deadline) {
		usleep(100000);
	}
	if (!file_exists($sockPath)) {
		return ['ok' => false, 'err' => gettext('Control socket not found; is the service running?')];
	}
	$payload = "set=1\n" . $uapiBody . "\n";
	$fp = @stream_socket_client('unix://' . $sockPath, $errno, $errstr, $timeout);
	if (!$fp) {
		return ['ok' => false, 'err' => $errstr ?: 'connect failed'];
	}
	stream_set_timeout($fp, $timeout);
	fwrite($fp, $payload);
	fflush($fp);
	$resp = stream_get_contents($fp);
	fclose($fp);
	if ($resp === false || $resp === '') {
		return ['ok' => false, 'err' => gettext('Empty response from daemon.')];
	}
	if (preg_match('/errno=(-?\d+)/', $resp, $m)) {
		$code = (int)$m[1];
		if ($code === 0) {
			return ['ok' => true, 'err' => ''];
		}
		return ['ok' => false, 'err' => sprintf(gettext('Daemon errno=%d'), $code)];
	}
	return ['ok' => false, 'err' => gettext('Could not parse daemon response.')];
}

/**
 * UAPI get dump (status).
 */
function amneziawg_uapi_get(string $ifname, int $timeout = 5): array
{
	$sockPath = AMNEZIAWG_RUN_DIR . '/' . $ifname . '.sock';
	if (!file_exists($sockPath)) {
		return ['ok' => false, 'data' => '', 'err' => gettext('Socket missing')];
	}
	$fp = @stream_socket_client('unix://' . $sockPath, $errno, $errstr, $timeout);
	if (!$fp) {
		return ['ok' => false, 'data' => '', 'err' => $errstr ?: 'connect'];
	}
	stream_set_timeout($fp, $timeout);
	fwrite($fp, "get=1\n\n");
	fflush($fp);
	$data = stream_get_contents($fp);
	fclose($fp);
	if ($data === false) {
		return ['ok' => false, 'data' => '', 'err' => gettext('Read failed')];
	}
	return ['ok' => true, 'data' => $data, 'err' => ''];
}

/**
 * Parse get= output into sections: device lines + list of peers.
 */
function amneziawg_parse_uapi_get(string $dump): array
{
	$device = [];
	$peers = [];
	$cur = null;
	foreach (explode("\n", $dump) as $line) {
		$line = rtrim($line, "\r");
		if ($line === '' || strpos($line, 'errno=') === 0) {
			continue;
		}
		if (strpos($line, '=') === false) {
			continue;
		}
		[$k, $v] = explode('=', $line, 2);
		if ($k === 'public_key') {
			if ($cur !== null) {
				$peers[] = $cur;
			}
			$cur = [$k => $v];
			continue;
		}
		if ($cur === null) {
			$device[$k] = $v;
		} else {
			if ($k === 'allowed_ip') {
				if (!isset($cur['allowed_ip'])) {
					$cur['allowed_ip'] = [];
				}
				$cur['allowed_ip'][] = $v;
			} else {
				$cur[$k] = $v;
			}
		}
	}
	if ($cur !== null) {
		$peers[] = $cur;
	}
	return ['device' => $device, 'peers' => $peers];
}

function amneziawg_binary_present(): bool
{
	return is_executable(AMNEZIAWG_BINARY);
}

/**
 * Update gateway_item entries from tunnel settings (call before write_config()).
 */
function amneziawg_sync_gateways_from_tunnel(array $tunnel): void
{
	if (($tunnel['enable'] ?? 'off') !== 'on' || ($tunnel['auto_gateway'] ?? 'off') !== 'on') {
		amneziawg_resync_gateways_off($tunnel);
		return;
	}
	amneziawg_resync_gateways($tunnel);
}

/**
 * Regenerate files and restart or reload rc service (does not modify gateways).
 */
function amneziawg_apply_config(bool $restart = true): string
{
	$cfg = amneziawg_get_config();
	$tunnel = array_merge(amneziawg_default_tunnel(), $cfg['tunnel'] ?? []);
	@file_put_contents(
		'/usr/local/etc/amneziawg/enabled',
		(($tunnel['enable'] ?? 'off') === 'on') ? "1\n" : "0\n",
		LOCK_EX
	);
	if (($tunnel['enable'] ?? 'off') !== 'on') {
		if ($restart) {
			mwexec('/usr/local/etc/rc.d/amneziawg stop');
		}
		return '';
	}
	try {
		amneziawg_write_daemon_files($tunnel);
	} catch (Exception $e) {
		return $e->getMessage();
	}
	if ($restart) {
		mwexec('/usr/local/etc/rc.d/amneziawg restart');
	} else {
		$body = file_get_contents(AMNEZIAWG_UAPI_PATH);
		$r = amneziawg_uapi_apply($tunnel['ifname'] ?? 'amnezia0', is_string($body) ? $body : '');
		if (!$r['ok'] && amneziawg_service_running($tunnel['ifname'] ?? 'amnezia0')) {
			mwexec('/usr/local/etc/rc.d/amneziawg restart');
		}
	}
	return '';
}

function amneziawg_service_running(string $ifname): bool
{
	$pidf = AMNEZIAWG_RUN_DIR . '/' . $ifname . '.pid';
	if (!file_exists($pidf)) {
		return false;
	}
	$pid = trim((string)file_get_contents($pidf));
	return $pid !== '' && is_numeric($pid) && posix_kill((int)$pid, 0);
}

/**
 * Find assigned pfSense interface key (wan, lan, optX) for physical if.
 */
function amneziawg_find_config_iface_key(string $realif): ?string
{
	if (function_exists('config_get_path')) {
		$ifcfgs = config_get_path('interfaces', []);
	} else {
		global $config;
		$ifcfgs = $config['interfaces'] ?? [];
	}
	if (!is_array($ifcfgs)) {
		return null;
	}
	foreach ($ifcfgs as $key => $row) {
		if (!is_array($row)) {
			continue;
		}
		if (($row['if'] ?? '') === $realif) {
			return (string)$key;
		}
	}
	return null;
}

/**
 * Add or update dynamic IPv4 gateway for assigned interface.
 */
function amneziawg_resync_gateways(array $tunnel): void
{
	global $config;

	if (($tunnel['auto_gateway'] ?? 'off') !== 'on') {
		amneziawg_resync_gateways_off();
		return;
	}
	$ifname = $tunnel['ifname'] ?? 'amnezia0';
	$akey = amneziawg_find_config_iface_key($ifname);
	if ($akey === null) {
		return;
	}
	$gname = preg_replace('/[^a-zA-Z0-9_]/', '_', $tunnel['gateway_name'] ?? 'AMNEZIAWG_VPNV4');
	if ($gname === '') {
		$gname = 'AMNEZIAWG_VPNV4';
	}
	$monitor = trim($tunnel['gateway_monitor'] ?? '');
	$item = [
		'interface' => $akey,
		'gateway' => 'dynamic',
		'name' => $gname,
		'weight' => '1',
		'ipprotocol' => 'inet',
		'descr' => 'AmneziaWG ' . ($tunnel['descr'] ?? ''),
		'defaultgw' => '',
		'disabled' => '',
	];
	if ($monitor !== '') {
		$item['monitor'] = $monitor;
	} else {
		$item['monitor_disable'] = '1';
	}

	if (!isset($config['gateways']['gateway_item']) || !is_array($config['gateways']['gateway_item'])) {
		$config['gateways']['gateway_item'] = [];
	}
	$list = &$config['gateways']['gateway_item'];
	$idx = null;
	foreach ($list as $i => $gw) {
		if (is_array($gw) && ($gw['name'] ?? '') === $gname) {
			$idx = $i;
			break;
		}
	}
	if ($idx !== null) {
		$list[$idx] = array_merge($list[$idx], $item);
	} else {
		$list[] = $item;
	}
}

function amneziawg_resync_gateways_off(?array $tunnel = null): void
{
	global $config;
	if ($tunnel === null) {
		$cfg = amneziawg_get_config();
		$tunnel = array_merge(amneziawg_default_tunnel(), $cfg['tunnel'] ?? []);
	}
	$gname = preg_replace('/[^a-zA-Z0-9_]/', '_', $tunnel['gateway_name'] ?? 'AMNEZIAWG_VPNV4');
	if (!isset($config['gateways']['gateway_item']) || !is_array($config['gateways']['gateway_item'])) {
		return;
	}
	$config['gateways']['gateway_item'] = array_values(array_filter(
		$config['gateways']['gateway_item'],
		function ($gw) use ($gname) {
			return !is_array($gw) || ($gw['name'] ?? '') !== $gname;
		}
	));
}

/**
 * Used from rc.d / CLI: apply UAPI after daemon listens.
 */
function amneziawg_rc_apply(): int
{
	if (!file_exists(AMNEZIAWG_UAPI_PATH)) {
		return 1;
	}
	$j = json_decode((string)file_get_contents(AMNEZIAWG_CONFIG_PATH), true);
	$if = is_array($j) && isset($j['interface']['name']) ? $j['interface']['name'] : 'amnezia0';
	$body = file_get_contents(AMNEZIAWG_UAPI_PATH);
	$r = amneziawg_uapi_apply($if, $body, 15);
	if (!$r['ok']) {
		error_log('amneziawg: ' . $r['err']);
		return 1;
	}
	return 0;
}

/**
 * Configure interface address and MTU via ifconfig (userspace TUN).
 */
function amneziawg_rc_ifconfig(): int
{
	if (!file_exists(AMNEZIAWG_CONFIG_PATH)) {
		return 0;
	}
	$j = json_decode((string)file_get_contents(AMNEZIAWG_CONFIG_PATH), true);
	if (!is_array($j)) {
		return 1;
	}
	$if = $j['interface']['name'] ?? 'amnezia0';
	$addr = trim((string)($j['interface']['address'] ?? ''));
	$mtu = (int)($j['interface']['mtu'] ?? 0);
	if ($mtu > 576 && $mtu < 65535) {
		mwexec('/sbin/ifconfig ' . escapeshellarg($if) . ' mtu ' . (int)$mtu);
	}
	if ($addr !== '') {
		mwexec('/sbin/ifconfig ' . escapeshellarg($if) . ' inet ' . escapeshellarg($addr));
	}
	return 0;
}

/**
 * Generate WireGuard private key if wg(1) is available.
 */
function amneziawg_genkey(): ?string
{
	$o = [];
	$c = 0;
	@exec('/usr/bin/wg genkey 2>/dev/null', $o, $c);
	if ($c === 0 && !empty($o[0]) && amneziawg_valid_key_b64($o[0])) {
		return trim($o[0]);
	}
	return null;
}

/**
 * True if string looks like IPv4/IPv6 CIDR.
 */
function amneziawg_valid_tunnel_cidr(string $s): bool
{
	if (strpos($s, '/') === false) {
		return false;
	}
	[$ip, $plen] = explode('/', $s, 2);
	if (!is_numeric($plen)) {
		return false;
	}
	$plen = (int)$plen;
	if (filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
		return $plen >= 0 && $plen <= 32;
	}
	if (filter_var($ip, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6)) {
		return $plen >= 0 && $plen <= 128;
	}
	return false;
}

/**
 * Maximum length for pasted vpn:// or JSON import (bytes).
 */
if (!defined('AMNEZIAWG_IMPORT_MAXLEN')) {
	define('AMNEZIAWG_IMPORT_MAXLEN', 524288);
}

/**
 * Decode Amnezia `vpn://` share link to UTF-8 text (JSON or WireGuard .ini).
 * Matches Amnezia desktop import: strip scheme, Base64URL, zlib payload after
 * 4-byte big-endian uncompressed size (Qt qUncompress layout).
 */
function amneziawg_decode_vpn_link(string $url): ?string
{
	$url = trim($url);
	if ($url === '' || strncasecmp($url, 'vpn://', 6) !== 0) {
		return null;
	}
	$b64 = substr($url, 6);
	$b64 = strtr($b64, '-_', '+/');
	$pad = strlen($b64) % 4;
	if ($pad !== 0) {
		$b64 .= str_repeat('=', 4 - $pad);
	}
	$raw = base64_decode($b64, true);
	if ($raw === false || $raw === '') {
		return null;
	}
	if (strlen($raw) > 4) {
		$body = substr($raw, 4);
		$unc = @gzuncompress($body);
		if ($unc !== false && $unc !== '') {
			return $unc;
		}
	}
	$unc = @gzuncompress($raw);
	if ($unc !== false && $unc !== '') {
		return $unc;
	}
	return null;
}

/**
 * Strip Amnezia DNS placeholders and tidy commas.
 */
function amneziawg_sanitize_import_dns(string $dns): string
{
	$dns = preg_replace('/\$PRIMARY_DNS|\$SECONDARY_DNS/i', '', $dns);
	$dns = preg_replace('/\s+/', ' ', trim($dns));
	return trim($dns, " \t,");
}

/**
 * Ensure tunnel address is CIDR (append /32 for plain host IPs).
 */
function amneziawg_normalize_tunnel_address(string $addr): string
{
	$addr = trim($addr);
	if ($addr === '') {
		return '';
	}
	if (amneziawg_valid_tunnel_cidr($addr)) {
		return $addr;
	}
	if (filter_var($addr, FILTER_VALIDATE_IP, FILTER_FLAG_IPV4)) {
		return $addr . '/32';
	}
	if (filter_var($addr, FILTER_VALIDATE_IP, FILTER_FLAG_IPV6)) {
		return $addr . '/128';
	}
	return $addr;
}

/**
 * Normalize pasted WireGuard / AmneziaWG text for parsing.
 * AmneziaVPN "AmneziaWG native format" copy uses escaped section headers: \[Interface\] \[Peer\].
 */
function amneziawg_normalize_wg_conf_paste(string $text): string
{
	$repl = [
		'\\[Interface\\]' => '[Interface]',
		'\\[interface\\]' => '[Interface]',
		'\\[Peer\\]' => '[Peer]',
		'\\[peer\\]' => '[Peer]',
	];
	foreach ($repl as $from => $to) {
		$text = str_replace($from, $to, $text);
	}
	return $text;
}

/**
 * Parse [Interface] / [Peer] sections into flat maps (WireGuard / AmneziaWG .conf).
 *
 * @return array{iface: array<string,string>, peers: array<int, array<string,string>>}
 */
function amneziawg_parse_wg_ini_sections(string $ini): array
{
	$iface = [];
	$peers = [];
	$cur = null;
	$section = null;
	$ini = amneziawg_normalize_wg_conf_paste($ini);
	$ini = str_replace("\r\n", "\n", $ini);
	foreach (explode("\n", $ini) as $line) {
		$line = trim($line);
		if ($line === '' || (isset($line[0]) && $line[0] === '#')) {
			continue;
		}
		if (preg_match('/^\[([^\]]+)\]\s*$/', $line, $m)) {
			$section = strtolower($m[1]);
			if ($section === 'peer') {
				if ($cur !== null) {
					$peers[] = $cur;
				}
				$cur = [];
			}
			continue;
		}
		if ($section === null || strpos($line, '=') === false) {
			continue;
		}
		[$k, $v] = explode('=', $line, 2);
		$k = trim($k);
		$v = trim($v);
		if ($section === 'interface') {
			$iface[$k] = $v;
		} elseif ($section === 'peer' && $cur !== null) {
			$cur[$k] = $v;
		}
	}
	if ($cur !== null && $cur !== []) {
		$peers[] = $cur;
	}
	return ['iface' => $iface, 'peers' => $peers];
}

/**
 * Map WireGuard [Interface]/[Peer] maps into package tunnel shape (merged with defaults).
 *
 * @param array<string,string> $iface
 * @param array<int, array<string,string>> $peerMaps
 */
function amneziawg_tunnel_from_wg_maps(array $iface, array $peerMaps, string $descr = ''): array
{
	$tunnel = amneziawg_default_tunnel();
	if ($descr !== '') {
		$tunnel['descr'] = $descr;
	}
	$mapObf = [
		'Jc' => 'jc', 'Jmin' => 'jmin', 'Jmax' => 'jmax',
		'S1' => 's1', 'S2' => 's2', 'S3' => 's3', 'S4' => 's4',
		'H1' => 'h1', 'H2' => 'h2', 'H3' => 'h3', 'H4' => 'h4',
		'I1' => 'i1', 'I2' => 'i2', 'I3' => 'i3', 'I4' => 'i4', 'I5' => 'i5',
	];
	foreach ($mapObf as $ik => $tk) {
		if (!empty($iface[$ik])) {
			$tunnel[$tk] = trim((string)$iface[$ik]);
		}
	}
	if (!empty($iface['PrivateKey'])) {
		$tunnel['privkey'] = trim((string)$iface['PrivateKey']);
	}
	if (!empty($iface['Address'])) {
		$tunnel['address'] = amneziawg_normalize_tunnel_address((string)$iface['Address']);
	}
	if (!empty($iface['MTU'])) {
		$tunnel['mtu'] = (string)(int)$iface['MTU'];
	}
	if (!empty($iface['DNS'])) {
		$tunnel['dns'] = amneziawg_sanitize_import_dns((string)$iface['DNS']);
	}
	if (isset($iface['ListenPort']) && $iface['ListenPort'] !== '') {
		$tunnel['listenport'] = (string)(int)$iface['ListenPort'];
	} else {
		$tunnel['listenport'] = '0';
	}
	$tunnel['peers'] = [];
	$n = 0;
	foreach ($peerMaps as $pmap) {
		$n++;
		$peer = amneziawg_default_peer();
		$peer['name'] = $n === 1 ? gettext('Imported') : (gettext('Imported') . ' ' . $n);
		if (!empty($pmap['PublicKey'])) {
			$peer['pubkey'] = trim((string)$pmap['PublicKey']);
		}
		if (!empty($pmap['PresharedKey'])) {
			$peer['psk'] = trim((string)$pmap['PresharedKey']);
		} elseif (!empty($pmap['PreSharedKey'])) {
			$peer['psk'] = trim((string)$pmap['PreSharedKey']);
		}
		if (!empty($pmap['Endpoint'])) {
			$peer['endpoint'] = trim((string)$pmap['Endpoint']);
		}
		if (!empty($pmap['AllowedIPs'])) {
			$peer['allowed_ips'] = trim((string)$pmap['AllowedIPs']);
		}
		if (isset($pmap['PersistentKeepalive']) && $pmap['PersistentKeepalive'] !== '') {
			$peer['keepalive'] = (string)(int)$pmap['PersistentKeepalive'];
		}
		$tunnel['peers'][] = $peer;
	}
	return $tunnel;
}

/**
 * Build tunnel from Amnezia `last_config` JSON object (when .conf text is absent).
 */
function amneziawg_tunnel_from_inner_json(array $inner): array
{
	$tunnel = amneziawg_default_tunnel();
	$tunnel['listenport'] = '0';
	$mapObf = [
		'Jc' => 'jc', 'Jmin' => 'jmin', 'Jmax' => 'jmax',
		'S1' => 's1', 'S2' => 's2', 'S3' => 's3', 'S4' => 's4',
		'H1' => 'h1', 'H2' => 'h2', 'H3' => 'h3', 'H4' => 'h4',
		'I1' => 'i1', 'I2' => 'i2', 'I3' => 'i3', 'I4' => 'i4', 'I5' => 'i5',
	];
	foreach ($mapObf as $ik => $tk) {
		if (!empty($inner[$ik])) {
			$tunnel[$tk] = trim((string)$inner[$ik]);
		}
	}
	if (!empty($inner['client_priv_key'])) {
		$tunnel['privkey'] = trim((string)$inner['client_priv_key']);
	}
	if (!empty($inner['client_ip'])) {
		$tunnel['address'] = amneziawg_normalize_tunnel_address((string)$inner['client_ip']);
	}
	if (!empty($inner['mtu'])) {
		$tunnel['mtu'] = (string)(int)$inner['mtu'];
	}
	$peer = amneziawg_default_peer();
	$peer['name'] = gettext('Imported');
	if (!empty($inner['server_pub_key'])) {
		$peer['pubkey'] = trim((string)$inner['server_pub_key']);
	}
	if (!empty($inner['psk_key'])) {
		$peer['psk'] = trim((string)$inner['psk_key']);
	}
	$host = trim((string)($inner['hostName'] ?? ''));
	$port = $inner['port'] ?? '';
	if ($host !== '' && $port !== '' && $port !== null) {
		$peer['endpoint'] = $host . ':' . (int)$port;
	}
	if (isset($inner['allowed_ips'])) {
		if (is_array($inner['allowed_ips'])) {
			$peer['allowed_ips'] = implode(', ', array_map('strval', $inner['allowed_ips']));
		} else {
			$peer['allowed_ips'] = trim((string)$inner['allowed_ips']);
		}
		if ($peer['allowed_ips'] === '') {
			$peer['allowed_ips'] = amneziawg_default_peer()['allowed_ips'];
		}
	}
	if (isset($inner['persistent_keep_alive']) && $inner['persistent_keep_alive'] !== '') {
		$peer['keepalive'] = (string)(int)$inner['persistent_keep_alive'];
	}
	$tunnel['peers'] = [$peer];
	return $tunnel;
}

/**
 * Validate imported tunnel material (same key rules as the settings form).
 */
function amneziawg_import_validate_tunnel(array $tunnel): ?string
{
	if ($tunnel['privkey'] !== '' && !amneziawg_valid_key_b64($tunnel['privkey'])) {
		return gettext('Invalid interface private key after import.');
	}
	foreach ($tunnel['peers'] as $i => $p) {
		if ($p['pubkey'] !== '' && !amneziawg_valid_key_b64($p['pubkey'])) {
			return sprintf(gettext('Invalid public key for peer #%d after import.'), $i + 1);
		}
		if ($p['psk'] !== '' && !amneziawg_valid_key_b64($p['psk'])) {
			return sprintf(gettext('Invalid pre-shared key for peer #%d after import.'), $i + 1);
		}
	}
	return null;
}

/**
 * True when text is a raw WireGuard .conf (first non-empty line is [Interface]).
 * Amnezia vpn:// decodes to JSON that *contains* "[Interface]" inside strings; that must not use this path.
 */
function amneziawg_text_looks_like_wg_conf(string $text): bool
{
	$text = amneziawg_normalize_wg_conf_paste($text);
	$text = str_replace("\r\n", "\n", $text);
	foreach (explode("\n", $text) as $line) {
		$line = trim($line);
		if ($line === '' || (isset($line[0]) && $line[0] === '#')) {
			continue;
		}
		return strncasecmp($line, '[Interface]', strlen('[Interface]')) === 0;
	}
	return false;
}

/**
 * Import from Amnezia JSON root (decoded vpn:// body or pasted export).
 *
 * @return array{ok: bool, error: string, tunnel: ?array}
 */
function amneziawg_import_from_amnezia_json(array $root): array
{
	$descr = '';
	if (!empty($root['description'])) {
		$descr = trim((string)$root['description']);
	}
	$lastConfigStr = null;
	foreach ($root['containers'] ?? [] as $c) {
		if (!is_array($c)) {
			continue;
		}
		if (!empty($c['awg']['last_config']) && is_string($c['awg']['last_config'])) {
			$lastConfigStr = $c['awg']['last_config'];
			break;
		}
	}
	if ($lastConfigStr === null) {
		foreach ($root['containers'] ?? [] as $c) {
			if (!is_array($c)) {
				continue;
			}
			if (!empty($c['wireguard']['last_config']) && is_string($c['wireguard']['last_config'])) {
				$lastConfigStr = $c['wireguard']['last_config'];
				break;
			}
		}
	}
	if ($lastConfigStr === null) {
		return ['ok' => false, 'error' => gettext('No Amnezia WireGuard or AmneziaWG (awg) block found in this export.'), 'tunnel' => null];
	}
	$inner = json_decode($lastConfigStr, true);
	if (!is_array($inner)) {
		return ['ok' => false, 'error' => gettext('Could not parse inner last_config JSON.'), 'tunnel' => null];
	}
	$tunnel = null;
	if (!empty($inner['config']) && is_string($inner['config']) && amneziawg_text_looks_like_wg_conf($inner['config'])) {
		$parsed = amneziawg_parse_wg_ini_sections($inner['config']);
		if ($parsed['iface'] !== [] && $parsed['peers'] !== []) {
			$tunnel = amneziawg_tunnel_from_wg_maps($parsed['iface'], $parsed['peers'], $descr);
		}
	}
	if ($tunnel === null) {
		$tunnel = amneziawg_tunnel_from_inner_json($inner);
	}
	if ($descr !== '') {
		$tunnel['descr'] = $descr;
	}
	if ($tunnel['privkey'] === '' || $tunnel['peers'] === [] || $tunnel['peers'][0]['pubkey'] === '') {
		return ['ok' => false, 'error' => gettext('Import is missing private key or peer public key.'), 'tunnel' => null];
	}
	$err = amneziawg_import_validate_tunnel($tunnel);
	if ($err !== null) {
		return ['ok' => false, 'error' => $err, 'tunnel' => null];
	}
	return ['ok' => true, 'error' => '', 'tunnel' => $tunnel];
}

/**
 * Import tunnel from decoded vpn:// text, JSON root, or raw WireGuard .ini.
 *
 * @return array{ok: bool, error: string, tunnel: ?array}
 */
function amneziawg_import_decoded_config(string $text): array
{
	$text = preg_replace("/^\xEF\xBB\xBF/", '', trim($text));
	if ($text === '') {
		return ['ok' => false, 'error' => gettext('Empty configuration.'), 'tunnel' => null];
	}

	$root = json_decode($text, true);
	if (json_last_error() === JSON_ERROR_NONE && is_array($root) && isset($root['containers']) && is_array($root['containers'])) {
		return amneziawg_import_from_amnezia_json($root);
	}

	if (amneziawg_text_looks_like_wg_conf($text)) {
		$parsed = amneziawg_parse_wg_ini_sections($text);
		if ($parsed['iface'] === [] || $parsed['peers'] === []) {
			return ['ok' => false, 'error' => gettext('WireGuard config must include [Interface] and at least one [Peer] section.'), 'tunnel' => null];
		}
		$tunnel = amneziawg_tunnel_from_wg_maps($parsed['iface'], $parsed['peers'], '');
		$err = amneziawg_import_validate_tunnel($tunnel);
		if ($err !== null) {
			return ['ok' => false, 'error' => $err, 'tunnel' => null];
		}
		return ['ok' => true, 'error' => '', 'tunnel' => $tunnel];
	}

	return ['ok' => false, 'error' => gettext('Unrecognized format: expected Amnezia JSON (with "containers"), or a WireGuard .conf whose first section line is [Interface].'), 'tunnel' => null];
}

/**
 * Import from Amnezia `vpn://` link, or raw JSON / .conf pasted without the scheme.
 *
 * @return array{ok: bool, error: string, tunnel: ?array}
 */
function amneziawg_import_vpn_link(string $url): array
{
	$url = trim($url);
	if ($url === '') {
		return ['ok' => false, 'error' => gettext('Paste a vpn:// link or configuration.'), 'tunnel' => null];
	}
	if (strlen($url) > AMNEZIAWG_IMPORT_MAXLEN) {
		return ['ok' => false, 'error' => gettext('Input is too long.'), 'tunnel' => null];
	}
	if (stripos($url, 'vpn://') === 0) {
		$decoded = amneziawg_decode_vpn_link($url);
		if ($decoded === null) {
			return ['ok' => false, 'error' => gettext('Could not decode vpn:// (invalid base64 or compressed data).'), 'tunnel' => null];
		}
		return amneziawg_import_decoded_config($decoded);
	}
	return amneziawg_import_decoded_config($url);
}

/**
 * Build tunnel + globals from HTTP POST (settings form).
 */
function amneziawg_parse_settings_post(): array
{
	$tunnel = array_merge(amneziawg_default_tunnel(), [
		'enable' => !empty($_POST['enable']) ? 'on' : 'off',
		'descr' => trim((string)($_POST['descr'] ?? '')),
		'ifname' => preg_replace('/[^a-zA-Z0-9_]/', '', (string)($_POST['ifname'] ?? 'amnezia0')) ?: 'amnezia0',
		'privkey' => trim((string)($_POST['privkey'] ?? '')),
		'listenport' => (string)(int)($_POST['listenport'] ?? 51820),
		'address' => trim((string)($_POST['address'] ?? '')),
		'mtu' => (string)(int)($_POST['mtu'] ?? 1420),
		'dns' => trim((string)($_POST['dns'] ?? '')),
		'auto_gateway' => !empty($_POST['auto_gateway']) ? 'on' : 'off',
		'gateway_name' => preg_replace('/[^a-zA-Z0-9_]/', '_', (string)($_POST['gateway_name'] ?? 'AMNEZIAWG_VPNV4')) ?: 'AMNEZIAWG_VPNV4',
		'gateway_monitor' => trim((string)($_POST['gateway_monitor'] ?? '')),
		'peers' => [],
	]);
	foreach (['jc', 'jmin', 'jmax', 's1', 's2', 's3', 's4'] as $f) {
		$tunnel[$f] = isset($_POST[$f]) ? trim((string)$_POST[$f]) : '';
	}
	foreach (['h1', 'h2', 'h3', 'h4', 'i1', 'i2', 'i3', 'i4', 'i5'] as $f) {
		$tunnel[$f] = isset($_POST[$f]) ? trim((string)$_POST[$f]) : '';
	}
	if (isset($_POST['peer_name']) && is_array($_POST['peer_name'])) {
		$n = count($_POST['peer_name']);
		for ($i = 0; $i < $n; $i++) {
			$tunnel['peers'][] = [
				'name' => trim((string)($_POST['peer_name'][$i] ?? '')),
				'pubkey' => trim((string)($_POST['peer_pubkey'][$i] ?? '')),
				'psk' => trim((string)($_POST['peer_psk'][$i] ?? '')),
				'endpoint' => trim((string)($_POST['peer_endpoint'][$i] ?? '')),
				'allowed_ips' => trim((string)($_POST['peer_allowed'][$i] ?? '')),
				'keepalive' => (string)(int)($_POST['peer_keepalive'][$i] ?? 0),
				'server_peer' => !empty($_POST['peer_server'][$i]) ? 'on' : 'off',
			];
		}
	}
	$globals = array_merge(amneziawg_default_globals(), [
		'log_enable' => !empty($_POST['log_enable']) ? 'on' : 'off',
		'log_level' => in_array($_POST['log_level'] ?? '', ['verbose', 'debug', 'error', 'silent'], true)
			? $_POST['log_level'] : 'error',
	]);
	return [$tunnel, $globals];
}
