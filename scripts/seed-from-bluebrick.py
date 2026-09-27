#!/usr/bin/env python3
"""Fill untranslated strings in translations/*.ts from BlueBrick's own
translations, wherever our English string is exactly one of BlueBrick's.

    scripts/seed-from-bluebrick.py <BlueBrick source dir, the one holding MainForm.resx>

BlueBrick 1.9.2 ships French, German, Spanish, Italian, Dutch, Portuguese,
Swedish and Norwegian, done by its translators. Its .resx files are paired
key by key (Form.resx with Form.<lang>.resx) to map each English text to its
translation. A match ignores "&" keyboard-accelerator marks; the translation
gets an accelerator only when ours has one. Strings with %-placeholders are
skipped; finished translations are left alone. Seeded entries are
marked finished: they are BlueBrick's shipped translations.
"""
import collections
import glob
import os
import re
import sys
import xml.etree.ElementTree as ET

LANGS = {"fr": "fr", "de": "de", "es": "es", "it": "it", "nl": "nl", "pt": "pt", "sv": "sv", "no": "no"}


def resx_texts(path):
    out = {}
    for data in ET.parse(path).getroot().iter("data"):
        if data.get("type") or data.get("mimetype"):
            continue
        name = data.get("name", "")
        # Form resources: only visible texts, not sizes, positions, ...
        if "." in name and not name.endswith((".Text", ".ToolTipText", ".ToolTip")):
            continue
        value = data.find("value")
        if value is not None and value.text and value.text.strip():
            out[name] = value.text
    return out


def norm(text):
    return re.sub(r"\s+", " ", text.replace("&&", "\0").replace("&", "").replace("\0", "&")).strip()


def dictionaries(bluebrick):
    tables = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))
    for base in glob.glob(os.path.join(bluebrick, "**", "*.resx"), recursive=True):
        stem, _ = os.path.splitext(base)
        if re.search(r"\.[a-z]{2}$", stem):
            continue
        english = resx_texts(base)
        for code, suffix in LANGS.items():
            translated_path = f"{stem}.{suffix}.resx"
            if not os.path.exists(translated_path):
                continue
            for key, text in resx_texts(translated_path).items():
                if key in english and text.strip() and text != english[key]:
                    tables[code][norm(english[key])][text] += 1
    # The most common translation of each English text.
    return {code: {en: c.most_common(1)[0][0] for en, c in t.items()} for code, t in tables.items()}


def seed(ts_path, table):
    tree = ET.parse(ts_path)
    seeded = 0
    for message in tree.getroot().iter("message"):
        source = message.findtext("source") or ""
        translation = message.find("translation")
        # Unfinished only (text there is just lupdate's guess from similar strings).
        if translation is None or translation.get("type") != "unfinished":
            continue
        if "%" in source or message.get("numerus") == "yes":
            continue
        found = table.get(norm(source))
        if not found:
            continue
        if "&" not in source.replace("&&", ""):
            found = found.replace("&&", "\0").replace("&", "").replace("\0", "&&")
        # Keep our leading / trailing whitespace (" studs", "Name: ").
        lead = source[: len(source) - len(source.lstrip())]
        trail = source[len(source.rstrip()):]
        found = lead + found.strip() + trail
        translation.text = found
        del translation.attrib["type"]
        seeded += 1
    tree.write(ts_path, encoding="utf-8", xml_declaration=True)
    # Keep lupdate's DOCTYPE line.
    with open(ts_path, encoding="utf-8") as f:
        body = f.read()
    if "<!DOCTYPE TS>" not in body:
        body = body.replace("?>\n", "?>\n<!DOCTYPE TS>\n", 1)
        with open(ts_path, "w", encoding="utf-8") as f:
            f.write(body)
    return seeded


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    tables = dictionaries(sys.argv[1])
    root = os.path.join(os.path.dirname(__file__), "..", "translations")
    for code, table in sorted(tables.items()):
        path = os.path.join(root, f"bld_{code}.ts")
        if os.path.exists(path):
            print(f"{code}: {seed(path, table)} strings seeded from {len(table)} BlueBrick texts")


if __name__ == "__main__":
    main()
