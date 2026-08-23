"""Python tooling graph (smgen/, graphify/) via the `ast` module.

Nodes: modules and top-level functions/methods. Edges: imports
(module -> module, package-relative resolved) and calls (function -> function
by simple name, resolved within the module first, then through the
module's `from .x import y` names). Good enough to show how the host
tooling hangs together and which tests touch which module.
"""

from __future__ import annotations

import ast
from dataclasses import dataclass, field
from pathlib import Path

PACKAGES = ("smgen", "graphify")


@dataclass
class PyFunc:
    module: str
    name: str
    line: int
    calls: set[str] = field(default_factory=set)   # "module.func"
    callers: set[str] = field(default_factory=set)

    @property
    def key(self) -> str:
        return f"py:{self.module}.{self.name}"


@dataclass
class PyModule:
    name: str            # dotted
    file: str
    imports: set[str] = field(default_factory=set)   # dotted module names
    names: dict[str, str] = field(default_factory=dict)  # local name -> module.func
    funcs: list[str] = field(default_factory=list)   # PyFunc keys
    lines: int = 0

    @property
    def key(self) -> str:
        return f"pymod:{self.name}"


@dataclass
class PyGraph:
    modules: dict[str, PyModule] = field(default_factory=dict)
    funcs: dict[str, PyFunc] = field(default_factory=dict)


def _module_name(root: Path, path: Path) -> str:
    rel = path.relative_to(root).with_suffix("")
    parts = list(rel.parts)
    if parts[-1] == "__init__":
        parts = parts[:-1]
    return ".".join(parts)


def build_pygraph(root: Path) -> PyGraph:
    pg = PyGraph()
    files: list[Path] = []
    for pkg in PACKAGES:
        base = root / pkg
        if base.is_dir():
            files.extend(sorted(base.rglob("*.py")))

    trees: dict[str, ast.Module] = {}
    for path in files:
        rel = path.relative_to(root).as_posix()
        if "__pycache__" in rel:
            continue
        mod = _module_name(root, path)
        src = path.read_text(encoding="utf-8", errors="replace")
        try:
            tree = ast.parse(src)
        except SyntaxError:
            continue
        trees[mod] = tree
        pm = PyModule(name=mod, file=rel, lines=src.count("\n") + 1)
        pg.modules[mod] = pm
        is_pkg_init = path.name == "__init__.py"
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for a in node.names:
                    pm.imports.add(a.name)
            elif isinstance(node, ast.ImportFrom):
                if node.level:
                    parts = mod.split(".")
                    if not is_pkg_init:
                        parts = parts[:-1]
                    parts = parts[: len(parts) - node.level + 1]
                    target = ".".join(parts + ([node.module] if node.module else []))
                else:
                    target = node.module or ""
                pm.imports.add(target)
                for a in node.names:
                    pm.names[a.asname or a.name] = f"{target}.{a.name}"
        for node in tree.body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                f = PyFunc(module=mod, name=node.name, line=node.lineno)
                pg.funcs[f.key] = f
                pm.funcs.append(f.key)
            elif isinstance(node, ast.ClassDef):
                for sub in node.body:
                    if isinstance(sub, (ast.FunctionDef, ast.AsyncFunctionDef)):
                        f = PyFunc(module=mod, name=f"{node.name}.{sub.name}",
                                   line=sub.lineno)
                        pg.funcs[f.key] = f
                        pm.funcs.append(f.key)

    # call edges
    for mod, tree in trees.items():
        pm = pg.modules[mod]
        local = {pg.funcs[k].name: k for k in pm.funcs}

        def visit_func(fn_key: str, node: ast.AST) -> None:
            for sub in ast.walk(node):
                if not isinstance(sub, ast.Call):
                    continue
                callee = sub.func
                name = None
                if isinstance(callee, ast.Name):
                    name = callee.id
                elif isinstance(callee, ast.Attribute):
                    name = callee.attr
                if not name:
                    continue
                target = None
                if name in local:
                    target = local[name]
                elif name in pm.names:
                    cand = "py:" + pm.names[name]
                    if cand in pg.funcs:
                        target = cand
                if target and target != fn_key:
                    pg.funcs[fn_key].calls.add(target)
                    pg.funcs[target].callers.add(fn_key)

        for node in tree.body:
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                visit_func(f"py:{mod}.{node.name}", node)
            elif isinstance(node, ast.ClassDef):
                for sub in node.body:
                    if isinstance(sub, (ast.FunctionDef, ast.AsyncFunctionDef)):
                        visit_func(f"py:{mod}.{node.name}.{sub.name}", sub)
    return pg
