"""Top-level orchestration — write_all, write_main, write_segment, verify.

Decides *what* to emit and *where*; delegates formatting to code.py
and data.py.
"""

from __future__ import annotations

import os
import subprocess
from typing import Dict, List, Optional, TYPE_CHECKING

from . import code as _code
from . import data as _data

if TYPE_CHECKING:
    from ..client import AltirraBridge
    from ..loader import XexImage, XexSegment
    from ..project import Project


def _build_program_memory(
    proj: "Project",
    image: "XexImage",
    reconstructed: bool,
) -> bytearray:
    """Build a flat 64KB ``bytearray`` view of the program in whichever
    address space the export mode walks through.

    In byte-exact mode this is just the raw segments placed at their
    XEX load addresses. In reconstructed mode, each copy source is
    overlaid at its runtime address (overwriting the original XEX-
    address bytes) so the analyzer sees the same code the walker will
    emit. Anything not covered by a segment or copy source stays zero.
    """
    mem = bytearray(65536)
    for seg in image.segments:
        for i, b in enumerate(seg.data):
            if 0 <= seg.start + i < 65536:
                mem[seg.start + i] = b
    if reconstructed:
        for cs in proj.iter_copy_sources():
            xs = cs["xex_start"]; xe = cs["xex_end"]
            rs = cs["runtime_start"]
            if xs is None or xe is None or rs is None:
                continue
            for si, seg in enumerate(image.segments):
                if seg.start <= xs and xe <= seg.end:
                    src_off = xs - seg.start
                    size = xe - xs + 1
                    for i in range(size):
                        if 0 <= rs + i < 65536:
                            mem[rs + i] = seg.data[src_off + i]
                    break
    return mem


def _compute_procedures(
    proj: "Project",
    image: "XexImage",
    reconstructed: bool,
) -> tuple:
    """Run recursive_descent + build_procedures to discover the
    program's call graph for ``.proc`` emission.

    Returns ``(procedures, xrefs)`` where ``procedures`` is the dict
    produced by :func:`altirra_bridge.analyzer.build_procedures` and
    ``xrefs`` is the list of ``{from, to, type}`` edges collected by
    :func:`altirra_bridge.analyzer.recursive_descent`. On any failure
    (e.g. analyzer unavailable, bad entry points) returns ``({}, [])``
    so the caller can degrade gracefully to flat output.
    """
    try:
        from ..analyzer.disasm import recursive_descent
        from ..analyzer.procedures import build_procedures
    except Exception:
        return {}, []

    mem = _build_program_memory(proj, image, reconstructed)

    # Seed entry points: the XEX RUN address and every project label
    # whose address lies inside a declared code region.
    entries: list = []
    if image.runad is not None:
        entries.append((image.runad, "entry"))
    if reconstructed:
        for cs in proj.iter_copy_sources():
            if cs.get("runtime_entry") is not None:
                entries.append((cs["runtime_entry"], "entry"))
    # Regions describe code/data layout; feed code-region starts as
    # additional entries to catch code the RUN path doesn't reach.
    code_regions: list = []
    data_regions: list = []
    for r in proj.regions:
        rs = r["start"] if isinstance(r["start"], int) else int(str(r["start"]).lstrip("$"), 16)
        re = r["end"]   if isinstance(r["end"], int)   else int(str(r["end"]).lstrip("$"), 16)
        if reconstructed:
            # Shift XEX-space regions that fall inside a copy source
            # to their runtime-space equivalents.
            for cs in proj.iter_copy_sources():
                xs = cs["xex_start"]; xe = cs["xex_end"]
                rs_cs = cs["runtime_start"]
                if xs is None or xe is None or rs_cs is None:
                    continue
                if xs <= rs and re <= xe:
                    rs += rs_cs - xs
                    re += rs_cs - xs
                    break
        reg = {"start": f"{rs:04X}", "end": f"{re:04X}", "type": r.get("type", "unknown")}
        if reg["type"] == "code":
            code_regions.append(reg)
            entries.append((rs, "code"))
        elif reg["type"] == "data":
            data_regions.append(reg)
    # Every project label gets seeded as a subroutine entry — the
    # walker will only treat those that are actually JSR'd or
    # branched to as real procedures.
    for addr, _name in proj.labels.items():
        entries.append((addr, "subroutine"))

    try:
        _new_regions, xrefs = recursive_descent(mem, entries)
    except Exception:
        return {}, []

    # build_procedures wants regions as dicts with hex-string
    # addresses and a type field, plus labels as
    # ``{hex_key: {name, type}}``.
    all_regions = code_regions + data_regions
    proc_labels = {
        f"{a:04X}": {"name": n, "type": "subroutine"}
        for a, n in proj.labels.items()
    }
    try:
        procedures = build_procedures(mem, xrefs, all_regions, proc_labels)
    except Exception:
        return {}, []
    return procedures, xrefs


