#!/usr/bin/env python3
"""Host test for references to doc headings: wherever the tree cites `docs/....md, "Heading"` (code comments, READMEs,
docs; not the tests themselves), that file must exist and have a heading that starts with that text (a heading's
trailing date may be left out).
Catches headings that were renamed, and citations left in the language the notes were first written in."""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout.split()
REF = re.compile(r'(docs/[\w./-]+\.md),\s*"([^"]+)"')
fails = []
seen = 0
for f in files:
    if f.startswith(("third_party/", "tools/tests/")) or not f.endswith((".md", ".c", ".h", ".cpp", ".py", ".sh", ".txt", ".patch")):
        continue
    text = open(os.path.join(ROOT, f), encoding="utf-8", errors="replace").read()
    # A citation may wrap onto the next comment line: join " * " and "# " continuations.
    flat = re.sub(r"\n\s*(?:\*|#|//)?\s*", " ", text)
    for doc, heading in REF.findall(flat):
        seen += 1
        path = os.path.join(ROOT, doc)
        if not os.path.exists(path):
            fails.append(f"{f}: cites {doc}, which does not exist")
            continue
        heads = [re.sub(r"^#+\s*", "", l).strip() for l in open(path, encoding="utf-8") if l.startswith("#")]
        if not any(h.startswith(heading) for h in heads):
            fails.append(f'{f}: cites {doc}, "{heading}", which is not a heading there')
for f in fails:
    print("FAIL:", f)
print(f"test_doc_refs: {seen} citations,", "FAIL" if fails else "ok")
sys.exit(1 if fails else 0)
