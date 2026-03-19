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
- Добавлен модуль heartbeat-таймера (`timer`) на базе аппаратного PIT с polling-сэмплированием счётчика и выводом состояния в UI (строка `HEARTBEAT` в footer).
- Добавлена базовая IRQ-обвязка для клавиатуры и мыши: remap PIC, отдельные обработчики `IRQ1`/`IRQ12` и счётчики прерываний в footer UI (в текущей штатной конфигурации линии PIC остаются замаскированными, а мышь работает через polling-путь драйвера).
- IDT теперь содержит IRQ-stub'ы для всего диапазона PIC (`32..47`), чтобы spurious/неожиданные IRQ корректно подтверждались через `EOI` и не провоцировали нестабильный boot.
- Добавлен модуль `drivers/virtio_gpu_renderer`: renderer-path для `virtio-gpu` с draw-командами (`fill/rect/glyph`), отдельной RAM draw-surface и публикацией dirty-rect через virtqueue (`TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH`) с безопасным fallback на software framebuffer.
- В `virtio_gpu_renderer` добавлена защита от MMIO-адресов выше 4 ГиБ (вне текущего identity-map): в таком случае драйвер автоматически остаётся на software framebuffer fallback, чтобы избежать page fault и циклического reset.
- `virtio-gpu` путь включён по умолчанию (`VIRTIO_GPU=1`); для fallback-диагностики его можно выключить флагом `VIRTIO_GPU=0`.
- Аппаратные IRQ-path включены по умолчанию (`HW_INTERRUPTS=1`), но флаг `HW_INTERRUPTS` сохранён для переключения в диагностический polling-режим.
- В `idt` для штатного режима все линии PIC оставлены замаскированными: это исключает спорадические IRQ в VM, при этом мышь и heartbeat продолжают работать через polling.
- Версионированный boot ABI между `stage2` и `kernel` с sanity-check в `kmain`.
- Исправлен рендер под разные framebuffer-форматы (`16/24/32 bpp`), убраны визуальные полосы на фоне и артефакты курсора.
- Исправлена причина циклической перезагрузки при исключениях: добавлен отдельный IDT-stub для векторов с аппаратным `error code`, чтобы корректно возвращаться через `iretq` без каскада `#GP/#DF`.
- Добавлен базовый heap-аллокатор ядра (`kheap`) и перевод очереди input-событий на runtime-буфер с fallback на статический путь.
- Добавлен базовый PMM (`pmm`) с E820 memory map из `stage2`: ядро получает список usable-регионов BIOS и поднимает stack-based allocator физических страниц 4 КиБ.
- В footer UI добавлен runtime-overlay: dirty-rect count последнего кадра, heap usage/free, PMM total/free pages и активный video-path (`VIRTIO`/`VBE`) для быстрой диагностики подсистем.
- В `kheap` исправлены расчёт split-блока и гарантия 16-байтного выравнивания payload-указателей.
- Исправлен bootloader: чтение payload из диска теперь выполняется chunked-подходом (до 127 секторов за INT13 call), что устраняет `Disk error` на части BIOS/QEMU-конфигураций при росте ядра.
- Уточнён boot-fix: исправлён расчёт следующего LBA при chunked-чтении (исключено чтение «лишних» байтов из структуры состояния), что убирает повторный `Disk error`.

## Быстрый старт
```bash
make clean
make os.img
```

По умолчанию `virtio-gpu` renderer включён (`VIRTIO_GPU=1`).
Для диагностики можно собрать безопасный fallback-профиль:
```bash
make clean
make os.img VIRTIO_GPU=0
```

Аппаратные IRQ (`sti`) в стандартной сборке включены (`HW_INTERRUPTS=1`).
Для диагностики можно принудительно выключить IRQ-path:
```bash
make clean
make os.img HW_INTERRUPTS=0
```

Двойная буферизация теперь включена по умолчанию (чтобы снизить мерцание UI).

Если нужно принудительно отключить её для диагностики:
```bash
make clean
make os.img DBL_BUFFER=0
```

Полезные цели:
- `make verify-layout` — быстрый дамп и проверка расположения загрузочного/ядерного кода в образе.
- `make clean` — очистка артефактов.

Запуск в QEMU (рекомендуется, базовый режим):
```bash
qemu-system-x86_64 \
  -drive format=raw,file=os.img \
  -m 1024 \
  -smp 4 \
  -enable-kvm \
  -cpu host \
  -vga virtio \
  -display sdl,gl=on \
  -usb -device usb-mouse -device usb-kbd \
  -net nic -net user \
  -monitor stdio
```

Если у вас `virtio-vga` с virgl не поднимается через `-vga virtio`, используйте более явный вариант:
```bash
qemu-system-x86_64 \
  -drive format=raw,file=os.img \
  -m 1024 \
  -smp 4 \
  -enable-kvm \
  -cpu host \
  -device virtio-vga-gl \
  -display sdl,gl=on \
  -usb -device usb-mouse -device usb-kbd \
  -net nic -net user \
  -monitor stdio
```

Важно: в текущем состоянии WoOS использует 2D command/render-path поверх `virtio-gpu` (без полноценного userspace 3D stack). UI отправляет draw-команды в renderer, который обновляет backing resource и отправляет dirty-rect в virtqueue. Если `virtio-gpu`/modern transport недоступен, автоматически остаётся software framebuffer-path от `stage2`.

Если в гостевой системе всё «как 1 FPS», чаще всего проблема в медленной эмуляции (TCG без аппаратного ускорения) и/или слишком больших задержках в основном цикле ядра.

## Структура проекта

### Ключевые исходники ядра
- `boot.asm` — MBR/первичный загрузчик.
- `stage2.asm` — второй этап загрузки и старт ядра.
- `kernel.c` — вход и orchestration базовых подсистем.
- `fb.c/.h` — примитивы framebuffer-рендера.
- `ui.c/.h` — отрисовка оболочки и обработка базовых UI-событий.
- `input.c/.h` — очередь событий и input dispatcher contract (с runtime-размещением буфера через `kheap`).
- `idt.c/.h`, `idt_asm.asm` — каркас подсистемы прерываний (IDT load + IRQ stubs для keyboard/mouse).
- `timer.c/.h` — heartbeat-таймер на базе аппаратного PIT с polling-сэмплированием счётчика для событий `timer tick`.
- `kheap.c/.h` — базовый heap-аллокатор ядра для внутренних runtime-структур.
- `pmm.c/.h` — базовый physical memory manager поверх BIOS E820 memory map.
- `mouse.c/.h` — polling-драйвер PS/2-мыши и трансляция пакетов в очередь input.
- `pci.c/.h` — минимальный доступ к PCI config space и поиск устройств.
- `drivers/virtio_gpu_renderer/virtio_gpu_renderer.c/.h` — renderer-драйвер virtio-gpu с command-oriented draw API, virtqueue-flush dirty-rect и fallback на stage2 framebuffer.

### Как читать документацию по порядку
- `docs/README.md` — карта документации и рекомендуемый порядок чтения по файлам.
- `DEVELOPMENT_PLAN.md` — расширенный поэтапный roadmap и статус этапов.
- `docs/ARCHITECTURE.md` — архитектурный центр проекта: слои, init-flow и контракты.
- `docs/MEMORY.md` — специализированный roadmap memory stack.
- `docs/DEBUGGING.md` — практическая памятка по типовым boot/runtime-проблемам.
- `RELEASE_CHECKLIST.md` — обязательный чеклист перед каждым PR.
- `docs/RELEASES.md` — навигация по релизному контуру и связанным process-файлам.

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
