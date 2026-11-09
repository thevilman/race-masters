"""Every instrument file the generator can load, found by reading its source (no imports, so
it needs no packages and no libraries): the .sfz, .fxp and .sf2 files its code names.

A path the generator plays is always written as one of
  * a library folder and literal parts: config.lib("vsco2-ce") / "Harp.sfz", or a module
    constant built so (VS = config.lib("vsco2-ce"); VS / "Harp.sfz"; I.DRUMS / "02-basic.sfz");
  * a call of a one-line helper that adds a literal to such a folder (render.sso("..."));
  * a Surge XT patch: a "patch": "<category>/<name>.fxp" entry, or a call of a helper that
    makes one (personality.S("...")), read from instruments.SURGE_PATCHES.
instrument_files() follows exactly these forms. Any other string in the code that names such
a file is reported as not understood, so the test (tests/test_tools.py) fails until this
module learns it: a new instrument can't be missing from the download list
(tgmusic/data/samples.tsv) without the test saying so.
"""
import ast
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
INSTRUMENT = re.compile(r"[^\n]+\.(sfz|fxp|sf2)", re.I)
PATCHES = ("instruments", "SURGE_PATCHES")  # where SurgePatch reads a "patch" from
DOWNLOADS = {"libraries", "needs"}  # name what is downloaded or loaded, not what is played: not checked


def _join(base, part):
    lib, path = base
    return lib, f"{path}/{part}" if path else part


class _Scan:
    def __init__(self, folder):
        self.trees = {p.stem: ast.parse(p.read_text(encoding="utf-8"), str(p)) for p in sorted(Path(folder).glob("*.py"))}
        self.alias = {m: {} for m in self.trees}     # module -> local name -> module
        self.imported = {m: {} for m in self.trees}  # module -> local name -> (module, name)
        self.consts = {}                             # (module, name) -> (library, path)
        self.helpers = {}                            # (module, name) -> ("path", base) | ("patch",)
        self.used = set()                            # ids of the string nodes understood
        for mod, tree in self.trees.items():
            for node in tree.body:
                if isinstance(node, ast.ImportFrom) and node.level == 1:
                    for a in node.names:
                        if node.module is None:
                            self.alias[mod][a.asname or a.name] = a.name
                        else:
                            self.imported[mod][a.asname or a.name] = (node.module, a.name)
        # Module constants, until nothing new is learnt (they may use each other's).
        changed = True
        while changed:
            changed = False
            for mod, tree in self.trees.items():
                for node in tree.body:
                    if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
                        key = (mod, node.targets[0].id)
                        if key not in self.consts:
                            v = self.path(mod, node.value)
                            if v is not None:
                                self.consts[key] = v
                                changed = True
        for mod, tree in self.trees.items():
            for node in tree.body:
                if isinstance(node, ast.FunctionDef) and len(node.args.args) >= 1:
                    h = self._helper(mod, node)
                    if h:
                        self.helpers[(mod, node.name)] = h

    def _name(self, mod, node):
        """The (module, name) a Name or module.attribute refers to."""
        if isinstance(node, ast.Name):
            return self.imported[mod].get(node.id, (mod, node.id))
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name) and node.value.id in self.alias[mod]:
            return self.alias[mod][node.value.id], node.attr
        return None

    def path(self, mod, node):
        """(library, path) for an expression naming a library file or folder, else None."""
        if isinstance(node, ast.Call) and len(node.args) == 1 and not node.keywords:
            f, arg = node.func, node.args[0]
            if self._name(mod, f) in (("config", "lib"),) and isinstance(arg, ast.Constant) and isinstance(arg.value, str):
                self.used.add(id(arg))
                return arg.value, ""
            if isinstance(f, ast.Name) and f.id == "str":
                return self.path(mod, arg)
            return None
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Div):
            base = self.path(mod, node.left)
            if base is not None and isinstance(node.right, ast.Constant) and isinstance(node.right.value, str):
                self.used.add(id(node.right))
                return _join(base, node.right.value)
            return None
        key = self._name(mod, node)
        return self.consts.get(key) if key else None

    def _helper(self, mod, fn):
        """def f(x): return <folder> / x  -> ("path", folder); def f(x): return {..."patch": x}
        -> ("patch",)."""
        if len(fn.body) != 1 or not isinstance(fn.body[0], ast.Return):
            return None
        arg, ret = fn.args.args[0].arg, fn.body[0].value
        if (isinstance(ret, ast.BinOp) and isinstance(ret.op, ast.Div) and isinstance(ret.right, ast.Name)
                and ret.right.id == arg):
            base = self.path(mod, ret.left)
            return ("path", base) if base is not None else None
        if isinstance(ret, ast.Dict):
            for k, v in zip(ret.keys, ret.values):
                if isinstance(k, ast.Constant) and k.value == "patch" and isinstance(v, ast.Name) and v.id == arg:
                    return ("patch",)
        return None

    def files(self):
        """{library: set of paths}, [(where, text) of instrument names not understood]."""
        found, unknown = {}, []
        patches = self.consts[PATCHES]

        def add(v):
            if v and INSTRUMENT.fullmatch(v[1]):
                found.setdefault(v[0], set()).add(v[1])

        for mod, tree in self.trees.items():
            for node in ast.walk(tree):
                if isinstance(node, ast.BinOp):
                    add(self.path(mod, node))
                elif isinstance(node, ast.Call) and len(node.args) == 1 and isinstance(node.args[0], ast.Constant):
                    key = self._name(mod, node.func)
                    h = self.helpers.get(key) if key else None
                    s = node.args[0].value
                    if h and isinstance(s, str):
                        self.used.add(id(node.args[0]))
                        add(_join(h[1], s) if h[0] == "path" else _join(patches, s))
                elif isinstance(node, ast.Dict):
                    for k, v in zip(node.keys, node.values):
                        if (isinstance(k, ast.Constant) and k.value == "patch" and isinstance(v, ast.Constant)
                                and isinstance(v.value, str)):
                            self.used.add(id(v))
                            add(_join(patches, v.value))
        for mod, tree in self.trees.items():
            if mod in DOWNLOADS:
                continue
            for node in ast.walk(tree):
                if (isinstance(node, ast.Constant) and isinstance(node.value, str) and id(node) not in self.used
                        and INSTRUMENT.fullmatch(node.value)):
                    unknown.append((f"tgmusic/{mod}.py:{node.lineno}", node.value))
        return found, unknown


def instrument_files(folder=HERE):
    """({library: sorted paths relative to its folder}, [(file:line, text) not understood])."""
    found, unknown = _Scan(folder).files()
    return {lib: sorted(paths) for lib, paths in sorted(found.items())}, unknown


def not_downloaded(entries):
    """(library, path) of the instrument files in `entries` that no download brings: neither
    in data/samples.tsv (a sample library's list) nor a whole file a library downloads."""
    from . import libraries as L
    out = []
    for lib, paths in sorted(entries.items()):
        spec = L.LIBRARIES.get(lib)
        if spec is None:
            out += [(lib, p) for p in paths]
            continue
        have = set()
        parts = list(spec["files"]) + [f for fs in spec.get("platform_files", {}).values() for f in fs]
        for f in parts:
            if L.is_listed(f):
                try:
                    have |= {L.disk_path(f, p) for p, _, _ in L.listed(lib, f)}
                except (OSError, RuntimeError):
                    pass
            elif "strip" not in f:
                have.add(f.get("name") or f["url"].rsplit("/", 1)[-1])
        out += [(lib, p) for p in paths if p not in have]
    return out