def _build_proc_info(
    proj: "Project",
    procedures: dict,
    xrefs: list,
    seg_start: int,
    seg_end: int,
) -> Dict[int, dict]:
    """Filter ``procedures`` down to the subset that is safe to wrap
    in a MADS ``.proc``/``.endp`` block within ``[seg_start, seg_end]``.

    A procedure is "safe" when:

    * It has a clean ``rts`` / ``rti`` exit (no fall-through, no mixed
      exit paths).
    * Its body lies entirely within the current segment.
    * It has no anomaly flags from :func:`build_procedures`.
    * Its body does not overlap another accepted procedure.
    * It has a project label at its entry (we need a name).
    * Every project label inside its body is *only* referenced from
      inside that body — MADS ``.proc`` makes inner labels local, so
      any outside reference would fail to resolve.

    Returns ``{entry_addr → {name, end}}``. Absent procedures get the
    old flat-label output.
    """
    if not procedures:
        return {}

    # Build a map of addresses with external references for the
    # "inner label only used locally" check.
    def _addr_of(hex_pair: str) -> int:
        return int(hex_pair.split(":")[0], 16)

    proc_info: Dict[int, dict] = {}
    used_ranges: list = []

    for key, proc in sorted(procedures.items()):
        entry = proc["entry"] if isinstance(proc["entry"], int) \
                else int(str(proc["entry"]), 16)
        end_addr = proc["end"] if isinstance(proc["end"], int) \
                else int(str(proc["end"]), 16)

        if entry < seg_start or entry > seg_end:
            continue
        if end_addr > seg_end:
            continue
        if proc["exit_type"] not in ("rts", "rti"):
            continue
        if proc.get("flags"):
            continue

        # Overlap check against already-accepted procs
        overlaps = False
        for rs, re in used_ranges:
            if entry <= re and end_addr >= rs:
                overlaps = True
                break
        if overlaps:
            continue

        name = proj.labels.get(entry)
        if not name:
            continue

        body_addrs = set(range(entry, end_addr + 1))

        # Any xref from *outside* the body landing *inside* the body
        # at an address other than the entry disqualifies this proc.
        # MADS ``.proc`` makes every internal label local, so an
        # outside branch or JMP to a mid-body instruction would fail
        # to resolve — this covers both project labels AND synthetic
        # ``loc_XXXX`` targets that the walker will introduce later.
        inbound_ok = True
        for x in xrefs:
            to_addr = _addr_of(x["to"])
            from_addr = _addr_of(x["from"])
            if to_addr in body_addrs and to_addr != entry and from_addr not in body_addrs:
                inbound_ok = False
                break
        if not inbound_ok:
            continue

        proc_info[entry] = {"name": name, "end": end_addr}
        used_ranges.append((entry, end_addr))

    return proc_info


def _resolve_source_path(proj: "Project") -> Optional[str]:
    """Return the absolute path to the project's source XEX, or None.

    Relative ``source_path`` values are resolved against the directory
    of the loaded ``project.json`` so case studies remain portable
    across checkouts.
    """
    src = proj.source_path
    if not src:
        return None
    if not os.path.isabs(src) and proj._path:
        src = os.path.normpath(os.path.join(os.path.dirname(proj._path), src))
    return src


def write_all(
    bridge: "AltirraBridge",
    image: "XexImage",
    proj: "Project",
    output_dir: str,
    *,
    disasm_unclassified: bool = True,
    reconstructed: Optional[bool] = None,
    emit_procs: bool = True,
) -> str:
    """Generate complete MADS source file set.

    Creates ``main.asm``, ``equates.asm``, and per-segment files
    in ``output_dir``. Returns a text summary.

    Loads OS symbols and project labels into the bridge so
    ``DISASM`` output uses standard names. OS symbols are loaded
    first; project labels override them for repurposed addresses.

    ``reconstructed``:
      * ``None`` (default): auto-select — use reconstructed mode if
        the project has any ``copy_sources``, otherwise classic mode.
      * ``False``: byte-exact classic mode — emit segments at their
        XEX load addresses and round-trip the original XEX.
      * ``True``: relocation-aware mode — split each segment around
        declared copy-source ranges and emit the copy-source bytes
        at their **runtime** addresses in their own virtual segments.
        Labels/comments keyed at runtime addresses now line up with
        the generated asm. The output XEX layout differs from the
        original (extra segments at relocated addresses, RUN address
        may be replaced with ``runtime_entry``), so byte-exact round-
        trip with the original XEX is intentionally given up.
    """
    os.makedirs(output_dir, exist_ok=True)

    if reconstructed is None:
        reconstructed = bool(proj.copy_sources)

    # Load symbols into bridge: OS first, then project (project wins)
    from ..symbols import load_os_symbols
    try:
        load_os_symbols(bridge)
    except Exception:
        pass  # OS symbols are optional
    lab_path = os.path.join(output_dir, "_project.lab")
    proj.export_lab(lab_path)
    try:
        bridge.sym_load(lab_path)
    except Exception:
        pass

    files = []
    files.append(write_equates(image, proj, output_dir))

    # Analyzer pass: discover procedures for ``.proc``/``.endp`` wrapping.
    # Cheap (one recursive_descent + one build_procedures on a 64KB view)
    # and purely local — no bridge round-trips.
    procedures: Dict[int, dict] = {}
    xrefs: list = []
    if emit_procs:
        procedures, xrefs = _compute_procedures(proj, image,
                                                 reconstructed=bool(reconstructed and proj.copy_sources))

    if reconstructed and proj.copy_sources:
        vsegments = _build_reconstructed_segments(proj, image)
        for vseg in vsegments:
            files.append(_write_vsegment(bridge, proj, image, vseg, output_dir,
                                         procedures=procedures, xrefs=xrefs))
        files.append(_write_main_reconstructed(proj, image, vsegments, output_dir))
    else:
        for i in range(len(image.segments)):
            files.append(write_segment(bridge, proj, i, image, output_dir,
                                       procedures=procedures, xrefs=xrefs))
        files.append(write_main(proj, image, output_dir))

    mode_tag = "reconstructed" if reconstructed and proj.copy_sources else "byte-exact"
    lines = [f"=== MADS Export ({mode_tag}) ==="]
    for f in files:
        lines.append(f"  {f}")
    return "\n".join(lines)


