import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, get, rm, rmdir
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


class CoinIpoptConan(ConanFile):
    name = "coin-ipopt"
    version = "3.14.20"
    license = "EPL-2.0"
    url = "https://github.com/coin-or/Ipopt"
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": True, "fPIC": True, "openblas/*:shared": True}

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def layout(self):
        basic_layout(self, src_folder="src")

    def requirements(self):
        self.requires("coin-mumps/3.0.14")
        self.requires("openblas/0.3.30")

    def build_requirements(self):
        if self.settings.os == "Linux":
            self.tool_requires("patchelf/0.18")

    def validate(self):
        if self.settings.os == "Windows" and self.settings.compiler != "gcc":
            raise ConanInvalidConfiguration("prototype: Windows only with MinGW gcc (MSVC has no Fortran)")

    def source(self):
        get(self, "https://github.com/coin-or/Ipopt/archive/refs/tags/releases/3.14.20.tar.gz",
            sha256="43bddd6fa793b1694aa94d6129fe3e4a8d452d97a84ef5f2ff721c8047c75605",
            strip_root=True)

    def generate(self):
        tc = AutotoolsToolchain(self)
        mumps = self.dependencies["coin-mumps"].cpp_info.aggregated_components()
        blas = self.dependencies["openblas"].cpp_info.aggregated_components()
        mumps_c = " ".join(f"-I{d}" for d in mumps.includedirs)
        mumps_parts = [mumps]
        if not self.dependencies["coin-mumps"].options.shared and self.dependencies["coin-mumps"].options.with_metis:
            # A static libcoinmumps.a carries none of its own dependencies.
            mumps_parts += [self.dependencies[d].cpp_info.aggregated_components() for d in ("metis", "gklib")]
        mumps_l = " ".join(mumps.sharedlinkflags
                           + [f"-L{d}" for c in mumps_parts for d in c.libdirs]
                           + [f"-l{l}" for c in mumps_parts for l in c.libs]
                           + [f"-l{l}" for c in mumps_parts for l in c.system_libs])
        blas_l = " ".join([f"-L{d}" for d in blas.libdirs] + [f"-l{l}" for l in blas.libs + blas.system_libs])
        # Static MUMPS/openblas/metis end up inside libipopt: keep them private.
        if self.settings.os == "Linux":
            tc.extra_ldflags.append("-Wl,--exclude-libs,ALL")
        tc.configure_args += [
            f"--with-mumps-cflags={mumps_c}",
            f"--with-mumps-lflags={mumps_l}",
            f"--with-lapack-lflags={blas_l}",
            # openblas carries a C LAPACK: no Fortran compiler needed here.
            "--disable-f77",
            "--without-hsl",
            "--without-asl",
            "--without-spral",
            "--disable-java",
            "--disable-sipopt",
            "--disable-pardisomkl",
        ]
        tc.generate()

    def build(self):
        autotools = Autotools(self)
        autotools.configure()
        autotools.make()

    def package(self):
        copy(self, "LICENSE", self.source_folder, os.path.join(self.package_folder, "licenses"))
        autotools = Autotools(self)
        autotools.install()
        rm(self, "*.la", os.path.join(self.package_folder, "lib"))
        if self.settings.os == "Linux":
            set_origin_rpath(self, os.path.join(self.package_folder, "lib"))
        rmdir(self, os.path.join(self.package_folder, "lib", "pkgconfig"))
        rmdir(self, os.path.join(self.package_folder, "share"))

    def package_info(self):
        self.cpp_info.set_property("pkg_config_name", "ipopt")
        self.cpp_info.set_property("cmake_file_name", "ipopt")
        self.cpp_info.set_property("cmake_target_name", "ipopt::ipopt")
        self.cpp_info.libs = ["ipopt"]
        self.cpp_info.includedirs = [os.path.join("include", "coin-or")]
        self.cpp_info.system_libs = ["m", "dl", "pthread"]
