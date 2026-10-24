# Installation

geomsolver is built from source, with conan 2 and GCC 15. Every library comes from conan: Dear ImGui, ImPlot, GLFW and GLEW for the window, nlohmann_json and json-schema-validator for the instance files, NLopt for the optimisation, Eigen for the sketch solver (in its MPL2-only configuration) and doctest for the tests. The build produces two programs, the GUI `geomsolver` and the headless `geomsolver-cli`.

## Prerequisites

| Tool | Notes |
| --- | --- |
| **GCC 15** | The code is C++26. The profile below expects the compiler in `/opt/gcc-15`. |
| **conan 2** | The package manager; this page was checked with conan 2.33.0. Conan also brings CMake. |
| **make** | The `Makefile` wraps the conan and test commands. |
| **X11 development packages** | GLFW's conan recipe uses the system's X11 libraries through `xorg/system`. Conan checks that they are installed (see [Troubleshooting](#troubleshooting)). |
| **zensical** | Optional: it builds this documentation. |

## 1. Create the conan profile

`make build` uses the conan profile `gcc15_c++26`. Create it as `~/.conan2/profiles/gcc15_c++26`:

```ini title="~/.conan2/profiles/gcc15_c++26"
[settings]
arch=x86_64
build_type=Release
compiler=gcc
compiler.cppstd=26
compiler.libcxx=libstdc++11
compiler.version=15
os=Linux

[buildenv]
CC=/opt/gcc-15/bin/gcc
CXX=/opt/gcc-15/bin/g++
```

Adapt `CC` and `CXX` if your GCC 15 lives elsewhere. Check that conan resolves every dependency with it, from the repository root:

```bash
conan graph info . -pr=gcc15_c++26 -c 'libpq/*:tools.build:cflags=["-std=gnu17"]' -c 'implot/*:tools.info.package_id:confs=["user.geomsolver:imgui"]' -c 'implot/*:user.geomsolver:imgui=docking' -s 'mesa-glu/*:compiler.cppstd=23'
```

The command lists the packages it would use, and stops with an error if a recipe cannot be resolved for this profile.

!!! tip
    If your shell sets `CPATH`, the headers found there take precedence over the ones conan provides. Add `CPATH=!` to the `[buildenv]` section of the profile to unset it during the build.

## 2. Build

```bash
make build
```

This runs `conan build` into `build/`, with the profile and four per-package settings:

```bash
conan build . -of=build -b=missing -pr=gcc15_c++26 -c 'libpq/*:tools.build:cflags=["-std=gnu17"]' -c 'implot/*:tools.info.package_id:confs=["user.geomsolver:imgui"]' -c 'implot/*:user.geomsolver:imgui=docking' -s mesa-glu/*:compiler.cppstd=23
```

`-b=missing` builds from source the dependencies that conan has no binary for, so the first build takes longer. It downloads the sources of Dear ImGui's docking branch from GitHub, so it needs network access; the binary then stays in the conan cache. Conan also copies the Dear ImGui backends and fonts into `imgui_backends/` and `imgui_fonts/` at the repository root. `make clean` removes `build/` and `imgui_backends/`.

!!! warning "Keep the two `implot/*` settings"
    geomsolver uses Dear ImGui's docking branch, `imgui/1.92.5-docking`. ImPlot's package id records only the minor version of Dear ImGui, so conan would otherwise reuse an ImPlot binary built against the regular `imgui/1.92.5`, whose internal structures have another layout. The two `implot/*` settings give ImPlot a package id of its own, and `-b=missing` builds it against the docking branch. Without them, conan stops with `Invalid: implot must be built against imgui's docking branch`.

The build produces:

| File | Content |
| --- | --- |
| **`build/geomsolver`** | the GUI, described in [The GUI](gui.md) |
| **`build/geomsolver-cli`** | the headless solver, described in [The command line](cli.md) |
| **`build/test_engine`**, **`build/test_solve`**, **`build/test_sketch`**, **`build/test_app`** | the unit tests |

## 3. Run the tests

```bash
make test
```

`make test` runs `make build` first, then `ctest --output-on-failure` in `build/`. To run the tests of an existing build without building again, call `ctest` directly:

```bash
cd build && ctest --output-on-failure
```

The suite has eleven tests:

| Test | What it checks |
| --- | --- |
| **`test_engine`** | parser, compiler, evaluator, geometry and instance files |
| **`test_solve`** | the NLP, local runs, multistart and Pareto jobs, and the background service |
| **`test_sketch`** | the sketch solver: a drawing settled onto its geometry constraints, the values written back, degrees of freedom, bodies whose frame is not anchored, redundant, Malformed and conflicting constraints, accidental configurations, free directions and monitored groups, drags (points, edges, circles, bodies, the pose gizmo, Edit body) and the assembly modes they keep, a diagnosis on another thread during drags; it prints the timings of its corpus, with a warning above their targets, and never fails on one |
| **`test_app`** | the GUI logic, headless: document edits and undo, renames, selection and deletion, the scene, the canvas's picking, clicks, drags, pan, zoom and what it draws, the solver session, the menu and keyboard actions, the dialogs' flows, change notifications, and what the panels show and do (the lists, Properties, Document, Solver, Margins, Messages, sweep bar), every section of the TV example edited from them, with a list model and a property grid as a retained toolkit would keep them |
| **`geomsolver_selftest`** | the ImGui front-end, headless: a canvas drag and keyboard shortcuts through `geomsolver --selftest tests/data/tv_corner_ref.json` |
| **`gui_scripts`** | the scripts of `tests/gui`, played with `geomsolver --headless` (a box selection, a context menu's Delete, a pan, F2, a constraint added through the formula pop-up...), each checked through its `--dump`, with an empty `XDG_CONFIG_HOME` that must stay empty (`tests/gui/gui_scripts.cmake`) |
| **`gs_app_toolkit_free`** | `gs_app`, the GUI logic under the front-end, includes no GUI toolkit header (`tests/app/toolkit_free.cmake`) |
| **`docs_screenshots`** | every screenshot a page of `docs/` shows is made by `tools/screenshots/docs.sh`, which makes no other, and `docs/assets/screenshots` holds nothing else (`docs.sh --check`) |
| **`geomsolver_cli`** | the CLI end to end (`tests/cli/cli_checks.cmake`) |
| **`live_instance`** | `data/tv_corner.json` loads, compiles and reaches a feasible design in an 8-start multistart |
| **`docs_instances`** | every instance of the documentation, `docs/instances/*.json`, evaluates without a warning; the files named `*-broken.json`, wrong on purpose, are still diagnosed (`tests/docs/docs_instances.cmake`) |

The same timings fail at 3 times their targets in **`bench_sketch`**, which a plain `ctest` leaves out: run it on a release build with `ctest -C bench -L bench`.

The tests that pin numbers read the frozen copies `tests/data/tv_corner_ref.json` and `tests/data/example_room_ref.json`, so editing the instances in `data/` never breaks them. Only `live_instance` reads `data/tv_corner.json` (see [Troubleshooting](#troubleshooting)).

## 4. Run the programs

Open an instance in the GUI:

```bash
./build/geomsolver tests/data/tv_corner_ref.json
```

Without an argument, the GUI opens `./data/tv_corner.json`. Check and solve an instance from the command line:

```bash
./build/geomsolver-cli tests/data/tv_corner_ref.json --eval
./build/geomsolver-cli tests/data/tv_corner_ref.json
```

The first command evaluates the hand-made design written in the file: it is infeasible, with a protrusion of 107.4117 cm. The second solves; the best design found protrudes 104.3729 cm.

!!! note "Binaries and libstdc++"
    A binary built with GCC 15 needs the C++ runtime of GCC 15, newer than the one of many distributions: Ubuntu 22.04's `libstdc++` stops at `GLIBCXX_3.4.30`, and the binaries ask for `GLIBCXX_3.4.32`. The build records the directory of GCC 15's `libstdc++` in the binaries (their `RUNPATH`), together with the conan directories of the shared libraries they use, such as libglvnd's `libGL` for the GUI. The binaries therefore run in place without any environment variable, but only on a machine with the same GCC 15 and conan cache. Check what they load with:

    ```bash
    ldd build/geomsolver-cli
    ```

## 5. Build the documentation

The documentation is a [Zensical](https://zensical.org) site: `zensical.toml` at the repository root, the pages in `docs/`.

```bash
pip install zensical
make doc
```

`make doc` runs `zensical serve`, which previews the site and rebuilds it on every change. `zensical build --strict` builds it into `site/` and stops on any warning, such as a broken link. The instances shown in the pages are the files of `docs/instances/`, included verbatim, so a page always shows the file that was tested.

## Troubleshooting

### GLIBCXX_3.4.32 not found

```text
build/geomsolver-cli: /usr/lib/x86_64-linux-gnu/libstdc++.so.6: version `GLIBCXX_3.4.32' not found (required by build/geomsolver-cli)
```

The binary loaded the system's `libstdc++`, older than the one of GCC 15. This happens when `LD_LIBRARY_PATH` points at the system's library directory, which takes precedence over the binary's `RUNPATH`, or when the binary was copied to a machine without GCC 15. Unset `LD_LIBRARY_PATH`, or run the binary where it was built.

### Conan stops on missing system packages

The `xorg/system` recipe checks that the X11 development packages are installed. Conan's default `tools.system.package_manager:mode` is `check`: it stops with `System requirements: '...' are missing but can't install because tools.system.package_manager:mode is 'check'`. Install the listed packages with your package manager, or let conan install them by adding `-c tools.system.package_manager:mode=install -c tools.system.package_manager:sudo=True` to the `conan build` command.

### The GUI does not start

```text
GLFW error 65550: X11: The DISPLAY environment variable is missing
```

The GUI needs a display, also for `--screenshot` and `--script`. Run it in a graphical session, or use `geomsolver-cli`, which needs none.

### `live_instance` fails

```text
83% tests passed, 1 tests failed out of 6

The following tests FAILED:
	  5 - live_instance (Failed)
```

`live_instance` solves your working copy `data/tv_corner.json` with 8 starts and fails when no start reaches a feasible design. The other five tests do not read `data/`. Run the instance yourself to see which constraints miss:

```bash
./build/geomsolver-cli data/tv_corner.json --starts 8
```

If several constraints miss by about the same small amount, the instance asks for more than the geometry allows: see [Troubleshooting](../guide/troubleshooting.md) in the modelling guide.

## Next steps

- [Your first problem](tutorial.md) builds and solves an instance step by step.
- [The GUI](gui.md) and [The command line](cli.md) describe the two programs.
