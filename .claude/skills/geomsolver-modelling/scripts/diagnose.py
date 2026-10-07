#!/usr/bin/env python3
"""Diagnose why a geomsolver instance has no feasible design, and find single relaxations that fix it.

Steps:
  1. compile check (--eval); on errors print them and stop (exit 2)
  2. multistart with N starts (default max(256, solver.starts))
  3. feasible: report k of n runs, FRAGILE when under 5 %, and the binding rows (exit 0)
  4. infeasible: violated rows at the 5 best runs, the over-constraint signature, then relaxation
     probes (each a multistart): loosen each violated criterion bound by k times its violation
     (k = 10 first, then the smallest of 2, 5 that still works); loosen the param of each violated
     constraint written "... <= P", "... >= P" or "... <= -P" the same way (k times its SI violation),
     and disable each violated constraint. Prints a RELAXATIONS table and a RECOMMEND line.
     Exit 0 when some single relaxation is feasible, 1 otherwise.

The instance file is never modified: every change is a CLI flag (--bound, --set).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gs_common as gc  # noqa: E402

LADDER_FIRST = 10
LADDER_REFINE = (2, 5)
LADDER_ESCALATE = 30
FRAGILE = 0.05
SIGNATURE_FACTOR = 5.0
TOP_RUNS = 5

EXAMPLES = """examples:
  diagnose.py inst.json
  diagnose.py inst.json --starts 1024 --budget 120
  diagnose.py inst.json --set params.edge_margin=0.16      diagnose a what-if without editing the file

