/*
 * 01_ping.c — minimal AltirraBridge C SDK example.
 *
 * Connects to a running AltirraSDL launched with --bridge, sends PING,
 * runs 60 frames, sends PING again, and exits.
 *
 * Build:
 *   cc -std=c99 -I../  01_ping.c ../altirra_bridge.c -o 01_ping
 *   (on Windows: cl /I.. 01_ping.c ..\altirra_bridge.c ws2_32.lib)
 *
 * Run:
 *   1. In one terminal: ./AltirraSDL --bridge
 *      It logs lines like:
 *        [bridge] listening on tcp:127.0.0.1:54321
 *        [bridge] token-file: /tmp/altirra-bridge-12345.token
 *   2. In another terminal: ./01_ping /tmp/altirra-bridge-12345.token
 */

#include "altirra_bridge.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <token-file>\n", argv[0]);
		fprintf(stderr,
			"  The token file path is logged on stderr by AltirraSDL\n"
			"  when launched with --bridge, e.g. /tmp/altirra-bridge-<pid>.token\n");
		return 2;
	}

	atb_client_t* c = atb_create();
	if (!c) {
		fprintf(stderr, "atb_create: out of memory\n");
		return 1;
	}

	if (atb_connect_token_file(c, argv[1]) != ATB_OK) {
		fprintf(stderr, "connect/hello failed: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}
	printf("connected. server said: %s\n", atb_last_response(c));

	if (atb_ping(c) != ATB_OK) {
		fprintf(stderr, "ping failed: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}
	printf("ping ok\n");

	printf("running 60 frames...\n");
	if (atb_frame(c, 60) != ATB_OK) {
		fprintf(stderr, "frame failed: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}
	printf("frame returned: %s\n", atb_last_response(c));

	if (atb_ping(c) != ATB_OK) {
		fprintf(stderr, "second ping failed: %s\n", atb_last_error(c));
		atb_close(c);
		return 1;
	}
	printf("ping ok (after frame step)\n");

	atb_close(c);
	return 0;
}
