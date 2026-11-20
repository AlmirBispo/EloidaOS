/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : Almir Bispo
* Version            : V7.0.0 - BASE PADRAO ELOIDA OS (rede robusta)
* Date               : 2026
* Description        : Base padrao EloidaOS.
*                      Rede: Ethernet + DHCP + DNS + HTTP cliente (Request_WEB +
*                      HTTP_Transceive) com backoff, sem auto-sabotagem do socket.
*                      SPI Flash (W25Qxx), UART4 (HLK-RM08K), RTC, kernel cooperativo.
*                      Provisionamento via HTTP (tags MAIUSCULAS).
*                      ESTE ARQUIVO E' A BASE. ADICIONE APENAS O NEGOCIO NAS TASKS.
*******************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include "string.h"
#include "eth_driver.h"
#include "stdlib.h"
#include "wchnet.h"
#include "debug.h"
#include "ch32v30x.h"
#include "ch32v30x_usart.h"
#include "ch32v30x_gpio.h"

/* ============================================================
 * DEFINES — PROVISIONAMENTO (Flash externa SPI W25Qxx)
 * ============================================================ */
#define FLASH_PROV_BASE            0x001000
#define FLASH_PROV_FLAG_ADDR       (FLASH_PROV_BASE + 0x0000)
#define FLAG_PROV_MAGIC            0xA5
#define FLASH_PROV_BIT0_ADDR       (FLASH_PROV_BASE + 0x0010)
#define FLASH_PROV_BIT1_ADDR       (FLASH_PROV_BASE + 0x0020)
#define FLASH_PROV_BIT2_ADDR       (FLASH_PROV_BASE + 0x0030)
#define FLASH_PROV_BIT3_ADDR       (FLASH_PROV_BASE + 0x0040)
#define FLASH_PROV_BIT4_ADDR       (FLASH_PROV_BASE + 0x0050)
#define FLASH_PROV_SETOR           (FLASH_PROV_BASE / 4096)

/* ============================================================
 * DEFINES — KERNEL ELOIDA OS
 * ============================================================ */
#define MAX_PROCESSES 255
#define MAX_TASKS     16

/* ============================================================
 * DEFINES — REDE
 * ============================================================ */
#define HTTP_PORT             4550
#define HTTP_RECV_BUF_SIZE    2048
#define MAX_HTTP_REQUEST_SIZE 800
#define DHCP_HOSTNAME         "ELOIDA_DEVICE"
#define MAX_HOSTNAME_LEN      64
#define SOCKET_STATUS_CLOSED      0
#define SOCKET_STATUS_ESTABLISHED 5
#define HTTP_ENDPOINT         "/cgi-bin/executa.exe/default"
u8 CONNECT_MODE = 0; // 0 = rj45 | 1 = wifi
/* Backoff de reconexao (equivalente ao master) */
#define RECONNECT_TIMEOUT     5000   /* ms */
#define MAX_RETRY_ATTEMPTS    5

/* ============================================================
 * DEFINES — UART4 (HLK-RM08K)
 * ============================================================ */
#define DEFAULT_TIMEOUT_CYCLES   200000

/* ============================================================
 * TIPOS
 * ============================================================ */
typedef struct {
    char bit0[2];
    char bit1[2];
    char bit2[2];
    char bit3[2];
    char bit4[2];
} ProvisaoCtx;

typedef struct {
    u8 ip[4];
    u8 resolved;
    u8 in_progress;
    u8 retry_count;
} DnsContext;

typedef struct {
    vu8  hour;
    vu8  min;
    vu8  sec;
    vu16 w_year;
    vu8  w_month;
    vu8  w_date;
    vu8  week;
} _calendar_obj;

typedef void (*TaskFunc)(void);

/* ============================================================
 * VARIAVEIS GLOBAIS — PROVISIONAMENTO
 * ============================================================ */
ProvisaoCtx CTX = {0};
volatile u8 FLAG_PROVISIONADO = 0;
volatile u8 PROV_POSSIBILITY = 0;

/* ============================================================
 * VARIAVEIS GLOBAIS — KERNEL
 * ============================================================ */
u8  PROCESS[MAX_PROCESSES] = {0};
u32 COUNTS[MAX_PROCESSES]  = {0};


u32 PRIME_NUMS[MAX_TASKS] = {
    2, 3, 7, 19, 41, 79, 199, 401,
    2003, 4001, 8009, 20011, 40009, 80021, 200003, 400009
};
/* ============================================================
 * VARIAVEIS GLOBAIS — REDE
 * ============================================================ */
u8 MACAddr[6];
u8 IPAddr[4]    = {0};
u8 GWIPAddr[4]  = {0};
u8 IPMask[4]    = {0};
u8 DnsServer[4] = {0};
u8 SocketId     = 0;

u8 SocketRecvBuf[WCHNET_MAX_SOCKET_NUM][HTTP_RECV_BUF_SIZE];
u8 HttpRecvBuffer[HTTP_RECV_BUF_SIZE];

char Hostname[MAX_HOSTNAME_LEN] = "192.168.101.22";
volatile char SERVER_RESPONSE[2048] = "";
char TOKEN_CLIENT[50] = "c17d76b5-ce93-74aa-0d2032185c29";

DnsContext DnsCtx = {0};
char REQ_NOSQL[2048] = "";
u32 CONTADOR_RESET = 0;

/* Backoff */
static u32 last_reconnect_attempt = 0;
static u8  reconnect_retries      = 0;

/* ============================================================
 * VARIAVEIS GLOBAIS — FLASH
 * ============================================================ */
u8 SPI_FLASH_BUF[4096];

/* ============================================================
 * VARIAVEIS GLOBAIS — UART4 (HLK-RM08K)
 * ============================================================ */
char WIFI_SERVER_RESPONSE[2048] = "";
char uart4_rx_buffer[HTTP_RECV_BUF_SIZE];
u32  uart4_rx_index = 0;

/* ============================================================
 * VARIAVEIS GLOBAIS — RTC / CALENDARIO
 * ============================================================ */
_calendar_obj calendar;
u8 const table_week[12] = {0,3,3,6,1,4,6,2,5,0,3,5};
const u8 mon_table[12]  = {31,28,31,30,31,30,31,31,30,31,30,31};

/* ============================================================
 * PROTOTIPOS
 * ============================================================ */

