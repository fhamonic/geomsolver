"""Shared helpers for check.py and diagnose.py: run geomsolver-cli, read its results file."""

from __future__ import annotations

import difflib
import json
import math
import os
import re
import signal
import subprocess
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]

# Display unit -> factor from SI. A criterion without a unit is shown in SI.
UNIT_SCALE = {"m": 1.0, "cm": 100.0, "mm": 1000.0, "deg": 180.0 / math.pi, "rad": 1.0, "%": 100.0}

ACTIVE_TOL = 1e-6

# Lines the CLI prints for a bad command line or file rather than a bad instance.
USAGE_PREFIXES = ("geomsolver-cli:", "--set ", "--fix", "--bound", "settings:", "unknown algorithm", "cannot open")


class ScriptUsageError(Exception):
    pass


def find_cli(explicit: str | None) -> Path:
    if explicit:
        cli = Path(explicit)
    elif os.environ.get("GEOMSOLVER_CLI"):
        cli = Path(os.environ["GEOMSOLVER_CLI"])
    else:
        cli = REPO / "build" / "geomsolver-cli"
    return cli


def g(x, digits: int = 7) -> str:
    """Number as the CLI prints it (7 significant digits); points as (x, y)."""
    if x is None:
        return "nan"
    if isinstance(x, (list, tuple)):
        return "(" + ", ".join(g(v, digits) for v in x) + ")"
    if isinstance(x, bool):
        return str(x).lower()
    if isinstance(x, (int, float)):
        return f"{x:.{digits}g}"
    return str(x)


def scale_of(unit: str | None) -> float:
    return UNIT_SCALE.get(unit or "", 1.0)


def parse_kv(text: str, flag: str) -> tuple[str, str]:
    if "=" not in text:
        raise ScriptUsageError(f"{flag} expects NAME=VALUE, got '{text}'")
    key, value = text.split("=", 1)
    if not key:
        raise ScriptUsageError(f"{flag} expects NAME=VALUE, got '{text}'")
    return key, value


def dedupe_bounds(bounds: list[str]) -> dict[str, str]:
    """Last --bound per criterion wins. The CLI must never get two --bound for one criterion:
    it then verifies the bound row wrongly (seen: value 90 cm <= 90 cm reported VIOLATED by 10 cm)."""
    out: dict[str, str] = {}
    for b in bounds:
        k, v = parse_kv(b, "--bound")
        out[k] = v
    return out


def override_args(sets: list[str], bounds: dict[str, str], fixes: list[str] | None = None) -> list[str]:
    args: list[str] = []
    for s in sets:
        parse_kv(s, "--set")
        args += ["--set", s]
    for k, v in bounds.items():
        args += ["--bound", f"{k}={v}"]
    for f in fixes or []:
        args += ["--fix", f]
    return args


def shell_quote(a: str) -> str:
    if re.fullmatch(r"[A-Za-z0-9_./=:,+-]+", a):
        return a
    return "'" + a.replace("'", "'\\''") + "'"


@dataclass
class CliRun:
    command: list[str]
    returncode: int | None = None
    stdout: str = ""
    stderr: str = ""
    results: dict | None = None
    errors: list[str] = field(default_factory=list)
    warnings: list[str] = field(default_factory=list)
    timed_out: bool = False
    launch_error: str | None = None
    wall: float = 0.0

    @property
    def usage_error(self) -> bool:
        return bool(self.errors) and all(e.startswith(USAGE_PREFIXES) for e in self.errors)

    def display_command(self) -> str:
        return " ".join(shell_quote(a) for a in self.command)


def split_messages(stderr: str) -> tuple[list[str], list[str]]:
    """CLI diagnostics -> (errors, warnings), verbatim. The usage text after a usage error is dropped."""
    errors, warnings = [], []
    for line in stderr.splitlines():
        if line.startswith("usage:"):
            break
        line = line.rstrip()
        if not line.strip() or re.match(r"\[\w+\] \d+/\d+ runs", line):
            continue
        (warnings if line.startswith("warning:") else errors).append(line)
    return errors, warnings


