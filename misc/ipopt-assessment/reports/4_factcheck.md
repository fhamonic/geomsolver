/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/FACTCHECK.md

# Fact-check of RESEARCH.md (IPOPT / CasADi packaging)

Checked 2026-10-12. Every verdict comes from a command I ran in this session or a primary source I fetched. Artifacts are in `scratchpad/factcheck/`. That folder holds the PR-head recipes (`cci31023/`, `prs/`, `tttapa/`, `robo/`), the vcpkg ports, the conda-forge recipes, Ipopt sources, wheel extracts, link tests (`abi/`) and the Fortran mix test (`fmix/`).

Verdicts:
- **confirmed**: matches the primary source or the command output.
- **refuted**: the primary source says otherwise.
- **qualified**: true as written, but missing a fact that changes how it reads.
- **unverified**: I could not check it.

Commands:
- `gh` means `gh api` with the user's token.
- `conan` ran with `CONAN_HOME=scratchpad/factcheck/conan_home` (Conan 2.33.0).
- Wheel files were checked against their sha256 digests on GitHub and PyPI. For example, `…abi3-manylinux_2_28_x86_64.whl` has sha256 `99c32594…` in both places, so the copies in `packaging/dl_wheels/` are the genuine files.

## 1. ConanCenter (CCI)

| Claim | Verdict | Evidence |
|---|---|---|
| Recipes for casadi and coin-ipopt are not on conancenter. `gfortran/10.2` exists but is deprecated in favour of gcc. | confirmed | `conan search casadi\|coin-ipopt -r conancenter` → "Recipe … not found". `gfortran` → `gfortran/10.2`. `recipes/gfortran/all/conanfile.py:19 deprecated = "gcc"`. |
| Issue #3657 `[request] coin-ipopt/3.13.3` is open, created 2020-11-22. | confirmed | `gh repos/conan-io/conan-center-index/issues/3657` → open, 2020-11-22T19:03:39Z. |
| PR #22466 (coin-mumps, valgur) was closed unmerged on 2025-03-17 by `conan-center-build-service[bot]`. The body says it "Requires GFortran … #23334, #18980". The recipe requires openmpi/4.1.6 and gcc/13.2.0, uses `build_requires("gcc/<host_version>")` and metis/5.2.1. CI failed with "Unable to find 'openmpi/4.1.6'". | confirmed | PR API: closed 2025-03-17T09:35:08Z, closed_by the bot, locked "off-topic". Recipe at `870498a`, lines 61/65/68/78. Bot comment: `ERROR: Unable to find 'openmpi/4.1.6' in remotes`. |
| PR #22467 (coin-ipopt) has `with_mumps=False # TODO`, and `validate()` raises "At least one solver is required". The CI said "All green" while every configuration was INVALID_CONFIGURATION, so nothing was built. | confirmed | Recipe at `016a224`, lines 40 and 70. `curl …/misc-v2/logs/pr/22467/1/coin-ipopt/3.14.13//summary.json` (Conan v2 pipeline) → 10 configurations, all `INVALID_CONFIGURATION`. The bot's "Conan v1 pipeline :heavy_check_mark: All green in build 1" links `…/misc/logs/pr/22467/1/coin-ipopt/3.14.13//summary.json` → 22 configurations, all `INVALID_CONFIGURATION`. Both pipelines skipped everything. |
| PR #22468 (casadi, valgur) has IPOPT as an option and was closed unmerged on 2025-03-17. | confirmed | Recipe at `79b1630`, line 66: `"with_ipopt": False, # TODO: enable after merging …/22467`. |
| PR #23334 (gfortran) was closed by its author on 2025-01-09. Quotes: "There is currently no usable Fortran compiler available on CCI" and "Should be a part of the CI image or installed on the consumer system, really." | confirmed | PR body and last comment. closed_by=`valgur`. |
| PR #23798 (lapack) was closed on 2025-01-09 with "Needs Fortran support." | confirmed; **qualified** | The author (valgur) wrote that comment and closed the PR himself. It was not a CCI reviewer decision. |
| PR #23417 (f2c) was closed on 2025-02-10 with "Too experimental and heavy patching for CCI." | confirmed; qualified | Also closed by its author, valgur. |
| "The 2024 attempts at gfortran, lapack, coin-mumps and coin-ipopt were all closed unmerged." | confirmed; **qualified** | None was turned down in a CCI technical review. gfortran, lapack and f2c were withdrawn by their author. coin-mumps, coin-ipopt and casadi went in a bot sweep. |
| Bulk sweep: 224 unmerged PRs closed on 2025-03-17, 222 of them by valgur. | confirmed | `gh search/issues q="… is:pr is:unmerged closed:2025-03-17"` → 224. Adding `author:valgur` → 222. |
| A CLA comment from 2025-03-09 says "Martin Valgur seems not to be a GitHub user". | confirmed; qualified | The comment text is as quoted. However, `gh users/valgur` shows the account has existed since 2012-05-25. The bot message is about commit-email linkage, not a missing account. Whether it triggered the sweep is **unverified**. |
| Issue #24327 listed casadi #22468, lapack #23798 and coin-ipopt #22467. | confirmed | Issue body rows: `casadi\|#22468\|1600`, `lapack\|#23798\|1400`, `coin-ipopt\|#22467\|1300`. |
| PR #31023 (casadi/3.8.1, jgillis, upstream maintainer) is open. czoido approved it on 2026-10-08 and `mergeable_state=blocked`. | confirmed | Reviews: czoido COMMENTED 10-02, DISMISSED 10-08, APPROVED 2026-10-08T16:00 on `d5480be` (the current head). Check runs Linux, Windows, macOS, linter and Validate PR are all `success`. CLA signed. |
| "…approved and close to merging" | **unverified** | CI is green and the CLA is signed, so `blocked` comes from a branch-protection rule I cannot see (possibly a second approval). There is no evidence on timing. |
| #31023 packages the core plus first-party plugins, LGPL-3.0-or-later, `shared=False` by default. | confirmed; qualified | At head `d5480be`: `license = "LGPL-3.0-or-later"`, `"shared": False`, `_first_party_plugins` lists 21 plugins. The PR body still says "LGPL-3.0-only AND BSD-2-Clause". The head requires `fmi2/2.0.4` and `fmi3/3.0.2`, both on conancenter. |
| #31023 prepends `CASADIPATH` in `runenv_info`. | **refuted** (at head) | Only the PR body says this. Head `conanfile.py` has no `env` at all (`grep -i env` matches only `WITH_SELFCONTAINED`). Shared consumers find plugins through the `WITH_SELFCONTAINED` `$ORIGIN` rpath and the bare-name `dlopen`, not through an environment variable. |
| #31023 body: "IPOPT is blocked on a `coin-ipopt` recipe (#22467 was closed)". | confirmed | PR body. |
| czoido tested shared and static builds on macOS armv8, Linux aarch64 gcc 10 and Windows ARM64 msvc 195. | confirmed | Comment of 2026-10-08: "macOS armv8, apple-clang 21 / Linux aarch64, gcc 10, glibc 2.31 / Windows ARM64, msvc 195". |
| The head's test_package only checks a linear interpolant, although the body mentions sqpmethod/qrqp. | confirmed | `test_package.cpp` at `d5480be` calls only `interpolant("f","linear",…)`. |
| CCI has no SPRAL attempt. | confirmed | `gh search/issues q="spral repo:conan-io/conan-center-index"` → total_count 0. |

