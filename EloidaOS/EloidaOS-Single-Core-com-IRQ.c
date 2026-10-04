/* =====================================================================
 * ELOIDA OS — Single-Core com IRQs integradas
 * Risc-V, CH32V307VCT6 @ 144 MHz
 * ---------------------------------------------------------------------
 * Arquitetura:
 *   - Tasks periódicas  : disparadas por primos (tempo é primo)
 *   - Tasks de IRQ      : disparadas por hardware (task é veredito)
 *   - MMC               : soma única de todos os vereditos
 *   - DESVIO            : política única e auditável
 *
 * Kernel explícito:
 *   process()  — incrementa o contador da task; se atingiu o primo,
 *                marca READY (a task está pronta para rodar).
 *   run()      — se READY, executa a task e captura seu veredito.
 *   destroy()  — limpa READY e zera o contador (pós-execução).
 *
 * Fluxo:
 *   ISR -> IRQ_OPS (flag) -> IRQ() -> task de IRQ -> OK[tid] (veredito)
 *   main: IRQ() -> TASKS() -> mmc() -> DESVIO() -> task arbitrada
 * ===================================================================== */

typedef unsigned char u8;
typedef unsigned int  u32;

/* ---------------------------------------------------------------------
 * 1. CONFIGURAÇÃO
 * --------------------------------------------------------------------- */
#define MAX_TASKS_PERIODIC   15
#define MAX_IRQ_TASKS        4
#define MAX_TOTAL            (MAX_TASKS_PERIODIC + MAX_IRQ_TASKS)   /* 19 */

/* Primos: tasks periódicas usam primos grandes;
 * tasks de IRQ usam primos pequenos não usados pelas periódicas. */
const u32 PRIME_NUMS[MAX_TOTAL] = {
    /* Tasks periódicas 0..14 */
          2, 5, 11, 23, 7919, 36007, 144013, 720007, 1800001,
    3600007, 7200011, 14400013, 28800017, 57600023, 115200029,
    /* Tasks de IRQ 15..18 */
        13, 17, 19, 29
};

/* ---------------------------------------------------------------------
 * 2. ESTADO
 * --------------------------------------------------------------------- */
u8  OK[MAX_TOTAL]       = {0};   /* veredito: primo (OK) ou 0 (falha)   */
u8  READY[MAX_TOTAL]    = {0};   /* flag: task pronta para rodar        */
u8  DONE[MAX_TOTAL]     = {0};   /* flag: task já executou ao menos 1x  */
u32 COUNTS[MAX_TOTAL]   = {0};   /* contador de ciclos por task         */

volatile u32 IRQ_OPS = 0;        /* flags de operação (setadas nas ISRs)*/

/* Bits de operação */
#define OP_TIMER   (1u << 0)
#define OP_UART    (1u << 1)
#define OP_ADC     (1u << 2)
#define OP_CAN     (1u << 3)
#define MAX_IRQS   4

/* ---------------------------------------------------------------------
 * 3. IPC — dados compartilhados
 * --------------------------------------------------------------------- */
typedef struct {
    u32 tensao_mv;
    u32 corrente_ma;
    u32 temperatura_c;
    u32 soc_pct;
    u32 balanceamento;
    u32 protecao;
    u32 modo;
    u32 timer_count;
    u32 adc_value;
    u8  debug_log[16];
} SharedData;

SharedData SHARED = {0};

/* Log de falhas (auditoria) */
u32 FAULT_LOG[MAX_TOTAL] = {0};

/* ---------------------------------------------------------------------
 * 4. ISRs — mínimas: só setam flag (e ação crítica se necessário)
 * --------------------------------------------------------------------- */
void TIMER_IRQHandler(void) {
    IRQ_OPS |= OP_TIMER;
    TIMER->SR = 0;
}

void UART_IRQHandler(void) {
    IRQ_OPS |= OP_UART;
    UART->SR = 0;
}

void ADC_IRQHandler(void) {
    IRQ_OPS |= OP_ADC;
    ADC->SR = 0;
}

void CAN_IRQHandler(void) {
    IRQ_OPS |= OP_CAN;
    CAN->SR = 0;
}

/* ---------------------------------------------------------------------
 * 5. TASKS PERIÓDICAS 0..14
 * --------------------------------------------------------------------- */
u8 task0(void) {
    u32 v = 3700;
    if (v < 2500 || v > 4200) { FAULT_LOG[0]++; return 0; }
    SHARED.tensao_mv = v;
    return PRIME_NUMS[0];
}

u8 task1(void) {
    u32 i = 1500;
    if (i > 5000) { FAULT_LOG[1]++; return 0; }
    SHARED.corrente_ma = i;
    return PRIME_NUMS[1];
}