def write_main(
    proj: "Project",
    image: "XexImage",
    output_dir: str,
) -> str:
    """Generate main.asm — master file with ICL + XEX headers.

    Uses ``opt h-`` (raw binary) with ORG bracket notation to emit
    XEX segment headers manually, giving byte-exact control over the
    layout including ``$FFFF`` marker placement.
    """
    xex_name = os.path.basename(proj.source_path or "output.xex")
    ffff_set = set(getattr(image, "ffff_positions", [0]))

    lines = [
        f"; Reconstructed from: {xex_name}",
        f"; Project: {proj.name}",
        "; Generated by altirra_bridge.asm_writer",
        f"; Assemble with: mads main.asm -o:{xex_name}",
        "",
        "    opt h-      ; raw binary — XEX headers via ORG brackets",
        "",
        "    icl 'equates.asm'",
        "",
    ]

    for i, seg in enumerate(image.segments):
        start, end = seg.start, seg.end
        filename = _segment_filename(i, start)

        lines.append(f"; === Segment {i}: ${start:04X}-${end:04X}"
                     f" ({seg.length} bytes) ===")

        if i in ffff_set:
            lines.append(f"    org [a($FFFF),a(${start:04X}),a(${end:04X})],${start:04X}")
        else:
            lines.append(f"    org [a(${start:04X}),a(${end:04X})],${start:04X}")

        lines.append(f"    icl '{filename}'")

        # INI segments (INITAD vectors that fire after this segment)
        # The loader associates initads with segments in load order.
        # For simplicity, attach them to the last data segment.
        lines.append("")

    # Emit INITAD vectors
    for ini_addr in image.initads:
        ini_name = proj.get_label(ini_addr) or f"${ini_addr:04X}"
        lines.append(f"; INI: {ini_name}")
        lines.append("    org [a($02E2),a($02E3)],$02E2")
        lines.append(f"    .word {ini_name}")
        lines.append("")

    # Emit RUNAD
    if image.runad is not None:
        run_name = proj.get_label(image.runad) or f"${image.runad:04X}"
        lines.append("; RUN address")
        lines.append("    org [a($02E0),a($02E1)],$02E0")
        lines.append(f"    .word {run_name}")
        lines.append("")

    # Trailing padding — some XEX files have DOS sector fill after
    # the last segment. Read the original file and emit any bytes
    # past the last parsed segment.
    src_path = _resolve_source_path(proj)
    if src_path and os.path.exists(src_path):
        with open(src_path, "rb") as f:
            raw = f.read()
        # Compute where our parsed segments end in the file
        parsed_size = _compute_parsed_size(raw, image)
        if parsed_size < len(raw):
            padding = raw[parsed_size:]
            lines.append(f"; Trailing padding ({len(padding)} bytes)")
            for j in range(0, len(padding), 16):
                chunk = padding[j:j+16]
                vals = ",".join(f"${b:02X}" for b in chunk)
                lines.append(f"    .byte {vals}")
            lines.append("")

    path = os.path.join(output_dir, "main.asm")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return "main.asm"


