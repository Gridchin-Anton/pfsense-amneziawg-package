<?php
##|+PRIV
##|*IDENT=page-vpn-amneziawg-status
##|*NAME=VPN: AmneziaWG Status
##|*DESCR=View AmneziaWG daemon status and peers.
##|*MATCH=pkg/amneziawg/vpn_amneziawg_status.php
##|-PRIV

require_once("guiconfig.inc");
require_once("authgui.inc");
require_once("/usr/local/www/pkg/amneziawg/amneziawg.inc");

$pgtitle = [gettext('VPN'), gettext('AmneziaWG'), gettext('Status')];
$tab_array = [];
$tab_array[] = [gettext('Settings'), false, '/pkg/amneziawg/vpn_amneziawg.php'];
$tab_array[] = [gettext('Status'), true];
$tab_array[] = [gettext('Logs'), false, '/pkg/amneziawg/vpn_amneziawg_log.php'];

$full = amneziawg_get_config();
$tunnel = array_merge(amneziawg_default_tunnel(), $full['tunnel'] ?? []);
$ifn = $tunnel['ifname'] ?? 'amnezia0';

$savemsg = '';
if ($_POST['restart'] ?? false) {
	$err = amneziawg_apply_config(true);
	if ($err !== '') {
		print_input_errors([$err]);
	} else {
		$savemsg = gettext('Service restarted.');
	}
}

$running = amneziawg_service_running($ifn);
$uapi = amneziawg_uapi_get($ifn);
$parsed = ['device' => [], 'peers' => []];
if ($uapi['ok']) {
	$parsed = amneziawg_parse_uapi_get($uapi['data']);
}

include("head.inc");
display_top_tabs($tab_array);

if (!empty($savemsg)) {
	print_info_box($savemsg, 'success', false);
}

if (!amneziawg_binary_present()) {
	print_info_box(gettext('amneziawg-go binary is not installed.'), 'warning', false);
}

?>
<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Service') ?></h2></div>
	<div class="panel-body">
		<p>
			<strong><?= gettext('State') ?>:</strong>
			<?php if ($running) { ?>
				<span class="text-success"><?= gettext('Running') ?></span>
			<?php } else { ?>
				<span class="text-danger"><?= gettext('Stopped') ?></span>
			<?php } ?>
		</p>
		<form method="post" class="form-inline">
			<button type="submit" name="restart" value="1" class="btn btn-warning">
				<i class="fa fa-refresh icon-embed-btn"></i> <?= gettext('Restart service') ?>
			</button>
		</form>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Interface') ?></h2></div>
	<div class="panel-body">
		<pre class="pre-scrollable"><?php
		$out = shell_exec('/sbin/ifconfig ' . escapeshellarg($ifn) . ' 2>&1');
		echo htmlspecialchars((string)$out, ENT_QUOTES | ENT_HTML401, 'UTF-8');
		?></pre>
	</div>
</div>

<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= gettext('Peers (UAPI)') ?></h2></div>
	<div class="panel-body">
		<?php if (!$uapi['ok']) { ?>
			<p class="text-muted"><?= htmlspecialchars($uapi['err']) ?></p>
		<?php } elseif ($parsed['peers'] === []) { ?>
			<p class="text-muted"><?= gettext('No peer data (daemon may still be handshaking).') ?></p>
		<?php } else { ?>
		<div class="table-responsive">
			<table class="table table-striped table-hover table-condensed">
				<thead>
					<tr>
						<th><?= gettext('Public key (hex prefix)') ?></th>
						<th><?= gettext('Endpoint') ?></th>
						<th><?= gettext('RX') ?></th>
						<th><?= gettext('TX') ?></th>
						<th><?= gettext('Keepalive') ?></th>
					</tr>
				</thead>
				<tbody>
				<?php foreach ($parsed['peers'] as $pr) {
					$pk = $pr['public_key'] ?? '';
					$pfx = strlen($pk) > 16 ? substr($pk, 0, 16) . '…' : $pk;
					?>
					<tr>
						<td><code><?= htmlspecialchars($pfx) ?></code></td>
						<td><?= htmlspecialchars($pr['endpoint'] ?? '') ?></td>
						<td><?= htmlspecialchars($pr['rx_bytes'] ?? '') ?></td>
						<td><?= htmlspecialchars($pr['tx_bytes'] ?? '') ?></td>
						<td><?= htmlspecialchars($pr['persistent_keepalive_interval'] ?? '') ?></td>
					</tr>
				<?php } ?>
				</tbody>
			</table>
		</div>
		<?php } ?>
	</div>
</div>

<?php include("foot.inc"); ?>
