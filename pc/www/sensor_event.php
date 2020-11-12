<?php

require_once('settings.inc');

$evt = $_GET['event'];

switch ($evt) {
case 'smoke_on':
case 'smoke_off':
case 'motion':
	$sock = fsockopen('unix://' . SENSOR_EVENT_SOCK);
	if ($sock) {
		fwrite($sock, $evt . "\n");
		fclose($sock);
	}
	break;
}

?>