def write_equates(
    image: "XexImage",
    proj: "Project",
    output_dir: str,
) -> str:
    """Generate equates.asm — only symbols referenced by the binary.

    Scans segment bytes for operand addresses, collects OS/HW names
    from the official Atari XL OS symbol table, and emits equate
    definitions grouped by category.

    Project labels win over OS symbols at overlapping addresses: the
    SDK's ``write_segment`` pipeline substitutes project names for
    Altirra's OS names in the disassembly, so emitting the OS name
    here as well would produce a dead equate that nothing uses. Any
    address that has a project label is suppressed from the OS-name
    emission below, and the project label is emitted in its place.
    """
    from .._analyzer_tables import OPCODES, HW_READ, HW_WRITE
    from ..symbols import get_os_symbols

    os_syms = get_os_symbols()  # {addr: (name, comment)}

    # Addresses that the project has renamed. The project label
    # always wins, so these are suppressed from OS-symbol emission.
    project_label_addrs = set()
    for a in proj.labels.keys():
        if isinstance(a, str):
            a = int(a.lstrip("$"), 16)
        project_label_addrs.add(a)

    # Build set of addresses within segments (these become code labels, not equates).
    # In reconstructed mode, also include the runtime ranges declared via
    # ``mark_copy_source`` — those bytes get their own virtual segment at
    # the runtime address so any project label inside them will be emitted
    # as a real code label and must NOT also appear as an equate.
    seg_addrs = set()
    for seg in image.segments:
        for a in range(seg.start, seg.end + 1):
            seg_addrs.add(a)
    for cs in proj.iter_copy_sources():
        if cs["runtime_start"] is None or cs["xex_start"] is None:
            continue
        size = cs["xex_end"] - cs["xex_start"] + 1
        for a in range(cs["runtime_start"], cs["runtime_start"] + size):
            seg_addrs.add(a)

    # Scan segment bytes for referenced addresses
    referenced = set()
    for seg in image.segments:
        addr = 0
        while addr < len(seg.data):
            opcode = seg.data[addr]
            entry = OPCODES.get(opcode)
            if entry:
                _mnem, mode, size = entry
                if mode in ("abs", "abx", "aby", "ind") and addr + 2 < len(seg.data):
                    target = seg.data[addr + 1] | (seg.data[addr + 2] << 8)
                    referenced.add(target)
                elif mode in ("zp", "zpx", "zpy", "izx", "izy") and addr + 1 < len(seg.data):
                    referenced.add(seg.data[addr + 1])
                addr += size
            else:
                addr += 1

    # Group equates by category
    categories = {
        "zp": ("; Zero page", []),
        "page2": ("; Page 2 — vectors and shadows", []),
        "gtia": ("; GTIA", []),
        "pokey": ("; POKEY", []),
        "pia": ("; PIA", []),
        "antic": ("; ANTIC", []),
        "os": ("; OS ROM entry points", []),
    }
    emitted_names = set()

    for addr in sorted(referenced):
        if addr in seg_addrs:
            continue  # will be a code/data label
        # Project label wins — skip any OS name at this address.
        # The project label is emitted later in the game-variable
        # block so disassembly ``stx entity_timer`` resolves.
        if addr in project_label_addrs and not (0xD000 <= addr <= 0xD40F):
            continue

        # HW registers — emit both read and write names
        if 0xD000 <= addr <= 0xD01F:
            for name in (HW_READ.get(addr), HW_WRITE.get(addr)):
                if name and name not in emitted_names:
                    categories["gtia"][1].append(f"{name:12s} = ${addr:04X}")
                    emitted_names.add(name)
            continue
        if 0xD200 <= addr <= 0xD20F:
            for name in (HW_READ.get(addr), HW_WRITE.get(addr)):
                if name and name not in emitted_names:
                    categories["pokey"][1].append(f"{name:12s} = ${addr:04X}")
                    emitted_names.add(name)
            continue
        if 0xD300 <= addr <= 0xD303:
            cat = "pia"
        elif 0xD400 <= addr <= 0xD40F:
            for name in (HW_READ.get(addr), HW_WRITE.get(addr)):
                if name and name not in emitted_names:
                    categories["antic"][1].append(f"{name:12s} = ${addr:04X}")
                    emitted_names.add(name)
            continue
        elif addr < 0x100:
            cat = "zp"
        elif 0x200 <= addr < 0x400:
            cat = "page2"
        elif addr >= 0xE400:
            cat = "os"
        else:
            cat = "page2"

        # Look up name: prefer OS symbol table, fall back to HW tables
        os_entry = os_syms.get(addr)
        name = os_entry[0] if os_entry else None
        if not name:
            name = HW_READ.get(addr) or HW_WRITE.get(addr)
        if not name or not name[0].isupper():
            continue
        if name in emitted_names:
            continue

        line = f"{name:12s} = ${addr:04X}"
        # Add OS comment if available
        if os_entry and os_entry[1]:
            line += f"     ; {os_entry[1]}"

        categories[cat][1].append(line)
        emitted_names.add(name)

    # Add the hardware-register and OS-ROM OS symbols even if the
    # scan above didn't flag them — the segment-emit pipeline may
    # substitute them into operands (e.g. ``sta NMIEN``) and MADS
    # would otherwise fail on "Undeclared label". Zero-page and
    # page-2 OS symbols are intentionally NOT bulk-added anymore:
    # the new code-emission path never substitutes those, so
    # emitting them here would just be dead-weight equates.
    for addr, (name, comment) in sorted(os_syms.items()):
        if addr in seg_addrs or name in emitted_names:
            continue
        if addr in project_label_addrs and not (0xD000 <= addr <= 0xD40F):
            continue
        hw_or_rom = (
            (0xD000 <= addr <= 0xD01F)
            or (0xD200 <= addr <= 0xD20F)
            or (0xD300 <= addr <= 0xD303)
            or (0xD400 <= addr <= 0xD40F)
            or (addr >= 0xE400)
        )
        if not hw_or_rom:
            continue
        # Only emit base symbols (not aliases at the same address)
        if 0xD000 <= addr <= 0xD01F:
            cat = "gtia"
        elif 0xD200 <= addr <= 0xD20F:
            cat = "pokey"
        elif 0xD300 <= addr <= 0xD303:
            cat = "pia"
        elif 0xD400 <= addr <= 0xD40F:
            cat = "antic"
        elif addr < 0x100:
            cat = "zp"
        elif 0x200 <= addr < 0x400:
            cat = "page2"
        elif addr >= 0xE400:
            cat = "os"
        else:
            continue  # skip misc ranges
        line = f"{name:12s} = ${addr:04X}"
        if comment:
            line += f"     ; {comment}"
        categories[cat][1].append(line)
        emitted_names.add(name)

    # Project labels outside segments → game variable equates
    game_equates = []
    for addr, name in sorted(proj.labels.items()):
        if addr in seg_addrs or name in emitted_names:
            continue
        line = f"{name:12s} = ${addr:04X}"
        cmt = proj.get_comment(addr)
        if cmt:
            line += f"     ; {cmt}"
        game_equates.append(line)
        emitted_names.add(name)

    # Build output
    out_lines = [
        "; OS and hardware equates — referenced by this program",
        "; Generated by altirra_bridge.asm_writer",
        "",
    ]
    for _cat_key, (header, equates) in categories.items():
        if equates:
            out_lines.append(header)
            out_lines.extend(equates)
            out_lines.append("")
    if game_equates:
        out_lines.append("; Game variables")
        out_lines.extend(game_equates)
        out_lines.append("")

    path = os.path.join(output_dir, "equates.asm")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines) + "\n")
    return "equates.asm"


