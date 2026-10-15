#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "orion_bus.pio.h" // Cabeçalho gerado automaticamente pelo pioasm
#include "ringbuffer.h"
#include "ps2_keyboard.h"
#include "sd.h"
#include "ds1307.h"

#ifdef ETHERNET_VIA_WIFI

extern uint8_t *arquivo_buffer;
extern bool arquivo_pronto;
extern uint8_t arquivo_ok;
extern uint8_t arquivo_tamh;
extern uint8_t arquivo_tamL;
extern uint32_t arquivo_tamanho;
extern uint32_t ponteiro_leitura;
extern uint32_t arquivo_crc32;
uint16_t ponteiro_leitura_setor = 0;
#endif

// Definições de bits para o status_registro (Reg 0x01)
#define STATUS_BUSY 0x00
#define STATUS_READY 0x01
#define OPER_ESCRITA    0x00
#define OPER_LEITURA    0x01
int i=0,j=0;
int sd_counter=0;

uint32_t crc32_calculate(const uint8_t *buffer, size_t length) ;
/*
 * pico_rtc_cases.c -- SUBSTITUI os cases 0x0E..0x15 do switch do Pico 2 W.
 *
 * Protocolo (68000):
 *   ESCREVER a hora: escreve os 4 bytes do timestamp nos cases 0x0E, 0x0F, 0x10 e 0x11
 *                    (mais significativo primeiro). No 0x11 o Pico grava no DS1307.
 *   LER a hora:      le os cases 0x12, 0x13, 0x14, 0x15 (mais significativo primeiro).
 *                    No 0x12 o Pico le o DS1307 e prepara os 4 bytes.
 *
 * Mudancas em relacao ao seu codigo:
 *   - ds1307_write saiu do case 0x15 (leitura) e foi para o 0x11 (fim da escrita). Antes, TODA
 *     leitura regravava o relogio, e cada ds1307_write zera a contagem interna dos segundos:
 *     o relogio atrasaria a cada leitura.
 *   - b3 << 24 com b3 uint8_t vira int e estoura com bytes >= 0x80; agora tem cast para uint32_t.
 *   - se o DS1307 nao responde (ds1307_read false), o timestamp devolvido e' 0.
 *   - printf de depuracao so' com RTC_DEBUG: printf dentro do tratador atrasa a resposta ao 68000.
 *
 * Variaveis que ja existem no seu codigo: dado_m68k, byte_resposta, b0..b3 (uint8_t),
 * rtc_to_timestamp(), timestamp_to_rtc().
 */
#define RTC_DEBUG 1 
#ifdef RTC_DEBUG
#define RTC_DBG(...) printf(__VA_ARGS__)
#else
#define RTC_DBG(...) ((void)0)
#endif
extern unsigned long rtc_to_timestamp(const rtc_time_t *rtc);
extern void timestamp_to_rtc(unsigned long ts, rtc_time_t *rtc);
extern bool ds1307_write(const rtc_time_t *t);
static rtc_time_t rtc;
uint8_t b3 = 0;
uint8_t b2 = 0;
uint8_t b1 = 0;
uint8_t b0 = 0;

