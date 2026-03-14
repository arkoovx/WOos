# WoOS

64-битная ОС с графическим framebuffer-интерфейсом, собранная в freestanding-режиме без libc.

## Что уже есть
- Загрузка через `boot.asm` + `stage2.asm`.
- Переход в long mode и запуск ядра.
- Простая графическая подсистема (`fb`) и desktop-style UI (`ui`) с partial redraw через dirty-rectangles.
- Добавлено базовое состояние курсора с clamp-to-screen и отрисовкой через локальный save/restore фонового блока.
- Добавлена очередь событий input (ring buffer) и базовый dispatcher для `mouse move` / `mouse button` / `timer tick`.
- Добавлен polling-драйвер PS/2-мыши (`mouse`) с инициализацией через 8042 и разбором 3-байтовых пакетов в реальные UI-события.
- Реализованы UI-handlers для hover/click по верхней панели (интерактивная подсветка и статус в footer).
- Текстовый bitmap-рендер для отображения статуса и версий, плюс счётчик dirty-областей как профилировочная метка кадра.
- Базовый init-flow ядра по стадиям: `early -> platform -> drivers -> ui`.
- Добавлен IDT skeleton (`idt`) с загрузкой таблицы дескрипторов и безопасным default-обработчиком для базовой платформенной инициализации.
- Добавлен модуль heartbeat-таймера (`timer`) и вывод его состояния в UI (строка `HEARTBEAT` в footer).
- Версионированный boot ABI между `stage2` и `kernel` с sanity-check в `kmain`.

## Быстрый старт
```bash
make clean
make os.img
```

Для сборки с двойной буферизацией:
```bash
make clean
make os.img DBL_BUFFER=1
```

Полезные цели:
- `make verify-layout` — быстрый дамп и проверка расположения загрузочного/ядерного кода в образе.
- `make clean` — очистка артефактов.

## Структура проекта
- `boot.asm` — MBR/первичный загрузчик.
- `stage2.asm` — второй этап загрузки и старт ядра.
- `kernel.c` — вход и orchestration базовых подсистем.
- `fb.c/.h` — примитивы framebuffer-рендера.
- `ui.c/.h` — отрисовка оболочки и обработка базовых UI-событий.
- `input.c/.h` — очередь событий и input dispatcher contract.
- `idt.c/.h`, `idt_asm.asm` — каркас подсистемы прерываний (IDT load + default stub).
- `timer.c/.h` — программный heartbeat-таймер для событий `timer tick`.
- `mouse.c/.h` — polling-драйвер PS/2-мыши и трансляция пакетов в очередь input.
- `DEVELOPMENT_PLAN.md` — расширенный поэтапный roadmap.

## Процесс разработки
В репозитории действует правило: **каждый PR = новая версия**.

Перед PR:
1. Обновите `VERSION`.
2. Добавьте секцию в `CHANGELOG.md`.
3. Актуализируйте документацию (например, `DEVELOPMENT_PLAN.md`).
4. Убедитесь, что `make os.img` выполняется без ошибок.

Подробности — в `RELEASE_CHECKLIST.md`.

## Релизы
- CI-сборка (`.github/workflows/build.yml`) публикует `os.img` как артефакт Actions для проверки изменений в PR.
- Релизный workflow (`.github/workflows/release.yml`) автоматически запускается после merge PR и публикует релиз только для одобренного PR (есть минимум один `APPROVED` review):
  - собирает `os.img` из merge-коммита,
  - берёт версию из `VERSION` и извлекает описание из соответствующей секции `CHANGELOG.md`,
  - создаёт GitHub Release с тегом `vX.Y.Z` и прикладывает `os.img`.
