# Changelog

## 1.13.0
- Добавлен модуль `kheap` с базовым runtime allocator (`kheap_init`, `kmalloc`, `kfree`) поверх PMM-страниц: first-fit, split/merge блоков, выравнивание 16 байт и диагностические метрики (`kheap_total_bytes`, `kheap_free_bytes`, `kheap_largest_free_block`).
- Инициализация heap встроена в `INIT_PLATFORM`, а в `kmain` добавлен ранний smoke-тест (`kmalloc/kfree`) с интеграцией в существующий индикатор готовности ядра.
- Подготовлен документ `docs/VMM_MIGRATION_PLAN.md` с целевой картой памяти, этапами внедрения page tables и первичной kernel mapping policy для перехода к VMM.
- В roadmap (`DEVELOPMENT_PLAN.md`) закрыт блок C.2 (heap allocator + план миграции к VMM), обновлены `README.md`, `Makefile` и UI-баннер до версии `1.13.0`.

## 1.12.0
- Добавлен модуль `pmm` (stack-based allocator страниц) с API `pmm_init/pmm_alloc_page/pmm_free_page` и диагностикой состояния пула (`pmm_total_pages`, `pmm_free_pages`, `pmm_is_ready`).
- Для baseline PMM выделен внутренний выровненный пул на 1024 страницы по 4 KiB с free-stack и защитой от дублирующего `free` в пределах пула.
- Инициализация PMM встроена в `INIT_PLATFORM` (`kmain`), чтобы подсистема памяти была готова до старта драйверов и UI.
- Обновлены `README.md`, roadmap (`DEVELOPMENT_PLAN.md`) и UI-баннер до версии `1.12.0`.

## 1.11.0
- В `idt` добавлена базовая IRQ-инфраструктура: remap 8259 PIC на векторы `32..47`, unmask только нужных линий (`IRQ1`, `IRQ2`, `IRQ12`), C-dispatcher IRQ и EOI после обработки.
- Добавлены отдельные IDT-stub для `IRQ1` (клавиатура) и `IRQ12` (мышь) с передачей вектора в `idt_dispatch_irq`.
- Добавлен модуль `keyboard` с обработкой PS/2 scancode по IRQ и публикацией `INPUT_EVENT_KEY_PRESS` в существующую очередь input.
- Модуль `mouse` расширен до hybrid режима (IRQ + polling fallback): обработка байтов AUX вынесена в общий путь, подключен `mouse_handle_irq`.
- В `kernel` зарегистрированы IRQ-обработчики для клавиатуры/мыши через `idt_set_irq_handler`, а roadmap-пункт C.1 «Обвязка IRQ для клавиатуры/мыши» отмечен выполненным.
- Добавлен `.github/PULL_REQUEST_TEMPLATE.md` с обязательным чеклистом (версия/changelog/docs/валидация/rollback), пункт roadmap по PR template отмечен выполненным.

## 1.10.1
- Убрано заметное мерцание UI в стандартной сборке: в `Makefile` двойная буферизация (`DBL_BUFFER`) теперь включена по умолчанию (`1`), поэтому `fb_present_rect` публикует только dirty-области из backbuffer без промежуточных артефактов.
- Обновлены `README.md`, `VERSION` и UI-баннер под версию `1.10.1`.

## 1.10.0
- В `drivers/virtio_gpu_renderer` добавлен command-oriented рендер-пайплайн: `fb`/UI теперь отправляют draw-команды (`fill/rect/glyph`) в renderer, а не пишут пиксели напрямую в stage2 framebuffer при активном `virtio-gpu`.
- Добавлена отдельная draw-surface в RAM для GPU-пути: ресурс `virtio-gpu` получает backing на этой поверхности, после чего dirty-области публикуются через virtqueue (`TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH`).
- Добавлена negotiated-поддержка `VIRTIO_GPU_F_VIRGL` (virgl capability bit) с безопасным fallback, если функция недоступна на устройстве.
- Сохранены текущие init-flow (`early -> platform -> drivers -> ui`), driver-layer разделение и существующий `fb/ui` API; software framebuffer fallback остаётся рабочим для non-virtio/legacy-path.

## 1.9.0
- Добавлен модуль `drivers/virtio_gpu_renderer`, который в driver-stage определяет `virtio-gpu` по PCI и при валидном MMIO BAR переключает `video->framebuffer` на framebuffer устройства.
- Сохранён текущий init-flow (`early -> platform -> drivers -> ui`): инициализация рендера virtio встроена в `INIT_DRIVERS` без изменений API UI/fb.
- Добавлен безопасный fallback: если `virtio-gpu` не найден или BAR невалиден, ядро продолжает рендерить в framebuffer, переданный `stage2`.
- В `fb_present_rect` добавлен backend-hook `virtio_gpu_renderer_present_rect(...)` как фундамент для следующего шага (virtqueue `TRANSFER_TO_HOST_2D` / `RESOURCE_FLUSH`) без поломки dirty-rect/double-buffer pipeline.
- Обновлены документация и UI-баннер до версии `1.9.0`.

