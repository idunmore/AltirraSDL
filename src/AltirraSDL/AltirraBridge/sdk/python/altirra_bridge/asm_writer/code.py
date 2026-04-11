"""Code emission — disassemble via bridge and format as MADS source.

This is the only module that calls ``bridge.disasm()``.
"""

from __future__ import annotations
from typing import Dict, List, Optional, Set, Tuple, TYPE_CHECKING

from . import data as _data

if TYPE_CHECKING:
    from ..client import AltirraBridge
    from ..project import Project


def strip_inline_address(
    text: str,
    project_labels: Optional[Dict[int, str]] = None,
) -> str:
    """Clean Altirra DISASM output for MADS emission.

    Altirra returns lines like::

        LBL  cpx COLINC  [$7A] = $00

    Two non-obvious details drive this function:

    1. The ``[$7A]`` annotation is Altirra's ground-truth resolved
       address for this instruction's operand. We use it — NOT
       ``sym_resolve`` — to substitute the symbolic operand, because
       Altirra's built-in OS database can disagree with the SDK's
       OS lab file about where a name lives (e.g. ``COLINC`` is
       $7A in Altirra but $02F9 in the SDK's symbols).
    2. Altirra prefers its built-in OS names over ``SYM_LOAD``ed
       project labels for zero-page / OS addresses, so we always
       need to rewrite the operand ourselves if we want project
       names to appear.

    The output has its leading label column stripped (we emit our
    own labels) and the trailing annotation removed. Operand
    symbols are replaced with project labels when available, and
    with raw ``$hex`` otherwise.
    """
    import re
    s = text.strip()

    # Capture the [$addr] annotation if present — ground truth for
    # the operand's resolved address.
    annot_match = re.search(
        r'\[\$([0-9A-Fa-f]+)\](\s*=\s*\$[0-9A-Fa-f]+)?$', s)
    annot_addr: Optional[int] = None
    if annot_match:
        try:
            annot_addr = int(annot_match.group(1), 16)
        except ValueError:
            annot_addr = None
        s = s[:annot_match.start()].rstrip()

    # Drop leading symbolic label column.  Altirra emits an uppercase
    # label >=2 chars followed by spaces, then the lowercase mnemonic.
    parts = s.split(None, 1)
    if len(parts) == 2 and parts[0].isupper() and len(parts[0]) >= 2:
        rest = parts[1].lstrip()
        if rest[:3].islower() and rest[:3].isalpha():
            s = rest

    # Replace Altirra-resolved uppercase symbols in the operand.
    # When we have the [$addr] annotation, prefer it (authoritative).
    # Otherwise fall back to sym_resolve via the deresolver.
    s = _deresolver.sub(
        s,
        project_labels=project_labels,
        annot_addr=annot_addr,
    )

    # Second pass: substitute any remaining raw $hex operand with a
    # project label when one exists at that address.
    if project_labels:
        s = _substitute_hex_with_labels(s, project_labels)

    return s


def _substitute_hex_with_labels(
    s: str,
    project_labels: Dict[int, str],
) -> str:
    """Replace ``$XX`` / ``$XXXX`` operands with project label names.

    Only the operand portion is touched (everything after the first
    whitespace). Hex inside immediate operands (``#$42``) is left
    alone because those are values, not addresses.
    """
    import re
    parts = s.split(None, 1)
    if len(parts) < 2:
        return s
    mnem, operand = parts[0], parts[1]

    def _repl(m: "re.Match[str]") -> str:
        full = m.group(0)
        # Immediate values start with '#' — never a label.
        if full.startswith("#"):
            return full
        hex_str = m.group(1)
        try:
            addr = int(hex_str, 16)
        except ValueError:
            return full
        name = project_labels.get(addr)
        if not name:
            return full
        # Preserve any suffix like ',X' or ')' that followed the hex.
        return full.replace(f"${hex_str}", name, 1)

    # Match $HH or $HHHH (not inside a longer hex token) that is
    # either at the start of the operand or preceded by ( / , / space.
    new_operand = re.sub(
        r'(?:(?<=^)|(?<=[(,\s]))\$([0-9A-Fa-f]{1,4})(?![0-9A-Fa-f])',
        _repl,
        operand,
    )
    return f"{mnem} {new_operand}"


