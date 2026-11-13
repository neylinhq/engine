# Engine: Sandbox и Benchmark

Пакет содержит приложения, скомпилированные шейдеры и примеры сценариев Benchmark.
Карты, палитры и настройки хранятся отдельно. Для работы нужны совместимый драйвер и загрузчик
Vulkan 1.3, а также оконная система.

Сохраняйте каталог `bin/shaders` рядом с приложениями. Следующие команды
выполняются из каталога установленного пакета; в Linux используйте
`./bin/sandbox` и `./bin/benchmark` вместо файлов `.exe`.

## Sandbox

```powershell
.\bin\sandbox.exe
.\bin\sandbox.exe --scene C:\Scenes\world.cvox --materials C:\Scenes\world.materials.json --settings C:\Scenes\day.settings.json --voxel-size 1
.\bin\sandbox.exe --help
```

Без `--scene` открывается пустой мир 256 × 256 × 256 вокселей. Через CLI можно
импортировать CVox, VOX, VL32, OBJ и STL. В меню `File > Open` открывается CVox;
для остальных форматов предусмотрены отдельные команды импорта.

`--camera X,Y,Z,YAW,PITCH` задаёт позицию в метрах и углы в радианах.
`--voxel-size` задаёт размер вокселя в метрах. `--materials FILE.materials.json`
заменяет палитру карты, указанной в `--scene`. Без `--materials` используется
палитра карты. `--settings FILE.settings.json` загружает настройки рендера
и атмосферы. Если файл не указан, действуют встроенные значения. Аргумент
`--mode` задаёт режим рендера поверх настроек. Кнопка `Save As` в панели
Settings сохраняет действующие значения в выбранный файл.

- W / A / S / D — движение, мышь при захвате курсора — обзор.
- В полёте Shift ускоряет камеру, Space / Ctrl поднимают и опускают её.
- F переключает ходьбу и полёт. В режиме ходьбы Shift включает бег, Space — прыжок.
- Tab / F1 — панели и захват курсора, Esc — освобождение курсора.
- F11 — оконный / безрамочный режим.
- Левая кнопка — выбранный инструмент, колесо — размер кисти.
- F2 — путь камеры, F7 — переключение Base / PathTracer.

Приложение запускается в режиме свободного полёта. `--walk` включает ходьбу
при запуске. Персонаж сталкивается с геометрией воксельного мира. Кисть и штамп
отклоняют правку, если новые воксели пересекут капсулу персонажа.
Снимки и карты сохраняйте во внешних каталогах.

## Benchmark

Benchmark выполняет сценарии движения, редактирования и работы с сущностями
без интерактивных панелей. Для исполнения сценария ему нужна поверхность Vulkan,
в том числе в режиме `--hidden`.

```powershell
.\bin\benchmark.exe validate --scenario .\benchmark\scenarios\strata_16k.json --scene C:\Scenes\strata-03125.cvox
.\bin\benchmark.exe run --scenario .\benchmark\scenarios\strata_16k.json --scene C:\Scenes\strata-03125.cvox --output C:\Measurements\strata --repetitions 5
.\bin\benchmark.exe compare --baseline C:\Measurements\before --candidate C:\Measurements\after
```

Каждый повтор запускается новым процессом; выходной каталог должен быть новым.
Отчёт содержит времена кадров и операций, временные метки GPU, показатели памяти
и сведения об окружении. Подробные определения метрик и формат сценария находятся
в `benchmark/docs/contract.md`, принципы сравнения — в
`benchmark/docs/methodology.md`. Схемы находятся в `benchmark/schemas`,
примеры — в `benchmark/scenarios`.

Условия использования Engine находятся в `LICENSE`, уведомления сторонних
компонентов — в `THIRD_PARTY_NOTICES.md` и каталоге `licenses`.
