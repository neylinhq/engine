# Лицензии и уведомления

Условия использования собственного кода Engine указаны в корневом `LICENSE`.
Сторонние компоненты сохраняют свои лицензии. Уведомление MIT для кода NAADF
находится в [licenses/naadf.txt](licenses/naadf.txt).
Оригинальные тексты уведомлений не переводятся и включаются в установочный пакет.

Зависимости устанавливаются отдельно. Их требования для сборки описаны в
`docs/build.md` проекта; лицензионные тексты сохранены ниже.

| Зависимость | Назначение | Уведомление |
|---|---|---|
| Boost.JSON | Чтение и запись JSON | [Boost Software License 1.0](licenses/boost.txt) |
| OpenSSL Crypto | SHA-256 файлов и отчётов benchmark | [Apache 2.0 и включённые уведомления](licenses/openssl.txt) |
| GLFW | Окно, события ввода и поверхность Vulkan | [Лицензия GLFW](licenses/glfw.txt) |
| Dear ImGui | Интерфейс Sandbox | [MIT](licenses/imgui.txt) |
| Вспомогательный код в составе ImGui | Внутренние средства ImGui | [Уведомления](licenses/imgui_bundled_stb.txt) |
| ProggyClean | Шрифт ImGui по умолчанию | [Уведомление о шрифте](licenses/imgui_default_font.txt) |
| Vulkan Headers / Vulkan-Hpp | Объявления API Vulkan | [Уведомление репозитория](licenses/vulkan_headers.txt), [Apache 2.0](licenses/apache_2.txt), [MIT](licenses/vulkan_mit.txt) |
| Slang | Компиляция шейдеров при сборке | [Apache 2.0 с исключением LLVM](licenses/slang.txt), [Текст Apache](licenses/apache_2.txt) |

Загрузчик Vulkan и библиотеки операционной системы предоставляет целевое
окружение. Slang не требуется при запуске приложения. Дополнительные кириллические
глифы могут загружаться из установленного системного шрифта; сам системный шрифт
в пакет не включается.

Декодеры изображений, форматов моделей и сжатия находятся в `world.storage`.
Код stb из состава ImGui не используется для импорта изображений движком.
