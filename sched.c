#include "sched.h"
#include "pmm.h"
#include "serial.h"
#include "timer.h"

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

    // Главный поток ядра (kmain)
    thread_t* main_thread = &g_threads[0];
    main_thread->id = 0;
    main_thread->rsp = 0; // Будет установлено при первом прерывании
    main_thread->state = THREAD_RUNNING;
    main_thread->stack_limit = 0; // Не отслеживаем стек для kmain

    g_num_threads = 1;
    g_sched_ready = 1;
    serial_printf("[Scheduler] Initialized. Main thread registered.\n");
}

void thread_create(void (*entry_point)(void)) {
    if (g_num_threads >= MAX_THREADS) {
        serial_printf("[Scheduler] Error: MAX_THREADS reached!\n");
        return;
    }

    void* stack_page = pmm_alloc_page();
    if (!stack_page) {
        serial_printf("[Scheduler] Error: failed to allocate stack page for new thread!\n");
        return;
    }

    thread_t* t = &g_threads[g_num_threads];
    t->id = g_num_threads;
    t->stack_limit = stack_page;

    uint64_t stack_top = (uint64_t)stack_page + 4096;
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

    serial_printf("[Scheduler] Created thread %d (entry=%p, stack=%p)\n", (int)t->id, entry_point, stack_page);
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

    return g_threads[next_idx].rsp;
}

void thread_yield(void) {
    // Временная имитация добровольной сдачи кванта через программное прерывание таймера
    __asm__ __volatile__("int $32");
}
