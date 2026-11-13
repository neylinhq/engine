# Benchmark

Консольное приложение для воспроизводимых измерений Engine. Сценарий задаёт
траекторию камеры, редактирование и операции с сущностями. Инструмент измеряет
время кадров и операций, интервалы GPU и память. Интерактивных панелей нет;
для исполнения нужна поверхность Vulkan и оконная система.

Пример из корня проекта после сборки:

```powershell
.\out\windows-release\bin\benchmark.exe validate --scenario .\apps\benchmark\resources\scenarios\smoke.json
.\out\windows-release\bin\benchmark.exe run --scenario .\apps\benchmark\resources\scenarios\smoke.json --output C:\Measurements\smoke --repetitions 5
.\out\windows-release\bin\benchmark.exe compare --baseline C:\Measurements\before --candidate C:\Measurements\after
```

`validate` проверяет сценарий и входные файлы без создания GPU-контекста.
`run` запускает независимые рабочие процессы. `compare` проверяет совместимость
нагрузок и среды, затем сравнивает результаты отдельных процессов.
`--scene`, `--materials` и `--settings` переопределяют соответствующие входные файлы.
`--help` выводит команды и параметры.

Карты, палитры, настройки и результаты хранятся вне проекта. Выходной каталог прогона должен быть
новым. Для измерения времени используйте Release без `--validation`; проверку корректности
выполняйте отдельно.

- [Контракт сценариев и отчётов](docs/contract.md)
- [Методика измерений](docs/methodology.md)
- [JSON-схемы](schemas)
- [Примеры сценариев](resources/scenarios)