void __not_in_flash_func(gerenciar_barramento_m68k)(PIO pio, uint sm){
    if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        kbd_int_off();

        uint16_t pacote = pio_sm_get_blocking(pio, sm);
        uint8_t operacao  = (pacote >> 14) & 0x03; // Bits 15:14 (0x0 = Escrita, 0x1 = Leitura)
        uint8_t reg       = (pacote >> 8)  & 0x3F; // Bits 13:8  (Endereço A1-A6: 0x00 a 0x3F)
        uint8_t dado_m68k = pacote & 0xFF;        // Bits 7:0   (Dado D0-D7)

        printf("reg: %02x\n", reg);
        pio_sm_clear_fifos(pio, sm);
        uint8_t byte_resposta = 0x0;
        //printf("operacao[%02x] sm[%02x]  reg[%02x]  \n",operacao,sm,reg);
#ifdef ETHERNET_VIA_WIFI
        if ( arquivo_pronto == 0){
            byte_resposta = 0x0;
        }else if ( ponteiro_leitura >= arquivo_tamanho ) {
            ponteiro_leitura = 0;
            arquivo_pronto = 0;
        }
#endif
        switch (reg) {
            case 0x00: // --- m68k leu 0xFF9101: REGISTRADOR DE DADOS ---
#ifdef ETHERNET_VIA_WIFI
                    byte_resposta = arquivo_buffer[ponteiro_leitura];
                    ponteiro_leitura++; // Avança o ponteiro do arquivo
#endif
                    break;

            case 0x01: // --- m68k leu 0xFF9103: REGISTRADOR DE STATUS ---
#ifdef ETHERNET_VIA_WIFI
                if (!arquivo_pronto) {
                    byte_resposta = 0x00; // PICO_STATE_IDLE
                } else if (ponteiro_leitura < arquivo_tamanho) {
                    byte_resposta = 0x01; // PICO_STATE_HAS_FILE
                } else {
                    byte_resposta = 0x02; // PICO_STATE_EOF
                }
#endif
                break;

            case 0x02: // --- m68k leu 0xFF9105: SIZE HIGH (Byte Alto) ---
#ifdef ETHERNET_VIA_WIFI
                    byte_resposta = (uint8_t)((arquivo_tamanho >> 8) & 0xFF);
#endif
                break;

            case 0x03: // --- m68k leu 0xFF9107: SIZE LOW (Byte Baixo) ---
#ifdef ETHERNET_VIA_WIFI
                    byte_resposta = (uint8_t)(arquivo_tamanho & 0xFF);
#endif
                break;
            case 0x04:
#ifdef ETHERNET_VIA_WIFI
                byte_resposta = (arquivo_crc32 >> 24)& 0xFF;
#endif
                break;
            case 0x05:
#ifdef ETHERNET_VIA_WIFI
                byte_resposta = (arquivo_crc32 >> 16)& 0xFF;
#endif
                break;
            case 0x06:
#ifdef ETHERNET_VIA_WIFI
                byte_resposta = (arquivo_crc32 >> 8 )& 0xFF;
#endif
                break;
            case 0x07:
#ifdef ETHERNET_VIA_WIFI
                byte_resposta = arquivo_crc32 & 0xFF;
#endif 
                break;
            case 0x08:
#ifdef ETHERNET_VIA_WIFI
                arquivo_pronto = 0x0;
                byte_resposta = 0x0;
#endif
                break;
            case 0x09:
                return;
            case 0x0A:      //sector low
#ifdef ETHERNET_VIA_WIFI
                SECTOR_DEF * sd0 = get_sdcard_instance();
                sd0->sector = dado_m68k;
                ponteiro_leitura_setor = 0;
#endif
                return;
            case 0x0B:      //sector high
#ifdef ETHERNET_VIA_WIFI
                sd0 = get_sdcard_instance();
                sd0->sector |= (dado_m68k << 8)&0xFF00;
                ponteiro_leitura_setor = 0;
#endif
                return;
            case 0x0C:     //read sector
#ifdef ETHERNET_VIA_WIFI
                sd0 = get_sdcard_instance();
                if (sd0->card){    // Garante que o cartão inicializou com sucesso
                    sd0->card->read_blocks(sd0->card, sd0->buffer, sd0->sector, 1);
                }
                arquivo_crc32 = crc32_calculate((const uint8_t *)sd0->buffer, 512);
#endif
                return;
            case 0x0D:      //get sector 
#ifdef ETHERNET_VIA_WIFI
                sd0 = get_sdcard_instance();
                byte_resposta = sd0->buffer[ponteiro_leitura_setor];
                //printf("%02x|",byte_resposta);
                //i++;
                //if( i == 16){
                //    i=0;
                //    printf("\n");
                //}
                ponteiro_leitura_setor++;
#endif
                break;
/******************** RTC ********************************************/
            case 0x0E:
                puts("b3");
                b3 = dado_m68k;
                break;
            case 0x0F:
                b2 = dado_m68k;
                break;
            case 0x10:
                b1 = dado_m68k;
                break;
            case 0x11: {                       /* ultimo byte: acerta o DS1307 */
                b0 = dado_m68k;
                uint32_t ts_w = ((uint32_t)b3 << 24) | ((uint32_t)b2 << 16) |
                                ((uint32_t)b1 <<  8) |  (uint32_t)b0;
                rtc_time_t rtc_w;
                timestamp_to_rtc(ts_w, &rtc_w);
                bool ok_w = ds1307_write(&rtc_w);
                RTC_DBG("RTC write ts=%lu ok=%d\n", (unsigned long)ts_w, ok_w);
                (void)ok_w;
                break;
            }
            case 0x12: {                       /* primeiro byte da leitura: le o DS1307 */
                puts("case 0x12");
                rtc_time_t rtc_r;
                uint32_t ts_r = 0;
                if (ds1307_read(&rtc_r))
                    ts_r = rtc_to_timestamp(&rtc_r);
                b3 = (uint8_t)(ts_r >> 24);
                b2 = (uint8_t)(ts_r >> 16);
                b1 = (uint8_t)(ts_r >>  8);
                b0 = (uint8_t)(ts_r);
                RTC_DBG("RTC read ts=%lu\n", (unsigned long)ts_r);
                byte_resposta = b3;
                break;
            }
            case 0x13:
                byte_resposta = b2;
                break;
            case 0x14:
                byte_resposta = b1;
                break;
            case 0x15:
                byte_resposta = b0;
                break;

/******************** RTC ********************************************/
            case 0x20:
                printf("Chegou aqui: %c\n",dado_m68k);
                break;
            case 0x21:    
                printf("Chegou aqui: %c\n",dado_m68k);
                break;
            default:
                byte_resposta = 0xFF;
                break;
        }
        //if( operacao == OPER_LEITURA ){
            pio_sm_put(pio, sm, byte_resposta);
//            printf("%02x|",byte_resposta);
//            i++;
//            if( i == 16){
//                i=0;
//                printf("\n");
//            }
        //}
        //sio_hw->gpio_clr = (1 << 19);
    }
}

/*

void __not_in_flash_func(gerenciar_barramento1_m68k)(PIO pio, uint sm){
    if (!pio_sm_is_rx_fifo_empty(pio, sm)) {
        //sio_hw->gpio_set = (1 << 19);

        uint16_t pacote = pio_sm_get_blocking(pio, sm);
        uint8_t operacao  = (pacote >> 14) & 0x03; // Bits 15:14 (0x0 = Escrita, 0x1 = Leitura)
        uint8_t reg       = (pacote >> 8)  & 0x3F; // Bits 13:8  (Endereço A1-A6: 0x00 a 0x3F)
        uint8_t dado_m68k = pacote & 0xFF;        // Bits 7:0   (Dado D0-D7)

        pio_sm_clear_fifos(pio, sm);
        uint8_t byte_resposta = 0x0;
        printf("   reg[%02x]  \n",reg);
                kbd_int_off();
        switch (reg) {
            case 0x09:
//                kb_get(&byte_resposta);
                kbd_int_off();
                return;
                break;    
            default:
                byte_resposta = 0xFF;
                break;
        }
        //if( operacao == OPER_LEITURA ){
        //    pio_sm_put(pio, sm, byte_resposta);
        //    //printf("Resposta [%c]\n",byte_resposta);
        //}
        //sio_hw->gpio_clr = (1 << 19);
    }
}
*/