## 1.8.0
- Добавлен базовый драйвер `virtio_gpu` с безопасным PCI-probing устройств `virtio-gpu` (modern/legacy) без изменения текущего framebuffer-пути, чтобы не ломать существующий kernel flow.
- Добавлен модуль `pci` для чтения PCI config space через порты `0xCF8/0xCFC` и поиска устройств по `vendor/device` и display-классу.
- Инициализация `virtio_gpu_init()` встроена в этап `INIT_DRIVERS`.
- Обновлён UI-баннер до версии `1.8.0`.
- README дополнен корректной командой запуска для `virtio-vga` и пояснением по 2D/3D-ускорению в QEMU.

## 1.7.3
- Исправлено «1 FPS»-поведение в QEMU: значительно уменьшена искусственная пауза в основном цикле ядра (`kmain`), из‑за которой интерфейс обновлялся слишком редко.
- Heartbeat-таймер в polling-режиме перенастроен на более редкий шаг (`timer_init(120)`), чтобы не провоцировать лишние перерисовки footer при ускорении основного цикла.
- Добавлена заметка в `README.md` с рекомендуемым запуском QEMU через `-machine accel=kvm:tcg` для нормальной производительности.
- Обновлён UI-баннер до версии `1.7.3`.

## 1.7.2
- Исправлена работа framebuffer для разных глубин цвета (`16/24/32 bpp`): добавлены универсальные `fb_readpixel`/`fb_writepixel`, благодаря чему исчезли артефакты в виде чёрных вертикальных полос при нестандартном формате видеорежима.
- Курсор переведён на чтение/запись пикселей через `fb`-API вместо прямого доступа к `uint32_t`-строкам, что устраняет повреждение изображения и сохраняет корректность с двойной буферизацией.
- Убран полосатый фон в desktop-области: фон теперь рисуется однотонно без серых горизонтальных линий.
- Обновлён UI-баннер до версии `1.7.2`.

## 1.7.1
- Убрано искусственное диагональное движение курсора из `kmain`: теперь курсор не «уплывает» сам по себе и обновляется только по реальным событиям мыши.
- Добавлен модуль `mouse` (`mouse.c/.h`) с инициализацией PS/2-мыши через контроллер 8042, разбором 3-байтовых пакетов и генерацией событий `INPUT_EVENT_MOUSE_MOVE`/`INPUT_EVENT_MOUSE_BUTTON` в существующую очередь input.
- Цикл ядра переведён на polling реальных данных от мыши (`mouse_poll`) плюс heartbeat-тик, без демонстрационных синтетических move/button событий.
- Обновлены `Makefile`, `README.md`, `DEVELOPMENT_PLAN.md` и UI-баннер до версии `1.7.1`.

## 1.7.0
- Добавлен модуль `idt` (`idt.c/.h`, `idt_asm.asm`): формирование и загрузка IDT на 256 векторов с безопасным default ISR-stub (`iretq`) как базовый каркас подсистемы прерываний.
- Добавлен модуль `timer` (`timer.c/.h`) с heartbeat-счётчиком в polling-режиме и генерацией `INPUT_EVENT_TIMER_TICK` через существующую очередь событий.
- В `kernel.c` platform/drivers init расширен инициализацией `idt_init()` и `timer_init()`, а обработка `timer tick` теперь публикует диагностику в UI.
- В UI добавлен индикатор состояния IDT (`STATUS: IDT READY/IDT BAD`) и счётчик `HEARTBEAT` в footer для базовой runtime-диагностики.
- Обновлены `README.md` и `DEVELOPMENT_PLAN.md` в соответствии с прогрессом этапа C.1.

## 1.6.0
- Добавлена подсистема input/event с фиксированной ring-buffer очередью (`input_push`, `input_pop`) и типами событий `mouse move`, `mouse button`, `timer tick`.
- В `kmain` реализован базовый event dispatcher: события теперь проходят через очередь и централизованно обрабатываются UI-слоем.
- Добавлены UI-handlers для `hover/click` по верхней панели: кнопка в заголовке меняет состояние (normal/hover/pressed), а в footer показывается текущий интерактивный статус.
- Обновлены `README.md` и `DEVELOPMENT_PLAN.md` в соответствии с прогрессом этапа B.2.

## 1.5.2
- Добавлено базовое состояние UI-курсора (`x/y`, `buttons`) с ограничением координат внутри экрана (clamp-to-screen).
- Реализована отрисовка курсора через локальный save/restore фонового блока, чтобы перемещение не требовало полного перерисовывания кадра.
- В `kmain` добавлен демонстрационный цикл перемещения курсора для визуальной проверки cursor path до интеграции PS/2.
- Обновлены `README.md` и `DEVELOPMENT_PLAN.md` по прогрессу этапа B.1.

