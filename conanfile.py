from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class EngineDependencies(ConanFile):
    name = "engine"
    required_conan_version = "==2.34.0"
    settings = "os", "arch", "compiler", "build_type"
    options = {"desktop": [True, False]}
    default_options = {"desktop": True, "*:shared": False}

    def requirements(self):
        self.requires("boost-json/1.92.0@engine/dependencies")
        if self.settings.os != "Windows":
            self.requires("openssl/3.6.3")
        if self.options.desktop:
            self.requires("glfw/3.4")
            self.requires("imgui-sources/1.91.9b@engine/dependencies")
            self.requires("physics/0.1.0@neylinhq/sdk")

    def generate(self):
        CMakeDeps(self).generate()
        toolchain = CMakeToolchain(self)
        if self.settings.os == "Windows":
            toolchain.blocks.remove("vs_runtime")
        toolchain.variables["ENGINE_DESKTOP"] = bool(self.options.desktop)
        toolchain.user_presets_path = False
        toolchain.generate()
