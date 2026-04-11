"""Persistent reverse-engineering project state.

A :class:`Project` is a JSON-on-disk container holding everything an
RE workflow accumulates *outside* the binary itself: labels (named
addresses), comments (one-liners attached to addresses), notes
(free-form prose attached to address ranges), and arbitrary metadata
the analyst wants to remember between sessions.

What this module deliberately does **not** store
------------------------------------------------

* **Disassembly.** The bridge's ``DISASM`` command (Altirra's own
  ``ATDisassembleInsn``) is the source of truth for instruction
  decoding. Caching disassembly here would just create staleness
  bugs the next time you re-disassemble after editing labels.
* **Profiling data.** The bridge's ``PROFILE_DUMP*`` returns plain
  JSON dicts that any user can save themselves; baking it into the
  project format would couple us to a particular analysis snapshot.
* **The XEX bytes.** Use :mod:`altirra_bridge.loader` to parse them
  fresh; record the file path in :attr:`Project.source_path` instead.

What it does store
------------------

* ``labels``    — ``addr -> name``        (one name per address)
* ``comments``  — ``addr -> str``         (one comment per address)
* ``regions``   — list of ``{start, end, type, hint?}`` dicts
* ``notes``     — list of ``Note`` objects with start/end ranges
* ``metadata``  — free-form dict for analyst-defined keys

Persistence is line-oriented JSON so a project file diffs cleanly in
git — important for collaborative RE.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field, asdict
from typing import Dict, Iterator, List, Optional


@dataclass
class Note:
    """Free-form prose attached to an address range.

    A note covers ``[start, end]`` (inclusive). Use ``end == start``
    for a single-address note.
    """

    start: int
    end:   int
    text:  str

    def covers(self, addr: int) -> bool:
        return self.start <= addr <= self.end


@dataclass
class Project:
    """A persistent RE project.

    Construct via :meth:`new` (fresh) or :meth:`load` (from disk).
    Save with :meth:`save`.

    The on-disk format is JSON with addresses stored as ``"$xxxx"``
    hex strings (so a hex editor can read them) but in-memory they
    are plain ``int``.
    """

    name:         str            = "untitled"
    source_path:  Optional[str]  = None
    labels:       Dict[int, str] = field(default_factory=dict)
    comments:     Dict[int, str] = field(default_factory=dict)
    regions:      List[dict]     = field(default_factory=list)
    notes:        List[Note]     = field(default_factory=list)
    metadata:     dict           = field(default_factory=dict)
    # Copy-source relocations for self-relocating XEX files.
    # Each entry describes a range of XEX bytes that the program
    # copies to a different runtime address at boot. The reconstructed
    # export mode re-emits these ranges at their runtime addresses so
    # labels and comments (which are typically keyed at runtime addrs)
    # line up with the generated asm.
    #
    # Each entry: {"xex_start", "xex_end", "runtime_start",
    #              "copy_routine"?, "runtime_entry"?}
    # where addresses are stored as "$xxxx" hex strings on disk and
    # int at runtime.
    copy_sources: List[dict]     = field(default_factory=list)
    # XEX byte ranges to drop entirely from reconstructed-mode export.
    # Use this to eliminate bootstrap code whose job becomes moot once
    # the runtime code is emitted directly at its runtime address —
    # typically the relocator itself, the init routine that calls it,
    # and the INITAD vector that triggers the init routine. Each entry
    # is ``{"start", "end"}`` as ``"$xxxx"`` hex strings on disk.
    reconstructed_excludes: List[dict] = field(default_factory=list)
    _path:        Optional[str]  = None  # populated by load() / save()

    # ------------------------------------------------------------------
    # Construction
    # ------------------------------------------------------------------

    @classmethod
    def new(cls, name: str, source_path: Optional[str] = None) -> "Project":
        return cls(name=name, source_path=source_path)

    @classmethod
    def load(cls, path: str) -> "Project":
        with open(path, "r", encoding="utf-8") as f:
            doc = json.load(f)

        def _addr(k: str) -> int:
            # Saved keys are "$xxxx" hex strings; tolerate plain hex
            # too so a hand-edited file with raw hex still loads.
            return int(k.lstrip("$"), 16)

        proj = cls(
            name=doc.get("name", "untitled"),
            source_path=doc.get("source_path"),
            labels={_addr(k): v for k, v in doc.get("labels", {}).items()},
            comments={_addr(k): v for k, v in doc.get("comments", {}).items()},
            regions=doc.get("regions", []),
            notes=[Note(**n) for n in doc.get("notes", [])],
            metadata=doc.get("metadata", {}),
            copy_sources=doc.get("copy_sources", []),
            reconstructed_excludes=doc.get("reconstructed_excludes", []),
        )
        proj._path = path
        return proj

    def save(self, path: Optional[str] = None) -> None:
        """Save to ``path`` (or to the path used in :meth:`load`)."""
        target = path or self._path
        if target is None:
            raise ValueError("project has no path; pass one explicitly")
        doc = {
            "name":         self.name,
            "source_path":  self.source_path,
            "labels":       {f"${k:04x}": v for k, v in sorted(self.labels.items())},
            "comments":     {f"${k:04x}": v for k, v in sorted(self.comments.items())},
            "regions":      self.regions,
            "notes":        [asdict(n) for n in self.notes],
            "metadata":     self.metadata,
            "copy_sources": self.copy_sources,
            "reconstructed_excludes": self.reconstructed_excludes,
        }
        # Ensure target dir exists for newly-created projects.
        d = os.path.dirname(target)
        if d:
            os.makedirs(d, exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            json.dump(doc, f, indent=2, sort_keys=False)
            f.write("\n")
        self._path = target

    # ------------------------------------------------------------------
    # Labels
    # ------------------------------------------------------------------

    def label(self, addr: int, name: str) -> None:
        """Add or update a label at ``addr``. The name should be a
        valid MADS identifier (alphanumeric + underscore, not starting
        with a digit) so it round-trips through :mod:`asm_writer`.
        """
        if not name:
            raise ValueError("label name must be non-empty")
        self.labels[addr] = name

    def unlabel(self, addr: int) -> bool:
        return self.labels.pop(addr, None) is not None

    def get_label(self, addr: int) -> Optional[str]:
        return self.labels.get(addr)

    def find_label(self, name: str) -> Optional[int]:
        for addr, n in self.labels.items():
            if n == name:
                return addr
        return None

    # ------------------------------------------------------------------
    # Comments
    # ------------------------------------------------------------------

    def comment(self, addr: int, text: str) -> None:
        self.comments[addr] = text

    def uncomment(self, addr: int) -> bool:
        return self.comments.pop(addr, None) is not None

    def get_comment(self, addr: int) -> Optional[str]:
        return self.comments.get(addr)

    # ------------------------------------------------------------------
    # Notes
    # ------------------------------------------------------------------

    def add_note(self, start: int, end: int, text: str) -> None:
        if end < start:
            raise ValueError("note end must be >= start")
        self.notes.append(Note(start=start, end=end, text=text))

    def notes_at(self, addr: int) -> Iterator[Note]:
        for n in self.notes:
            if n.covers(addr):
                yield n

    # ------------------------------------------------------------------
    # Regions (code vs data classification)
    # ------------------------------------------------------------------

    def add_region(self, start: int, end: int, rtype: str,
                   hint: str = "", label: str = "") -> None:
        """Mark an address range as code or data.

        ``rtype`` is ``"code"`` or ``"data"``.  For data regions,
        ``hint`` can be ``"bytes"``, ``"string"``, ``"charset"``,
        ``"addr_table"``, ``"display_list"``, ``"sprite"``,
        ``"sound_table"``, ``"fill_zero"``, or any custom value.
        """
        r: dict = {"start": start, "end": end, "type": rtype}
        if hint:
            r["hint"] = hint
        if label:
            r["label"] = label
        self.regions.append(r)

    def region_at(self, addr: int) -> Optional[dict]:
        """Return the region covering ``addr``, or ``None``."""
        for r in self.regions:
            s = r["start"] if isinstance(r["start"], int) else int(str(r["start"]).lstrip("$"), 16)
            e = r["end"]   if isinstance(r["end"], int)   else int(str(r["end"]).lstrip("$"), 16)
            if s <= addr <= e:
                return r
        return None

    # ------------------------------------------------------------------
    # Copy-source relocations
    # ------------------------------------------------------------------

    def mark_copy_source(self, xex_start: int, xex_end: int,
                         runtime_start: int,
                         copy_routine: Optional[int] = None,
                         runtime_entry: Optional[int] = None) -> None:
        """Declare that ``[xex_start..xex_end]`` in the XEX file is a
        block the program relocates to ``runtime_start`` at boot.

        The offset is constant: ``runtime = xex - xex_start + runtime_start``.
        In reconstructed export mode (:func:`asm_writer.write_all` with
        ``reconstructed=True``) this block is re-emitted as a separate
        segment at ``org $runtime_start`` so labels and comments keyed
        at runtime addresses line up with the generated asm.

        :param xex_start: first XEX address of the copy source.
        :param xex_end:   last XEX address of the copy source (inclusive).
        :param runtime_start: destination start address at runtime.
        :param copy_routine: address of the copy routine itself (optional,
                             informational — not used by the writer).
        :param runtime_entry: actual entry point after the copy runs
                              (optional). If set, reconstructed mode uses
                              this as the XEX RUN address so the output
                              boots straight into the relocated code.
        """
        self.copy_sources.append({
            "xex_start":     f"${xex_start:04x}",
            "xex_end":       f"${xex_end:04x}",
            "runtime_start": f"${runtime_start:04x}",
            "copy_routine":  f"${copy_routine:04x}"  if copy_routine  is not None else None,
            "runtime_entry": f"${runtime_entry:04x}" if runtime_entry is not None else None,
        })

    def exclude_from_reconstructed(self, start: int, end: int) -> None:
        """Drop ``[start..end]`` (XEX addresses, inclusive) from
        reconstructed-mode export. Use this to eliminate bootstrap
        code whose job becomes moot once the runtime slices are
        emitted directly — typically the relocator body, the init
        routine that calls it, and the INITAD vector that triggers
        it. Affects :func:`asm_writer.write_all` when
        ``reconstructed=True``. Byte-exact mode ignores it.
        """
        self.reconstructed_excludes.append({
            "start": f"${start:04x}",
            "end":   f"${end:04x}",
        })

    def iter_reconstructed_excludes(self) -> Iterator[tuple]:
        """Yield ``(start, end)`` int pairs for all exclusion ranges."""
        def _to_int(v):
            if isinstance(v, int):
                return v
            return int(str(v).lstrip("$"), 16)
        for ex in self.reconstructed_excludes:
            yield _to_int(ex["start"]), _to_int(ex["end"])

    def iter_copy_sources(self) -> Iterator[dict]:
        """Yield copy-source entries with addresses as plain ``int``.

        Each dict has ``xex_start``, ``xex_end``, ``runtime_start``
        as ints and ``copy_routine`` / ``runtime_entry`` as ints or
        ``None``. The stored form uses ``"$xxxx"`` hex strings; this
        helper does the conversion.
        """
        def _to_int(v):
            if v is None:
                return None
            if isinstance(v, int):
                return v
            return int(str(v).lstrip("$"), 16)
        for cs in self.copy_sources:
            yield {
                "xex_start":     _to_int(cs.get("xex_start")),
                "xex_end":       _to_int(cs.get("xex_end")),
                "runtime_start": _to_int(cs.get("runtime_start")),
                "copy_routine":  _to_int(cs.get("copy_routine")),
                "runtime_entry": _to_int(cs.get("runtime_entry")),
            }

    # ------------------------------------------------------------------
    # Bridge integration
    # ------------------------------------------------------------------

    def export_lab(self, path: str) -> None:
        """Write all labels to a MADS ``.lab`` file. The format is
        ``ADDR LABEL_NAME`` with addresses as 4-digit hex (no $),
        which is what MADS produces and what Altirra's
        :meth:`AltirraBridge.sym_load` accepts.
        """
        with open(path, "w", encoding="utf-8") as f:
            for addr in sorted(self.labels):
                f.write(f"{addr:04X} {self.labels[addr]}\n")

    def import_from_bridge(self, bridge, addrs) -> int:
        """Reverse-lookup each address in ``addrs`` against the
        bridge's loaded symbols and add any matches as labels in
        this project. Returns the number of labels added.

        ``bridge`` is an :class:`AltirraBridge` with at least one
        ``sym_load`` already done. Useful for "pull every Atari OS
        symbol the kernel matches into my project at once".
        """
        from .client import RemoteError
        added = 0
        for addr in addrs:
            try:
                sym = bridge.sym_lookup(addr)
            except RemoteError:
                continue
            if sym.get("offset") == 0 and sym.get("name"):
                if addr not in self.labels:
                    self.labels[addr] = sym["name"]
                    added += 1
        return added
