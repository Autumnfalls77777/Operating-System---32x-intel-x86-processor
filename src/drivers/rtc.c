/* ============================================================================
 * src/drivers/rtc.c  -  CMOS real-time clock
 * ============================================================================ */

#include "kernel.h"
#include "rtc.h"

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static u8 cmos_read(u8 reg)
{
    port_outb(CMOS_INDEX, reg);
    io_wait();
    return port_inb(CMOS_DATA);
}

static int bcd_to_bin(u8 v)
{
    return (v & 0x0F) + ((v >> 4) * 10);
}

void rtc_read(rtc_time_t *out)
{
    if (!out) return;

    int guard = 1000000;
    while ((cmos_read(0x0A) & 0x80) && guard-- > 0) cpu_pause();

    u8 sec = cmos_read(0x00);
    u8 min = cmos_read(0x02);
    u8 hour = cmos_read(0x04);
    u8 day = cmos_read(0x07);
    u8 month = cmos_read(0x08);
    u8 year = cmos_read(0x09);
    u8 wday = cmos_read(0x06);
    u8 century = cmos_read(0x32);
    u8 status_b = cmos_read(0x0B);

    int binary = (status_b & 0x04) != 0;
    int hour12 = (status_b & 0x02) == 0;
    int pm = (hour & 0x80) != 0;

    if (!binary) {
        sec = (u8)bcd_to_bin(sec);
        min = (u8)bcd_to_bin(min);
        day = (u8)bcd_to_bin(day);
        month = (u8)bcd_to_bin(month);
        year = (u8)bcd_to_bin(year);
        century = (u8)bcd_to_bin(century);
        int h = bcd_to_bin((u8)(hour & 0x7F));
        hour = (u8)h;
    } else {
        hour = (u8)(hour & 0x7F);
    }

    if (hour12 && pm && hour < 12) hour = (u8)(hour + 12);
    if (hour12 && !pm && hour == 12) hour = 0;

    int full_year = (century >= 19 && century <= 99)
                  ? century * 100 + year
                  : 2000 + year;

    out->second = sec;
    out->minute = min;
    out->hour = hour;
    out->day = day;
    out->month = month;
    out->year = full_year;
    out->weekday = (wday == 0) ? 0 : (wday - 1) % 7;
}

void rtc_format_time(const rtc_time_t *t, char *buf, int cap)
{
    ksnprintf(buf, cap, "%02d:%02d", t->hour, t->minute);
}

void rtc_format_time_sec(const rtc_time_t *t, char *buf, int cap)
{
    ksnprintf(buf, cap, "%02d:%02d:%02d", t->hour, t->minute, t->second);
}

void rtc_format_date(const rtc_time_t *t, char *buf, int cap)
{
    static const char *days[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int wd = clamp(t->weekday, 0, 6);
    int mo = clamp(t->month - 1, 0, 11);
    ksnprintf(buf, cap, "%s %02d %s %04d", days[wd], t->day, months[mo], t->year);
}

void rtc_format_short_date(const rtc_time_t *t, char *buf, int cap)
{
    ksnprintf(buf, cap, "%02d/%02d/%04d", t->day, t->month, t->year);
}
