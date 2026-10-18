import argparse
import collections
import json
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

from omori import mvcodes


def load(root, rel):
    return json.loads((root / rel).read_text(encoding="utf-8-sig"))


def iter_lists(root):
    for p in sorted((root / "maps").glob("*.json")):
        m = load(root, p.relative_to(root))
        for ev in m.get("events") or []:
            if not ev:
                continue
            for pi, page in enumerate(ev["pages"]):
                yield f"{p.stem}/ev{ev['id']}/p{pi}", page["list"], page
    for ce in load(root, "data/CommonEvents.json"):
        if ce:
            yield f"ce{ce['id']}", ce["list"], None
    for tr in load(root, "data/Troops.json"):
        if tr:
            for pi, page in enumerate(tr["pages"]):
                yield f"troop{tr['id']}/p{pi}", page["list"], None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--out", default="_work/audit")
    args = ap.parse_args()
    root = pathlib.Path(args.root)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cmd_count = collections.Counter()
    route_count = collections.Counter()
    plugin_cmds = collections.Counter()
    plugin_samples = collections.defaultdict(list)
    scripts = collections.Counter()
    branch_scripts = collections.Counter()
    var_scripts = collections.Counter()
    route_scripts = collections.Counter()
    comments = collections.Counter()
    text_codes = collections.Counter()
    branch_types = collections.Counter()
    triggers = collections.Counter()
    lists = 0

    def add_route(route):
        for step in route.get("list", []):
            route_count[step["code"]] += 1
            p = step.get("parameters") or []
            if step["code"] == 45 and p:
                route_scripts[p[0]] += 1

    for where, lst, page in iter_lists(root):
        lists += 1
        if page is not None:
            triggers[page.get("trigger")] += 1
            if page.get("moveType") == 3:
                add_route(page.get("moveRoute") or {})
        i = 0
        while i < len(lst):
            cmd = lst[i]
            code = cmd["code"]
            p = cmd.get("parameters") or []
            cmd_count[code] += 1
            if code == 355:
                body = [p[0]]
                j = i + 1
                while j < len(lst) and lst[j]["code"] == 655:
                    body.append(lst[j]["parameters"][0])
                    j += 1
                scripts["\n".join(body)] += 1
            elif code == 356:
                name = p[0].split(" ")[0] if p else ""
                plugin_cmds[name] += 1
                if len(plugin_samples[name]) < 8 and p[0] not in plugin_samples[name]:
                    plugin_samples[name].append(p[0])
            elif code == 111:
                branch_types[p[0]] += 1
                if p[0] == 12:
                    branch_scripts[p[1]] += 1
            elif code == 122 and p[3] == 4:
                var_scripts[p[4]] += 1
            elif code in (108, 408):
                for t in re.findall(r"<([^:>\s]+)", p[0]):
                    comments[t] += 1
            elif code == 401:
                for m in re.findall(r"\\([A-Za-z]+)", p[0]):
                    text_codes[m] += 1
            elif code == 205:
                add_route(p[1])
            i += 1

    def calls(counter):
        c = collections.Counter()
        for src, n in counter.items():
            for m in re.findall(r"([A-Za-z_$][\w$]*(?:\.[A-Za-z_$][\w$]*)*)\s*\(", src):
                c[m] += n
            for m in re.findall(r"(\$game[A-Za-z]+(?:\.[A-Za-z_]\w*)?)", src):
                c["@" + m] += n
        return c

    all_scripts = collections.Counter()
    for counter in (scripts, branch_scripts, var_scripts, route_scripts):
        all_scripts.update(counter)
    report = {
        "lists": lists,
        "commands": [{"code": c, "name": mvcodes.COMMANDS.get(c, "?"), "count": n} for c, n in cmd_count.most_common()],
        "route": [{"code": c, "name": mvcodes.ROUTE.get(c, "?"), "count": n} for c, n in route_count.most_common()],
        "plugin_commands": [{"name": k, "count": n, "samples": plugin_samples[k]} for k, n in plugin_cmds.most_common()],
        "branch_types": dict(branch_types.most_common()),
        "triggers": dict(triggers),
        "comment_tags": dict(comments.most_common(80)),
        "text_codes": dict(text_codes.most_common()),
        "scripts": {
            "event": len(scripts), "event_uses": sum(scripts.values()), "branch": len(branch_scripts),
            "variable": len(var_scripts), "route": len(route_scripts),
            "identifiers": dict(calls(all_scripts).most_common(150)),
        },
    }
    (out / "scripts.json").write_text(json.dumps({
        "event": scripts.most_common(), "branch": branch_scripts.most_common(),
        "variable": var_scripts.most_common(), "route": route_scripts.most_common(),
    }, indent=1, ensure_ascii=False), encoding="utf-8")
    (out / "report.json").write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")


if __name__ == "__main__":
    main()
