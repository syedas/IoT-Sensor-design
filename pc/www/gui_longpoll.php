<?php

require_once('settings.inc');

$sock = fsockopen('unix://' . GUI_LONGPOLL_SOCK);

if ($sock) {
	if (array_key_exists('token', $_GET)) {
		fwrite($sock, $_GET['token']);
	}
	
	fwrite($sock, "\n");
	fflush($sock);
	
	fpassthru($sock);
}

?>
