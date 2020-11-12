<?php

require_once('settings.inc');

$evt = $_GET['event'];

switch ($evt) {
case 'led':
case 'siren_ctrl':
case 'smoke_sleep':
	$sock = fsockopen('unix://' . GUI_EVENT_SOCK);
	if ($sock) {
		fwrite($sock, $evt . "\n");
		fwrite($sock, $_GET['content'] . "\n");
		fclose($sock);
	}
	break;
case 'robot_say':
	$sock = fsockopen('unix://' . GUI_EVENT_SOCK);
	if ($sock) {
		fwrite($sock, 'audio_stream' . "\n");
		$len = strlen($_GET['content']);
		for ($i = 0; $i < $len; $i++) {
			$data = file_get_contents('robot_say/' . $_GET['content'][$i] . '.raw');
			if ($data != false) {
				fwrite($sock, $data);
			}
		}
		fclose($sock);
	}
	break;
}

?>