A probe is a full multistart with --probe-starts starts (default: the same N as step 2,
because a probe with fewer starts than the main run can miss a feasible relaxation)."""


class Parser(argparse.ArgumentParser):
    def error(self, message: str):
        self.print_usage(sys.stderr)
        print(f"VERDICT: ERROR\nERRORS\n  {self.prog}: {message}\nNEXT: fix the command line (diagnose.py --help)")
        sys.exit(2)


def build_parser() -> argparse.ArgumentParser:
    p = Parser(prog="diagnose.py",
               description="Diagnose feasibility of a geomsolver instance and probe single relaxations. "
                           "Exit 0 feasible (or some single relaxation is), 1 no relaxation found, "
                           "2 compile/usage error, 3 other error.",
               epilog=EXAMPLES, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("instance", help="instance JSON file (read only)")
    p.add_argument("--starts", type=int, help="starts of the main multistart (default max(256, solver.starts))")
    p.add_argument("--probe-starts", type=int, metavar="M", help="starts of each relaxation probe (default: N)")
    p.add_argument("--max-probes", type=int, default=24, metavar="K", help="at most K relaxation probes (default 24)")
    p.add_argument("--budget", type=float, default=45.0, metavar="SEC",
                   help="stop starting new probes after SEC seconds of probing (default 45)")
    p.add_argument("--timeout", type=float, default=300.0, metavar="SEC",
                   help="interrupt any single CLI call after SEC seconds (default 300)")
    p.add_argument("--seed", type=int, help="multistart seed (default: solver.seed)")
    p.add_argument("--set", action="append", default=[], metavar="PATH=VALUE",
                   help="edit the instance for every run of this diagnosis (repeatable)")
    p.add_argument("--bound", action="append", default=[], metavar="NAME=VALUE",
                   help="replace a criterion bound, display unit, for every run (repeatable)")
    p.add_argument("--fix", action="append", default=[], metavar="NAME[,NAME...]",
                   help="fix design variables at their written values (repeatable)")
    p.add_argument("--json", metavar="OUT", help="also write the diagnosis as JSON to OUT")
    p.add_argument("--cli", metavar="PATH",
                   help="geomsolver-cli to use (default: $GEOMSOLVER_CLI, else <repo>/build/geomsolver-cli)")
    return p


def plain(x: float) -> str:
    """A number without exponent when it is of everyday size (270, not 2.7e+02)."""
    if x == 0 or 1e-4 <= abs(x) < 1e9:
        text = f"{x:.12f}".rstrip("0").rstrip(".")
        return text if text not in ("", "-0") else "0"
    return repr(x)


def nice_bound(exact: float, bound: float, step: float, direction: int) -> str:
    """Shortest decimal for a loosened bound: within 5 % of the step and never tighter than the old bound."""
    for p in range(2, 13):
        r = float(f"{exact:.{p}g}")
        if (r - bound) * direction > 0 and abs(r - exact) <= 0.05 * abs(step):
            return plain(r)
    return plain(exact)


LARGE_VIOLATION = 0.05  # SI: 5 cm or 2.9 deg is far from "almost feasible" at furniture scale

# "expr <= P", "expr >= P", "expr <= -P" (also "-(P)"): a constraint whose limit is one param P
CONSTRAINT_PARAM = re.compile(r"^(?P<lhs>[^<>]*?)(?P<op><=|>=|<|>)\s*(?P<neg>-)?\s*(?P<open>\()?\s*"
                              r"(?P<p>[A-Za-z_]\w*)\s*(?(open)\))\s*$")


def constraint_param(expr: str, params: dict) -> tuple[str, int] | None:
    """(param, sign of the change that LOOSENS the row) for a constraint whose limit is a lone param."""
    m = CONSTRAINT_PARAM.match(expr or "")
    if not m or m.group("p") not in params:
        return None
    upper = m.group("op") in ("<=", "<")
    neg = bool(m.group("neg"))
    return m.group("p"), (1 if upper != neg else -1)


def magnitude_cluster(values: list[float]) -> tuple[int, float, float]:
    """Largest number of values inside a window [x, SIGNATURE_FACTOR * x]."""
    vs = sorted(v for v in values if v > 0)
    best = (0, 0.0, 0.0)
    j = 0
    for i, lo in enumerate(vs):
        j = max(j, i)
        while j + 1 < len(vs) and vs[j + 1] <= SIGNATURE_FACTOR * lo:
            j += 1
        if j - i + 1 > best[0]:
            best = (j - i + 1, lo, vs[j])
    return best


class Diagnosis:
    def __init__(self, args):
        self.args = args
        self.cli = gc.find_cli(args.cli)
        self.instance = gc.load_instance(args.instance)
        self.base_sets = list(args.set)
        self.base_bounds = gc.dedupe_bounds(args.bound)
        self.out: list[str] = []
        self.report: dict = {"instance": args.instance}
        self.t0 = time.monotonic()
        self.times: dict[str, float] = {}
        self.probed: list[dict] = []
        self.lint: list[str] = []

    # ------------------------------------------------------------ helpers
    def cli_args(self, starts: int, extra_sets=(), extra_bounds=None, mode=None) -> list[str]:
        bounds = dict(self.base_bounds)
        bounds.update(extra_bounds or {})
        a = [mode] if mode else ["--starts", str(starts)]
        if self.args.seed is not None and not mode:
            a += ["--seed", str(self.args.seed)]
        return a + gc.override_args(self.base_sets + list(extra_sets), bounds, self.args.fix)

    def emit(self, verdict: str, code: int, next_line: str, head: list[str] | None = None) -> int:
        self.times["total"] = time.monotonic() - self.t0
        times = ", ".join(f"{k} {v:.1f} s" for k, v in self.times.items())
        lines = [f"VERDICT: {verdict}"] + (head or []) + self.out + [f"TIME: {times}", f"NEXT: {next_line}"]
        print("\n".join(lines))
        self.report.update(verdict=verdict, exit=code, next=next_line, times=self.times)
        if self.args.json:
            try:
                with open(self.args.json, "w") as fh:
                    json.dump(self.report, fh, indent=1, default=str)
            except OSError as e:
                print(f"diagnose.py: cannot write {self.args.json}: {e}", file=sys.stderr)
        return code

    def constraint_index(self, name: str) -> int | None:
        for i, c in enumerate(self.instance.get("constraints") or []):
            if isinstance(c, dict) and c.get("name") == name:
                return i
        return None

    def criterion_entry(self, name: str) -> tuple[int, dict] | tuple[None, None]:
        for i, c in enumerate(self.instance.get("criteria") or []):
            if isinstance(c, dict) and c.get("name") == name:
                return i, c
        return None, None

    # ------------------------------------------------------------ steps
    def run(self) -> int:
        a = self.args
        try:
            self.cli_args(1)
        except gc.ScriptUsageError as e:
            self.out += ["ERRORS", f"  {e}"]
            return self.emit("ERROR", 2, "fix the command line of diagnose.py")

        # 1. compile
        t = time.monotonic()
        ev = gc.run_cli(self.cli, a.instance, self.cli_args(0, mode="--eval"), a.timeout)
        self.times["compile"] = time.monotonic() - t
        self.report["warnings"] = ev.warnings
        if ev.launch_error and ev.results is None:
            self.out += ["ERRORS", f"  {ev.launch_error}"]
            return self.emit("ERROR", 3, "check the CLI path: pass --cli PATH or set GEOMSOLVER_CLI")
        warn = ["WARNINGS"] + ([f"  {w}" for w in ev.warnings] or ["  (none)"])
        if ev.returncode == 2 or ev.results is None:
            self.report["errors"] = ev.errors
            self.out += ["STEP 1 compile: FAILED", "ERRORS"] + ([f"  {e}" for e in ev.errors] or
                                                               [f"  geomsolver-cli exited with {ev.returncode}"]) + warn
            if ev.usage_error:
                return self.emit("ERROR", 2, "fix the command line (the message names the bad option, path or name)")
            return self.emit("COMPILE_ERROR", 2, "fix the errors above (path = JSON location, column = character "
                                                 "in the expression), then run check.py on the instance again.")
        self.out += [f"STEP 1 compile: ok, {len(ev.warnings)} warning(s)"] + warn
        self.lint = gc.lint(self.cli, a.instance, self.cli_args(0, mode="--eval")[1:], min(a.timeout, 60.0))
        self.report["lint"] = self.lint
        self.out += ["LINT (likely modelling mistakes the CLI does not report; fix these FIRST)"] + \
            ([f"  {x}" for x in self.lint] or ["  (none)"])

        settings = ev.results.get("settings", {})
        n = a.starts if a.starts is not None else max(256, int(settings.get("starts", 64)))
        m = a.probe_starts if a.probe_starts is not None else n
        self.report.update(starts=n, probe_starts=m)

        # 2. multistart
        t = time.monotonic()
        ms = gc.run_cli(self.cli, a.instance, self.cli_args(n), a.timeout)
        self.times["multistart"] = time.monotonic() - t
        if ms.results is None:
            self.out += ["ERRORS"] + [f"  {e}" for e in ms.errors] + [f"  multistart failed (exit {ms.returncode}"
                                                                     + (", timed out" if ms.timed_out else "") + ")"]
            return self.emit("ERROR", 3, "raise --timeout or lower --starts")
        res = ms.results
        tol = gc.feas_tol(res)
        nfeas, nruns = gc.run_counts(res)
        stopped = bool(res.get("stopped")) or ms.timed_out
        self.out.append(f"STEP 2 multistart: {nfeas} feasible of {nruns} runs ({100.0 * nfeas / max(nruns, 1):.1f} %)"
                        f", {len(res.get('solutions', []))} distinct solutions, {ms.wall:.2f} s"
                        + ("  [STOPPED by --timeout]" if stopped else ""))
        self.out.append(f"COMMAND: {ms.display_command()}")
        self.report["multistart"] = {"feasible": nfeas, "runs": nruns, "stopped": stopped,
                                     "command": ms.display_command()}
        best = gc.best_run(res)
        if nfeas > 0:
            return self.feasible(res, best, nfeas, nruns, tol, n)
        return self.infeasible(res, tol, n, m)

    def feasible(self, res, best, nfeas, nruns, tol, n) -> int:
        frac = nfeas / nruns
        fragile = frac < FRAGILE
        self.out.append(f"FRAGILE: {'YES' if fragile else 'no'} ({nfeas} of {nruns} runs feasible; "
                        f"fragile means under {FRAGILE * 100:.0f} %)")
        obj = gc.objective_name(best)
        self.out.append(f"BEST: {obj} = {gc.objective_text(best)}" if obj else "BEST: feasible (no objective)")
        act = [gc.describe_row(r, best, self.instance) for r in gc.active_rows(best, tol)]
        self.out.append(f"BINDING ROWS AT THE BEST DESIGN (|violation| <= {gc.ACTIVE_TOL:g}): these limit the objective")
        self.out += gc.table([gc.row_cells(d, False) for d in act]) or ["  (none)"]
        self.report.update(fragile=fragile, binding=act, best_objective=gc.objective_display(best))
        me = gc.rel_path(str(Path(__file__).resolve().parent / "check.py"))
        flags = " ".join(gc.shell_quote(x) for x in self.cli_args(n)[2:])
        if fragile:
            nxt = (f"fragile: confirm with python3 {me} {gc.shell_quote(self.args.instance)} --solve --starts {max(4 * n, 1024)} "
                   f"{flags}".rstrip() + " (or --polish) before trusting the optimum.")
        else:
            bound_rows = [d for d in act if d["kind"] == "criterion bound" and d.get("bound") is not None]
            if bound_rows and obj:
                d = bound_rows[0]
                b = d["bound"]
                step = 0.05 * abs(b) if b else 1.0
                sgn = 1 if d["role"] == "max" else -1
                bs = ",".join(f"{b + sgn * i * step:.4g}" for i in (-1, 0, 1, 2))
                pareto = (f"{gc.rel_path(str(self.cli))} {gc.shell_quote(self.args.instance)} {flags} --pareto {d['criterion']} "
                          f"--bounds {bs}").replace("  ", " ")
                nxt = (f"feasible. Each binding bound limits {obj}; to see the trade-off of one, run a Pareto study, "
                       f"e.g. {pareto}")
            else:
                nxt = "feasible. Save the design with check.py's NEXT line (check.py INSTANCE --solve)."
        return self.emit("FEASIBLE", 0, nxt)

    # ------------------------------------------------------------ infeasible
    def infeasible(self, res, tol, n, m) -> int:
        runs = sorted(gc.all_runs_full(res), key=lambda r: (r.get("max_violation") is None,
                                                             r.get("max_violation") or math.inf))[:TOP_RUNS]
        if not runs:
            self.out.append("ERRORS\n  the multistart returned no run to analyse")
            return self.emit("ERROR", 3, "run check.py on the instance with --solve to see the CLI's report")
        best = runs[0]
        self.out.append(f"VIOLATED ROWS AT THE BEST RUN (criterion bounds in their display unit, other rows in SI)")
        cands: dict[str, dict] = {}
        for i, r in enumerate(runs):
            descr = [gc.describe_row(row, r, self.instance) for row in gc.violated_rows(r, tol)]
            descr.sort(key=lambda d: -(d["violation_si"] or 0))
            head = (f"run {r.get('index')} ({r.get('origin')}): objective {gc.objective_text(r)}, {len(descr)} rows "
                    f"violated, max violation {gc.g(r.get('max_violation'), 4)} (SI) on {r.get('worst')}")
            if i == 0:
                self.out.append(f"  {head}")
                self.out += gc.table([gc.row_cells(d, True) for d in descr], indent="    ")
                if len(runs) > 1:
                    self.out.append(f"NEXT BEST RUNS ({len(runs) - 1})")
            else:
                self.out.append(f"  {head}")
            for d in descr:
                c = cands.setdefault(d["name"], {"name": d["name"], "kind": d["kind"], "count": 0,
                                                 "at_best": i == 0, "viol_si": 0.0, "viol": 0.0, "first": i})
                c["count"] += 1
                c["viol_si"] = max(c["viol_si"], d["violation_si"] or 0.0)
                c["viol"] = max(c["viol"], d["violation"] or 0.0)
                if d["kind"] == "criterion bound":
                    c.update(criterion=d["criterion"], role=d["role"], unit=d["unit"], bound=d["bound"])
        best_descr = [gc.describe_row(row, best, self.instance) for row in gc.violated_rows(best, tol)]
        self.report["violated_best_runs"] = [
            {"run": r.get("index"), "objective": gc.objective_display(r), "max_violation": r.get("max_violation"),
             "rows": [gc.describe_row(row, r, self.instance) for row in gc.violated_rows(r, tol)]} for r in runs]

        # signature
        # Compared in display units and in SI: the solver balances SI violations, so an impossible
        # requirement often shows as equal SI violations on rows of different display units.
        disp = magnitude_cluster([d["violation"] for d in best_descr if d["violation"]])
        si = magnitude_cluster([d["violation_si"] for d in best_descr if d["violation_si"]])
        size, lo, hi = si if si[0] > disp[0] else disp
        units = "SI" if si[0] > disp[0] else "display unit for bounds, SI for other rows"
        if size == 2 and len(best_descr) == 2:
            names = " and ".join(d["name"] for d in best_descr)
            sig = (f"SIGNATURE: CONFLICT. Exactly 2 rows are violated at the best run, by similar amounts ({gc.g(lo, 3)} "
                   f"to {gc.g(hi, 3)} {units}): {names} cannot hold together, and the solver splits the miss between "
                   f"them. More starts will not help. Relax ONE of the two (or, for an 'in domain' row, enlarge "
                   f"the domain).")
        elif size >= 3:
            sig = (f"SIGNATURE: OVER-CONSTRAINED. {len(best_descr)} rows are violated at the best run and {size} of "
                   f"them by similar amounts ({gc.g(lo, 3)} to {gc.g(hi, 3)} {units}, within a factor "
                   f"{SIGNATURE_FACTOR:g}). The solver spreads one impossible requirement over the rows that compete "
                   f"for it: more starts or looser solver settings will not help. Relax ONE requirement.")
        else:
            sig = (f"SIGNATURE: not the over-constraint pattern ({len(best_descr)} violated row(s) at the best run). "
                   f"Either one requirement is impossible on its own (check its sign, units and target), or the "
                   f"solver misses the feasible designs: try --starts {4 * n}, narrower design domains, or a "
                   f"better written design with check.py --polish.")
        self.out.append(sig)
        worst = best.get("max_violation") or 0.0
        if worst > LARGE_VIOLATION:
            self.out.append(f"SCALE: LARGE. The best design misses by {gc.g(worst, 3)} (SI) on {best.get('worst')}: far "
                            f"from feasible. Look for a modelling error before relaxing: a wrong sign or unit, an "
                            f"unreachable target, a motion the mechanism cannot make (assembly rows), a domain that "
                            f"excludes every solution.")
        self.report["large_violation"] = worst > LARGE_VIOLATION
        self.report["signature"] = {"over_constrained": size >= 3, "conflict_pair": size == 2 and len(best_descr) == 2,
                                    "violated_rows": len(best_descr),
                                    "cluster": size, "low": lo, "high": hi}

        # explanations for rows that cannot be relaxed by a flag
        explain = []
        for c in cands.values():
            if c["kind"] == "assembly":
                explain.append(f"  {c['name']}: the linkage cannot close (the two links cannot reach each other) at "
                               f"some pose. Lengths come from the reference pose, so widen the pivot domains or "
                               f"the link range, or shorten the motion (span); see let-and-mechanisms.md.")
            elif c["kind"] == "membership":
                explain.append(f"  {c['name']}: the design point leaves its (non-parallelogram) domain. Enlarge the "
                               f"domain, or relax what pushes the point out.")
        if explain:
            self.out += ["EXPLAIN (rows no flag can relax)"] + explain

        # candidate knobs
        order = sorted(cands.values(), key=lambda c: (-c["count"], c["first"], -c["viol_si"]))
        probes_todo = [c for c in order if c["kind"] in ("criterion bound", "constraint")]
        knob_lines = []
        for c in probes_todo:
            if c["kind"] == "criterion bound":
                idx, entry = self.criterion_entry(c["criterion"])
                c["criterion_index"] = idx
                bnd = entry.get("bound") if entry else None
                src = ""
                if isinstance(bnd, str) and bnd in (self.instance.get("params") or {}):
                    users = [x.get("name") for x in self.instance.get("criteria") or []
                             if isinstance(x, dict) and x.get("name") != c["criterion"] and
                             bnd in gc.IDENT.findall(f"{x.get('bound', '')} {x.get('expr', '')}")]
                    users += [x.get("name") for x in self.instance.get("constraints") or []
                              if isinstance(x, dict) and bnd in gc.IDENT.findall(x.get("expr", ""))]
                    src = f"bound = params.{bnd}" + (f" (also used by {', '.join(users)})" if users else "")
                    c["param"] = bnd
                elif bnd is not None:
                    src = f"bound = {json.dumps(bnd)} in criteria[{idx}]"
                d, v = gc.referenced_params(entry.get("expr", "") if entry else "", self.instance)
                knob_lines.append(f"  {c['name']}: {src}; violated in {c['count']} of {len(runs)} best runs"
                                  + (f"; expression params: {', '.join(d)}" if d else "")
                                  + (f"; via lets: {', '.join(v)}" if v else ""))
            else:
                idx = self.constraint_index(c["name"])
                c["constraint_index"] = idx
                entry = (self.instance.get("constraints") or [])[idx] if idx is not None else {}
                d, v = gc.referenced_params(entry.get("expr", ""), self.instance)
                where = f"constraints[{idx}]" if idx is not None else "not in the file (added by --set?), not probed"
                cp = constraint_param(entry.get("expr", ""), self.instance.get("params") or {}) if idx is not None else None
                if cp:
                    c["param"], c["param_dir"] = cp
                    c["param_users"] = self.param_users(cp[0], c["name"])
                knob_lines.append(f"  {c['name']}: {where} \"{entry.get('expr', '?')}\"; violated in "
                                  f"{c['count']} of {len(runs)} best runs; params: {', '.join(d) or '-'}"
                                  + (f"; via lets: {', '.join(v)}" if v else "")
                                  + (f"; its limit is params.{cp[0]} ({'raise' if cp[1] > 0 else 'lower'} it to "
                                     f"relax; also used by: {', '.join(c['param_users']) or 'nothing else'})" if cp
                                     else " (change one of these params to relax it partly)"))
        self.out += ["KNOBS (what each violated requirement depends on)"] + (knob_lines or ["  (none)"])
        self.param_values([c for c in probes_todo if c.get("param")])

        self.report["candidates"] = [{k: v for k, v in c.items() if k not in ("probe_first", "probe_param_first")}
                                     for c in order]
        probes = self.run_probes(probes_todo, m)
        return self.relaxation_report(probes, best, n, m)

    def probe(self, change: dict, m: int) -> dict:
        for p in self.probed:
            if p["flag"] == change["flag"]:
                return dict(p, k=change.get("k", p.get("k")), duplicate=True)
        t = time.monotonic()
        sets = [change["set"]] if "set" in change else []
        bounds = {change["criterion"]: change["value"]} if "value" in change else {}
        r = gc.run_cli(self.cli, self.args.instance, self.cli_args(m, sets, bounds), self.args.timeout)
        out = dict(change, wall=time.monotonic() - t)
        self.probed.append(out)
        if r.results is None:
            out.update(feasible=0, runs=0, error="; ".join(r.errors) or f"exit {r.returncode}")
            return out
        k, nr = gc.run_counts(r.results)
        b = gc.best_run(r.results)
        out.update(feasible=k, runs=nr, objective=gc.objective_display(b) if k else None,
                   objective_text=gc.objective_text(b) if k else "-",
                   max_violation=b.get("max_violation") if b else None,
                   stopped=bool(r.results.get("stopped")) or r.timed_out)
        return out

    def param_users(self, param: str, exclude: str) -> list[str]:
        users = []
        for x in (self.instance.get("constraints") or []) + (self.instance.get("criteria") or []):
            if isinstance(x, dict) and x.get("name") != exclude and \
                    param in gc.IDENT.findall(f"{x.get('expr', '')} {x.get('bound', '')}"):
                users.append(x.get("name"))
        for kind in ("let", "params"):
            for n, body in (self.instance.get(kind) or {}).items():
                if isinstance(body, str) and param in gc.IDENT.findall(body):
                    users.append(f"{kind}.{n}")
        for kind in ("design", "sweeps"):
            for n, entry in (self.instance.get(kind) or {}).items():
                if isinstance(entry, dict) and any(isinstance(entry.get(k), str) and param in gc.IDENT.findall(entry[k])
                                                   for k in ("min", "max", "domain")):
                    users.append(f"{kind}.{n}")
        return users

    def param_values(self, cands: list[dict]) -> None:
        """SI value of each constraint's limit param (one --eval with a --probe per param)."""
        names = list(dict.fromkeys(c["param"] for c in cands))
        if not names:
            return
        extra = [x for n in names for x in ("--probe", n)]
        r = gc.run_cli(self.cli, self.args.instance, self.cli_args(0, mode="--eval") + extra, self.args.timeout)
        values = ((r.results or {}).get("probes") or {})
        for c in cands:
            v = values.get(c["param"])
            if isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v):
                c["param_value"] = float(v)
            else:
                c.pop("param", None)

    def param_change(self, c: dict, k: float) -> dict:
        v0, d = c["param_value"], c["param_dir"]
        step = k * c["viol_si"]
        exact = v0 + d * step
        clamped = v0 != 0 and (exact > 0) != (v0 > 0)
        if clamped:  # a negative gap or length would change the row's meaning
            exact, step, val = 0.0, abs(v0), "0"
        else:
            val = nice_bound(exact, v0, step, d)
        rel = abs(exact - v0) / abs(v0) if v0 else math.inf
        return {"name": c["name"], "param": c["param"], "k": k, "pvalue": val, "clamped": clamped, "rel": rel,
                "set": f"params.{c['param']}={val}", "flag": f"--set params.{c['param']}={val}",
                "from": v0, "direction": "increase" if d > 0 else "decrease", "users": c.get("param_users", [])}

    def bound_change(self, c: dict, k: float) -> dict:
        direction = 1 if c["role"] == "max" else -1
        step = k * c["viol"]
        exact = c["bound"] + direction * step
        clamped = c["bound"] != 0 and (exact > 0) != (c["bound"] > 0)
        if clamped:  # crossing zero flips the meaning of a gap, length, angle or ratio
            exact, step = 0.0, abs(c["bound"])
            val = "0"
        else:
            val = nice_bound(exact, c["bound"], step, direction)
        rel = abs(exact - c["bound"]) / abs(c["bound"]) if c["bound"] else math.inf
        return {"name": c["name"], "criterion": c["criterion"], "k": k, "value": val, "clamped": clamped, "rel": rel,
                "flag": f"--bound {c['criterion']}={val}", "unit": c["unit"],
                "from": c["bound"], "direction": "increase" if direction > 0 else "decrease"}

    def run_probes(self, cands: list[dict], m: int) -> list[dict]:
        a = self.args
        done: list[dict] = []
        t_start = time.monotonic()

        def budget_left() -> bool:
            return len(done) < a.max_probes and time.monotonic() - t_start < a.budget

        skipped = []
        # pass 1: every candidate once (bounds and constraint params at k = 10; constraints disabled)
        for c in cands:
            if not budget_left():
                skipped.append(c["name"])
                continue
            if c["kind"] == "criterion bound":
                ch = self.bound_change(c, LADDER_FIRST)
            elif c.get("constraint_index") is not None:
                if c.get("param"):
                    res = self.probe(self.param_change(c, LADDER_FIRST), m)
                    c["probe_param_first"] = res
                    if not res.get("duplicate"):
                        done.append(res)
                    if not budget_left():
                        skipped.append(c["name"] + " (disable)")
                        continue
                i = c["constraint_index"]
                ch = {"name": c["name"], "set": f"constraints[{i}].enabled=false", "rel": 1.0,
                      "flag": f"--set constraints[{i}].enabled=false"}
            else:
                continue
            res = self.probe(ch, m)
            c["probe_first"] = res
            if not res.get("duplicate"):
                done.append(res)
        # pass 2: smallest working step for the bounds and params that work at k = 10
        for c in cands:
            if c["kind"] == "criterion bound" and c.get("probe_first", {}).get("feasible"):
                change = self.bound_change
            elif c.get("param") and c.get("probe_param_first", {}).get("feasible"):
                change = self.param_change
            else:
                continue
            for k in LADDER_REFINE:
                if not budget_left():
                    break
                res = self.probe(change(c, k), m)
                if not res.get("duplicate"):
                    done.append(res)
                if res["feasible"]:
                    break
        # pass 3: nothing works yet -> larger steps
        if not any(p["feasible"] for p in done):
            for c in cands:
                if c["kind"] == "criterion bound" and "probe_first" in c and budget_left():
                    res = self.probe(self.bound_change(c, LADDER_ESCALATE), m)
                elif c.get("param") and "probe_param_first" in c and budget_left():
                    res = self.probe(self.param_change(c, LADDER_ESCALATE), m)
                else:
                    continue
                if not res.get("duplicate"):
                    done.append(res)
        self.times["probes"] = time.monotonic() - t_start
        self.report["skipped_candidates"] = skipped
        if skipped:
            self.out.append(f"NOT PROBED (--max-probes / --budget reached): {', '.join(skipped)}")
        return done

    def relaxation_report(self, probes: list[dict], best: dict, n: int, m: int) -> int:
        obj = gc.objective_name(best)
        sign = -1 if obj and best["criteria"][obj]["role"] == "maximize" else 1

        def key(p):
            o = p.get("objective")
            return (0 if p["feasible"] else 1, math.inf if o is None else sign * o, -p["feasible"])

        probes.sort(key=key)
        rows = [["change", "what", "feasible", "objective", "note"]]
        for p in probes:
            what = self.what(p)
            if p.get("error"):
                note = f"error: {p['error']}"
            elif p["feasible"] and p["feasible"] / max(p["runs"], 1) < FRAGILE:
                note = "fragile"
            elif not p["feasible"]:
                note = f"best max viol {gc.g(p.get('max_violation'), 3)}"
            else:
                note = ""
            rows.append([p["flag"], what, f"{p['feasible']} of {p['runs']}", p.get("objective_text", "-"), note])
        self.out.append(f"RELAXATIONS ({len(probes)} probes, {m} starts each; feasible first, then best objective)")
        self.out += gc.table(rows) if probes else ["  (none: no violated criterion bound or constraint to relax)"]
        self.out.append("NEVER loosen solver.feas_tol: it would accept the violated design")
        self.report["relaxations"] = probes

        feas = [p for p in probes if p["feasible"]]
        if not feas:
            first = ("fix the rows under EXPLAIN first (no bound relaxation can close a linkage); then "
                     if any(c["kind"] in ("assembly", "membership") for c in self.report.get("candidates", [])) else "")
            self.out.append(f"RECOMMEND: no single relaxation tried here is feasible; {first}relax two requirements "
                            f"together (--bound A=.. --bound B=..), make one bigger, or revisit the model "
                            f"(see KNOBS and SCALE).")
            me = gc.rel_path(str(Path(__file__).resolve().parent / "check.py"))
            return self.emit("INFEASIBLE", 1, f"try a combined relaxation with python3 {me} {gc.shell_quote(self.args.instance)} "
                                              f"--solve --starts {m} --bound A=.. --bound B=.., or rethink the "
                                              f"requirements.")
        # the smallest change that works, in multiples k of the row's own violation (unit-free);
        # disabling a constraint comes last
        pick = sorted(feas, key=lambda p: (p.get("k", math.inf), p.get("rel", 1.0), -p["feasible"], key(p)))[0]
        smallest: dict[str, dict] = {}
        for p in feas:
            if p["name"] not in smallest or p.get("k", math.inf) < smallest[p["name"]].get("k", math.inf):
                smallest[p["name"]] = p
        best_obj = sorted(smallest.values(), key=key)[0]
        frag = pick["feasible"] / max(pick["runs"], 1) < FRAGILE
        if "value" in pick:
            idx, entry = self.criterion_entry(pick["criterion"])
            param = entry.get("bound") if entry else None
            unit = pick["unit"]
            si = float(pick["value"]) / gc.scale_of(unit)
            keep = f"write \"bound\": \"{pick['value']}{unit}\" in criteria[{idx}] ({pick['criterion']})"
            if isinstance(param, str) and param in (self.instance.get("params") or {}):
                keep += f", or set params.{param} = {gc.g(si)} (SI) if no other row uses it (see KNOBS)"
        elif "param" in pick:
            keep = (f"set params.{pick['param']} = {pick['pvalue']} (SI: a JSON number in metres or radians)"
                    + (f"; this ALSO changes {', '.join(pick['users'])}" if pick.get("users") else ""))
        else:
            keep = (f"set \"enabled\": false in {pick['flag'].split()[1].split('.')[0]} ({pick['name']}). This "
                    f"REMOVES the requirement: say so to the user, or change one of its params instead (see KNOBS)")
        self.out.append(f"RECOMMEND: {pick['flag']}  (smallest tried change that restores feasibility: "
                        f"{self.what(pick)}; {pick['feasible']} of {pick['runs']} runs feasible, objective "
                        f"{pick.get('objective_text', '-')})"
                        + (". FRAGILE: few runs reach it, confirm with more starts or --polish" if frag else ""))
        if best_obj is not pick:
            self.out.append(f"BEST OBJECTIVE (among the smallest working change of each requirement): "
                            f"{best_obj['flag']}  ({self.what(best_obj)}; objective "
                            f"{best_obj.get('objective_text', '-')}). Which requirement to give up is a design "
                            f"decision: compare the RELAXATIONS table.")
        self.out.append(f"TO KEEP IT: {keep}")
        me = gc.rel_path(str(Path(__file__).resolve().parent / "check.py"))
        base = self.cli_args(m)[2:]
        if "value" in pick:  # the probe's bound replaces a --bound of the same criterion
            drop = {i for i, a in enumerate(base) if a == "--bound" and i + 1 < len(base)
                    and base[i + 1].split("=", 1)[0] == pick["criterion"]}
            base = [a for i, a in enumerate(base) if i not in drop and i - 1 not in drop]
        cmd = " ".join(["python3", me, gc.shell_quote(self.args.instance), "--solve", "--starts", str(m),
                        *(gc.shell_quote(a) for a in base), *(gc.shell_quote(a) for a in pick["flag"].split(" ", 1))])
        return self.emit("INFEASIBLE", 0, f"confirm with: {cmd} ; then make the change in the instance (TO KEEP IT).")

    @staticmethod
    def what(p: dict) -> str:
        if "param" in p:
            pct = f", {p['rel'] * 100:.1f} %" if math.isfinite(p.get("rel", math.inf)) else ""
            clamp = ", clamped at 0" if p.get("clamped") else ""
            users = f"; also changes {', '.join(p['users'])}" if p.get("users") else ""
            return (f"{p['direction']} params.{p['param']} {gc.g(p['from'])} -> {p['pvalue']} SI "
                    f"(row {p['name']}, k={p['k']:g}{pct}{clamp}{users})")
        if "value" not in p:
            return f"disable constraint {p['name']} (removes the requirement)"
        pct = f", {p['rel'] * 100:.1f} %" if math.isfinite(p.get("rel", math.inf)) else ""
        clamp = ", clamped at 0" if p.get("clamped") else ""
        return f"{p['direction']} bound {gc.g(p['from'])} -> {p['value']} {p['unit']} (k={p['k']:g}{pct}{clamp})"


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    return Diagnosis(args).run()


if __name__ == "__main__":
    sys.exit(main())
