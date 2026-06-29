#include "sched.h"
#include "pmm.h"
#include "serial.h"
#include "timer.h"
#include "vmm.h"
#include "tss.h"

static uint64_t g_kernel_cr3 = 0;

typedef struct context {
    // Сохранено PUSH_GPRS (порядок обратный от pop в POP_GPRS)
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rax;
    // Сохранено аппаратно при прерывании
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) context_t;

static thread_t g_threads[MAX_THREADS];
static uint32_t g_num_threads = 0;
static uint32_t g_current_thread_idx = 0;
static uint8_t g_sched_ready = 0;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port));
}

void sched_init(void) {
    g_num_threads = 0;
    g_current_thread_idx = 0;
    g_sched_ready = 0;

    extern uint64_t read_cr3(void);
    g_kernel_cr3 = read_cr3();

    // Главный поток ядра (kmain)
    thread_t* main_thread = &g_threads[0];
    main_thread->id = 0;
    main_thread->rsp = 0; // Будет установлено при первом прерывании
    main_thread->state = THREAD_RUNNING;
    main_thread->stack_limit = 0; // Не отслеживаем стек для kmain
    main_thread->cr3 = g_kernel_cr3;
    main_thread->kernel_stack_top = 0x200000; // Стековая вершина загрузки ядра

    g_num_threads = 1;
    g_sched_ready = 1;
    serial_printf("[Scheduler] Initialized. Main thread registered.\n");
}

void thread_create(void (*entry_point)(void)) {
    if (g_num_threads >= MAX_THREADS) {
        serial_printf("[Scheduler] Error: MAX_THREADS reached!\n");
        return;
    }

    // Выделяем 16 страниц (64 КБ) для стека каждого потока
    uint32_t stack_pages_count = 16;
    void* stack_pages = pmm_alloc_pages_multi(stack_pages_count);
    if (!stack_pages) {
        serial_printf("[Scheduler] Error: failed to allocate stack pages for new thread!\n");
        return;
    }

    thread_t* t = &g_threads[g_num_threads];
    t->id = g_num_threads;
    t->stack_limit = stack_pages;

    uint64_t stack_top = (uint64_t)stack_pages + (stack_pages_count * 4096);
    context_t* ctx = (context_t*)(stack_top - sizeof(context_t));

    // Инициализируем контекст для возврата через iretq
    ctx->ss = 0x10;             // Сегмент данных ядра
    ctx->rsp = stack_top;       // Указатель на вершину стека
    ctx->rflags = 0x202;        // IF=1 (прерывания включены), IOPL=0
    ctx->cs = 0x18;             // Сегмент 64-битного кода ядра
    ctx->rip = (uint64_t)entry_point;

    // Очищаем регистры общего назначения
    ctx->rax = 0; ctx->rcx = 0; ctx->rdx = 0; ctx->rbx = 0;
    ctx->rbp = 0; ctx->rsi = 0; ctx->rdi = 0;
    ctx->r8 = 0;  ctx->r9 = 0;  ctx->r10 = 0; ctx->r11 = 0;
    ctx->r12 = 0; ctx->r13 = 0; ctx->r14 = 0; ctx->r15 = 0;

    t->rsp = (uint64_t)ctx;
    t->state = THREAD_READY;
    t->cr3 = g_kernel_cr3;
    t->kernel_stack_top = stack_top;

    serial_printf("[Scheduler] Created thread %d (entry=%p, stack=%p)\n", (int)t->id, entry_point, stack_pages);
    g_num_threads++;
}

uint64_t schedule_interrupt(uint64_t current_rsp) {
    // Инкрементируем тики таймера и отправляем событие в очередь ввода
    timer_handler();

    // Шлем EOI контроллеру PIC (таймер — это IRQ0)
    outb(0x20, 0x20);

    if (!g_sched_ready || g_num_threads <= 1) {
        return 0; // Не переключаем, если планировщик не готов или поток всего один
    }

    // Сохраняем состояние текущего потока
    g_threads[g_current_thread_idx].rsp = current_rsp;
    if (g_threads[g_current_thread_idx].state == THREAD_RUNNING) {
        g_threads[g_current_thread_idx].state = THREAD_READY;
    }

    // Выбираем следующий READY поток (Round Robin)
    uint32_t next_idx = (g_current_thread_idx + 1) % g_num_threads;
    uint32_t loop_count = 0;
    while (g_threads[next_idx].state != THREAD_READY && loop_count < g_num_threads) {
        next_idx = (next_idx + 1) % g_num_threads;
        loop_count++;
    }

    if (g_threads[next_idx].state != THREAD_READY) {
        // Если не нашли готовых потоков, остаемся на текущем
        g_threads[g_current_thread_idx].state = THREAD_RUNNING;
        return 0;
    }

    g_threads[next_idx].state = THREAD_RUNNING;
    g_current_thread_idx = next_idx;

    // Переключаем стек ядра в TSS
    tss_set_rsp0(g_threads[next_idx].kernel_stack_top);

    // Переключаем виртуальное адресное пространство
    vmm_switch((pml4_t*)g_threads[next_idx].cr3);

    return g_threads[next_idx].rsp;
}

