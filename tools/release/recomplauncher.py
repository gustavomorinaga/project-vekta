#!/usr/bin/env python3
"""recomplauncher.json for the release folders: what a launcher such as RecompLauncher needs to set the port up and
start it (the inputs it asks for, the prepare step, the program to start, the saves, folders and exit codes).

    tools/release/recomplauncher.py [--out FILE]     write it for this tree's VERSION (default: standard output)
    tools/release/recomplauncher.py --check          check installer/recomplauncher.json, and the file it makes

One source, installer/recomplauncher.json; build() stamps prepare.version with VERSION (a new release asks the
launcher to build again, as stale_build does in tools/release/launch.py) and fills exit_codes from port/kz1hd/
kz_exit.h, the one exit-code table, with "ok" for 0. assemble_payload.py puts the result at the top of the Windows
and the Linux folder (the one file serves both: start.program and prepare.program are tried in order). It is checked
against tools/release/recomplauncher.schema.json, the part of RecompLauncher's format 1 this project uses, written
here from its developer guide (RecompLauncher's own schema is GPL-3.0 and is not copied); check() covers the
draft-07 keywords that schema uses. Standard library only.
"""
import argparse
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SOURCE = os.path.join("installer", "recomplauncher.json")
SCHEMA = os.path.join("tools", "release", "recomplauncher.schema.json")


def exit_codes(root=ROOT):
    """{"code": text} from port/kz1hd/kz_exit.h, and "0": "ok" (a normal quit is no crash)."""
    with open(os.path.join(root, "port", "kz1hd", "kz_exit.h"), encoding="utf-8") as f:
        rows = re.findall(r"^#define KZ_EXIT_\w+\s+(\d+)\s*/\*\s*(.*?)\s*\*/", f.read(), re.M)
    return {"0": "ok", **{code: text for code, text in rows}}


def build(root=ROOT, version=None):
    """The recomplauncher.json document for this tree."""
    with open(os.path.join(root, SOURCE), encoding="utf-8") as f:
        doc = json.load(f)
    if version is None:
        with open(os.path.join(root, "VERSION"), encoding="utf-8") as f:
            version = f.read().strip()
    doc["prepare"]["version"] = version
    doc["exit_codes"] = exit_codes(root)
    return doc


def text(doc):
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


