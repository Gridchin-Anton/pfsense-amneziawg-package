<?php
##|+PRIV
##|*IDENT=page-vpn-amneziawg-logs
##|*NAME=VPN: AmneziaWG Logs
##|*DESCR=View AmneziaWG-related log lines.
##|*MATCH=pkg/amneziawg/vpn_amneziawg_log.php
##|-PRIV

require_once("guiconfig.inc");
require_once("authgui.inc");
require_once("/usr/local/www/pkg/amneziawg/amneziawg.inc");

$pgtitle = [gettext('VPN'), gettext('AmneziaWG'), gettext('Logs')];
$tab_array = [];
$tab_array[] = [gettext('Settings'), false, '/pkg/amneziawg/vpn_amneziawg.php'];
$tab_array[] = [gettext('Status'), false, '/pkg/amneziawg/vpn_amneziawg_status.php'];
$tab_array[] = [gettext('Logs'), true];

$lines = 80;
$tif = array_merge(amneziawg_default_tunnel(), amneziawg_get_config()['tunnel'] ?? [])['ifname'];
$grep = '(amneziawg-go|AmneziaWG|\\(' . preg_quote($tif, '/') . '\\))';
$cmd = '/usr/bin/grep -i -E ' . escapeshellarg($grep) . ' /var/log/system.log 2>/dev/null | /usr/bin/tail -n ' . (int)$lines;
$out = shell_exec($cmd);

include("head.inc");
display_top_tabs($tab_array);
?>
<div class="panel panel-default">
	<div class="panel-heading"><h2 class="panel-title"><?= sprintf(gettext('Last %d matching lines from system.log'), $lines) ?></h2></div>
	<div class="panel-body">
		<pre class="pre-scrollable" style="max-height: 480px;"><?=
		htmlspecialchars((string)$out, ENT_QUOTES | ENT_HTML401, 'UTF-8') ?: gettext('(no lines found — logging may be silent or tag differs)')
		?></pre>
		<p class="text-muted"><?= gettext('Daemon uses LOG_LEVEL; verbose/debug emit more to syslog. Adjust on the Settings tab.') ?></p>
	</div>
</div>
<?php include("foot.inc"); ?>
