# Engine: Sandbox и Benchmark

Пакет содержит приложения, скомпилированные шейдеры и примеры сценариев Benchmark.
Карты хранятся отдельно. Для работы нужны совместимый драйвер Vulkan 1.3 и
оконная система. Если приложения собраны с динамическими библиотеками
(например, GLFW, Boost.JSON или средой выполнения C++), эти библиотеки должны
быть доступны на целевой машине. CMake, Slang и заголовки SDK для запуска не нужны.

Сохраняйте каталог `bin/shaders` рядом с приложениями. Следующие команды
выполняются из каталога установленного пакета; в Linux используйте
`./bin/sandbox` и `./bin/benchmark` вместо файлов `.exe`.

## Sandbox

```powershell
.\bin\sandbox.exe
.\bin\sandbox.exe --scene C:\Scenes\world.cvox --voxel-size 1
.\bin\sandbox.exe --help
```

Без `--scene` открывается пустой мир 256 × 256 × 256 вокселей. Через CLI можно
импортировать CVox, VOX, VL32, OBJ и STL. В меню `File > Open` открывается CVox;
для остальных форматов предусмотрены отдельные команды импорта.

`--camera X,Y,Z,YAW,PITCH` задаёт позицию в метрах и углы в радианах.
`--voxel-size` задаёт размер вокселя в метрах. `--settings FILE.json` выбирает
файл настроек; по умолчанию используется `out/user/settings.json`
относительно каталога запуска.

- W / A / S / D — движение, мышь при захвате курсора — обзор.
- Shift — ускорение, Space / Ctrl — подъём / спуск.
- Tab / F1 — панели и захват курсора, Esc — освобождение курсора.
- F11 — оконный / безрамочный режим.
- Левая кнопка — выбранный инструмент, колесо — размер кисти.
- F2 — путь камеры, F7 — переключение Base / PathTracer.

Камера свободно проходит через геометрию и границы мира. Снимки и карты
сохраняйте во внешних каталогах.

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
