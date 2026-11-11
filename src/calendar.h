/* calendar.h - EU2 game time: 30-day months, 360-day years, day 0-based (as in the scenario files).
 * Day numbers are y*360 + (m-1)*30 + d. cal_month_days gives the REAL length, for display only. */
#ifndef EU2_CALENDAR_H
#define EU2_CALENDAR_H
long cal_day(int year, int month, int day);            /* month 1..12, day 0..29 */
void cal_date(long dayno, int *year, int *month, int *day);
int  cal_month_days(int month);                         /* display only */
#endif
