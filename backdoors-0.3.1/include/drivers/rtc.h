/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DRIVERS_RTC_H
#define BD_DRIVERS_RTC_H

#include <stdbool.h>
#include <stdint.h>

struct rtc_time {
    uint16_t year;     /* ano completo, ex.: 2026 */
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  hour;     /* 0..23 */
    uint8_t  minute;   /* 0..59 */
    uint8_t  second;   /* 0..59 */
};

/* Lê o relógio de tempo real (CMOS). Hora local conforme a configuração da máquina. */
bool rtc_read(struct rtc_time *out);

#endif /* BD_DRIVERS_RTC_H */