def run_cli(cli: Path, instance: str, args: list[str], timeout: float) -> CliRun:
    """Run the CLI with --quiet --out <temp>; on timeout, interrupt it (SIGINT) so it still
    writes the runs that finished."""
    with tempfile.TemporaryDirectory(prefix="gs_") as tmp:
        out = os.path.join(tmp, "results.json")
        shown = [rel_path(str(cli)), instance, *args]
        run = CliRun(command=shown)
        cmd = [str(cli), instance, "--quiet", "--out", out, *args]
        t0 = time.monotonic()
        try:
            proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        except OSError as e:
            run.launch_error = f"cannot run {cli}: {e.strerror or e}"
            return run
        try:
            run.stdout, run.stderr = proc.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            run.timed_out = True
            proc.send_signal(signal.SIGINT)
            try:
                run.stdout, run.stderr = proc.communicate(timeout=30)
            except subprocess.TimeoutExpired:
                proc.kill()
                run.stdout, run.stderr = proc.communicate()
        run.wall = time.monotonic() - t0
        run.returncode = proc.returncode
        run.errors, run.warnings = split_messages(run.stderr)
        if os.path.exists(out):
            try:
                with open(out) as fh:
                    run.results = json.load(fh)
            except (OSError, json.JSONDecodeError) as e:
                run.launch_error = f"unreadable results file: {e}"
        return run


def load_instance(path: str) -> dict:
    try:
        with open(path) as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, json.JSONDecodeError):
        return {}


# ---------------------------------------------------------------- results helpers

def all_runs_full(results: dict) -> list[dict]:
    """Full run objects available in a results file: the evaluation, or each solution's best run."""
    if results.get("kind") == "evaluation":
        return [results["evaluation"]]
    return [s["best"] for s in results.get("solutions", [])]


def best_run(results: dict) -> dict | None:
    runs = all_runs_full(results)
    return runs[0] if runs else None


def run_counts(results: dict) -> tuple[int, int]:
    if results.get("kind") == "evaluation":
        return (1 if results["evaluation"].get("feasible") else 0), 1
    runs = results.get("runs", [])
    return sum(1 for r in runs if r.get("feasible")), len(runs)


def objective_name(run: dict | None) -> str | None:
    if not run:
        return None
    for name, c in run.get("criteria", {}).items():
        if c.get("role") in ("minimize", "maximize"):
            return name
    return None


def objective_text(run: dict | None) -> str:
    name = objective_name(run)
    if not name:
        return "-"
    c = run["criteria"][name]
    return f"{g(c.get('display', c.get('value')))} {c.get('unit') or ''}".rstrip()


def objective_display(run: dict | None) -> float | None:
    name = objective_name(run)
    if not name:
        return None
    c = run["criteria"][name]
    return c.get("display", c.get("value"))


def feas_tol(results: dict) -> float:
    return float(results.get("settings", {}).get("feas_tol", 1e-7))


def sweep_label(results_run: dict, row: dict, instance: dict, criterion_expr: str | None = None) -> str:
    if row.get("sweep_value") is None:
        return ""
    sweeps = list((instance.get("sweeps") or {}).keys())
    name = None
    if criterion_expr:
        m = re.match(r"\s*(?:max_over|min_over)\s*\(\s*([A-Za-z_]\w*)", criterion_expr)
        if m:
            name = m.group(1)
    if name is None:
        for c in instance.get("constraints") or []:
            if isinstance(c, dict) and c.get("name") == row.get("name") and c.get("forall"):
                name = c["forall"]
    if name is None and len(sweeps) == 1:
        name = sweeps[0]
    return f"{name or 'sweep'}={g(row['sweep_value'], 6)}"


def criterion_of_row(row: dict, run: dict) -> tuple[str, dict] | None:
    if row.get("kind") != "criterion bound":
        return None
    name = row["name"][: -len(":bound")] if row["name"].endswith(":bound") else row["name"]
    c = run.get("criteria", {}).get(name)
    return (name, c) if c else None


def criterion_expr(instance: dict, name: str) -> str | None:
    for c in instance.get("criteria") or []:
        if isinstance(c, dict) and c.get("name") == name:
            return c.get("expr")
    return None


