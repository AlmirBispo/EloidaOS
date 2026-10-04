/* =====================================================================
 * ELOIDA OS — Multi-Core com Tabela Verdade e Desvio Global
 * Risc-V multi-hart @ 144 MHz
 * ---------------------------------------------------------------------
 * Arquitetura:
 *   - Cada núcleo é uma instância EloidaOS completa e independente.
 *   - Comunicação por mailbox, tratada como IRQ normal.
 *   - O núcleo mestre (Core 0) tem, opcionalmente:
 *       * USER_FLAGS      — flags personalizadas dos núcleos remotos
 *       * TABELA_GLOBAL   — tabela verdade da relação global
 *       * CONSULTA_GLOBAL() — desvio global
 *   - Os núcleos escravos são EloidaOS puros (sem flags extras).
 *
 * Kernel explícito (comum a todos os núcleos):
 *   process()  — incrementa contador; marca READY se atingiu primo.
 *   run()      — executa se READY; captura veredito em OK[tid].
 *   destroy()  — limpa READY e zera COUNTS.
 * ===================================================================== */

typedef unsigned char u8;
typedef unsigned int  u32;

/* =====================================================================
 * PARTE A — KERNEL ELOIDA COMUM (igual para todos os núcleos)
 * ===================================================================== */

#define MAX_TASKS_PERIODIC   8
#define MAX_IRQ_TASKS        2
#define MAX_TOTAL            (MAX_TASKS_PERIODIC + MAX_IRQ_TASKS)

/* Primos: periódicas grandes, IRQs pequenos exclusivos */
const u32 PRIME_NUMS[MAX_TOTAL] = {
    /* Tasks periódicas 0..7 */
    2, 5, 11, 23, 7919, 36007, 144013, 720007,
    /* Tasks de IRQ 8..9 */
    13, 17
};

u8  OK[MAX_TOTAL]       = {0};
u8  READY[MAX_TOTAL]    = {0};
u8  DONE[MAX_TOTAL]     = {0};
u32 COUNTS[MAX_TOTAL]   = {0};

volatile u32 IRQ_OPS = 0;

#define OP_TIMER     (1u << 0)
#define OP_MAILBOX   (1u << 1)
#define MAX_IRQS     2

typedef struct {
    u32 tensao_mv;
    u32 corrente_ma;
    u32 temperatura_c;
    u32 estado_local;
    u8  debug_log[16];
} SharedData;

SharedData SHARED = {0};

u32 FAULT_LOG[MAX_TOTAL] = {0};

/* ---- ISRs (comuns) ---- */
void TIMER_IRQHandler(void) {
    IRQ_OPS |= OP_TIMER;
    TIMER->SR = 0;
}

void MAILBOX_IRQHandler(void) {
    IRQ_OPS |= OP_MAILBOX;
    MAILBOX->SR = 0;
}

/* ---- Tasks periódicas ---- */
u8 task0(void) { SHARED.tensao_mv = 3700; return PRIME_NUMS[0]; }
u8 task1(void) { SHARED.corrente_ma = 1500; return PRIME_NUMS[1]; }
u8 task2(void) { SHARED.temperatura_c = 35; return PRIME_NUMS[2]; }
u8 task3(void) { return PRIME_NUMS[3]; }
u8 task4(void) { return PRIME_NUMS[4]; }
u8 task5(void) { return PRIME_NUMS[5]; }
u8 task6(void) { return PRIME_NUMS[6]; }
u8 task7(void) { return PRIME_NUMS[7]; }

/* ---- Tasks de IRQ (comuns) ---- */
u8 task_irq_timer(void) {
    SHARED.estado_local = mmc_local();
    return PRIME_NUMS[8];
}

u8 task_irq_mailbox(void) {
    u32 msg = MAILBOX->RX;
    if (msg == 0xFFFFFFFF) { FAULT_LOG[9]++; return 0; }
    return PRIME_NUMS[9];
}

typedef u8 (*TaskFunc)(void);
const TaskFunc TASK_TABLE[MAX_TOTAL] = {
    task0, task1, task2, task3, task4, task5, task6, task7,
    task_irq_timer, task_irq_mailbox
};

const u8 IRQ_TASK_MAP[MAX_IRQS] = { 8, 9 };

