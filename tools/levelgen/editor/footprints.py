#!/usr/bin/env python3
"""Write the prop footprints a layout's doorway clearance check needs, read off your extracted stock models
(gen_level.prop_half_extents), as {model id: [half width, half depth]}. Drop the file on the level editor (or pass it to
cli.js validate --footprints) and the editor checks props near doorways exactly as gen_level does. The file is derived from your
own extracted assets: keep it out of the repo (tools/levelgen/preview.sh writes it to build_preview/).

    python3 tools/levelgen/editor/footprints.py <layout.json> <out.json>
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_level as g      # noqa: E402


def main():
    with open(sys.argv[1]) as f:
        L = json.load(f)
    out = {}
    for r in L.get("rooms", []):
        for o in r.get("objects", []):
            if o.get("type") == "prop" and "size" not in o:
                m = g.ival(o.get("model"))
                e = g.prop_half_extents(ROOT, m)
                if e is not None:
                    out[str(m)] = [e[0], e[1]]
    with open(sys.argv[2], "w") as f:
        json.dump(out, f, indent=1, sort_keys=True)
    print("footprints: %d prop models -> %s" % (len(out), sys.argv[2]))


if __name__ == "__main__":
    main()
