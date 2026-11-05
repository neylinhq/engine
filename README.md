# Engine

Воксельный движок на C++23, Vulkan и Slang, основанный на
[NAADF](https://github.com/cg-tuwien/NAADF). Поддерживает трассировку лучей,
направленные поля расстояний AADF, редактирование мира, материалы и динамические
сущности. Доступны режимы рендера Albedo, Base и PathTracer.

В проект входят два приложения:

- [Sandbox](apps/sandbox/README.md) — просмотр, импорт и редактирование сцен
  со свободным полётом камеры.
- [Benchmark](apps/benchmark/README.md) — CLI для воспроизводимых сценариев
  движения и редактирования, измерения времени и памяти.

Библиотеки находятся в `components/<group>/<component>`, приложения — в `apps/`.
Публичный API использует пространство имён `engine`, заголовки `<engine/...>`
и цели CMake `engine::group.component`. Карты и результаты измерений хранятся
вне проекта.

- [Сборка и запуск](docs/build.md)
- [Архитектурные правила и archcheck](docs/architecture_rules.md)
- [Контракт benchmark](apps/benchmark/docs/contract.md)
- [Методика измерений](apps/benchmark/docs/methodology.md)
- [Лицензии и уведомления](docs/third_party_notices.md)

Engine — проприетарный проект. Условия использования собственного кода указаны
в [LICENSE](LICENSE); сторонние компоненты сохраняют свои лицензии.
