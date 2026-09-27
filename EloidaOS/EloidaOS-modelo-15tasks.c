/*
ELOIDA OS - versao 15 tarefas, kernel enxuto
Risc-V, CH32V307VCT6 @ 144 MHz
Contadores de 32 bits baseados no CLOCK PRINCIPAL (144 MHz)
==========================================================================
 TABELA DE TEMPOS (estimativa com ~450 ns por varredura do main loop)
==========================================================================
  #   PRIMO          PERIODO APROX.        FAIXA     USO TIPICO
--------------------------------------------------------------------------
  0          2      ~13.9 ns              ns        sensor rapido / debounce
  1          5      ~34.7 ns              ns        filtro IIR rapido
  2         11      ~76.4 ns              ns        controle PWM
  3         23      ~159.7 ns             ns        aquisicao ADC
  4       7919      ~55.0 us              us        filtro FIR / media movel
  5      36007      ~250.0 us             us        comunicacao UART/CAN
  6     144013      ~1.00 ms              ms        refresh de display
  7     720007      ~5.00 ms              ms        gestao de energia
  8    1800001      ~12.5 ms              ms        watchdog feed
  9    3600007      ~25.0 ms              ms        heartbeat / LED status
 10    7200011      ~50.0 ms              ms        logging em flash
 11   14400013      ~100.0 ms             ms        PID lento / supervisor
 12   28800017      ~200.0 ms             ms        sincronizacao RTC
 13   57600023      ~400.0 ms             ms        autoteste / calibracao
 14  115200029      ~800.0 ms             ms        processamento pesado
==========================================================================
*/

typedef unsigned char u8;
typedef unsigned int  u32;

#define MAX_TASKS    15

/* ------------------------------------------------------------------
 * TABELA DE PRIORIDADES PRIMAS - 15 TAREFAS
 * (comentario de tempo ao lado de cada primo, base 144 MHz)
 * ------------------------------------------------------------------ */
const u32 PRIME_NUMS[MAX_TASKS] = {
    /* FAIXA ns ------------------------------------------------- */
          2,          /* task0  -> ~13.9 ns  | maxima prioridade  */
          5,          /* task1  -> ~34.7 ns                       */
         11,          /* task2  -> ~76.4 ns                       */
         23,          /* task3  -> ~159.7 ns                      */
    /* FAIXA us ------------------------------------------------- */
       7919,          /* task4  -> ~55.0 us                       */
      36007,          /* task5  -> ~250.0 us                      */
    /* FAIXA ms ------------------------------------------------- */
     144013,          /* task6  -> ~1.00 ms                       */
     720007,          /* task7  -> ~5.00 ms                       */
    1800001,          /* task8  -> ~12.5 ms                       */
    3600007,          /* task9  -> ~25.0 ms                       */
    7200011,          /* task10 -> ~50.0 ms                       */
   14400013,          /* task11 -> ~100.0 ms                      */
   28800017,          /* task12 -> ~200.0 ms                      */
   57600023,          /* task13 -> ~400.0 ms                      */
  115200029           /* task14 -> ~800.0 ms  | menor prioridade  */
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
 * task0  -  primo 2  -  ~13.9 ns  -  FAIXA ns
 * ------------------------------------------------------------------
 * Prioridade MAXIMA. Roda a cada ~2 ciclos de clock.
 * Deve ser ULTRA-RAPIDA: apenas leitura e escrita de registrador.
 * Ideal para: leitura de ADC, debounce de botao, captura de encoder.
 * ------------------------------------------------------------------ */
void task0(void) { SHARED.sensor_raw = 42; }

/* ------------------------------------------------------------------
 * task1  -  primo 5  -  ~34.7 ns  -  FAIXA ns
 * ------------------------------------------------------------------
 * Alta prioridade. Filtro simples (media, IIR de 1a ordem).
 * Cuidado: ainda esta na faixa de nanossegundos.
 * ------------------------------------------------------------------ */
void task1(void) { SHARED.sensor_filtered = (SHARED.sensor_raw * 3) / 2; }

/* ------------------------------------------------------------------
 * task2  -  primo 11  -  ~76.4 ns  -  FAIXA ns
 * ------------------------------------------------------------------
 * Controle de atuador (PWM, rele). Decide com base no filtrado.
 * ------------------------------------------------------------------ */
void task2(void) { SHARED.actuator_cmd = (SHARED.sensor_filtered > 60) ? 1 : 0; }

/* ------------------------------------------------------------------
 * task3  -  primo 23  -  ~159.7 ns  -  FAIXA ns
 * ------------------------------------------------------------------
 * Ultima task da faixa ns. Ainda deve ser curta (< ~50 ns de CPU).
 * ------------------------------------------------------------------ */
void task3(void) { SHARED.system_status |= 0x01; }

/* ------------------------------------------------------------------
 * task4  -  primo 7919  -  ~55.0 us  -  FAIXA us
 * ------------------------------------------------------------------
 * Inicio da faixa de microssegundos. Tem ~55 us entre execucoes.
 * Ideal para: filtro FIR, media movel, controle de motor.
 * ------------------------------------------------------------------ */
void task4(void) { SHARED.system_status |= 0x02; }

/* ------------------------------------------------------------------
 * task5  -  primo 36007  -  ~250.0 us  -  FAIXA us
 * ------------------------------------------------------------------
 * ~250 us. Comunicacao UART/CAN, parsing de protocolo.
 * ------------------------------------------------------------------ */
void task5(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task6  -  primo 144013  -  ~1.00 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~1 ms = 1 kHz. Refresh de display, atualizacao de UI.
 * ------------------------------------------------------------------ */
void task6(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task7  -  primo 720007  -  ~5.00 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~5 ms = 200 Hz. Gestao de energia, modos de operacao.
 * ------------------------------------------------------------------ */
void task7(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task8  -  primo 1800001  -  ~12.5 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~12.5 ms = 80 Hz. Watchdog feed, verificacao de sanidade.
 * ------------------------------------------------------------------ */
void task8(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task9  -  primo 3600007  -  ~25.0 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~25 ms = 40 Hz. Heartbeat, pisca LED de status.
 * ------------------------------------------------------------------ */
void task9(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task10  -  primo 7200011  -  ~50.0 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~50 ms = 20 Hz. Logging em flash, persistencia de dados.
 * ------------------------------------------------------------------ */
void task10(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task11  -  primo 14400013  -  ~100.0 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~100 ms = 10 Hz. PID lento, supervisor de estados.
 * ------------------------------------------------------------------ */
void task11(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task12  -  primo 28800017  -  ~200.0 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~200 ms = 5 Hz. Sincronizacao com RTC, ajuste de clock.
 * ------------------------------------------------------------------ */
void task12(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task13  -  primo 57600023  -  ~400.0 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * ~400 ms = 2.5 Hz. Autoteste, calibracao de sensores.
 * ------------------------------------------------------------------ */
void task13(void) { /* logica customizada */ }

/* ------------------------------------------------------------------
 * task14  -  primo 115200029  -  ~800.0 ms  -  FAIXA ms
 * ------------------------------------------------------------------
 * Prioridade MINIMA. ~800 ms entre execucoes.
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