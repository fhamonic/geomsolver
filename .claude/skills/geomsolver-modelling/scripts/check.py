#!/usr/bin/env python3
"""Evaluate or solve a geomsolver instance and print a short fixed-format report.

The report always has these parts, in this order:
  VERDICT: FEASIBLE | INFEASIBLE | COMPILE_ERROR | ERROR
  MODE, COMMAND, then the sections ERRORS, WARNINGS, LINT, RUNS, DESIGN, CRITERIA,
  ACTIVE, VIOLATED, PROBES (each "(none)" when empty), and a final "NEXT:" line.
  LINT lists likely modelling mistakes that the CLI accepts without a warning (a bare number
  read as SI where a unit was meant; clearance(shape, region) <= -m used to keep a shape inside).

Exit status: 0 feasible, 1 infeasible, 2 compile or usage error, 3 other error.
The instance file is never modified: edits go through the CLI's --set/--bound/--fix.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gs_common as gc  # noqa: E402

EXAMPLES = """examples:
  check.py inst.json                          verify the design values written in the file
  check.py inst.json --solve                  multistart solve (solver.starts starts)
  check.py inst.json --solve --starts 512     more starts
  check.py inst.json --polish                 one run from the written design values
  check.py inst.json --solve --set params.clr=0.03 --bound protrusion=110
  check.py inst.json --solve --probe "dist(A, B)" --json report.json

--set PATH=VALUE: VALUE is JSON when it parses, else a string (params.gap=5cm,
  constraints[2].enabled=false, criteria[0].role=maximize). Values in SI unless a unit
  suffix is written. --bound NAME=VALUE: in the criterion's display unit (cm, deg, %)."""


class Parser(argparse.ArgumentParser):
    def error(self, message: str):
        self.print_usage(sys.stderr)
        print(f"VERDICT: ERROR\nERRORS\n  {self.prog}: {message}\nNEXT: fix the command line (check.py --help)")
        sys.exit(2)


