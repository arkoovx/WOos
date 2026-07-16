#include "sched.h"
#include "pmm.h"
#include "serial.h"
#include "timer.h"
#include "vmm.h"
#include "tss.h"

static uint64_t g_kernel_cr3 = 0;
uint64_t g_current_kernel_stack_top = 0;



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
    main_thread->kernel_stack_top = 0x0009F000; // Стековая вершина загрузки ядра (в безопасной низкой памяти)

    g_num_threads = 1;
    g_sched_ready = 1;
    g_current_kernel_stack_top = 0x0009F000;
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
    ctx->ss = 0x20;             // Сегмент данных ядра (64-bit Kernel Data)
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

static volatile uint8_t g_is_yield = 0;

uint64_t schedule_interrupt(uint64_t current_rsp) {
    // Only call timer_handler for real hardware timer IRQs, not software yields
    if (!g_is_yield) {
        timer_handler();
    }
    // Performance stats reporting (once per second = 20 real timer ticks)
    static uint32_t ticks_count = 0;
    if (!g_is_yield) {
        ticks_count++;
    }
    
    // Reset yield flag after we've used it
    g_is_yield = 0;
    
    if (ticks_count >= 20) {
        ticks_count = 0;
        extern woos_perf_t g_perf_stats;
        extern uint64_t g_tsc_per_ms;
        extern uint32_t idt_mouse_irq_count(void);
        extern uint32_t idt_keyboard_irq_count(void);
        static uint32_t prev_mouse_irqs = 0;
        static uint32_t prev_kb_irqs = 0;
        uint32_t curr_mouse = idt_mouse_irq_count();
        uint32_t curr_kb = idt_keyboard_irq_count();
        uint32_t mouse_delta = curr_mouse - prev_mouse_irqs;
        uint32_t kb_delta = curr_kb - prev_kb_irqs;
        prev_mouse_irqs = curr_mouse;
        prev_kb_irqs = curr_kb;
        
        if (g_tsc_per_ms > 0) {
            uint64_t pres_ms = g_perf_stats.present_cycles / g_tsc_per_ms;
            uint64_t blit_ms = g_perf_stats.blit_cycles / g_tsc_per_ms;
            uint64_t draw_ms = g_perf_stats.draw_win_cycles / g_tsc_per_ms;
            
            serial_printf("[Perf Stats] FPS (presents): %d (Total time: %d ms), Blits: %d (Total time: %d ms), Window Draws: %d (Total time: %d ms), Mouse IRQs: %d, KB IRQs: %d, M_Recv: %d, M_DropA: %d, M_DropO: %d, M_Push: %d\n",
                          (int)g_perf_stats.present_count, (int)pres_ms,
                          (int)g_perf_stats.blit_count, (int)blit_ms,
                          (int)g_perf_stats.draw_win_count, (int)draw_ms,
                          (int)mouse_delta, (int)kb_delta,
                          (int)g_perf_stats.mouse_recv, (int)g_perf_stats.mouse_drop_align,
                          (int)g_perf_stats.mouse_drop_overflow, (int)g_perf_stats.mouse_push);
        }
        g_perf_stats.present_count = 0;
        g_perf_stats.present_cycles = 0;
        g_perf_stats.blit_count = 0;
        g_perf_stats.blit_cycles = 0;
        g_perf_stats.draw_win_count = 0;
        g_perf_stats.draw_win_cycles = 0;
        g_perf_stats.mouse_recv = 0;
        g_perf_stats.mouse_drop_align = 0;
        g_perf_stats.mouse_drop_overflow = 0;
        g_perf_stats.mouse_push = 0;
    }

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
    uint32_t old_idx = g_current_thread_idx;
    g_current_thread_idx = next_idx;
    g_current_kernel_stack_top = g_threads[next_idx].kernel_stack_top;

    // Переключаем стек ядра в TSS
    tss_set_rsp0(g_threads[next_idx].kernel_stack_top);

    // Переключаем виртуальное адресное пространство
    vmm_switch((pml4_t*)g_threads[next_idx].cr3);



    return g_threads[next_idx].rsp;
}

uint64_t schedule_irq(uint64_t current_rsp, uint64_t vector) {
    extern void idt_handle_irq(uint32_t vector);
    idt_handle_irq((uint32_t)vector);

    if (!g_sched_ready || g_num_threads <= 1) {
        return 0;
    }

    uint32_t best_idx = g_current_thread_idx;
    uint32_t best_priority = 0;

    if (g_threads[g_current_thread_idx].state == THREAD_RUNNING) {
        if (g_current_thread_idx == 1) best_priority = 2;
        else if (g_current_thread_idx == 0) best_priority = 0;
        else best_priority = 1;
    } else {
        best_priority = 0;
        best_idx = 0;
    }

    for (uint32_t i = 0; i < g_num_threads; i++) {
        if (g_threads[i].state == THREAD_READY) {
            uint32_t prio = 0;
            if (i == 1) prio = 2;
            else if (i == 0) prio = 0;
            else prio = 1;

            if (prio > best_priority) {
                best_priority = prio;
                best_idx = i;
            }
        }
    }

    if (best_idx == g_current_thread_idx) {
        return 0;
    }

    g_threads[g_current_thread_idx].rsp = current_rsp;
    if (g_threads[g_current_thread_idx].state == THREAD_RUNNING) {
        g_threads[g_current_thread_idx].state = THREAD_READY;
    }

    g_threads[best_idx].state = THREAD_RUNNING;
    uint32_t old_idx = g_current_thread_idx;
    g_current_thread_idx = best_idx;
    g_current_kernel_stack_top = g_threads[best_idx].kernel_stack_top;

    tss_set_rsp0(g_threads[best_idx].kernel_stack_top);
    vmm_switch((pml4_t*)g_threads[best_idx].cr3);



    return g_threads[best_idx].rsp;
}

