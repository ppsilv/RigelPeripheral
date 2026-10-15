#include <stdint.h>

typedef struct {
    uint16_t year;      /* 2000..2099 */
    uint8_t  month;     /* 1..12 */
    uint8_t  day;       /* 1..31 */
    uint8_t  hour;      /* 0..23 */
    uint8_t  minute;    /* 0..59 */
    uint8_t  second;    /* 0..59 */
    uint8_t  weekday;   /* 0 = domingo ... 6 = sabado */
} rtc_time_t;

/* Converte timestamp (unsigned long) para rtc_time_t */
void timestamp_to_rtc(unsigned long ts, rtc_time_t *rtc) {
    unsigned long seconds = ts % 60;
    ts /= 60;
    rtc->second = (uint8_t)seconds;
    
    unsigned long minutes = ts % 60;
    ts /= 60;
    rtc->minute = (uint8_t)minutes;
    
    unsigned long hours = ts % 24;
    ts /= 24;
    rtc->hour = (uint8_t)hours;

    rtc->weekday = (uint8_t)((ts + 4) % 7); /* 1970-01-01 foi quinta-feira (4) */

    /* Algoritmo de conversão de dias para data civil (Gregório) */
    unsigned long z = ts + 719468;
    unsigned long era = z / 146097;
    unsigned long doe = z - era * 146097;
    unsigned year = (era * 400) + (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    unsigned doy = doe - (365 * year + year/4 - year/100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned day = doy - (153 * mp + 2) / 5 + 1;
    unsigned month = mp < 10 ? mp + 3 : mp - 9;
    year += (month <= 2);

    rtc->year = (uint16_t)year;
    rtc->month = (uint8_t)month;
    rtc->day = (uint8_t)day;
}

/* Converte rtc_time_t para timestamp (unsigned long) */
unsigned long rtc_to_timestamp(const rtc_time_t *rtc) {
    unsigned y = rtc->year;
    unsigned m = rtc->month;
    unsigned d = rtc->day;
    
    if (m <= 2) {
        y -= 1;
        m += 12;
    }
    unsigned long era = y / 400;
    unsigned yoe = y - era * 400;
    unsigned doy = (153 * (m - 3) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    unsigned long days = era * 146097 + doe - 719468;

    return days * 86400UL + rtc->hour * 3600UL + rtc->minute * 60UL + rtc->second;
}