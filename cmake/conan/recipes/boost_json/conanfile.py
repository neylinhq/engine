from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
from conan.tools.files import collect_libs, copy, get
import os


class BoostJson(ConanFile):
    name = "boost-json"
    version = "1.92.0"
    package_type = "library"
    license = "BSL-1.0"
    homepage = "https://www.boost.org/"
    description = "Boost.JSON and its header dependencies from the official Boost release"
    settings = "os", "arch", "compiler", "build_type"
    no_copy_source = True
    options = {"shared": [False], "fPIC": [True, False]}
    default_options = {"shared": False, "fPIC": True}

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def layout(self):
        cmake_layout(self)

    def source(self):
        get(self,
            "https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.gz",
            sha256="f51707c27359a0df0cac1beada86de31bb5eed5e8285592dadec384df99c2984",
            strip_root=True)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.variables["BOOST_INCLUDE_LIBRARIES"] = "json"
        toolchain.variables["BOOST_INSTALL_LAYOUT"] = "system"
        if self.settings.os == "Windows":
            toolchain.variables["BOOST_RUNTIME_LINK"] = "static"
        toolchain.variables["BUILD_TESTING"] = False
        toolchain.variables["BUILD_SHARED_LIBS"] = False
        toolchain.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        copy(self, "LICENSE_1_0.txt", self.source_folder,
             os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "Boost")
        self.cpp_info.components["json"].set_property("cmake_target_name", "Boost::json")
        libraries = collect_libs(self)
        self.cpp_info.components["container"].set_property("cmake_target_name", "Boost::container")
        self.cpp_info.components["container"].libs = [name for name in libraries if "boost_container" in name]
        self.cpp_info.components["json"].libs = [name for name in libraries if "boost_json" in name]
        self.cpp_info.components["json"].requires = ["container"]
        self.cpp_info.components["json"].defines = ["BOOST_ALL_NO_LIB"]
