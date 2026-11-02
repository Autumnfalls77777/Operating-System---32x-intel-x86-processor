/* ============================================================================
 * include/rtc.h  -  CMOS real-time clock
 * ============================================================================ */

#ifndef NOVA_RTC_H
#define NOVA_RTC_H

#include "types.h"

typedef struct {
    int second;
    int minute;
    int hour;
    int day;
    int month;
    int year;
    int weekday;   /* 0 = Sunday */
} rtc_time_t;

void rtc_read(rtc_time_t *out);
void rtc_format_time(const rtc_time_t *t, char *buf, int cap);   /* HH:MM     */
void rtc_format_time_sec(const rtc_time_t *t, char *buf, int cap); /* HH:MM:SS */
void rtc_format_date(const rtc_time_t *t, char *buf, int cap);   /* Mon 02 Nov 2026 */
void rtc_format_short_date(const rtc_time_t *t, char *buf, int cap); /* 02/11/2026 */

#endif /* NOVA_RTC_H */
