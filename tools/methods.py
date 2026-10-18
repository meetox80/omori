import argparse
import json
import pathlib
import re
import sys

CORE = ["rpg_core.js", "rpg_managers.js", "rpg_objects.js", "rpg_scenes.js", "rpg_sprites.js", "rpg_windows.js"]

DEF_RE = re.compile(
    r"^\s*(?:(?:var|let|const)\s+)?([A-Za-z_$][\w$]*)(\.prototype)?\.([A-Za-z_$][\w$]*)\s*=\s*(?:async\s+)?function\b",
)
CLASS_RE = re.compile(r"^\s*(?:(?:var|let|const)\s+)?([A-Za-z_$][\w$]*)\s*=\s*class\s+extends\s+([A-Za-z_$][\w$.]*)\s*\{")
CLASS_DECL_RE = re.compile(r"^\s*class\s+([A-Za-z_$][\w$]*)(?:\s+extends\s+([A-Za-z_$][\w$.]*))?\s*\{")
METHOD_RE = re.compile(r"^\s*(static\s+)?(?:get\s+|set\s+)?([A-Za-z_$][\w$]*)\s*\([^)]*\)\s*\{")


REGEX_PREV = set("(,=:[!&|?{};+-*%<>~^")
REGEX_WORDS = {"return", "typeof", "case", "do", "else", "in", "of", "new", "delete", "void", "throw", "yield", "await"}


def strip_js(text, blank_literals=True):
    out = list(text)
    n = len(text)
    i = 0
    last = ""
    word = ""

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            j = text.find("\n", i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
            continue
        if c == "/" and i + 1 < n and text[i + 1] == "*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
            continue
        if c in "'\"`":
            j = i + 1
            while j < n and text[j] != c:
                if text[j] == "\\":
                    j += 1
                elif text[j] == "\n" and c != "`":
                    break
                j += 1
            if blank_literals:
                blank(i + 1, min(j, n))
            i = j + 1
            last = c
            word = ""
            continue
        if c == "/" and (last == "" or last in REGEX_PREV or word in REGEX_WORDS):
            j = i + 1
            in_class = False
            while j < n and text[j] != "\n":
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "[":
                    in_class = True
                elif text[j] == "]":
                    in_class = False
                elif text[j] == "/" and not in_class:
                    break
                j += 1
            if j < n and text[j] == "/":
                if blank_literals:
                    blank(i + 1, j)
                i = j + 1
                last = "/"
                word = ""
                continue
        if not c.isspace():
            if c.isalnum() or c in "_$":
                word = word + c if (last.isalnum() or last in "_$") else c
            else:
                word = ""
            last = c
        i += 1
    return "".join(out)


def plugin_order(root):
    text = (root / "js" / "plugins.js").read_text(encoding="utf-8", errors="replace")
    start, end = text.index("["), text.rindex("]")
    body = re.sub(r",\s*\]$", "]", text[start : end + 1])
    return [p["name"] for p in json.loads(body) if p["status"]]


def files(root):
    out = [root / "js" / f for f in CORE]
    for name in plugin_order(root):
        p = root / "js" / "plugins" / (name + ".js")
        if p.exists():
            out.append(p)
    return out


def scan(path):
    lines = strip_js(path.read_text(encoding="utf-8", errors="replace")).split("\n")
    defs = []
    cls = None
    depth = 0
    cls_depth = 0
    for i, line in enumerate(lines):
        m = DEF_RE.match(line)
        if m:
            owner, proto, name = m.group(1), m.group(2), m.group(3)
            defs.append((owner + (".prototype" if proto else ""), name, i + 1))
        m = CLASS_RE.match(line) or CLASS_DECL_RE.match(line)
        if m and cls is None:
            cls = m.group(1)
            cls_depth = depth
        elif cls is not None and depth == cls_depth + 1:
            mm = METHOD_RE.match(line)
            if mm and mm.group(2) not in ("if", "for", "while", "switch", "catch", "function", "return"):
                defs.append((cls + ("" if mm.group(1) else ".prototype"), mm.group(2), i + 1))
        depth += line.count("{") - line.count("}")
        if cls is not None and depth <= cls_depth:
            cls = None
    return defs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("query", nargs="*")
    ap.add_argument("--show", action="store_true")
    ap.add_argument("--list-class")
    ap.add_argument("--dump-class")
    ap.add_argument("--plugin")
    args = ap.parse_args()
    root = pathlib.Path(args.root)
    if args.plugin:
        p = root / "js" / "plugins" / (args.plugin + ".js")
        if not p.exists():
            p = root / "js" / (args.plugin + ".js")
        text = strip_js(p.read_text(encoding="utf-8", errors="replace"), blank_literals=False)
        print("\n".join(l.rstrip() for l in text.split("\n") if l.strip()))
        return
    index = []
    for f in files(root):
        for owner, name, line in scan(f):
            index.append((owner, name, f, line))
    if args.dump_class:
        order = []
        groups = {}
        for owner, name, f, line in index:
            base = owner.split(".")[0]
            if base != args.dump_class:
                continue
            key = owner + "." + name
            if key not in groups:
                order.append(key)
                groups[key] = []
            groups[key].append((f, line))
        cache = {}
        for key in order:
            print(f"/* ===== {key} ===== */")
            for f, line in groups[key]:
                if f not in cache:
                    raw = f.read_text(encoding="utf-8", errors="replace")
                    cache[f] = (raw.split("\n"), strip_js(raw).split("\n"))
                src, stripped = cache[f]
                print(f"// --- {f.relative_to(root / 'js')}:{line}")
                depth = 0
                started = False
                for k in range(line - 1, min(len(src), line + 600)):
                    print(src[k])
                    s2 = stripped[k]
                    depth += s2.count("{") - s2.count("}")
                    if "{" in s2:
                        started = True
                    if started and depth <= 0:
                        break
        return
    if args.list_class:
        names = {}
        for owner, name, f, line in index:
            if owner.split(".")[0] == args.list_class:
                names.setdefault(owner + "." + name, []).append(f"{f.name}:{line}")
        for k, v in names.items():
            print(k, " ".join(v))
        return
    for q in args.query:
        parts = q.split(".")
        owner, name = ".".join(parts[:-1]), parts[-1]
        hits = [(f, line) for o, n, f, line in index if n == name and (o == owner or not owner)]
        print(f"== {q}: {len(hits)} definition(s)")
        for f, line in hits:
            rel = f.relative_to(root / "js")
            print(f"  {rel}:{line}")
            if args.show:
                raw = f.read_text(encoding="utf-8", errors="replace")
                src, stripped = raw.split("\n"), strip_js(raw).split("\n")
                depth = 0
                started = False
                for k in range(line - 1, min(len(src), line + 400)):
                    print("    " + src[k])
                    s = stripped[k]
                    depth += s.count("{") - s.count("}")
                    if "{" in s:
                        started = True
                    if started and depth <= 0:
                        break


if __name__ == "__main__":
    sys.exit(main())
