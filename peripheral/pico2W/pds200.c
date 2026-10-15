#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include "pico/stdlib.h"
//#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
//#include "lwip/tcp.h"
#include "orion_bus.pio.h" // Cabeçalho gerado automaticamente pelo pioasm
#include "ringbuffer.h"
#include "ps2_keyboard.h"
#include "hardware/vreg.h"
#include "sd.h"
#include "ds1307.h"

extern void gerenciar_barramento_m68k(PIO pio, uint sm);
extern void configurar_dma_pico(PIO pio, uint sm) ;
extern void core1_entry(void);
extern void sdtest(void);
extern unsigned long rtc_to_timestamp(const rtc_time_t *rtc);

#include <stdint.h>
#include <stdio.h>

#define SYSINFO_BASE   0x40000000UL
#define CHIP_ID_REG    (*(volatile uint32_t *)(SYSINFO_BASE + 0x00))

//SECTOR_DEF *sd0 = NULL;


int get_chip_id(void)
{
    uint32_t id = CHIP_ID_REG;

    uint32_t revision     = (id >> 28) & 0xF;
    uint32_t part         = (id >> 12) & 0xFFFF;
    uint32_t manufacturer = id & 0xFFF;

    printf("CHIP_ID bruto = 0x%08X\n", (unsigned int) id);
    printf("REVISION      = %u\n", (unsigned int) revision);
    printf("PART          = 0x%04X\n", (unsigned int) part);
    printf("MANUFACTURER  = 0x%03X\n", (unsigned int) manufacturer);

    return 0;
}

rtc_time_t rtc;

int main() {
    stdio_init_all();
//    crc32_init();
    kbd_int_off();
    ds1307_init();

    // 1. Aumenta a tensão do núcleo para suportar o overclock (ex: VREG_VOLTAGE_1_20V ou 1_30V)
   //vreg_set_voltage(VREG_VOLTAGE_1_30);
   //sleep_ms(2); // Dá um tempo para a tensão estabilizar
   set_sys_clock_khz(150000, true);

    sleep_ms(2500);

    get_chip_id();
    // --- DISPARA O CORE 1 PARA CUIDAR DO PS/2 ---
    multicore_launch_core1(core1_entry);

    // --- CONFIGURAÇÃO DA PIO PARA O BARRAMENTO M68K ---
    PIO pio_barramento = pio0;  // Escolhe o bloco PIO 0
    uint sm_m68k = 0;           // Escolhe a State Machine 0
//    uint sm_m68k1 = 1;          // Escolhe a State Machine 1
  
    // Tenta carregar o programa assembly na memória de instruções da PIO
    uint offset_programa = pio_add_program(pio_barramento, &orion_bus_program);
//    uint offset_programa1 = pio_add_program(pio_barramento, &orion_bus1_program);

    // Chama a nossa função auxiliar de inicialização que configurou os pinos
    orion_bus_program_init(pio_barramento, sm_m68k, offset_programa);
//    orion_bus1_program_init(pio_barramento, sm_m68k1, offset_programa1);

    // --- CORREÇÃO DE SEGURANÇA NO BOOT ---
    // Desliga o SM temporariamente, limpa as FIFOs de lixo elétrico e religa
    pio_sm_set_enabled(pio_barramento, sm_m68k, false);
    pio_sm_clear_fifos(pio_barramento, sm_m68k);
    pio_sm_set_enabled(pio_barramento, sm_m68k, true);

    //pio_sm_set_enabled(pio_barramento, sm_m68k1, false);
    //pio_sm_clear_fifos(pio_barramento, sm_m68k1);
    //pio_sm_set_enabled(pio_barramento, sm_m68k1, true);


    printf("PIO Inicializada! Aguardando ciclos de barramento do m68k...\n");

//      crc32_init();
  // Initialize Wi-Fi chip in station architecture mode
//    if (cyw43_arch_init()) {
//        printf("Wi-Fi initialization failed\n");
//        return -1;
//    }

//    cyw43_arch_enable_sta_mode();
//    printf("Connecting to Wi-Fi...\n");

    // Connect to your local network using standard timeout settings
 //   if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
 //       printf("Wi-Fi connection failed\n");
 //       //return -1;
 //   }
//    printf("Connected! IP Address: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    //Launch the socket setup
//    start_tcp_server();

    kb_init();
    initPS2();
    //SECTOR_DEF *sd0;
    //init_sdcard();
    //sd0 = get_sdcard_instance();
    
    ds1307_read(&rtc);
    printf("%04d/%02d/%02d %02d:%02d:%02d\n",rtc.year,rtc.month,rtc.day,rtc.hour,rtc.minute,rtc.second);
    printf("Timestamp [%ld]\n",rtc_to_timestamp(&rtc));
    uint32_t brtc = rtc_to_timestamp(&rtc);
    uint8_t b3 =  brtc >> 24 & 0xFF;
    uint8_t b2 =  brtc >> 16 & 0xFF;
    uint8_t b1 =  brtc >>  8 & 0xFF;
    uint8_t b0 =  brtc & 0xFF;
    printf("b3:[%02x] b2:[%02x] b1:[%02x] b0:[%02x] \n",b3,b2,b1,b0);
    uint32_t brtcR = b3 << 24; 
    brtcR |= b2 << 16 & 0x00FF0000;
    brtcR |= b1 <<  8 & 0x0000FF00;
    brtcR |= b0  & 0x000000FF;;
    printf("Timestamp [%ld]\n",brtcR);
     
    // Main background execution loop
    while (true) {
        // Keep the Wi-Fi architecture driver responsive (polls for network events)
//        cyw43_arch_poll();
        // Atende a PIO o mais rápido possível caso o m68k tenha pedido algo
        gerenciar_barramento_m68k(pio_barramento, sm_m68k);
        //gerenciar_barramento_m68k(pio_barramento, sm_m68k1);
    }
}
