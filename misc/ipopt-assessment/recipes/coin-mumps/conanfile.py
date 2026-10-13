import io
import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, get, load, patch, rename, rm, rmdir, save
from conan.tools.gnu import Autotools, AutotoolsToolchain
from conan.tools.layout import basic_layout

required_conan_version = ">=2.0"


def set_origin_rpath(conanfile, libdir):
    """Give every installed shared library DT_RPATH=$ORIGIN.

    libtool bakes the build machine's compiler libdir in as DT_RUNPATH. A
    library with a DT_RUNPATH makes glibc skip the executable's DT_RPATH
    for that library's own dependencies (and an executable's DT_RUNPATH
    never applies to them), so libcoinmumps went unfound and the system
    libopenblas.so.0 was silently picked up instead of the Conan one.
    """
    for name in sorted(os.listdir(libdir)):
        path = os.path.join(libdir, name)
        if ".so" in name and os.path.isfile(path) and not os.path.islink(path):
            conanfile.run(f"patchelf --force-rpath --set-rpath '$ORIGIN' {path}")


class CoinMumpsConan(ConanFile):
    """Sequential MUMPS built the way Ipopt expects it (coin-or ThirdParty-Mumps)."""

    name = "coin-mumps"
    version = "3.0.14"  # ThirdParty-Mumps glue; it pins MUMPS 5.9.1
    license = ("CECILL-C", "EPL-2.0")
    url = "https://github.com/coin-or-tools/ThirdParty-Mumps"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False], "with_metis": [True, False]}
    # A static openblas would be embedded, and exported, by both libcoinmumps
    # and libipopt: two copies whose internal symbols interpose on each other.
    default_options = {"shared": True, "fPIC": True, "with_metis": True, "openblas/*:shared": True}

    _sources = {
        "thirdparty": (
            "https://github.com/coin-or-tools/ThirdParty-Mumps/archive/refs/tags/releases/3.0.14.tar.gz",
            "49c2773de7b34dea0152b1894da7634cb8319605a9e05b0d9bbc29746acb247c",
        ),
        "mumps": (
            "http://coin-or-tools.github.io/ThirdParty-Mumps/MUMPS_5.9.1.tar.gz",
            "659c9b57646b5a003ac618baa1faf9dd2044e46c732b3daaccbc7158003e1b46",
        ),
    }

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        # C and Fortran only: the C++ standard and library do not change the binary.
        self.settings.rm_safe("compiler.cppstd")
        self.settings.rm_safe("compiler.libcxx")

    def layout(self):
        basic_layout(self, src_folder="src")

    def requirements(self):
        if self.options.with_metis:
            # ConanCenter's gklib/5.1.1 does not build with MinGW (sys/resource.h).
            self.requires("metis/5.2.1")
        self.requires("openblas/0.3.30")

    def build_requirements(self):
        if self.settings.os == "Linux":
            self.tool_requires("patchelf/0.18")

    def validate(self):
        if self.settings.os == "Windows" and self.settings.compiler != "gcc":
            raise ConanInvalidConfiguration("prototype: Windows only with MinGW gcc (MSVC has no Fortran)")

    @property
    def _fortran(self):
        return self.conf.get("tools.build:compiler_executables", default={}, check_type=dict).get("fortran")

    def source(self):
        url, sha = self._sources["thirdparty"]
        get(self, url, sha256=sha, strip_root=True)
        url, sha = self._sources["mumps"]
        get(self, url, sha256=sha, destination="MUMPS", strip_root=True)
        # What ThirdParty-Mumps/get.Mumps does after downloading.
        patch(self, patch_file=os.path.join(self.source_folder, "mumps_mpi.patch"))
        rename(self, os.path.join(self.source_folder, "MUMPS", "libseq", "mpi.h"),
               os.path.join(self.source_folder, "MUMPS", "libseq", "mumps_mpi.h"))
        patch(self, patch_file=os.path.join(self.source_folder, "mumps_call.patch"))
        patch(self, patch_file=os.path.join(self.source_folder, "mumps_stdalign.patch"))

    @staticmethod
    def _flags(cpp_info):
        cflags = [f"-I{d}" for d in cpp_info.includedirs] + [f"-D{d}" for d in cpp_info.defines]
        lflags = [f"-L{d}" for d in cpp_info.libdirs] + [f"-l{l}" for l in cpp_info.libs + cpp_info.system_libs]
        return " ".join(cflags), " ".join(lflags)

    def generate(self):
        if not self._fortran:
            raise ConanInvalidConfiguration(
                "coin-mumps needs a Fortran compiler: set "
                'coin-mumps/*:tools.build:compiler_executables={"fortran": "/usr/bin/gfortran"}')
        tc = AutotoolsToolchain(self)
        blas = self.dependencies["openblas"].cpp_info.aggregated_components()
        _, blas_l = self._flags(blas)
        if self.options.with_metis:
            # metis needs gklib after it on the link line.
            metis = self.dependencies["metis"].cpp_info.aggregated_components()
            gklib = self.dependencies["gklib"].cpp_info.aggregated_components()
            metis_c, metis_l = self._flags(metis)
            _, gk_l = self._flags(gklib)
            tc.configure_args += [f"--with-metis-cflags={metis_c}", f"--with-metis-lflags={metis_l} {gk_l}"]
        else:
            tc.configure_args.append("--without-metis")
        # Keep the static metis/gklib symbols private to libcoinmumps.
        if self.settings.os == "Linux":
            tc.extra_ldflags.append("-Wl,--exclude-libs,ALL")
        tc.configure_args += [
            f"--with-lapack-lflags={blas_l}",
            "--with-precision=double",
            "--with-intsize=32",
        ]
        env = tc.environment()
        env.define("FC", self._fortran)
        fcflags = "-O2" if self.settings.build_type != "Debug" else "-g"
        if self.settings.os != "Windows" and self.options.get_safe("fPIC", True):
            fcflags += " -fPIC"
        env.define("FCFLAGS", fcflags)
        if self.settings.os == "Windows":
            # The xPack toolchain's libgfortran.la points libtool at a
            # libquadmath.la on xPack's CI machine: name the import libs by path.
            libs = []
            for lib in ("libgfortran.dll.a", "libquadmath.dll.a"):
                out = io.StringIO()
                self.run(f"{self._fortran} -print-file-name={lib}", stdout=out)
                libs.append(os.path.normpath(out.getvalue().strip()))
            env.define("FCLIBS", " ".join(libs))
        tc.generate(env)

    def build(self):
        autotools = Autotools(self)
        autotools.configure()
        autotools.make()

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        copy(self, "LICENSE", os.path.join(self.source_folder, "MUMPS"),
             os.path.join(self.package_folder, "licenses", "MUMPS"))
        autotools = Autotools(self)
        autotools.install()
        rm(self, "*.la", os.path.join(self.package_folder, "lib"))
        if self.options.shared and self.settings.os == "Linux":
            set_origin_rpath(self, os.path.join(self.package_folder, "lib"))
        if not self.options.shared:
            out = io.StringIO()
            self.run(f"{self._fortran} -print-file-name=libgfortran.so", stdout=out)
            save(self, os.path.join(self.package_folder, "res", "fortran_libdir.txt"),
                 os.path.dirname(out.getvalue().strip()))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))

    def package_info(self):
        self.cpp_info.set_property("pkg_config_name", "coinmumps")
        self.cpp_info.libs = ["coinmumps"]
        self.cpp_info.includedirs = [os.path.join("include", "coin-or", "mumps")]
        self.cpp_info.system_libs = ["m", "pthread"]
        # libcoinmumps.so already records NEEDED libgfortran.so.5. Do not add
        # -lgfortran for consumers: their C++ compiler (GCC 15 without
        # Fortran) has no libgfortran.so dev link and the link fails.
        if not self.options.shared:
            # A static libcoinmumps needs the Fortran runtime on the final
            # link line, from a directory GCC 15's driver does not search.
            self.cpp_info.system_libs += ["gfortran", "quadmath"]
            flibdir = load(self, os.path.join(self.package_folder, "res", "fortran_libdir.txt")).strip()
            self.cpp_info.sharedlinkflags = [f"-L{flibdir}"]
            self.cpp_info.exelinkflags = [f"-L{flibdir}"]
