/* calendar.c - see calendar.h. EU2 time (docs/SPEC_TIME_ECONOMY.md, eu2.exe 0x417130/0x4171a0):
 * every month has 30 days, the year 360; day 0..29, month 0..11 internally. Real month lengths are
 * used only when a date is displayed. */
#include "calendar.h"

static const unsigned char mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

int cal_month_days(int m) { return (m >= 1 && m <= 12) ? mdays[m - 1] : 30; }

long cal_day(int y, int m, int d)
{
	if (m < 1) m = 1;
	if (m > 12) m = 12;
	if (d < 0) d = 0;
	if (d > 29) d = 29;
	return (long)y * 360 + (m - 1) * 30 + d;
}

void cal_date(long n, int *y, int *m, int *d)
{
	*y = (int)(n / 360);
	*m = (int)((n % 360) / 30) + 1;
	*d = (int)(n % 30);
}
