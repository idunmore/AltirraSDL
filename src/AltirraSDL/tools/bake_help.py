#!/usr/bin/env python3
# Bake the Altirra Windows help XML into a single gzip-compressed binary
# blob embedded in the AltirraSDL executable.  Source lives at
# src/ATHelpFile/source/ (68 topic XMLs + toc.xml + pics/*).  Decoded by
# ui_help_contents at runtime — no external deps, no on-disk asset dir.
#
# Reads:
#   <src>/toc.xml        — table of contents tree (canonical TOC)
#   <src>/*.xml          — topic files (XSL-rendered to HTML on Windows)
#   <src>/pics/*.png     — image assets
#   <src>/pics/*.jpg
#
# Emits:
#   <out>/help_data.h    — declarations
#   <out>/help_data.cpp  — kATHelpData[]: gzip(magic + payload)
#   <out>/help_dump.txt  — human-readable view of every topic (debug aid)
#
# Uses Python stdlib only.

import os
import sys
import gzip
import struct
import xml.etree.ElementTree as ET


# -- Block opcodes (must match ui_help_render.cpp) -------------------------

OP_PARA              = 1
OP_H2                = 2
OP_H3                = 3
OP_H4                = 4
OP_LIST_START        = 5    # payload: 1 byte (0=bullet, 1=ordered)
OP_LIST_END          = 6
OP_LIST_ITEM         = 7
OP_NOTE_START        = 8    # payload: title runs (may be empty)
OP_NOTE_END          = 9
OP_BLOCKQUOTE_START  = 10
OP_BLOCKQUOTE_END    = 11
OP_TABLE_START       = 12   # payload: 1 byte = column count
OP_TABLE_ROW         = 13
OP_TABLE_CELL        = 14   # payload: runs
OP_TABLE_HCELL       = 15   # payload: runs (header cell)
OP_TABLE_END         = 16
OP_DL_START          = 17
OP_DL_END            = 18
OP_DT                = 19
OP_DD                = 20
OP_PRE               = 21   # payload: 1 string idx (raw text)
OP_IMAGE             = 22   # payload: u16 imageIndex, u16 captionStrIdx
OP_MINI_TOC          = 23   # payload: empty (renderer walks topic headings)
OP_HR                = 24

# Block header flag bits.
HBF_HAS_ANCHOR       = 1

# Run style flags.
SF_BOLD              = 1
SF_ITALIC            = 2
SF_MONO              = 4
SF_LINK              = 8

# Magic + version for the outer container.
MAGIC = b"ATHELP\x00\x01"


# -- String pool ------------------------------------------------------------

class StringPool:
    """UTF-8 string pool. Index 0 is reserved for the empty string."""

    def __init__(self):
        self._lookup = {}
        self._buf = bytearray()
        self.add("")  # index 0 = empty

    def add(self, s):
        if s is None:
            s = ""
        if s in self._lookup:
            return self._lookup[s]
        idx = len(self._lookup)
        if idx >= 0xFFFF:
            raise RuntimeError("string pool overflow (>=65535 entries)")
        self._lookup[s] = idx
        encoded = s.encode("utf-8")
        if len(encoded) > 0xFFFF:
            raise RuntimeError(
                f"single string too long ({len(encoded)} bytes): "
                f"{s[:60]!r}…"
            )
        # length-prefixed (uint16 little-endian) UTF-8 followed by a NUL
        # so the runtime can return a direct (const char*) into the
        # blob without any per-call buffering.
        self._buf += struct.pack("<H", len(encoded))
        self._buf += encoded
        self._buf += b"\x00"
        return idx

    def serialize(self):
        # Per-string offset table so the runtime can index by id without
        # scanning.  Layout:
        #   uint32 count; uint32 offsetTable[count];
        #   (u16 len; bytes[len]; u8 0)[count]
        # The trailing NUL lets the runtime return a (const char*)
        # pointing directly into the blob.
        offsets = []
        cur = 0
        while cur < len(self._buf):
            offsets.append(cur)
            (n,) = struct.unpack_from("<H", self._buf, cur)
            cur += 2 + n + 1  # length prefix + bytes + trailing NUL
        out = bytearray()
        out += struct.pack("<I", len(offsets))
        for off in offsets:
            out += struct.pack("<I", off)
        out += self._buf
        return bytes(out)

    def count(self):
        return len(self._lookup)


# -- Run accumulator --------------------------------------------------------