## 1.5.1
- В `.github/workflows/release.yml` добавлена явная проверка payload перед публикацией: наличие `os.img` и непустого файла release notes, чтобы предотвратить публикацию некорректного релиза.
- Обновлены формулировки в документации (`README.md`, `DEVELOPMENT_PLAN.md`) без упоминаний, что WoOS является учебной, простой или минималистичной ОС.

## 1.5.0
- Переработан workflow `.github/workflows/release.yml`: теперь релиз автоматически создаётся после merge PR, если у PR есть минимум один review со статусом `APPROVED`.
- В релизном workflow добавлена автоматическая сборка `os.img` из merge-коммита PR и публикация образа во вложения GitHub Release.
- Формирование release notes переведено на секцию текущей версии из `CHANGELOG.md` через `.github/scripts/extract_changelog.sh`; в описание добавляется номер одобренного PR.
- Добавлена защита от повторной публикации: workflow валидирует отсутствие существующего тега `vX.Y.Z` перед выпуском релиза.
- Обновлены `README.md`, `RELEASE_CHECKLIST.md` и roadmap в части релизной политики после одобренного PR.

## 1.4.0
- В `fb` добавлена опциональная двойная буферизация с compile-time переключателем `WOOS_ENABLE_DBL_BUFFER` и инициализацией через `fb_init`.
- Добавлен `fb_present_rect`, чтобы в режиме double buffer публиковать только dirty-области во фронт-буфер.
- `ui_render_dirty` теперь вызывает публикацию dirty-регионов после рендера каждого clip.
- В `Makefile` добавлен флаг сборки `DBL_BUFFER` (`make os.img DBL_BUFFER=1`).
- Обновлены `README.md` и roadmap: пункт A.2 по опциональной двойной буферизации отмечен выполненным.

## 1.3.0
- В UI добавлена очередь dirty-прямоугольников (`ui_mark_dirty`, `ui_render_dirty`) с объединением пересекающихся областей для частичной перерисовки.
- Полная отрисовка рабочего стола переведена на механизм dirty-областей: `ui_render_desktop` теперь ставит один full-screen dirty-region и рендерит через общий путь.
- Добавлен счётчик `ui_last_dirty_count()` как базовая профилировочная метка «число dirty-областей за кадр».
- Обновлены roadmap и README в соответствии с прогрессом этапа A.2.

## 1.2.0
- Зафиксирован boot ABI: в `boot_info` добавлены `magic`, `version` и `size` для версионирования контракта между `stage2` и `kernel`.
- В ядре добавлен sanity-check `boot_info` и fallback-инициализация критичных полей (`framebuffer`, `pitch`).
- В `kmain` введена явная последовательность init-стадий (`early`, `platform`, `drivers`, `ui`) как каркас для дальнейшего развития подсистем.
- Обновлён UI-баннер до версии `1.2.0` и изменена строка следующего шага на фокус этапа input/event.
- Обновлены `DEVELOPMENT_PLAN.md` и `README.md` в соответствии с выполненными пунктами этапа A.1.

## 1.1.0
- Существенно расширен `DEVELOPMENT_PLAN.md`: добавлены инженерные принципы, детальные этапы A–E, Definition of Done, метрики и порядок ближайших PR.
- Добавлен `README.md` с быстрым стартом, структурой проекта, процессом разработки и описанием релизного контура.
- Улучшен CI-build workflow: артефакт `os.img` публикуется с именем, привязанным к SHA (`os-image-<sha>`), добавлены настройки retention и concurrency.
- Добавлен автоматический релизный workflow по тегам `v*`, который забирает `os.img` из Actions artifacts, формирует release notes из `CHANGELOG.md` и публикует GitHub Release.
- Добавлен скрипт `.github/scripts/extract_changelog.sh` для извлечения секции версии из changelog.
- Обновлён `RELEASE_CHECKLIST.md` с формальной политикой тегов и автопубликации.

## 1.0.2
- Добавлен bitmap-рендер текста в framebuffer (`fb_draw_char`, `fb_draw_text`).
- Обновлён desktop UI: отрисовка версии, статуса и технической строки следующего этапа.
- Обновлён и детализирован план разработки, добавлен отдельный этап по поддержке мыши.
- Добавлена релизная памятка: каждый PR приравнивается к новой версии с обязательным обновлением `VERSION`, `CHANGELOG` и документации.

## 1.0.1
- Базовый desktop-style интерфейс и модульный графический рендер (`fb`, `ui`).
- Добавлен исходный высокоуровневый план развития WoOS.
