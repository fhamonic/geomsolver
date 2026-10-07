from conan import ConanFile
from conan.tools.cmake import CMake
from conan.tools.files import copy


class Recipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    build_policy = "missing"
    # Without the Luksan code (LD_LBFGS, LD_VAR*, LD_TNEWTON*) nlopt is MIT
    # instead of LGPL; none of the algorithms the solver offers need it.
    default_options = {"nlopt/*:enable_luksan": False}

    def requirements(self):
        self.requires("imgui/1.92.5")
        self.requires("implot/0.17")
        self.requires("glfw/3.4")
        self.requires("glew/2.2.0")
        self.requires("nlohmann_json/3.11.3", override=True)
        self.requires("json-schema-validator/2.3.0")
        self.requires("onetbb/2021.12.0")
        self.requires("nlopt/2.11.0")

    def build_requirements(self):
        self.tool_requires("cmake/3.27.1")
        self.test_requires("doctest/2.4.12")

    def generate(self):
        print(
            'conanfile.py: Include directories:\n\t"{}"'.format(
                '",\n\t"'.join(
                    [
                        dir
                        for lib, dep in self.dependencies.items()
                        if lib.headers
                        for dir in dep.cpp_info.includedirs
                    ]
                )
            )
        )

        for lib, dep in self.dependencies.items():
            if lib.ref.name == "imgui":
                imgui_path = dep.package_folder
                break
        else:
            raise ValueError("imgui not found in dependencies")

        copy(
            self,
            "*.cpp",
            imgui_path + "/res/bindings",
            self.source_folder + "/imgui_backends",
        )
        copy(
            self,
            "*.h",
            imgui_path + "/res/bindings",
            self.source_folder + "/imgui_backends",
        )
        copy(
            self,
            "*.ttf",
            imgui_path + "/res/fonts",
            self.source_folder + "/imgui_fonts",
        )

    def build(self):
        cmake = CMake(self)
        # Passed on every configure: an ENABLE_TESTING=OFF cached in the build
        # folder would otherwise survive and leave `make test` with no tests.
        skip = self.conf.get("tools.build:skip_test", check_type=bool)
        cmake.configure(variables={"ENABLE_TESTING": "OFF" if skip else "ON"})
        cmake.build()
