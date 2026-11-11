#ifndef EU2_SYS_H
#define EU2_SYS_H
/* sleep at least ms milliseconds (AmigaDOS ticks of 20 ms, minimum one tick) */
void eu2_sleep_ms(int ms);
/* create a directory (ignored if it exists) */
void eu2_mkdir(const char *path);
#endif
