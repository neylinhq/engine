from pathlib import Path

from conan import ConanFile
from conan.tools.files import copy, get


class ImGuiSources(ConanFile):
    name = "imgui-sources"
    version = "1.91.9b"
    package_type = "header-library"
    license = "MIT"
    homepage = "https://github.com/ocornut/imgui"
    description = "Dear ImGui sources and official GLFW and Vulkan backends"
    no_copy_source = True

    def source(self):
        get(
            self,
            url=f"https://codeload.github.com/ocornut/imgui/tar.gz/refs/tags/v{self.version}",
            filename="imgui.tar.gz",
            sha256="8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb",
            strip_root=True,
        )

    def package(self):
        source = Path(self.source_folder)
        package = Path(self.package_folder)
        for pattern in ("imgui*.h", "imstb*.h", "imconfig.h", "imgui.cpp", "imgui_draw.cpp",
                        "imgui_tables.cpp", "imgui_widgets.cpp"):
            copy(self, pattern, src=source, dst=package / "include", excludes=("*/**",))
        for backend in ("imgui_impl_glfw", "imgui_impl_vulkan"):
            copy(self, f"{backend}.*", src=source / "backends", dst=package / "include/backends")
        copy(self, "LICENSE.txt", src=source, dst=package / "licenses", excludes=("*/**",))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "ImGuiSources")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
