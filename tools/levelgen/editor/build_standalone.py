#!/usr/bin/env python3
"""Make a one-file copy of the level editor (tools/levelgen/editor/README.md): index.html with layout.js, edits.js and editor.js
inlined and a layout embedded as the sample it opens with. Works from file:// or as a published page.

    py -3 tools/levelgen/editor/build_standalone.py <out.html> [layout.json] [--selftest]
        layout: default tools/levelgen/janus_bank_v3.json; --selftest also inlines selftest.js (open it with #selftest)
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def script(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return "<script>\n" + f.read().replace("</script", "<\\/script") + "</script>"


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = args[0]
    layout = args[1] if len(args) > 1 else os.path.join(HERE, "..", "janus_bank_v3.json")
    with open(os.path.join(HERE, "index.html"), encoding="utf-8") as f:
        html = f.read()
    html = re.sub(r'<script src="([\w.]+)"></script>', lambda m: script(m.group(1)), html)
    if "--selftest" in sys.argv:
        html = html.replace("</body>", script("selftest.js") + "\n</body>")
    with open(layout, encoding="utf-8") as f:
        text = f.read()
    # "<\/" is the same string to a JSON reader ("\/" is "/"), and cannot close the script element
    sample = text.replace("</", "<\\/")
    name = os.path.basename(layout)
    html = html.replace('<script type="application/json" id="sample-layout" data-name="janus_bank_v3.json"></script>',
                        '<script type="application/json" id="sample-layout" data-name="%s">%s</script>' % (name, sample))
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(html)
    print("wrote %s (%d KB, sample %s)" % (out, len(html.encode()) // 1024, name))


if __name__ == "__main__":
    main()
