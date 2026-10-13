"""First-order stationarity of the max row violation at failed endpoints.

At x with rows g (equality rows as +h and -h) and Jacobian J, the max
violation phi(x) = max_i g_i(x) has a descent direction d inside the box iff
the LP  min s  s.t.  J_i d <= s (i active),  -1 <= d <= 1, d_j >= 0 at x_j = 0,
d_j <= 0 at x_j = 1  has s* < 0. s* = 0: x is a first-order stationary point of
phi (a local minimiser of infeasibility to first order), which a restoration
phase cannot leave either.
"""
import collections
import json
import sys

import numpy as np
from scipy.optimize import linprog


def lp(point, n, eps):
    g = np.array(point["g"])
    J = np.array(point["J"]).reshape(len(g), n)
    x = np.array(point["x"])
    finite = np.isfinite(g) & np.all(np.isfinite(J), axis=1)
    vmax = g[finite].max()
    act = np.where(finite & (g >= vmax - eps))[0]
    c = np.zeros(n + 1)
    c[-1] = 1.0
    A = np.hstack([J[act], -np.ones((len(act), 1))])
    b = np.zeros(len(act))
    bounds = []
    for j in range(n):
        lo, hi = -1.0, 1.0
        if x[j] <= 1e-12:
            lo = 0.0
        if x[j] >= 1 - 1e-12:
            hi = 0.0
        bounds.append((lo, hi))
    bounds.append((None, None))
    r = linprog(c, A_ub=A, b_ub=b, bounds=bounds, method="highs")
    names = [point["names"][i] for i in act]
    return vmax, r.fun, names


def main():
    path, which = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
    d = json.load(open(path))
    n = d["n"]
    runs = d["runs"][:limit]
    for eps in (1e-9, 1e-6, 1e-4):
        stat = desc = 0
        smin = []
        act_names = collections.Counter()
        for r in runs:
            p = r[which]
            vmax, s, names = lp(p, n, eps)
            if vmax <= 1e-8:
                continue
            # Scale-free: s relative to the largest active gradient norm.
            if s < -1e-8:
                desc += 1
                smin.append(s)
            else:
                stat += 1
                for nm in names:
                    act_names[nm.split("[")[0]] += 1
        print(f"{path.split('/')[-1]} {which} eps={eps:g}: {len(runs)} runs, "
              f"stationary {stat}, descent available {desc}"
              + (f" (median s* {np.median(smin):.3g})" if smin else ""))
        if eps == 1e-6:
            print("   active rows at stationary points:",
                  ", ".join(f"{k} x{v}" for k, v in act_names.most_common(12)))
    v = [r[which]["g"] for r in runs]
    vm = [max(x for x in gg if np.isfinite(x)) for gg in v]
    print(f"   max violation at {which}: min {min(vm):.3g} median "
          f"{np.median(vm):.3g} max {max(vm):.3g}")
    st = collections.Counter(r["p1_status"] for r in runs)
    print("   replayed phase-1 attempt 0 statuses:", dict(st),
          "median p1 evals", np.median([r["p1_evals"] for r in runs]))


main()