/* --- UART4 (HLK-RM08K) --- */
void  uart_wifi_data_init(void);
void  UART4_NVIC_Config(void);
void  UART4_IRQHandler(void);
void  print_wifi_data(char *Text);
void  fragmentar(char *input_buffer, u32 tam_buffer);
int   UART4_Receive_Polling_NoTick(char *buffer_destino, int max_length);
char *Request_Wifi(char *DataRequest);
u8    get_hlk_rssi(char *rssi_buffer);

/* --- RTC / Calendario --- */
void  RTC_NVIC_Config(void);
void  RTC_IRQHandler(void);
u8    RTC_Init(void);
u8    Is_Leap_Year(u16 year);
u8    RTC_Set(u16 syear, u8 smon, u8 sday, u8 hour, u8 min, u8 sec);
u8    RTC_Alarm_Set(u16 syear, u8 smon, u8 sday, u8 hour, u8 min, u8 sec);
u8    RTC_Get(void);
u8    RTC_Get_Week(u16 year, u8 month, u8 day);
char *format_iso8601_utc(const _calendar_obj *cal, char *buffer);
void  timestamp_to_iso8601(uint32_t timestamp, char *buffer);

/* --- Rede / Ethernet --- */
void  TIM2_Init(void);
void  mStopIfError(u8 iError);
void  dns_resolve_callback(uint8_t *ip, void *arg);
u8    WCHNET_StartDNSResolution(const char *hostname);
u8    WCHNET_CreateTcpSocket(void);
void  WCHNET_HandleSockInt(u8 socketid, u8 intstat);
void  WCHNET_HandleGlobalInt(void);
u8    WCHNET_DHCPCallBack(u8 status, void *arg);
void  set_hostname(const char *name);
u8    is_socket_connected(u8 socket_id);

/* --- HTTP (robusto) --- */
u8    Request_WEB(char *packet);
u8    HTTP_Transceive(char *packet);
void  Request_Server(char *anydata);      /* HTTP: header+body, robusto */
void  GET_LIST_ITEM(char *container, int max_len, const char *source,
                    const char *start_delim, const char *end_delim,
                    const char *separator, int index);
void  GET_VAR_AUTO(char *container, int max_len, const char *source,
                   const char *start_delim, const char *end_delim);

/* --- SPI Flash --- */
u8    SPI1_ReadWriteByte(u8 TxData);
void  SPI_Flash_Init(void);
u8    SPI_Flash_ReadSR(void);
void  SPI_Flash_Wait_Busy(void);
void  SPI_FLASH_Write_Enable(void);
void  SPI_Flash_Erase_Sector(u32 Dst_Addr);
void  SPI_Flash_Read(u8 *pBuffer, u32 ReadAddr, u16 size);
void  SPI_Flash_Write_Page(u8 *pBuffer, u32 WriteAddr, u16 size);
void  SPI_Flash_Write(u8 *pBuffer, u32 WriteAddr, u16 size);
void  SPI_Flash_Write_NoCheck(u8 *pBuffer, u32 WriteAddr, u16 size);

/* --- Base: ~13 us/ciclo @ 144 MHz) --- */
void  task0(void);      /* ~26 us    (~38,5 kHz)  | Divisor: 2      */
void  task1(void);      /* ~39 us    (~25,6 kHz)  | Divisor: 3      */
void  task2(void);      /* ~91 us    (~11,0 kHz)  | Divisor: 7      */
void  task3(void);      /* ~247 us   (~4,05 kHz)  | Divisor: 19     */
void  task4(void);      /* ~533 us   (~1,88 kHz)  | Divisor: 41     */
void  task5(void);      /* ~1,03 ms  (~973 Hz)    | Divisor: 79     */
void  task6(void);      /* ~2,59 ms  (~386 Hz)    | Divisor: 199    */
void  task7(void);      /* ~5,21 ms  (~192 Hz)    | Divisor: 401    */
void  task8(void);      /* ~26,0 ms  (~38,4 Hz)   | Divisor: 2003   */
void  task9(void);      /* ~52,0 ms  (~19,2 Hz)   | Divisor: 4001   */
void  task10(void);     /* ~104,1 ms (~9,6 Hz)    | Divisor: 8009   */
void  task11(void);     /* ~260,1 ms (~3,84 Hz)   | Divisor: 20011  */
void  task_rede(void);  /* ~520,1 ms (~1,92 Hz)   | Divisor: 40009  */
void  task12(void);     /* ~1,04 s   (~0,96 Hz)   | Divisor: 80021  */
void  task13(void);     /* ~2,60 s   (~0,38 Hz)   | Divisor: 200003 */
void  task14(void);     /* ~5,20 s   (~0,19 Hz)   | Divisor: 400009 (MAX_TASKS = 16) */
/* --- Kernel --- */
void  process_create(u8 pointer);
void  process(u32 Limit_count, u8 pointer);
void  run(u8 any_process);
void  destroy(u8 any_process);

/* --- Provisionamento --- */
void  gravar_provisao(void);
void  carregar_provisao(void);
void  reset_provisao(void);

/* ============================================================
 * UART4 — HLK-RM08K
 * ============================================================ */
void uart_wifi_data_init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_UART4, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = 115200;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(UART4, &USART_InitStructure);

    USART_Cmd(UART4, ENABLE);
    USART_ITConfig(UART4, USART_IT_RXNE, DISABLE);
}

void UART4_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    NVIC_InitStructure.NVIC_IRQChannel                   = UART4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void UART4_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void UART4_IRQHandler(void)
{
    if (USART_GetITStatus(UART4, USART_IT_RXNE) != RESET) {
        (void)USART_ReceiveData(UART4);
        USART_ClearITPendingBit(UART4, USART_IT_RXNE);
    }
}

void print_wifi_data(char *Text)
{
    for (u8 i = 0; i < strlen(Text); i++) {
        USART_SendData(UART4, Text[i]);
        Delay_Us(100);
    }
}


void fragmentar(char *input_buffer, u32 tam_buffer)
{
    u32 DIM = 250, step = tam_buffer / DIM, resto = tam_buffer % DIM;
    char byte_envio[2]; byte_envio[1] = '\0';

    for (u32 i = 0; i < step; i++) 
    {
        for (u32 j = 0; j < DIM; j++) 
        {
            byte_envio[0] = input_buffer[(i * DIM) + j];
            print_wifi_data(byte_envio);
        }
        Delay_Ms(5);
    }
    if (resto > 0) 
    {
        for (u32 j = 0; j < resto; j++) 
        {
            byte_envio[0] = input_buffer[(step * DIM) + j];
            print_wifi_data(byte_envio);
        }
        Delay_Ms(5);
    }
}
int UART4_Receive_Polling_NoTick(char *buffer_destino, int max_length)
{
    u32 current_timeout = DEFAULT_TIMEOUT_CYCLES;
    int bytes_received  = 0;

    while (bytes_received < max_length) {
        if (USART_GetFlagStatus(UART4, USART_FLAG_RXNE) != RESET) {
            buffer_destino[bytes_received++] = USART_ReceiveData(UART4);
            current_timeout = DEFAULT_TIMEOUT_CYCLES;
        }
        current_timeout--;
        if (current_timeout == 0) break;
    }

    if (bytes_received < max_length) buffer_destino[bytes_received] = '\0';
    else                             buffer_destino[max_length - 1]  = '\0';

    return bytes_received;
}