def describe_row(row: dict, run: dict, instance: dict) -> dict:
    """One verification row in natural units: display unit for criterion bounds, SI otherwise."""
    v = row.get("violation")
    out = {"name": row["name"], "kind": row["kind"], "violation_si": v}
    crit = criterion_of_row(row, run)
    expr = criterion_expr(instance, crit[0]) if crit else None
    out["at"] = sweep_label(run, row, instance, expr)
    if crit:
        name, c = crit
        unit = c.get("unit") or ""
        s = scale_of(unit)
        out.update(criterion=name, unit=unit, role=c.get("role"),
                   value=c.get("display", c.get("value")),
                   bound=None if c.get("bound") is None else c["bound"] * s,
                   violation=None if v is None else v * s, margin=None if v is None else -v * s)
    else:
        unit = {"assembly": "m", "membership": "m"}.get(row["kind"], "SI")
        out.update(unit=unit, violation=v, margin=None if v is None else -v)
    return out


def row_cells(d: dict, violated: bool) -> list[str]:
    """[name, kind, detail] for a described row."""
    at = f" at {d['at']}" if d.get("at") else ""
    u = f" {d['unit']}" if d.get("unit") and d["unit"] != "SI" else ""
    kind = d["kind"]
    if kind == "criterion bound":
        rel = "<=" if d.get("role") == "max" else ">="
        word = "over by" if d.get("role") == "max" else "short by"
        if violated:
            detail = (f"value {g(d['value'])}{u}, needs {rel} {g(d['bound'])}{u}, "
                      f"{word} {g(d['violation'])}{u}{at}")
        else:
            detail = f"value {g(d['value'])}{u} {rel} {g(d['bound'])}{u}, margin {g(d['margin'], 3)}{u}{at}"
    elif kind == "constraint":
        detail = (f"violated by {g(d['violation'])} (SI: m, rad or ratio){at}" if violated
                  else f"margin {g(d['margin'], 3)} (SI){at}")
    elif kind == "assembly":
        detail = (f"gap {g(d['violation'])} m{at}: the linkage cannot close there" if violated
                  else f"margin {g(d['margin'], 3)} m{at}")
    elif kind == "membership":
        detail = (f"{g(d['violation'])} m outside its domain" if violated
                  else "on the edge of its domain (a larger domain may improve the objective)")
    else:
        detail = f"violation {g(d['violation'])}{at}"
    return [d["name"], kind, detail]


def table(rows: list[list[str]], indent: str = "  ") -> list[str]:
    if not rows:
        return []
    widths = [max(len(r[i]) for r in rows) for i in range(len(rows[0]))]
    return [indent + "  ".join(c.ljust(w) for c, w in zip(r, widths)).rstrip() for r in rows]


def rel_path(p: str) -> str:
    try:
        r = os.path.relpath(p)
    except ValueError:
        return str(p)
    return r if len(r) < len(str(p)) else str(p)


def violated_rows(run: dict, tol: float) -> list[dict]:
    return [r for r in run.get("verification", []) if r.get("violation") is not None and r["violation"] > tol]


def active_rows(run: dict, tol: float) -> list[dict]:
    return [r for r in run.get("verification", [])
            if r.get("violation") is not None and -ACTIVE_TOL <= r["violation"] <= tol]


IDENT = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")


# ---------------------------------------------------------------- lint: mistakes the CLI accepts silently

# A JSON number in min/max/bound is SI whatever the "unit" key says. Above these SI magnitudes a
# number was almost surely written in the display unit (90 meant 90 deg is 90 rad = 14 turns).
LINT_LIMITS = {"deg": 2 * math.pi + 1e-9, "cm": 20.0, "mm": 20.0, "%": 1.0}
RELATION = re.compile(r"<=|>=|==|<|>")


def _is_num(x) -> bool:
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def _plain(x: float) -> str:
    return f"{x:.10g}"