class Runs:
    """Builds a sequence of styled inline runs."""

    def __init__(self, pool):
        self._pool = pool
        self._runs = []  # list of (flags, textIdx, hrefIdx_or_0)
        self._raw = []   # parallel list of raw text strings for dumps

    def add_text(self, text, style_stack, href=None):
        if not text:
            return
        # Collapse spaces but preserve at least one.
        # XSL transforms leave whitespace as-is; help XML uses indented
        # source so we collapse runs of whitespace to a single space.
        # Embedded newlines become spaces.  Pre blocks bypass this path
        # entirely.
        flat = " ".join(text.split())
        if not flat:
            # Whitespace-only — preserve a separator if this isn't the
            # very first run on the line.
            if self._runs and not self._runs[-1][1] == 0:
                if not self._runs_endswith_space():
                    self._runs.append((self._flags(style_stack, href), -1, 0))
            return
        # If the source had leading whitespace, prepend a space (so
        # "foo<b>bar</b>" stays "foobar" but "foo <b>bar</b>" stays "foo bar").
        leading = text and text[0].isspace()
        trailing = text and text[-1].isspace()
        flags = self._flags(style_stack, href)
        # We cannot encode bare-space style runs cleanly, so we just
        # bake leading/trailing spaces into the text.
        if leading and self._runs and not self._runs_endswith_space():
            flat = " " + flat
        if trailing:
            flat = flat + " "
        href_idx = self._pool.add(href) if href else 0
        text_idx = self._pool.add(flat)
        self._runs.append((flags, text_idx, href_idx))
        self._raw.append(flat)

    def _runs_endswith_space(self):
        if not self._runs:
            return True
        # Look up the last text run's resolved string.
        for flags, text_idx, _ in reversed(self._runs):
            if text_idx == -1 or text_idx == 0:
                continue
            # Can't introspect from the pool here without keeping the
            # raw text, so just allow duplicate spaces in pathological
            # cases.  In practice the wrap renderer collapses them.
            return False
        return True

    @staticmethod
    def _flags(style_stack, href):
        f = 0
        for s in style_stack:
            if s == "b":
                f |= SF_BOLD
            elif s == "i":
                f |= SF_ITALIC
            elif s == "tt":
                f |= SF_MONO
        if href is not None:
            f |= SF_LINK
        return f

    def empty(self):
        return not self._runs

    def serialize(self):
        # Filter sentinel separator runs.
        runs = [r for r in self._runs if r[1] != -1]
        if len(runs) > 0xFFFF:
            raise RuntimeError("too many runs in a single block")
        out = bytearray()
        out += struct.pack("<H", len(runs))
        for flags, text_idx, href_idx in runs:
            out += struct.pack("<BH", flags, text_idx)
            if flags & SF_LINK:
                out += struct.pack("<H", href_idx)
        return bytes(out)


# -- Topic walker -----------------------------------------------------------

INLINE_TAGS = {"b", "i", "em", "strong", "tt", "a", "span", "br"}
STYLE_TAGS = {"b": "b", "strong": "b", "i": "i", "em": "i", "tt": "tt"}
BLOCK_TAGS = {"p", "ul", "ol", "table", "pre", "blockquote", "dl",
              "div", "note", "h2", "h3", "h4", "img", "hr", "toc"}