char *Request_Wifi(char *DataRequest)
{
    u32 tamanho_real = strlen(DataRequest);
    printf("[WIFI] Enviando %lu bytes via HLK-RM08K\r\n", tamanho_real);
    fragmentar(DataRequest, tamanho_real);
    Delay_Ms(30); 
    memset(SERVER_RESPONSE, 0, sizeof(SERVER_RESPONSE));
    UART4_Receive_Polling_NoTick((char *)SERVER_RESPONSE, sizeof(SERVER_RESPONSE));

    return (char *)SERVER_RESPONSE;
}

u8 get_hlk_rssi(char *rssi_buffer)
{
    char at_cmd[32]   = "";
    char response[256] = "";
    u32 timeout;
    u8 rssi_found = 0;
    s16 rssi_value = 0;

    strcpy(rssi_buffer, "0");

    strcpy(at_cmd, "+++"); print_wifi_data(at_cmd); Delay_Ms(1000);

    memset(response, 0, sizeof(response));
    timeout = DEFAULT_TIMEOUT_CYCLES / 10;
    while (timeout > 0) {
        if (USART_GetFlagStatus(UART4, USART_FLAG_RXNE) != RESET) {
            u8 byte = USART_ReceiveData(UART4);
            u8 len  = strlen(response);
            if (len < sizeof(response) - 1) {
                response[len]     = byte;
                response[len + 1] = '\0';
            }
        }
        if (strstr(response, "OK") != NULL) break;
        timeout--;
    }

    if (timeout == 0) {
        strcpy(at_cmd, "ATO\r\n"); print_wifi_data(at_cmd); Delay_Ms(100);
        return 0;
    }

    Delay_Ms(100);
    strcpy(at_cmd, "AT+RSSI?\r\n"); print_wifi_data(at_cmd);

    memset(response, 0, sizeof(response));
    timeout = DEFAULT_TIMEOUT_CYCLES / 10;
    while (timeout > 0) {
        if (USART_GetFlagStatus(UART4, USART_FLAG_RXNE) != RESET) {
            u8 byte = USART_ReceiveData(UART4);
            u8 len  = strlen(response);
            if (len < sizeof(response) - 1) {
                response[len]     = byte;
                response[len + 1] = '\0';
            }
            if (strstr(response, "\r\n") != NULL) break;
        }
        timeout--;
    }

    char *rssi_ptr = strstr(response, "+RSSI:");
    if (rssi_ptr != NULL) {
        rssi_ptr += 6;
        s16 temp_rssi = 0; u8 negative = 0;
        if (*rssi_ptr == '-') { negative = 1; rssi_ptr++; }
        while ((*rssi_ptr >= '0') && (*rssi_ptr <= '9')) {
            temp_rssi = (temp_rssi * 10) + (*rssi_ptr - '0');
            rssi_ptr++;
        }
        if (negative) temp_rssi = -temp_rssi;
        rssi_value = temp_rssi;
        rssi_found = 1;
        sprintf(rssi_buffer, "%d", rssi_value);
    }

    Delay_Ms(100);
    strcpy(at_cmd, "ATO\r\n"); print_wifi_data(at_cmd); Delay_Ms(100);

    if (!rssi_found) strcpy(rssi_buffer, "0");
    return rssi_found;
}

/* ============================================================
 * RTC / CALENDARIO
 * ============================================================ */
void RTC_NVIC_Config(void)
{
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    NVIC_InitStructure.NVIC_IRQChannel                   = RTC_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void RTC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void RTC_IRQHandler(void)
{
    if (RTC_GetITStatus(RTC_IT_SEC) != RESET)
    {
        RTC_Get();
        RTC_ClearITPendingBit(RTC_IT_SEC);
    }
}

u8 RTC_Init(void)
{
    u8 temp = 0;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RCC_LSEConfig(RCC_LSE_ON);
    while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) == RESET && temp < 250) { temp++; Delay_Ms(20); }
    if (temp >= 250) return 1;
    RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
    RCC_RTCCLKCmd(ENABLE);
    RTC_WaitForLastTask();
    RTC_WaitForSynchro();
    RTC_ITConfig(RTC_IT_SEC, ENABLE);
    RTC_WaitForLastTask();
    RTC_EnterConfigMode();
    RTC_SetPrescaler(32767);
    RTC_WaitForLastTask();
    if (BKP_ReadBackupRegister(BKP_DR1) != 0XA1A1) {
        RTC_Set(2026, 7, 14, 10, 0, 0);
        BKP_WriteBackupRegister(BKP_DR1, 0XA1A1);
    }
    RTC_ExitConfigMode();
    RTC_NVIC_Config();
    RTC_Get();
    return 0;
}

u8 Is_Leap_Year(u16 year)
{
    if (year % 4 == 0) {
        if (year % 100 == 0) {
            if (year % 400 == 0) return 1; else return 0;
        } else return 1;
    } else return 0;
}

u8 RTC_Set(u16 syear, u8 smon, u8 sday, u8 hour, u8 min, u8 sec)
{
    u16 t; u32 seccount = 0;
    if (syear < 1970 || syear > 2099) return 1;
    for (t = 1970; t < syear; t++)
        seccount += Is_Leap_Year(t) ? 31622400 : 31536000;
    smon -= 1;
    for (t = 0; t < smon; t++) {
        seccount += (u32)mon_table[t] * 86400;
        if (Is_Leap_Year(syear) && t == 1) seccount += 86400;
    }
    seccount += (u32)(sday - 1)*86400 + (u32)hour*3600 + (u32)min*60 + sec;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetCounter(seccount);
    RTC_WaitForLastTask();
    return 0;
}

