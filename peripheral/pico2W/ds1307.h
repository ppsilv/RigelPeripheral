/*
 * ds1307.h -- driver do RTC DS1307 para o Pico 2 W (RP2350, Pico SDK).
 *
 * Ligacao:  SDA = GP26, SCL = GP27  (I2C1)      endereco I2C = 0x68
 *
 * ATENCAO ao hardware: o DS1307 pede VCC de 4,5 a 5,5 V, mas os GPIO do Pico NAO
 * toleram 5 V. Ligue os resistores de pull-up de SDA/SCL em 3,3 V (nao em 5 V),
 * ou use um conversor de nivel. Muitos modulos "DS1307" trazem pull-ups ligados
 * ao VCC: se o seu for assim, tire-os ou alimente o modulo com 3,3 V.
 *
 * Uso:
 *     ds1307_init();                         // uma vez, no boot
 *     rtc_time_t t;
 *     if (ds1307_read(&t)) { ... }           // le a hora
 *     ds1307_write(&t);                      // acerta a hora (e liga o relogio)
 *
 * Chame as funcoes sempre do MESMO core (o I2C nao e' protegido contra dois cores).
 */
#ifndef DS1307_H
#define DS1307_H

#include <stdint.h>
#include <stdbool.h>

/* mude aqui se trocar os pinos */
#ifndef DS1307_SDA_PIN
#define DS1307_SDA_PIN   26
#endif
#ifndef DS1307_SCL_PIN
#define DS1307_SCL_PIN   27
#endif
#ifndef DS1307_I2C
#define DS1307_I2C       i2c1            /* GP26/GP27 sao os pinos do I2C1 */
#endif
#ifndef DS1307_BAUD
#define DS1307_BAUD      100000          /* 100 kHz: o maximo do DS1307 */
#endif

#define DS1307_ADDR      0x68

typedef struct {
    uint16_t year;      /* 2000..2099 */
    uint8_t  month;     /* 1..12 */
    uint8_t  day;       /* 1..31 */
    uint8_t  hour;      /* 0..23 */
    uint8_t  minute;    /* 0..59 */
    uint8_t  second;    /* 0..59 */
    uint8_t  weekday;   /* 0 = domingo ... 6 = sabado (calculado pela data na escrita) */
} rtc_time_t;

/* Configura o I2C e confere se o DS1307 responde. false = nao achou o chip. */
bool ds1307_init(void);

/* true se o oscilador esta' rodando (false = bateria acabou/primeira vez: hora invalida) */
bool ds1307_is_running(void);

/* Le a hora. false = erro de I2C, ou valores invalidos (relogio nunca acertado). */
bool ds1307_read(rtc_time_t *t);

/* Acerta a hora e liga o oscilador. false = campo fora de faixa ou erro de I2C. */
bool ds1307_write(const rtc_time_t *t);

/* dia da semana (0 = domingo) para uma data; util sem acessar o chip */
uint8_t ds1307_weekday(uint16_t year, uint8_t month, uint8_t day);

#endif /* DS1307_H */
