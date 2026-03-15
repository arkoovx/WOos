# План миграции WoOS к VMM

> Цель: перейти от baseline PMM + kernel heap к полноценному virtual memory manager без ломки текущего boot/runtime flow.

## 1. Текущее baseline-состояние
- PMM выдаёт страницы из внутреннего пула (stack-based allocator).
- `kheap` использует непрерывную arena поверх PMM-страниц для runtime-аллокатора внутренних структур.
- Ядро и framebuffer пока работают в identity-like соглашении, заданном загрузчиком.

## 2. Целевая карта памяти ядра

### 2.1 Виртуальные диапазоны (канонический high-half)
- `0xFFFF800000000000 - 0xFFFF8000FFFFFFFF`: прямое отображение (physmap) для ранней отладки и PMM-инструментов.
- `0xFFFFFFFF80000000 - ...`: образ ядра (`.text/.rodata/.data/.bss`) с фиксированной базой линковки.
- `0xFFFFFFFF90000000 - ...`: kernel heap (`kmalloc`) с динамическим ростом виртуального диапазона.
- `0xFFFFFFFFA0000000 - ...`: временные окна для `kmap`/I/O буферов драйверов.

### 2.2 Физические регионы
- Region A: ядро + bootstrap структуры (reserved, never free).
- Region B: low memory для DMA-совместимых буферов (ограниченный пул).
- Region C: general-purpose RAM для PMM/VMM и page cache в будущем.
- MMIO области (framebuffer, PCI BAR) маппятся отдельно с cache-policy `uncached/write-combining`.

## 3. План по page tables

### Шаг VMM-1: Introspection и memory map
1. Расширить boot-contract: loader передаёт e820/UEFI memory map с типами регионов.
2. PMM переводится на bitmap/segregated free-lists поверх реальной карты памяти.
3. Добавить диагностику: количество total/free/reserved страниц по типам.

### Шаг VMM-2: Собственный PML4 ядра
1. Построить отдельный `kernel_pml4` в раннем init после PMM.
2. Реплицировать минимально нужные identity mapping для безопасного switch CR3.
3. Подготовить high-half mapping ядра и physmap-окно.

### Шаг VMM-3: Политика атрибутов страниц
- `.text` -> RX, `.rodata` -> R, `.data/.bss` -> RW, `NX` для неисполняемых регионов.
- Guard pages вокруг критичных стеков/служебных арен.
- MMIO mapping через отдельный API (`vmm_map_mmio`) с правильными PAT-флагами.

### Шаг VMM-4: Интеграция с heap
1. `kheap` уходит от фиксированной arena и запрашивает виртуальные диапазоны у VMM.
2. Для роста heap: `vmm_map_pages` + lazy commit физических страниц.
3. Добавить kmalloc-метрики фрагментации и pressure-сигналы для будущих подсистем.

## 4. Kernel mapping policy (первый релиз VMM)
- По умолчанию kernel-only (`U/S=0`) для всех регионов ядра.
- User-space отсутствует, но layout готовится с отдельным нижним диапазоном под future processes.
- Любое новое mapping API должно явно указывать права (`R/W/X`, `GLOBAL`, cache-mode).
- Запрещены «немые» identity-map расширения вне раннего bootstrap.

## 5. Риски и меры
- Риск: ранний switch на новый CR3 ломает IRQ path -> держать fallback identity-map до прохождения smoke-тестов.
- Риск: неверные cache-флаги для framebuffer/MMIO -> ввести отдельные helper-функции, не использовать общий map-path.
- Риск: регрессии по производительности TLB -> использовать большие страницы (2 MiB) для physmap, где это безопасно.

## 6. Критерии готовности (Definition of Done для VMM-блока)
- Ядро стабильно работает на собственном `kernel_pml4`.
- `kmalloc` способен расти через VMM-мэппинг, не только за счёт фиксированной bootstrap-arena.
- Карта памяти и mapping policy задокументированы и синхронизированы с `README`/`DEVELOPMENT_PLAN`.
