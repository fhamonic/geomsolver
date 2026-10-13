# Getting IPOPT into the geomsolver Conan build: hands-on results

All work was done in an isolated `CONAN_HOME` (`scratchpad/conan-home`). The profile `gcc15_c++26` was copied verbatim from `~/.conan2/profiles`. The repository and `~/.conan2` were not touched. Paths below are relative to `scratchpad/packaging/` unless they are absolute.

## Verdict

| | Route 1: repackage the CasADi wheel | Route 2: build MUMPS + Ipopt from source |
|---|---|---|
| Works on Linux with the gcc15 / C++26 profile | **Yes.** HS071 solved through the C interface. `casadi::nlpsol("ipopt")` from C++ also solved, but only with `-D_GLIBCXX_USE_CXX11_ABI=0`. | **Yes.** HS071 solved through the C interface. It also runs from a relocated deploy folder. |
| Recipe size (code lines, without comments or blanks) | 72 (96 total) | 144 (coin-mumps, 180 total) + 97 (coin-ipopt, 118 total). metis and openblas come from ConanCenter. Tests: about 100 lines per route. |
| Cold build time | about 6 s (75.6 MB download, then unzip 223 MB) | about 2 min wall: openblas 77 to 79 s, MUMPS 10.3 to 10.7 s, Ipopt 23 s (2 interleaved rounds). Well inside the 60 min time box. |
| IPOPT version | 3.14.11, fixed by the CasADi release | 3.14.20, the latest, chosen by you |
| Main blockers found | METIS 4.0 licence on `libcoinmetis`. The C++ API uses the old (pre-C++11) `std::string` ABI. The wheel libs re-export about 300 libstdc++ symbols. | The Fortran compiler and libgfortran sit outside Conan's model. libtool's `DT_RUNPATH` broke loading of indirect dependencies until patched. libgfortran.so.5 is a system runtime dependency. |
| Production effort, Linux only | about 0.5 to 1 day | about 1.5 to 2 days |
| Production effort, Linux + Windows | +2 to 4 days, high uncertainty (see the Windows section) | +1.5 to 2.5 days with a MinGW+gfortran toolchain (a cross build already compiles and links), +3 to 6 days with MSVC |

**Recommendation from the packaging side:** if geomsolver drives IPOPT through its **C interface** (`IpStdCInterface.h`), Route 2 is the one to keep. It is the cleaner and more controllable option. The build is fast, there is no ABI trap and no non-free METIS, and Ipopt is current. Two of its costs are now handled in the recipes: the Fortran compiler (three profile lines) and the `$ORIGIN`/patchelf RPATH fix. One remains: the gfortran runtime (`libgfortran.so.5`) is a system dependency outside Conan. Route 1 is the quickest way to experiment today (it works in about 6 s), but it is not a dependency to ship.

Dependency friction, put plainly, counting packaging failures only:
- Linux: 5. Consumer `-lgfortran` link, the unversioned CasADi plugin name, `libcoinmumps.so.3 not found` / system openblas picked up, metis missing from the fat link, and the static-libgfortran TLS dead end (an optional extra).
- MinGW probe: 2 more (gklib, the xPack `.la` paths).

Each one was diagnosed and fixed (or ruled out) within minutes. These are three local recipes plus one profile file, not a project. "Not in ConanCenter" turns out to be a moderate one-off cost on Linux, not a blocker.

If you want the **CasADi C++ API** (Opti), neither route gives it cheaply. The wheel's libcasadi needs `_GLIBCXX_USE_CXX11_ABI=0` in every translation unit that includes it. Route 2 does not build CasADi at all. A CasADi-from-source recipe on top of coin-ipopt would be a third, larger piece of work. I did not attempt it here, so that remains unverified.

---

## Route 1: binary repackaging of the CasADi wheel (`casadi-bin/3.7.2`)

Files:
- `recipes/casadi-bin/conanfile.py` (96 lines)
- `recipes/casadi-bin/test_package/{conanfile.py, CMakeLists.txt, hs071_c.cpp, casadi_nlp.cpp}`

