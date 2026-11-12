# Архитектурные правила

Engine следует редакции 1.0 архитектурного контракта и общему стеку C++.
Документы хранятся в [neylinhq/notes](https://github.com/neylinhq/notes).

- [Архитектурный контракт C++](https://github.com/neylinhq/notes/blob/main/standards/cpp_architecture_contract.md)
- [Конвенция имён](https://github.com/neylinhq/notes/blob/main/standards/project_naming.md)
- [Стек технологий](https://github.com/neylinhq/notes/blob/main/architecture/technology_stack.md)

[archcheck](https://github.com/neylinhq/archcheck) проверяет структуру репозитория,
владение исходниками, зависимости, факты сборки и правила качества C++.
Области проверки и требования к окружению описаны в его README. Инструмент
хранится и собирается отдельно от Engine.

В `architecture.toml` перечислены конфигурации и профили проверки. Файлы
`component.toml` задают владельцев и роли компонентов. CMake регистрирует
исходники, зависимости, ресурсы и тесты через экспортёр установленного archcheck.
Локальный профиль описывает форматы ресурсов и параметры сборки Engine.

После подготовки обеих сборок на Windows выполните из корня проекта:

```powershell
archcheck verify --root . --build out/windows-debug --build out/windows-release `
  --configuration windows/windows-debug --configuration windows/windows-release `
  --scope full --jobs 2 --timings
```

Полный охват проекта включает также `linux/linux-debug` и `linux/linux-release`.
Для отчёта с обеих платформ используйте `archcheck merge` с JSON-отчётами,
полученными на одном состоянии исходников.