class TopicBuilder:
    """Walks a parsed XML tree and emits the binary block stream."""

    def __init__(self, pool, image_index_map, filename, dump_lines):
        self._pool = pool
        self._image_index = image_index_map
        self._filename = filename
        self._stream = bytearray()
        self._block_count = 0
        self._anchors = []          # list of (anchorStrIdx, blockIndex)
        self._pending_anchor = None # next emitted block will own this
        self._title_runs = None     # runs accumulator for current container
        self._dump = dump_lines

    # ---- Public API ----

    def topic_title(self):
        return self._title

    def block_stream(self):
        return bytes(self._stream)

    def anchors(self):
        return self._anchors

    def block_count(self):
        return self._block_count

    # ---- Walking ----

    def walk(self, root):
        if root.tag != "topic":
            raise RuntimeError(f"{self._filename}: root is <{root.tag}>")
        self._title = root.attrib.get("title", "")
        self._dump.append(f"=== {self._filename}  [{self._title}] ===")
        for child in root:
            self._block(child, [], inside_block=False)

    # ---- Block dispatch ----

    def _block(self, el, style_stack, inside_block):
        tag = el.tag
        if tag == "style":
            return  # CSS — drop entirely
        if tag in ("featurelist", "stockmsg", "stockref"):
            return  # unused / skipped
        if tag == "p":
            self._emit_paragraph(el)
        elif tag == "h2":
            self._emit_heading(el, OP_H2, "h2")
        elif tag == "h3":
            self._emit_heading(el, OP_H3, "h3")
        elif tag == "h4":
            self._emit_heading(el, OP_H4, "h4")
        elif tag == "ul" or tag == "ol":
            self._emit_list(el, ordered=(tag == "ol"))
        elif tag == "note":
            self._emit_note(el)
        elif tag == "table":
            self._emit_table(el)
        elif tag == "blockquote":
            self._emit_blockquote(el)
        elif tag == "pre":
            self._emit_pre(el)
        elif tag == "dl":
            self._emit_dl(el)
        elif tag == "img":
            self._emit_image(el)
        elif tag == "toc":
            self._emit_simple(OP_MINI_TOC, b"", el)
        elif tag == "div":
            # Treat <div> as a transparent container.
            for child in el:
                self._block(child, style_stack, inside_block)
            # Trailing/inline text in a div is rare; flush as a paragraph
            # only if there's any.
            tail = (el.text or "").strip()
            if tail:
                self._emit_text_paragraph(el)
        elif tag == "hr":
            self._emit_simple(OP_HR, b"", el)
        elif tag == "a" and "name" in el.attrib:
            # Standalone anchor outside a heading/paragraph.
            self._pending_anchor = el.attrib["name"]
            # If <a name="X">Title</a> stands alone with text content,
            # render as a paragraph carrying the anchor.
            if (el.text and el.text.strip()) or len(el):
                self._emit_text_paragraph(el)
        elif tag in INLINE_TAGS:
            # Inline element appearing at block level — wrap as paragraph.
            self._emit_text_paragraph_from_inline(el)
        elif tag in (
            "compat-toc", "compat-tags", "compat-list",
            "compat-category", "compat-title", "compat-issue",
            "compat-tag", "inline-text", "desc",
        ):
            # Compatibility XML uses its own DTD; handled by the
            # compatibility special-case in convert_topic.  If we get
            # here it means the XML wasn't rerouted — drop silently.
            return
        else:
            # Unknown block — emit children transparently.
            for child in el:
                self._block(child, style_stack, inside_block)

    # ---- Block emit helpers ----

    def _emit_simple(self, op, payload, el):
        self._write_block(op, payload, anchor=self._consume_anchor())
        self._dump.append(f"  [op={op}] (no payload)")

    def _emit_heading(self, el, op, label):
        runs = Runs(self._pool)
        anchor = self._consume_anchor()
        anchor = self._collect_inline(el, runs, [], anchor)
        payload = runs.serialize()
        self._write_block(op, payload, anchor=anchor)
        self._dump.append(f"  [{label}] {self._dump_runs(runs)}")

    def _emit_paragraph(self, el):
        runs = Runs(self._pool)
        anchor = self._consume_anchor()
        anchor = self._collect_inline(el, runs, [], anchor)
        if runs.empty():
            return
        self._write_block(OP_PARA, runs.serialize(), anchor=anchor)
        self._dump.append(f"  [p] {self._dump_runs(runs)}")

    def _emit_text_paragraph(self, el):
        # Generic fallback: build runs from el's text + tail-less children
        # and emit as paragraph.
        runs = Runs(self._pool)
        anchor = self._consume_anchor()
        anchor = self._collect_inline(el, runs, [], anchor)
        if not runs.empty():
            self._write_block(OP_PARA, runs.serialize(), anchor=anchor)
            self._dump.append(f"  [p] {self._dump_runs(runs)}")

    def _emit_text_paragraph_from_inline(self, el):
        # <i>...</i> at block level → wrap as paragraph carrying that
        # style across its content.
        runs = Runs(self._pool)
        anchor = self._consume_anchor()
        # The element itself contributes its style to the runs.
        style_stack = []
        if el.tag in STYLE_TAGS:
            style_stack.append(STYLE_TAGS[el.tag])
        href = None
        if el.tag == "a" and "href" in el.attrib:
            href = el.attrib["href"]
        if el.text:
            runs.add_text(el.text, style_stack, href=href)
        for ch in el:
            anchor = self._inline_recurse(ch, runs, style_stack, anchor, href)
            if ch.tail:
                runs.add_text(ch.tail, style_stack, href=href)
        if not runs.empty():
            self._write_block(OP_PARA, runs.serialize(), anchor=anchor)
            self._dump.append(f"  [p<{el.tag}>] {self._dump_runs(runs)}")

    def _emit_list(self, el, ordered):
        self._write_block(
            OP_LIST_START,
            struct.pack("<B", 1 if ordered else 0),
            anchor=self._consume_anchor(),
        )
        self._dump.append(f"  [list start ordered={ordered}]")
        for child in el:
            if child.tag != "li":
                continue
            # Collect ONLY inline content (text + inline tags); stop at
            # block-level children.
            runs = Runs(self._pool)
            item_anchor = self._consume_anchor()
            item_anchor = self._collect_inline(
                child, runs, [], item_anchor, inline_only=True
            )
            self._write_block(
                OP_LIST_ITEM, runs.serialize(), anchor=item_anchor
            )
            self._dump.append(f"    [li] {self._dump_runs(runs)}")
            # Block-level children become nested blocks under the item.
            for grand in child:
                if grand.tag in BLOCK_TAGS:
                    self._block(grand, [], True)
        self._write_block(OP_LIST_END, b"", anchor=0)
        self._dump.append("  [list end]")

    def _emit_note(self, el):
        title_runs = Runs(self._pool)
        if "title" in el.attrib:
            title_runs.add_text(el.attrib["title"], [])
        else:
            title_runs.add_text("Note", [])
        self._write_block(
            OP_NOTE_START, title_runs.serialize(),
            anchor=self._consume_anchor(),
        )
        self._dump.append(f"  [note start] {self._dump_runs(title_runs)}")
        for child in el:
            self._block(child, [], True)
        if el.text and el.text.strip():
            # Text directly inside <note> with no <p> wrapper.
            runs = Runs(self._pool)
            runs.add_text(el.text, [])
            self._write_block(OP_PARA, runs.serialize(), anchor=0)
        self._write_block(OP_NOTE_END, b"", anchor=0)
        self._dump.append("  [note end]")

    def _emit_blockquote(self, el):
        self._write_block(
            OP_BLOCKQUOTE_START, b"", anchor=self._consume_anchor()
        )
        self._dump.append("  [blockquote start]")
        for child in el:
            self._block(child, [], True)
        if el.text and el.text.strip():
            runs = Runs(self._pool)
            runs.add_text(el.text, [])
            self._write_block(OP_PARA, runs.serialize(), anchor=0)
        self._write_block(OP_BLOCKQUOTE_END, b"", anchor=0)
        self._dump.append("  [blockquote end]")

    def _emit_table(self, el):
        # Determine column count from the widest row.
        rows = [r for r in el if r.tag == "tr"]
        col_count = max((sum(1 for c in r if c.tag in ("td", "th"))
                         for r in rows), default=0)
        if col_count == 0:
            return
        self._write_block(
            OP_TABLE_START, struct.pack("<B", col_count),
            anchor=self._consume_anchor(),
        )
        self._dump.append(f"  [table start cols={col_count}]")
        for r in rows:
            self._write_block(OP_TABLE_ROW, b"", anchor=0)
            for cell in r:
                if cell.tag not in ("td", "th"):
                    continue
                runs = Runs(self._pool)
                self._collect_inline(cell, runs, [], 0, inline_only=True)
                op = OP_TABLE_HCELL if cell.tag == "th" else OP_TABLE_CELL
                self._write_block(op, runs.serialize(), anchor=0)
                self._dump.append(
                    f"    [{cell.tag}] {self._dump_runs(runs)}"
                )
        self._write_block(OP_TABLE_END, b"", anchor=0)
        self._dump.append("  [table end]")

    def _emit_dl(self, el):
        self._write_block(OP_DL_START, b"", anchor=self._consume_anchor())
        self._dump.append("  [dl start]")
        for child in el:
            if child.tag == "dt":
                runs = Runs(self._pool)
                self._collect_inline(child, runs, [], 0, inline_only=True)
                self._write_block(OP_DT, runs.serialize(), anchor=0)
                self._dump.append(f"    [dt] {self._dump_runs(runs)}")
                for grand in child:
                    if grand.tag in BLOCK_TAGS:
                        self._block(grand, [], True)
            elif child.tag == "dd":
                runs = Runs(self._pool)
                self._collect_inline(child, runs, [], 0, inline_only=True)
                self._write_block(OP_DD, runs.serialize(), anchor=0)
                self._dump.append(f"    [dd] {self._dump_runs(runs)}")
                for grand in child:
                    if grand.tag in BLOCK_TAGS:
                        self._block(grand, [], True)
        self._write_block(OP_DL_END, b"", anchor=0)
        self._dump.append("  [dl end]")

    def _emit_pre(self, el):
        # Preserve whitespace; children are unknown tags treated as text.
        text = self._serialize_pre(el)
        # Strip a leading blank line (XML indentation artifact).
        if text.startswith("\n"):
            text = text[1:]
        text_idx = self._pool.add(text)
        self._write_block(
            OP_PRE, struct.pack("<H", text_idx),
            anchor=self._consume_anchor(),
        )
        first = text.splitlines()[0] if text else ""
        self._dump.append(f"  [pre {len(text)}b] {first[:60]!r}…")

    def _serialize_pre(self, el):
        out = []
        if el.text:
            out.append(el.text)
        for child in el:
            # Re-emit unknown tags as literal text so the user sees what
            # the source contains (e.g. <Debugger readable name>).
            child_text = self._serialize_pre_child(child)
            out.append(child_text)
            if child.tail:
                out.append(child.tail)
        return "".join(out)

    def _serialize_pre_child(self, el):
        attrs = "".join(f' {k}="{v}"' for k, v in el.attrib.items())
        inner = self._serialize_pre(el)
        if inner:
            return f"<{el.tag}{attrs}>{inner}</{el.tag}>"
        return f"<{el.tag}{attrs}/>"

    def _emit_image(self, el):
        src = el.attrib.get("src", "")
        # Source is "pics/foo.png" — index by basename (matches how we
        # built image_index in convert).
        key = os.path.basename(src)
        if key not in self._image_index:
            return
        idx = self._image_index[key]
        caption_idx = 0
        if "alt" in el.attrib:
            caption_idx = self._pool.add(el.attrib["alt"])
        self._write_block(
            OP_IMAGE, struct.pack("<HH", idx, caption_idx),
            anchor=self._consume_anchor(),
        )
        self._dump.append(f"  [img] {key}")

    # ---- Inline collection ----

    def _collect_inline(self, el, runs, style_stack, anchor,
                        inline_only=False):
        # Accumulate text and inline elements from el into runs.  Returns
        # the resolved anchor str-idx (or 0).
        # If inline_only is True, stop at block-level children (caller
        # will handle them separately as nested blocks).
        if el.text:
            runs.add_text(el.text, style_stack, href=self._href(style_stack))
        for child in el:
            if inline_only and child.tag in BLOCK_TAGS:
                # The tail text after a block child is normally just
                # source-formatting whitespace; ignore it.
                continue
            anchor = self._inline_recurse(
                child, runs, style_stack, anchor, self._href(style_stack)
            )
            if child.tail:
                runs.add_text(
                    child.tail, style_stack, href=self._href(style_stack)
                )
        return anchor

    def _href(self, style_stack):
        # Currently style_stack carries strings only; href is threaded
        # through _inline_recurse explicitly.
        return None

    def _inline_recurse(self, el, runs, style_stack, anchor, parent_href):
        tag = el.tag
        if tag == "br":
            # Render as a newline by adding a special run that breaks
            # the wrap.  We encode as a text run with a single newline;
            # the renderer detects \n and forces a line break.
            runs.add_text("\n", style_stack, href=parent_href)
            return anchor
        if tag == "a":
            # <a href> is an inline link; <a name> is an anchor target.
            if "name" in el.attrib:
                if anchor == 0:
                    anchor = self._pool.add(el.attrib["name"])
                else:
                    # Multiple anchors per block: register all anchors
                    # via the topic anchor table directly.  (One anchor
                    # is held in the block header; extras get blockIndex
                    # of the *current* block when written.)
                    self._anchors.append(
                        (self._pool.add(el.attrib["name"]), self._block_count)
                    )
                href = el.attrib.get("href")
            else:
                href = el.attrib.get("href")
            if el.text:
                runs.add_text(el.text, style_stack, href=href or parent_href)
            for ch in el:
                anchor = self._inline_recurse(
                    ch, runs, style_stack, anchor, href or parent_href
                )
                if ch.tail:
                    runs.add_text(
                        ch.tail, style_stack, href=href or parent_href
                    )
            return anchor
        if tag in STYLE_TAGS:
            new_stack = style_stack + [STYLE_TAGS[tag]]
            if el.text:
                runs.add_text(el.text, new_stack, href=parent_href)
            for ch in el:
                anchor = self._inline_recurse(
                    ch, runs, new_stack, anchor, parent_href
                )
                if ch.tail:
                    runs.add_text(ch.tail, new_stack, href=parent_href)
            return anchor
        if tag == "span":
            # Pass-through; ignore class/style.
            if el.text:
                runs.add_text(el.text, style_stack, href=parent_href)
            for ch in el:
                anchor = self._inline_recurse(
                    ch, runs, style_stack, anchor, parent_href
                )
                if ch.tail:
                    runs.add_text(ch.tail, style_stack, href=parent_href)
            return anchor
        # Unknown inline — recurse children flatly.
        if el.text:
            runs.add_text(el.text, style_stack, href=parent_href)
        for ch in el:
            anchor = self._inline_recurse(
                ch, runs, style_stack, anchor, parent_href
            )
            if ch.tail:
                runs.add_text(ch.tail, style_stack, href=parent_href)
        return anchor

    # ---- Anchor / block mechanics ----

    def _consume_anchor(self):
        if self._pending_anchor is not None:
            idx = self._pool.add(self._pending_anchor)
            self._pending_anchor = None
            return idx
        return 0

    def _write_block(self, op, payload, anchor):
        if len(payload) > 0xFFFF:
            raise RuntimeError(f"block payload too long: op={op}")
        flags = 0
        if anchor:
            flags |= HBF_HAS_ANCHOR
            # Append to anchor table for the renderer's lookup index.
            self._anchors.append((anchor, self._block_count))
            self._dump.append(f"    (anchor str#{anchor} -> block {self._block_count})")
        # Header: u8 op, u8 flags, [u16 anchor], u16 payloadLen
        self._stream += struct.pack("<BB", op, flags)
        if flags & HBF_HAS_ANCHOR:
            self._stream += struct.pack("<H", anchor)
        self._stream += struct.pack("<H", len(payload))
        self._stream += payload
        self._block_count += 1

    # ---- Dump helpers ----

    def _dump_runs(self, runs):
        text = "".join(runs._raw)
        if len(text) > 100:
            text = text[:97] + "..."
        return repr(text)


