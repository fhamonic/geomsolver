import json, sys, collections
import numpy as np
d = json.load(open(sys.argv[1])); n = d["n"]; which = sys.argv[2]
lim = int(sys.argv[3]) if len(sys.argv) > 3 else 10**9
cnt = collections.Counter(); flat = 0; tot = 0
for r in d["runs"][:lim]:
    p = r[which]; g = np.array(p["g"]); J = np.array(p["J"]).reshape(len(g), n)
    ok = np.isfinite(g); vmax = g[ok].max()
    if vmax <= 1e-8: continue
    tot += 1
    act = np.where(ok & (g >= vmax - 1e-6))[0]
    norms = np.linalg.norm(J[act], axis=1)
    allflat = bool(np.all(norms < 1e-12))
    flat += allflat
    key = " + ".join(sorted(set(p["names"][i].split("[")[0] + (" (zero grad)" if norms[k] < 1e-12 else "") for k, i in enumerate(act))))
    cnt[(key, round(vmax, 3) if len(sys.argv) <= 3 else 0)] += 1
print(f"{which}: {tot} infeasible endpoints, {flat} with every active row gradient exactly zero")
for (k, v), c in cnt.most_common(15): print(f"  x{c}: {k}" + (f"  vmax={v}" if v else ""))
