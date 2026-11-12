{% set llvm = os.environ["ENGINE_LLVM_ROOT"] %}
[settings]
os=Linux
arch=x86_64
compiler=clang
compiler.version=23
compiler.cppstd=23
compiler.libcxx=libstdc++11
build_type=Release
[conf]
tools.build:compiler_executables={"c": "{{ llvm }}/bin/clang", "cpp": "{{ llvm }}/bin/clang++"}
tools.cmake.cmaketoolchain:generator=Ninja
tools.cmake.cmaketoolchain:user_toolchain=["{{ profile_dir }}/../../toolchain.cmake"]
[platform_tool_requires]
cmake/4.4.5
ninja/1.13.2
