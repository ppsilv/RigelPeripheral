/*
 * ds1307.c -- driver do RTC DS1307 (Pico SDK). Veja ds1307.h.
 *
 * Registradores do DS1307:
 *   0x00 segundos (bit7 = CH: 1 = oscilador PARADO)    0x04 dia do mes
 *   0x01 minutos                                       0x05 mes
 *   0x02 horas (bit6 = 1 modo 12 h; bit5 = PM)         0x06 ano (00..99)
 *   0x03 dia da semana (1..7)                          0x07 controle (SQW)
 * Todos em BCD.
 */
#include "ds1307.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"

#define REG_SECONDS   0x00
#define CH_BIT        0x80
#define HOUR_12H      0x40
#define HOUR_PM       0x20
#define I2C_TIMEOUT_US 20000          /* 20 ms: nao trava se o chip sumir */

static inline uint8_t bcd2bin(uint8_t v) { return (uint8_t)((v >> 4) * 10 + (v & 0x0F)); }
static inline uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static inline bool    bcd_ok(uint8_t v)  { return (v >> 4) <= 9 && (v & 0x0F) <= 9; }

/* le 'len' registradores a partir de 'reg' */
static bool reg_read(uint8_t reg, uint8_t *dst, size_t len)
{
    if (i2c_write_timeout_us(DS1307_I2C, DS1307_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1)
        return false;
    return i2c_read_timeout_us(DS1307_I2C, DS1307_ADDR, dst, len, false, I2C_TIMEOUT_US) == (int)len;
}

/* escreve 'len' registradores a partir de 'reg' */
static bool reg_write(uint8_t reg, const uint8_t *src, size_t len)
{
    uint8_t buf[9];
    if (len > 8) return false;
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) buf[1 + i] = src[i];
    return i2c_write_timeout_us(DS1307_I2C, DS1307_ADDR, buf, len + 1, false, I2C_TIMEOUT_US) == (int)(len + 1);
}

/* dia da semana, 0 = domingo (algoritmo de Sakamoto) */
uint8_t ds1307_weekday(uint16_t year, uint8_t month, uint8_t day)
{
    static const uint8_t t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (month < 3) year--;
    return (uint8_t)((year + year / 4 - year / 100 + year / 400 + t[month - 1] + day) % 7);
}

static bool is_leap(uint16_t y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
    static const uint8_t d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

bool ds1307_init(void)
{
    i2c_init(DS1307_I2C, DS1307_BAUD);
    gpio_set_function(DS1307_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(DS1307_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(DS1307_SDA_PIN);          /* reforco: o ideal sao pull-ups externos de 4,7 k em 3,3 V */
    gpio_pull_up(DS1307_SCL_PIN);

    uint8_t sec;
    return reg_read(REG_SECONDS, &sec, 1);   /* so' confere se o chip responde */
}

bool ds1307_is_running(void)
{
    uint8_t sec;
    if (!reg_read(REG_SECONDS, &sec, 1)) return false;
    return (sec & CH_BIT) == 0;
}

bool ds1307_read(rtc_time_t *t)
{
    uint8_t r[7];

    if (t == NULL) return false;
    if (!reg_read(REG_SECONDS, r, 7)) return false;      /* 7 bytes numa so' transacao: leitura coerente */

    if (r[0] & CH_BIT) return false;                     /* oscilador parado: hora nao vale */

    uint8_t sec = r[0] & 0x7F;
    uint8_t min = r[1];
    uint8_t hrs = r[2];
    bool    h12 = (hrs & HOUR_12H) != 0;
    bool    pm  = (hrs & HOUR_PM) != 0;
    uint8_t hbcd = h12 ? (hrs & 0x1F) : (hrs & 0x3F);

    if (!bcd_ok(sec) || !bcd_ok(min) || !bcd_ok(hbcd) || !bcd_ok(r[4]) || !bcd_ok(r[5]) || !bcd_ok(r[6]))
        return false;

    uint8_t hour = bcd2bin(hbcd);
    if (h12) {                                           /* converte 12 h -> 24 h */
        if (hour < 1 || hour > 12) return false;
        hour = (uint8_t)((hour % 12) + (pm ? 12 : 0));
    }

    t->second = bcd2bin(sec);
    t->minute = bcd2bin(min);
    t->hour   = hour;
    t->day    = bcd2bin(r[4]);
    t->month  = bcd2bin(r[5]);
    t->year   = (uint16_t)(2000 + bcd2bin(r[6]));

    if (t->second > 59 || t->minute > 59 || t->hour > 23 ||
        t->month < 1 || t->month > 12 || t->day < 1 || t->day > days_in_month(t->year, t->month))
        return false;

    t->weekday = ds1307_weekday(t->year, t->month, t->day);   /* nao confia no registrador 3 */
    return true;
}

bool ds1307_write(const rtc_time_t *t)
{
    uint8_t r[7];

    if (t == NULL) return false;
    if (t->year < 2000 || t->year > 2099 || t->month < 1 || t->month > 12 ||
        t->day < 1 || t->day > days_in_month(t->year, t->month) ||
        t->hour > 23 || t->minute > 59 || t->second > 59)
        return false;

    r[0] = bin2bcd(t->second);                           /* CH = 0: liga o oscilador */
    r[1] = bin2bcd(t->minute);
    r[2] = bin2bcd(t->hour);                             /* bit6 = 0: modo 24 h */
    r[3] = (uint8_t)(ds1307_weekday(t->year, t->month, t->day) + 1);   /* 1..7 (1 = domingo) */
    r[4] = bin2bcd(t->day);
    r[5] = bin2bcd(t->month);
    r[6] = bin2bcd((uint8_t)(t->year - 2000));

    return reg_write(REG_SECONDS, r, 7);
}