What the recipe does:
- `build()` downloads `casadi-3.7.2-cp310-none-manylinux2014_x86_64.whl` from files.pythonhosted.org. The sha256 is pinned (`e1cb725c…0082`, checked against the PyPI JSON). Only `casadi/*` is unzipped.
- `package()` copies the SONAME files exactly as the wheel's `NEEDED` entries spell them: `libipopt.so.3`, `libcoinmumps.so.3`, `libcoinmetis.so.2`, `libcasadi-tp-openblas.so.0`, `libgfortran-040039e1.so.5.0.0` and `libquadmath-96973f99.so.0.0.0`. With `with_casadi=True` it also copies `libcasadi.so.3.7` and `libcasadi_nlpsol_ipopt.so.3.7`. It recreates the dev symlinks and copies the headers (`include/coin-or`, `include/casadi`) and the licence folders.
- Components: `casadi-bin::ipopt` and `casadi-bin::casadi`. `package_id` drops the compiler and build_type settings.
- The package is 43 MB.

Results (`logs/final_casadi-bin.log`):
```
Ipopt 3.14.11  status=0  f=17.0140171  x=(1.000000, 4.743000, 3.821150, 1.379408)
HS071 OK
casadi 3.7.2  status=Solve_Succeeded  f=17.014  x1=4.743
casadi HS071 OK
```
- Both test executables also run **without** `conanrun.sh`, with only `/opt/gcc-15/lib64` on `LD_LIBRARY_PATH`. The wheel libraries that have dependencies (libipopt, libcoinmumps, libcasadi, the plugin) carry `DT_RPATH=$ORIGIN`, and everything sits in one package `lib/`, so the build-tree `RUNPATH` that CMake writes into the executable is enough.
- The CasADi test also runs from a relocated `bin/` + `lib/` copy, with the executable's `RUNPATH` set to `$ORIGIN/../lib`. The C test uses the same mechanism but was not run that way.