u8 task2(void) {
    u32 t = 35;
    if (t > 60) { FAULT_LOG[2]++; return 0; }
    SHARED.temperatura_c = t;
    return PRIME_NUMS[2];
}

u8 task3(void) {
    if (!DONE[0] || !OK[0]) return 0;
    if (!DONE[1] || !OK[1]) return 0;
    SHARED.soc_pct = (SHARED.tensao_mv - 2500) / 17;
    if (SHARED.soc_pct > 100) SHARED.soc_pct = 100;
    return PRIME_NUMS[3];
}

u8 task4(void) {
    if (!DONE[0] || !OK[0]) return 0;
    SHARED.balanceamento = (SHARED.tensao_mv > 4000) ? 1 : 0;
    return PRIME_NUMS[4];
}

u8 task5(void) {
    if (!DONE[1] || !OK[1]) return 0;
    if (!DONE[2] || !OK[2]) return 0;
    if (SHARED.corrente_ma > 4500 || SHARED.temperatura_c > 55)
        SHARED.protecao = 1;
    else
        SHARED.protecao = 0;
    return PRIME_NUMS[5];
}

u8 task6(void) {
    if (!DONE[3] || !OK[3]) return 0;
    if (!DONE[4] || !OK[4]) return 0;
    if (!DONE[5] || !OK[5]) return 0;
    if (SHARED.protecao)          SHARED.modo = 0;  /* EMERGENCIA */
    else if (SHARED.soc_pct < 20) SHARED.modo = 1;  /* ECONOMIA   */
    else                          SHARED.modo = 2;  /* NORMAL     */
    return PRIME_NUMS[6];
}

u8 task7 (void) { return PRIME_NUMS[7];  }
u8 task8 (void) { return PRIME_NUMS[8];  }
u8 task9 (void) { return PRIME_NUMS[9];  }
u8 task10(void) { return PRIME_NUMS[10]; }
u8 task11(void) { return PRIME_NUMS[11]; }
u8 task12(void) { return PRIME_NUMS[12]; }
u8 task13(void) { return PRIME_NUMS[13]; }
u8 task14(void) { return PRIME_NUMS[14]; }

/* ---------------------------------------------------------------------
 * 6. TASKS DE IRQ 15..18 — validam, publicam, retornam veredito
 * --------------------------------------------------------------------- */
u8 task_irq_timer(void) {
    SHARED.timer_count++;
    return PRIME_NUMS[15];
}

u8 task_irq_uart(void) {
    u8 b = UART->DR;
    if (b == 0xFF) { FAULT_LOG[16]++; return 0; }
    return PRIME_NUMS[16];
}

u8 task_irq_adc(void) {
    u32 v = ADC->DR;
    if (v > 4095) { FAULT_LOG[17]++; return 0; }
    SHARED.adc_value = v;
    return PRIME_NUMS[17];
}

u8 task_irq_can(void) {
    u32 frame = CAN->RFR;
    if (!(frame & CAN_VALID)) { FAULT_LOG[18]++; return 0; }
    return PRIME_NUMS[18];
}

/* ---------------------------------------------------------------------
 * 7. TABELAS DE DESPACHO E ROTEAMENTO
 * --------------------------------------------------------------------- */
typedef u8 (*TaskFunc)(void);

const TaskFunc TASK_TABLE[MAX_TOTAL] = {
    task0, task1, task2, task3, task4, task5, task6, task7, task8,
    task9, task10, task11, task12, task13, task14,
    task_irq_timer, task_irq_uart, task_irq_adc, task_irq_can
};

const u8 IRQ_TASK_MAP[MAX_IRQS] = { 15, 16, 17, 18 };

/* =====================================================================
 * 8. KERNEL — process(), run(), destroy()
 * =====================================================================
 * Estas três funções são o coração do kernel cooperativo.
 * Cada uma tem uma responsabilidade única e explícita.
 * ===================================================================== */

/* ---------------------------------------------------------------------
 * process() — incrementa o contador de ciclos de uma task periódica.
 * ---------------------------------------------------------------------
 * O que faz:
 *   1. Incrementa COUNTS[tid].
 *   2. Se COUNTS[tid] >= PRIME_NUMS[tid], a task atingiu seu período:
 *      - Seta READY[tid] = 1 (task pronta para rodar).
 *      - Zera COUNTS[tid] (próximo período começa do zero).
 *
 * Pré-condição:  tid < MAX_TASKS_PERIODIC
 * Pós-condição:  READY[tid] = 1 se o período foi atingido.
 * --------------------------------------------------------------------- */
void process(u8 tid)
{
    if (++COUNTS[tid] >= PRIME_NUMS[tid])
    {
        READY[tid]  = 1;
        COUNTS[tid] = 0;
    }
}