def write_segment(
    bridge: "AltirraBridge",
    proj: "Project",
    seg_index: int,
    image: "XexImage",
    output_dir: str,
    *,
    procedures: Optional[Dict[int, dict]] = None,
    xrefs: Optional[list] = None,
) -> str:
    """Generate one segment source file.

    Builds a region map from ``proj.regions``, then calls
    ``code.emit_mixed()`` to walk the segment respecting code/data
    boundaries. Passes ``proc_info`` down to the walker so
    analyzer-identified safe procedures land as ``.proc``/``.endp``
    blocks instead of flat labels.
    """
    seg = image.segments[seg_index]
    start, end = seg.start, seg.end
    filename = _segment_filename(seg_index, start)

    # Header
    lines = [
        f"; =============================================================",
        f"; Segment {seg_index}: ${start:04X}-${end:04X} ({seg.length} bytes)",
        f"; =============================================================",
        "",
        f"    org ${start:04X}",
        "",
    ]

    # Memory accessor for raw bytes — always reads from the XEX file
    # data, NOT from live emulator memory. This ensures data regions
    # and init segments that the game has overwritten at runtime still
    # produce the original bytes.
    class _SegMem:
        def __init__(self, data, base):
            self._data = data
            self._base = base
        def __getitem__(self, addr):
            off = addr - self._base
            if 0 <= off < len(self._data):
                return self._data[off]
            return 0

    mem = _SegMem(seg.data, start)

    # Ensure the XEX segment data is in emulator memory for disasm.
    # Some segments (like init code) are overwritten at runtime by
    # the game. Re-loading from the XEX ensures DISASM sees the
    # original bytes.
    bridge.memload(start, seg.data)

    # Build labels: all project labels for operand resolution,
    # but only emit definitions for labels within this segment.
    #
    # Operand resolution also uses the OS/HW symbol database so
    # references like ``sta WSYNC`` appear even when the project
    # didn't explicitly alias those addresses. Project labels
    # override OS names at overlapping addresses. OS symbols whose
    # address falls inside any segment are skipped — those are
    # in-program locations and should be named by the project or
    # by a synthesised ``loc_`` label instead.
    from ..symbols import get_os_symbols
    os_syms = get_os_symbols()  # {addr: (name, comment)}
    seg_ranges = [(s.start, s.end) for s in image.segments]

    def _in_any_segment(a: int) -> bool:
        return any(s <= a <= e for s, e in seg_ranges)

    def _is_equatable(a: int) -> bool:
        # Hardware register range and OS ROM entry points have
        # fixed hardware meaning — always safe to name. Zero-page
        # and page-2 locations are usually repurposed by games for
        # their own variables, so those addresses are intentionally
        # left to project labels only (raw hex is more honest than
        # an OS name that misdescribes a game variable).
        if 0xD000 <= a <= 0xD01F:
            return True
        if 0xD200 <= a <= 0xD20F:
            return True
        if 0xD300 <= a <= 0xD303:
            return True
        if 0xD400 <= a <= 0xD40F:
            return True
        if a >= 0xE400:
            return True
        return False

    all_labels: Dict[int, str] = {}
    for a, (n, _c) in os_syms.items():
        if _in_any_segment(a):
            continue
        if not _is_equatable(a):
            continue
        all_labels[a] = n
    all_labels.update(proj.labels)
    emit_labels = {a: n for a, n in proj.labels.items()
                   if start <= a <= end}

    # Build region map
    region_map = _code.build_region_map(proj, start, end)

    # Per-segment procedure info for .proc emission
    proc_info = _build_proc_info(proj, procedures or {}, xrefs or [], start, end)

    # Emit content
    content = _code.emit_mixed(
        bridge, proj, mem, start, end,
        region_map, emit_labels, all_labels,
        proc_info=proc_info)
    lines.append(content)

    path = os.path.join(output_dir, filename)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return filename


# ----------------------------------------------------------------------
# Reconstructed-mode (copy_sources) export
# ----------------------------------------------------------------------

