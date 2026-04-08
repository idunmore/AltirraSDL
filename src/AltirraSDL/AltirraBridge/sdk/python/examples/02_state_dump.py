#!/usr/bin/env python3
"""02_state_dump.py — Phase 2 state-read example.

Connects, advances 60 frames so the OS has booted past the memo-pad
screen, then dumps CPU + ANTIC + GTIA + POKEY + PIA state and walks
the active display list.

Run:
    1. ./AltirraSDL --bridge
    2. python 02_state_dump.py /tmp/altirra-bridge-<pid>.token
"""

import sys

from altirra_bridge import AltirraBridge


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <token-file>", file=sys.stderr)
        return 2

    with AltirraBridge.from_token_file(sys.argv[1]) as a:
        a.pause()
        a.frame(60)  # let the OS boot

        cpu = a.regs()
        print(f"CPU: PC={cpu['PC']} A={cpu['A']} X={cpu['X']} Y={cpu['Y']} "
              f"S={cpu['S']} P={cpu['P']} ({cpu['flags']})")
        print(f"     mode={cpu['mode']} cycles={cpu['cycles']}")

        antic = a.antic()
        print(f"\nANTIC: DLIST={antic['DLIST']} DMACTL={antic['DMACTL']} "
              f"NMIEN={antic['NMIEN']}")
        print(f"       beam=({antic['beam_x']},{antic['beam_y']})")

        gtia = a.gtia()
        colors = " ".join(f"{k}={gtia[k]}" for k in
                          ("COLPF0", "COLPF1", "COLPF2", "COLPF3", "COLBK"))
        print(f"\nGTIA:  {colors}")
        print(f"       PRIOR={gtia['PRIOR']} GRACTL={gtia['GRACTL']}")

        pokey = a.pokey()
        print(f"\nPOKEY: AUDCTL={pokey['AUDCTL']} IRQEN={pokey['IRQEN']} "
              f"SKCTL={pokey['SKCTL']}")

        pia = a.pia()
        print(f"\nPIA:   PORTA_OUT={pia['PORTA_OUT']} "
              f"PORTB_OUT={pia['PORTB_OUT']}")

        print(f"\nDisplay list ({len(a.dlist())} entries, first 5):")
        for entry in a.dlist()[:5]:
            tag = " LMS" if entry.get("lms") else ""
            tag += " DLI" if entry.get("dli") else ""
            print(f"  {entry['addr']}: {entry['kind']} mode={entry['mode']}"
                  f" pf={entry['pf']}{tag}")

        # Read the boot ROM signature at $E000.
        rom = a.peek(0xE000, 8)
        print(f"\nROM @ $E000: {rom.hex()}")

        # Sample of the analysis palette: a few well-known indices.
        pal = a.palette()
        print(f"\nPalette: 256 RGB24 entries ({len(pal)} bytes)")
        for idx in (0, 0x1A, 0x44, 0xA8, 0xFF):
            r, g, b = pal[idx*3:idx*3+3]
            print(f"  ${idx:02x}: #{r:02x}{g:02x}{b:02x}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
