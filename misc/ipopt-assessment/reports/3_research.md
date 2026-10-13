# IPOPT / CasADi in a Conan build: packaging cost and alternatives

Desk research, 2026-10-12. "Ran" means I executed the command on this machine (scratch dir `packaging/`). "Unverified" means I did not run it and found no primary source. Effort figures are estimates. Report saved to `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/RESEARCH.md`.

## Bottom line

- **Waiting for ConanCenter (CCI) gets you CasADi without IPOPT.** A core-only `casadi/3.8.1` recipe by the CasADi maintainer is approved and close to merging. It deliberately leaves IPOPT out because there is no `coin-ipopt` recipe.
- **IPOPT in CCI is not in sight.** Every open-source sparse solver IPOPT can use (MUMPS, SPRAL) is written in Fortran. CCI has no working Fortran setup: the 2024 attempts at gfortran, lapack, coin-mumps and coin-ipopt were all closed unmerged.
- **Three practical routes:**
  1. **Two local recipes** (coinmumps + ipopt), built with the system gfortran. Linux is straightforward; Windows is hard.
  2. **A Fortran-free IPOPT.** Build IPOPT with no sparse solver and pass it your own small dense KKT solver through `linear_solver=custom`. It works on all three OSes in principle, but uses an expert-level API and I have not tried it.
  3. **Reuse the binaries in the CasADi 3.8.1 manylinux_2_28 wheel.** I linked and ran this with GCC 15 / C++26. It is fine for experiments but not for shipping: the wheel carries a METIS 4 library whose licence forbids redistribution, and the Windows wheel is built with MinGW.
- **Packaging favours a solve-time-only prototype first.** Use the pip wheel, or its IPOPT C API from C++. Pay the packaging cost only if IPOPT measurably fixes the phase-1 failures.

---

## 1. ConanCenter: history and the Fortran situation

### 1.1 Attempts to add ipopt / mumps / casadi / spral

Query ran: `gh api search/issues -f q="<term> repo:conan-io/conan-center-index"` for ipopt, mumps, casadi, spral and fortran.

| Item | State | Facts |
|---|---|---|
| Issue #3657 `[request] coin-ipopt/3.13.3` | open since 2020-11-22 | https://github.com/conan-io/conan-center-index/issues/3657 |
| PR #22466 `coin-mumps: add recipe` (valgur) | closed unmerged 2025-03-17 by `conan-center-build-service[bot]` | The body says it "Requires GFortran with properly exported runtime libraries and updated OpenMPI: #23334, #18980". The recipe `requires("openmpi/4.1.6")` and `requires("gcc/13.2.0")` with `build_requires("gcc/<host_version>")` for gfortran, and uses CCI `metis/5.2.1`. CI failed: `Unable to find 'openmpi/4.1.6'`. https://github.com/conan-io/conan-center-index/pull/22466 (recipe read from head `870498a`) |
| PR #22467 `coin-ipopt: new recipe` (valgur) | closed unmerged 2025-03-17, same bot | `default_options: with_mumps=False # TODO: enable after merging #22466`, and `validate()` raises "At least one solver is required (currently only MUMPS is supported)". The CI reported "All green" because every configuration was skipped: the c3i `summary.json` shows `"status": "INVALID_CONFIGURATION"` for every configuration (ran `curl https://c3i.jfrog.io/c3i/misc-v2/logs/pr/22467/1/coin-ipopt/3.14.13//summary.json`). Nothing was ever built. https://github.com/conan-io/conan-center-index/pull/22467 |
| PR #22468 `casadi: new recipe` (valgur, IPOPT optional) | closed unmerged 2025-03-17 | https://github.com/conan-io/conan-center-index/pull/22468 |
| PR #23334 `gfortran: revive and redesign deprecated recipe` | closed by its author 2025-01-09 | "There is currently no usable Fortran compiler available on CCI". Author's closing comment: "Should be a part of the CI image or installed on the consumer system, really." https://github.com/conan-io/conan-center-index/pull/23334 |
| PR #23798 `lapack: new recipe` | closed 2025-01-09 | Closing comment: "Needs Fortran support." https://github.com/conan-io/conan-center-index/pull/23798 |
| PR #23417 `f2c: new recipe` | closed 2025-02-10 | "Too experimental and heavy patching for CCI." https://github.com/conan-io/conan-center-index/pull/23417 |
| **PR #31023 `casadi: new recipe` (jgillis, CasADi upstream maintainer)** | **open; approved by czoido (Conan team) 2026-10-08; `mergeable_state=blocked`** | casadi/3.8.1, core library plus first-party plugins only, LGPL-3.0-or-later, `shared=False` by default, `CASADIPATH` prepended in `runenv_info`. The body says: "IPOPT is blocked on a `coin-ipopt` recipe (#22467 was closed)". czoido tested shared and static builds on macOS armv8, Linux aarch64 gcc 10 and Windows ARM64 msvc 195. The body says the test_package solves an NLP with `sqpmethod`/`qrqp`, but the current head's `test_package.cpp` (`d5480be`) only checks a linear interpolant. https://github.com/conan-io/conan-center-index/pull/31023 |
| spral | nothing found | search total_count=0 |

