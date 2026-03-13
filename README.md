# WoOS

Минималистичная 64-битная учебная ОС с графическим framebuffer-интерфейсом, собранная в freestanding-режиме без libc.

## Что уже есть
- Загрузка через `boot.asm` + `stage2.asm`.
- Переход в long mode и запуск ядра.
- Простая графическая подсистема (`fb`) и desktop-style UI (`ui`) с partial redraw через dirty-rectangles.
- Текстовый bitmap-рендер для отображения статуса и версий, плюс счётчик dirty-областей как профилировочная метка кадра.
- Базовый init-flow ядра по стадиям: `early -> platform -> drivers -> ui`.
- Версионированный boot ABI между `stage2` и `kernel` с sanity-check в `kmain`.

## Быстрый старт
```bash
make clean
make os.img
```

Полезные цели:
- `make verify-layout` — быстрый дамп и проверка расположения загрузочного/ядерного кода в образе.
- `make clean` — очистка артефактов.

## Структура проекта
- `boot.asm` — MBR/первичный загрузчик.
- `stage2.asm` — второй этап загрузки и старт ядра.
- `kernel.c` — вход и orchestration базовых подсистем.
- `fb.c/.h` — примитивы framebuffer-рендера.
- `ui.c/.h` — простая отрисовка оболочки.
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
- CI-сборка (`.github/workflows/build.yml`) публикует `os.img` как артефакт Actions.
- Релизный workflow (`.github/workflows/release.yml`) при пуше тега `v*`:
  - достаёт `os.img` из артефактов Actions,
  - генерирует релизные заметки из `CHANGELOG.md`,
  - создаёт GitHub Release и прикладывает образ.
