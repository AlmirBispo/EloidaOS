/*
ELOIDA OS - Exemplo BMS com interdependencia e MMC por primos
Risc-V, CH32V307VCT6 @ 144 MHz
CLOCK PRINCIPAL (144 MHz) como base de tempo
Cada task retorna seu proprio primo (ou 0 em falha)
MMC soma os retornos -> numero unico
Tabela de desvio mapeia soma -> task a executar
*/

typedef unsigned char u8;
typedef unsigned int  u32;

#define MAX_TASKS    15

/* ==================================================================
 * 1. TABELA DE PRIMOS - periodo em ciclos de clock (144 MHz)
 * ==================================================================
 * Primos escolhidos para dar periodos uteis em 144 MHz:
 *   - Primos pequenos (2..47)  -> dezenas a centenas de ns
 *   - Primos medios            -> microssegundos
 *   - Primos grandes           -> milissegundos
 * ================================================================== */
const u32 PRIME_NUMS[MAX_TASKS] = {
     2,  3,  5,  7, 11,       /* tasks 0..4  - criticas   (ns)      */
    13, 17, 19, 23, 29,       /* tasks 5..9  - medias    (ns/us)    */
    31, 37, 41, 43, 47        /* tasks 10..14 - lentas   (us)       */
};

/* ==================================================================
 * 2. ESTADO DAS TASKS
 * ================================================================== */
u8  READY[MAX_TASKS]  = {0};   /* flag: contador estourou        */
u32 COUNTS[MAX_TASKS] = {0};   /* contador de ciclos de clock    */
u8  OK[MAX_TASKS]     = {0};   /* ultimo retorno (primo ou 0)    */
u8  DONE[MAX_TASKS]   = {0};   /* ja executou ao menos 1x        */

/* ==================================================================
 * 3. IPC - dados compartilhados entre tasks
 * ================================================================== */
typedef struct {
    u32 tensao_mv;        /* task0 preenche                        */
    u32 corrente_ma;      /* task1 preenche                        */
    u32 temperatura_c;    /* task2 preenche                        */
    u32 soc_pct;          /* task3 preenche                        */
    u32 balanceamento;    /* task4 preenche                        */
    u32 protecao;         /* task5 preenche                        */
    u32 modo;             /* task6 define                          */
    u8  debug_log[16];
} SharedData;

SharedData SHARED = {0};

/* ==================================================================
 * 4. TAREFAS - cada uma retorna seu proprio primo (ou 0 em falha)
 * ==================================================================
 *
 * Regra geral:
 *   - Se a leitura/calculo e valido  -> retorna PRIME_NUMS[i]
 *   - Se ha falha ou dado invalido   -> retorna 0
 *
 * Task com dependencia so executa se dependencias estao OK.
 * ================================================================== */

/* ------------------------------------------------------------------
 * task0 - primo 2 - le tensao da celula
 * Sem dependencia. Retorna 2 se tensao valida, 0 se fora da faixa.
 * ------------------------------------------------------------------ */
u8 task0(void)
{
    u32 v = 3700;                    /* simulacao: 3.7 V       */
    if (v < 2500 || v > 4200) return 0;   /* fora da faixa      */
    SHARED.tensao_mv = v;
    return PRIME_NUMS[0];            /* 2                      */
}

/* ------------------------------------------------------------------
 * task1 - primo 3 - le corrente
 * Sem dependencia. Retorna 3 se corrente valida, 0 se fora.
 * ------------------------------------------------------------------ */
u8 task1(void)
{
    u32 i = 1500;                    /* simulacao: 1.5 A       */
    if (i > 5000) return 0;          /* sobrecorrente          */
    SHARED.corrente_ma = i;
    return PRIME_NUMS[1];            /* 3                      */
}

/* ------------------------------------------------------------------
 * task2 - primo 5 - le temperatura
 * Sem dependencia. Retorna 5 se temperatura valida, 0 se fora.
 * ------------------------------------------------------------------ */