void thread_yield(void) {
    // Set flag so schedule_interrupt knows this is a voluntary yield, not a timer IRQ
    g_is_yield = 1;
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
        0x0f, 0x05,                                                 // syscall
        0xb8, 0x3c, 0x00, 0x00, 0x00,                               // mov eax, 60 (SYS_EXIT)
        0xbf, 0x00, 0x00, 0x00, 0x00,                               // mov edi, 0
        0x0f, 0x05                                                  // syscall
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
    ctx->ss = 0x2B;             // User Data Segment (Selector 0x28 | RPL=3)
    ctx->rsp = 0x100002000;     // Вершина стека пользователя (0x100001000 + 4096)
    ctx->rflags = 0x202;        // IF=1
    ctx->cs = 0x33;             // User Code Segment (Selector 0x30 | RPL=3)
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

uint32_t sched_current_thread_id(void) {
    return (uint32_t)g_threads[g_current_thread_idx].id;
}

#define IPC_MAX_MSG 32
static woos_ipc_msg_t g_ipc_queue[IPC_MAX_MSG];
static uint32_t g_ipc_head = 0;
static uint32_t g_ipc_tail = 0;

int32_t woos_ipc_send(uint32_t dest_id, uint32_t type, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5) {
    __asm__ __volatile__("cli");
    
    uint32_t next_head = (g_ipc_head + 1) % IPC_MAX_MSG;
    if (next_head == g_ipc_tail) {
        serial_printf("[IPC] Error: queue is full!\n");
        __asm__ __volatile__("sti");
        return -1;
    }
    
    g_ipc_queue[g_ipc_head].sender_id = sched_current_thread_id();
    g_ipc_queue[g_ipc_head].dest_id = dest_id;
    g_ipc_queue[g_ipc_head].type = type;
    g_ipc_queue[g_ipc_head].arg1 = arg1;
    g_ipc_queue[g_ipc_head].arg2 = arg2;
    g_ipc_queue[g_ipc_head].arg3 = arg3;
    g_ipc_queue[g_ipc_head].arg4 = arg4;
    g_ipc_queue[g_ipc_head].arg5 = arg5;
    
    g_ipc_head = next_head;
    
    sched_unblock_all();
    
    __asm__ __volatile__("sti");
    return 0;
}

int32_t woos_ipc_recv(woos_ipc_msg_t* msg) {
    __asm__ __volatile__("cli");
    
    uint32_t current_id = sched_current_thread_id();
    uint32_t scan = g_ipc_tail;
    
    while (scan != g_ipc_head) {
        if (g_ipc_queue[scan].dest_id == current_id) {
            memcpy(msg, &g_ipc_queue[scan], sizeof(woos_ipc_msg_t));
            
            uint32_t shift = scan;
            while (shift != g_ipc_head) {
                uint32_t next = (shift + 1) % IPC_MAX_MSG;
                if (next == g_ipc_head) break;
                g_ipc_queue[shift] = g_ipc_queue[next];
                shift = next;
            }
            if (g_ipc_head == 0) {
                g_ipc_head = IPC_MAX_MSG - 1;
            } else {
                g_ipc_head--;
            }
            
            __asm__ __volatile__("sti");
            return 1;
        }
        scan = (scan + 1) % IPC_MAX_MSG;
    }
    
    __asm__ __volatile__("sti");
    return 0;
}

static char g_spawn_path[64];
static volatile uint8_t g_spawn_pending = 0;

void wasm_dynamic_runner_thread(void) {
    char path[64];
    __asm__ __volatile__("cli");
    memcpy(path, g_spawn_path, 64);
    g_spawn_pending = 0;
    __asm__ __volatile__("sti");
    
    serial_printf("[WASM Runner] Starting spawned WASM application %s...\n", path);
    extern void wasm_runtime_run_file(const char* filepath);
    wasm_runtime_run_file(path);
    
    thread_exit(0);
}

int32_t woos_system_spawn(const char* path) {
    __asm__ __volatile__("cli");
    if (g_spawn_pending) {
        __asm__ __volatile__("sti");
        return -1;
    }
    memcpy(g_spawn_path, path, 64);
    g_spawn_pending = 1;
    __asm__ __volatile__("sti");
    
    thread_create(wasm_dynamic_runner_thread);
    return 0;
}

void sched_block_current(void) {
    __asm__ __volatile__("cli");
    g_threads[g_current_thread_idx].state = THREAD_BLOCKED;
    __asm__ __volatile__("sti");
    thread_yield();
}

void sched_unblock_all(void) {
    __asm__ __volatile__("cli");
    for (uint32_t i = 0; i < g_num_threads; i++) {
        if (g_threads[i].state == THREAD_BLOCKED) {
            g_threads[i].state = THREAD_READY;
        }
    }
    __asm__ __volatile__("sti");
}

uint32_t woos_ipc_pending(void) {
    return (g_ipc_head != g_ipc_tail);
}