def check(value, schema, root=None, where="$"):
    """[problem] for value against a draft-07 schema, for the keywords recomplauncher.schema.json uses."""
    root = schema if root is None else root
    if "$ref" in schema:
        node = root
        for part in schema["$ref"].lstrip("#/").split("/"):
            node = node[part]
        return check(value, node, root, where)
    out = []
    t = schema.get("type")
    types = {"object": dict, "array": list, "string": str, "integer": int, "number": (int, float), "boolean": bool}
    if t and not (isinstance(value, types[t]) and not (t in ("integer", "number") and isinstance(value, bool))):
        return [f"{where}: not a {t}"]
    if "const" in schema and value != schema["const"]:
        out.append(f"{where}: must be {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        out.append(f"{where}: {value!r} is not one of {schema['enum']}")
    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            out.append(f"{where}: empty")
        if "pattern" in schema and not re.search(schema["pattern"], value):
            out.append(f"{where}: {value!r} does not match {schema['pattern']}")
    if isinstance(value, int) and "minimum" in schema and value < schema["minimum"]:
        out.append(f"{where}: below {schema['minimum']}")
    if isinstance(value, list):
        if not schema.get("minItems", 0) <= len(value) <= schema.get("maxItems", len(value)):
            out.append(f"{where}: {len(value)} items")
        for i, item in enumerate(value):
            out += check(item, schema.get("items", {}), root, f"{where}[{i}]")
    if isinstance(value, dict):
        props = schema.get("properties", {})
        out += [f"{where}: no {k}" for k in schema.get("required", []) if k not in value]
        if len(value) < schema.get("minProperties", 0):
            out.append(f"{where}: empty")
        for k, v in value.items():
            if "propertyNames" in schema:
                out += check(k, schema["propertyNames"], root, f"{where} key {k!r}")
            if k in props:
                out += check(v, props[k], root, f"{where}.{k}")
            elif schema.get("additionalProperties") is False:
                out.append(f"{where}: {k} is not allowed")
            elif isinstance(schema.get("additionalProperties"), dict):
                out += check(v, schema["additionalProperties"], root, f"{where}.{k}")
    return out


def problems(root=ROOT):
    """[problem] for the source and the document it makes: the schema, and the references inside it."""
    with open(os.path.join(root, SCHEMA), encoding="utf-8") as f:
        schema = json.load(f)
    doc = build(root)
    out = check(doc, schema)
    names = set(doc["folders"])
    out += [f"actions: {a['id']} opens {a['open']!r}, no such folder" for a in doc["actions"]
            if a.get("open") and a["open"] not in names]
    ids = {i["id"] for i in doc["inputs"]}
    used = set(re.findall(r"\{input:([^}]+)\}", json.dumps(doc)))
    out += [f"{{input:{u}}} names no input" for u in sorted(used - ids)]
    out += [f"optional input {i['id']} is not passed as one argument (--x={{input:{i['id']}}})" for i in doc["inputs"]
            if i.get("optional") and not any(a.endswith(f"={{input:{i['id']}}}") for a in doc["prepare"]["args"])]
    out += [f"input {i['id']}: dumps, but no file format (the launcher reads only a file whole)" for i in doc["inputs"]
            if i.get("dumps") and not any(f["type"] == "file" for f in i["formats"])]
    out += settings_problems(doc)
    return out


def setting_keys(s):
    """The settings-file keys a setting writes: its slider's key, or its choices' set keys."""
    return {s["slider"]["key"]} if "slider" in s else {k for c in s.get("choices", []) for k in c["set"]}


def settings_problems(doc):
    """[problem] for settings that refer to what is not there: each has choices or a slider (not both), its defaults
    are the keys it writes, every choice writes the same keys, and a when names a key some setting writes with a
    value one of its choices has."""
    out, settings = [], doc.get("settings", [])
    if settings and not doc.get("config"):
        out.append("settings without config: no file to write them in")
    values = {}
    for s in settings:
        if ("choices" in s) == ("slider" in s):
            out.append(f"settings: {s['id']} needs choices or a slider, one of them")
            continue
        keys = setting_keys(s)
        if set(s["defaults"]) != keys:
            out.append(f"settings: {s['id']}'s defaults {sorted(s['defaults'])} are not the keys it writes {sorted(keys)}")
        if "choices" in s:
            if any(set(c["set"]) != keys for c in s["choices"]):
                out.append(f"settings: {s['id']}'s choices do not all write the same keys")
            for c in s["choices"]:
                for k, v in c["set"].items():
                    values.setdefault(k, set()).add(v)
    for s in settings:
        for k, v in s.get("when", {}).items():
            if k not in values:
                out.append(f"settings: {s['id']} is greyed by {k}, which no setting with choices writes")
            elif v not in values[k]:
                out.append(f"settings: {s['id']} is greyed unless {k}={v}, which no choice writes")
    ids = [s["id"] for s in settings]
    out += [f"settings: id {i} used twice" for i in sorted({i for i in ids if ids.count(i) > 1})]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", help="write it here (default: standard output)")
    ap.add_argument("--check", action="store_true", help="check the source and the document against the schema")
    ap.add_argument("--root", default=ROOT)
    a = ap.parse_args(argv)
    if a.check:
        found = problems(a.root)
        for p in found:
            print("recomplauncher.json:", p, file=sys.stderr)
        print("recomplauncher.json: " + ("FAIL" if found else "ok"))
        return 1 if found else 0
    t = text(build(a.root))
    if a.out:
        with open(a.out, "w", encoding="utf-8", newline="\n") as f:
            f.write(t)
    else:
        sys.stdout.write(t)
    return 0


if __name__ == "__main__":
    sys.exit(main())
