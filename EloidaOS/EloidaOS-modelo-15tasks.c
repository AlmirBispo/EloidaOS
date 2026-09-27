/*
ELOIDA OS - versao 15 tarefas, kernel enxuto
Risc-V, CH32V307VCT6 @ 144 MHz
Contadores de 32 bits, 4 faixas temporais (ns / ms / cs / s)

==========================================================================
 TABELA DE TEMPOS (estimativa com ~450 ns por varredura do main loop)
==========================================================================
  #   PRIMO          PERIODO APROX.        FAIXA     USO TIPICO
--------------------------------------------------------------------------
  0          2      ~0.9  us              ns        sensor rapido / debounce
  1          5      ~2.25 us              ns        filtro IIR rapido
  2         11      ~4.95 us              ns        controle PWM
  3         23      ~10.4 us              ns        aquisicao ADC
  4       7919      ~3.56 ms              ms        filtro FIR / media movel
  5      36007      ~16.2 ms              ms        comunicacao UART/CAN
  6     144013      ~64.8 ms              ms        refresh de display
  7     720007      ~324  ms              ms        gestao de energia
  8    1800001      ~810  ms              cs        watchdog feed
  9    3600007      ~1.62 s               cs        heartbeat / LED status
 10    7200011      ~3.24 s               cs        logging em flash
 11   14400013      ~6.48 s               s         PID lento / supervisor
 12   28800017      ~12.96 s              s         sincronizacao RTC
 13   57600023      ~25.9 s               s         autoteste / calibracao
 14  115200029      ~51.8 s               s         processamento pesado
==========================================================================
*/

typedef unsigned char u8;
typedef unsigned int  u32;

#define MAX_TASKS    15

/* ------------------------------------------------------------------
 * TABELA DE PRIORIDADES PRIMAS - 15 TAREFAS
 * (comentario de tempo ao lado de cada primo)
 * ------------------------------------------------------------------ */
const u32 PRIME_NUMS[MAX_TASKS] = {
    /* FAIXA ns ------------------------------------------------- */
          2,          /* task0  -> ~0.9  us  | maxima prioridade    */
          5,          /* task1  -> ~2.25 us                        */
         11,          /* task2  -> ~4.95 us                        */
         23,          /* task3  -> ~10.4 us                        */
    /* FAIXA ms ------------------------------------------------- */
       7919,          /* task4  -> ~3.56 ms                        */
      36007,          /* task5  -> ~16.2 ms                        */
     144013,          /* task6  -> ~64.8 ms                        */
     720007,          /* task7  -> ~324  ms                        */
    /* FAIXA cs ------------------------------------------------- */
    1800001,          /* task8  -> ~810  ms                        */
    3600007,          /* task9  -> ~1.62 s                         */
    7200011,          /* task10 -> ~3.24 s                         */
    /* FAIXA s  ------------------------------------------------- */
   14400013,          /* task11 -> ~6.48 s                         */
   28800017,          /* task12 -> ~12.96 s                        */
   57600023,          /* task13 -> ~25.9 s                         */
  115200029           /* task14 -> ~51.8 s   | menor prioridade     */
};

/* ------------------------------------------------------------------
 * ESTADO DAS TAREFAS
 * ------------------------------------------------------------------ */
u8  READY [MAX_TASKS] = {0};
u32 COUNTS[MAX_TASKS] = {0};

/* ------------------------------------------------------------------
 * IPC COOPERATIVO
 * ------------------------------------------------------------------ */
typedef struct {
    u32 sensor_raw;
    u32 sensor_filtered;
    u32 actuator_cmd;
    u32 system_status;
    u8  debug_log[16];
} SharedData;

SharedData SHARED = {0};

/* ==================================================================
 * TAREFAS 0..14
 * ================================================================== */

/* ------------------------------------------------------------------
 * task0  -  primo 2  -  ~0.9 us  -  FAIXA ns
 * ------------------------------------------------------------------
 * Prioridade MAXIMA. Roda a cada ~2 varreduras.
 * Deve ser ULTRA-RAPIDA: apenas leitura e escrita de registrador.
 * Ideal para: leitura de ADC, debounce de botao, captura de encoder.
 * ------------------------------------------------------------------ */
void task0(void) { SHARED.sensor_raw = 42; }

/* ------------------------------------------------------------------
 * task1  -  primo 5  -  ~2.25 us  -  FAIXA ns
 * ------------------------------------------------------------------
 * Alta prioridade. Filtro simples (media, IIR de 1a ordem).
 * Cuidado: ainda esta na faixa de microssegundos.
 * ------------------------------------------------------------------ */
void task1(void) { SHARED.sensor_filtered = (SHARED.sensor_raw * 3) / 2; }

