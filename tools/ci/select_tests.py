#!/usr/bin/env python3
"""Which tests a change can affect: CI runs those and no others.

The answer comes from the build's own description of itself (CMake's file API: each test program's sources and the
object libraries it links), the #include closure of those sources, the files they read by name (string literals such
as "assets/materials" or "tests/fixtures/..."), and the generated sources' inputs. A test is picked when any file it
depends on has changed since the last commit that passed CI (the caller finds that commit; see build.yml). Whenever
the answer can't be sure, everything runs:

- no base commit to compare with, or it can't be read (a new branch, history rewritten);
- the build itself changed (CMakeLists.txt, any .cmake file, the workflow, this script);
- a test whose inputs can't be told from its sources: a Python or CMake script test, a test given the source tree
  (by its arguments or a compile definition), or a generated source this script doesn't know the inputs of.

Documentation never selects anything. The nightly run and every release run everything regardless (build.yml).

Usage:
  select_tests.py --build-dir build [--config Release] [--exclude REGEX] [--shards N --shard K] (--base SHA | --changed FILE...)
Prints the decision; with GITHUB_OUTPUT set, writes mode=all|some|none, regex= (a ctest -R pattern) and targets= (the
build targets those tests need) there, and a summary to GITHUB_STEP_SUMMARY.
"""
import argparse, fnmatch, glob, json, os, re, subprocess, sys

# a change to any of these can change any test: run them all
RUN_ALL = ["CMakeLists.txt", "*.cmake", "**/*.cmake", ".github/**", "tools/ci/**"]
# never read by a test
DOCS = ["docs/**", "*.md", "**/*.md", "LICENSE*", "proposals/**", ".gitignore", ".gitattributes"]
# generated sources: what each is made from (CMakeLists.txt's add_custom_command DEPENDS)
GENERATED = {
    "shaders_gen.h": ["src/shaders/", "tools/embed_shaders.cmake"],
    "terrain_material_actual.inc": ["tests/terrain_material_test.py", "src/shaders/", "src/shaders.h"],
}
EVERYTHING = "*"   # a dependency on every file that isn't documentation
PATH_LITERAL = re.compile(r'"((?:\.\./)*(?:src|assets|tests|tools)/[^"\s]*)"')
INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.M)


def match(path, patterns):
    return any(fnmatch.fnmatchcase(path, p) or (p.endswith("/**") and path.startswith(p[:-2])) for p in patterns)


def git(src, *args):
    return subprocess.run(["git", "-C", src, *args], capture_output=True, text=True)


TOKEN = re.compile(r'\[(=*)\[(.*?)\]\1\]|"((?:[^"\\]|\\.)*)"|([^\s()"]+)', re.S)


def read_ctest_file(build, config):
    """The tests and their commands, from the CTestTestfile.cmake the configure wrote (ctest itself leaves a command
    out until its program is built, and the selection comes before the build). A multi-configuration build (Visual
    Studio) has a block per configuration: the one for `config`."""
    path = os.path.join(build, "CTestTestfile.cmake")
    text = open(path, encoding="utf-8", errors="replace").read()
    tests, order, cond, props = {}, [], None, {}
    for line in text.splitlines():
        s = line.strip()
        m = re.match(r'(?:else)?if\s*\(\s*"?(?:\$\{)?CTEST_CONFIGURATION_TYPE\}?"?\s+MATCHES\s+"(.*)"\s*\)', s)
        if m:
            cond = m.group(1); continue
        if s.startswith("else(") or s.startswith("else ("):
            cond = "<else>"; continue
        if s.startswith("endif"):
            cond = None; continue
        m = re.match(r'set_tests_properties\(\s*(\S+)\s+PROPERTIES\b(.*)', s)
        if m:   # (its measured seconds: how the shards are dealt; its fixtures: what ctest runs before it)
            p = props.setdefault(m.group(1).strip('"[]='), {})
            c = re.search(r'\bCOST\s+"?([0-9.]+)', m.group(2))
            if c: p["cost"] = float(c.group(1))
            for key in ("FIXTURES_SETUP", "FIXTURES_REQUIRED"):
                f = re.search(r'\b' + key + r'\s+"([^"]*)"', m.group(2))
                if f: p[key] = set(x for x in f.group(1).split(";") if x)
            continue
        if not s.startswith("add_test("):
            continue
        args = []
        for g in TOKEN.finditer(s[len("add_test("):s.rfind(")")]):
            args.append(g.group(2) if g.group(2) is not None else g.group(3).replace('\\"', '"') if g.group(3) is not None else g.group(4))
        if not args or cond == "<else>":
            continue
        if cond is not None and not (config and re.match(cond, config)):
            continue
        name = args[0]
        if name not in tests:
            order.append(name)
        tests[name] = {"name": name, "command": args[1:]}
    for n in order:
        p = props.get(n, {})
        tests[n].update(cost=p.get("cost", 1.0), setup=p.get("FIXTURES_SETUP", set()), requires=p.get("FIXTURES_REQUIRED", set()))
    return [tests[n] for n in order]


