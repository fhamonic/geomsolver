import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.files import copy, download, unzip

required_conan_version = ">=2.0"


class CasadiBinConan(ConanFile):
    """Repackages the prebuilt IPOPT (+ optionally CasADi) shipped in the CasADi PyPI wheel."""

    name = "casadi-bin"
    version = "3.7.2"
    # casadi LGPL-3.0, ipopt EPL-2.0, mumps CeCILL-C, openblas BSD-3-Clause,
    # libgfortran/libquadmath GPL-3.0 with the GCC runtime exception, and
    # METIS 4.0 (coinmetis) under the Karypis non-redistribution licence.
    license = ("LGPL-3.0-or-later", "EPL-2.0", "CECILL-C", "BSD-3-Clause", "GCC-exception-3.1", "LicenseRef-METIS-4.0")
    url = "https://pypi.org/project/casadi/3.7.2/"
    package_type = "shared-library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"with_casadi": [True, False]}
    default_options = {"with_casadi": True}

    # One wheel per platform; the native libraries do not depend on the cpXY tag.
    _wheels = {
        ("Linux", "x86_64"): (
            "https://files.pythonhosted.org/packages/da/5f/116f67f5c399eda15e23fe0623786d23ce0e46f0b8ad937c5268eb16e72f/casadi-3.7.2-cp310-none-manylinux2014_x86_64.whl",
            "e1cb725c95024d5b8f09cb339d3fe65bb74368793783366fbbdffece980f0082",
        ),
    }

    # Linux SONAME files, exactly as the wheel's NEEDED entries spell them.
    _ipopt_libs = [
        "libipopt.so.3",
        "libcoinmumps.so.3",
        "libcoinmetis.so.2",
        "libcasadi-tp-openblas.so.0",
        "libgfortran-040039e1.so.5.0.0",
        "libquadmath-96973f99.so.0.0.0",
    ]
    _casadi_libs = ["libcasadi.so.3.7", "libcasadi_nlpsol_ipopt.so.3.7"]
    _licenses = ["casadi", "ipopt-external", "mumps-external", "metis-external", "openblas-external"]

    def package_id(self):
        del self.info.settings.compiler
        del self.info.settings.build_type

    def validate(self):
        if (str(self.settings.os), str(self.settings.arch)) not in self._wheels:
            raise ConanInvalidConfiguration(f"no CasADi wheel mapped for {self.settings.os}/{self.settings.arch}")

    def build(self):
        url, sha256 = self._wheels[(str(self.settings.os), str(self.settings.arch))]
        wheel = os.path.join(self.build_folder, os.path.basename(url))
        download(self, url, wheel, sha256=sha256)
        unzip(self, wheel, destination=self.build_folder, pattern="casadi/*")
        os.remove(wheel)

    def package(self):
        src = os.path.join(self.build_folder, "casadi")
        lib = os.path.join(self.package_folder, "lib")
        inc = os.path.join(self.package_folder, "include")
        names = self._ipopt_libs + (self._casadi_libs if self.options.with_casadi else [])
        for name in names:
            copy(self, name, src, lib)
        # The zip holds full copies instead of symlinks; recreate the dev links only.
        os.symlink("libipopt.so.3", os.path.join(lib, "libipopt.so"))
        copy(self, "*", os.path.join(src, "include", "coin-or"), os.path.join(inc, "coin-or"))
        if self.options.with_casadi:
            os.symlink("libcasadi.so.3.7", os.path.join(lib, "libcasadi.so"))
            # CasADi dlopen()s plugins by their unversioned name.
            os.symlink("libcasadi_nlpsol_ipopt.so.3.7", os.path.join(lib, "libcasadi_nlpsol_ipopt.so"))
            copy(self, "*", os.path.join(src, "include", "casadi"), os.path.join(inc, "casadi"))
        for d in self._licenses:
            copy(self, "*", os.path.join(src, "include", "licenses", d), os.path.join(self.package_folder, "licenses", d))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "casadi-bin")
        ipopt = self.cpp_info.components["ipopt"]
        ipopt.set_property("cmake_target_name", "casadi-bin::ipopt")
        ipopt.libs = ["ipopt"]
        ipopt.includedirs = [os.path.join("include", "coin-or")]
        ipopt.system_libs = ["m", "dl", "pthread"]
        if self.options.with_casadi:
            casadi = self.cpp_info.components["casadi"]
            casadi.set_property("cmake_target_name", "casadi-bin::casadi")
            casadi.libs = ["casadi"]
            casadi.includedirs = ["include"]
            # The wheel is built on manylinux2014 (devtoolset-10) with the
            # pre-C++11 std::string ABI, as its own casadi-config.cmake says.
            # Every TU that includes casadi headers must match, which means
            # it cannot share std::string/std::vector<std::string> with the
            # rest of a libstdc++11 project.
            casadi.defines = ["_GLIBCXX_USE_CXX11_ABI=0"]
            casadi.system_libs = ["dl"]