class _SymbolDeresolverHelper:
    """Rewrite Altirra-resolved symbol references in operands.

    Altirra's DISASM output contains resolved OS symbols like
    ``DOSINI``, ``FR1+3``, ``CASINI+1``. MADS needs either a
    matching equate or something it can assemble directly. The
    deresolver converts those uppercase symbols to:

    * a project label name, if one exists at the resolved address
      (so ``stx FREQ`` becomes ``stx entity_timer`` when the
      project renames $40); or
    * a raw ``$XX`` / ``$XXXX`` address otherwise.

    Cached per-session; the bridge is consulted once per unique
    symbol.
    """

    def __init__(self):
        self._cache: dict = {}   # symbol_name → addr (int) or None
        self._bridge = None

    def set_bridge(self, bridge):
        """Set the bridge for symbol resolution."""
        self._bridge = bridge
        self._cache.clear()

    def _resolve(self, name: str) -> Optional[int]:
        """Resolve a symbol name to an address via bridge."""
        if name in self._cache:
            return self._cache[name]
        if self._bridge is None:
            self._cache[name] = None
            return None
        try:
            result = self._bridge.sym_resolve(name)
            addr = result if isinstance(result, int) else int(result)
            self._cache[name] = addr
            return addr
        except Exception:
            self._cache[name] = None
            return None

    def sub(
        self,
        s: str,
        project_labels: Optional[Dict[int, str]] = None,
        annot_addr: Optional[int] = None,
    ) -> str:
        """Replace symbol operands in disassembly text.

        When ``annot_addr`` is given, it is the authoritative
        resolved address from Altirra's ``[$XX]`` annotation and is
        used directly — avoiding symbol-name collisions between
        Altirra's built-in OS DB and the SDK's OS lab file.

        When ``project_labels`` is given, a resolved address that
        has a project label is rewritten to the label name; otherwise
        it is rewritten to raw hex.
        """
        import re
        parts = s.split(None, 1)
        if len(parts) < 2:
            return s
        mnem, operand = parts[0], parts[1]

        def _format(addr: int) -> str:
            if project_labels:
                name = project_labels.get(addr)
                if name:
                    return name
            if addr < 0x100:
                return f"${addr:02X}"
            return f"${addr:04X}"

        def _repl(m):
            full = m.group(0)
            base_name = m.group(1)
            offset_str = m.group(2) or ""

            # Skip register names and known tokens
            if base_name in ("X", "Y", "A", "S"):
                return full

            # Ground-truth address from Altirra annotation overrides
            # sym_resolve. Only applicable when the operand has no
            # explicit +/- offset, since the annotation is already
            # post-offset resolved.
            if annot_addr is not None and not offset_str:
                return _format(annot_addr)

            addr = self._resolve(base_name)
            if addr is None:
                return full  # can't resolve — keep as-is

            # Apply offset
            if offset_str:
                op = offset_str[0]
                try:
                    off = int(offset_str[1:])
                except ValueError:
                    return full
                addr = (addr + off) if op == "+" else (addr - off)

            return _format(addr)

        # Match uppercase symbols NOT preceded by $ (to avoid
        # replacing hex digits inside $XXXX addresses).
        new_operand = re.sub(
            r'(?<!\$)(?<![0-9A-Fa-f])([A-Z_][A-Z_][A-Z0-9_]*)([+-]\d+)?',
            _repl,
            operand
        )
        # Fix double-dollar from "#$" prefix + "$XX" replacement
        new_operand = new_operand.replace("#$$", "#$")
        # Fix "($$" from indirect addressing
        new_operand = new_operand.replace("($$", "($")
        return f"{mnem} {new_operand}"


_deresolver = _SymbolDeresolverHelper()


