#!/usr/bin/env python3
"""01_hello.py — minimal AltirraBridge Python SDK example.

Connects to a running AltirraSDL launched with --bridge, sends PING,
runs 60 frames, sends PING again, and exits.

Run:
    1. In one terminal: ./AltirraSDL --bridge
       It logs lines like:
         [bridge] listening on tcp:127.0.0.1:54321
         [bridge] token-file: /tmp/altirra-bridge-12345.token
    2. In another terminal:
         python 01_hello.py /tmp/altirra-bridge-12345.token
"""

import sys

from altirra_bridge import AltirraBridge


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <token-file>", file=sys.stderr)
        print(
            "  The token file path is logged on stderr by AltirraSDL\n"
            "  when launched with --bridge, e.g. /tmp/altirra-bridge-<pid>.token",
            file=sys.stderr,
        )
        return 2

    with AltirraBridge.from_token_file(sys.argv[1]) as a:
        # The HELLO response carries the protocol version and current pause state.
        # AltirraBridge stores it implicitly; you can issue a PING to confirm.
        print("connected.")

        a.ping()
        print("ping ok")

        print("running 60 frames...")
        resp = a.frame(60)
        print(f"frame returned: {resp}")

        a.ping()
        print("ping ok (after frame step)")

    return 0


if __name__ == "__main__":
    sys.exit(main())