# -- Compatibility XML special case ----------------------------------------

def convert_compatibility(root, pool, image_index, filename, dump):
    """Custom DTD walker for compatibility.xml."""
    # First pass: collect <compat-tag> definitions.
    tag_def = {}  # id -> name
    for tags in root.findall(".//compat-tags"):
        for tag in tags.findall("compat-tag"):
            tag_def[tag.attrib["id"]] = tag.attrib.get("name", tag.attrib["id"])
    # Now walk the topic linearly.
    builder = TopicBuilder(pool, image_index, filename, dump)
    builder._title = root.attrib.get("title", "")
    dump.append(f"=== {filename}  [{builder._title}] ===")
    for child in root:
        if child.tag == "p":
            builder._emit_paragraph(child)
        elif child.tag == "h2":
            builder._emit_heading(child, OP_H2, "h2")
        elif child.tag == "h3":
            builder._emit_heading(child, OP_H3, "h3")
        elif child.tag == "compat-toc":
            # Auto-generate a mini-TOC of categories — let the renderer
            # use the standard MINI_TOC opcode (it walks h2 in topic).
            builder._write_block(OP_MINI_TOC, b"", anchor=0)
            dump.append("  [mini-toc]")
        elif child.tag == "compat-list":
            for cat in child.findall("compat-category"):
                # Category becomes h2.
                runs = Runs(pool)
                runs.add_text(cat.attrib.get("name", ""), [])
                builder._write_block(OP_H2, runs.serialize(), anchor=0)
                dump.append(f"  [h2] {cat.attrib.get('name', '')}")
                for prog in cat.findall("compat-title"):
                    runs = Runs(pool)
                    runs.add_text(prog.attrib.get("name", ""), [])
                    builder._write_block(OP_H3, runs.serialize(), anchor=0)
                    dump.append(f"    [h3] {prog.attrib.get('name', '')}")
                    for issue in prog.findall("compat-issue"):
                        # Tag-name chip becomes a bold paragraph; the
                        # inline-text + issue paragraphs follow.
                        tag_id = issue.attrib.get("id", "")
                        chip_name = tag_def.get(tag_id, tag_id)
                        chip_runs = Runs(pool)
                        chip_runs.add_text(chip_name, ["b"])
                        builder._write_block(
                            OP_PARA, chip_runs.serialize(), anchor=0
                        )
                        dump.append(f"      [p bold] {chip_name}")
                        for p in issue.findall("p"):
                            builder._emit_paragraph(p)
        elif child.tag == "compat-tags":
            continue  # already collected
        else:
            # Anything else (notes, divs, comments) — pass through.
            builder._block(child, [], False)
    return builder