class Model:
    """The build's targets (CMake file API) and its tests (ctest), read from a configured build directory."""

    def __init__(self, src, build, config):
        self.src, self.build = os.path.realpath(src), os.path.realpath(build)
        reply = os.path.join(self.build, ".cmake", "api", "v1", "reply")
        if not glob.glob(os.path.join(reply, "index-*.json")):   # (ask, and configure again to be answered)
            q = os.path.join(self.build, ".cmake", "api", "v1", "query")
            os.makedirs(q, exist_ok=True)
            open(os.path.join(q, "codemodel-v2"), "w").close()
            subprocess.run(["cmake", self.build], check=True, capture_output=True)
        index = json.load(open(sorted(glob.glob(os.path.join(reply, "index-*.json")))[-1]))
        cm = next(o for o in index["objects"] if o["kind"] == "codemodel")
        code = json.load(open(os.path.join(reply, cm["jsonFile"])))
        confs = code["configurations"]
        conf = next((c for c in confs if c["name"] == config), confs[0])
        self.targets, self.by_id, self.artifact = {}, {}, {}
        for t in conf["targets"]:
            tj = json.load(open(os.path.join(reply, t["jsonFile"])))
            self.targets[t["name"]] = tj
            self.by_id[tj["id"]] = t["name"]
            for a in tj.get("artifacts", []):
                p = a["path"] if os.path.isabs(a["path"]) else os.path.join(self.build, a["path"])
                self.artifact[os.path.normcase(os.path.realpath(p))] = t["name"]
        self.tests = read_ctest_file(self.build, config)
        self._deps = {}

    def rel(self, p):
        """A path in the source tree, relative to it (None if it is elsewhere: the build tree, the system)."""
        p = os.path.realpath(p if os.path.isabs(p) else os.path.join(self.src, p))
        r = os.path.relpath(p, self.src)
        return None if r.startswith("..") or os.path.isabs(r) else r.replace(os.sep, "/")

    def target_of(self, path):
        return self.artifact.get(os.path.normcase(os.path.realpath(path))) if path else None

    def target_deps(self, name, seen=None):
        """What a target is built from: its sources' include closure, the paths they name, generated inputs."""
        if name in self._deps:
            return self._deps[name]
        seen = seen if seen is not None else set()
        seen.add(name)
        tj = self.targets[name]
        deps, why = set(), []
        incdirs = [i["path"] for g in tj.get("compileGroups", []) for i in g.get("includes", [])]
        for g in tj.get("compileGroups", []):   # (a program told where files are reads them: SOLACE_ASSETS, SOLACE_SOURCE_DIR)
            for d in g.get("defines", []):
                v = d.get("define", "").split("=", 1)[-1].strip('"')
                r = self.rel(v) if os.path.isabs(v) else None
                if r == ".":
                    deps.add(EVERYTHING); why.append(name + ": given the source tree")
                elif r:
                    deps.add(r.rstrip("/") + ("/" if os.path.isdir(os.path.join(self.src, r)) else ""))
        todo = []
        for s in tj.get("sources", []):
            r = self.rel(s["path"])
            if r:
                todo.append(r)
            elif s.get("isGenerated"):
                base = os.path.basename(s["path"])
                if base.endswith((".rule", ".o", ".obj")) or base == name or base.startswith(name + "-"):   # (its own stamps)
                    continue
                if base in GENERATED:
                    deps.update(GENERATED[base])
                else:
                    deps.add(EVERYTHING); why.append(name + ": generated " + base)
        done = set()
        while todo:
            f = todo.pop()
            if f in done:
                continue
            done.add(f); deps.add(f)
            try:
                text = open(os.path.join(self.src, f), encoding="utf-8", errors="replace").read()
            except OSError:
                continue
            for inc in INCLUDE.findall(text):
                cands = [os.path.join(os.path.dirname(os.path.join(self.src, f)), inc)] + [os.path.join(d, inc) for d in incdirs]
                hit = next((c for c in cands if os.path.isfile(c)), None)
                r = self.rel(hit) if hit else None
                if r:
                    todo.append(r)
                elif os.path.basename(inc) in GENERATED:
                    deps.update(GENERATED[os.path.basename(inc)])
            # files read by name: the file, a folder (a fixture's or an asset's: its neighbours go with it)
            for lit in PATH_LITERAL.findall(INCLUDE.sub("", text)):
                p = lit.replace("\\", "/")
                while p.startswith("../"):
                    p = p[3:]
                p = p.rstrip("/")
                if os.path.isdir(os.path.join(self.src, p)) or "." not in os.path.basename(p):
                    deps.add(p + "/")
                else:
                    deps.add(p)
                    if p.startswith(("assets/", "tests/fixtures/")):
                        deps.add(os.path.dirname(p) + "/")
        for d in tj.get("dependencies", []):   # the object libraries it links, the generated headers it waits for
            dn = self.by_id.get(d["id"])
            if dn and dn not in seen and self.targets[dn]["type"] in ("OBJECT_LIBRARY", "STATIC_LIBRARY", "UTILITY"):
                sub, subwhy = self.target_deps(dn, seen)
                deps |= sub; why += subwhy
                if dn == "shaders_gen":
                    deps.update(GENERATED["shaders_gen.h"])
        self._deps[name] = (deps, why)
        return deps, why

    def test_deps(self, test):
        """(the files a ctest test depends on, the build targets it needs, why it depends on everything)"""
        cmd = test.get("command") or []
        exe = self.target_of(cmd[0]) if cmd else None
        deps, targets, why = set(), set(), []
        if exe:
            d, w = self.target_deps(exe); deps |= d; why += w; targets.add(exe)
        else:
            deps.add(EVERYTHING); why.append(test["name"] + ": a script")
        for a in cmd[1:]:   # what it is handed: a program (shader_glsl's -DCHECK=<shader_check>), a file, the source tree
            v = a.split("=")[-1]
            t = self.target_of(v)
            if t:
                d, w = self.target_deps(t); deps |= d; why += w; targets.add(t)
            elif os.path.isabs(v):
                r = self.rel(v)
                if r == ".":
                    deps.add(EVERYTHING); why.append(test["name"] + ": given the source tree")
                elif r:
                    deps.add(r + ("/" if os.path.isdir(os.path.join(self.src, r)) else ""))
                elif os.path.realpath(v).startswith(os.path.join(self.build, "gen")) and "shaders_gen" in self.targets:
                    targets.add("shaders_gen")   # (the generated sources it reads: built)
        return deps, targets, why