def _build_reconstructed_segments(
    proj: "Project",
    image: "XexImage",
) -> List[dict]:
    """Split ``image.segments`` around declared copy-source ranges.

    For each XEX segment, slice out the byte ranges that the project
    marked as copy sources and give them their own virtual segment at
    the runtime address. The parts of the segment outside every copy
    source stay at their original XEX load address.

    Each returned vseg is a dict:

    ==================  =============================================
    ``seg_index``       index into ``image.segments`` this slice came from
    ``start``/``end``   runtime (inclusive) address range of the slice
    ``data``            raw bytes (slice of the segment's ``data``)
    ``is_runtime``      True if this slice is a relocated copy source
    ``ffff``            True if this slice should lead with a ``$FFFF``
                        XEX marker (only the first slice of a segment
                        that had a marker in the original)
    ``init_addrs``      list of INITAD vectors to attach to this slice
                        (only the trailing non-runtime slice of each
                        segment carries the original segment's inits;
                        runtime slices get none)
    ``copy_source``     the copy-source dict this slice comes from
                        (runtime slices only)
    ``purpose``         short human-readable tag for the header comment
    ==================  =============================================
    """
    # Build seg_index → list of (xex_start, xex_end, runtime_start, cs_dict)
    # for the copy sources overlapping that segment.
    copy_map: Dict[int, list] = {}
    for cs in proj.iter_copy_sources():
        xs = cs["xex_start"]
        xe = cs["xex_end"]
        if xs is None or xe is None or cs["runtime_start"] is None:
            continue
        for si, seg in enumerate(image.segments):
            if seg.start <= xs and xe <= seg.end:
                copy_map.setdefault(si, []).append(cs)
                break
        else:
            # Copy source doesn't fall inside any single segment —
            # skip it rather than split across segments.
            continue

    ffff_set = set(getattr(image, "ffff_positions", [0]))
    excludes = list(proj.iter_reconstructed_excludes())

    def _is_excluded(lo: int, hi: int) -> bool:
        """True if ``[lo..hi]`` is entirely inside any exclusion range."""
        for ex_lo, ex_hi in excludes:
            if ex_lo <= lo and hi <= ex_hi:
                return True
        return False

    vsegments: List[dict] = []

    for si, seg in enumerate(image.segments):
        # Fully-excluded segments are dropped entirely (bootstrap code,
        # init-code segments, the INITAD vector for the relocator, etc.).
        if _is_excluded(seg.start, seg.end):
            continue
        seg_inits = [a for a in image.initads] if si == len(image.segments) - 1 else []
        # ^ The XEX loader only runs INITAD between segments; without
        #   richer metadata we conservatively attach all of image.initads
        #   to the last original segment's trailing slice.
        is_first_chunk = True

        if si not in copy_map:
            vsegments.append({
                "seg_index":   si,
                "start":       seg.start,
                "end":         seg.end,
                "data":        seg.data,
                "is_runtime":  False,
                "ffff":        si in ffff_set,
                "init_addrs":  list(seg_inits),
                "copy_source": None,
                "purpose":     "",
            })
            continue

        sources = sorted(copy_map[si], key=lambda c: c["xex_start"])
        cursor = seg.start

        for cs in sources:
            xs = cs["xex_start"]
            xe = cs["xex_end"]
            rs = cs["runtime_start"]
            runtime_size = xe - xs + 1
            runtime_end = rs + runtime_size - 1

            # Part A: bytes before this copy source (still XEX-addressed).
            # Skip if the whole pre-copy range is marked as excluded —
            # this drops the relocator body for games where the user
            # declared it as bootstrap.
            if cursor < xs and not _is_excluded(cursor, xs - 1):
                part_a = seg.data[cursor - seg.start : xs - seg.start]
                vsegments.append({
                    "seg_index":   si,
                    "start":       cursor,
                    "end":         xs - 1,
                    "data":        part_a,
                    "is_runtime":  False,
                    "ffff":        is_first_chunk and (si in ffff_set),
                    "init_addrs":  [],
                    "copy_source": None,
                    "purpose":     "pre-relocation (bootstrap)",
                })
                is_first_chunk = False

            # Part B: the copy source at its runtime address
            part_b = seg.data[xs - seg.start : xe - seg.start + 1]
            vsegments.append({
                "seg_index":   si,
                "start":       rs,
                "end":         runtime_end,
                "data":        part_b,
                "is_runtime":  True,
                "ffff":        is_first_chunk and (si in ffff_set),
                "init_addrs":  [],
                "copy_source": cs,
                "purpose":     f"runtime code (copied from ${xs:04X}-${xe:04X})",
            })
            is_first_chunk = False
            cursor = xe + 1

        # Part C: bytes after the last copy source
        if cursor <= seg.end:
            part_c = seg.data[cursor - seg.start:]
            vsegments.append({
                "seg_index":   si,
                "start":       cursor,
                "end":         seg.end,
                "data":        part_c,
                "is_runtime":  False,
                "ffff":        is_first_chunk and (si in ffff_set),
                "init_addrs":  list(seg_inits),
                "copy_source": None,
                "purpose":     "post-relocation tail",
            })
        else:
            # Nothing past the last copy source; INITADs, if any, have
            # nowhere to live in this split. Attach them to the last
            # runtime slice so they still fire — they'll reference the
            # same vector either way.
            if seg_inits and vsegments:
                vsegments[-1]["init_addrs"].extend(seg_inits)

    return vsegments


def _vseg_filename(vseg: dict) -> str:
    tag = "run" if vseg["is_runtime"] else "xex"
    return f"seg_{vseg['seg_index']:02d}_{tag}_{vseg['start']:04X}.asm"


