<?php
##|+PRIV
##|*IDENT=page-vpn-amneziawg-settings
##|*NAME=VPN: AmneziaWG
##|*DESCR=Configure AmneziaWG VPN tunnel.
##|*MATCH=pkg/amneziawg/vpn_amneziawg.php
##|-PRIV

require_once("guiconfig.inc");
require_once("authgui.inc");
require_once("/usr/local/www/pkg/amneziawg/amneziawg.inc");

$pgtitle = [gettext('VPN'), gettext('AmneziaWG'), gettext('Settings')];
$shortcut_section = 'amneziawg';

$tab_array = [];
$tab_array[] = [gettext('Settings'), true];
$tab_array[] = [gettext('Status'), false, '/pkg/amneziawg/vpn_amneziawg_status.php'];
$tab_array[] = [gettext('Logs'), false, '/pkg/amneziawg/vpn_amneziawg_log.php'];

$savemsg = '';
$input_errors = [];

$full = amneziawg_get_config();
$tunnel = array_merge(amneziawg_default_tunnel(), $full['tunnel'] ?? []);
$globals = array_merge(amneziawg_default_globals(), $full['globals'] ?? []);

if ($_POST['addpeer'] ?? false) {
	$tunnel['peers'][] = amneziawg_default_peer();
	amneziawg_set_config(['tunnel' => $tunnel, 'globals' => $globals]);
	write_config('AmneziaWG: add peer row');
	header('Location: /pkg/amneziawg/vpn_amneziawg.php');
	exit;
}

if (($_POST['act'] ?? '') === 'genkey') {
	[$tunnel, $globals] = amneziawg_parse_settings_post();
	$k = amneziawg_genkey();
	if ($k !== null) {
		$tunnel['privkey'] = $k;
		$savemsg = gettext('New private key generated. Click Save to store and apply.');
	} else {
		$input_errors[] = gettext('Could not run wg genkey. Install wireguard-tools or paste a key from another client.');
	}
}

if ($_POST['save'] ?? false) {
	[$tunnel, $globals] = amneziawg_parse_settings_post();

	if ($tunnel['enable'] === 'on') {
		if (!amneziawg_valid_key_b64($tunnel['privkey'])) {
			$input_errors[] = gettext('Interface private key must be a valid WireGuard base64-encoded 32-byte key.');
		}
		$anyPeer = false;
		foreach ($tunnel['peers'] as $idx => $p) {
			if ($p['pubkey'] !== '' && amneziawg_valid_key_b64($p['pubkey'])) {
				$anyPeer = true;
			}
			if ($p['pubkey'] === '') {
				continue;
			}
			if (!amneziawg_valid_key_b64($p['pubkey'])) {
				$input_errors[] = sprintf(gettext('Peer #%d: invalid public key.'), $idx + 1);
			}
			if ($p['psk'] !== '' && !amneziawg_valid_key_b64($p['psk'])) {
				$input_errors[] = sprintf(gettext('Peer #%d: invalid pre-shared key.'), $idx + 1);
			}
			if ($p['server_peer'] !== 'on' && $p['endpoint'] === '') {
				$input_errors[] = sprintf(gettext('Peer #%d: endpoint is required unless "server peer" is checked.'), $idx + 1);
			}
		}
		if ($tunnel['address'] !== '' && !amneziawg_valid_tunnel_cidr($tunnel['address'])) {
			$input_errors[] = gettext('Tunnel address must be a valid IPv4 or IPv6 CIDR (e.g. 10.8.0.2/32).');
		}
		if (!$anyPeer) {
			$input_errors[] = gettext('When the tunnel is enabled, configure at least one peer with a valid public key.');
		}
	}

	if (!$input_errors) {
		amneziawg_set_config(['tunnel' => $tunnel, 'globals' => $globals]);
		amneziawg_sync_gateways_from_tunnel($tunnel);
		write_config('AmneziaWG: saved settings');
		$err = amneziawg_apply_config(true);
		if ($err !== '') {
			$input_errors[] = $err;
		} else {
			$savemsg = gettext('Configuration saved and service reloaded.');
		}
	}
}