u8 RTC_Alarm_Set(u16 syear, u8 smon, u8 sday, u8 hour, u8 min, u8 sec)
{
    u16 t; u32 seccount = 0;
    if (syear < 1970 || syear > 2099) return 1;
    for (t = 1970; t < syear; t++)
        seccount += Is_Leap_Year(t) ? 31622400 : 31536000;
    smon -= 1;
    for (t = 0; t < smon; t++) {
        seccount += (u32)mon_table[t] * 86400;
        if (Is_Leap_Year(syear) && t == 1) seccount += 86400;
    }
    seccount += (u32)(sday - 1)*86400 + (u32)hour*3600 + (u32)min*60 + sec;
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    RTC_SetAlarm(seccount);
    RTC_WaitForLastTask();
    return 0;
}

u8 RTC_Get(void)
{
    static u16 daycnt = 0;
    u32 timecount = 0, temp = 0; u16 temp1 = 0;
    timecount = RTC_GetCounter();
    temp = timecount / 86400;
    if (daycnt != temp) {
        daycnt = temp; temp1 = 1970;
        while (temp >= 365) {
            if (Is_Leap_Year(temp1)) {
                if (temp >= 366) temp -= 366;
                else { temp1++; break; }
            } else temp -= 365;
            temp1++;
        }
        calendar.w_year = temp1; temp1 = 0;
        while (temp >= 28) {
            if (Is_Leap_Year(calendar.w_year) && temp1 == 1) {
                if (temp >= 29) temp -= 29; else break;
            } else {
                if (temp >= mon_table[temp1]) temp -= mon_table[temp1]; else break;
            }
            temp1++;
        }
        calendar.w_month = temp1 + 1;
        calendar.w_date = temp + 1;
    }
    temp = timecount % 86400;
    calendar.hour = temp / 3600;
    calendar.min = (temp % 3600) / 60;
    calendar.sec = (temp % 3600) % 60;
    calendar.week = RTC_Get_Week(calendar.w_year, calendar.w_month, calendar.w_date);
    return 0;
}

u8 RTC_Get_Week(u16 year, u8 month, u8 day)
{
    u16 temp2; u8 yearH, yearL;
    yearH = year / 100; yearL = year % 100;
    if (yearH > 19) yearL += 100;
    temp2 = yearL + yearL / 4;
    temp2 = temp2 % 7;
    temp2 = temp2 + day + table_week[month - 1];
    if (yearL % 4 == 0 && month < 3) temp2--;
    return (temp2 % 7);
}

char *format_iso8601_utc(const _calendar_obj *cal, char *buffer)
{
    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
            (int)cal->w_year, (int)cal->w_month, (int)cal->w_date,
            (int)cal->hour, (int)cal->min, (int)cal->sec);
    return buffer;
}

void timestamp_to_iso8601(uint32_t timestamp, char *buffer)
{
    uint32_t days, seconds;
    uint16_t year = 1970;
    uint8_t month, day, hour, min, sec;
    uint8_t month_days;

    days = timestamp / 86400;
    seconds = timestamp % 86400;

    while (days >= 365) {
        uint16_t days_in_year = (year % 4 == 0) ? 366 : 365;
        if (year % 100 == 0 && year % 400 != 0) days_in_year = 365;
        if (days >= days_in_year) { days -= days_in_year; year++; }
        else break;
    }

    for (month = 1; month <= 12; month++) {
        if (month == 2) {
            month_days = (year % 4 == 0) ? 29 : 28;
            if (year % 100 == 0 && year % 400 != 0) month_days = 28;
        } else {
            month_days = mon_table[month - 1];
        }
        if (days < month_days) { day = days + 1; break; }
        else days -= month_days;
    }

    hour = seconds / 3600;
    min  = (seconds % 3600) / 60;
    sec  = seconds % 60;

    sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02d.000Z",
            year, month, day, hour, min, sec);
}

/* ============================================================
 * REDE
 * ============================================================ */
void TIM2_Init(void) {
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseStructure.TIM_Period        = SystemCoreClock / 1000000 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler     = WCHNETTIMERPERIOD * 1000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    NVIC_SetPriority(TIM2_IRQn, 0x80);
    NVIC_EnableIRQ(TIM2_IRQn);
}

void mStopIfError(u8 iError) {
    if (iError != WCHNET_ERR_SUCCESS) printf("Error: 0x%02X\r\n", iError);
}

void dns_resolve_callback(uint8_t *ip, void *arg) {
    DnsContext *ctx = (DnsContext *)arg;
    if (ip) { memcpy(ctx->ip, ip, 4); ctx->resolved = 1; }
    else    { ctx->resolved = 0; }
    ctx->in_progress = 0;
    ctx->retry_count = 0;
}

u8 WCHNET_StartDNSResolution(const char *hostname)
{
    u8 ret;
    u16 phyStatus;

    phyStatus = WCHNET_GetPHYStatus();
    if (!(phyStatus & PHY_Linked_Status)) return 0;
    if (DnsServer[0] == 0) return 0;
    if (DnsCtx.in_progress) return 0;

    WCHNET_InitDNS(DnsServer, 53);

    DnsCtx.resolved    = 0;
    DnsCtx.in_progress = 1;
    DnsCtx.retry_count++;
    memset(DnsCtx.ip, 0, 4);

    ret = WCHNET_HostNameGetIp(hostname, DnsCtx.ip, dns_resolve_callback, &DnsCtx);
    if (ret != WCHNET_ERR_SUCCESS) {
        if (strcmp(hostname, "192.168.101.22") == 0) {
            DnsCtx.ip[0] = 192; DnsCtx.ip[1] = 168;
            DnsCtx.ip[2] = 101; DnsCtx.ip[3] = 22;
            DnsCtx.resolved    = 1;
            DnsCtx.in_progress = 0;
            return 1;
        }
        DnsCtx.in_progress = 0;
        return 0;
    }
    return 1;
}

