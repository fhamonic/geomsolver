# Does an l1 (sum of violations) measure, as used by IPOPT's restoration
# phase, have a first-order descent direction where the l-inf phase 1 stopped?
import json, sys, numpy as np
from scipy.optimize import linprog
d = json.load(open(sys.argv[1])); n = d["n"]; which = sys.argv[2]
lim = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
res = {"l1 descent": 0, "l1 stationary": 0}; only_flat = 0
for r in d["runs"][:lim]:
    p = r[which]; g = np.array(p["g"]); J = np.array(p["J"]).reshape(len(g), n); x = np.array(p["x"])
    ok = np.isfinite(g) & np.all(np.isfinite(J), axis=1)
    viol = ok & (g > 1e-8)
    if not viol.any(): continue
    grads = J[viol]
    if np.all(np.linalg.norm(grads, axis=1) < 1e-12): only_flat += 1
    c = grads.sum(axis=0)
    bounds = [((0.0 if x[j] <= 1e-12 else -1.0), (0.0 if x[j] >= 1 - 1e-12 else 1.0)) for j in range(n)]
    lr = linprog(c, bounds=bounds, method="highs")
    res["l1 descent" if lr.fun < -1e-8 else "l1 stationary"] += 1
print(which, res, "endpoints where every violated row is flat:", only_flat)