## 2. Fortran in Conan and CCI

| Claim | Verdict | Evidence |
|---|---|---|
| Conan 2.33 accepts a `fortran` key in `tools.build:compiler_executables`. CMakeToolchain maps it to `CMAKE_Fortran_COMPILER` and AutotoolsToolchain to `FC`. Settings have no Fortran model. | confirmed | `conan/internal/model/conf.py:167`, `tools/cmake/toolchain/blocks.py:984`, `tools/gnu/autotoolstoolchain.py:300`. `grep -ci fortran internal/default_settings.py` → 0. |
| memsharded (conan#18008, 2025-06-24) said openblas "do[es] not properly model the fortran usage" and that a `with_fortran` option is needed. | confirmed | Comment of 2025-06-24T13:34:17Z. conan#18008 itself is closed as not-a-bug. |
| CCI #27778 is open. PR #29446 (`openblas: add with_fortran`, opened 2026-01-27) has no review, and its author pinged on 2026-04-06. | confirmed | #27778 open. The #29446 reviews list is empty. Its only comment is tttapa, 2026-04-06: "Any feedback? …". |
| Code search finds 18 conanfiles mentioning Fortran, mostly to turn it off (nlopt, highs, hdf5, openmpi `fortran="no"`, …). | confirmed | `gh search/code` → 18 paths. `NLOPT_FORTRAN=False`, `FORTRAN=False`, `HDF5_BUILD_FORTRAN=False`, openmpi default `"no"`. |
| Only the `gcc` recipe builds gfortran (`--enable-languages=c,c++,fortran`, defines `FC`). Its `validate()` raises on Windows. `gcc/15.2.0` has no binaries. | confirmed | `gcc/all/conanfile.py` lines 58–60, 85, 171. `conan list "gcc/15.2.0:*"` → "No packages found for this revision". |
| No CCI library recipe requires gfortran. | confirmed | `gh search/code gfortran path:recipes` returns only the gcc and gfortran recipes plus openblas. openblas adds `gfortran` to `system_libs` only when a Fortran compiler is configured. |
| CCI openblas builds C_LAPACK when no Fortran compiler is set. `build_lapack` defaults to True. Upstream 0.3.21 added the "f2c-converted copy of LAPACK 3.9.0". | confirmed | `openblas/all/conanfile.py` lines 84, 156–159 and 220–221. OpenBLAS `Changelog.txt@v0.3.30`, section "Version 0.3.21". |
| openblas/0.3.30 binaries exist only for gcc 11 and 13. | confirmed; qualified | `conan list "openblas/0.3.30:*"`: Linux gcc 11 and 13, plus apple-clang 13 and msvc 192/194. There is no gcc 15 binary, so it builds from source. |
| Trap: setting `fortran` profile-wide flips openblas to Fortran LAPACK without changing package_id. | confirmed | Code at lines 156 and 220. #27778: "Recipe doesn't model the fortran effect on binary compatibility". |
| The pagmo2 `with_ipopt` option is a placeholder. | confirmed | `pagmo2/all/conanfile.py` lines 83–84 raise "ipopt recipe not available yet in CCI". |

## 3. Community recipes

| Claim | Verdict | Evidence |
|---|---|---|
| tttapa: `ipopt` 3.14.16 and `coinmumps` 3.0.7 (MUMPS 5.7.1), Conan 2, about 100 lines each, openblas/0.3.30, no MPI, no METIS, `system_libs` includes gfortran and quadmath, a static-Fortran option, no licence, ipopt last touched 2025-09-01. | confirmed | Files are 103 and 101 lines. `conandata.yml` points to `MUMPS_5.7.1.tar.gz`. Uses `mumps_mpi.patch` (sequential build). `static_fortran_libs` option. Repo `license` is none. Last commit on `recipes/ipopt` is 2025-09-01. |
| (Missing caveat) The coinmumps recipe does not pin METIS off. | unverified risk | ThirdParty-Mumps `configure.ac:128` probes `-lmetis` by default (`AC_COIN_CHK_LIBHDR(Metis…)`). On this machine, where a system METIS is installed, the build could link it without Conan knowing. Not built. |
| RoboConan: MIT, active (pushed 2026-10-11). Has coin-ipopt (`with_spral=True`), coin-mumps (needs openmpi, `lapack/latest`, metis, and a Fortran entry in `compiler_executables` or it raises), spral, coin-hsl, a `fortran` package and casadi. | confirmed | Recipes fetched. coin-mumps lines 49–63. The casadi recipe has `"with_ipopt": True` (default) and `requires("coin-ipopt/[^3.14.13]")`. |
| "You would have to adopt the fork's remote." | **refuted** | README: "recipes-only … **No pre-built binaries will be provided by design**", "not compatible with ConanCenter", "currently a placeholder while active development takes place at github.com/valgur/conan-center-index". All recent commits are by Martin Valgur, so this is the same author continuing the closed CCI work. There is no binary remote. You would export the recipes from a git checkout and build everything from source. |
| sintef-ocean is Conan 1 only (last commit 2022-01-10). rhololkeolke's last push was 2019. | confirmed | `from conans import …`. Last commit 2022-01-10T23:04. rhololkeolke pushed_at 2019-07-27. |

## 4. vcpkg, conda-forge, Homebrew

| Claim | Verdict | Evidence |
|---|---|---|
| vcpkg `casadi` 3.8.1 is core only, with FMI off and `WITH_SELFCONTAINED=OFF`. | confirmed | `ports/casadi/vcpkg.json` has no dependency beyond vcpkg-cmake. Portfile sets `-DWITH_SELFCONTAINED=OFF -DWITH_FMI2=OFF -DWITH_FMI3=OFF`. vcpkg labels the licence `LGPL-3.0-only`. |
| vcpkg `coin-or-ipopt` (version-date 2023-02-01) gained a default `mumps` feature on 2026-09-25, along with `mumps-solver` 5.9.1. The portfile comment is as quoted. | confirmed | Commit 2026-09-25 "[mumps-solver, coin-or-ipopt] Add mumps-solver… (#54077)". Comment: "LAPACK alone is dense only and leaves the solver unusable at run time. MUMPS is the only sparse solver Ipopt supports that is redistributable". |
| `mumps-solver` uses the scivision CMake build and `vcpkg_find_fortran`. On Windows this means MinGW gfortran and forced dynamic linkage. | confirmed | Portfile lines 2–9 and 46–50. Supports `… & !(windows & !mingw & static)`. `scivision/mumps` now redirects to `scivision/mumps-superbuild` (MIT). |
| vcpkg has no `ipopt`, `mumps` or `spral` ports. | confirmed | `gh repos/microsoft/vcpkg/contents/ports/{ipopt,mumps,spral}` → 404. |
| conda-forge `ipopt` 3.14.20 is on linux-64/aarch64/ppc64le, osx-64/arm64 and win-64. Host deps are `mumps-seq`, `libspral # [linux]` and `ampl-asl`. Windows uses `autotools_clang_conda`. | confirmed | anaconda.org API and `ipopt-feedstock/recipe/meta.yaml`. Side note: the feedstock declares `license: EPL-1.0`, but upstream is EPL-2.0. |
| `mumps-seq` 5.8.2 is built on Windows with flang 21 ("Pinning flang to 21 because of compilation error with 23") or ifx. | confirmed | `mumps-feedstock/recipe/conda_build_config.yaml`. |
| conda-forge MUMPS has no METIS 4 (the report marked this unverified). | confirmed | `mumps-feedstock/recipe/recipe.yaml`: `metis` on non-Windows hosts. The conda-forge `metis` package is 5.2.1, Apache-2.0. |
| conda-forge `casadi` 3.7.2 builds against ipopt and mumps-seq, and its test runs `cmake-package-check casadi --targets casadi::casadi`. There is no `libcasadi` package. | confirmed; qualified | `casadi-feedstock/recipe/meta.yaml`. API: `"libcasadi" could not be found`. The package has a `run: python` requirement, so C++ users also get Python. |
| Homebrew `ipopt` 3.14.20 has `depends_on "gcc" # for gfortran` and a MUMPS 5.6.2 resource without METIS. There is no `casadi` formula, although #31023 says "packaged for Homebrew". | confirmed | formulae.brew.sh `ipopt.json` and `Formula/i/ipopt.rb`. `casadi.json` → 404, and `Formula/c/casadi.rb` → 404. A third-party tap is possible but not checked. |

## 5. Building IPOPT from source

| Claim | Verdict | Evidence |
|---|---|---|
| Ipopt 3.14.20 was released on 2026-08-27. It builds with autotools only ("No, it doesn't", #649). The changelog has `--disable-f77` and "Lapack is no longer optional". | confirmed | `gh repos/coin-or/Ipopt/releases`. Issue #649 comment by svigerske. `ChangeLog.md` lines 555–557. |
| The linear-solver interfaces are HSL MA27/57/77/86/97, MUMPS, Pardiso, PardisoMKL, SPRAL, WSMP and iterative WSMP. There is no LAPACK dense solver. | confirmed | Listing of `src/Algorithm/LinearSolvers@releases/3.14.20`. |
| MUMPS needs a Fortran 90 compiler. SPRAL is BSD-3, 1.76 MB Fortran against 0.40 MB C++, needs METIS and hwloc, builds with Meson, and needs the OMP env vars. | confirmed | ThirdParty-Mumps README line 11. Ipopt `install.dox` line 287. `gh repos/ralna/spral/languages`. SPRAL README lines 34–49 and LICENCE (STFC, BSD-3 text). |
| HSL: "freely available for personal commercial or non-commercial usage (Coin-HSL Archive)" / "academic use only". | confirmed; **qualified** | The next sentence in `install.dox` says "you may not redistribute these codes in either source or binary form without purchasing a licence". The report also leaves out that Ipopt can **load HSL at run time** (`hsllib`, `install.dox` §LINEARSOLVERLOADER). |
| WSMP is a 90-day trial and Pardiso-project is licensed. | unverified | Not checked. |
| No open-source, Fortran-free sparse solver exists for IPOPT. | confirmed; qualified | True for open source. The report does not explore PardisoMKL, which is proprietary and prebuilt, so no Fortran compiler is needed. |
| `"custom"` is always registered: "use custom linear solver (expert use)". | confirmed | `IpAlgBuilder.cpp:204–205`. |
| "…and it becomes the default when no solver is linked." | **refuted** | The fall-through tests `availablesolvers & IPOPTLINEARSOLVER_MA27` (lines 249–255). `IpLinearSolvers.c` adds MA27 whenever `IPOPT_HAS_LINEARSOLVERLOADER`, and `configure.ac:478` defaults `use_linearsolverloader=yes`. So the default is `ma27`, which then fails at run time without libhsl. `custom` becomes the default only with `--disable-linear-solver-loader`. Setting `linear_solver=custom` explicitly still works. |
| The `AlgorithmBuilder(custom_solver, name)` object "is passed to `IpoptApplication::OptimizeTNLP(tnlp, alg_builder)`". | **refuted** (detail) | No such overload exists. At 3.14.20, `OptimizeTNLP` takes only `tnlp`. The builder goes through `OptimizeNLP(const SmartPtr<NLP>&, SmartPtr<AlgorithmBuilder>&)`, so you wrap your TNLP in a `TNLPAdapter` yourself, as `IpIpoptApplication.cpp:472` does. The custom-solver route needs the C++ API; the C API cannot do it. The constructor arguments are confirmed (`IpAlgBuilder.hpp:56–57`, name "since 3.14.12"). |
| The expert headers ship in the wheel: `IpAlgBuilder`, `IpAugSystemSolver`, `IpStdAugSystemSolver`, `IpTSymLinearSolver`, `IpSparseSymLinearSolverInterface` (`NumberOfNegEVals()`). | confirmed | Zip listing of the 3.8.1 manylinux_2_28 wheel. `IpSparseSymLinearSolverInterface.hpp:207`. |
| EPL-2.0 excludes from Modified Works code that only links, binds by name or subclasses. | confirmed | Ipopt `LICENSE`, line 47. |
| ThirdParty-Mumps fetches MUMPS 5.9.1, METIS is optional ("Both Metis 4 and Metis 5 can be used"), and it adds `-std=legacy` for gfortran 10 and later. | confirmed; minor **refuted** | `get.Mumps`: `mumps_ver=5.9.1`. README line 21. `configure.ac:213` adds `-std=legacy` for **any** gfortran, with no version test. |
| ThirdParty-Metis downloads metis-4.0.3. METIS 4 licence: "may not be sold or redistributed without prior approval… Other organizations are allowed to use METIS only for evaluation purposes". METIS 5 is Apache-2.0. | confirmed | `get.Metis` lines 24–33. The wheel's `licenses/metis-external/metis-4.0/LICENSE`. KarypisLab/METIS `LICENSE` line 7. |
| This machine: `/usr/bin/gfortran` 11.4. `/opt/gcc-15/lib64` has libquadmath but no libgfortran, and `/opt/gcc-15/bin` has no gfortran. libgfortran5 is 12.3. `libgfortran.a` is in `/usr/lib/gcc/x86_64-linux-gnu/11/`. | confirmed | `ls` and `dpkg-query` (libgfortran5 12.3.0-1ubuntu1~22.04.3, gfortran-11 11.4.0). |
| "Mixing gfortran-11 objects with a GCC 15 C++ link is normal practice … not built here." | **confirmed by test**; qualified | `fmix/`: a gfortran-11 `bind(C)` subroutine using `real(16)` and formatted I/O, linked into a g++-15 `-std=c++26` program. It runs and prints `6.5`, both with shared `libgfortran.so.5` and with static `libgfortran.a`. The soname is stable: `libgfortran/libtool-version` is `5:0:0` at gcc-8.1.0 and at gcc-15.1.0. **Gotcha:** plain `-lgfortran` fails with g++-15 ("cannot find -lgfortran"), because the `.so` symlink lives only in `/usr/lib/gcc/x86_64-linux-gnu/11/`. The tttapa recipes' `system_libs=["gfortran"]` therefore needs `-L/usr/lib/gcc/x86_64-linux-gnu/11`, or `-l:libgfortran.so.5`, in the consumer profile. |
| Windows build (MSYS2/MinGW, or MSYS2 with MSVC compilers). macOS: Xcode tools "do not come with a Fortran compiler". | confirmed | `install.dox` lines 64–95 and around line 51. |

## 6. Binary routes

| Claim | Verdict | Evidence |
|---|---|---|
| 3.7.2 (2025-09-16) ships per-OS `*-pyXX.zip`, matlab and octave zips, plus wheels. | confirmed | `gh repos/casadi/casadi/releases/tags/3.7.2` (124 assets). |
| 3.8.1 (2026-09-16) "ships only matlab/octave zips, source and wasm; its wheels are on PyPI". | **refuted** | The 3.8.1 release has 49 assets, including 33 `.whl` files (cp37–cp310 `none`, cp311 `abi3`; manylinux2014 and manylinux_2_28, macOS and win_amd64). The digests are identical to PyPI's. Only the per-OS `pyXX.zip` archives were dropped. |
| No C++-only SDK archive is released. | confirmed (for 3.7.2 and 3.8.1) | Asset lists. Older releases were not checked. |
| All three 3.8.1 wheels (linux 2_28, win, mac arm64) contain the include dir, the CMake config, libipopt, libcoinmumps, libcoinmetis, libcasadi_nlpsol_ipopt, libgfortran, libquadmath and the licences, including METIS 4. | confirmed | `factcheck/scripts/whl_ls.py`. The licences are under `casadi/include/licenses/`. The macOS wheel has no `libspral.a` and no `casadi-tp-openblas`. |
| Linux wheel: `libipopt.so.3.14.19`, built with GCC 14.2.1 (Red Hat 14.2.1-7), cxx11 ABI with 1341 `std::__cxx11::basic_string` exports. | confirmed | `strings`, then `nm -D --defined-only \| c++filt \| grep -c` → 1341. SONAME is `libcasadi.so.3.7`, RUNPATH `$ORIGIN`. |
| macOS wheel: `libipopt.3.dylib`, `libgfortran.5.dylib`, `libgcc_s.1.1.dylib`. | confirmed (present) | Zip listing. Not run, Mach-O type not re-checked. |
| Windows wheel is MinGW-built: imports `msvcrt`, `libstdc++-6`, `libgcc_s_seh-1`, `libwinpthread-1`, with `GCC: (GNU) 11.2.0`. `ipopt.lib` is an ar archive exporting `CreateIpoptProblem`, `IpoptSolve` and `GetIpoptCurrentIterate`. | confirmed | `objdump -p`, `strings`, `od`, `nm` (also exports `GetIpoptCurrentViolations`). The wheel ships the MinGW runtime DLLs. Linking `ipopt.lib` from MSVC is **unverified**. |
| The 3.8.1 manylinux2014 wheel and the installed pip 3.7.2 are old-ABI GCC 10.2.1 builds whose `casadi-config.cmake` injects `_GLIBCXX_USE_CXX11_ABI=0`. pip on this machine prefers manylinux_2_28. | confirmed | `casadi::Function::Function(std::string const&)` with no `__cxx11`. `GCC: (GNU) 10.2.1`. Config lines 6–7: `if(ON AND GNU)`. The 2_28 config has `if(OFF …)`. The installed WHEEL says `Tag: cp310-none-manylinux2014_x86_64`. `pip debug --verbose`: `cp310-none-manylinux_2_28` is at line 112, ahead of `manylinux_2_17` at 123. |
| Distro table (jammy 3.11.9 / mumps 5.4.1; noble 3.11.9 / 5.6.2; plucky and questing 3.14.17 / 5.7.3; resolute 3.14.19 / 5.8.2 / casadi 3.7.0+ds2-3; trixie 3.14.17; sid 3.14.20 and casadi 3.8.1+ds2-1 built against coinor-libipopt-dev). | confirmed | Launchpad `getPublishedSources` and the sources.debian.org API. `apt-cache show coinor-libipopt-dev` → `Depends: … libmumps-seq-dev`. Debian `casadi/3.8.1+ds2-1/debian/control` build-depends on `coinor-libipopt-dev`. |
| Ipopt 3.11.9 dates from 2014, and its `IpStdCInterface.h` has no `GetIpoptCurrentIterate`. | confirmed | The tag commit is dated 2014-08-16. `grep -c` → 0. |

## 7. CasADi from C++

| Claim | Verdict | Evidence |
|---|---|---|
| Link tests with g++-15 `-std=c++26`: pip 3.7.2 with the default ABI fails with undefined references (`_sym`, `nlpsol`, …); with `-D_GLIBCXX_USE_CXX11_ABI=0` it links and gives `x=[4.37331e-09, 1] status=Solve_Succeeded`. The 3.8.1 2_28 wheel with the default ABI gives the same result, with libstdc++ from `/opt/gcc-15/lib64`. | confirmed (reproduced) | `factcheck/abi/` holds fresh builds `t372_new` (link fails), `t372_old` and `t381` (both run), and `ldd`. |
| IPOPT's C API works with the default ABI against both 3.14.11 and 3.14.19 (`status=0 x=(0,1) f=2`). A null `eval_h` gives status -11, even with L-BFGS. | confirmed | `capi372` and `capi381` run. `capi_noh`: "INVALID_STDINTERFACE_NLP … No callback function for evaluating the Hessian of the constraints provided", `status=-11` (`Invalid_Problem_Definition`). |
| CMake config: one target `casadi::casadi` with defines `CASADI_SNPRINTF=snprintf;CASADI_WITH_THREADSAFE_SYMBOLICS;CASADI_WITH_THREAD`, link libraries `dl`, location `${_IMPORT_PREFIX}/casadi/libcasadi.so.3.7`. There is no ipopt target, only `pkgconfig/ipopt.pc`. Windows ships `libcasadi.lib` and `libcasadi.dll.a`. | confirmed | `casadi-targets.cmake` lines 58–63, `casadi-targets-release.cmake` line 11, zip listings. |
| Plugin search order: `CASADI_PLUGIN_SEARCH_PATH`, `GlobalOptions::casadipath`, `CASADIPATH`, bare name, `PLUGIN_EXTRA_SEARCH_PATH`, then `.`. `dlopen` uses `RTLD_LAZY\|RTLD_LOCAL` (plus `DEEPBIND`). Windows uses `AddDllDirectory` + `LoadLibraryExW`. | confirmed | `casadi/core/casadi_os.cpp@3.8.1` lines 84–125, 213–219 and 267–268. Also: `RTLD_NOW\|RTLD_GLOBAL` when global, and a legacy `SetDllDirectory` fallback. |
| "libstdc++ must be at least the version the wheel needs; /opt/gcc-15 satisfies that." | confirmed; qualified | The wheel needs only `GLIBCXX_3.4.22` / `CXXABI_1.3.9` (`objdump -T`). Ubuntu 22.04's system libstdc++ (3.4.30) already satisfies it, so `/opt/gcc-15` is not required for this. |

## 8. Licences

| Claim | Verdict | Evidence |
|---|---|---|
| IPOPT is EPL-2.0. §3.1(a) requires making the source available. No Exhibit A secondary-licence notice ships. Without one, EPL-2.0 is GPL-incompatible. | confirmed | Ipopt `LICENSE` lines 57, 110 and 262. `grep -rl "Secondary Licen" include/coin-or` → 0. `IpTNLP.hpp:3`. FSF licence list: "the EPL2 without this designation remains incompatible with the GPL". |
| CasADi is LGPL-3.0-or-later. | confirmed | Header text: "version 3 of the License, or (at your option) any later version". PyPI: "LGPLv3+". CCI head: `LGPL-3.0-or-later`. vcpkg metadata says `LGPL-3.0-only`. The LGPL-3 relinking duty for static linking is not re-read. |
| MUMPS is CeCILL-C, with BSD-3 parts and PORD separate. ThirdParty-Mumps is EPL-2.0 and ThirdParty-Metis is EPL-1.0. | confirmed | Wheel `licenses/mumps-external/MUMPS/LICENSE` lines 6–12. GitHub licence metadata. |
| The wheel's `libcoinmetis` is METIS 4: METIS 4 entry points and no `METIS_SetDefaultOptions`. | confirmed | `nm -D`: `METIS_EstimateMemory`, `METIS_mCHPartGraphRecursive`, … The SetDefaultOptions count is 0 in both 3.8.1 and the installed 3.7.2. |
| libgfortran is GPL-3 with the Runtime Library Exception. libquadmath is LGPL-2.1. | confirmed | `libgfortran/runtime/main.c` header. `libquadmath/COPYING.LIB` is LGPL 2.1, and `quadmath.h` says "Library GPL v2 or later". |
| BQPD is BSD-3 (Argonne 2025), while Uno's docs still say "academic licence". | confirmed | Wheel `licenses/BQPD/LICENSE` (Dundee 1996 + Argonne 2025, BSD text). Uno `docs/installation.md` line 71. |
| Licences of the alternatives: pagmo2 LGPL3/GPL3, qpOASES LGPL-2.1, HiGHS MIT, OSQP Apache-2.0, proxsuite BSD-2, ensmallen BSD-3, sleqp LGPL-3.0 (last push 2023-12-18), PRIMA BSD-3 (6.0 MB Fortran against 66 KB C), Uno MIT. | confirmed | GitHub licence metadata and files (pagmo2 `COPYING.lgpl3` and `COPYING.gpl3`, ensmallen `LICENSE.txt`). `chrhansk/sleqp` pushed 2023-12-18. PRIMA languages API. |

## 9. Alternatives

| Claim | Verdict | Evidence |
|---|---|---|
| NLopt: "only MMA and SLSQP support arbitrary nonlinear inequality constraints… only SLSQP supports nonlinear equality constraints". AUGLAG wraps a subsidiary optimizer. The project disables Luksan, so `LD_LBFGS` is unavailable. | confirmed | `nlopt/doc/docs/NLopt_Algorithms.md` lines 315 and 426–430. Project `conanfile.py` lines 10–12 (`enable_luksan: False`). The CCI nlopt recipe gates the Luksan files on that option. |
| Uno v2.9.0 (2026-09-08, MIT). `QP_solver` BQPD/HiGHS; `linear_solver` MA57/MA27/MA86/MUMPS/SSIDS; `hessian_model` includes LBFGS. The filtersqp preset uses a QP solver. Binaries ship for Linux, macOS and MinGW. | confirmed | Releases API (assets include `x86_64-w64-mingw32`). `docs/options.md` lines 19 and 28–30. `docs/presets.md` lines 15–34; the filtersqp preset defaults to `hessian_model=exact`. Whether HiGHS + LBFGS is robust on this problem is **unverified**. |
| CasADi 3.8.1 `casadi/solvers` has 21 plugins, including sqpmethod, feasiblesqpmethod, qrsqp, scpgen, qrqp, ipqp, linsol_ldl and linsol_qr. | confirmed | `_first_party_plugins` in CCI #31023 head (21 entries). |
| Ceres supports only bounds; ensmallen is built on Armadillo; "MMA/CCSAQ could be a more robust phase 1"; all effort estimates. | unverified | Not checked. These are estimates. |

---

## Corrected summary of the key facts

1. **CasADi in CCI.** PR #31023 adds a core-only casadi/3.8.1 with 21 first-party plugins and no IPOPT. The author is the upstream maintainer. A Conan team member (czoido) approved it on 2026-10-08 and tested it on macOS armv8, Linux aarch64 and Windows ARM64, shared and static. CI is green. The PR is still `blocked` by a merge rule I cannot see, so there is no evidence on timing. The recipe at head defines **no** `CASADIPATH`; shared builds find plugins through `$ORIGIN` rpaths. Licence is LGPL-3.0-or-later, default `shared=False`. Even after the merge, this gives CasADi without IPOPT.

2. **IPOPT in CCI.** No coin-ipopt recipe exists and none is open. The only attempts (coin-mumps #22466, coin-ipopt #22467, casadi #22468) came from one contributor, valgur. They were never rejected in a technical review: they died in a 2025-03-17 bot sweep of 222 of his PRs. His prerequisites, gfortran #23334 and lapack #23798, he withdrew himself; his view was that Fortran "should be a part of the CI image or installed on the consumer system". The coin-ipopt PR never built a single configuration (all INVALID_CONFIGURATION in both the v1 and v2 pipelines). The blocker is that CCI has no Fortran toolchain model: Conan settings have none, no library recipe requires gfortran, gcc/15.2.0 has no binaries, and the openblas `with_fortran` PR #29446 has had no review since January 2026. Every open-source sparse solver IPOPT supports (MUMPS, SPRAL) is mostly Fortran, and IPOPT has no LAPACK dense solver. **IPOPT in CCI is not in sight.**

3. **Ready-made Conan 2 recipes.**
   - **tttapa/conan-recipes** (ipopt 3.14.16 + coinmumps 3.0.7/MUMPS 5.7.1, about 100 lines each, CCI openblas, sequential MUMPS, no licence file) is the smallest template.
   - **RoboConan** (valgur's continuation of the CCI work) has coin-ipopt, coin-mumps, spral, coin-hsl and casadi with `with_ipopt=True`. It is **recipes-only, with no binary remote by design**, "not compatible with ConanCenter", and its README calls it a placeholder. Neither is consumable as a remote. You export the recipes locally and build from source.

4. **Fortran on this machine works with one gotcha.** I compiled a gfortran-11 object and linked it into a g++-15 `-std=c++26` program; it runs, both shared and static. The libgfortran soname has been 5 from GCC 8.1 through 15.1. However, g++-15 cannot resolve plain `-lgfortran`, because the `.so` symlink lives only in `/usr/lib/gcc/x86_64-linux-gnu/11/`. A local coinmumps/ipopt recipe whose `system_libs` contains `gfortran` therefore needs `-L/usr/lib/gcc/x86_64-linux-gnu/11` (or `-l:libgfortran.so.5`) in the profile. A second, unverified risk: ThirdParty-Mumps probes `-lmetis` by default and could pick up the system METIS without Conan knowing. Building the recipes themselves was not verified here; "about 1 day" is an estimate.

5. **Fortran-free IPOPT (`linear_solver=custom`) is possible, but the report understates the work.**
   - `custom` is **not** the default in a solver-less build. The run-time HSL loader is on by default, so the default is `ma27`, which then fails without libhsl. Either set `linear_solver=custom` explicitly or configure with `--disable-linear-solver-loader`.
   - There is no `OptimizeTNLP(tnlp, alg_builder)`. You must wrap the TNLP in a `TNLPAdapter` and call `OptimizeNLP(nlp, alg_builder)`, which means the C++ API; the C API cannot do this.
   - The required headers do ship.
   - The report leaves out two related options. Ipopt can load HSL at run time (`hsllib`); the Coin-HSL Archive is free to use but not redistributable. PardisoMKL is proprietary but needs no Fortran compiler.

6. **Wheel binaries.**
   - **The 3.8.1 wheels are also GitHub release assets** (33 wheels), identical to PyPI's. The report wrongly said 3.8.1 shipped no wheels on GitHub.
   - The manylinux_2_28 wheel (GCC 14.2.1, cxx11 ABI) links and runs with GCC 15 / C++26. It needs only GLIBCXX_3.4.22, so even the system libstdc++ suffices.
   - The installed pip 3.7.2 and the 3.8.1 manylinux2014 builds are old-ABI, and their CMake config injects `_GLIBCXX_USE_CXX11_ABI=0`. The C++ API fails to link without that define. IPOPT's C API works with either build.
   - The Windows wheel is MinGW (GCC 11.2) with MinGW runtime DLLs. Linking the C API through `ipopt.lib` from MSVC is unverified.
   - Every wheel bundles METIS 4 (`libcoinmetis`). Its licence forbids redistribution without approval and limits use by non-academic organisations to evaluation. That makes the wheels unsuitable for shipping, and questionable even for non-evaluation use outside academia.

7. **Other package managers.** These are good references, but each one is a second package manager next to Conan.
   - vcpkg has had a usable IPOPT (with MUMPS) only since 2026-09-25. On Windows it uses MinGW gfortran and DLLs only. vcpkg's casadi port is core-only.
   - conda-forge has ipopt 3.14.20 on all six platforms, using flang 21 or ifx on Windows, and MUMPS with METIS 5, not METIS 4. Its casadi (3.7.2) pulls in Python.
   - Homebrew has ipopt (using its own gfortran) but no casadi formula.
   - Ubuntu 22.04 and 24.04 ship Ipopt 3.11.9 (2014), which lacks the 3.14 C API used by the notes. Ubuntu 26.04 and Debian sid have 3.14.19/3.14.20 plus casadi.

8. **Licences.**
   - IPOPT is EPL-2.0 with no secondary-licence notice, so it is GPL-incompatible (FSF), and shipping binaries requires offering the source.
   - CasADi is LGPL-3.0-or-later.
   - MUMPS is CeCILL-C.
   - libquadmath is LGPL-2.1 and libgfortran is GPL-3 with the Runtime Library Exception.
   - METIS 5 is Apache-2.0 and safe; METIS 4 is not.
   - The repo has no LICENSE file, so its own licence is undecided. For a GPL geomsolver, IPOPT would be a blocker.

Not verified anywhere (keep labelled): every effort estimate, Windows and macOS build feasibility of local recipes, the robustness of MMA/CCSAQ, Uno + HiGHS or a custom dense KKT solver on this problem, the WSMP and Pardiso-project terms, whether C_LAPACK openblas is enough for MUMPS, the coinbrew `ls` under `~/Softwares/solvers`, the Ceres and ensmallen characterisations, the macOS wheel at run time, and why #31023 is blocked.