u8 WCHNET_CreateTcpSocket(void) {
    SOCK_INF TmpSocketInf;
    u8 i;

    if (!DnsCtx.resolved && !DnsCtx.in_progress) {
        if (!WCHNET_StartDNSResolution(Hostname)) return 0;
        return 1;
    }
    if (DnsCtx.in_progress) return 1;

    if (SocketId < WCHNET_MAX_SOCKET_NUM) {
        WCHNET_SocketClose(SocketId, TCP_CLOSE_NORMAL);
        Delay_Ms(100);
    }

    memset(&TmpSocketInf, 0, sizeof(SOCK_INF));
    memcpy(TmpSocketInf.IPAddr, DnsCtx.ip, 4);
    TmpSocketInf.DesPort    = HTTP_PORT;
    TmpSocketInf.SourPort   = 5000 + (rand() % 1000);
    TmpSocketInf.ProtoType  = PROTO_TYPE_TCP;
    TmpSocketInf.RecvBufLen = HTTP_RECV_BUF_SIZE;

    i = WCHNET_SocketCreat(&SocketId, &TmpSocketInf);
    if (i != WCHNET_ERR_SUCCESS) { printf("Socket creation failed: %02X\r\n", i); return 0; }

    i = WCHNET_SocketConnect(SocketId);
    if (i != WCHNET_ERR_SUCCESS) { printf("Connect failed: %02X\r\n", i); return 0; }

    printf("TCP connecting to %d.%d.%d.%d:%d (Socket %d)\r\n",
           DnsCtx.ip[0], DnsCtx.ip[1], DnsCtx.ip[2], DnsCtx.ip[3],
           HTTP_PORT, SocketId);
    return 1;
}

void WCHNET_HandleSockInt(u8 socketid, u8 intstat)
 {
 
    if (intstat & SINT_STAT_RECV) 
    {
        u32 len = WCHNET_SocketRecvLen(socketid, NULL);
        if (len > 0) 
        {
            // Buffer temporário para o pedaço que acabou de chegar
            static u8 chunk_buf[512]; 
            u32 copy = (len > sizeof(chunk_buf) - 1) ? sizeof(chunk_buf) - 1 : len;
            
            WCHNET_SocketRecv(socketid, chunk_buf, &copy);
            chunk_buf[copy] = '\0';

            // Concatena no buffer principal (SERVER_RESPONSE)
            // Verifica se há espaço para não estourar
            if (strlen((char*)SERVER_RESPONSE) + strlen((char*)chunk_buf) < sizeof(SERVER_RESPONSE) - 1) 
            {
                strcat((char*)SERVER_RESPONSE, (char*)chunk_buf);
            }
        }
    }
//
    if (intstat & SINT_STAT_CONNECT) 
    {
        printf("<<<<<<< TCP Connected >>>>>>>\r\n");
        WCHNET_ModifyRecvBuf(socketid, (u32)SocketRecvBuf[socketid], HTTP_RECV_BUF_SIZE);
    }
    if (intstat & SINT_STAT_DISCONNECT) printf("<<<<<<< TCP Disconnected >>>>>>>\r\n");
    if (intstat & SINT_STAT_TIM_OUT)    printf("<<<<<<< TCP Timeout >>>>>>>\r\n");
}

void WCHNET_HandleGlobalInt(void)
{
    u8 intstat = WCHNET_GetGlobalInt();

    if (intstat & GINT_STAT_UNREACH)   printf("Network Unreachable\r\n");
    if (intstat & GINT_STAT_IP_CONFLI) printf("IP Conflict\r\n");
    if (intstat & GINT_STAT_PHY_CHANGE) {
        u16 phyStatus = WCHNET_GetPHYStatus();
        if (phyStatus & PHY_Linked_Status) 
        {
            printf("PHY Link Up\r\n");
            CONNECT_MODE = 0; // 0 = rj45 | 1 = wifi
        }
        else                                
        {
            printf("PHY Link Down\r\n");
            CONNECT_MODE = 1; // 0 = rj45 | 1 = wifi
        }
    }
    if (intstat & GINT_STAT_SOCKET) {
        for (u8 i = 0; i < WCHNET_MAX_SOCKET_NUM; i++) {
            u8 socketint = WCHNET_GetSocketInt(i);
            if (socketint) WCHNET_HandleSockInt(i, socketint);
        }
    }
}

/* Callback DHCP: NAO cria socket. Apenas atualiza IP/DNS. A task_rede decide. */
u8 WCHNET_DHCPCallBack(u8 status, void *arg) {
    u8 *p = arg;
    u8 zero_ip[4] = {0};

    if (!status) {
        printf("DHCP Success\r\n");
        if (memcmp(IPAddr, p, 4)) {
            if (memcmp(IPAddr, zero_ip, 4))
                WCHNET_SocketClose(SocketId, TCP_CLOSE_NORMAL);
            memcpy(IPAddr,    p,      4);
            memcpy(GWIPAddr,  &p[4],  4);
            memcpy(IPMask,    &p[8],  4);
            memcpy(DnsServer, &p[12], 4);
            printf("IP:  %d.%d.%d.%d\r\n", IPAddr[0], IPAddr[1], IPAddr[2], IPAddr[3]);
            printf("GW:  %d.%d.%d.%d\r\n", GWIPAddr[0], GWIPAddr[1], GWIPAddr[2], GWIPAddr[3]);
            printf("DNS: %d.%d.%d.%d\r\n", DnsServer[0], DnsServer[1], DnsServer[2], DnsServer[3]);
        }
        return 1;
    }
    printf("DHCP Failed: %02X\r\n", status);
    return 0;
}

void set_hostname(const char *name) {
    strncpy(Hostname, name, MAX_HOSTNAME_LEN - 1);
    Hostname[MAX_HOSTNAME_LEN - 1] = '\0';
    DnsCtx.resolved    = 0;
    DnsCtx.in_progress = 0;
    memset(DnsCtx.ip, 0, 4);
}

u8 is_socket_connected(u8 socket_id)
{
    if (socket_id >= WCHNET_MAX_SOCKET_NUM) return 0;
    return (SocketInf[socket_id].SockStatus == SOCKET_STATUS_ESTABLISHED);
}

/* ============================================================
 * HTTP ROBUSTO — Request_WEB + HTTP_Transceive
 * ============================================================ */

/* Envio com backpressure (QueryUnack) e stall. NAO bloqueia o stack. */
u8 Request_WEB(char *packet)
{
    u32 len = strlen(packet), sent = 0, stall = 0;

    if (!is_socket_connected(SocketId)) {
        printf("Socket offline\r\n");
        return 0;
    }

    while (sent < len) {
        u8 unack = WCHNET_QueryUnack(SocketId, NULL, 0);
        if (unack >= (WCHNET_NUM_TCP_SEG - 2)) {
            WCHNET_MainTask();
            if (WCHNET_QueryGlobalInt()) WCHNET_HandleGlobalInt();
            Delay_Ms(5);
            if (++stall > 600) { printf("[TCP] Stall, abortando\r\n"); return 0; }
            continue;
        }
        stall = 0;

        u32 chunk = len - sent;
        u8 ret = WCHNET_SocketSend(SocketId, (u8 *)packet + sent, &chunk);
        if (ret != WCHNET_ERR_SUCCESS) {
            printf("[TCP] Send error: %02X\r\n", ret);
            return 0;
        }
        sent += chunk;
        Delay_Ms(1);
    }
    return 1;
}