def _write_vsegment(
    bridge: "AltirraBridge",
    proj: "Project",
    image: "XexImage",
    vseg: dict,
    output_dir: str,
    *,
    procedures: Optional[Dict[int, dict]] = None,
    xrefs: Optional[list] = None,
) -> str:
    """Write one virtual segment source file (for reconstructed mode).

    Works like :func:`write_segment` but operates on the already-sliced
    ``vseg`` dict. For runtime slices the ORG is the *runtime* address,
    so all project labels/comments keyed at runtime addresses naturally
    align with the walker's ``pc``.
    """
    start = vseg["start"]
    end = vseg["end"]
    data = vseg["data"]
    is_runtime = vseg["is_runtime"]
    filename = _vseg_filename(vseg)

    if is_runtime:
        cs = vseg["copy_source"] or {}
        xs = cs.get("xex_start") or 0
        xe = cs.get("xex_end") or 0
        header = [
            "; =============================================================",
            f"; Runtime segment: ${start:04X}-${end:04X} "
            f"(from XEX ${xs:04X}-${xe:04X})",
            "; =============================================================",
            "; Copied by the game's relocator at boot. In reconstructed",
            "; export mode these bytes are emitted at their runtime",
            "; address so labels and comments line up with the code.",
        ]
    else:
        header = [
            "; =============================================================",
            f"; Segment {vseg['seg_index']}: ${start:04X}-${end:04X}"
            f" ({len(data)} bytes){' — ' + vseg['purpose'] if vseg['purpose'] else ''}",
            "; =============================================================",
        ]

    lines = header + ["", f"    org ${start:04X}", ""]

    # Memory accessor — read from the slice, not live memory.
    class _SegMem:
        def __init__(self, d, base):
            self._d = d
            self._base = base
        def __getitem__(self, addr):
            off = addr - self._base
            if 0 <= off < len(self._d):
                return self._d[off]
            return 0

    mem = _SegMem(data, start)

    # Place the slice in emulator memory at the same address the
    # walker iterates (``start``). For a runtime slice that's the
    # runtime address, so the bridge's DISASM sees correct bytes at
    # the runtime addresses and operand resolution uses the right
    # symbols.
    bridge.memload(start, data)

    # Labels — same convention as write_segment.
    from ..symbols import get_os_symbols
    os_syms = get_os_symbols()
    # When we check "is this address inside a segment" we use the
    # *virtual* segment list we're about to emit, not the raw XEX
    # segments, so a symbol at a runtime address still gets treated
    # as "in-program" and doesn't get a duplicate equate.
    vseg_ranges: List[tuple] = []
    # Build from the global vsegments — caller passes us just one
    # vseg, so reconstruct the whole virtual address map from the
    # project. We approximate by trusting image.segments for the
    # bootstrap parts and the copy_sources for runtime parts.
    for s in image.segments:
        vseg_ranges.append((s.start, s.end))
    for cs in proj.iter_copy_sources():
        if cs["runtime_start"] is None or cs["xex_start"] is None:
            continue
        size = cs["xex_end"] - cs["xex_start"] + 1
        vseg_ranges.append((cs["runtime_start"],
                            cs["runtime_start"] + size - 1))

    def _in_any_segment(a: int) -> bool:
        return any(s <= a <= e for s, e in vseg_ranges)

    def _is_equatable(a: int) -> bool:
        if 0xD000 <= a <= 0xD01F: return True
        if 0xD200 <= a <= 0xD20F: return True
        if 0xD300 <= a <= 0xD303: return True
        if 0xD400 <= a <= 0xD40F: return True
        if a >= 0xE400:           return True
        return False

    all_labels: Dict[int, str] = {}
    for a, (n, _c) in os_syms.items():
        if _in_any_segment(a):
            continue
        if not _is_equatable(a):
            continue
        all_labels[a] = n
    all_labels.update(proj.labels)

    emit_labels = {a: n for a, n in proj.labels.items()
                   if start <= a <= end}

    # Build a region map for ``[start, end]``. For runtime vsegs we
    # translate every project region from XEX-space to runtime-space
    # using the copy source's offset, so a region classified as data
    # at XEX ``$5876-$60FF`` becomes a data region at runtime
    # ``$B776-$BFFF`` and the walker stops trying to disassemble it.
    # The translated region dicts must carry runtime-space ``start``
    # and ``end`` so ``emit_mixed``'s block-extent logic matches the
    # walker's ``pc``.
    if is_runtime:
        cs = vseg["copy_source"] or {}
        xs = cs.get("xex_start")
        rs = cs.get("runtime_start")
        offset = (rs - xs) if (xs is not None and rs is not None) else 0
        region_map: Dict[int, dict] = {}
        for r in proj.regions:
            r_rs = _code._parse_addr(r["start"]) + offset
            r_re = _code._parse_addr(r["end"])   + offset
            if r_re < start or r_rs > end:
                continue
            # Shift the dict so the walker sees runtime-space bounds.
            shifted = dict(r)
            shifted["start"] = max(r_rs, start)
            shifted["end"]   = min(r_re, end)
            for a in range(shifted["start"], shifted["end"] + 1):
                region_map[a] = shifted
    else:
        region_map = _code.build_region_map(proj, start, end)

    # Per-vseg procedure info — the analyzer already computed
    # procedures in whichever address space matches this export
    # mode, so the same ``{entry_addr → proc}`` dict filters down
    # cleanly for runtime vsegs and XEX-addressed vsegs alike.
    proc_info = _build_proc_info(proj, procedures or {}, xrefs or [], start, end)

    content = _code.emit_mixed(
        bridge, proj, mem, start, end,
        region_map, emit_labels, all_labels,
        proc_info=proc_info)
    lines.append(content)

    path = os.path.join(output_dir, filename)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return filename