def build_parser() -> argparse.ArgumentParser:
    p = Parser(
        prog="check.py",
        description="Check a geomsolver instance: compile it, evaluate (default) or solve it, and print a "
                    "short report ending with a NEXT: line. Exit 0 feasible, 1 infeasible, "
                    "2 compile/usage error, 3 other error.",
        epilog=EXAMPLES, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("instance", help="instance JSON file (read only)")
    p.add_argument("--solve", action="store_true", help="multistart solve instead of evaluating the written design")
    p.add_argument("--polish", action="store_true", help="solve with one run from the written design values")
    p.add_argument("--starts", type=int, help="multistart starts (implies --solve; default: solver.starts)")
    p.add_argument("--seed", type=int, help="multistart seed (implies --solve; default: solver.seed)")
    p.add_argument("--set", action="append", default=[], metavar="PATH=VALUE",
                   help="edit the instance for this run only (repeatable)")
    p.add_argument("--bound", action="append", default=[], metavar="NAME=VALUE",
                   help="replace a max/min criterion's bound, in its display unit (repeatable; last one per name wins)")
    p.add_argument("--fix", action="append", default=[], metavar="NAME[,NAME...]",
                   help="fix design variables at their written values (repeatable)")
    p.add_argument("--probe", action="append", default=[], metavar="EXPR",
                   help="also evaluate EXPR on the reported design (repeatable)")
    p.add_argument("--timeout", type=float, default=300.0, metavar="SEC",
                   help="interrupt the CLI after SEC seconds and report the runs that finished (default 300)")
    p.add_argument("--json", metavar="OUT", help="also write the report as JSON to OUT")
    p.add_argument("--cli", metavar="PATH",
                   help="geomsolver-cli to use (default: $GEOMSOLVER_CLI, else <repo>/build/geomsolver-cli)")
    return p


def fixed_variables(results: dict, instance: dict, sets: list[str], fixes: list[str]) -> set[str]:
    names = list((results.get("evaluation") or best(results) or {}).get("design", {}).keys())
    coords = results.get("coordinates")
    if coords is not None:
        bases = {c.split(".")[0] for c in coords}
        return {n for n in names if n not in bases}
    fixed = {n for n, d in (instance.get("design") or {}).items() if isinstance(d, dict) and d.get("fixed") is True}
    for s in sets:
        k, v = s.split("=", 1)
        parts = k.split(".")
        if len(parts) == 3 and parts[0] == "design" and parts[2] == "fixed":
            (fixed.add if v.strip() == "true" else fixed.discard)(parts[1])
    for f in fixes:
        fixed.update(x for x in f.split(",") if x)
    return fixed


def best(results: dict) -> dict | None:
    return gc.best_run(results)


def section(lines: list[str], title: str, body: list[str]) -> None:
    lines.append(title)
    lines.extend(body if body else ["  (none)"])


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    report: dict = {"instance": args.instance, "errors": [], "warnings": []}
    lines: list[str] = []

    def finish(verdict: str, code: int, next_line: str) -> int:
        report.update(verdict=verdict, exit=code, next=next_line)
        out = [f"VERDICT: {verdict}"] + lines + [f"NEXT: {next_line}"]
        print("\n".join(out))
        if args.json:
            try:
                with open(args.json, "w") as fh:
                    json.dump(report, fh, indent=1)
            except OSError as e:
                print(f"check.py: cannot write {args.json}: {e}", file=sys.stderr)
        return code

    try:
        bounds = gc.dedupe_bounds(args.bound)
        overrides = gc.override_args(args.set, bounds, args.fix)
    except gc.ScriptUsageError as e:
        lines.append("ERRORS")
        lines.append(f"  {e}")
        report["errors"] = [str(e)]
        return finish("ERROR", 2, "fix the command line of check.py (see ERRORS)")

    solve = args.solve or args.polish or args.starts is not None or args.seed is not None
    cli_args: list[str] = []
    if args.polish:
        mode = "polish: one run from the design values written in the instance"
        cli_args.append("--polish")
    elif solve:
        mode = "multistart solve"
    else:
        mode = "evaluation of the design values written in the instance (no solve)"
        cli_args.append("--eval")
    if args.starts is not None and not args.polish:
        cli_args += ["--starts", str(args.starts)]
    if args.seed is not None and not args.polish:
        cli_args += ["--seed", str(args.seed)]
    cli_args += overrides
    solve_flags = list(cli_args[1:] if cli_args and cli_args[0] == "--eval" else cli_args)
    for pr in args.probe:
        cli_args += ["--probe", pr]

    cli = gc.find_cli(args.cli)
    run = gc.run_cli(cli, args.instance, cli_args, args.timeout)
    report["command"] = run.display_command()
    lines.append(f"MODE: {mode}")
    lines.append(f"COMMAND: {run.display_command()}")
    report["mode"] = mode

    if run.launch_error and run.results is None:
        section(lines, "ERRORS", [f"  {run.launch_error}"])
        report["errors"] = [run.launch_error]
        return finish("ERROR", 3, "check the CLI path: pass --cli PATH or set GEOMSOLVER_CLI")

    report["errors"], report["warnings"] = run.errors, run.warnings
    err_body = [f"  {e}" for e in run.errors]
    warn_body = [f"  {w}" for w in run.warnings]
    warn_hint = " (Also read the WARNINGS: a misspelt key is ignored and often causes an error elsewhere.)" \
        if run.warnings else ""

    if run.returncode == 2:
        section(lines, "ERRORS", err_body)
        section(lines, "WARNINGS", warn_body)
        if run.usage_error:
            return finish("ERROR", 2, "fix the command line (the message names the bad option, path or name)"
                          + warn_hint)
        if run.errors and all(e.startswith("probes[") for e in run.errors):
            return finish("ERROR", 2, "a --probe expression does not compile (probes[i] = the i-th --probe, "
                          "column counted from 0): fix that --probe; the instance itself is fine.")
        if any("parse error" in e for e in run.errors):
            return finish("COMPILE_ERROR", 2, "the file is not valid JSON: fix it at the line and column given "
                          "(typically a missing comma, quote or bracket), then rerun check.py.")
        return finish("COMPILE_ERROR", 2,
                      "fix the errors above in the instance (path = JSON location, column = character in the "
                      "expression counted from 0), then rerun check.py." + warn_hint)

    results = run.results
    if results is None:
        section(lines, "ERRORS", err_body + [f"  geomsolver-cli exited with status {run.returncode} and no results"
                                             + (" (timed out)" if run.timed_out else "")])
        section(lines, "WARNINGS", warn_body)
        return finish("ERROR", 3, "raise --timeout or run the CLI by hand to see what happened"
                      if run.timed_out else "run the COMMAND above by hand to see the CLI's message")

    instance = gc.load_instance(args.instance)
    tol = gc.feas_tol(results)
    nfeas, nruns = gc.run_counts(results)
    brun = best(results)
    if brun is None:
        section(lines, "ERRORS", err_body + ["  the results file has no run"])
        return finish("ERROR", 3, "run the COMMAND above by hand to see the CLI's message")
    feasible = bool(brun.get("feasible")) if results.get("kind") == "evaluation" else nfeas > 0

    section(lines, "ERRORS", err_body)
    section(lines, "WARNINGS", warn_body)
    lint = gc.lint(cli, args.instance, overrides, min(args.timeout, 60.0))
    report["lint"] = lint
    section(lines, "LINT (likely mistakes the CLI does not report; read before trusting the result)",
            [f"  {x}" for x in lint])

    # RUNS
    runs_body = []
    obj_name = gc.objective_name(brun)
    obj_role = brun["criteria"][obj_name]["role"] if obj_name else None
    stopped = bool(results.get("stopped")) or run.timed_out
    if results.get("kind") == "evaluation":
        runs_body.append(f"  evaluation only: the design values written in the file are "
                         f"{'feasible' if feasible else 'NOT feasible'} (this says nothing about the problem: "
                         f"use --solve to search)")
    else:
        pct = 100.0 * nfeas / nruns if nruns else 0.0
        total = results.get("runs_total", nruns)
        runs_body.append(f"  {nfeas} feasible of {nruns} runs ({pct:.1f} %), {len(results.get('solutions', []))} "
                         f"distinct solutions, {results.get('wall_seconds', run.wall):.2f} s"
                         + (f"  [STOPPED by --timeout after {nruns} of {total} runs]" if stopped else ""))
    if obj_name:
        runs_body.append(f"  best objective: {obj_name} = {gc.objective_text(brun)} ({obj_role})"
                         + ("" if brun.get("feasible") else "  [infeasible design]"))
    else:
        runs_body.append("  no objective (no minimize/maximize criterion): a pure feasibility problem")
    worst, mv = brun.get("worst"), brun.get("max_violation")
    if worst:
        where = f" on {worst}" + (f" at {brun['worst_at']}" if brun.get("worst_at") else "")
        runs_body.append(f"  every row holds with margin >= {gc.g(-mv)} (SI); the tightest is{where[3:]}"
                         if mv is not None and mv < 0 else f"  largest violation {gc.g(mv)} (SI){where}")
    sols = results.get("solutions", [])
    if sols:
        runs_body.append("  top solutions:")
        for s in sols[:3]:
            b = s["best"]
            runs_body.append(f"    {s['rank']}. {gc.objective_text(b):<16} {'feasible' if b.get('feasible') else 'infeasible':<10}"
                             f" max viol {gc.g(b.get('max_violation'), 3):<10} hits {s.get('hits')}"
                             f"  run {b.get('index')} ({b.get('origin')})")
    section(lines, "RUNS", runs_body)
    report["runs"] = {"feasible": nfeas, "total": nruns, "distinct": len(sols), "stopped": stopped,
                      "objective": obj_name, "objective_role": obj_role,
                      "best_objective": gc.objective_display(brun),
                      "best_objective_unit": brun["criteria"][obj_name].get("unit") if obj_name else None,
                      "wall_seconds": results.get("wall_seconds", run.wall),
                      "top": [{"rank": s["rank"], "objective": gc.objective_display(s["best"]),
                               "feasible": s["best"].get("feasible"), "max_violation": s["best"].get("max_violation"),
                               "hits": s.get("hits"), "run": s["best"].get("index")} for s in sols[:3]]}

    # DESIGN
    fixed = fixed_variables(results, instance, args.set, args.fix)
    dd = brun.get("design_display", {})
    rows = []
    report["design"] = {}
    for name, val in brun.get("design", {}).items():
        if name in dd:
            v, unit = dd[name]["value"], dd[name]["unit"]
        else:
            v, unit = val, ("m" if isinstance(val, list) else "")
        rows.append([name, f"{gc.g(v)} {unit}".rstrip(), "[fixed]" if name in fixed else ""])
        report["design"][name] = {"value": v, "unit": unit, "si": val, "fixed": name in fixed}
    section(lines, "DESIGN", gc.table(rows))

    # CRITERIA
    rows = [["name", "role", "value", "unit", "bound", "margin", "status", "at"]]
    report["criteria"] = []
    for name, c in brun.get("criteria", {}).items():
        unit = c.get("unit") or ""
        s = gc.scale_of(unit)
        value = c.get("display", c.get("value"))
        bound = margin = None
        status = "-"
        if c.get("role") in ("max", "min") and c.get("bound") is not None:
            bound = c["bound"] * s
            viol = c.get("violation")
            margin = None if viol is None else -viol * s
            if viol is None or viol > tol:
                status = "VIOLATED"
            elif viol >= -gc.ACTIVE_TOL:
                status = "ACTIVE"
            else:
                status = "ok"
        rel = {"max": "<= ", "min": ">= "}.get(c.get("role"), "")
        expr = gc.criterion_expr(instance, name)
        at = gc.sweep_label(brun, c, instance, expr) if c.get("sweep_value") is not None else ""
        rows.append([name, c.get("role", ""), gc.g(value), unit or "-",
                     f"{rel}{gc.g(bound)}" if bound is not None else "-",
                     gc.g(margin, 4) if margin is not None else "-", status, at])
        report["criteria"].append({"name": name, "role": c.get("role"), "value": value, "unit": unit,
                                   "bound": bound, "margin": margin, "status": status, "at": at})
    section(lines, "CRITERIA", gc.table(rows) if len(rows) > 1 else [])

    # ACTIVE / VIOLATED
    act = [gc.describe_row(r, brun, instance) for r in gc.active_rows(brun, tol)]
    vio = [gc.describe_row(r, brun, instance) for r in gc.violated_rows(brun, tol)]
    vio.sort(key=lambda d: -(d["violation_si"] or 0))
    section(lines, f"ACTIVE (binding rows: |violation| <= {gc.ACTIVE_TOL:g})",
            gc.table([gc.row_cells(d, False) for d in act]))
    section(lines, f"VIOLATED (violation > feas_tol {tol:g}; criterion bounds in their display unit)",
            gc.table([gc.row_cells(d, True) for d in vio]))
    report["active"], report["violated"] = act, vio

    # PROBES
    probes = results.get("probes") if results.get("kind") == "evaluation" else results.get("probes_of_best")
    report["probes"] = probes or {}
    section(lines, "PROBES", [f"  {k} = {gc.g(v, 10)}" for k, v in (probes or {}).items()])

    # NEXT
    me = Path(__file__).resolve().parent

    def script(name: str, *extra: str) -> str:
        return " ".join(["python3", gc.rel_path(str(me / name)), gc.shell_quote(args.instance), *extra,
                         *(gc.shell_quote(a) for a in overrides)])

    stop_note = f"stopped by --timeout after {nruns} runs; " if stopped else ""
    if results.get("kind") == "evaluation":
        if feasible:
            nxt = f"the written design is feasible; optimise it with: {script('check.py', '--solve')}"
        else:
            nxt = (f"the written design is not feasible (normal for a first draft); solve with: "
                   f"{script('check.py', '--solve')}")
    elif not feasible:
        if args.polish:
            nxt = (f"{stop_note}polish did not reach a feasible design; run a multistart: "
                   f"{script('check.py', '--solve')} , then {script('diagnose.py')} if still infeasible.")
        else:
            nxt = (f"{stop_note}no feasible design in {nruns} runs; run: {script('diagnose.py')} "
                   f"(never loosen solver.feas_tol).")
    elif nruns and nfeas / nruns < 0.05 and not args.polish:
        nxt = (f"{stop_note}fragile: only {nfeas} of {nruns} runs are feasible; confirm with: "
               f"{script('check.py', '--solve', '--starts', str(max(512, 4 * int(results['settings'].get('starts', nruns)))))}"
               f" (or --polish) "
               f"before trusting the optimum.")
    else:
        if stopped and not args.polish:  # rerun only the starts that finished, not the whole job
            if "--starts" in solve_flags:
                solve_flags[solve_flags.index("--starts") + 1] = str(max(nruns - 1, 1))
            else:
                solve_flags = ["--starts", str(max(nruns - 1, 1)), *solve_flags]
        save = " ".join([gc.rel_path(str(cli)), gc.shell_quote(args.instance), *(gc.shell_quote(a) for a in solve_flags),
                         "--write-instance", "NEW.json"])
        nxt = f"{stop_note}feasible. To save this design to a new file (the original stays): {save}"
    if lint:
        nxt = (f"LINT lists {len(lint)} likely modelling mistake(s): fix them in the instance and rerun check.py "
               f"BEFORE using this result. Otherwise: " + nxt)
    return finish("FEASIBLE" if feasible else "INFEASIBLE", 0 if feasible else 1, nxt + warn_hint)


if __name__ == "__main__":
    sys.exit(main())
