/*
=================================================================================
 Sistema Operacional baseado em numeros primos para economia de recursos "ELOIDA OS"
 Exemplo Prático: Implementando Preempção via Interrupções de Hardware (ISR)
 Plataforma: RISC-V, CH32V307VCT6
 Autor do Kernel: Almir Bispo
=================================================================================
 Descrição:
 Este exemplo demonstra como o usuário pode interromper uma tarefa longa (Task 11)
 utilizando interrupções físicas de hardware. Duas portas (Sensores) monitoram
 eventos críticos. Se disparadas, a ISR força PROCESS[id] = 0, abortando a tarefa.
=================================================================================
*/

#include "ch32v30x.h" // Biblioteca oficial do fabricante WCH para o CH32V307

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define MAX_PROCESSES 255
#define LONG_TASK_ID  11  // Vamos associar a tarefa longa ao ID 11

// Variável do Kernel - OBRIGATORIAMENTE 'volatile' para que o compilador
// não otimize os acessos e leia o valor real alterado pela interrupção.
volatile u8 PROCESS[MAX_PROCESSES] = {0};
u32 COUNTS[MAX_PROCESSES] = {0};

// Macro de Checkpoint: O usuário insere isso dentro de loops da tarefa longa.
// Se a interrupção limpou a flag do processo, a tarefa aborta na hora.
#define ELOIDA_CHECKPOINT(id) if (PROCESS[id] == 0) return;

// =================================================================================
// ESTRUTURA DE DADOS COMPARTILHADA (IPC COOPERATIVO)
// =================================================================================
typedef struct {
    u32 sensor_port_A;    // Atualizado pela Interrupção do Sensor 1
    u32 sensor_port_B;    // Atualizado pela Interrupção do Sensor 2
    u32 system_status;    // Flags de controle do sistema
} SharedData;

volatile SharedData SHARED = {0};

// =================================================================================
// INTERRUPÇÕES DE HARDWARE (PREEMPÇÃO REAL)
// =================================================================================

/* ISR do Sensor 1 (Ex: Pino PA0 configurado como EXTI_Line0) */
void EXTI0_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI0_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line0) != RESET) {
        
        // A MÁGICA DA PREEMPÇÃO NO ELOIDA OS:
        // Força o fechamento da tarefa longa atual limpando sua flag de execução
        // Chamada direta da função do seu Kernel para abortar a tarefa
        destroy(LONG_TASK_ID); 
        // Trata o evento crítico imediatamente no espaço da interrupção
        SHARED.sensor_port_A++;
        SHARED.system_status |= 0x01; // Sinaliza que o sensor A interceptou o fluxo
        
        EXTI_ClearITPendingBit(EXTI_Line0); // Limpa o registrador de interrupção do CH32
    }
}

/* ISR do Sensor 2 (Ex: Pino PA1 configurado como EXTI_Line1) */
void EXTI1_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI1_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line1) != RESET) {
        
        // Interrompe a tarefa longa da mesma forma
        // Chamada direta da função do seu Kernel para abortar a tarefa
        destroy(LONG_TASK_ID);  
        // Trata o evento crítico do segundo sensor
        SHARED.sensor_port_B++;
        SHARED.system_status |= 0x02; // Sinaliza que o sensor B interceptou o fluxo
        
        EXTI_ClearITPendingBit(EXTI_Line1);
    }
}

// =================================================================================
// TAREFAS DO USUÁRIO
// =================================================================================

void task0(void) { /* Lógica leve */ }
void task1(void) { /* Lógica leve */ }

/* 
   Tarefa 11: Simula um processo longo (Ex: cálculo pesado ou varredura de display)
   O programador adiciona pontos de checagem para torná-la "abortável".
*/
void task11(void) {
    u32 i, j;
    
    for (i = 0; i < 5000; i++) {
        
        // Ponto de checagem: Se alguma porta disparou a interrupção, sai IMEDIATAMENTE
        ELOIDA_CHECKPOINT(LONG_TASK_ID);
        
        // Simulação de processamento pesado interno
        for (j = 0; j < 100; j++) {
            __asm__("nop"); // Gasta ciclos de clock propositalmente
        }
        
        // Outro ponto de checagem estratégico no meio da lógica
        ELOIDA_CHECKPOINT(LONG_TASK_ID);
    }
}

// Tabela de despacho estática
typedef void (*TaskFunc)(void);
const TaskFunc TASK_TABLE[12] = {
    task0, task1, 0, 0, 0, 0, 0, 0, 0, 0, 0, task11
};

// =================================================================================
// NÚCLEO DO KERNEL (Funções originais adaptadas para auto-destruição interna)
// =================================================================================
void process(u32 Limit_count, u8 pointer) {
    if (pointer < MAX_PROCESSES) {
        COUNTS[pointer]++;
        if (COUNTS[pointer] >= Limit_count) {
            PROCESS[pointer] = 1;
            COUNTS[pointer] = 0;
        }
    }
}

void run(u8 any_process) {
    if (any_process < MAX_PROCESSES && PROCESS[any_process] == 1) {
        
        if (any_process < 12 && TASK_TABLE[any_process] != 0) {
            TASK_TABLE[any_process](); // Invoca a tarefa
        }
        
        // Auto-limpeza da flag: Se a tarefa terminou normalmente por completo,
        // zera a flag. Se foi abortada pela ISR, já estará em zero.
        PROCESS[any_process] = 0; 
    }
}
// =================================================================================
// Destroi a tarefa imediatamente 
// =================================================================================
void destroy(u8 any_process)
{
    if (any_process < MAX_PROCESSES) PROCESS[any_process] = 0;
}

// =================================================================================
// CONFIGURAÇÃO DOS PINOS (Hardware CH32V307)
// =================================================================================
void GPIO_Interrupt_Config(void) {
    // Código padrão do CH32V307 para ligar o clock do GPIOA e do AFIO (Interrupções)
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
    
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1; // Pinos PA0 e PA1
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;          // Entrada com Pull-Up interno
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // Conecta as linhas de interrupção externa (EXTI) aos pinos configurados
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource0);
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource1);
    
    EXTI_InitTypeDef EXTI_InitStructure = {0};
    EXTI_InitStructure.EXTI_Line = EXTI_Line0 | EXTI_Line1;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling; // Dispara na descida (0V)
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);
    
    // Habilita as interrupções no controlador do RISC-V (NVIC/PFIC)
    NVIC_EnableIRQ(EXTI0_IRQn);
    NVIC_EnableIRQ(EXTI1_IRQn);
}

// =================================================================================
// LOOP PRINCIPAL
// =================================================================================
int main(void) {
    // Inicializa o hardware de interrupção das portas antes do loop do OS
    GPIO_Interrupt_Config();
    
    while(1) {
        u8 i;
        for (i = 0; i < MAX_PROCESSES; i++) {
            // Definição fictícia de limites (substituir pelos primos na versão final)
            u32 limit = (i == LONG_TASK_ID) ? 500 : (1000 + i);
            
            process(limit, i);
            run(i); 
        }
    }
}