def affected(deps, changed):
    if EVERYTHING in deps and changed:
        return True
    return any(c == d or (d.endswith("/") and c.startswith(d)) for c in changed for d in deps)


def setups_for(model, names):
    """The fixture setups the tests need that aren't among them: ctest runs those first whether it was asked for them or
    not (tests/test_world.cpp's islands), so they are built, and listed, with them"""
    need = set()
    for t in model.tests:
        if t["name"] in names: need |= t["requires"]
    return [t["name"] for t in model.tests if t["setup"] & need and t["name"] not in names]


def decide(model, changed, exclude=None):
    """('all' | 'some' | 'none', the tests picked, their targets, notes)"""
    tests = [t for t in model.tests if not (exclude and re.search(exclude, t["name"]))]
    every = [t["name"] for t in tests]
    alltargets = set()
    for t in tests:
        alltargets |= model.test_deps(t)[1]
    if changed is None:
        return "all", every, alltargets, ["no base commit to compare with"]
    trig = [c for c in changed if match(c, RUN_ALL)]
    if trig:
        return "all", every, alltargets, ["the build changed: " + ", ".join(trig[:5])]
    code = [c for c in changed if not match(c, DOCS)]
    if not code:
        return "none", [], set(), ["documentation only"]
    picked, targets, notes = [], set(), []
    for t in tests:
        deps, tg, why = model.test_deps(t)
        if affected(deps, code):
            picked.append(t["name"]); targets |= tg
            if EVERYTHING in deps:
                notes.append(t["name"] + " (" + "; ".join(sorted(set(why))[:2]) + ")")
    if len(picked) == len(every):
        return "all", every, alltargets, notes
    for n in setups_for(model, picked):
        picked.append(n); targets |= model.test_deps(next(t for t in model.tests if t["name"] == n))[1]
    return ("some" if picked else "none"), picked, targets, notes


