# Архитектурные правила

Общие правила C++-проектов хранятся в [neylinhq/notes](https://github.com/neylinhq/notes):

- [Архитектурный контракт C++](https://github.com/neylinhq/notes/blob/main/standards/cpp_architecture_contract.md)
- [Конвенция имён](https://github.com/neylinhq/notes/blob/main/standards/project_naming.md)

[archcheck](https://github.com/neylinhq/archcheck) проверяет структуру репозитория,
владение исходниками, зависимости, факты сборки и правила качества C++.
Области проверки и требования к окружению описаны в его README. Инструмент
хранится и собирается отдельно от Engine.

В Engine конфигурации проверки объявлены в `architecture.toml`, метаданные
компонентов — в `component.toml`, исходники и зависимости — в CMake.
`cmake/architecture.cmake` экспортирует факты сборки. При изменении структуры
согласованно обновляйте манифесты и CMake. Общий контракт здесь не дублируется.