def emit_code_range(
    bridge: "AltirraBridge",
    start: int,
    end: int,
    labels: Dict[int, str],
    comments: Dict[int, str],
    *,
    procedure_starts: Optional[Set[int]] = None,
    resolve_labels: Optional[Dict[int, str]] = None,
    proc_info: Optional[Dict[int, dict]] = None,
) -> List[str]:
    """Disassemble [start, end] via bridge and return MADS source lines.

    Each instruction's mnemonic + addressing mode comes from the
    OPCODES table, and the operand is computed directly from the
    instruction bytes — this avoids the symbol-collision and
    effective-address issues that plague post-processing of
    Altirra's DISASM text. Illegal opcodes (which the OPCODES
    table doesn't cover) fall back to Altirra's disassembler so
    we still get a helpful comment next to the raw bytes.

    ``procedure_starts`` (optional) is a set of addresses that
    should get a blank-line separator above their label, to make
    procedure boundaries visible in the output.
    """
    from .._analyzer_tables import OPCODES
    _deresolver.set_bridge(bridge)
    procedure_starts = procedure_starts or set()
    proc_info = proc_info or {}
    # ``labels`` holds in-range entries that should be printed as
    # "NAME:" when the walker reaches their address. ``resolve``
    # holds ALL known labels (including zero-page / out-of-range
    # entries) so that operand substitution picks up names like
    # ``entity_timer`` at $40 even when walking a $4080-$60FF range.
    resolve = resolve_labels or labels

    # Read segment bytes once — avoid peek() per-instruction chatter.
    # The bridge caps PEEK at 16384 bytes, so chunk larger ranges.
    seg_bytes = bytearray()
    want = end - start + 1
    pos = 0
    while pos < want:
        chunk = bridge.peek(start + pos, min(16384, want - pos))
        seg_bytes.extend(chunk)
        pos += len(chunk)
        if not chunk:
            break

    def _b(addr: int) -> int:
        off = addr - start
        if 0 <= off < len(seg_bytes):
            return seg_bytes[off]
        return bridge.peek(addr, 1)[0]

    lines: List[str] = []
    pc = start
    first = True
    # --- .proc tracking ---
    # Open a ``.proc name`` when ``pc`` reaches a proc entry, close
    # with ``.endp`` when ``pc`` passes its declared end. A ``.proc``
    # block implicitly defines its own name as a label, so we SUPPRESS
    # the usual ``name:`` line at the entry when we're emitting it via
    # ``.proc`` instead.
    in_proc_end: Optional[int] = None

    while pc <= end:
        # Close an open .proc when we've moved past its body.
        if in_proc_end is not None and pc > in_proc_end:
            lines.append(".endp")
            lines.append("")
            in_proc_end = None

        # Open a .proc when reaching an entry the analyzer flagged as
        # safe to wrap. Suppress the normal label line since .proc
        # already emits the name.
        proc = proc_info.get(pc) if in_proc_end is None else None
        if proc is not None:
            if not first:
                lines.append("")
            lines.append(f".proc {proc['name']}")
            in_proc_end = proc["end"]
            # Comment (if any) follows the .proc line
            if pc in comments:
                lines.append(f"    ; {comments[pc]}")
            first = False
        else:
            # Label
            if pc in labels:
                # Blank-line separator before each procedure entry
                # (skip the very first line of the range so we don't
                # stack a blank on top of an existing region header).
                if pc in procedure_starts and not first:
                    lines.append("")
                lines.append(f"{labels[pc]}:")
            # Comment
            if pc in comments:
                lines.append(f"    ; {comments[pc]}")
            first = False

        opcode = _b(pc)
        entry = OPCODES.get(opcode)

        if entry is None:
            # Illegal opcode — use Altirra's DISASM text as an
            # annotated .byte fallback so the reader still sees
            # what the instruction would be.
            ins_result = bridge.disasm(pc, count=1)
            if ins_result:
                ins = ins_result[0]
                ins_len = ins.get("length", 1)
                raw = [f"${_b(pc + i):02X}" for i in range(ins_len)]
                annot = ins["text"].strip()
                lines.append(
                    f"    .byte {','.join(raw):20s} ; ${pc:04X}: {annot}")
                next_pc = int(ins["next"].lstrip("$"), 16)
                pc = next_pc if next_pc > pc else pc + 1
            else:
                lines.append(f"    .byte ${opcode:02X}  ; disasm failed at ${pc:04X}")
                pc += 1
            continue

        mnem, mode, size = entry

        # Refuse to run past end
        if pc + size - 1 > end:
            # Tail fragment — emit as raw bytes
            for i in range(end - pc + 1):
                lines.append(f"    .byte ${_b(pc + i):02X}  ; ${pc + i:04X}")
            break

        # Mid-instruction label safety: if a label or comment sits
        # inside the operand bytes, emit as raw bytes instead so the
        # label position is preserved.
        if any((pc + i) in labels or (pc + i) in comments
               for i in range(1, size)):
            for i in range(size):
                a = pc + i
                if i > 0 and a in labels:
                    lines.append(f"{labels[a]}:")
                if a in comments and i > 0:
                    lines.append(f"    ; {comments[a]}")
                lines.append(f"    .byte ${_b(a):02X}  ; ${a:04X}")
            pc += size
            continue

        operand_bytes = [_b(pc + i) for i in range(1, size)]
        line = format_instruction(mnem, mode, pc, operand_bytes, resolve)
        lines.append(f"    {line}")
        pc += size

    # If we end inside a .proc, close it out — the walker may stop
    # exactly at the proc's last instruction or at a region boundary.
    if in_proc_end is not None:
        lines.append(".endp")

    return lines