# -- TOC ------------------------------------------------------------------

class TocNode:
    __slots__ = ("name", "href", "children")

    def __init__(self, name, href):
        self.name = name
        self.href = href
        self.children = []


def parse_toc(toc_xml_path):
    tree = ET.parse(toc_xml_path)
    root = tree.getroot()
    if root.tag != "toc":
        raise RuntimeError(f"toc.xml: root is <{root.tag}>")
    out_root = TocNode("", "")
    for child in root:
        if child.tag == "t":
            out_root.children.append(_parse_toc_entry(child))
    return out_root


def _parse_toc_entry(el):
    n = TocNode(el.attrib.get("name", ""), el.attrib.get("href", ""))
    for child in el:
        if child.tag == "t":
            n.children.append(_parse_toc_entry(child))
    return n


NODE_SIZE = 8  # u16 nameIdx, u16 hrefIdx, u16 hasChildren, u16 nextSiblingOff


def serialize_toc(root, pool):
    # Preorder layout.  Each node is 8 bytes:
    #   u16 nameStrIdx
    #   u16 hrefStrIdx
    #   u16 hasChildren     (0 = leaf, 1 = first child immediately follows)
    #   u16 nextSiblingOff  (byte offset of next sibling within node array,
    #                        or 0xFFFF for end of sibling chain)
    # Children of a non-leaf node always start at parent_offset + 8.
    # This avoids the "compute subtree size" walk at render time.
    flat = []

    def flatten(n):
        flat.append(n)
        for c in n.children:
            flatten(c)

    for c in root.children:
        flatten(c)

    node_offset = {}
    cur = 0
    for n in flat:
        node_offset[id(n)] = cur
        cur += NODE_SIZE

    # Compute next-sibling for each node.
    next_sib_off = {}

    def assign(parent_children):
        for i, n in enumerate(parent_children):
            if i + 1 < len(parent_children):
                next_sib_off[id(n)] = node_offset[id(parent_children[i + 1])]
            else:
                next_sib_off[id(n)] = 0xFFFF
            assign(n.children)

    assign(root.children)

    out = bytearray()
    for n in flat:
        out += struct.pack(
            "<HHHH",
            pool.add(n.name),
            pool.add(n.href),
            1 if n.children else 0,
            next_sib_off[id(n)],
        )

    # Header: u16 firstRootOff (or 0xFFFF if empty), u16 reserved,
    # u32 totalNodeBytes.
    first_root = node_offset[id(root.children[0])] if root.children else 0xFFFF
    header = struct.pack("<HHI", first_root, 0, len(out))
    return header + bytes(out)


