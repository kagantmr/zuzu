#!/usr/bin/env python3
"""Generate zuzu-docs Jekyll pages from Doxygen XML.

Consumes the XML produced by ``doxygen docs/Doxyfile`` and emits one
Markdown page per Doxygen group into the zuzu-docs collection folders.

    doxygen docs/Doxyfile
    python3 docs/gen_docs.py --out ../zuzu-docs

Currently wired for the ``sync`` group tree -> ``_sync/<slug>.md`` using
the ``primitive`` layout. The ``api`` front-matter block is a signature
index; the page body carries the struct fields and a full section per
function (prose, parameters, return, error table, notes).
"""

from __future__ import annotations

import argparse
import copy
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Doxygen group id prefix -> zuzu-docs collection dir
COLLECTIONS = {
    "group__sync__": "_sync",
}

STRUCTURED_TAGS = {"parameterlist", "simplesect", "xrefsect"}
ADMONITIONS = {"note", "warning", "attention", "remark", "pre", "post"}


# --------------------------------------------------------------------------
# Description -> Markdown
# --------------------------------------------------------------------------
def flatten(node: ET.Element | None) -> str:
    """Render a Doxygen description node to Markdown-ish text."""
    if node is None:
        return ""
    out: list[str] = []

    def walk(el: ET.Element) -> None:
        tag = el.tag
        if tag == "para":
            if el.text:
                out.append(el.text)
            for child in el:
                walk(child)
            out.append("\n\n")
        elif tag in ("computeroutput", "verbatim"):
            out.append(f"`{''.join(el.itertext())}`")
        elif tag in ("emphasis", "italic"):
            out.append(f"*{''.join(el.itertext())}*")
        elif tag in ("bold", "strong"):
            out.append(f"**{''.join(el.itertext())}**")
        elif tag == "ref":
            out.append("".join(el.itertext()))
        elif tag == "programlisting":
            lines = [_codeline(c) for c in el.findall("codeline")]
            out.append("\n```c\n" + "\n".join(lines) + "\n```\n\n")
        elif tag == "simplesect":
            kind = el.get("kind", "note")
            out.append(f"\n> **{kind.capitalize()}:** {_inline(el)}\n\n")
        elif tag in ("itemizedlist", "orderedlist"):
            for i, item in enumerate(el.findall("listitem"), 1):
                bullet = "-" if tag == "itemizedlist" else f"{i}."
                out.append(f"{bullet} {_inline(item)}\n")
            out.append("\n")
        elif tag in ("parameterlist", "xrefsect"):
            pass  # rendered structurally by the caller
        else:
            if el.text:
                out.append(el.text)
            for child in el:
                walk(child)
        if el.tail:
            out.append(el.tail)

    if node.text:
        out.append(node.text)
    for child in node:
        walk(child)
    text = re.sub(r"[ \t]+\n", "\n", "".join(out))
    return re.sub(r"\n{3,}", "\n\n", text).strip()


def _codeline(cl: ET.Element) -> str:
    buf: list[str] = []

    def rec(el: ET.Element) -> None:
        if el.tag == "sp":
            buf.append(" ")
        if el.text:
            buf.append(el.text)
        for c in el:
            rec(c)
        if el.tail:
            buf.append(el.tail)

    if cl.text:
        buf.append(cl.text)
    for c in cl:
        rec(c)
    return "".join(buf)


def _inline(node: ET.Element | None) -> str:
    return re.sub(r"\s+", " ", flatten(node)).strip()


def _prose(dd: ET.Element | None) -> str:
    """Detailed description with the structured blocks removed."""
    if dd is None:
        return ""
    clone = copy.deepcopy(dd)

    def strip(el: ET.Element) -> None:
        for child in list(el):
            if child.tag in STRUCTURED_TAGS:
                el.remove(child)
            else:
                strip(child)

    strip(clone)
    return flatten(clone)


def _param_pairs(dd: ET.Element | None, kind: str) -> list[tuple[str, str]]:
    pairs: list[tuple[str, str]] = []
    if dd is None:
        return pairs
    for pl in dd.iter("parameterlist"):
        if pl.get("kind") != kind:
            continue
        for item in pl.findall("parameteritem"):
            names = [n.text or "" for n in item.iter("parametername")]
            desc = _inline(item.find("parameterdescription"))
            pairs.append((", ".join(n for n in names if n), desc))
    return pairs


def _return(dd: ET.Element | None) -> str:
    if dd is None:
        return ""
    for ss in dd.iter("simplesect"):
        if ss.get("kind") == "return":
            return _inline(ss)
    return ""


def _admonitions(dd: ET.Element | None) -> list[tuple[str, str]]:
    res: list[tuple[str, str]] = []
    if dd is None:
        return res
    for ss in dd.iter("simplesect"):
        if ss.get("kind") in ADMONITIONS:
            res.append((ss.get("kind"), _inline(ss)))
    return res


# --------------------------------------------------------------------------
# Front matter
# --------------------------------------------------------------------------
def yaml_str(s: str) -> str:
    if s and re.search(r'[:#\[\]{}",\n]|^\s|\s$', s):
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return s


def proto_of(m: ET.Element) -> str:
    name = (m.findtext("name") or "").strip()
    if m.get("kind") == "define":
        params = [p.findtext("defname", "").strip() for p in m.findall("param")]
        return f"#define {name}({', '.join(params)})" if params else f"#define {name}"
    defn = (m.findtext("definition") or name).strip()
    return f"{defn}{(m.findtext('argsstring') or '').strip()}"