u8 task2(void)
{
    u32 t = 35;                      /* simulacao: 35 C        */
    if (t > 60) return 0;            /* sobretemperatura       */
    SHARED.temperatura_c = t;
    return PRIME_NUMS[2];            /* 5                      */
}

/* ------------------------------------------------------------------
 * task3 - primo 7 - calcula SOC
 * DEPENDE de task0 (tensao) e task1 (corrente).
 * So executa se ambas OK. Retorna 7 se SOC calculado, 0 se falha.
 * ------------------------------------------------------------------ */
u8 task3(void)
{
    if (!DONE[0] || !OK[0]) return 0;   /* tensao nao OK      */
    if (!DONE[1] || !OK[1]) return 0;   /* corrente nao OK    */

    SHARED.soc_pct = (SHARED.tensao_mv - 2500) / 17;
    if (SHARED.soc_pct > 100) SHARED.soc_pct = 100;
    return PRIME_NUMS[3];               /* 7                  */
}

/* ------------------------------------------------------------------
 * task4 - primo 11 - balanceia celulas
 * DEPENDE de task0 (tensao).
 * ------------------------------------------------------------------ */
u8 task4(void)
{
    if (!DONE[0] || !OK[0]) return 0;

    SHARED.balanceamento = (SHARED.tensao_mv > 4000) ? 1 : 0;
    return PRIME_NUMS[4];               /* 11                 */
}

/* ------------------------------------------------------------------
 * task5 - primo 13 - protecao
 * DEPENDE de task1 (corrente) e task2 (temperatura).
 * ------------------------------------------------------------------ */
u8 task5(void)
{
    if (!DONE[1] || !OK[1]) return 0;
    if (!DONE[2] || !OK[2]) return 0;

    if (SHARED.corrente_ma > 4500 || SHARED.temperatura_c > 55)
        SHARED.protecao = 1;            /* ativa protecao     */
    else
        SHARED.protecao = 0;

    return PRIME_NUMS[5];               /* 13                 */
}

/* ------------------------------------------------------------------
 * task6 - primo 17 - supervisor
 * DEPENDE de task3, task4, task5 (as tres logicas de alto nivel).
 * Define o modo de operacao do sistema.
 * ------------------------------------------------------------------ */
u8 task6(void)
{
    if (!DONE[3] || !OK[3]) return 0;
    if (!DONE[4] || !OK[4]) return 0;
    if (!DONE[5] || !OK[5]) return 0;

    if (SHARED.protecao)
        SHARED.modo = 0;                /* EMERGENCIA         */
    else if (SHARED.soc_pct < 20)
        SHARED.modo = 1;                /* ECONOMIA           */
    else
        SHARED.modo = 2;                /* NORMAL             */

    return PRIME_NUMS[6];               /* 17                 */
}

/* Tasks 7..14 - reserva / logging (sem dependencia critica) */
u8 task7 (void) { return PRIME_NUMS[7];  }
u8 task8 (void) { return PRIME_NUMS[8];  }
u8 task9 (void) { return PRIME_NUMS[9];  }
u8 task10(void) { return PRIME_NUMS[10]; }
u8 task11(void) { return PRIME_NUMS[11]; }
u8 task12(void) { return PRIME_NUMS[12]; }
u8 task13(void) { return PRIME_NUMS[13]; }
u8 task14(void) { return PRIME_NUMS[14]; }

/* ==================================================================
 * 5. TABELA DE DESPACHO
 * ================================================================== */
typedef u8 (*TaskFunc)(void);
const TaskFunc TASK_TABLE[MAX_TASKS] = {
    task0,  task1,  task2,  task3,  task4,
    task5,  task6,  task7,  task8,  task9,
    task10, task11, task12, task13, task14
};

/* ==================================================================
 * 6. MMC - soma de primos das tasks criticas
 * ==================================================================
 * As tasks 0..6 formam o nucleo do BMS. O MMC soma os retornos
 * (cada um e o proprio primo se OK, 0 se falha).
 *
 * Soma possivel: 0 .. (2+3+5+7+11+13+17) = 58
 * Cada combinacao de OKs produz uma soma unica (primos distintos).
 * ================================================================== */