/* =====================================================================
 * KERNEL COMUM — process(), run(), destroy()
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
 * Por que zerar COUNTS aqui?
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

/* ---- mmc local (usa OK[] preenchido por run()) ---- */
u32 mmc_local(void) {
    u32 soma = 0;
    for (u8 i = 0; i < MAX_TOTAL; i++) soma += OK[i];
    return soma;
}

/* =====================================================================
 * BLOCO IRQ() — roteia flags de hardware para tasks de IRQ
 * ===================================================================== */
void IRQ(void)
{
    u32 snapshot = __atomic_exchange_n(&IRQ_OPS, 0, __ATOMIC_SEQ_CST);
    for (u8 i = 0; i < MAX_IRQS; i++)
    {
        if (snapshot & (1u << i))
        {
            u8 tid = IRQ_TASK_MAP[i];
            OK[tid]   = TASK_TABLE[tid]();
            DONE[tid] = 1;
        }
    }
}

/* =====================================================================
 * BLOCO TASKS() — executa tasks periódicas via process/run/destroy
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
 * PARTE B — NÚCLEO ESCRAVO (EloidaOS puro)
 * =====================================================================
 * O escravo:
 *   - Roda seu próprio EloidaOS.
 *   - Responde à mailbox do mestre com seu veredito local (mmc_local).
 *   - Não tem flags personalizadas nem tabela verdade.
 * ===================================================================== */
u8 task_irq_mailbox_slave(void) {
    MAILBOX->TX = mmc_local();
    return PRIME_NUMS[9];
}

void main_slave(void) {
    while (1) {
        IRQ();
        TASKS();
        u32 soma = mmc_local();
        (void)soma;
    }
}

/* =====================================================================
 * PARTE C — NÚCLEO MESTRE (EloidaOS + integração global)
 * ===================================================================== */

/* ---- Flags personalizadas (estado dos núcleos remotos) ---- */
u32 USER_FLAGS = 0;

#define UF_CORE1_OK    (1u << 0)
#define UF_CORE2_OK    (1u << 1)
#define UF_CORE3_OK    (1u << 2)
#define UF_CORE1_FAULT (1u << 3)
#define UF_CORE2_FAULT (1u << 4)
#define UF_CORE3_FAULT (1u << 5)

/* ---- Primos das flags personalizadas (exclusivos) ---- */
#define PRIME_UF_CORE1_OK   (1u << 20)
#define PRIME_UF_CORE2_OK   (1u << 21)
#define PRIME_UF_CORE3_OK   (1u << 22)

/* ---- Constantes de veredito esperado de cada escravo ---- */
#define SOMA_OK_CORE1   (2 + 5 + 11 + 23 + 7919 + 36007 + 144013 + 720007 + 13 + 17)
#define SOMA_OK_CORE2   SOMA_OK_CORE1
#define SOMA_OK_CORE3   SOMA_OK_CORE1

/* ---- Task de IRQ da mailbox do mestre: recebe vereditos ---- */
u8 task_irq_mailbox_master(void)
{
    u32 veredito = MAILBOX->RX;
    u8  core_id  = (veredito >> 28) & 0x0F;
    u32 soma     = veredito & 0x0FFFFFFF;

    switch (core_id) {
        case 1:
            if (soma == SOMA_OK_CORE1) { USER_FLAGS |=  UF_CORE1_OK;
                                         USER_FLAGS &= ~UF_CORE1_FAULT; }
            else                       { USER_FLAGS |=  UF_CORE1_FAULT;
                                         USER_FLAGS &= ~UF_CORE1_OK; }
            break;
        case 2:
            if (soma == SOMA_OK_CORE2) { USER_FLAGS |=  UF_CORE2_OK;
                                         USER_FLAGS &= ~UF_CORE2_FAULT; }
            else                       { USER_FLAGS |=  UF_CORE2_FAULT;
                                         USER_FLAGS &= ~UF_CORE2_OK; }
            break;
        case 3:
            if (soma == SOMA_OK_CORE3) { USER_FLAGS |=  UF_CORE3_OK;
                                         USER_FLAGS &= ~UF_CORE3_FAULT; }
            else                       { USER_FLAGS |=  UF_CORE3_FAULT;
                                         USER_FLAGS &= ~UF_CORE3_OK; }
            break;
        default:
            FAULT_LOG[9]++;
            return 0;
    }
    return PRIME_NUMS[9];
}

