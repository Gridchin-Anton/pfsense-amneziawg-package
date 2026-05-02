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

/**
 * Return package configuration array (tunnel + peers + globals).
 */
function amneziawg_get_config(): array
{
	if (function_exists('config_get_path')) {
		$c = config_get_path('amneziawg');
		return is_array($c) ? $c : [];
	}
	global $config;
	return isset($config['amneziawg']) && is_array($config['amneziawg'])
		? $config['amneziawg'] : [];
}

/**
 * Persist full AmneziaWG package config.
 */
function amneziawg_set_config(array $cfg): void
{
	if (function_exists('config_set_path')) {
		config_set_path('amneziawg', $cfg);
		return;
	}
	global $config;
	$config['amneziawg'] = $cfg;
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
