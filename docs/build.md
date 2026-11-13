# Сборка и запуск

Нужны LLVM 23.1.5, CMake и CTest 4.4.5, Ninja 1.13.2, sccache 0.19.0 и
Conan 2.34.0. Установите [archcheck](https://github.com/neylinhq/archcheck)
с экспортёром CMake для контракта 1.0 и укажите префикс установки в
`ENGINE_ARCHCHECK_ROOT`. В `ENGINE_LLVM_ROOT` укажите каталог LLVM.
Добавьте каталоги инструментов в `PATH`.

Conan устанавливает Boost.JSON 1.92.0. Для настольной сборки он также
устанавливает GLFW 3.4, исходники Dear ImGui 1.91.9b
с официальными backend GLFW и Vulkan, а также Neylin Physics.
Ревизии рецептов закреплены в
`conan.lock`. Системный Vulkan SDK предоставляет заголовки, загрузчик и
`slangc`. Требования к устройству перечислены в
[документации рендерера](../components/rendering/vulkan/README.md).

Команды выполняются из корня Engine. В новом кэше Conan один раз
зарегистрируйте локальные рецепты:

```sh
conan export cmake/conan/recipes/boost_json --user=engine --channel=dependencies
conan export cmake/conan/recipes/imgui_sources --user=engine --channel=dependencies
```

Для настольной сборки нужны Git 2.49 или новее и доступ по SSH к репозиторию
[Neylin Physics](https://github.com/neylinhq/physics).
Скрипт загружает закреплённый коммит SDK в `out/dependencies/` и экспортирует
его рецепт в кэш Conan. При установке зависимостей Conan использует ревизию
из `conan.lock` и собирает пакет для выбранного профиля, если его ещё нет в кэше.

```sh
cmake -P cmake/conan/export_physics.cmake
```

## Windows x64

Откройте Developer PowerShell для Visual C++ Build Tools. `clang-cl` использует
MSVC STL и Windows SDK, а программы линкуются через `lld-link`. Debug и Release
используют статическую Release CRT, совместимую с общими пакетами Conan.
Debug собирается без оптимизации и с отладочной информацией.
Для SHA-256 Engine использует системный CNG.

```powershell
$env:ENGINE_LLVM_ROOT = 'C:/SDK/llvm-23.1.5'
$env:ENGINE_ARCHCHECK_ROOT = 'C:/SDK/archcheck'
conan install . -pr:h=cmake/conan/profiles/windows.profile -pr:b=cmake/conan/profiles/windows.profile -of=out/windows-release/conan --build=missing
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
.\out\windows-release\bin\sandbox.exe
```

Для Debug при установке Conan добавьте `-s:h build_type=Debug` и замените
`windows-release` на `windows-debug` во всех командах.

## Linux x86-64

`clang++` использует libstdc++ и LLD. Для SHA-256 Conan устанавливает
OpenSSL Crypto 3.6.3. Для настольной сборки установите системные
зависимости GLFW для X11 и Vulkan SDK. Загрузчик ОС должен находить динамические
библиотеки Slang и Vulkan.

```sh
export ENGINE_LLVM_ROOT=/opt/llvm-23.1.5
export ENGINE_ARCHCHECK_ROOT=/opt/archcheck
export PATH="$ENGINE_LLVM_ROOT/bin:$PATH"
conan install . -pr:h=cmake/conan/profiles/linux.profile -pr:b=cmake/conan/profiles/linux.profile -of=out/linux-release/conan --build=missing
cmake --preset linux-release
cmake --build --preset linux-release --parallel
ctest --preset linux-release
./out/linux-release/bin/sandbox
```

Для Debug добавьте `-s:h build_type=Debug` и используйте `linux-debug`.
Для локальной сборки CPU-компонентов добавьте `-o '&:desktop=False'` к команде
Conan. Чтобы запустить AddressSanitizer и UndefinedBehaviorSanitizer вместе,
установите зависимости Debug в `out/sanitizers/conan` и используйте пресет
`sanitizers`.

## Проверки

Пресеты Windows включают GPU-тесты. Им нужны дисплей, совместимое устройство
и `VK_LAYER_KHRONOS_validation`. В Linux их можно включить параметром
`-DENGINE_GPU_TESTS=ON`. Для проверки синхронизации задайте
`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`.

Порядок архитектурной проверки описан в [docs/architecture_rules.md](architecture_rules.md).
Для замеров используйте Release. Условия запуска и формат отчётов описаны в
[методике Benchmark](../apps/benchmark/docs/methodology.md).
Логи и результаты сохраняйте вне проекта.

## Установка

```powershell
cmake --install out/windows-release --prefix C:/Apps/Engine
```

В пакет входят Sandbox, Benchmark, шейдеры, сценарии и схемы Benchmark,
инструкции и лицензии. При переносе пакета сохраняйте каталог `bin/shaders`
рядом с исполняемыми файлами. Карты загружаются из внешних файлов.
Для запуска нужны совместимый драйвер и загрузчик Vulkan.

Параметры и примеры запуска приведены в документации
[Sandbox](../apps/sandbox/README.md) и [Benchmark](../apps/benchmark/README.md).