/* Transceive: envia e espera resposta (CABO = TCP; WIFI = UART4). */
u8 HTTP_Transceive(char *packet)
{
    memset(SERVER_RESPONSE, 0, sizeof(SERVER_RESPONSE));

    if (CONNECT_MODE == 0) {
        if (!Request_WEB(packet)) { printf("[HTTP] Falha no envio (CABO)\r\n"); return 0; }

        u32 timeout_ms = 3000;
        while (timeout_ms > 0 && SERVER_RESPONSE[0] == '\0') {
            WCHNET_MainTask();
            if (WCHNET_QueryGlobalInt()) WCHNET_HandleGlobalInt();
            Delay_Ms(1);
            timeout_ms--;
        }
        if (SERVER_RESPONSE[0] == '\0') {
            printf("[HTTP] Timeout sem resposta (CABO)\r\n");
            return 0;
        }
    } else {
        u32 tamanho_real = strlen(packet);
        printf("[WIFI] Enviando %lu bytes via HLK-RM08K\r\n", tamanho_real);
        fragmentar(packet, tamanho_real);
        UART4_Receive_Polling_NoTick((char *)SERVER_RESPONSE, sizeof(SERVER_RESPONSE));
        if (SERVER_RESPONSE[0] == '\0') {
            printf("[HTTP] Timeout sem resposta (WIFI)\r\n");
            return 0;
        }
    }
    printf("[HTTP] Resposta (%d bytes)\r\n", (int)strlen((char *)SERVER_RESPONSE));
    return 1;
}


#define HTTP_ENDPOINT  "/your_end_point"

void Request_Server(char *anydata)
{
    if (!is_socket_connected(SocketId)) 
    {
        printf("[HTTP] Socket not connected\r\n");
        return;
    }

    int body_len = strlen(anydata);

    int hdr_len = snprintf(REQ_NOSQL, sizeof(REQ_NOSQL),
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: WCHNET/1.0\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"  //querystring mode
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        HTTP_ENDPOINT,
        Hostname,
        body_len);

    if ((hdr_len + body_len) >= (int)sizeof(REQ_NOSQL)) {
        printf("[HTTP] Payload too big (%d bytes)\r\n", hdr_len + body_len);
        return;
    }

    memcpy(REQ_NOSQL + hdr_len, anydata, body_len);
    REQ_NOSQL[hdr_len + body_len] = '\0';

    u32 total = hdr_len + body_len;
    u32 sent  = 0;

    while (sent < total) {
        u32 chunk = total - sent;
        u8 ret = WCHNET_SocketSend(SocketId, (u8*)REQ_NOSQL + sent, &chunk);

        if (ret != WCHNET_ERR_SUCCESS) {
            printf("[HTTP] Error on Sent: 0x%02X\r\n", ret);
            return;
        }

        sent += chunk;
        Delay_Ms(10);
    }

    printf("[HTTP] Sendding %d bytes to %s\r\n", total, HTTP_ENDPOINT);
}

/* ============================================================
 * STRINGS / PARSING
 * ============================================================ */
void GET_LIST_ITEM(char *container, int max_len, const char *source,
                   const char *start_delim, const char *end_delim,
                   const char *separator, int index) {
    if (!container || max_len <= 0 || !source || !start_delim || !separator) {
        if (container && max_len > 0) container[0] = '\0';
        return;
    }
    container[0] = '\0';

    const char *p_start = strstr(source, start_delim);
    if (!p_start) return;
    p_start += strlen(start_delim);

    const char *p_end;
    if (end_delim && end_delim[0] != '\0') {
        p_end = strstr(p_start, end_delim);
        if (!p_end) return;
    } else p_end = source + strlen(source);

    int current_index = 0;
    const char *token_start = p_start;
    const char *p = p_start;
    size_t sep_len = strlen(separator);

    while (p <= p_end) {
        int is_end_of_token = 0;
        if (p == p_end) is_end_of_token = 1;
        else if (strncmp(p, separator, sep_len) == 0) is_end_of_token = 1;

        if (is_end_of_token) {
            if (current_index == index) {
                size_t token_len = p - token_start;
                if (token_len >= (size_t)max_len) token_len = max_len - 1;
                memcpy(container, token_start, token_len);
                container[token_len] = '\0';
                return;
            }
            current_index++;
            p += sep_len;
            token_start = p;
        } else p++;
    }
}

void GET_VAR_AUTO(char *container, int max_len, const char *source,
                  const char *start_delim, const char *end_delim) {
    if (container && max_len > 0) container[0] = '\0';
    if (!source || !start_delim || !container || max_len <= 0) return;

    const char *p_start = strstr(source, start_delim);
    if (!p_start) return;
    p_start += strlen(start_delim);

    const char *p_end;
    if (end_delim && end_delim[0] != '\0') {
        p_end = strstr(p_start, end_delim);
        if (!p_end) return;
    } else p_end = source + strlen(source);

    int copy_len = p_end - p_start;
    if (copy_len < 0) copy_len = 0;
    if (copy_len >= max_len) copy_len = max_len - 1;

    memcpy(container, p_start, copy_len);
    container[copy_len] = '\0';
}

/* ============================================================
 * SPI FLASH
 * ============================================================ */
u8 SPI1_ReadWriteByte(u8 TxData) {
    u8 i = 0;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET) { i++; if (i > 200) return 0; }
    SPI_I2S_SendData(SPI1, TxData);
    i = 0;
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET) { i++; if (i > 200) return 0; }
    return SPI_I2S_ReceiveData(SPI1);
}

void SPI_Flash_Init(void) {
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    SPI_InitTypeDef  SPI_InitStructure  = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_SPI1, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_2);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_6;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    SPI_InitStructure.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}

u8 SPI_Flash_ReadSR(void) {
    u8 byte = 0;
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 0);
    SPI1_ReadWriteByte(0x05);
    byte = SPI1_ReadWriteByte(0xFF);
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 1);
    return byte;
}

void SPI_Flash_Wait_Busy(void) { while ((SPI_Flash_ReadSR() & 0x01) == 0x01); }

void SPI_FLASH_Write_Enable(void) {
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 0);
    SPI1_ReadWriteByte(0x06);
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 1);
}