void thread_yield(void) {
    // Временная имитация добровольной сдачи кванта через программное прерывание таймера
    __asm__ __volatile__("int $32");
}

void thread_exit(int status) {
    serial_printf("[Scheduler] Thread %d exited with status %d\n", (int)g_threads[g_current_thread_idx].id, status);
    g_threads[g_current_thread_idx].state = THREAD_TERMINATED;
    thread_yield();
    while (1) {
        __asm__ __volatile__("hlt");
    }
}

extern void* memset(void* s, int c, size_t n);
extern void* memcpy(void* dest, const void* src, size_t n);

void process_create(void) {
    if (g_num_threads >= MAX_THREADS) {
        serial_printf("[Scheduler] Error: MAX_THREADS reached when creating process!\n");
        return;
    }

    // 1. Создаем новое адресное пространство PML4
    pml4_t* pml4 = vmm_create_address_space();
    if (!pml4) {
        serial_printf("[Scheduler] Error: failed to create address space for process!\n");
        return;
    }

    // 2. Выделяем страницу под код пользователя и копируем туда бинарник
    void* user_code_phys = pmm_alloc_page();
    if (!user_code_phys) {
        serial_printf("[Scheduler] Error: failed to allocate user code page!\n");
        return;
    }
    memset(user_code_phys, 0, 4096);

    uint8_t user_code[] = {
        0xb8, 0x01, 0x00, 0x00, 0x00,                               // mov eax, 1 (SYS_WRITE)
        0xbf, 0x01, 0x00, 0x00, 0x00,                               // mov edi, 1 (stdout)
        0x48, 0xbe, 0x40, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, // mov rsi, 0x100000040 (строка)
        0xba, 0x13, 0x00, 0x00, 0x00,                               // mov edx, 19 (длина)
        0xcd, 0x80,                                                 // int 0x80
        0xb8, 0x3c, 0x00, 0x00, 0x00,                               // mov eax, 60 (SYS_EXIT)
        0xbf, 0x00, 0x00, 0x00, 0x00,                               // mov edi, 0
        0xcd, 0x80                                                  // int 0x80
    };
    memcpy(user_code_phys, user_code, sizeof(user_code));
    
    // Копируем тестовую строку в страницу кода со смещением 0x40
    memcpy((void*)((uint64_t)user_code_phys + 0x40), "Hello from Ring 3!\n", 19);

    // Мапим страницу кода пользователя на виртуальный адрес 0x100000000
    vmm_map_page(pml4, 0x100000000, (uint64_t)user_code_phys, VMM_PAGE_USER | VMM_PAGE_WRITABLE | VMM_PAGE_PRESENT);

    // 3. Выделяем и мапим страницу под стек пользователя (на 0x100001000)
    void* user_stack_phys = pmm_alloc_page();
    if (!user_stack_phys) {
        serial_printf("[Scheduler] Error: failed to allocate user stack page!\n");
        return;
    }
    memset(user_stack_phys, 0, 4096);
    vmm_map_page(pml4, 0x100001000, (uint64_t)user_stack_phys, VMM_PAGE_USER | VMM_PAGE_WRITABLE | VMM_PAGE_PRESENT);

    // 4. Выделяем стек ядра для переключения TSS
    void* kernel_stack_page = pmm_alloc_page();
    if (!kernel_stack_page) {
        serial_printf("[Scheduler] Error: failed to allocate kernel stack page for process!\n");
        return;
    }
    memset(kernel_stack_page, 0, 4096);

    uint64_t kernel_stack_top = (uint64_t)kernel_stack_page + 4096;

    thread_t* t = &g_threads[g_num_threads];
    t->id = g_num_threads;
    t->stack_limit = kernel_stack_page;
    t->kernel_stack_top = kernel_stack_top;
    t->cr3 = (uint64_t)pml4;

    // Готовим контекст для iretq на стеке ядра
    context_t* ctx = (context_t*)(kernel_stack_top - sizeof(context_t));
    ctx->ss = 0x23;             // User Data Segment (RPL=3)
    ctx->rsp = 0x100002000;     // Вершина стека пользователя (0x100001000 + 4096)
    ctx->rflags = 0x202;        // IF=1
    ctx->cs = 0x2B;             // User Code Segment (RPL=3)
    ctx->rip = 0x100000000;     // Точка входа в программу пользователя

    ctx->rax = 0; ctx->rcx = 0; ctx->rdx = 0; ctx->rbx = 0;
    ctx->rbp = 0; ctx->rsi = 0; ctx->rdi = 0;
    ctx->r8 = 0;  ctx->r9 = 0;  ctx->r10 = 0; ctx->r11 = 0;
    ctx->r12 = 0; ctx->r13 = 0; ctx->r14 = 0; ctx->r15 = 0;

    t->rsp = (uint64_t)ctx;
    t->state = THREAD_READY;

    serial_printf("[Scheduler] Created User Process %d (cr3=%p, RIP=0x100000000, RSP=0x100002000)\n", 
                  (int)t->id, pml4);
    g_num_threads++;
}