/* ------------------------------------------------------------------
 * task2  -  primo 11  -  ~4.95 us  -  FAIXA ns
 * ------------------------------------------------------------------
 * Controle de atuador (PWM, rele). Decide com base no filtrado.
 * ------------------------------------------------------------------ */
void task2(void) { SHARED.actuator_cmd = (SHARED.sensor_filtered > 60) ? 1 : 0; }

/* ------------------------------------------------------------------
 * task3  -  primo 23  -  ~10.4 us  -  FAIXA ns
 * ------------------------------------------------------------------
 * Ultima task da faixa ns. Ainda deve ser curta (< ~5 us de CPU).
 * ------------------------------------------------------------------ */
void task3(void) { SHARED.system_status |= 0x01; }

/* ------------------------------------------------------------------
 * task4  -  primo 7919  -  ~3.56 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * Inicio da faixa de milissegundos. Tem ~3.5 ms entre execucoes.
 * Ideal para: filtro FIR, media movel, controle de motor.
 * ------------------------------------------------------------------ */
void task4(void) { SHARED.system_status |= 0x02; }

/* ------------------------------------------------------------------
 * task5  -  primo 36007  -  ~16.2 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~16 ms = ~60 Hz. Sincronizado com frame rate tipico.
 * Ideal para: comunicacao UART/CAN, parsing de protocolo.
 * ------------------------------------------------------------------ */
void task5(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task6  -  primo 144013  -  ~64.8 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~65 ms = ~15 Hz. Refresh de display, atualizacao de UI.
 * ------------------------------------------------------------------ */
void task6(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task7  -  primo 720007  -  ~324 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * Ultima task da faixa ms. Gestao de energia, modos de operacao.
 * ------------------------------------------------------------------ */
void task7(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task8  -  primo 1800001  -  ~810 ms  -  FAIXA cs
 * ------------------------------------------------------------------
 * ~0.8 s. Watchdog feed, verificacao de sanidade do sistema.
 * ------------------------------------------------------------------ */
void task8(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task9  -  primo 3600007  -  ~1.62 s  -  FAIXA cs
 * ------------------------------------------------------------------
 * ~1.6 s. Heartbeat, pisca LED de status.
 * ------------------------------------------------------------------ */
void task9(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task10  -  primo 7200011  -  ~3.24 s  -  FAIXA cs
 * ------------------------------------------------------------------
 * ~3.2 s. Logging em flash, persistencia de dados.
 * ------------------------------------------------------------------ */
void task10(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task11  -  primo 14400013  -  ~6.48 s  -  FAIXA s
 * ------------------------------------------------------------------
 * ~6.5 s. PID lento, supervisor de estados, maquina de estados.
 * ------------------------------------------------------------------ */
void task11(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task12  -  primo 28800017  -  ~12.96 s  -  FAIXA s
 * ------------------------------------------------------------------
 * ~13 s. Sincronizacao com RTC, ajuste de clock.
 * ------------------------------------------------------------------ */
void task12(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task13  -  primo 57600023  -  ~25.9 s  -  FAIXA s
 * ------------------------------------------------------------------
 * ~26 s. Autoteste, calibracao de sensores, diagnostico.
 * ------------------------------------------------------------------ */
void task13(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task14  -  primo 115200029  -  ~51.8 s  -  FAIXA s
 * ------------------------------------------------------------------
 * Prioridade MINIMA. ~52 s entre execucoes.
 * Tempo de sobra para: processamento pesado, criptografia,
 * compactacao de logs, atualizacao de firmware, etc.
 * ------------------------------------------------------------------ */
void task14(void) { /* processamento pesado */ }

/* ------------------------------------------------------------------
 * TABELA DE DESPACHO
 * ------------------------------------------------------------------ */
typedef void (*TaskFunc)(void);
const TaskFunc TASK_TABLE[MAX_TASKS] = {
    task0,  task1,  task2,  task3,  task4,
    task5,  task6,  task7,  task8,  task9,
    task10, task11, task12, task13, task14
};

/* ------------------------------------------------------------------
 * KERNEL - sem verificacao de indice (pre-condicao: tid < MAX_TASKS)
 * ------------------------------------------------------------------ */
void process(u32 limit, u8 tid)
{
    if (++COUNTS[tid] >= limit) 
    {
        READY[tid] = 1;
        COUNTS[tid] = 0;
    }
}

void run(u8 tid)
{
    if (READY[tid]) 
    {
        TASK_TABLE[tid]();
    }
}

void destroy(u8 tid)
{
    READY[tid] = 0;
}

/* ------------------------------------------------------------------
 * MAIN LOOP
 * ------------------------------------------------------------------ */
void main(void)
{
    u8 i;

    while (1)
    {
        for (i = 0; i < MAX_TASKS; i++)
        {
            process(PRIME_NUMS[i], i);
            run(i);
            destroy(i);
        }
    }
}