**Why these PRs died.**
- The technical blocker was Fortran. coin-ipopt needed coin-mumps, which needed a gfortran package (#23334); its author closed #23334 on 2025-01-09 ("Should be a part of the CI image or installed on the consumer system").
- The final closure was a bulk sweep: `gh api search/issues q="repo:conan-io/conan-center-index is:pr is:unmerged closed:2025-03-17"` returns 224 PRs, **222 of them by valgur** (`… author:valgur` → 222). The bot posted no reason on #22466, #22467 or #22468, and locked them. A CLA-assistant comment from 2025-03-09 on each says "Martin Valgur seems not to be a GitHub user". Whether that triggered the sweep is unverified.
- Issue #24327 ("Prioritization of new recipe PRs") had listed casadi #22468, lapack #23798 and coin-ipopt #22467: https://github.com/conan-io/conan-center-index/issues/24327

### 1.2 Fortran in Conan 2 and in CCI

- **Conan 2.33** accepts the `"fortran"` key in `tools.build:compiler_executables`. CMakeToolchain maps it to `CMAKE_Fortran_COMPILER` and AutotoolsToolchain maps it to `FC`. Ran: `grep -rn fortran` over the installed `conan/` package, which shows `internal/model/conf.py:167`, `tools/cmake/toolchain/blocks.py:984` and `tools/gnu/autotoolstoolchain.py:300`. Conan settings have no Fortran compiler model, so the Fortran ABI is not part of `package_id`.
- **The Conan team's position** (memsharded, conan-io/conan#18008, 2025-06-24): openblas "do[es] not properly model the fortran usage… would be necessary to introduce… a `with_fortran` … option". This was tracked as CCI issue #27778, still open. The fix, PR #29446 (`openblas: add with_fortran option`, opened 2026-01-27), has no review; its author pinged on 2026-04-06. https://github.com/conan-io/conan/issues/18008, https://github.com/conan-io/conan-center-index/issues/27778, https://github.com/conan-io/conan-center-index/pull/29446
- **CCI recipes that touch Fortran.** Ran `gh api search/code q='fortran repo:conan-io/conan-center-index path:recipes filename:conanfile.py'`, which returned 18 files.
  - Most use it only to switch Fortran off: hdf5 `HDF5_BUILD_FORTRAN=False`, cgns, calceph, nlopt `NLOPT_FORTRAN=False`, highs `FORTRAN=False`, libmeshb, and openmpi with option `fortran` defaulting to `"no"`.
  - Only `gcc` builds a Fortran compiler: `--enable-languages=c,c++,fortran`, with `buildenv_info.define("FC", …gfortran-<ver>)`. However, its `validate()` raises on Windows, and ConanCenter has **no prebuilt binaries for gcc/15.2.0** (ran `conan list "gcc/15.2.0:*" -r conancenter`: "No packages found"). Using it means compiling GCC from source.
  - The old `gfortran/10.2` recipe is marked `deprecated = "gcc"`.
  - In short, no CCI library recipe currently requires gfortran.
- **openblas in CCI builds LAPACK without Fortran.** In `recipes/openblas/all/conanfile.py` (master), when `build_lapack` is set and `compiler_executables["fortran"]` is absent, the recipe sets `C_LAPACK=True` and `NOFORTRAN=True` and prints "Building LAPACK without a Fortran compiler". Upstream added this in OpenBLAS **0.3.21**: "when no Fortran compiler is available, OpenBLAS builds will now automatically build LAPACK from an f2c-converted copy of LAPACK 3.9.0" (OpenBLAS `Changelog.txt`, v0.3.30 tag). CCI versions run up to 0.3.30. Its binaries on conancenter are for gcc 11 and 13 only (ran `conan list "openblas/0.3.30:*"`), so with your gcc15 profile it builds from source, which `-b=missing` already allows.
  - Practical trap: if you set `compiler_executables["fortran"]` profile-wide, openblas switches to Fortran LAPACK and adds `gfortran` to `system_libs` without changing its package_id (issue #27778). Scope the setting per package, e.g. `coinmumps/*:tools.build:compiler_executables=...`.
- **pagmo2 recipe** (CCI master): it has a `with_ipopt` option (default False), but `validate()` does `if self.options.with_ipopt: raise ConanInvalidConfiguration("ipopt recipe not available yet in CCI")`. So the option is a placeholder.

### 1.3 Community recipes and remotes

Ran `gh api search/repositories` and `search/code` for `"coin-ipopt"` and `name = "ipopt"`.

| Source | State | What it does |
|---|---|---|
| `tttapa/conan-recipes` (Pieter P., alpaqa author) `recipes/ipopt` (3.14.16) and `recipes/coinmumps` (ThirdParty-Mumps 3.0.7 / MUMPS 5.7.1) | Conan 2; ipopt last touched 2025-09-01; no licence file | About 100 lines each. Uses Autotools with CCI `openblas/0.3.30`, no MPI and no METIS, and picks up the Fortran compiler from the environment. `system_libs` include `gfortran` and `quadmath`; there is an option to link them statically. **This is the best template for a local recipe.** https://github.com/tttapa/conan-recipes |
| `RoboConan/RoboConan` (MIT; a CCI fork) | Conan 2, active (pushed 2026-10-11) | Has coin-ipopt (default `with_spral=True`), coin-mumps (needs `openmpi`, its own `lapack/latest` meta-package and `metis`, and `compiler_executables["fortran"]` or it refuses), spral, coin-hsl, a `fortran` meta-package and casadi. You would have to adopt the fork's remote and its meta-packages. https://github.com/RoboConan/RoboConan |
| `sintef-ocean/conan-ipopt` | Conan 1 only (`from conans import …`); last commit 2022-01-10 | Unusable with Conan 2.33 as is. |
| `rhololkeolke/conan-ipopt` | last push 2019 | Dead. |

### 1.4 Reference points: vcpkg, conda-forge, Homebrew

- **vcpkg** (read `ports/*/vcpkg.json` and `portfile.cmake` via `gh api repos/microsoft/vcpkg/contents`):
  - `casadi` 3.8.1 is core only: no IPOPT, FMI off, `WITH_SELFCONTAINED=OFF`.
  - `coin-or-ipopt` (version-date 2023-02-01) gained a `mumps` default feature on **2026-09-25** with the new `mumps-solver` 5.9.1 port. Before that, the portfile comment says, it had no sparse solver: "LAPACK alone is dense only and leaves the solver unusable at run time. MUMPS is the only sparse solver Ipopt supports that is redistributable".
  - `mumps-solver` uses the scivision CMake build and calls `vcpkg_find_fortran`: "the system gfortran on Linux/macOS, or the MinGW gfortran from the vcpkg-gfortran dependency on Windows. On Windows this also switches the whole port to the MinGW toolchain and forces dynamic linkage." It supports `!(windows & !mingw & static)`.
  - There are no `ipopt`, `mumps` or `spral` ports.
- **conda-forge**:
  - `ipopt` 3.14.20 for linux-64/aarch64/ppc64le, osx-64/arm64 and win-64 (ran `curl https://api.anaconda.org/package/conda-forge/ipopt`). Host requirements are `mumps-seq`, `libspral # [linux]` and `ampl-asl`; Windows builds use `autotools_clang_conda` (ipopt-feedstock `recipe/meta.yaml`, `bld.bat`).
  - `mumps-seq` 5.8.2 is built on Windows with LLVM **flang** ("Pinning flang to 21 because of compilation error with 23") or Intel **ifx** (mumps-feedstock `recipe/conda_build_config.yaml`).
  - `casadi` 3.7.2 is built against `ipopt` and `mumps-seq`, and its test runs `cmake-package-check casadi --targets casadi::casadi`, so headers and a CMake config ship. There is no separate `libcasadi` package (ran the API query: not found).
- **Homebrew**: the `ipopt` 3.14.20 formula has `depends_on "gcc" # for gfortran` and builds a MUMPS 5.6.2 `resource` inside the formula, without METIS (homebrew-core `Formula/i/ipopt.rb`). There is **no `casadi` formula in homebrew-core**: `formulae.brew.sh/api/formula/casadi.json` returned 404, although PR #31023 says "packaged for Homebrew".

---

## 2. Building IPOPT from source

- **Build system.** Ipopt 3.14.20 (released 2026-08-27, `gh api repos/coin-or/Ipopt/releases`) uses autotools only. The maintainer answered "Does Ipopt support building with cmake?" with "No, it doesn't" (coin-or/Ipopt#649). CMake ports exist only as unofficial forks (rjodon/coinor-ipopt-with-cmake, last push 2020; and others). Ipopt itself is C++. Since 3.14 "the check for a Fortran compiler can be disabled via `--disable-f77`", and "Lapack is no longer optional, but required" (Ipopt `ChangeLog.md`).
- **Linear solvers.** The interfaces in `src/Algorithm/LinearSolvers/` at `releases/3.14.20` are MA27, MA57, MA77, MA86, MA97, MUMPS, Pardiso (project), PardisoMKL, SPRAL, WSMP and iterative WSMP. Their status:
  - **MUMPS**: CeCILL-C, Fortran 90 ("MUMPS source requires a Fortran 90 compiler", ThirdParty-Mumps README).
  - **SPRAL**: BSD-3 (`LICENCE`, STFC). GitHub's language breakdown is 1.76 MB Fortran against 0.40 MB C++. It needs METIS and hwloc, builds with Meson, and needs `OMP_CANCELLATION=TRUE OMP_PROC_BIND=TRUE` (README; Ipopt INSTALL).
  - **HSL**: "freely available for personal commercial or non-commercial usage (Coin-HSL Archive)" or "academic use only" (Ipopt INSTALL). Not open source.
  - **Pardiso project**: licensed. **WSMP**: 90-day trial. **Pardiso from MKL**: free but proprietary (licence terms not checked here).
  - **There is no Fortran-free, open-source sparse solver for IPOPT**, and IPOPT has no built-in LAPACK/dense solver (the interface list above has none).
- **The Fortran-free loophole: `linear_solver=custom`.**
  - `IpAlgBuilder.cpp` (3.14.20) always registers the option value `"custom"` ("use custom linear solver (expert use)"), and it becomes the default when no solver is linked.
  - `AlgorithmBuilder(SmartPtr<AugSystemSolver> custom_solver, const std::string& name)` is then passed to `IpoptApplication::OptimizeTNLP(tnlp, alg_builder)`.
  - The headers needed to write one ship in the installed `include/coin-or`. Ran `ls` on the 3.8.1 wheel: `IpAlgBuilder.hpp`, `IpAugSystemSolver.hpp`, `IpStdAugSystemSolver.hpp`, `IpTSymLinearSolver.hpp` and `IpSparseSymLinearSolverInterface.hpp` are present.
  - A ~15-variable, few-hundred-row KKT system could be handled by a subclass of `SparseSymLinearSolverInterface` that densifies the triplets and calls LAPACK `dsytrf`/`dsytrs`. Bunch–Kaufman gives the inertia that `NumberOfNegEVals()` must report.
  - Licence: EPL-2.0 excludes from "Modified Works" files that only "link to, bind by name, or subclass the Program". **Not tried; quality and effort unverified.**
- **MUMPS build options**:
  - (a) coin-or `ThirdParty-Mumps` (autotools, EPL-2.0 harness). `get.Mumps` fetches MUMPS **5.9.1** on the default branch. METIS is optional ("Both Metis 4 and Metis 5 can be used"; `configure.ac` uses `AC_COIN_CHK_LIBHDR(Metis…)`). It adds `-std=legacy` for gfortran ≥ 10.
  - (b) `scivision/mumps`, now `scivision/mumps-superbuild` (MIT, CMake). This is what vcpkg uses.
  - Both need a Fortran 90 compiler.
- **METIS**: coin-or `ThirdParty-Metis` downloads **metis-4.0.3** (`get.Metis`). Its licence: "may not be sold or redistributed without prior approval… Other organizations are allowed to use METIS only for evaluation purposes". METIS 5 (KarypisLab) is Apache-2.0 and in CCI as `metis/5.1.1` and `5.2.1`. For n ≈ 15, MUMPS's AMD/PORD orderings are enough, so METIS can be dropped (Homebrew does this).
- **BLAS/LAPACK**: CCI `openblas` with `build_lapack=True` and no Fortran compiler gives C_LAPACK (section 1.2). This is enough for Ipopt and for MUMPS.
- **This machine**:
  - `/usr/bin/gfortran` is 11.4. `/opt/gcc-15/lib64` has `libquadmath` but no `libgfortran`. The system has `libgfortran.so.5` (from libgfortran5 12.3) and `/usr/lib/gcc/x86_64-linux-gnu/11/libgfortran.a` (ran `ls`).
  - Mixing gfortran-11 objects with a GCC 15 C++ link is normal practice: libgfortran soname 5 has been stable since GCC 8. Not built here.
  - `coinbrew/dist` under `~/Softwares/solvers` has CoinUtils/Clp/Cbc/Osi but no Ipopt (ran `ls`).
- **Windows / macOS**:
  - Ipopt INSTALL: "For Windows, one can compile Ipopt under MSYS2/MinGW"; "To use MSYS2/MinGW to compile Ipopt with native MSVC/Intel compilers, the basic version MSYS2 version without MinGW is sufficient." The closed CCI coin-ipopt recipe sketched MSVC builds through `msys2` + `automake` compile wrappers, but it never built a single configuration.
  - For MUMPS on Windows the choices are MinGW gfortran (vcpkg; switches the port to MinGW and forces DLLs), LLVM flang (conda-forge flang 21, MSVC ABI) or Intel ifx (conda-forge).
  - macOS: "Xcode tools do not come with a Fortran compiler" (Ipopt INSTALL); Homebrew `gcc` supplies gfortran (Homebrew ipopt formula).
  - None of this is modelled by Conan settings (section 1.2).

---

## 3. Binary routes

- **CasADi GitHub releases** (ran `gh api repos/casadi/casadi/releases/tags/{3.7.2,3.8.1}`):
  - No release has a C++-only SDK archive.
  - 3.7.2 (2025-09-16) ships `*-py3x.zip`, `*-matlab2018b.zip` and `*-octave7.3.0.zip` per OS, plus wheels.
  - 3.8.1 (2026-09-16) ships only matlab/octave zips, source and wasm; its wheels are on PyPI.
  - **The pip wheel is the de facto C++ SDK.**
- **CasADi 3.8.1 wheels.** Ran: downloaded the `cp311-abi3` wheels for `win_amd64`, `macosx_11_0_arm64` and `manylinux_2_28_x86_64` and listed them with `zipfile`. All three contain:
  - `casadi/include` (casadi + coin-or with `IpStdCInterface.h` and `IpTNLP.hpp`)
  - `casadi/cmake/casadi-config.cmake` and the targets files
  - `libipopt`, `libcoinmumps`, `libcoinmetis`, `libcasadi_nlpsol_ipopt`, `libgfortran` and `libquadmath`
  - `licenses/` (including **`metis-external/metis-4.0/LICENSE`**)

  Per platform:
  - **Linux** (manylinux_2_28): `libipopt.so.3.14.19`, `libcasadi-tp-openblas`. Built with `GCC: (GNU) 14.2.1 (Red Hat 14.2.1-7)`. Uses the **cxx11 string ABI**: 1341 `std::__cxx11::basic_string` exports.
  - **macOS** arm64: `libipopt.3.dylib`, `libgfortran.5.dylib`, `libgcc_s.1.1.dylib` (Mach-O arm64).
  - **Windows**: built with MinGW. `objdump -p libcasadi.dll` imports `msvcrt.dll`, `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll`, and `strings` shows `GCC: (GNU) 11.2.0`. It also ships `ipopt.lib`, an ar-format import library exporting `CreateIpoptProblem`, `IpoptSolve` and `GetIpoptCurrentIterate` (ran `nm`). The C++ API cannot be used from MSVC, because libstdc++ types cross it. The C API through `ipopt.lib` might link from MSVC (**unverified**).
  - **3.8.1 manylinux2014** wheel (also on PyPI): old ABI. `casadi::Function::Function(std::string const&)` has no `__cxx11`, it was built with GCC 10.2.1, and its `casadi-config.cmake` contains `if(ON AND GNU) target_compile_definitions(casadi::casadi INTERFACE _GLIBCXX_USE_CXX11_ABI=0)`. On this machine pip prefers `manylinux_2_28` (ran `pip debug --verbose`: `cp310-none-manylinux_2_28_x86_64` at rank 112, ahead of `manylinux_2_17` at 123).
  - The **installed pip casadi 3.7.2** is the manylinux2014 build (WHEEL `Tag: cp310-none-manylinux2014_x86_64`): old ABI, GCC 10.2.1, and the same `_GLIBCXX_USE_CXX11_ABI=0` injection.
- **Distro packages** (ran Launchpad `getPublishedSources` and sources.debian.org API):

| Distro | coinor-ipopt | mumps | casadi |
|---|---|---|---|
| Ubuntu 22.04 jammy | 3.11.9-2.2build5 (`coinor-libipopt-dev` depends on `libmumps-seq-dev`) | 5.4.1 | none |
| Ubuntu 24.04 noble | 3.11.9-2.3build4 | 5.6.2 | none |
| Ubuntu 25.04 / 25.10 | 3.14.17 | 5.7.3 | none |
| Ubuntu 26.04 resolute | 3.14.19 | 5.8.2 | 3.7.0+ds2-3 |
| Debian trixie / sid | 3.14.17 / 3.14.20 | | sid: 3.8.1+ds2-1 (`libcasadi-dev`, built against `coinor-libipopt-dev`) |

  Ipopt 3.11.9 (the version for 22.04 and 24.04) is from 2014, and its `IpStdCInterface.h` has no `GetIpoptCurrentIterate` (ran `curl … releases/3.11.9/…/IpStdCInterface.h | grep -c` → 0).

---

## 4. CasADi from C++ (ABI, CMake, plugins)

- **Link tests.** Ran `/opt/gcc-15/bin/g++ -std=c++26` on a small `nlpsol("S","ipopt",…)` program; sources in `packaging/abi_test/src/`.
  - Against **pip 3.7.2** with the default ABI, linking **fails**: `undefined reference to casadi::Matrix<casadi::SXElem>::_sym(std::__cxx11::basic_string…)`, `casadi::nlpsol(std::__cxx11::basic_string…)`.
  - Against **pip 3.7.2** with `-D_GLIBCXX_USE_CXX11_ABI=0`, it links and runs: `x=[4.37331e-09, 1] status=Solve_Succeeded`.
  - Against the **3.8.1 manylinux_2_28** wheel with the default ABI, it links and runs with the same result. libstdc++ resolves to `/opt/gcc-15/lib64` (ran `ldd`).
  - Consequence: going through `find_package(casadi)` on the 3.7.2 / manylinux2014 builds silently compiles your targets with the old ABI. They then fail to link against Conan dependencies built with `compiler.libcxx=libstdc++11` wherever `std::string` crosses (inferred, not built). Use the manylinux_2_28 wheel, or isolate CasADi in one translation unit.
- **IPOPT's C API sidesteps the C++ ABI entirely.** Ran a C-API program (`IpStdCInterface.h`, `hessian_approximation=limited-memory`) with the default GCC 15 ABI against both `libipopt.so.3.14.11` (the 3.7.2 old-ABI build) and `3.14.19`. Both print `status=0 x=(0.000000, 1.000000) f=2.000000`. Gotcha: 3.14 requires a non-null `eval_h` callback even with L-BFGS; otherwise you get `INVALID_STDINTERFACE_NLP … No callback function for evaluating the Hessian`, status -11.
- **CMake config** (3.8.1 wheel): one imported target, `casadi::casadi`, with `INTERFACE_COMPILE_DEFINITIONS "CASADI_SNPRINTF=snprintf;CASADI_WITH_THREADSAFE_SYMBOLICS;CASADI_WITH_THREAD"` and `INTERFACE_LINK_LIBRARIES "dl"`. The location is relative to the config file (`${_IMPORT_PREFIX}/casadi/libcasadi.so.3.7`). There is no target for ipopt itself; the wheel also carries `pkgconfig/ipopt.pc`. On Windows the import library is `libcasadi${CMAKE_IMPORT_LIBRARY_SUFFIX}`, and the wheel ships both `libcasadi.lib` and `libcasadi.dll.a`.
- **Plugin loading.** `casadi/core/casadi_os.cpp` (tag 3.8.1) searches, in order: `CASADI_PLUGIN_SEARCH_PATH`, `GlobalOptions::casadipath`, the `CASADIPATH` env var, a bare name (so the dynamic loader's `RUNPATH`/`LD_LIBRARY_PATH`/`PATH` apply), `PLUGIN_EXTRA_SEARCH_PATH`, then `.`. On Linux it uses `dlopen(RTLD_LAZY|RTLD_LOCAL[|RTLD_DEEPBIND])`; on Windows, `AddDllDirectory` + `LoadLibraryExW`. In my test the bare-name lookup succeeded because of `-Wl,-rpath` to the wheel dir. Static builds must call `casadi_load_<type>_<name>()` (CCI PR #31023 test_package).
- **ABI and compiler mix** (Linux): the 3.8.1 manylinux_2_28 binaries (GCC 14.2.1, cxx11) work with GCC 15 / C++26 in this test. Upgrading the toolchain does not break this, but libstdc++ must be at least the version the wheel needs; `/opt/gcc-15` satisfies that. **Windows**: MinGW-built C++ API, so it is unusable from MSVC.

---

## 5. Licences

| Component | Licence (source) | Obligation when you ship binaries of geomsolver |
|---|---|---|
| IPOPT | EPL-2.0 (`LICENSE` at `releases/3.14.20`) | §3.1(a): "the Program must also be made available as Source Code… [and] accompany the Program with a statement that the Source Code… is available". Include the EPL text. Linking or subclassing is not a "Modified Work". **No Exhibit A secondary-licence notice in the shipped headers** (ran `grep -rl "Secondary Licen"` over `include/coin-or`: 0 files; `IpTNLP.hpp` says "published under the Eclipse Public License"). Without that notice, the EPL-2.0 secondary-licence (GPL) option is not available. The usual reading, which matches the notes, is that IPOPT then cannot be combined with GPL code such as libslvs or a GPL geomsolver (not re-read here). |
| CasADi | LGPL-3.0-or-later (headers: "either version 3 of the License, or (at your option) any later version"; PyPI classifier LGPLv3+; CCI recipe review) | Dynamic linking: ship the licence and allow library replacement. Static linking (the CCI recipe default is `shared=False`): you must enable relinking, e.g. by providing object files (LGPL-3 §4; not re-read here). |
| MUMPS | CeCILL-C (bundled `licenses/mumps-external/MUMPS/LICENSE`: "released under the CeCILL-C license", with BSD-3 parts and PORD separately) | LGPL-like and file-level. Keep the notices. |
| ThirdParty-Mumps / ThirdParty-Metis harness | EPL-2.0 / EPL-1.0 (GitHub metadata) | Build scripts only. |
| **METIS 4.0.3** (coin-or `libcoinmetis`, bundled in every CasADi wheel) | Not open source: "may not be sold or redistributed without prior approval" (`licenses/metis-external/metis-4.0/LICENSE`). Ran `nm -D libcoinmetis.so.2`: METIS 4-style entry points, no `METIS_SetDefaultOptions` | **Do not redistribute.** The notes' licence table does not list METIS. |
| METIS 5.x | Apache-2.0 (KarypisLab/METIS `LICENSE`) | Fine; optional for MUMPS. |
| OpenBLAS | BSD-3 (bundled licence) | Keep the notice. |
| libgfortran | GPL-3 with GCC Runtime Library Exception 3.1 (`libgfortran/runtime/main.c` header) | Fine to ship. |
| libquadmath | **LGPL-2.1** (`libquadmath/COPYING.LIB`) | Ship as a shared library with its notice. The notes omit it. |
| SPRAL | BSD-3 (`LICENCE`) | Also needs hwloc. |
| BQPD (bundled in the 3.8.1 wheel) | BSD-3 ("Copyright (c) 2025, Argonne National Laboratory", `licenses/BQPD/LICENSE`) | Uno's docs still say "academic licence"; unresolved. |

**Your project.** There is no `LICENSE` file, so by default nobody else has any rights to it. `conanfile.py` disables NLopt's Luksan code to keep NLopt MIT rather than LGPL, which shows you care about licences. IPOPT (EPL-2.0) together with CasADi (LGPL-3) would be your first weak-copyleft runtime dependencies.
- If you stay permissive (MIT/BSD/Apache) or LGPL, both are compatible as long as you meet the source-availability and notice obligations above.
- A GPL licence for geomsolver would conflict with linking IPOPT, under the usual reading of EPL-2.0 without the secondary-licence notice (not re-read here).

---

## 6. Alternatives without Fortran (n ≈ 15, m up to a few hundred, dense, exact Jacobians, no Hessians)

| Option | In CCI? | Fits this problem? (primary source) |
|---|---|---|
| **NLopt** (already a dependency) | nlopt/2.11.0 | "only MMA and SLSQP support arbitrary nonlinear inequality constraints… only SLSQP supports nonlinear equality constraints". AUGLAG handles "nonlinear inequality/equality constraints" around a subsidiary algorithm (https://nlopt.readthedocs.io/en/latest/NLopt_Algorithms/). Your build disables Luksan, so **LD_LBFGS cannot be AUGLAG's subsidiary**; use LD_MMA, LD_CCSAQ or LD_SLSQP. Zero packaging cost. MMA/CCSAQ have conservative convex approximations and could be a more robust phase 1 than SLSQP (unverified). |
| **CasADi core** (once #31023 merges) | pending | Core plugins only: `casadi/solvers` at tag 3.8.1 has 21 plugin headers, including `sqpmethod`, `feasiblesqpmethod`, `qrsqp`, `scpgen`, `qrqp`, `ipqp`, `linsol_ldl` and `linsol_qr` (ran `gh api repos/casadi/casadi/contents/casadi/solvers?ref=3.8.1`). No IPOPT. LGPL-3.0-or-later. You would re-express the model in CasADi symbolics, duplicating your compiler, `exec.hpp` and `Dual<N>`. NLP quality here is unverified. |
| **IPOPT + custom dense linear solver** | no (local recipe) | See section 2. The only Fortran-free IPOPT. Needs CCI openblas (C_LAPACK) plus one local autotools recipe with `--disable-f77 --without-mumps --without-hsl --without-spral --without-asl`, plus about 200–400 lines of solver code (estimate). Use L-BFGS (`hessian_approximation=limited-memory`), since you have no Hessians. |
| **Uno** (MIT, C++, v2.9.0 2026-09-08) | no | `QP_solver: BQPD, HiGHS`; `linear_solver: MA57, MA27, MA86, MUMPS, SSIDS`; `hessian_model: exact, LBFGS, LSR1, identity, zero` (docs/options.md). The `filtersqp` preset needs only a QP solver; the `ipopt` preset needs a linear solver (docs/presets.md). **Uno + HiGHS (CCI, MIT) + LBFGS is Fortran-free**, but needs a local CMake recipe. How robust the HiGHS-QP path is here is unverified. Binaries ship for Linux, macOS and Windows (MinGW). |
| **sleqp** (LGPL-3.0, C) | no | Needs an LP solver (Gurobi, HiGHS or SoPlex), plus a factorization from CHOLMOD, Umfpack, MUMPS, MA27/57/86/97 or SPQR, plus trlib (README). SuiteSparse and trlib are not in CCI. Last push 2023-12-18. Not worth it. |
| **PRIMA** (BSD-3) | no | Derivative-free; GitHub languages show 6.0 MB Fortran against 66 KB C. Throws away your exact Jacobians. No. |
| **Ceres** | ceres-solver/2.2.0 | Least squares with bounds only, no general constraints (site unreachable from here, ECONNREFUSED; from its docs, not re-checked). Suits the feasibility LM solver in the notes, not the design NLP. |
| **ensmallen** (BSD-3) | ensmallen/2.21.0 | `AugLagrangian` "for differentiable constrained functions… Internally, the optimizer uses L-BFGS" (doc/optimizers.md). Penalty formulation; it is built on Armadillo (not re-checked here). Weaker than NLopt AUGLAG + SLSQP for this use. |
| **pagmo2** (LGPL-3 / GPL-3 dual: `COPYING.lgpl3`, `COPYING.gpl3`) | pagmo2/2.19.0 | Global and multistart meta-heuristics. Its local NLP wraps NLopt/Ipopt, and CCI blocks `with_ipopt`. Adds nothing over your TBB multistart. |
| **qpOASES** (LGPL-2.1) | qpoases/3.2.1 | Dense active-set QP: the right size for an SQP subproblem with n ≈ 15 and m ≈ hundreds, with warm starts. A building block for a custom SQP. |
| **HiGHS** (MIT) | highs/1.15.1 | LP and **convex** QP. Usable as an SQP subproblem solver with a positive-definite BFGS Hessian. |
| **OSQP** (Apache-2.0) | osqp/0.6.3 (0.6.x API) | ADMM; low-accuracy active sets unless polished. Poor fit for SQP subproblems needing exact multipliers. |
| **proxsuite** (BSD-2) | no | Dense/sparse QP; not in CCI. |
| **Eigen-based custom SQP** | eigen/5.0.1 | Fully under your control; no external solver. You must write the globalization (merit function or filter), BFGS damping and infeasibility handling yourself. This rebuilds SLSQP plus a restoration phase. Large effort (estimate: weeks), with a risk of being no better than SLSQP. |

---

## Summary table

| Route | Linux / Windows / macOS | Needs Fortran | Effort to integrate in this Conan build | Licence notes | Verdict |
|---|---|---|---|---|---|
| Wait for CCI | casadi core: L/W/M (czoido tested) / ipopt: none | no / — | Zero once #31023 merges | LGPL-3.0+ | **Gives CasADi without IPOPT.** IPOPT blocked by CCI's lack of Fortran (#23334 closed; #27778 and #29446 open); no timeline. |
| Local recipes `coinmumps` + `ipopt` (tttapa template) + CCI `openblas` (+ optional CCI `metis/5.2.1`), system gfortran via scoped `compiler_executables` | L: yes (gfortran 11 here). M: Homebrew gcc (unverified). W: MinGW gfortran or flang/ifx (hard, unverified) | **yes** (MUMPS) | Two recipes of about 100 lines, `conan create` or `conan export` into the local cache, and one profile line. First build compiles openblas, MUMPS and Ipopt (minutes). About 1 day on Linux (estimate, not built here). You maintain them. | EPL-2.0, CeCILL-C, BSD-3, libgfortran runtime exception, libquadmath LGPL-2.1. Avoid ThirdParty-Metis (METIS 4). | **Best real IPOPT for a Linux dev box.** Shipping to Windows is expensive. |
| IPOPT without a sparse solver + custom dense KKT solver (`linear_solver=custom`) + CCI openblas C_LAPACK | L/W/M in principle (Windows: autotools via msys2, sketched in the closed CCI recipe but never built) | **no** | One autotools recipe plus about 200–400 lines of expert-API solver code (estimate, untried) | EPL-2.0 only (+ BSD-3). A subclass is not a Modified Work. | **The only Fortran-free IPOPT.** Worth it only after a prototype proves IPOPT helps. |
| Vendor CasADi 3.8.1 manylinux_2_28 wheel (prebuilt "binary" recipe, or `CMAKE_PREFIX_PATH`), C++ API or IPOPT C API | L x86_64: **tested** with GCC 15 / C++26. M: binaries present, untested. W: MinGW build, C++ API unusable from MSVC; C API via `ipopt.lib` unverified | no (prebuilt) | About 50 lines of download-and-copy recipe, plus a plugin path via rpath or `CASADIPATH`. A few hours (estimate). | Carries **METIS 4 (no redistribution without approval)**; LGPL-3 plus EPL-2.0 plus the rest. | **Good for local experiments and A/B tests; do not ship.** |
| pip casadi 3.7.2 (already installed) | L only | no | Zero; but its CMake config injects `_GLIBCXX_USE_CXX11_ABI=0` | same as above | **Use from Python, or its C API only.** The C++ API needs the old ABI (link test failed with the default ABI). |
| Ubuntu `coinor-libipopt-dev` | 22.04 / 24.04: 3.11.9 | no (prebuilt) | apt; outside Conan | EPL | **Too old** (2014, no 3.14 C API). 26.04 has 3.14.19 plus casadi 3.7.0. |
| conda-forge ipopt 3.14.20 / casadi 3.7.2 | L/W/M (W: clang/flang builds) | no (prebuilt) | A second package manager next to Conan | as above (MUMPS without METIS 4, unverified) | Good reference or CI source, awkward to mix with Conan. |
| vcpkg coin-or-ipopt + mumps-solver | L/M; W via MinGW gfortran, DLL only | yes (at build) | Switching package managers | as above | No, unless you leave Conan. |
| NLopt AUGLAG/MMA/CCSAQ/SLSQP | L/W/M | no | **Zero** (already a dependency; Luksan off excludes LD_LBFGS) | MIT (Luksan off) | **Try first for phase-1 robustness.** |
| Uno + HiGHS (filtersqp, LBFGS) | L/W/M (binaries for all three) | no | One local CMake recipe; HiGHS from CCI. About 1–2 days (estimate). | MIT + MIT | Most interesting Fortran-free modern SQP. Robustness on this problem unverified. |
| Custom SQP on Eigen + qpOASES/HiGHS | L/W/M | no | CCI deps only; weeks of solver work (estimate) | LGPL-2.1 (qpOASES) or MIT (HiGHS) | Only if both IPOPT and Uno disappoint. |
| sleqp / PRIMA / Ceres / ensmallen / pagmo2 | — | sleqp no (needs SuiteSparse and trlib, not in CCI); PRIMA yes | — | sleqp LGPL-3; PRIMA BSD-3 | Not suitable (see section 6). |

---

## Commands and artifacts (scratch: `packaging/`)

- CCI: `gh api -X GET search/issues …`, `gh api repos/conan-io/conan-center-index/pulls/{22466,22467,22468,23334,23798,23417,29446,31023}`, PR heads' recipes in `cci_prs/`, master recipes in `cci/` (openblas, pagmo2, gcc, gfortran, …). c3i summary for #22467 fetched with curl.
- Conan with an isolated `CONAN_HOME=packaging/conan_home`: `conan search <name> -r conancenter`, `conan list "gcc/15.2.0:*"`, `conan list "openblas/0.3.30:*"`.
- vcpkg ports: `gh api repos/microsoft/vcpkg/contents/ports/{casadi,coin-or-ipopt,mumps-solver,vcpkg-gfortran}`, and the commit history of each.
- conda-forge feedstocks in `condaforge/`; anaconda.org API.
- Wheels in `dl_wheels/` (sha256 recorded in the shell log); partial extracts in `x_win/`, `x_mac/`, `x_lin/`, `x_m2014/`; full Linux extract in `x_lin_full/`.
- Link and run tests: `abi_test/src/t.cpp` (CasADi C++ API) and `abi_test/src/c_api.cpp` (IPOPT C API); binaries `t372_old`, `t381`, `capi372`, `capi381`.
- Ipopt sources (`ipopt_src/`): `IpAlgBuilder.{cpp,hpp}`, `IpIpoptApplication.hpp`, `IpSparseSymLinearSolverInterface.hpp` at `releases/3.14.20`; `licences/ipopt_LICENSE`.
- CasADi plugin loader: `casadi_src/casadi_os.cpp` and `plugin_interface.hpp` at tag 3.8.1.
- Community recipes in `community/` (tttapa, RoboConan).