void SPI_Flash_Erase_Sector(u32 Dst_Addr) {
    Dst_Addr *= 4096;
    SPI_FLASH_Write_Enable();
    SPI_Flash_Wait_Busy();
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 0);
    SPI1_ReadWriteByte(0x20);
    SPI1_ReadWriteByte((u8)((Dst_Addr) >> 16));
    SPI1_ReadWriteByte((u8)((Dst_Addr) >> 8));
    SPI1_ReadWriteByte((u8)Dst_Addr);
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 1);
    SPI_Flash_Wait_Busy();
}

void SPI_Flash_Read(u8 *pBuffer, u32 ReadAddr, u16 size) {
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 0);
    SPI1_ReadWriteByte(0x03);
    SPI1_ReadWriteByte((u8)((ReadAddr) >> 16));
    SPI1_ReadWriteByte((u8)((ReadAddr) >> 8));
    SPI1_ReadWriteByte((u8)ReadAddr);
    for (u16 i = 0; i < size; i++) pBuffer[i] = SPI1_ReadWriteByte(0xFF);
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 1);
}

void SPI_Flash_Write_Page(u8 *pBuffer, u32 WriteAddr, u16 size) {
    SPI_FLASH_Write_Enable();
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 0);
    SPI1_ReadWriteByte(0x02);
    SPI1_ReadWriteByte((u8)((WriteAddr) >> 16));
    SPI1_ReadWriteByte((u8)((WriteAddr) >> 8));
    SPI1_ReadWriteByte((u8)WriteAddr);
    for (u16 i = 0; i < size; i++) SPI1_ReadWriteByte(pBuffer[i]);
    GPIO_WriteBit(GPIOA, GPIO_Pin_2, 1);
    SPI_Flash_Wait_Busy();
}

void SPI_Flash_Write(u8 *pBuffer, u32 WriteAddr, u16 size) {
    u32 secpos = WriteAddr / 4096;
    u16 secoff = WriteAddr % 4096;
    u16 secremain = 4096 - secoff;
    if (size <= secremain) secremain = size;

    while (1) {
        SPI_Flash_Read(SPI_FLASH_BUF, secpos * 4096, 4096);
        u16 i;
        for (i = 0; i < secremain; i++) {
            if (SPI_FLASH_BUF[secoff + i] != 0xFF) break;
        }
        if (i < secremain) {
            SPI_Flash_Erase_Sector(secpos);
            for (i = 0; i < secremain; i++) SPI_FLASH_BUF[i + secoff] = pBuffer[i];
            SPI_Flash_Write_NoCheck(SPI_FLASH_BUF, secpos * 4096, 4096);
        } else {
            SPI_Flash_Write_NoCheck(pBuffer, WriteAddr, secremain);
        }
        if (size == secremain) break;
        secpos++;
        secoff = 0;
        pBuffer   += secremain;
        WriteAddr += secremain;
        size      -= secremain;
        secremain = (size > 4096) ? 4096 : size;
    }
}

void SPI_Flash_Write_NoCheck(u8 *pBuffer, u32 WriteAddr, u16 size) {
    u16 pageremain = 256 - WriteAddr % 256;
    if (size <= pageremain) pageremain = size;
    while (1) {
        SPI_Flash_Write_Page(pBuffer, WriteAddr, pageremain);
        if (size == pageremain) break;
        pBuffer   += pageremain;
        WriteAddr += pageremain;
        size      -= pageremain;
        pageremain = (size > 256) ? 256 : size;
    }
}

/* ============================================================
 * KERNEL — PRIMITIVAS E TABELA
 * ============================================================ */
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

const TaskFunc TASK_TABLE[MAX_TASKS] = {
    task0,  task1,  task2,  task3,  task4,
    task5,  task6,  task7,  task8,  task9,
    task10, task11, task_rede, task12, task13, task14
};

void run(u8 any_process)
{
    if (any_process < MAX_PROCESSES && PROCESS[any_process] == 1)
    {
        if (any_process < MAX_TASKS) TASK_TABLE[any_process]();
    }
}

void destroy(u8 any_process)
{
    if (any_process < MAX_PROCESSES) PROCESS[any_process] = 0;
}

/* ============================================================
 * PROVISIONAMENTO
 * ============================================================ */
void gravar_provisao(void)
{
    static u8 buf[4096];
    memset(buf, 0xFF, sizeof(buf));

    buf[FLASH_PROV_FLAG_ADDR - FLASH_PROV_BASE] = FLAG_PROV_MAGIC;

    memcpy(&buf[FLASH_PROV_BIT0_ADDR - FLASH_PROV_BASE], CTX.bit0, 2);
    memcpy(&buf[FLASH_PROV_BIT1_ADDR - FLASH_PROV_BASE], CTX.bit1, 2);
    memcpy(&buf[FLASH_PROV_BIT2_ADDR - FLASH_PROV_BASE], CTX.bit2, 2);
    memcpy(&buf[FLASH_PROV_BIT3_ADDR - FLASH_PROV_BASE], CTX.bit3, 2);
    memcpy(&buf[FLASH_PROV_BIT4_ADDR - FLASH_PROV_BASE], CTX.bit4, 2);

    SPI_Flash_Erase_Sector(FLASH_PROV_SETOR);
    SPI_Flash_Write_NoCheck(buf, FLASH_PROV_BASE, 4096);

    FLAG_PROVISIONADO = 1;
    printf("[PROV] Gravado na flash externa\r\n");
}

void carregar_provisao(void)
{
    u8 magic = 0;
    SPI_Flash_Read(&magic, FLASH_PROV_FLAG_ADDR, 1);

    if (magic != FLAG_PROV_MAGIC) {
        FLAG_PROVISIONADO = 0;
        printf("[PROV] Device nao provisionado\r\n");
        return;
    }

    SPI_Flash_Read((u8*)CTX.bit0, FLASH_PROV_BIT0_ADDR, 2);
    SPI_Flash_Read((u8*)CTX.bit1, FLASH_PROV_BIT1_ADDR, 2);
    SPI_Flash_Read((u8*)CTX.bit2, FLASH_PROV_BIT2_ADDR, 2);
    SPI_Flash_Read((u8*)CTX.bit3, FLASH_PROV_BIT3_ADDR, 2);
    SPI_Flash_Read((u8*)CTX.bit4, FLASH_PROV_BIT4_ADDR, 2);

    CTX.bit0[1] = '\0'; CTX.bit1[1] = '\0'; CTX.bit2[1] = '\0';
    CTX.bit3[1] = '\0'; CTX.bit4[1] = '\0';

    if (CTX.bit0[0] != '0' && CTX.bit0[0] != '1') CTX.bit0[0] = '0';
    if (CTX.bit1[0] != '0' && CTX.bit1[0] != '1') CTX.bit1[0] = '0';
    if (CTX.bit2[0] != '0' && CTX.bit2[0] != '1') CTX.bit2[0] = '0';
    if (CTX.bit3[0] != '0' && CTX.bit3[0] != '1') CTX.bit3[0] = '0';
    if (CTX.bit4[0] != '0' && CTX.bit4[0] != '1') CTX.bit4[0] = '0';

    FLAG_PROVISIONADO = 1;
    printf("[PROV] Provisionado: b0=%s b1=%s b2=%s b3=%s b4=%s\r\n",
           CTX.bit0, CTX.bit1, CTX.bit2, CTX.bit3, CTX.bit4);
}