def deal(tests, n):
    """The tests in n shards of about equal cost (their COST, the seconds they take under the sanitizers): the costliest
    first, each to the shard with the least so far"""
    shards = [[] for _ in range(n)]; load = [0.0] * n
    for t in sorted(tests, key=lambda t: (-t["cost"], t["name"])):
        k = load.index(min(load)); shards[k].append(t); load[k] += t["cost"]
    return shards, load


def shard(model, picked, n, k):
    """Shard k of n: the picked tests dealt out by their cost, and the fixture setups its own share needs (every shard
    that needs one runs it, so those are left out of the dealing; one picked for itself alone is dealt like any test).
    (its tests, the programs they need, the shards' costs)"""
    byName = {t["name"]: t for t in model.tests}
    shared = set(setups_for(model, [x for x in picked if not byName[x]["setup"]]))
    shards, load = deal([byName[x] for x in picked if x not in shared], n)
    mine = [t["name"] for t in shards[k - 1]]
    mine += setups_for(model, mine)
    targets = set()
    for x in mine: targets |= model.test_deps(byName[x])[1]
    return mine, targets, load


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build-dir", required=True)
    ap.add_argument("--source-dir", default=os.path.join(os.path.dirname(__file__), "..", ".."))
    ap.add_argument("--config", default="")
    ap.add_argument("--exclude", default="", help="tests this job never runs (a ctest -E pattern)")
    ap.add_argument("--base", default="", help="the last commit that passed: compared with HEAD")
    ap.add_argument("--changed", nargs="*", help="the changed files themselves (instead of --base)")
    ap.add_argument("--shards", type=int, default=1, help="the picked tests dealt out to this many jobs by their cost")
    ap.add_argument("--shard", type=int, default=1, help="this job's shard, 1..--shards")
    a = ap.parse_args()
    model = Model(a.source_dir, a.build_dir, a.config)
    changed = None
    if a.changed is not None:
        changed = a.changed
    elif a.base:
        r = git(model.src, "diff", "--name-only", a.base, "HEAD")
        changed = [l for l in r.stdout.splitlines() if l] if r.returncode == 0 else None
    mode, picked, targets, notes = decide(model, changed, a.exclude or None)
    total = len(picked)
    shardNote = ""
    if a.shards > 1 and picked:   # (this job's share of them: the tests and the programs they need)
        picked, targets, load = shard(model, picked, a.shards, a.shard)
        shardNote = f"Shard {a.shard} of {a.shards}: {len(picked)} tests, {load[a.shard - 1]:.0f} s of their measured cost (the shards: {', '.join('%.0f' % l for l in load)} s)"
        if not picked: mode = "none"
    regex = "^(" + "|".join(re.escape(n) for n in picked) + ")$" if picked else ""
    lines = [f"Tests: {mode} - {total} of {len([t for t in model.tests if not (a.exclude and re.search(a.exclude, t['name']))])}"]
    if shardNote: lines.append(shardNote)
    if changed is not None:
        lines.append(f"Changed since {a.base[:12] or 'the given list'}: {len(changed)} files")
    lines += ["  " + n for n in notes[:12]]
    if mode == "some" or shardNote:
        lines.append("Picked: " + ", ".join(picked))
    print("\n".join(lines))
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as f:
            f.write(f"mode={mode}\nregex={regex}\ntargets={' '.join(sorted(targets))}\n")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as f:
            f.write("### Test selection\n\n```\n" + "\n".join(lines) + "\n```\n")


if __name__ == "__main__":
    main()