u32 mmc(void)
{
    return OK[0] + OK[1] + OK[2] + OK[3] + OK[4] + OK[5] + OK[6];
}

/* ==================================================================
 * 7. TABELA DE DESVIO - mapeia soma MMC -> task a executar
 * ==================================================================
 * Cada soma identifica um estado do sistema. O desvio aponta
 * qual task deve assumir o controle naquele estado.
 *
 * Somas notaveis:
 *   0             = nada OK          -> failsafe (task7)
 *   2+3+5 = 10    = sensores OK      -> calcula SOC (task3)
 *   2+3+5+7 = 17  = SOC pronto       -> balanceia (task4)
 *   2+3+5+7+11+13+17 = 58 = tudo OK  -> supervisor (task6)
 * ================================================================== */

/* Indice = soma MMC, valor = task a executar */
u8 DESVIO(u32 soma)
{
    /* Soma 0 - nenhuma dependencia OK -> modo de emergencia */
    if (soma == 0) return 7;              /* task7 = failsafe    */

    /* Apenas sensores basicos OK -> calcula SOC */
    if (soma == (2 + 3 + 5)) return 3;    /* 10 -> task3         */

    /* Sensores + SOC -> balanceamento */
    if (soma == (2 + 3 + 5 + 7)) return 4;/* 17 -> task4         */

    /* Sensores + SOC + balanceamento -> protecao */
    if (soma == (2 + 3 + 5 + 7 + 11)) return 5; /* 28 -> task5   */

    /* Tudo OK -> supervisor */
    if (soma == (2 + 3 + 5 + 7 + 11 + 13 + 17)) return 6; /* 58   */

    /* Qualquer outro estado -> supervisor decide */
    return 6;
}

/* ==================================================================
 * 8. ISR DO CLOCK - chamada a cada ciclo de 144 MHz (ou via SysTick)
 * ==================================================================
 * NOTA: em 144 MHz, um contador de 32 bits estoura em ~29.8 s.
 * Para periodos maiores, use prescaler ou contador de 64 bits.
 * ================================================================== */
void SysTick_Handler(void)
{
    u8 i;
    for (i = 0; i < MAX_TASKS; i++) {
        if (++COUNTS[i] >= PRIME_NUMS[i]) {
            COUNTS[i] = 0;
            READY[i]  = 1;
        }
    }
}

/* ==================================================================
 * 9. EXECUCAO DE UMA TASK COM CAPTURA DE VEREDITO
 * ==================================================================
 * Executa a task tid, captura o retorno (primo ou 0), armazena
 * em OK[tid] e marca DONE[tid].
 * ================================================================== */
void execute(u8 tid)
{
    u8 retorno;

    retorno = TASK_TABLE[tid]();   /* task retorna seu primo ou 0 */
    OK[tid]   = retorno;
    DONE[tid] = 1;
    READY[tid] = 0;
}

/* ==================================================================
 * 10. MAIN LOOP
 * ==================================================================
 * Para cada ciclo:
 *   (a) Executa todas as tasks cujo contador estourou
 *   (b) Calcula a soma MMC
 *   (c) Consulta a tabela de desvio
 *   (d) Executa a task indicada (arbitragem)
 * ================================================================== */
void main(void)
{
    u8 i;
    u32 soma;
    u8 proxima;

    while (1)
    {
        /* --- (a) executa tasks periodicas ---------------------- */
        for (i = 0; i < MAX_TASKS; i++) 
        {
            if (READY[i]) 
            {
                execute(i);
            }
        }

        /* --- (b) correlaciona via MMC -------------------------- */
        soma = mmc();

        /* --- (c) consulta tabela de desvio --------------------- */
        proxima = DESVIO(soma);

        /* --- (d) executa a task arbitrada ---------------------- */
        execute(proxima);
    }
}