def lint_units(instance: dict) -> list[str]:
    out = []
    for name, v in (instance.get("design") or {}).items():
        if not isinstance(v, dict) or v.get("type") != "scalar":
            continue
        unit = v.get("unit") or ""
        lim = LINT_LIMITS.get(unit)
        for key in ("min", "max"):
            x = v.get(key)
            if lim and _is_num(x) and abs(x) > lim:
                out.append(f"design.{name}.{key} is the number {g(x)}: a JSON number is SI, so this is "
                           f"{g(x * scale_of(unit))} {unit}. If you meant {g(x)} {unit}, write \"{_plain(x)}{unit}\".")
    for i, c in enumerate(instance.get("criteria") or []):
        if not isinstance(c, dict) or c.get("role") not in ("max", "min"):
            continue
        unit, b = c.get("unit") or "", c.get("bound")
        if _is_num(b) and unit in LINT_LIMITS:
            out.append(f"criteria[{i}].bound ({c.get('name')}) is the number {g(b)}: a JSON number is SI, so the "
                       f"bound is {g(b * scale_of(unit))} {unit}. If you meant {g(b)} {unit}, write "
                       f"\"{_plain(b)}{unit}\" (a string with the unit) or a param name.")
    for name, s in (instance.get("sweeps") or {}).items():
        if not isinstance(s, dict):
            continue
        lo, hi = s.get("min"), s.get("max")
        if _is_num(lo) and _is_num(hi) and hi - lo > 2 * math.pi + 1e-9:
            out.append(f"sweeps.{name} runs from {g(lo)} to {g(hi)}: JSON numbers are SI, so if it is an angle "
                       f"that is {g((hi - lo) / (2 * math.pi), 3)} turns. If you meant degrees, write "
                       f"\"{_plain(lo)}deg\" and \"{_plain(hi)}deg\".")
    return out


TOP_KEYS = ("format", "description", "include", "params", "geometry", "design", "sweeps", "let", "constraints",
            "criteria", "display", "solver")
SECTION_KEYS = {  # keys the CLI accepts; any other key in these sections is dropped WITHOUT a warning
    "design": {"type", "min", "max", "domain", "value", "fixed", "unit", "note"},
    "sweeps": {"min", "max", "note"},
    "geometry": {"type", "x", "y", "width", "height", "angle", "r", "points", "closed", "note"},
}


def lint_keys(instance: dict) -> list[str]:
    out = []
    for key in instance:
        if key not in TOP_KEYS:
            close = difflib.get_close_matches(key, TOP_KEYS, n=1, cutoff=0.75)
            if close:
                out.append(f"top-level key '{key}' is ignored without a warning: did you mean '{close[0]}'? "
                           f"Everything under it is lost.")
    for section, allowed in SECTION_KEYS.items():
        for name, entry in (instance.get(section) or {}).items():
            if not isinstance(entry, dict):
                continue
            for key in entry:
                if key not in allowed:
                    close = difflib.get_close_matches(key, sorted(allowed), n=1, cutoff=0.6)
                    out.append(f"{section}.{name}.{key} is ignored without a warning"
                               + (f": did you mean '{close[0]}'?" if close else f" (allowed: {', '.join(sorted(allowed))})"))
    return out


def _call_args(text: str, open_paren: int) -> list[str]:
    """Top-level arguments of the call whose '(' is at open_paren."""
    depth, start, args = 0, open_paren + 1, []
    for i in range(open_paren, len(text)):
        ch = text[i]
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append(text[start:i].strip())
                return args
        elif ch == "," and depth == 1:
            args.append(text[start:i].strip())
            start = i + 1
    return []