# -- Topic conversion ------------------------------------------------------

def convert_topic(xml_path, pool, image_index, dump):
    tree = ET.parse(xml_path)
    root = tree.getroot()
    fname = os.path.basename(xml_path)
    if fname == "compatibility.xml":
        return convert_compatibility(root, pool, image_index, fname, dump)
    if fname == "toc.xml":
        return None
    builder = TopicBuilder(pool, image_index, fname, dump)
    builder.walk(root)
    return builder


# -- Top-level packing ------------------------------------------------------

def main(argv):
    if len(argv) < 3:
        sys.stderr.write("usage: bake_help.py <out_dir> <help_src_dir>\n")
        return 2
    out_dir = argv[1]
    src_dir = argv[2]
    os.makedirs(out_dir, exist_ok=True)

    # Discover topics & images.
    xml_files = sorted(
        f for f in os.listdir(src_dir)
        if f.endswith(".xml") and f != "toc.xml"
    )
    pics_dir = os.path.join(src_dir, "pics")
    image_files = []
    if os.path.isdir(pics_dir):
        image_files = sorted(
            f for f in os.listdir(pics_dir)
            if f.lower().endswith((".png", ".jpg", ".jpeg"))
        )

    pool = StringPool()
    image_index = {f: i for i, f in enumerate(image_files)}

    dump_lines = []

    # Read all image bytes up front.
    image_blobs = []
    for f in image_files:
        with open(os.path.join(pics_dir, f), "rb") as fh:
            image_blobs.append(fh.read())

    # Convert topics.
    topic_builders = []  # (filename, builder)
    for f in xml_files:
        builder = convert_topic(
            os.path.join(src_dir, f), pool, image_index, dump_lines
        )
        if builder is None:
            continue
        topic_builders.append((f, builder))
        dump_lines.append("")

    # --- Pack ----------------------------------------------------------

    # Layout (uncompressed payload, all offsets are byte offsets from
    # the start of the *uncompressed payload*):
    #   u32 topicCount
    #   u32 imageCount
    #   u32 tocOffset
    #   u32 topicTableOffset
    #   u32 imageTableOffset
    #   u32 stringPoolOffset
    #   u32 stringPoolSize
    #   u32 reserved
    # ... then concatenated sections.

    # Topic section: per-topic: u16 filenameStrIdx, u16 titleStrIdx,
    #                           u32 streamOffset, u32 streamSize,
    #                           u32 anchorOffset, u32 anchorCount,
    #                           u32 blockCount.
    # Image section: per-image: u16 nameStrIdx, u32 dataOffset, u32 dataSize.
    # Block streams concatenated.
    # Anchor tables concatenated.
    # Image data concatenated.
    # String pool last.

    # Compute sizes / offsets.
    HEADER_SIZE = 4 * 8

    topic_count = len(topic_builders)
    image_count = len(image_files)

    toc_blob = serialize_toc(parse_toc(os.path.join(src_dir, "toc.xml")), pool)

    streams_concat = bytearray()
    anchor_table_blob = bytearray()
    topic_records = []  # (filenameIdx, titleIdx, streamOff, streamSize,
                        #  anchorOff, anchorCount, blockCount)
    for f, b in topic_builders:
        stream = b.block_stream()
        stream_off = len(streams_concat)
        streams_concat += stream
        anchor_off = len(anchor_table_blob)
        anchors = b.anchors()
        for str_idx, blk_idx in anchors:
            anchor_table_blob += struct.pack("<HI", str_idx, blk_idx)
        topic_records.append((
            pool.add(f),
            pool.add(b.topic_title()),
            stream_off,
            len(stream),
            anchor_off,
            len(anchors),
            b.block_count(),
        ))

    image_records = []
    images_concat = bytearray()
    for f, blob in zip(image_files, image_blobs):
        off = len(images_concat)
        images_concat += blob
        image_records.append((pool.add(f), off, len(blob)))

    string_pool_blob = pool.serialize()

    # Allocate sections in order:
    # [header][toc][topic table][image table][streams][anchors][images][string pool]
    cursor = HEADER_SIZE
    toc_off = cursor
    cursor += len(toc_blob)
    topic_table_off = cursor
    cursor += topic_count * (2 + 2 + 4 + 4 + 4 + 4 + 4)
    image_table_off = cursor
    cursor += image_count * (2 + 4 + 4)
    streams_off = cursor
    cursor += len(streams_concat)
    anchors_off = cursor
    cursor += len(anchor_table_blob)
    images_off = cursor
    cursor += len(images_concat)
    string_pool_off = cursor
    cursor += len(string_pool_blob)
    total_size = cursor

    # Rewrite topic records' streamOff/anchorOff to absolute payload-byte
    # offsets, plus shift image data offsets too.
    payload = bytearray(total_size)
    # Header.
    struct.pack_into(
        "<IIIIIIII", payload, 0,
        topic_count,
        image_count,
        toc_off,
        topic_table_off,
        image_table_off,
        string_pool_off,
        len(string_pool_blob),
        0,  # reserved
    )
    # Sections.
    payload[toc_off:toc_off + len(toc_blob)] = toc_blob

    cur = topic_table_off
    for (filename_idx, title_idx, stream_off, stream_size,
         anchor_off, anchor_count, block_count) in topic_records:
        struct.pack_into(
            "<HHIIIII", payload, cur,
            filename_idx, title_idx,
            streams_off + stream_off, stream_size,
            anchors_off + anchor_off, anchor_count,
            block_count,
        )
        cur += 2 + 2 + 4 + 4 + 4 + 4 + 4

    cur = image_table_off
    for (name_idx, off, size) in image_records:
        struct.pack_into(
            "<HII", payload, cur,
            name_idx, images_off + off, size,
        )
        cur += 2 + 4 + 4

    payload[streams_off:streams_off + len(streams_concat)] = streams_concat
    payload[anchors_off:anchors_off + len(anchor_table_blob)] = anchor_table_blob
    payload[images_off:images_off + len(images_concat)] = images_concat
    payload[string_pool_off:string_pool_off + len(string_pool_blob)] = string_pool_blob

    # Compress with gzip; runtime decompression uses VDGUnzipStream
    # from the system library (already linked by AltirraSDL).  Header
    # is: magic (8 bytes) + uncompressedSize (u32) + compressedSize
    # (u32), followed by the gzip stream.
    compressed = gzip.compress(bytes(payload), compresslevel=9)
    final = (MAGIC + struct.pack("<II", total_size, len(compressed))
             + compressed)

    # --- Emit C++ -----------------------------------------------------
    write_cpp_blob(out_dir, final)

    # --- Emit dump ----------------------------------------------------
    dump_path = os.path.join(out_dir, "help_dump.txt")
    with open(dump_path, "w", encoding="utf-8") as fh:
        fh.write("\n".join(dump_lines))

    print(
        f"bake_help: {topic_count} topics, {image_count} images, "
        f"{pool.count()} strings; "
        f"uncompressed {total_size:,} B, "
        f"final blob {len(final):,} B"
    )
    return 0


