# Сборка и запуск

## Зависимости

Для полной сборки нужны:

- CMake 3.27+, Ninja и компилятор C++23 со стандартной библиотекой,
  поддерживающей `std::expected`, `std::jthread` и `std::from_chars`
  для вещественных чисел;
- Boost 1.83+ с собранной библиотекой Boost.JSON;
- OpenSSL Crypto; CMake запрашивает статическую библиотеку;
- Vulkan 1.3: заголовки, загрузчик и драйвер;
- Slang с программой `slangc`;
- GLFW 3.4+;
- исходники Dear ImGui **1.91.9b**. Эта версия проверяется при конфигурации.

Требования к возможностям GPU перечислены в
[документации рендерера](../components/rendering/vulkan/README.md).

CMake не скачивает зависимости. Установите их отдельно и передайте пути через
параметры конфигурации или локальный `CMakeUserPresets.json`, который не входит
в Git. Пресеты `debug` и `release` используют Ninja и каталоги `out/debug`
и `out/release`. Команды ниже выполняются из корня проекта.

## Windows

Откройте Developer PowerShell для MSVC. Замените пути в примере на свои:

```powershell
cmake --preset release `
  "-DCMAKE_PREFIX_PATH=C:/SDK/Boost;C:/SDK/GLFW" `
  -DOPENSSL_ROOT_DIR=C:/SDK/OpenSSL `
  -DENGINE_IMGUI_SOURCE_DIR=C:/SDK/imgui-1.91.9b `
  -DSLANGC_EXECUTABLE=C:/SDK/slang/bin/slangc.exe
cmake --build --preset release --parallel
ctest --preset release
.\out\release\bin\sandbox.exe
```

Для Debug используйте пресет `debug` при конфигурации, сборке и запуске CTest.
Параметры внешних зависимостей задаются отдельно для каждой конфигурации.
Компиляция MSVC использует `/utf-8`.

## Linux

```sh
cmake --preset release \
  -DCMAKE_PREFIX_PATH="/path/to/boost;/path/to/glfw" \
  -DOPENSSL_ROOT_DIR=/path/to/openssl \
  -DENGINE_IMGUI_SOURCE_DIR=/path/to/imgui-1.91.9b \
  -DSLANGC_EXECUTABLE=/path/to/slang/bin/slangc
cmake --build --preset release --parallel
ctest --preset release
./out/release/bin/sandbox
```

Если Vulkan не найден автоматически, задайте `Vulkan_INCLUDE_DIR` и
`Vulkan_LIBRARY`. Slang должен находить свои динамические библиотеки через
окружение загрузчика ОС. Для запуска приложений нужна оконная система с
поддержкой Vulkan; `--hidden` создаёт скрытое окно и не устраняет это требование.

## Проверки

`-DENGINE_GPU_TESTS=ON` при конфигурации добавляет интеграционные GPU-тесты:
им нужны дисплей, совместимое устройство и `VK_LAYER_KHRONOS_validation`.
Для проверки синхронизации задайте переменную окружения
`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`.

Пресет `sanitizers` включает AddressSanitizer и UndefinedBehaviorSanitizer
для GCC/Clang. Совместный режим этих санитайзеров не поддерживается сборкой MSVC.
`-DENGINE_DESKTOP=OFF` исключает приложения и компоненты, которым нужны Vulkan,
GLFW и ImGui; Boost.JSON и OpenSSL Crypto по-прежнему требуются.

Архитектуру проверяет отдельный [archcheck](architecture_rules.md).
Для измерений времени используйте Release без слоя валидации Vulkan и санитайзеров;
методика описана в [документации Benchmark](../apps/benchmark/docs/methodology.md).
Логи и результаты измерений сохраняйте вне проекта.

## Установка

```powershell
cmake --install out/release --prefix C:/Apps/Engine
```

Пакет содержит Sandbox, Benchmark, каталог `bin/shaders`, сценарии и схемы
benchmark, инструкции и лицензионные уведомления. Карты загружаются из внешних
файлов. Сохраняйте `shaders` рядом с исполняемыми файлами при переносе пакета.

Установка не копирует DLL импортированных зависимостей. Если GLFW, Boost или
среда выполнения C++ связаны динамически, обеспечьте доступность соответствующих
библиотек на целевой машине. Slang, CMake и заголовки SDK для запуска не нужны.

Параметры приложений и примеры:
[Sandbox](../apps/sandbox/README.md), [Benchmark](../apps/benchmark/README.md).