def _write_main_reconstructed(
    proj: "Project",
    image: "XexImage",
    vsegments: List[dict],
    output_dir: str,
) -> str:
    """Write main.asm for a reconstructed multi-segment export.

    Each vseg becomes its own XEX loadable segment at its virtual
    ``start``/``end``. If any copy source carried a ``runtime_entry``
    we replace the XEX RUN address with it, so the reconstructed XEX
    boots straight into the runtime code instead of going through the
    now-broken relocator (whose copy source lives at different XEX
    offsets in this layout).
    """
    xex_name = os.path.basename(proj.source_path or "output.xex")

    runtime_entry: Optional[int] = None
    for cs in proj.iter_copy_sources():
        if cs.get("runtime_entry") is not None:
            runtime_entry = cs["runtime_entry"]
            break

    lines = [
        f"; Reconstructed from: {xex_name}",
        f"; Project: {proj.name}",
        "; Generated by altirra_bridge.asm_writer (reconstructed mode)",
        "; Copy-source ranges are emitted at their runtime addresses",
        "; so project labels/comments line up with the code. The XEX",
        "; layout differs from the original — this is NOT a byte-exact",
        "; round-trip.",
        f"; Assemble with: mads main.asm -o:{xex_name}",
        "",
        "    opt h-      ; raw binary — XEX headers via ORG brackets",
        "",
        "    icl 'equates.asm'",
        "",
    ]

    for vseg in vsegments:
        start = vseg["start"]
        end = vseg["end"]
        filename = _vseg_filename(vseg)
        tag = "Runtime" if vseg["is_runtime"] else f"Segment {vseg['seg_index']}"
        purpose = f" — {vseg['purpose']}" if vseg["purpose"] else ""
        lines.append(f"; === {tag}: ${start:04X}-${end:04X}{purpose} ===")
        if vseg["ffff"]:
            lines.append(f"    org [a($FFFF),a(${start:04X}),a(${end:04X})],${start:04X}")
        else:
            lines.append(f"    org [a(${start:04X}),a(${end:04X})],${start:04X}")
        lines.append(f"    icl '{filename}'")

        for ini_addr in vseg.get("init_addrs", []):
            if ini_addr is None:
                continue
            ini_name = proj.get_label(ini_addr) or f"${ini_addr:04X}"
            lines.append("")
            lines.append(f"; INI: {ini_name}")
            lines.append("    org [a($02E2),a($02E3)],$02E2")
            lines.append(f"    .word {ini_name}")
        lines.append("")

    # RUN: prefer runtime_entry over the XEX's original RUN address
    run_addr = runtime_entry if runtime_entry is not None else image.runad
    if run_addr is not None:
        run_name = proj.get_label(run_addr) or f"${run_addr:04X}"
        lines.append("; RUN address")
        lines.append("    org [a($02E0),a($02E1)],$02E0")
        lines.append(f"    .word {run_name}")
        lines.append("")

    path = os.path.join(output_dir, "main.asm")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return "main.asm"


def verify(
    proj: "Project",
    output_dir: str,
    mads_path: str = "mads",
) -> str:
    """Assemble with MADS and compare binary output to original XEX.

    Returns a text report: ``"VERIFIED: byte-exact match"`` or a
    mismatch description.
    """
    main_asm = os.path.join(output_dir, "main.asm")
    if not os.path.exists(main_asm):
        return "Error: main.asm not found. Run write_all() first."

    xex_name = os.path.basename(proj.source_path or "output.xex")
    out_xex = os.path.join(output_dir, xex_name)

    try:
        result = subprocess.run(
            [mads_path, "main.asm", f"-o:{xex_name}"],
            capture_output=True, text=True, timeout=30,
            cwd=output_dir,
        )
    except FileNotFoundError:
        return f'Error: MADS not found at "{mads_path}".'
    except subprocess.TimeoutExpired:
        return "Error: MADS assembly timed out."

    if result.returncode != 0:
        return f"Assembly FAILED:\n{result.stdout}\n{result.stderr}"

    if not os.path.exists(out_xex):
        return "Assembly succeeded but output file not found."

    src_path = _resolve_source_path(proj)
    if not src_path or not os.path.exists(src_path):
        return f"Assembly succeeded → {out_xex}\n(Cannot verify: original XEX not available)"

    with open(src_path, "rb") as f:
        original = f.read()
    with open(out_xex, "rb") as f:
        assembled = f.read()

    if original == assembled:
        return f"VERIFIED: byte-exact match ({len(original)} bytes)"

    report = [f"MISMATCH: original={len(original)} assembled={len(assembled)}"]
    for i in range(min(len(original), len(assembled))):
        if original[i] != assembled[i]:
            report.append(f"  First diff at offset ${i:04X}: "
                          f"original=${original[i]:02X} assembled=${assembled[i]:02X}")
            break
    return "\n".join(report)


# --- helpers ---

def _segment_filename(seg_index: int, start_addr: int) -> str:
    return f"seg_{seg_index:02d}_{start_addr:04X}.asm"


def _compute_parsed_size(raw: bytes, image) -> int:
    """Compute how many bytes of the raw XEX file are accounted for
    by the parsed segments + INITAD/RUNAD vectors. The remainder
    is trailing padding."""
    pos = 0
    n = len(raw)
    while pos < n:
        if n - pos < 4:
            break
        w = raw[pos] | (raw[pos + 1] << 8)
        if w == 0xFFFF:
            pos += 2
            continue
        start = w
        end = raw[pos + 2] | (raw[pos + 3] << 8)
        if end < start:
            break  # invalid header — padding starts HERE (before the 4 bytes)
        length = end - start + 1
        pos += 4
        if pos + length > n:
            break
        pos += length
    return pos