def write_cpp_blob(out_dir, data):
    h_path = os.path.join(out_dir, "help_data.h")
    c_path = os.path.join(out_dir, "help_data.cpp")
    h_lines = [
        "// Auto-generated by tools/bake_help.py - do not edit.",
        "#pragma once",
        "#include <stdint.h>",
        "#include <stddef.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "extern const uint8_t kATHelpData[];",
        "extern const size_t  kATHelpDataSize;",
        "",
        "#ifdef __cplusplus",
        "}",
        "#endif",
        "",
    ]
    tmp = h_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write("\n".join(h_lines))
    os.replace(tmp, h_path)

    c_lines = [
        "// Auto-generated by tools/bake_help.py - do not edit.",
        '#include "help_data.h"',
        "",
        f"const uint8_t kATHelpData[{len(data)}] = {{",
    ]
    chunk = []
    for i, b in enumerate(data):
        chunk.append(f"0x{b:02x},")
        if (i + 1) % 16 == 0:
            c_lines.append("    " + " ".join(chunk))
            chunk = []
    if chunk:
        c_lines.append("    " + " ".join(chunk))
    c_lines.append("};")
    c_lines.append("")
    c_lines.append(f"const size_t kATHelpDataSize = {len(data)};")
    c_lines.append("")

    tmp = c_path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as fh:
        fh.write("\n".join(c_lines))
    os.replace(tmp, c_path)

    print(f"  wrote {h_path}")
    print(f"  wrote {c_path}")


if __name__ == "__main__":
    sys.exit(main(sys.argv))