/* ---- MMC do mestre: soma local + flags personalizadas ---- */
u32 mmc_master(void)
{
    u32 soma = mmc_local();
    if (USER_FLAGS & UF_CORE1_OK) soma += PRIME_UF_CORE1_OK;
    if (USER_FLAGS & UF_CORE2_OK) soma += PRIME_UF_CORE2_OK;
    if (USER_FLAGS & UF_CORE3_OK) soma += PRIME_UF_CORE3_OK;
    return soma;
}

/* ---- Tabela verdade global ---- */
#define CORE1_OK   (1u << 0)
#define CORE2_OK   (1u << 1)
#define CORE3_OK   (1u << 2)

typedef struct {
    u8 estado_cores;   /* bitmask: quais núcleos estão OK */
    u8 acao;           /* task a executar no mestre       */
} TabelaVerdade;

#define ACAO_MODO_NORMAL       0
#define ACAO_ASSUMIR_MOTOR     1
#define ACAO_ASSUMIR_LOGGING   2
#define ACAO_MODO_DEGRADADO    3
#define ACAO_FAILSAFE          4

const TabelaVerdade TABELA_GLOBAL[] = {
    { CORE1_OK | CORE2_OK | CORE3_OK, ACAO_MODO_NORMAL },
    { CORE2_OK | CORE3_OK,            ACAO_ASSUMIR_MOTOR },
    { CORE1_OK | CORE3_OK,            ACAO_ASSUMIR_LOGGING },
    { CORE3_OK,                       ACAO_MODO_DEGRADADO },
    { 0,                              ACAO_FAILSAFE },
};

#define NUM_REGRAS (sizeof(TABELA_GLOBAL)/sizeof(TabelaVerdade))

/* ---- Consulta global: mapeia estado dos cores -> ação ---- */
u8 CONSULTA_GLOBAL(void)
{
    u8 estado = 0;
    if (USER_FLAGS & UF_CORE1_OK) estado |= CORE1_OK;
    if (USER_FLAGS & UF_CORE2_OK) estado |= CORE2_OK;
    if (USER_FLAGS & UF_CORE3_OK) estado |= CORE3_OK;

    for (u8 i = 0; i < NUM_REGRAS; i++) {
        if (TABELA_GLOBAL[i].estado_cores == estado)
            return TABELA_GLOBAL[i].acao;
    }
    return ACAO_FAILSAFE;
}

/* ---- Desvio global do mestre: local + tabela verdade ---- */
u8 DESVIO_GLOBAL(u32 soma_local, u8 acao_global)
{
    /* Política local (tasks 0..7) */
    if (OK[0] == 0 && DONE[0]) return 7;   /* failsafe local */

    /* Política global (tabela verdade) */
    switch (acao_global) {
        case ACAO_MODO_NORMAL:      return 6;
        case ACAO_ASSUMIR_MOTOR:    return 3;
        case ACAO_ASSUMIR_LOGGING:  return 4;
        case ACAO_MODO_DEGRADADO:   return 5;
        case ACAO_FAILSAFE:         return 7;
        default:                    return 6;
    }
}

/* =====================================================================
 * MAIN DO MESTRE
 * =====================================================================
 * Fluxo:
 *   IRQ()        — trata IRQs (inclusive mailbox dos escravos)
 *   TASKS()      — process/run/destroy das tasks periódicas
 *   mmc_master() — soma local + flags personalizadas
 *   CONSULTA_GLOBAL() — tabela verdade -> ação global
 *   DESVIO_GLOBAL()   — política local + global -> próxima task
 *   executa task arbitrada
 * ===================================================================== */
void main_master(void)
{
    while (1)
    {
        IRQ();
        TASKS();

        u32 soma = mmc_master();
        u8 acao_global = CONSULTA_GLOBAL();
        u8 proxima = DESVIO_GLOBAL(soma, acao_global);

        OK[proxima]   = TASK_TABLE[proxima]();
        DONE[proxima] = 1;
    }
}