# Addressing modes that cannot use an assembler label for the
# operand without changing the encoding: forcibly hex-encoded.
_IMM_MODES = frozenset({"imm"})
_IMPL_MODES = frozenset({"imp", "acc"})


def format_instruction(
    mnem: str,
    mode: str,
    pc: int,
    operand_bytes: List[int],
    labels: Dict[int, str],
) -> str:
    """Format a single legal 6502 instruction as MADS source.

    ``mnem`` is from OPCODES (uppercase); output is lowercase.
    ``operand_bytes`` has 0, 1, or 2 entries depending on mode.
    ``labels`` is the symbol dictionary used to substitute the
    operand address with a name when available.
    """
    m = mnem.lower()
    if mode in _IMPL_MODES:
        return m
    if mode in _IMM_MODES:
        return f"{m} #${operand_bytes[0]:02X}"

    def _name(addr: int, zp: bool) -> str:
        name = labels.get(addr)
        if name:
            return name
        return f"${addr:02X}" if zp else f"${addr:04X}"

    if mode == "zp":
        return f"{m} {_name(operand_bytes[0], True)}"
    if mode == "zpx":
        return f"{m} {_name(operand_bytes[0], True)},X"
    if mode == "zpy":
        return f"{m} {_name(operand_bytes[0], True)},Y"
    if mode == "abs":
        a = operand_bytes[0] | (operand_bytes[1] << 8)
        return f"{m} {_name(a, False)}"
    if mode == "abx":
        a = operand_bytes[0] | (operand_bytes[1] << 8)
        return f"{m} {_name(a, False)},X"
    if mode == "aby":
        a = operand_bytes[0] | (operand_bytes[1] << 8)
        return f"{m} {_name(a, False)},Y"
    if mode == "rel":
        off = operand_bytes[0]
        if off & 0x80:
            off -= 0x100
        a = (pc + 2 + off) & 0xFFFF
        return f"{m} {_name(a, False)}"
    if mode == "ind":
        a = operand_bytes[0] | (operand_bytes[1] << 8)
        return f"{m} ({_name(a, False)})"
    if mode == "izx":
        return f"{m} ({_name(operand_bytes[0], True)},X)"
    if mode == "izy":
        return f"{m} ({_name(operand_bytes[0], True)}),Y"
    # Unknown mode — fall back to hex
    return f"{m} ; unhandled mode {mode}"