if ($tunnel['peers'] === []) {
	$tunnel['peers'][] = amneziawg_default_peer();
}

include("head.inc");

display_top_tabs($tab_array);

if (!amneziawg_binary_present()) {
	print_info_box(
		gettext('The amneziawg-go binary is missing from /usr/local/bin/. Build the package on FreeBSD or copy the binary before enabling the tunnel.'),
		'danger',
		false
	);
}

if ($input_errors) {
	print_input_errors($input_errors);
}
if ($savemsg) {
	print_info_box($savemsg, 'success', false);
}

function h($s)
{
	return htmlspecialchars((string)$s, ENT_QUOTES | ENT_HTML401, 'UTF-8');
}

?>
<form method="post" class="form-horizontal">
<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Tunnel') ?></h2></div>
	<div class="panel-body">
		<?php
		$ck = ($tunnel['enable'] === 'on') ? ' checked="checked"' : '';
		echo '<div class="form-group"><label class="col-sm-2 control-label">' . gettext('Enable') . '</label><div class="col-sm-10">';
		echo '<input type="checkbox" name="enable" value="1"' . $ck . '/> ' . gettext('Enable AmneziaWG service') . '</div></div>';
		?>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Description') ?></label>
			<div class="col-sm-10"><input name="descr" type="text" class="form-control" value="<?= h($tunnel['descr']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Interface name') ?></label>
			<div class="col-sm-10"><input name="ifname" type="text" class="form-control" value="<?= h($tunnel['ifname']) ?>"/>
				<span class="help-block"><?= gettext('TUN name (e.g. amnezia0). Assign under Interfaces > Assignments after the service has started.') ?></span></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Private key') ?></label>
			<div class="col-sm-8"><input name="privkey" type="text" class="form-control" value="<?= h($tunnel['privkey']) ?>" autocomplete="off"/></div>
			<div class="col-sm-2">
				<button type="submit" name="act" value="genkey" class="btn btn-sm btn-info" formnovalidate="formnovalidate"><?= gettext('Generate') ?></button>
			</div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Listen port') ?></label>
			<div class="col-sm-10"><input name="listenport" type="text" class="form-control" value="<?= h($tunnel['listenport']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Tunnel address') ?></label>
			<div class="col-sm-10"><input name="address" type="text" class="form-control" value="<?= h($tunnel['address']) ?>" placeholder="10.8.0.2/32"/>
				<span class="help-block"><?= gettext('CIDR applied with ifconfig after the daemon starts.') ?></span></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('MTU') ?></label>
			<div class="col-sm-10"><input name="mtu" type="text" class="form-control" value="<?= h($tunnel['mtu']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('DNS (notes)') ?></label>
			<div class="col-sm-10"><input name="dns" type="text" class="form-control" value="<?= h($tunnel['dns']) ?>"/></div>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Obfuscation (interface-wide)') ?></h2></div>
	<div class="panel-body">
		<p class="text-muted"><?= gettext('Optional AmneziaWG parameters (see amneziawg-go README). Leave blank to omit.') ?></p>
		<?php
		$rows = [
			['jc', 'Jc'], ['jmin', 'Jmin'], ['jmax', 'Jmax'],
			['s1', 'S1'], ['s2', 'S2'], ['s3', 'S3'], ['s4', 'S4'],
			['h1', 'H1'], ['h2', 'H2'], ['h3', 'H3'], ['h4', 'H4'],
			['i1', 'I1'], ['i2', 'I2'], ['i3', 'I3'], ['i4', 'I4'], ['i5', 'I5'],
		];
		foreach ($rows as [$k, $lab]) {
			echo '<div class="form-group"><label class="col-sm-2 control-label">' . h($lab) . '</label>';
			echo '<div class="col-sm-10"><input type="text" class="form-control" name="' . h($k) . '" value="' . h($tunnel[$k] ?? '') . '"/></div></div>';
		}
		?>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Gateway integration') ?></h2></div>
	<div class="panel-body">
		<?php
		$ck = ($tunnel['auto_gateway'] === 'on') ? ' checked="checked"' : '';
		echo '<div class="form-group"><label class="col-sm-2 control-label">' . gettext('Auto gateway') . '</label><div class="col-sm-10">';
		echo '<input type="checkbox" name="auto_gateway" value="1"' . $ck . '/> ';
		echo gettext('Create dynamic IPv4 gateway entry when the tunnel interface is assigned (same name as above).') . '</div></div>';
		?>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Gateway name') ?></label>
			<div class="col-sm-10"><input name="gateway_name" type="text" class="form-control" value="<?= h($tunnel['gateway_name']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Monitor IP') ?></label>
			<div class="col-sm-10"><input name="gateway_monitor" type="text" class="form-control" value="<?= h($tunnel['gateway_monitor']) ?>"/></div>
		</div>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Peers') ?></h2></div>
	<div class="panel-body">
		<?php foreach ($tunnel['peers'] as $i => $p) { ?>
		<hr/>
		<h4><?= sprintf(gettext('Peer %d'), $i + 1) ?></h4>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Name') ?></label>
			<div class="col-sm-10"><input type="text" class="form-control" name="peer_name[<?= (int)$i ?>]" value="<?= h($p['name']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Public key') ?></label>
			<div class="col-sm-10"><input type="text" class="form-control" name="peer_pubkey[<?= (int)$i ?>]" value="<?= h($p['pubkey']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Pre-shared key') ?></label>
			<div class="col-sm-10"><input type="text" class="form-control" name="peer_psk[<?= (int)$i ?>]" value="<?= h($p['psk']) ?>" autocomplete="off"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Endpoint') ?></label>
			<div class="col-sm-10"><input type="text" class="form-control" name="peer_endpoint[<?= (int)$i ?>]" value="<?= h($p['endpoint']) ?>" placeholder="host:51820"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Allowed IPs') ?></label>
			<div class="col-sm-10"><input type="text" class="form-control" name="peer_allowed[<?= (int)$i ?>]" value="<?= h($p['allowed_ips']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Keepalive (s)') ?></label>
			<div class="col-sm-10"><input type="text" class="form-control" name="peer_keepalive[<?= (int)$i ?>]" value="<?= h($p['keepalive']) ?>"/></div>
		</div>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Server peer') ?></label>
			<div class="col-sm-10">
				<?php $ck = ($p['server_peer'] === 'on') ? ' checked="checked"' : ''; ?>
				<input type="checkbox" name="peer_server[<?= (int)$i ?>]" value="1"<?= $ck ?>/>
				<?= gettext('No outbound endpoint (incoming connections / server side).') ?>
			</div>
		</div>
		<?php } ?>
		<button type="submit" name="addpeer" value="1" class="btn btn-sm btn-success" formnovalidate="formnovalidate"><?= gettext('Add peer') ?></button>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Logging') ?></h2></div>
	<div class="panel-body">
		<?php
		$ck = ($globals['log_enable'] === 'on') ? ' checked="checked"' : '';
		echo '<div class="form-group"><label class="col-sm-2 control-label">' . gettext('Enable') . '</label><div class="col-sm-10">';
		echo '<input type="checkbox" name="log_enable" value="1"' . $ck . '/></div></div>';
		?>
		<div class="form-group">
			<label class="col-sm-2 control-label"><?= gettext('Log level') ?></label>
			<div class="col-sm-10">
				<select name="log_level" class="form-control">
					<?php foreach (['verbose' => gettext('Verbose'), 'debug' => gettext('Debug'), 'error' => gettext('Error'), 'silent' => gettext('Silent')] as $v => $lab) {
						$sel = ($globals['log_level'] === $v) ? ' selected="selected"' : '';
						echo '<option value="' . h($v) . '"' . $sel . '>' . h($lab) . '</option>';
					} ?>
				</select>
			</div>
		</div>
	</div>
</div>

<button type="submit" name="save" value="1" class="btn btn-primary"><?= gettext('Save') ?></button>
</form>

<?php include("foot.inc"); ?>
