{% set llvm = os.environ["ENGINE_LLVM_ROOT"].replace("\\", "/") %}
[settings]
os=Windows
arch=x86_64
compiler=clang
compiler.version=23
compiler.cppstd=23
compiler.runtime=static
compiler.runtime_type=Release
compiler.runtime_version=v145
build_type=Release
boost-json/*:build_type=Release
glfw/*:build_type=Release
[conf]
tools.build:compiler_executables={"c": "{{ llvm }}/bin/clang-cl.exe", "cpp": "{{ llvm }}/bin/clang-cl.exe"}
tools.cmake.cmaketoolchain:generator=Ninja
tools.cmake.cmaketoolchain:user_toolchain=["{{ (profile_dir + '/../../toolchain.cmake').replace('\\', '/') }}"]
[platform_tool_requires]
cmake/4.4.5
ninja/1.13.2
