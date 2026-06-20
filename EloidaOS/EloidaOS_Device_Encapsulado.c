/*
=================================================================================
 Sistema Operacional baseado em numeros primos para economia de recursos "ELOIDA OS"
 Autor: Almir Bispo
 Arquitetura: RISC-V, CH32V307VCT6
 Atualizado: Loop for() encapsulado na função Device() + Preempção Segura via destroy()
=================================================================================
*/

#include "ch32v30x.h" // Biblioteca oficial do fabricante WCH para o CH32V307

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define MAX_PROCESSES 255
#define LONG_TASK_ID  11  

// Variáveis do Kernel do ELOIDA OS
volatile u8 PROCESS[MAX_PROCESSES] = {0};
u32 COUNTS[MAX_PROCESSES] = {0};

// Tabela de Primos na FLASH (const u16) para Dispersão Harmônica de Carga
const u16 PRIME_NUMS[255] = {
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

// Macro de Checkpoint para tarefas longas abortáveis
#define ELOIDA_CHECKPOINT(id) if (PROCESS[id] == 0) return;

// IPC Cooperativo (SharedData)
typedef struct {
    u32 sensor_port_A;    
    u32 sensor_port_B;    
    u32 system_status;    
} SharedData;

volatile SharedData SHARED = {0};

// =================================================================================
// FUNÇÕES DE GERENCIAMENTO DO KERNEL
// =================================================================================
void process_create(u8 pointer) {
    if (pointer < MAX_PROCESSES) PROCESS[pointer] = 0;
}

void process(u32 Limit_count, u8 pointer) {
    if (pointer < MAX_PROCESSES) {
        COUNTS[pointer]++;
        if (COUNTS[pointer] >= Limit_count) {
            PROCESS[pointer] = 1;
            COUNTS[pointer] = 0;
        }
    }
}

void destroy(u8 any_process) {
    if (any_process < MAX_PROCESSES) PROCESS[any_process] = 0;
}

// =================================================================================
// TAREFAS DO USUÁRIO
// =================================================================================
void task0(void) { /* Lógica de Escala Alta (Primos baixos) */ }
void task1(void) { /* Lógica de Escala Alta */ }

// Tarefa longa protegida por checkpoints
void task11(void) {
    for (u32 i = 0; i < 10000; i++) {
        ELOIDA_CHECKPOINT(LONG_TASK_ID); // Aborta se destroy() foi invocado pela ISR
        
        // Execução da lógica do usuário...
        __asm__("nop"); 
        
        ELOIDA_CHECKPOINT(LONG_TASK_ID);
    }
}

typedef void (*TaskFunc)(void);
const TaskFunc TASK_TABLE[12] = {
    task0, task1, 0, 0, 0, 0, 0, 0, 0, 0, 0, task11
};

void run(u8 any_process) {
    if (any_process < MAX_PROCESSES && PROCESS[any_process] == 1) {
        if (any_process < 12 && TASK_TABLE[any_process] != 0) {
            TASK_TABLE[any_process](); 
        }
        destroy(any_process); // Auto-limpeza segura via método do Kernel
    }
}

// =================================================================================
// INTERRUPÇÕES DE HARDWARE (REATIVIDADE / PREEMPÇÃO SEGURA)
// =================================================================================
void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        destroy(LONG_TASK_ID); // Método com condicional para proteção de memória
        SHARED.sensor_port_A++;
        EXTI_ClearITPendingBit(EXTI_Line0);
    }
}

void EXTI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI1_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        destroy(LONG_TASK_ID); // Aborta preemptivamente a tarefa se a outra porta disparar
        SHARED.sensor_port_B++;
        EXTI_ClearITPendingBit(EXTI_Line1);
    }
}

// =================================================================================
// ENCAPSULAMENTO DO ESCALONADOR DO PROJETO
// =================================================================================
void Device(void) {
    u8 i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        // Escalonamento por primos (índices baixos) ou offset fixo
        u32 limit = (i < 3) ? PRIME_NUMS[i] : (1000 + i);
        
        process(limit, i);
        run(i); 
    }
}

// Hardware Init do CH32V307
void GPIO_Interrupt_Config(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);
    
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    EXTI_InitStructure.EXTI_Line = EXTI_Line0 | EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
}

// =================================================================================
// MAIN LOOP MODULAR
// =================================================================================
int main(void) {
    GPIO_Interrupt_Config();
    
    while(1) {
        Device(); // Ciclo contínuo de processamento e escalonamento harmônico
    }
}
