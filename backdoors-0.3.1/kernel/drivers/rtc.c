/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>

#include "arch/io.h"
#include "drivers/rtc.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define REG_SECOND   0x00
#define REG_MINUTE   0x02
#define REG_HOUR     0x04
#define REG_DAY      0x07
#define REG_MONTH    0x08
#define REG_YEAR     0x09
#define REG_STATUS_A 0x0A
#define REG_STATUS_B 0x0B
#define REG_CENTURY  0x32

#define STATUS_A_UPDATING 0x80
#define STATUS_B_24H      0x02
#define STATUS_B_BINARY   0x04
#define HOUR_PM           0x80

static uint8_t cmos_read(uint8_t reg)
{
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static uint8_t from_bcd(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

struct raw_time {
    uint8_t second, minute, hour, day, month, year, century;
};

static bool raw_equal(const struct raw_time *x, const struct raw_time *y)
{
    return x->second == y->second && x->minute == y->minute && x->hour == y->hour &&
           x->day == y->day && x->month == y->month && x->year == y->year &&
           x->century == y->century;
}

static void read_raw(struct raw_time *t)
{
    t->second  = cmos_read(REG_SECOND);
    t->minute  = cmos_read(REG_MINUTE);
    t->hour    = cmos_read(REG_HOUR);
    t->day     = cmos_read(REG_DAY);
    t->month   = cmos_read(REG_MONTH);
    t->year    = cmos_read(REG_YEAR);
    t->century = cmos_read(REG_CENTURY);
}

bool rtc_read(struct rtc_time *out)
{
    /* Espera o fim de uma atualização do relógio antes de ler. */
    for (uint32_t i = 0; i < 1000000u; i++) {
        if (!(cmos_read(REG_STATUS_A) & STATUS_A_UPDATING)) {
            break;
        }
    }

    /* Lê duas vezes até obter valores estáveis (evita leitura durante atualização). */
    struct raw_time a, b;
    read_raw(&a);
    do {
        b = a;
        read_raw(&a);
    } while (!raw_equal(&a, &b));

    uint8_t status_b = cmos_read(REG_STATUS_B);
    bool binary = (status_b & STATUS_B_BINARY) != 0;
    bool h24    = (status_b & STATUS_B_24H) != 0;

    uint8_t hour = a.hour;
    bool pm = !h24 && (hour & HOUR_PM);
    hour &= (uint8_t)~HOUR_PM;

    if (!binary) {
        a.second  = from_bcd(a.second);
        a.minute  = from_bcd(a.minute);
        hour      = from_bcd(hour);
        a.day     = from_bcd(a.day);
        a.month   = from_bcd(a.month);
        a.year    = from_bcd(a.year);
        a.century = from_bcd(a.century);
    }
    if (!h24) {
        if (pm && hour != 12) {
            hour = (uint8_t)(hour + 12);
        } else if (!pm && hour == 12) {
            hour = 0;
        }
    }

    uint16_t century = a.century ? a.century : 20;   /* registrador opcional */
    out->year   = (uint16_t)(century * 100 + a.year);
    out->month  = a.month;
    out->day    = a.day;
    out->hour   = hour;
    out->minute = a.minute;
    out->second = a.second;
    return true;
}