void reset_provisao(void)
{
    SPI_Flash_Erase_Sector(FLASH_PROV_SETOR);
    FLAG_PROVISIONADO = 0;
    memset(&CTX, 0, sizeof(CTX));
    printf("[PROV] Reset efetuado\r\n");
}

/* ============================================================
 * TASKS — BASE (o negocio entra AQUI)
 * ============================================================ */

/* task_rede: backoff de reconexao. Substitui task_reconnect.
 * NAO fecha socket em SYN_SENT. Chama CreateTcpSocket so a cada 5 s. */
void task_rede(void)
{
    if (CONNECT_MODE == 0)
     {
        u8 zero_ip[4] = {0};
        if (memcmp(IPAddr, zero_ip, 4) == 0) return;  /* espera DHCP */

        if (is_socket_connected(SocketId)) {
            reconnect_retries      = 0;
            last_reconnect_attempt = 0;
            return;
        }

        u32 now = RTC_GetCounter();
        if ((now - last_reconnect_attempt) > 5) {      /* 5 s */
            if (reconnect_retries < MAX_RETRY_ATTEMPTS) {
                WCHNET_CreateTcpSocket();
                reconnect_retries++;
            } else {
                reconnect_retries = 0;
            }
            last_reconnect_attempt = now;
        }
    }
    /* CONNECT_MODE == 1: reconexao via HLK-RM08K, nada a fazer aqui */
}

void task0(void)  { }   /* negocio */
void task1(void)  { }   /* negocio */
void task2(void)  { }   /* negocio */
void task3(void)  { }   /* negocio */
void task4(void)  { }   /* negocio */
void task5(void)  { RTC_Get(); }
void task6(void)  { }   /* negocio */
void task7(void)  { }   /* negocio */
void task8(void)  { }   /* negocio */
void task9(void)  { }   /* negocio */
void task10(void) { }   /* negocio */
void task11(void) { }   /* negocio */
void task12(void) { }   /* negocio */
void task13(void) { Request_Server("{ Other Request TEST }\r\n"); }   /* negocio */

/* task14: provisionamento via HTTP (Request_Server robusto). */
void task14(void)
{
    char request[1024] = "";
    char ind[5];
    char istoken[50]  = "";



   // if (PROV_POSSIBILITY < 10)
    {
        strcpy(request, "id-token=");
        strcat(request, TOKEN_CLIENT);
        strcat(request, "&request_proviment.cql=action");
        Request_Server(request);

        printf("Tentativa de Provisao:");
        itoa(PROV_POSSIBILITY, ind, 10);
        printf(ind);
        printf("\r\n");
        printf("%s", SERVER_RESPONSE);

        GET_VAR_AUTO(istoken, sizeof(istoken), (char*)SERVER_RESPONSE,
                     "<device_token_id>", "</device_token_id>");

        GET_VAR_AUTO(CTX.bit0, sizeof(CTX.bit0), (char*)SERVER_RESPONSE, "<bit0>", "</bit0>");
        GET_VAR_AUTO(CTX.bit1, sizeof(CTX.bit1), (char*)SERVER_RESPONSE, "<bit1>", "</bit1>");
        GET_VAR_AUTO(CTX.bit2, sizeof(CTX.bit2), (char*)SERVER_RESPONSE, "<bit2>", "</bit2>");
        GET_VAR_AUTO(CTX.bit3, sizeof(CTX.bit3), (char*)SERVER_RESPONSE, "<bit3>", "</bit3>");
        GET_VAR_AUTO(CTX.bit4, sizeof(CTX.bit4), (char*)SERVER_RESPONSE, "<bit4>", "</bit4>");

        if (strcmp(istoken, TOKEN_CLIENT) == 0)
        {
            gravar_provisao();
        }
        PROV_POSSIBILITY++;
    }
}

/* ============================================================
 * MAIN — loop limpo, sem auto-sabotagem
 * ============================================================ */
int main(void)
{
    u8 i;

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    SystemCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(115200);
    Delay_Ms(3000);

    TIM2_Init();
    SPI_Flash_Init();
    Delay_Ms(500);

    printf("\r\n============================================\r\n");
    printf("                  EloidaOS System                  \r\n");
    printf("ELOIDA OS - Standard Firmware\r\n");
    printf("Clock: %d MHz\r\n", SystemCoreClock / 1000000);
    printf("========== by ABSF-TECH,2026 ================\r\n");

    carregar_provisao();

    if (WCHNET_LIB_VER != WCHNET_GetVer()) {
        printf("Library version mismatch!\r\n");
        while (1);
    }

    WCHNET_GetMacAddr(MACAddr);
    printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
           MACAddr[0], MACAddr[1], MACAddr[2],
           MACAddr[3], MACAddr[4], MACAddr[5]);

    set_hostname("192.168.101.22");
    WCHNET_DHCPSetHostname(DHCP_HOSTNAME);

    u8 ret = ETH_LibInit(IPAddr, GWIPAddr, IPMask, MACAddr);
    mStopIfError(ret);
    printf("Network initialized, waiting for DHCP...\r\n");

    WCHNET_DHCPStart(WCHNET_DHCPCallBack);
    memset(&DnsCtx, 0, sizeof(DnsContext));

    uart_wifi_data_init();
    RTC_Init();

    printf("Tabela de tarefas (MAX_TASKS=%d)\r\n", MAX_TASKS);

    while (1)
    {
        WCHNET_MainTask();

        for (i = 0; i < MAX_TASKS; i++)
        {
            process(PRIME_NUMS[i], i);
            run(i);
            destroy(i);
        }

        if (WCHNET_QueryGlobalInt())
        {
            WCHNET_HandleGlobalInt();
        }
        Delay_Us(10);
    }
}