def collect_branch_targets(
    mem,
    start: int,
    end: int,
) -> Set[int]:
    """Scan [start, end] for branch/JSR/JMP targets.

    Uses the OPCODES table to walk instructions linearly (same
    layout that ``emit_code_range`` will disassemble). Returns the
    set of target addresses within ``[start, end]`` — these are
    candidates for synthetic ``loc_XXXX`` labels.
    """
    from .._analyzer_tables import OPCODES
    targets: Set[int] = set()
    pc = start
    while pc <= end:
        opcode = mem[pc]
        entry = OPCODES.get(opcode)
        if not entry:
            pc += 1
            continue
        _mnem, mode, size = entry
        if pc + size - 1 > end:
            break
        if mode == "rel":
            # Signed 8-bit relative branch
            off = mem[pc + 1]
            if off & 0x80:
                off -= 0x100
            tgt = (pc + 2 + off) & 0xFFFF
            if start <= tgt <= end:
                targets.add(tgt)
        elif mode == "abs" and _mnem in ("JMP", "JSR"):
            tgt = mem[pc + 1] | (mem[pc + 2] << 8)
            if start <= tgt <= end:
                targets.add(tgt)
        pc += size
    return targets


def emit_mixed(
    bridge: "AltirraBridge",
    proj: "Project",
    mem,
    start: int,
    end: int,
    region_map: Dict[int, dict],
    emit_labels: Dict[int, str],
    all_labels: Dict[int, str],
    proc_info: Optional[Dict[int, dict]] = None,
) -> str:
    """Emit mixed code/data content respecting region boundaries.

    Code regions → ``bridge.disasm()`` (real mnemonics).
    Data regions → ``.byte``/``.word``/``.ds`` via ``data.py``.
    Unclassified → attempt disassembly (fallback to ``.byte``).

    Before emitting, all code ranges are scanned for branch / JSR /
    JMP targets. Any target that lacks a project label gets a
    synthetic ``loc_XXXX`` label so the generated source reads like
    hand-written code instead of jumping through raw hex addresses.

    ``proc_info`` (optional) is a dict ``{entry_addr → {name, end, …}}``
    of procedures that are safe to wrap in MADS ``.proc``/``.endp``
    blocks. When a code walker reaches one of these addresses it
    opens a ``.proc`` block; it closes with ``.endp`` once ``pc``
    passes the procedure's ``end``. This is opt-in: pass ``None`` to
    keep the walker's current flat-label layout.
    """
    proc_info = proc_info or {}
    # --- Pre-pass: synthesise ``loc_XXXX`` labels for branch targets.
    synth_labels: Dict[int, str] = {}
    existing = set(all_labels.keys())

    def _scan(s: int, e: int) -> None:
        for tgt in collect_branch_targets(mem, s, e):
            if tgt in existing or tgt in synth_labels:
                continue
            synth_labels[tgt] = f"loc_{tgt:04X}"

    a = start
    while a <= end:
        r = region_map.get(a)
        if r and r["type"] == "code":
            _scan(a, min(_region_end(r), end))
            a = min(_region_end(r), end) + 1
        elif r and r["type"] == "data":
            a = min(_region_end(r), end) + 1
        else:
            ge = a
            while ge + 1 <= end and region_map.get(ge + 1) is None:
                ge += 1
            _scan(a, ge)
            a = ge + 1

    # Merge synthesised labels into the emit/resolve dictionaries.
    # Synthetic labels never override existing project labels.
    local_emit_labels = dict(emit_labels)
    local_all_labels = dict(all_labels)
    for a, name in synth_labels.items():
        local_emit_labels.setdefault(a, name)
        local_all_labels.setdefault(a, name)

    # --- Procedure starts ---
    # Every address that has a user-defined project label is treated
    # as a procedure entry (or at least a conceptually-separated
    # section of code). Synthetic ``loc_`` labels are NOT treated as
    # procedure starts — they're just local branch targets.
    procedure_starts: Set[int] = set(all_labels.keys())

    lines: List[str] = []
    addr = start

    while addr <= end:
        region = region_map.get(addr)

        # --- Code region ---
        if region and region["type"] == "code":
            block_end = min(_region_end(region), end)
            code_lines = emit_code_range(
                bridge, addr, block_end,
                local_emit_labels, proj.comments,
                procedure_starts=procedure_starts,
                resolve_labels=local_all_labels,
                proc_info=proc_info)
            lines.extend(code_lines)
            addr = block_end + 1

        # --- Data region ---
        elif region and region["type"] == "data":
            block_end = min(_region_end(region), end)
            # Emit label at start
            if addr in local_emit_labels:
                lines.append(f"{local_emit_labels[addr]}:")
            if addr in proj.comments:
                lines.append(f"    ; {proj.comments[addr]}")
            inner_labels = {a: n for a, n in local_emit_labels.items()
                           if a > addr and a <= block_end}
            text = _data.emit_data_range(
                mem, addr, block_end,
                region.get("hint", "bytes"),
                inner_labels, proj.comments,
                resolve_labels=local_all_labels)
            lines.append(text)
            addr = block_end + 1

        # --- Unclassified: try disasm ---
        else:
            # Find extent of unclassified gap
            gap_end = addr
            while (gap_end + 1 <= end
                   and region_map.get(gap_end + 1) is None):
                gap_end += 1

            code_lines = emit_code_range(
                bridge, addr, gap_end,
                local_emit_labels, proj.comments,
                procedure_starts=procedure_starts,
                resolve_labels=local_all_labels,
                proc_info=proc_info)
            lines.extend(code_lines)
            addr = gap_end + 1

    return "\n".join(lines)


