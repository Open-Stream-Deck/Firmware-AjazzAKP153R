# Разработка

## Модули

| Файл | Назначение |
| --- | --- |
| `src/main.c` | Запуск, очередь HID-отчётов и обслуживание платы |
| `src/platform.c` | Тактирование, кэши, watchdog и вход в загрузчик |
| `src/timebase.c` | Время через DWT или SysTick |
| `src/keys.c` | Матрица клавиш и подавление дребезга |
| `src/display.c` | SPI, DMA, дисплейный контроллер и рисование |
| `src/usb_device.c` | USB MUSB и транспорт HID |
| `src/usb_descriptors.c` | USB/HID-дескрипторы |
| `src/osd_protocol.c` | Потоковый разбор команд OSD1 |
| `src/boot_anim.c` | Заставка и переход к экрану приложения |
| `src/idle_game.c` | Игра во время ожидания приложения |
| `src/memory.c` | `memcpy` и `memset` |
| `include/profile.h` | Счётчики тактов этапов главного цикла на DWT |
| `startup/startup.S` | Векторы, начальная инициализация и обработчик сбоя |
| `linker/application.ld` | Размещение секций в памяти |

## Проверки

```bash
bash test.sh
bash analyze.sh
bash build.sh
```

Тесты C работают на хосте с AddressSanitizer и UndefinedBehaviorSanitizer. Проверяются команды OSD1, разбиение потока между отчётами, ошибки и SYNC, USB control transfers, Full Speed/High Speed, заполнение очереди, клавиши, таймер, транзакции дисплея, заставка и игра. Тесты Python проверяют скомпилированные HID-дескрипторы, ELF, контрольные суммы и версию.

Аппаратные обращения подменяются через `USB_HOST_TEST`, `KEYS_HOST_TEST`, `TIMEBASE_HOST_TEST` и `DISPLAY_HOST_TEST`. В целевой сборке эти определения отсутствуют.

Тесты на хосте не проверяют электрические задержки, DMA и физическую ориентацию кнопок. После аппаратных изменений проверьте на устройстве запуск, USB, все кнопки и их отпускание, одновременные нажатия, цвета, яркость, заставку, игру и переподключение USB.

GitHub Actions проверяет синтаксис скриптов, форматирование, статический анализ ARM GCC, тесты и ARM-сборку при push и pull request. В Summary запуска доступны результаты и сборочные файлы. Даже при ошибке одной проверки остальные выполняются; решение о слиянии принимается после ручного ревью.

Релиз собирается по тегу `vX.Y.Z`: версия тега должна совпадать с `tools/version.py`, текст релиза берётся из раздела этой версии в `CHANGELOG.md`, к релизу прикладываются BIN, ELF, MAP, `firmware.json` и LICENSE.

## Сборка и стиль

Целевая платформа: ARMv7-M, Thumb, soft-float, C11, freestanding. Основные модули собираются с `-Os`, дисплей и протокол с `-O2`. Предупреждения компилятора считаются ошибками. `CC` задаёт ARM-компилятор, `HOST_CC` задаёт компилятор тестов.

`tools/image.py` проверяет границы ELF-сегментов и таблицу векторов, экспортирует flash-сегменты и заполняет промежутки байтами `FF`. Linker проверяет размер образа и резерв стека.

Версия задаётся в `FIRMWARE_VERSION` файла `tools/version.py`: `MAJOR.MINOR.PATCH`, каждое число от 0 до 255. Она используется в HID-ответе, именах BIN, ELF и MAP и в `firmware.json`.

В Windows без WSL `build.sh` и `test.sh` запускаются из Git Bash, если в `PATH` есть `arm-none-eabi-gcc` из ARM GNU Toolchain, `python3` и `gcc` из MinGW. MinGW не поддерживает AddressSanitizer, поэтому полную проверку тестов с санитайзерами выполняет CI.

Для форматирования нужен clang-format 18:

```bash
sudo apt install clang-format-18
CLANG_FORMAT=clang-format-18 bash format.sh
CLANG_FORMAT=clang-format-18 bash format.sh --check
```

Стиль задают `.clang-format`, `.editorconfig` и `.gitattributes`. Используются UTF-8, LF и отступ в четыре пробела. Описание протокола и аппаратных ограничений находится в `docs/`, комментарии в исходниках не используются.

При рефакторинге сохраняйте порядок volatile-доступов, повторные регистровые записи, барьеры и выравнивание DMA-буферов. Изменение задержек или оптимизации требует проверки на плате.

## Видеопамять заставки и игры

Заставка использует canvas `0x600000`, игра рисует кадр в `0x400000` и копирует готовый результат в canvas заставки. До передачи экрана приложению эти области зарезервированы: `0x400000..0x52C3FF` и `0x600000..0x72C3FF`. Диапазоны учитывают RGB888; в обычном режиме используется RGB565. Кадры не хранятся в RAM микроконтроллера.

## Генерация букв

Для обычной сборки достаточно `include/boot_glyphs.h`. Чтобы изменить буквы заставки, нужны variable Inter (`inter-latin-wght-normal.woff2` из `@fontsource-variable/inter`), Pillow, fontTools и brotli:

```bash
python3 -m venv .venv
. .venv/bin/activate
python3 -m pip install Pillow fontTools brotli
python3 tools/glyphs.py inter-latin-wght-normal.woff2 --weight 600 --size 68
CLANG_FORMAT=clang-format-18 bash format.sh
```

Генератор сохраняет 4-битную альфу, по два пикселя на байт. `--size` задаёт размер шрифта в пикселях.
