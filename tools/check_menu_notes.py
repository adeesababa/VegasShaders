# Checks the menu's simple views and "hiding effects" notes against the shipped defaults (2026-11-05).
#   py -3.11 -I tools/check_menu_notes.py [repo]      exit 1 on any problem
# 1. kSimpleRows: every row's setting and every choice's Key=Value exists in resource/VegasShaders.defaults.toml under that section,
#    with a value of the setting's type (true/false for a switch, a number for a number) and inside the "[lo-hi]" range its comment gives.
# 2. kDebugViews: every setting in the defaults whose name says Debug (except the developer-only ones below) is listed, so a debug view
#    left on always shows in the notes at the bottom of the menu.
# 3. kHidingNotes / kDebugViews: every Section/Key they name exists.
import os, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MENU = os.path.join(ROOT, "src", "core", "ImGuiManager.cpp")
DEFAULTS = os.path.join(ROOT, "resource", "VegasShaders.defaults.toml")
NOT_VIEWS = {("Main.Develop.Main", "DebugMode"), ("Main.Develop.Main", "GpuDebugLabels"), ("Main.Develop.Main", "DebugVar1"),
             ("Main.Develop.Main", "DebugVar2"), ("Main.Develop.Main", "DebugVar3"), ("Main.Develop.Main", "DebugVar4")}


def defaults():
    out, sec = {}, None
    for line in open(DEFAULTS, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*\[_?([^\]]+)\]", line)
        if m: sec = m.group(1); continue
        m = re.match(r"\s*([A-Za-z0-9_]+)\s*=\s*(\"[^\"]*\"|[^#\s]+)\s*(#.*)?$", line.rstrip("\r\n"))
        if m and sec:
            v = m.group(2); comment = m.group(3) or ""
            kind = "bool" if v in ("true", "false") else "str" if v.startswith('"') else "num"
            r = re.search(r"\[\s*(-?[0-9.]+)\s*-\s*(-?[0-9.]+)\s*\]", comment)
            out[(sec, m.group(1))] = (kind, (float(r.group(1)), float(r.group(2))) if r else None)
    return out


class L(list): joinable = False


def table(src, name):
    a = src.index(name); a = src.index("{", src.index("=", a)) + 1
    depth, i, rows, start = 0, a, [], None
    while True:
        c = src[i]
        if c == '"':
            i = src.index('"', i + 1) if src[i + 1] != '"' else i + 1
            while src[i - 1] == "\\": i = src.index('"', i + 1)
        elif c == "{":
            depth += 1
            if depth == 1: start = i
        elif c == "}":
            if depth == 0: break
            depth -= 1
            if depth == 0: rows.append(src[start + 1:i])
        i += 1
    out = []
    for r in rows:
        lst = L()
        j = 0
        while j < len(r):
            if r.startswith("//", j): k = r.find("\n", j); j = len(r) if k < 0 else k; continue
            if r[j] == '"':
                k, s = j + 1, ""
                while r[k] != '"':
                    if r[k] == "\\": s += r[k + 1]; k += 2
                    else: s += r[k]; k += 1
                if lst and lst.joinable and lst[-1] is not None: lst[-1] += s
                else: lst.append(s)
                lst.joinable = True; j = k + 1; continue
            if r.startswith("nullptr", j): lst.append(None); lst.joinable = False; j += 7; continue
            if r[j] == ",": lst.joinable = False
            j += 1
        out.append(list(lst))
    return out


def main():
    d = defaults(); src = open(MENU, encoding="utf-8").read(); bad = []

    def check_value(where, sec, key, value):
        if (sec, key) not in d: bad.append("%s: %s/%s is not in the defaults" % (where, sec, key)); return
        kind, rng = d[(sec, key)]
        if kind == "bool" and value not in ("true", "false"): bad.append("%s: %s/%s is a switch, given %r" % (where, sec, key, value))
        if kind == "num":
            try: f = float(value)
            except ValueError: bad.append("%s: %s/%s is a number, given %r" % (where, sec, key, value)); return
            if rng and not (rng[0] - 1e-6 <= f <= rng[1] + 1e-6): bad.append("%s: %s/%s=%s is outside [%g-%g]" % (where, sec, key, value, rng[0], rng[1]))

    rows = table(src, "kSimpleRows[]")
    for r in rows:
        sec, key, label, options = r[0], r[1], r[2], r[3] if len(r) > 3 else None
        if key and (sec, key) not in d: bad.append("simple row %r: %s/%s is not in the defaults" % (label, sec, key))
        if not key and options:
            for part in options.split("|"):
                name, _, kvs = part.partition(":")
                for kv in kvs.split():
                    k, _, v = kv.partition("=")
                    check_value("choice %r / %r" % (label, name.strip()), sec, k, v)

    listed = set()
    for r in table(src, "kDebugViews[]"):
        listed.add((r[0], r[1]))
        if (r[0], r[1]) not in d: bad.append("kDebugViews: %s/%s is not in the defaults" % (r[0], r[1]))
    for (sec, key) in d:
        if "debug" in key.lower() and (sec, key) not in listed and (sec, key) not in NOT_VIEWS and not sec.startswith("Main.Develop"):
            bad.append("debug setting %s/%s is not in kDebugViews (a view left on would not show in the menu's notes)" % (sec, key))

    for m in re.finditer(r'"((?:Main|Shaders)\.[A-Za-z0-9.]+)/([A-Za-z0-9_]+)=([^" ]+)', src[src.index("kHidingNotes[]"):src.index("static void ApplySets")]):
        check_value("hiding note", m.group(1), m.group(2), m.group(3))

    print("%d simple rows, %d debug views checked" % (len(rows), len(listed)))
    for b in bad: print("PROBLEM:", b)
    print("OK" if not bad else "%d problem(s)" % len(bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