/* ---------------------------------------------------------------------
 * run() — executa a task se ela estiver pronta (READY[tid] == 1).
 * ---------------------------------------------------------------------
 * O que faz:
 *   1. Verifica se READY[tid] == 1.
 *   2. Se sim, executa a task via TASK_TABLE[tid]().
 *   3. Captura o retorno (primo em OK, 0 em falha) e armazena em OK[tid].
 *   4. Marca DONE[tid] = 1 (a task já executou ao menos uma vez).
 *
 * Pré-condição:  tid < MAX_TOTAL
 * Pós-condição:  OK[tid] contém o veredito da task (primo ou 0).
 * --------------------------------------------------------------------- */
void run(u8 tid)
{
    if (READY[tid])
    {
        OK[tid]   = TASK_TABLE[tid]();
        DONE[tid] = 1;
    }
}

/* ---------------------------------------------------------------------
 * destroy() — limpa o estado da task após execução.
 * ---------------------------------------------------------------------
 * O que faz:
 *   1. Limpa READY[tid] = 0 (a task não está mais pronta).
 *   2. Zera COUNTS[tid] = 0 (o próximo período começa do zero).
 *
 * Por que zerar COUNTS aqui e não em process()?
 *   - Em preempção (IRQ), a task pode rodar fora do ciclo periódico.
 *   - Zerar COUNTS em destroy() garante que a próxima execução
 *     periódica comece do zero absoluto, sem "roubar" tempo.
 *
 * Pré-condição:  tid < MAX_TOTAL
 * Pós-condição:  READY[tid] = 0 e COUNTS[tid] = 0.
 * --------------------------------------------------------------------- */
void destroy(u8 tid)
{
    READY[tid]  = 0;
    COUNTS[tid] = 0;
}

/* =====================================================================
 * 9. BLOCO IRQ() — roteia flags de hardware para tasks de IRQ
 * =====================================================================
 * Lê o snapshot atômico de IRQ_OPS, limpa as flags, e para cada bit
 * setado executa a task de IRQ correspondente via TASK_TABLE[].
 * ===================================================================== */
void IRQ(void)
{
    u32 snapshot = __atomic_exchange_n(&IRQ_OPS, 0, __ATOMIC_SEQ_CST);

    for (u8 i = 0; i < MAX_IRQS; i++)
    {
        if (snapshot & (1u << i))
        {
            u8 tid = IRQ_TASK_MAP[i];

            /* Task de IRQ: valida, publica, retorna veredito. */
            OK[tid]   = TASK_TABLE[tid]();
            DONE[tid] = 1;
        }
    }
}

/* =====================================================================
 * 10. BLOCO TASKS() — executa tasks periódicas via process/run/destroy
 * =====================================================================
 * Para cada task periódica:
 *   1. process(tid) — incrementa contador; marca READY se atingiu primo.
 *   2. run(tid)     — executa se READY; captura veredito.
 *   3. destroy(tid) — limpa READY e zera COUNTS.
 * ===================================================================== */
void TASKS(void)
{
    for (u8 i = 0; i < MAX_TASKS_PERIODIC; i++)
    {
        process(i);   /* incrementa contador; marca READY se atingiu primo */
        run(i);       /* executa se READY; captura veredito              */
        destroy(i);   /* limpa READY e zera COUNTS                        */
    }
}

/* =====================================================================
 * 11. MMC — soma única de todos os vereditos
 * ===================================================================== */
u32 mmc(void)
{
    u32 soma = 0;
    for (u8 i = 0; i < MAX_TOTAL; i++) soma += OK[i];
    return soma;
}

/* =====================================================================
 * 12. DESVIO — política única e auditável
 * ===================================================================== */
u8 DESVIO(u32 soma)
{
    if (soma == 0) return 7;                            /* failsafe   */
    if (OK[5] == 0 && DONE[5]) return 5;                /* proteção   */
    if (OK[0] && OK[1] && OK[2] && !OK[3]) return 3;    /* calcula SOC*/
    if (OK[3] && !OK[4]) return 4;                      /* balanceia  */
    if (OK[4] && !OK[5]) return 5;                      /* proteção   */
    if (OK[0] && OK[1] && OK[2] && OK[3] && OK[4] && OK[5] && OK[6])
        return 6;                                       /* supervisor */
    return 6;
}

/* =====================================================================
 * 13. MAIN LOOP
 * ===================================================================== */
void main(void)
{
    while (1)
    {
        /* (a) IRQs: hardware -> tasks de IRQ -> vereditos */
        IRQ();

        /* (b) Tasks periódicas: process -> run -> destroy */
        TASKS();

        /* (c) Correlação via MMC */
        u32 soma = mmc();

        /* (d) Política via DESVIO */
        u8 proxima = DESVIO(soma);

        /* (e) Executa task arbitrada (mesmo contrato das demais) */
        OK[proxima]   = TASK_TABLE[proxima]();
        DONE[proxima] = 1;
    }
}