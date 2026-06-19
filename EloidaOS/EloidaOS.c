/*
Sistema Operacional baseado em numeros primos para economia de recursos "ELOIDA OS"
Autor: Almir Bispo
Risc-V, CH32V307VCT6
22/01/2026
Atualizado: IPC via SharedData + Tabela de 12 tarefas
*/

typedef unsigned char u8;
typedef unsigned int  u32;

#define MAX_PROCESSES 255

u8 PROCESS[MAX_PROCESSES] = {0};
u32 COUNTS[MAX_PROCESSES] = {0};

u32 PRIME_NUMS[255] = {
2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131, 137, 139, 149, 151,
157, 163, 167, 173, 179, 181, 191, 193, 197, 199, 211, 223, 227, 229, 233,
239, 241, 251, 257, 263, 269, 271, 277, 281, 283, 293, 307, 311, 313, 317,
331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389, 397, 401, 409, 419,
421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491, 499, 503,
509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607,
613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701,
709, 719, 727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811,
821, 823, 827, 829, 839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911,
919, 929, 937, 941, 947, 953, 967, 971, 977, 983, 991, 997, 1009, 1013, 1019,
1021, 1031, 1033, 1039, 1049, 1051, 1061, 1063, 1069, 1087, 1091, 1093, 1097,
1103, 1109, 1117, 1123, 1129, 1151, 1153, 1163, 1171, 1181, 1187, 1193, 1201,
1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259, 1277, 1279, 1283, 1289, 1291,
1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367, 1373, 1381, 1399, 1409,
1423, 1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471, 1481, 1483, 1487,
1489, 1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567, 1571, 1579,
1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663, 1667,
1669, 1693, 1697, 1699, 1709, 65521
};

// =========================================================
// OPÇÃO 2: TIPO COMPARTILHADO (IPC COOPERATIVO)
// =========================================================
typedef struct {
    u32 sensor_raw;       // Preenchido por task0
    u32 sensor_filtered;  // Preenchido por task1
    u32 actuator_cmd;     // Preenchido por task2
    u32 system_status;    // Status geral / flags
    u8  debug_log[16];    // Buffer circular ou mensagens
} SharedData;

// Instância global. Como o scheduler é cooperativo, não precisa de mutex.
SharedData SHARED = {0};

// =========================================================
// TAREFAS 0 a 11 (assinatura void, acessam SHARED diretamente)
// =========================================================
void task0(void) { SHARED.sensor_raw = 42; } // Ex: leitura de ADC/I2C
void task1(void) { SHARED.sensor_filtered = (SHARED.sensor_raw * 3) / 2; } // Ex: filtro
void task2(void) { SHARED.actuator_cmd = (SHARED.sensor_filtered > 60) ? 1 : 0; } // Ex: PWM/Relé
void task3(void)  { SHARED.system_status |= 0x08; }
void task4(void)  { /* lógica customizada */ }
void task5(void)  { /* lógica customizada */ }
void task6(void)  { /* lógica customizada */ }
void task7(void)  { /* lógica customizada */ }
void task8(void)  { /* lógica customizada */ }
void task9(void)  { /* lógica customizada */ }
void task10(void) { /* lógica customizada */ }
void task11(void) { /* lógica customizada */ }

// Tabela de despacho estática (zero overhead, resolvida em compilação)
typedef void (*TaskFunc)(void);
const TaskFunc TASK_TABLE[12] = {
    task0, task1, task2, task3, task4, task5,
    task6, task7, task8, task9, task10, task11
};

// =========================================================
// FUNÇÕES DO KERNEL DO ELOIDA OS
// =========================================================
void process_create(u8 pointer)
{
    if (pointer < MAX_PROCESSES) PROCESS[pointer] = 0;
}

void process(u32 Limit_count, u8 pointer)
{
    if (pointer < MAX_PROCESSES) {
        COUNTS[pointer]++;
        if (COUNTS[pointer] >= Limit_count) {
            PROCESS[pointer] = 1;
            COUNTS[pointer] = 0;
        }
    }
}

void run(u8 any_process)
{
    if (any_process < MAX_PROCESSES && PROCESS[any_process] == 1) {
        if (any_process < 12) {
            TASK_TABLE[any_process](); // Invoca a tarefa associada ao ID
        } else {
            // Fallback para processos 12~254
            // Ex: toggle_generic_led(any_process);
        }
    }
}

void destroy(u8 any_process)
{
    if (any_process < MAX_PROCESSES) PROCESS[any_process] = 0;
}

// =========================================================
// MAIN LOOP
// =========================================================
void main(void)
{
    u8 i;
    while(1) 
    {
        for (i = 0; i < MAX_PROCESSES; i++) 
        {
            if (i < 3) 
            {
                process(PRIME_NUMS[i], i);
            } else
             {
                process(1000 + i, i);
            }

            run(i);    // Despacha para a tabela ou fallback
            destroy(i);// Limpa a flag para o próximo ciclo
        }
    }
}