Gotchas, each one observed:
1. **Old libstdc++ string ABI.** The wheel is built with devtoolset-10 (GCC 10.2.1) on manylinux2014. The wheel's own `cmake/casadi-config.cmake` adds `_GLIBCXX_USE_CXX11_ABI=0`, and `nm` shows `casadi::nlpsol(std::string const&, …)` and `Ipopt::OptionsList::SetStringValue(std::string const&, …)` with no `__cxx11`.
   - Compiling `casadi_nlp.cpp` with the project's default `libstdc++11` gives 10 undefined references, for example ``casadi::nlpsol(std::__cxx11::basic_string…)`` and ``casadi::GlobalOptions::casadipath[abi:cxx11]``. With `-D_GLIBCXX_USE_CXX11_ABI=0` it links and solves.
   - Consequence: CasADi C++ code (and Ipopt's C++ `TNLP` API) has to live in an isolated library compiled with ABI=0, behind a POD/C interface. The recipe puts the define on the `casadi` component, so every TU of any target linking it gets the define.
   - The IPOPT **C interface** is unaffected.
2. **libstdc++ symbol interposition.** devtoolset links parts of libstdc++ statically (`libstdc++_nonshared.a`), and those symbols are exported: 353 symbols shared with GCC 15's libstdc++ from `libcasadi.so.3.7`, 303 from `libipopt.so.3`. `LD_DEBUG=bindings` shows the executable's own `operator delete(void*, size_t)` binding to `libcasadi.so.3.7` rather than to libstdc++. I saw no failure from it, but it applies to the whole process.
3. **Plugin loading.** CasADi `dlopen()`s plugins by their **unversioned** name (`libcasadi_nlpsol_ipopt.so`). The first test failed with `Plugin 'ipopt' is not found` until the recipe added that symlink. Search behaviour, all measured:
   - found when the plugin is next to `libcasadi` (through libcasadi's `DT_RPATH=$ORIGIN`, seen with `LD_DEBUG=libs`), or in a directory on the executable's `RUNPATH` / `LD_LIBRARY_PATH`;
   - not found from a separate `plugins/` directory unless `CASADIPATH=<dir>` or `casadi::GlobalOptions::setCasadiPath(<dir>)` is set (both tested and working).
4. **Licences: METIS 4.0.** `include/licenses/metis-external/metis-4.0/LICENSE` says METIS "may not be sold or redistributed without prior approval". The symbols confirm the METIS 4 API (`METIS_EstimateMemory`, `METIS_mCPartGraphKway`, `__`-prefixed internals).
   - `libcoinmetis.so.2` is `NEEDED` by both `libipopt` and `libcoinmumps`, so it cannot be dropped.
   - This matters if geomsolver binaries are ever distributed. The repo has no LICENSE yet.
   - The other components: Ipopt EPL-2.0, MUMPS CeCILL-C, OpenBLAS BSD-3, libgfortran/libquadmath under the GCC runtime exception, CasADi LGPL-3.0.
5. **Version lag.** You get Ipopt 3.14.11, whatever CasADi bundled; upstream is at 3.14.20. Its C interface has `GetIpoptCurrentIterate`/`GetIpoptCurrentViolations` but no `GetIpoptVersion` (the test prints the `IPOPT_VERSION` macro instead).
6. **Minor.** The wheel zip stores `.so`, `.so.3` and `.so.3.14.11` as three full copies, not symlinks. Its pkg-config files point to `/work/build/external_projects`, so they cannot be used. The unzipped `.so` files lose their exec bit, which is harmless.

### Windows / macOS for Route 1 (from inspecting the wheels; nothing run)

**Windows** (`…-win_amd64.whl`):
- The DLLs are built by **MinGW-w64 GCC 11.2.0 against `msvcrt.dll`**. Each one imports `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` and `libgfortran-5.dll`, all shipped in the wheel. The C++ API uses the **new** `__cxx11` ABI on Windows, the opposite of Linux.
- For the C interface there is `ipopt.lib`, a dlltool-made archive with `__imp_IpoptSolve` / `__imp_CreateIpoptProblem`, plus `libipopt.dll.a`.
- With **MSVC**: the CasADi C++ API is unusable. The IPOPT C interface should link through `ipopt.lib` if the MinGW runtime DLLs are shipped next to the exe (unverified).
- With **your xPack MinGW GCC 12.2** (the `linux_to_win64` profile): its runtime links **UCRT** (`api-ms-win-crt-*`), while the wheel's links msvcrt. Both ship a `libstdc++-6.dll` with the same name, and only one can load per process. That is a CRT mix with a DLL-name clash. Plausible for the C interface only, unverified.
- The recipe needs a second wheel entry and DLL/import-lib handling in `package_info`: about 0.5 day to write, plus an unknown amount of debugging on a real Windows box.

**macOS** (`…-macosx_11_0_arm64.whl`):
- `@rpath` install names with `LC_RPATH @loader_path`, so they relocate cleanly.
- libipopt uses the Accelerate framework but links a **bundled `@rpath/libc++.1.0.dylib`**. That puts two C++ runtimes in one process next to Apple's `/usr/lib/libc++.1.dylib`. Probably fine for the C interface, a real problem for the C++ API (unverified).

---

## Route 2: build from source (`coin-mumps/3.0.14` + `coin-ipopt/3.14.20`)

Files:
- `recipes/coin-mumps/conanfile.py` (180 lines; options `shared`, `with_metis`): coin-or ThirdParty-Mumps 3.0.14, which fetches MUMPS 5.9.1. Both sha256 are pinned. It replays `get.Mumps` (3 patches plus the `mpi.h`→`mumps_mpi.h` rename) and builds with `AutotoolsToolchain`. It links metis 5.2.1 (+gklib) and openblas 0.3.30 from ConanCenter.
- `recipes/coin-ipopt/conanfile.py` (118 lines): Ipopt 3.14.20 via autotools with `--with-mumps-*`, `--with-lapack-lflags` and `--disable-f77`. No Fortran is needed here, because openblas carries its C-translated LAPACK.
- `recipes/coin-ipopt/test_package/` runs HS071 through the C interface, compiled as C++26 with `std::println`.
- Profile `conan-home/profiles/gcc15_c++26_fortran` is `include(gcc15_c++26)` plus three lines:
  ```
  coin-mumps/*:tools.build:compiler_executables={"c": "/opt/gcc-15/bin/gcc", "cpp": "/opt/gcc-15/bin/g++", "fortran": "/usr/bin/gfortran"}
  coin-mumps/*:tools.build:cflags=["-std=gnu17"]
  coin-ipopt/*:tools.build:cflags=["-std=gnu17"]
  ```
  The Fortran compiler is scoped to coin-mumps. That way openblas keeps building `C_LAPACK` and never depends on libgfortran.

Result (`logs/final_shared_coin-ipopt.log`, `logs/final_fat_coin-ipopt.log` and both timing rounds):
```
Ipopt 3.14.20  status=0  f=17.0140171  x=(1.000000, 4.743000, 3.821150, 1.379408)
HS071 OK
```

Build times (wall clock; other agents were compiling at the same time, load average 7 to 13 on 16 cores). Repeated rounds, interleaved openblas → mumps → ipopt, from `logs/route2_timings.tsv`:

| round | openblas 0.3.30 (shared, ConanCenter) | coin-mumps | coin-ipopt (+ test_package build and run) |
|---|---|---|---|
| 1 | 76.9 s | 10.7 s | 23.4 s |
| 2 | 78.7 s | 10.3 s | 23.1 s |

Both rounds ended with `HS071 OK`.

Earlier one-off runs: openblas (static) + metis + gklib 1 min 45 s; coin-mumps 10.9 to 13.8 s; coin-ipopt 24 to 25 s including the test_package. The cmake/patchelf tool binaries come prebuilt from ConanCenter.

Blockers hit, in order, each fixed in the recipes:
1. **No Fortran in Conan's settings model.** It has to come in through `tools.build:compiler_executables["fortran"]` in the profile, and the recipe sets `FC`/`FCFLAGS` itself. The gfortran version does **not** enter the `package_id`.
2. **`cannot find -lgfortran` when linking consumers.** GCC 15 in `/opt/gcc-15` has no Fortran, and Ubuntu keeps the `libgfortran.so` dev link only in `/usr/lib/gcc/x86_64-linux-gnu/11/`. Fix: for a shared libcoinmumps, do not propagate `gfortran`/`quadmath` as `system_libs`, because `NEEDED libgfortran.so.5` is already recorded. For a static libcoinmumps, the recipe exports `-L$(gfortran -print-file-name=…)`.
3. **Two embedded copies of OpenBLAS.** With static openblas, libcoinmumps exported all of OpenBLAS (`dgemm_`, `dgemm_kernel`, …), and libipopt would embed a second copy, the two interposing on each other. Fix: `openblas/*:shared=True` by default, plus `-Wl,--exclude-libs,ALL` to keep the static metis/gklib private.
4. **`libcoinmumps.so.3 => not found`, and the system `/lib/x86_64-linux-gnu/libopenblas.so.0` silently picked up instead of Conan's**, whenever no `LD_LIBRARY_PATH` was set (which is how you run `build/geomsolver-cli`). Cause: libtool bakes `DT_RUNPATH=/opt/gcc-15/lib/../lib64` into libipopt, and glibc never applies an executable's `RUNPATH` to indirect dependencies. It also ignores the executable's `DT_RPATH` for the dependencies of a library that has its own `DT_RUNPATH`. Fix: `tool_requires("patchelf/0.18")` and `patchelf --force-rpath --set-rpath '$ORIGIN'` on every installed `.so`. Tested afterwards:
   - executable linked with `RUNPATH` only (CMake's default): still fails in the Conan cache, because the libraries live in different package folders;
   - executable linked with `-Wl,--disable-new-dtags` (`DT_RPATH`): works from the cache with no environment;
   - `conan install --deployer=runtime_deploy` into one folder, executable `RUNPATH=$ORIGIN/../lib`: works with no environment.
   - Today, geomsolver-cli's nlopt has no indirect shared dependencies, so this problem is new with Ipopt.
5. **Alternative "fat" libipopt**, also tested: `-o coin-mumps/*:shared=False -o openblas/*:shared=False`. You get a single 8.3 MB `libipopt.so.3` with MUMPS + METIS 5 + OpenBLAS inside and nothing exported from them. It runs with a plain CMake build-tree `RUNPATH`, exactly like nlopt today. That needed coin-ipopt to add metis/gklib to the MUMPS link flags when MUMPS is static. Remaining `NEEDED`: `libgfortran.so.5` and `libquadmath.so.0`.
6. **Static libgfortran into libipopt.so is impossible here.** Ubuntu's `libgfortran.a` is PIC, but it uses local-exec TLS: `relocation R_X86_64_TPOFF32 against hidden symbol 'thread_unit' can not be used when making a shared object`. Also, `-Wl,-Bstatic -lgfortran` gets reordered by libtool. That leaves a runtime dependency on the `libgfortran5` system package. The other options are copying `libgfortran.so.5`/`libquadmath.so.0` into the package (what auditwheel did for the wheel) or making the exe static.

### gfortran 11 objects + GCC 15 libstdc++/libgcc in one process

Measured with `ldd` and runs:
- `libgfortran.so.5` (gfortran 11) needs only `GCC_4.3.0` from `libgcc_s` and `GLIBC_2.34`. With `/opt/gcc-15/lib64` reachable, `libgcc_s.so.1` and `libstdc++.so.6` both come from GCC 15, and `libquadmath.so.0` comes from GCC 15 or the system depending on search order. HS071 solves in both cases.
- Rule: one `libgcc_s.so.1` loads per process, and it must be the newest any component needs. Here GCC 15's satisfies everything.
- Without `/opt/gcc-15/lib64`, the run fails, but because of libstdc++ (`GLIBCXX_3.4.31 not found`, needed by the GCC 15-built libipopt and the test). That is the constraint geomsolver-cli already has, through its own `RUNPATH` to `/opt/gcc-15/lib64`. The gfortran side adds nothing new.
- libgfortran does not link libstdc++, and MUMPS's Fortran objects never touch C++, so no C++ ABI is mixed.
- The real costs:
  - libgfortran.so.5 must exist on the target machine (it is not a Conan package, so `runtime_deploy` does not copy it);
  - a gfortran upgrade does not rebuild the package.

### Windows for Route 2

Probe: a MinGW cross build from this Linux box. I used your `linux_to_win64` profile (xPack MinGW-w64 GCC 12.2, which ships `x86_64-w64-mingw32-gfortran` and links UCRT), copied as `conan-home/profiles/mingw12_win64_fortran` plus the Fortran conf. The build profile was `-pr:b gcc15_c++26`. Logs are `logs/win_*.log`.

What worked:
- openblas 0.3.30 cross-builds as a DLL. The whole first run, openblas plus a full MUMPS compile that failed at the link, took 2 min 50 s.
- MUMPS cross-builds: `bin/libcoinmumps-3.dll`, 12 s. It imports `libgfortran-5.dll`, `libopenblas.dll` and UCRT.
- Ipopt cross-builds: `bin/libipopt-3.dll`, 21 s. It imports `libcoinmumps-3.dll`, `libopenblas.dll`, `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and `libwinpthread-1.dll`.
- An `hs071.exe` (C++20 variant of the test) links against it.
- **Not run:** there is no wine here. `-tf ""` skipped the test_package, because GCC 12 has no `<print>` / C++26.

Blockers hit:
1. **ConanCenter `gklib/5.1.1` does not build for MinGW** (`gk_arch.h:45: sys/resource.h: No such file or directory`), so there is no metis. I added `coin-mumps:with_metis` (default True) and used `False` on Windows, where MUMPS falls back to its built-in orderings. The fix is to patch gklib or vendor metis.
2. **Bad paths in the xPack toolchain's libtool archives.** Its `libgfortran.la` points libtool at a `libquadmath.la` on xPack's CI machine (`/__w/mingw-w64-gcc-xpack/…`), and the DLL link failed. Fixed by setting `FCLIBS` to the full paths of `libgfortran.dll.a` / `libquadmath.dll.a`, found with `-print-file-name`.

What this does and does not show:
- The autotools recipes cross to MinGW with small, local changes.
- geomsolver itself needs GCC 15 / C++26, so a real Windows build would need a newer MinGW with gfortran (MSYS2's UCRT64 GCC 15, for example; unverified) or MSVC.
- **MSVC has no Fortran compiler.** MUMPS would need Intel ifx or LLVM flang plus MSYS2 for the autotools scripts, and Ipopt's `--enable-msvc` path.
- The other option is building Ipopt+MUMPS with MinGW (UCRT) and calling only the C interface from MSVC. That needs a `.lib` made from a `.def`, and you ship `libgfortran-5.dll`, `libquadmath-0.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` and `libstdc++-6.dll` next to the exe. Unverified.

---

## Effort to make each route production-quality in this repo

Both estimates assume the recipes live in the repo (for example `conan/recipes/`) and are exposed as a `local-recipes-index` remote, or exported by a `make deps` step. They cover packaging only; the `LocalProblem` → IPOPT adapter is separate.

**Route 1, Linux only: about 0.5 to 1 day.**
- Move the recipe into the repo and add `requires("casadi-bin/3.7.2")`.
- Link `casadi-bin::ipopt` (C interface only).
- Add a CI check that `IpoptSolve` loads from the build tree.
- Decide on the METIS 4 licence question.
- Adding the CasADi C++ API adds about 1 more day: an ABI=0 shim library with a POD interface, plus guarding against the define leaking into other targets.

**Route 1, Linux + Windows: +2 to 4 days, uncertain.** Wheel mapping, DLL/import-lib `package_info`, the MSVC or UCRT-MinGW vs msvcrt question, testing on real Windows. macOS would be another +1 day (two libc++).

**Route 2, Linux only: about 1.5 to 2 days.**
- Factor the shared helper (`set_origin_rpath`, flag aggregation) into a `python_requires`.
- Pick shared vs fat (fat is simplest to consume).
- Decide how libgfortran ships: system package, or copy it into the package.
- Put the gfortran version into `package_id` (for example `self.info.conf` or a custom setting).
- Mirror the MUMPS tarball (it comes over plain http from coin-or-tools.github.io).
- Add `make` / profile documentation for the Fortran conf.
- Add a CI job that builds from scratch, about 2 min.

**Route 2, Linux + Windows:**
- **With a MinGW toolchain that has gfortran: +1.5 to 2.5 days.** Fix or replace the gklib build, ship the MinGW runtime DLLs, and test on a real Windows machine, since nothing Windows was run here.
- **With MSVC: +3 to 6 days.** MSVC has no Fortran, which makes MUMPS the hard part: Intel ifx or LLVM flang plus MSYS2 for autotools, and Ipopt's `--enable-msvc` path.

---

## Files and logs (absolute paths)

- Recipes:
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/recipes/casadi-bin/conanfile.py` + `test_package/`
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/recipes/coin-mumps/conanfile.py`
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/recipes/coin-ipopt/conanfile.py` + `test_package/`
  - shared HS071 source: `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/recipes/common/hs071_c.cpp`
- Profiles (isolated home):
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/conan-home/profiles/gcc15_c++26_fortran`
  - `.../conan-home/profiles/mingw12_win64_fortran`
- Final verification logs, all passing `HS071 OK`:
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/logs/final_shared_coin-ipopt.log`
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/logs/final_fat_coin-ipopt.log`
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/logs/final_casadi-bin.log` (includes `casadi HS071 OK`)
  - `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/logs/final_casadi-bin_ipopt_only.log`
- Timings: `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/logs/route2_timings.tsv` (script `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/scripts/time_route2.sh`)
- Experiments (ABI link failure, plugin search, relocation, RPATH vs RUNPATH, fat libipopt, Windows exe): `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/exp/`
- The failed static-libgfortran attempt: `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/exp/coin-ipopt_static_fortran_attempt.py`, log `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/logs/coin-ipopt_selfcontained_create_2.log`
- Linux-only snapshot of the recipes, taken before the MinGW changes: `/tmp/claude-1000/-home-plaiseek-Projects-geomsolver/192360ef-7bce-4b34-86f0-9714a0363695/scratchpad/packaging/exp/recipes_linux_only_snapshot/`

To reproduce: `export CONAN_HOME=…/scratchpad/conan-home LD_LIBRARY_PATH=/opt/gcc-15/lib64`, then:
- `conan create recipes/coin-mumps -pr:a gcc15_c++26_fortran -b missing`
- `conan create recipes/coin-ipopt -pr:a gcc15_c++26_fortran -b missing`
- `conan create recipes/casadi-bin -pr:a gcc15_c++26 -b missing`