def containment_suspects(instance: dict) -> list[tuple[str, str, str]]:
    """(where, first argument, second argument) of every clearance(X, R) bounded by a negative number from
    above: the form that keeps a POINT inside R, and that silently fails for a polygon or circle X."""
    found = []

    def scan(where: str, side: str) -> None:
        for m in re.finditer(r"\bclearance\s*\(", side):
            args = _call_args(side, m.end() - 1)
            if len(args) == 2:
                found.append((where, args[0], args[1]))

    for i, c in enumerate(instance.get("constraints") or []):
        if not isinstance(c, dict) or c.get("enabled") is False or not isinstance(c.get("expr"), str):
            continue
        e = c["expr"]
        m = RELATION.search(e)
        if not m:
            continue
        lhs, op, rhs = e[:m.start()], m.group(0), e[m.end():]
        if op in ("<=", "<") and rhs.strip().startswith("-"):
            scan(f"constraints[{i}] ({c.get('name')})", lhs)
        elif op in (">=", ">") and lhs.strip().startswith("-"):
            scan(f"constraints[{i}] ({c.get('name')})", rhs)
    params = instance.get("params") or {}
    for i, c in enumerate(instance.get("criteria") or []):
        if not isinstance(c, dict) or c.get("role") != "max" or not isinstance(c.get("expr"), str):
            continue
        b = c.get("bound")
        b = params.get(b, b) if isinstance(b, str) else b
        if (_is_num(b) and b < 0) or (isinstance(b, str) and b.strip().startswith("-")):
            scan(f"criteria[{i}] ({c.get('name')})", c["expr"])
    return found


def lint_containment(cli: Path, instance_path: str, instance: dict, overrides: list[str],
                     timeout: float) -> list[str]:
    """Probe the first argument of each suspect: a shape (not a 2-number point) there is the containment trap."""
    geometry = instance.get("geometry") or {}
    out = []
    for where, x, r in containment_suspects(instance)[:12]:
        for name in (x, r):
            gdef = geometry.get(name)
            if isinstance(gdef, dict) and gdef.get("type") == "polyline" and not gdef.get("closed"):
                out.append(f"{where}: clearance({x}, {r}) <= -m can never hold: '{name}' is an OPEN polyline, which "
                           f"has no inside (clearance to it is never negative). Close it (\"closed\": true) or use "
                           f"min_x/max_x/min_y/max_y/max_proj rows for the walls.")
                break
        else:
            run = run_cli(cli, instance_path, ["--eval", *overrides, "--probe", x], timeout)
            val = ((run.results or {}).get("probes") or {}).get(x)
            if isinstance(val, list) and len(val) != 2:
                if len(val) == 3:
                    kind, fix = "circle", (f"use its CENTRE: clearance(<centre of {x}>, {r}) <= -(<radius> + m) "
                                           f"(references/patterns.md row 19)")
                else:
                    kind, fix = "polygon/polyline", (f"use one row per vertex, clearance(vertex({x}, i), {r}) <= -m "
                                                     f"for i = 0, 1, ... (convex {r}; patterns.md row 18)")
                out.append(f"{where}: clearance({x}, {r}) <= -m with the {kind} '{x}' does NOT keep it inside {r}: "
                           f"for overlapping shapes clearance is minus the penetration depth, so a shape crossing "
                           f"the boundary passes. To keep it inside, {fix}, or write per-wall "
                           f"min_x/max_x/min_y/max_y/max_proj rows (rows 20-21). Ignore this only if you meant "
                           f"\"overlaps {r} by at least m\".")
    return out


def lint(cli: Path, instance_path: str, overrides: list[str], timeout: float) -> list[str]:
    """Likely modelling mistakes that compile without any warning. Reads the file as written
    (edits made with --set are not seen)."""
    instance = load_instance(instance_path)
    if not instance:
        return []
    try:
        return lint_keys(instance) + lint_units(instance) + \
            lint_containment(cli, instance_path, instance, overrides, timeout)
    except Exception as e:  # a lint must never break the report
        return [f"(lint skipped: {e})"]


def referenced_params(expr: str, instance: dict) -> tuple[list[str], list[str]]:
    """Params an expression names directly, and params it reaches through lets or other params."""
    params = instance.get("params") or {}
    lets = instance.get("let") or {}
    direct = [n for n in dict.fromkeys(IDENT.findall(expr or "")) if n in params]
    seen, via = set(), []
    stack = [n for n in IDENT.findall(expr or "") if n in lets or n in params]
    while stack:
        n = stack.pop()
        if n in seen:
            continue
        seen.add(n)
        body = lets.get(n) if n in lets else params.get(n)
        if n in params and n not in direct and n not in via:
            via.append(n)
        if isinstance(body, str):
            stack += [m for m in IDENT.findall(body) if (m in lets or m in params) and m not in seen]
    return direct, via