# --------------------------------------------------------------------------
# Assembly
# --------------------------------------------------------------------------
def public_members(group: ET.Element) -> list[ET.Element]:
    out: list[ET.Element] = []
    for sec in group.findall("sectiondef"):
        for m in sec.findall("memberdef"):
            if m.get("prot") == "public" and m.get("static") != "yes":
                out.append(m)
    return out


def struct_fields(xmldir: Path, refid: str) -> list[tuple[str, str, str]]:
    path = xmldir / f"{refid}.xml"
    if not path.exists():
        return []
    cd = ET.parse(path).getroot().find("compounddef")
    rows: list[tuple[str, str, str]] = []
    for m in cd.iter("memberdef"):
        if m.get("kind") != "variable":
            continue
        rows.append(
            (
                m.findtext("name", "").strip(),
                (m.findtext("type") or "").strip(),
                _inline(m.find("briefdescription")),
            )
        )
    return rows


def md_table(headers: list[str], rows: list[tuple[str, ...]]) -> str:
    line = "| " + " | ".join(headers) + " |\n"
    line += "|" + "|".join("---" for _ in headers) + "|\n"
    for r in rows:
        line += "| " + " | ".join(c.replace("|", "\\|") for c in r) + " |\n"
    return line


def render_function(m: ET.Element) -> str:
    dd = m.find("detaileddescription")
    name = m.findtext("name", "").strip()
    parts = [f"### `{name}`", "", "```c", proto_of(m), "```", ""]

    prose = _prose(dd)
    if prose:
        parts += [prose, ""]

    params = _param_pairs(dd, "param")
    if params:
        parts += ["**Parameters**", "", md_table(["Name", "Description"],
                  [(f"`{n}`", d) for n, d in params]).rstrip(), ""]

    ret = _return(m.find("detaileddescription"))
    if ret:
        parts += [f"**Returns** — {ret}", ""]

    retvals = _param_pairs(dd, "retval")
    if retvals:
        parts += ["**Errors**", "", md_table(["Code", "Condition"],
                  [(f"`{n}`", d) for n, d in retvals]).rstrip(), ""]

    for kind, text in _admonitions(dd):
        parts += [f"> **{kind.capitalize()}:** {text}", ""]

    return "\n".join(parts).rstrip()


def render_primitive(group: ET.Element, slug: str, xmldir: Path) -> str:
    title = (group.findtext("title") or slug).strip()
    summary = _inline(group.find("briefdescription"))
    overview = flatten(group.find("detaileddescription"))
    members = public_members(group)

    header = ""
    for m in members:
        loc = m.find("location")
        if loc is not None and loc.get("file"):
            header = loc.get("file").split("include/", 1)[-1]
            break

    fm = ["---", f"name: {yaml_str(title)}"]
    if header:
        fm.append(f"header: {yaml_str(header)}")
    if summary:
        fm.append(f"summary: {yaml_str(summary)}")
    if members:
        fm.append("api:")
        for m in members:
            fm.append(f"  - proto: {yaml_str(proto_of(m))}")
            fm.append(f"    desc: {yaml_str(_inline(m.find('briefdescription')))}")
    fm.append("---")

    body = ["", "<!-- AUTOGENERATED by docs/gen_docs.py from Doxygen XML. Do not edit. -->", ""]
    if overview:
        body += [overview, ""]

    for ic in group.findall("innerclass"):
        rows = struct_fields(xmldir, ic.get("refid", ""))
        if rows:
            body += [f"## `{ic.text}` fields", "",
                     md_table(["Field", "Type", "Description"],
                              [(f"`{n}`", f"`{t}`", d) for n, t, d in rows]).rstrip(), ""]

    funcs = [m for m in members if m.get("kind") in ("function", "define")]
    if funcs:
        body += ["## API", ""]
        for m in funcs:
            body += [render_function(m), ""]

    return "\n".join(fm) + "\n" + "\n".join(body).rstrip() + "\n"


# --------------------------------------------------------------------------
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--xml", default=Path("docs/doxygen/xml"), type=Path)
    ap.add_argument("--out", default=Path("../zuzu-docs"), type=Path)
    ap.add_argument("--check", action="store_true",
                    help="exit 1 if any output would change (drift check)")
    args = ap.parse_args()

    if not (args.xml / "index.xml").exists():
        print(f"error: {args.xml}/index.xml not found -- run doxygen first", file=sys.stderr)
        return 2

    drift = False
    seen = 0
    for xmlfile in sorted(args.xml.glob("group__*.xml")):
        group = ET.parse(xmlfile).getroot().find("compounddef")
        if group is None or group.get("kind") != "group":
            continue
        gid = group.get("id", "")
        for prefix, coll in COLLECTIONS.items():
            if not gid.startswith(prefix) or gid == prefix.rstrip("_"):
                continue
            slug = gid[len(prefix):].replace("__", "-")
            text = render_primitive(group, slug, args.xml)
            dest = args.out / coll / f"{slug}.md"
            dest.parent.mkdir(parents=True, exist_ok=True)
            old = dest.read_text() if dest.exists() else None
            changed = old != text
            drift |= changed
            if changed and not args.check:
                dest.write_text(text)
            seen += 1
            print(f"{'DRIFT ' if changed else 'ok    '}{dest}")

    if not seen:
        print("warning: no matching groups found", file=sys.stderr)
    if args.check and drift:
        print("\nerror: generated docs are stale -- run docs/gen_docs.py", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
