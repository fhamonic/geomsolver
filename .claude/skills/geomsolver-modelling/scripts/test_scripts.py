#!/usr/bin/env python3
"""End-to-end tests of check.py and diagnose.py against the real geomsolver-cli.

Run from anywhere: python3 .claude/skills/geomsolver-modelling/scripts/test_scripts.py
Uses copies of tests/data/tv_corner_ref.json in a temp dir for the broken and infeasible
variants, and checks that no input file is modified. Prints the wall time of every case.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
CHECK = HERE / "check.py"
DIAG = HERE / "diagnose.py"
FIXTURE = REPO / "tests" / "data" / "tv_corner_ref.json"
ROOM = REPO / "tests" / "data" / "example_room_ref.json"
TUTORIAL = REPO / "docs" / "instances" / "start-tutorial-5.json"
USER = REPO / "data" / "tv_corner.json"
LAYOUT = HERE.parent / "assets" / "templates" / "layout.json"
DIAG_LIMIT = 60.0

SECTIONS = ["ERRORS", "WARNINGS", "RUNS", "DESIGN", "CRITERIA", "ACTIVE", "VIOLATED", "PROBES"]

failures: list[str] = []
timings: list[tuple[str, float, int]] = []


def run(name: str, script: Path, *args: str, timeout: float = 300) -> tuple[int, str, float]:
    t = time.monotonic()
    p = subprocess.run([sys.executable, str(script), *args], capture_output=True, text=True,
                       timeout=timeout, cwd=REPO)
    wall = time.monotonic() - t
    timings.append((name, wall, p.returncode))
    return p.returncode, p.stdout, wall


def expect(name: str, cond: bool, detail: str = "") -> None:
    print(f"  {'ok  ' if cond else 'FAIL'} {name}" + (f"  [{detail}]" if detail and not cond else ""))
    if not cond:
        failures.append(name)


def section_order_ok(out: str) -> bool:
    heads = [ln.split(" ")[0] for ln in out.splitlines() if ln and not ln.startswith(" ")]
    pos = [heads.index(s) for s in SECTIONS if s in heads]
    return len(pos) == len(SECTIONS) and pos == sorted(pos)


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest()


def make_variants(tmp: Path) -> dict[str, Path]:
    shutil.copy(ROOM, tmp / ROOM.name)
    base = json.loads(FIXTURE.read_text())
    text = FIXTURE.read_text()
    out = {}

    def write(name: str, content: str) -> None:
        (tmp / name).write_text(content)
        out[name] = tmp / name

    inf = json.loads(text)
    inf["params"]["edge_margin"] = 0.16  # 0.14 solves, 0.145 and above do not (256 starts)
    write("tv_infeasible.json", json.dumps(inf, indent=1))
    write("tv_syntax.json", text.replace('"clr": 0.02,', '"clr": 0.02', 1))
    write("tv_unknown.json", text.replace("clearance(tv, couch) >= clr", "clearance(tv, cuoch) >= clr", 1))
    warn = json.loads(text)
    warn["constraints"][0]["notes"] = "misspelt key"
    write("tv_warning.json", json.dumps(warn, indent=1))

    # mistakes the CLI accepts silently: check.py must list them under LINT
    trap = json.loads(LAYOUT.read_text())
    trap["constraints"] = [{"name": "desk_in", "expr": "clearance(desk, room) <= -gap"},
                           {"name": "chair_in", "expr": "clearance(chair, room) <= -gap"},
                           {"name": "chair_column", "expr": "clearance(chair, column) >= gap"}]
    trap["design"]["da"].update(min=-90, max=90)
    trap["criteria"][2]["bound"] = 5
    trap["design"]["S"]["fix"] = True
    trap["constraint"] = [{"name": "lost", "expr": "D.x >= 1"}]
    write("layout_trap.json", json.dumps(trap, indent=1))
    # two bounds that cannot hold together (lamp and door 2 m apart): a CONFLICT, not a search failure
    write("pair.json", json.dumps({
        "format": "geomsolver-instance/1",
        "params": {"near_max": "50cm", "far_min": "260cm"},
        "geometry": {"lamp": {"type": "point", "x": 1, "y": 1}, "door": {"type": "point", "x": 3, "y": 1}},
        "design": {"P": {"type": "point", "domain": "box(0, 0, 4, 3)", "value": [2, 2]}},
        "criteria": [{"name": "height", "expr": "P.y", "role": "minimize", "unit": "cm"},
                     {"name": "near_lamp", "expr": "dist(P, lamp)", "role": "max", "bound": "near_max", "unit": "cm"},
                     {"name": "far_door", "expr": "dist(P, door)", "role": "min", "bound": "far_min", "unit": "cm"}]},
        indent=1))
    # the only violated row is a constraint whose limit is a param: loosen the param, do not delete the row
    write("constraint_only.json", json.dumps({
        "format": "geomsolver-instance/1",
        "params": {"tol": "5cm"},
        "geometry": {"target": {"type": "point", "x": 1.3, "y": 0.5}},
        "design": {"P": {"type": "point", "domain": "box(0, 0, 1, 1)", "value": [0.5, 0.5]}},
        "constraints": [{"name": "near_target", "expr": "dist(P, target) <= tol"}],
        "criteria": [{"name": "low", "expr": "P.y", "role": "minimize", "unit": "cm"}]}, indent=1))
    assert base["params"]["edge_margin"] == 0.10
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="End-to-end tests of check.py and diagnose.py (needs build/geomsolver-cli).")
    ap.add_argument("--show", action="store_true", help="print every script output")
    a = ap.parse_args()

    inputs = [FIXTURE, ROOM, TUTORIAL] + ([USER] if USER.exists() else [])
    before = {p: sha(p) for p in inputs}
    tmp = Path(tempfile.mkdtemp(prefix="gs_skill_test_"))
    v = make_variants(tmp)
    var_before = {p: sha(p) for p in v.values()}

    def case(title, script, *args, timeout=300):
        print(f"\n== {title}")
        code, out, wall = run(title, script, *args, timeout=timeout)
        if a.show:
            print("\n".join("    | " + ln for ln in out.splitlines()))
        return code, out, wall

    for s in (CHECK, DIAG):
        code, out, _ = case(f"{s.name} --help", s, "--help")
        expect("exit 0 and usage", code == 0 and "usage:" in out)

    # ---- check.py
    code, out, _ = case("check: fixture, evaluate the written design", CHECK, str(FIXTURE))
    expect("exit 1, INFEASIBLE (the hand design violates bounds)", code == 1 and out.startswith("VERDICT: INFEASIBLE"))
    expect("all sections in order", section_order_ok(out))
    expect("VIOLATED lists link_angle in deg", "link_angle:bound" in out and "needs >= 15 deg" in out)
    expect("NEXT suggests --solve", out.splitlines()[-1].startswith("NEXT:") and "--solve" in out.splitlines()[-1])
    expect("no LINT on the fixture", "LINT" in out and out.split("LINT", 1)[1].split("RUNS")[0].count("\n  ") == 1
           and "(none)" in out.split("LINT", 1)[1].split("RUNS")[0])

    js = tmp / "check.json"
    code, out, _ = case("check: fixture --solve with a probe and --json", CHECK, str(FIXTURE), "--solve",
                        "--probe", "dist(A, B)", "--json", str(js))
    expect("exit 0, FEASIBLE", code == 0 and out.startswith("VERDICT: FEASIBLE"))
    expect("all sections in order", section_order_ok(out))
    expect("best objective 104.3729 cm", "protrusion = 104.3729 cm" in out)
    expect("ACTIVE lists centre_setback", "centre_setback:bound" in out.split("ACTIVE", 1)[1].split("VIOLATED")[0])
    expect("probe value printed", "dist(A, B) = 0.4332840195" in out)
    rep = json.loads(js.read_text())
    expect("--json has verdict, design and probes",
           rep["verdict"] == "FEASIBLE" and "A" in rep["design"] and "dist(A, B)" in rep["probes"])

    code, out, _ = case("check: fixture, duplicate --bound is deduplicated", CHECK, str(FIXTURE), "--solve",
                        "--bound", "centre_setback=12", "--bound", "centre_setback=11")
    cmd = next(ln for ln in out.splitlines() if ln.startswith("COMMAND:"))
    expect("only the last --bound reaches the CLI", cmd.count("--bound") == 1 and "centre_setback=11" in cmd)
    expect("exit 0, FEASIBLE", code == 0)

    code, out, _ = case("check: infeasible copy (edge_margin 0.16) --solve", CHECK, str(v["tv_infeasible.json"]),
                        "--solve")
    expect("exit 1, INFEASIBLE", code == 1 and out.startswith("VERDICT: INFEASIBLE"))
    expect("NEXT points to diagnose.py", "diagnose.py" in out.splitlines()[-1])

    code, out, _ = case("check: syntax error", CHECK, str(v["tv_syntax.json"]))
    expect("exit 2, COMPILE_ERROR", code == 2 and out.startswith("VERDICT: COMPILE_ERROR"))
    expect("parse error line verbatim", "parse error at line 10, column 14" in out)

    code, out, _ = case("check: unknown name", CHECK, str(v["tv_unknown.json"]), "--solve")
    expect("exit 2, COMPILE_ERROR", code == 2 and out.startswith("VERDICT: COMPILE_ERROR"))
    expect("error line verbatim", "  constraints[0].expr at column 14: unknown name 'cuoch'" in out)

    code, out, _ = case("check: warning (misspelt key)", CHECK, str(v["tv_warning.json"]), "--solve")
    expect("exit 0, FEASIBLE", code == 0 and out.startswith("VERDICT: FEASIBLE"))
    expect("warning listed", "warning: constraints[0].notes: unknown key 'notes'" in out)

    code, out, _ = case("check: start-tutorial-5 --solve", CHECK, str(TUTORIAL), "--solve")
    expect("exit 0, width 98.81456 cm", code == 0 and "width = 98.81456 cm" in out)

    code, out, _ = case("check: layout with silent mistakes", CHECK, str(v["layout_trap.json"]), "--solve")
    lint = out.split("LINT", 1)[1].split("\nRUNS")[0]
    expect("LINT: da min/max as bare numbers", "design.da.min is the number -90" in lint and "design.da.max" in lint)
    expect("LINT: numeric bound in deg", "criteria[2].bound (desk_view) is the number 5" in lint)
    expect("LINT: polygon and circle containment", "constraints[0] (desk_in)" in lint and "constraints[1] (chair_in)" in lint
           and "chair_column" not in lint)
    expect("LINT: misspelt keys the CLI drops silently",
           "top-level key 'constraint'" in lint and "design.S.fix is ignored without a warning: did you mean 'fixed'?" in lint)
    expect("NEXT starts with LINT", out.splitlines()[-1].startswith("NEXT: LINT lists 7"))

    code, out, _ = case("check: --probe that does not compile", CHECK, str(FIXTURE), "--probe", "dist(A)")
    expect("exit 2, ERROR naming the probe", code == 2 and out.startswith("VERDICT: ERROR") and "--probe" in out.splitlines()[-1])

    code, out, _ = case("check: --timeout stops a long job", CHECK, str(FIXTURE), "--starts", "200000", "--timeout", "2")
    nxt = out.splitlines()[-1]
    expect("runs reported, save command reruns only the finished starts",
           "[STOPPED by --timeout" in out and "--write-instance" in nxt and "--starts 200000" not in nxt)

    code, out, _ = case("check: unknown criterion in --bound", CHECK, str(FIXTURE), "--bound", "nope=3")
    expect("exit 2, ERROR", code == 2 and out.startswith("VERDICT: ERROR") and "no criterion named 'nope'" in out)

    code, out, _ = case("check: missing CLI", CHECK, str(FIXTURE), "--cli", str(tmp / "no-such-cli"))
    expect("exit 3, ERROR", code == 3 and out.startswith("VERDICT: ERROR"))

    code, out, _ = case("check: missing instance file", CHECK, str(tmp / "missing.json"))
    expect("exit 2, ERROR", code == 2 and out.startswith("VERDICT: ERROR") and "cannot open" in out)

    # ---- diagnose.py
    code, out, wall = case("diagnose: fixture (feasible)", DIAG, str(FIXTURE))
    expect("exit 0, FEASIBLE, not fragile", code == 0 and out.startswith("VERDICT: FEASIBLE") and "FRAGILE: no" in out)
    expect("binding rows listed", "BINDING ROWS" in out and "centre_setback:bound" in out)

    dj = tmp / "diag.json"
    code, out, wall = case("diagnose: infeasible copy (edge_margin 0.16)", DIAG, str(v["tv_infeasible.json"]),
                           "--json", str(dj))
    expect("exit 0: a single relaxation is feasible", code == 0 and out.startswith("VERDICT: INFEASIBLE"))
    expect("over-constraint signature detected", "SIGNATURE: OVER-CONSTRAINED" in out)
    expect("RECOMMEND line with a flag", any(ln.startswith("RECOMMEND: --") for ln in out.splitlines()))
    expect("feas_tol warning", "NEVER loosen solver.feas_tol" in out)
    expect(f"wall time under {DIAG_LIMIT:.0f} s", wall < DIAG_LIMIT, f"{wall:.1f} s")
    rep = json.loads(dj.read_text())
    expect("--json lists feasible relaxations", any(p["feasible"] for p in rep["relaxations"]))
    expect("centre_setback relaxation is among the feasible ones",
           any(p["feasible"] and p.get("criterion") == "centre_setback" for p in rep["relaxations"]))

    code, out, wall = case("diagnose: what-if via --set on the fixture", DIAG, str(FIXTURE),
                           "--set", "params.edge_margin=0.16", "--max-probes", "4")
    expect("exit 0, INFEASIBLE with a relaxation", code == 0 and out.startswith("VERDICT: INFEASIBLE"))

    for name in ("tv_syntax.json", "tv_unknown.json"):
        code, out, _ = case(f"diagnose: {name}", DIAG, str(v[name]))
        expect("exit 2, COMPILE_ERROR", code == 2 and out.startswith("VERDICT: COMPILE_ERROR"))

    code, out, wall = case("diagnose: two conflicting bounds", DIAG, str(v["pair.json"]))
    expect("exit 0, SIGNATURE: CONFLICT", code == 0 and "SIGNATURE: CONFLICT" in out)
    expect("RECOMMEND relaxes one of the two bounds", "RECOMMEND: --bound far_door=" in out or "RECOMMEND: --bound near_lamp=" in out)

    code, out, wall = case("diagnose: only a constraint is violated", DIAG, str(v["constraint_only.json"]))
    expect("exit 0, RECOMMEND loosens the param", code == 0 and "RECOMMEND: --set params.tol=" in out)
    expect("disabling is offered but marked", "removes the requirement" in out)

    code, out, wall = case("diagnose: start-tutorial-5 (feasible)", DIAG, str(TUTORIAL))
    expect("exit 0, FEASIBLE", code == 0 and out.startswith("VERDICT: FEASIBLE"))

    code, out, wall = case("diagnose: start-tutorial-5 with max_protrusion 50 cm", DIAG, str(TUTORIAL),
                           "--set", "params.max_protrusion=0.5")
    expect("exit 0, protrusion relaxation found", code == 0 and "RECOMMEND: --bound protrusion=" in out)
    expect("equal SI violations read as over-constrained", "SIGNATURE: OVER-CONSTRAINED" in out)

    if USER.exists():
        code, out, _ = case("check: data/tv_corner.json --solve (user instance, read only)", CHECK, str(USER),
                            "--solve")
        user_infeasible = code == 1
        print(f"  info: the user instance is currently {'INFEASIBLE' if user_infeasible else 'FEASIBLE'}")
        code, out, wall = case("diagnose: data/tv_corner.json (user instance, read only)", DIAG, str(USER),
                               "--json", str(dj))
        expect("exit 0", code == 0)
        expect(f"wall time under {DIAG_LIMIT:.0f} s", wall < DIAG_LIMIT, f"{wall:.1f} s")
        if user_infeasible:
            rep = json.loads(dj.read_text())
            ok = [p["flag"] for p in rep["relaxations"] if p["feasible"]]
            print(f"  info: feasible single relaxations: {', '.join(ok)}")
            expect("a centre_setback or centred_offset relaxation is feasible",
                   any(p["feasible"] and p.get("criterion") in ("centre_setback", "centred_offset")
                       for p in rep["relaxations"]))
    else:
        print("\n== data/tv_corner.json not present: skipped")

    print("\n== inputs unchanged")
    expect("repository inputs unchanged", all(sha(p) == h for p, h in before.items()))
    expect("temp variants unchanged", all(sha(p) == h for p, h in var_before.items()))
    shutil.rmtree(tmp, ignore_errors=True)

    print("\nWALL TIMES")
    for name, wall, code in timings:
        print(f"  {wall:6.2f} s  exit {code}  {name}")
    print(f"\n{len(failures)} failure(s)" + (": " + "; ".join(failures) if failures else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
