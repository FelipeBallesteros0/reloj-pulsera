#include "cts_parse.h"
#include <string.h>

bool cts_parse(const uint8_t *d, size_t n, struct tm *t)
{
    if (!d || !t || n < 7) {
        return false;
    }
    int anio = d[0] | (d[1] << 8);
    if (anio < 2000 || anio > 2099 || d[2] < 1 || d[2] > 12 || d[3] < 1 || d[3] > 31 ||
        d[4] > 23 || d[5] > 59 || d[6] > 59) {
        return false;
    }
    memset(t, 0, sizeof(*t));
    t->tm_year = anio - 1900;
    t->tm_mon = d[2] - 1;
    t->tm_mday = d[3];
    t->tm_hour = d[4];
    t->tm_min = d[5];
    t->tm_sec = d[6];
    t->tm_wday = (n >= 8 && d[7] >= 1 && d[7] <= 7) ? (d[7] % 7) : 0;   // CTS 7=domingo → tm 0
    t->tm_isdst = -1;
    return true;
}
