#!/usr/bin/env python3
"""Turn BlueBrick's compiled help (BlueBrick.chm) into the offline HTML help
shipped with Brick Layout Designer (help/en/).

    scripts/help/chm_to_html.py /path/to/BlueBrick.chm help/en

Needs 7z to unpack the .chm. The pages are copied flat (their "../"
references to the style sheet and a few pictures are rewritten), and an
index.html is generated from the table of contents (BlueBrickTOC_en.hhc):
the contents on the left, the pages on the right.
"""
import html
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


def toc_entries(hhc):
    """Yield (depth, name, page) from a .hhc sitemap."""
    depth = 0
    for token in re.finditer(r'<(/?)UL>|<OBJECT type="text/sitemap">(.*?)</OBJECT>', hhc, re.S | re.I):
        if token.group(0).upper() == "<UL>":
            depth += 1
        elif token.group(0).upper() == "</UL>":
            depth -= 1
        else:
            params = dict(re.findall(r'<param name="(\w+)" value="([^"]*)"', token.group(2), re.I))
            yield depth, params.get("Name", ""), params.get("Local", "")


def index_page(entries):
    items, depth = [], 1
    for level, name, page in entries:
        while depth < level:
            items.append("<ul>")
            depth += 1
        while depth > level:
            items.append("</ul>")
            depth -= 1
        items.append('<li><a href="%s" target="page">%s</a></li>' % (html.escape(page), html.escape(name)))
    items.extend("</ul>" for _ in range(depth - 1))
    return """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Brick Layout Designer Help</title>
<style>
  body { margin: 0; display: flex; height: 100vh; font-family: sans-serif; }
  nav { width: 18em; overflow: auto; padding: 0.5em 1em; border-right: 1px solid #ccc; font-size: 90%%; }
  nav ul { padding-left: 1.2em; margin: 0.2em 0; }
  nav p { font-size: 85%%; color: #555; }
  iframe { flex: 1; border: 0; height: 100%%; }
</style>
</head>
<body>
<nav>
<h3>Contents</h3>
<p>Brick Layout Designer follows BlueBrick, so this is BlueBrick's own
manual (by Alban Nanty and Alex McKenna). Where the two differ, see the
<a href="https://github.com/brick-layout-designer/brick-layout-designer#readme" target="_blank">README</a>.</p>
<ul>
%s
</ul>
</nav>
<iframe name="page" src="Start_Page.htm"></iframe>
</body>
</html>
""" % "\n".join(items)


def main(chm, out):
    out = pathlib.Path(out)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["7z", "x", "-y", "-o" + tmp, chm], check=True, stdout=subprocess.DEVNULL)
        src = pathlib.Path(tmp)
        if out.exists():
            shutil.rmtree(out)
        out.mkdir(parents=True)
        for f in sorted(src.iterdir()):
            suffix = f.suffix.lower()
            if suffix in (".png", ".css", ".gif", ".jpg"):
                shutil.copy(f, out / f.name)
            elif suffix == ".htm":
                text = f.read_text(encoding="latin-1")
                text = re.sub(r'((?:src|href)="|url\()\.\./', r"\1", text, flags=re.I)
                (out / f.name).write_text(text, encoding="latin-1", newline="")
        hhc = next(src.glob("*.hhc")).read_text(encoding="latin-1")
        (out / "index.html").write_text(index_page(toc_entries(hhc)), encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