def build_region_map(
    proj: "Project",
    seg_start: int,
    seg_end: int,
) -> Dict[int, dict]:
    """Build addr → region dict for a segment range."""
    rmap: Dict[int, dict] = {}
    for r in proj.regions:
        rs = _parse_addr(r["start"])
        re = _parse_addr(r["end"])
        if re < seg_start or rs > seg_end:
            continue
        for a in range(max(rs, seg_start), min(re, seg_end) + 1):
            rmap[a] = r
    return rmap


# --- helpers ---

def _region_end(region: dict) -> int:
    return _parse_addr(region["end"])


def _parse_addr(v) -> int:
    if isinstance(v, int):
        return v
    return int(str(v).lstrip("$"), 16)


def _peek(bridge, addr: int) -> int:
    return bridge.peek(addr, 1)[0]


# MADS 6502 legal mnemonics (all standard NMOS 6502 instructions).
# Anything not in this set is an illegal opcode that MADS won't assemble.
_LEGAL_MNEMONICS = frozenset({
    "adc", "and", "asl", "bcc", "bcs", "beq", "bit", "bmi", "bne",
    "bpl", "brk", "bvc", "bvs", "clc", "cld", "cli", "clv", "cmp",
    "cpx", "cpy", "dec", "dex", "dey", "eor", "inc", "inx", "iny",
    "jmp", "jsr", "lda", "ldx", "ldy", "lsr", "nop", "ora", "pha",
    "php", "pla", "plp", "rol", "ror", "rti", "rts", "sbc", "sec",
    "sed", "sei", "sta", "stx", "sty", "tax", "tay", "tsx", "txa",
    "txs", "tya",
})


def _is_illegal_opcode(text: str, opcode_byte: int = 0xEA) -> bool:
    """Return True if the instruction can't be assembled by MADS.

    Checks both mnemonic text and opcode byte to catch:
    - Unknown mnemonics (slo, dcp, isb, sha, etc.)
    - NOP with operands (nop $XX — illegal nop variants)
    - Implied-NOP with wrong byte ($1A,$3A,$5A,$7A,$DA,$FA)
    """
    parts = text.strip().split(None, 1)
    if not parts:
        return True
    mnem = parts[0].lower()
    if mnem not in _LEGAL_MNEMONICS:
        return True
    # NOP is implied-only in MADS — any operand makes it illegal
    if mnem == "nop" and len(parts) > 1:
        return True
    # Illegal NOP variants that disassemble as bare "nop" but are
    # not $EA — MADS would encode as $EA, changing the binary
    if mnem == "nop" and opcode_byte != 0xEA:
        return True
    # Branch instructions in data regions can have out-of-range
    # targets. Detect branches that target addresses far from PC
    # (relative branch can only reach ±127 bytes). We don't know
    # PC here, but if the target is > 256 bytes away from any
    # reasonable position, it's suspect. This is caught more
    # reliably by marking data regions properly.
    return False
