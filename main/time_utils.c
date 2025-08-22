#include "display.h"

int clock_x = 0, clock_y = 0;

void draw_line_text(int y, const char *text, uint16_t color) {
    fill_rect(0, y, TFT_WIDTH, 10, COLOR_BLACK);
    draw_string(4, y, text, color);
}

void fmt_time(int h, int m, char *out5) {
    out5[0] = '0' + (h/10);
    out5[1] = '0' + (h%10);
    out5[2] = ':';
    out5[3] = '0' + (m/10);
    out5[4] = '0' + (m%10);
    out5[5] = 0;
}

void fmt_date(int y, int m, int d, char out[11]) {
    out[0]='0'+(y/1000)%10; out[1]='0'+(y/100)%10; out[2]='0'+(y/10)%10; out[3]='0'+(y%10);
    out[4]='-';
    out[5]='0'+(m/10); out[6]='0'+(m%10);
    out[7]='-';
    out[8]='0'+(d/10); out[9]='0'+(d%10);
    out[10]=0;
}

void parse_date(const char* s, int* y, int* m, int* d) {
    *y = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    *m = (s[5]-'0')*10 + (s[6]-'0');
    *d = (s[8]-'0')*10 + (s[9]-'0');
}

void clock_draw_full(int h, int m) {
    fill_rect(clock_x, clock_y, FONT_W*5, FONT_H, COLOR_BLACK);
    char buf[6]; fmt_time(h, m, buf);
    draw_string(clock_x, clock_y, buf, COLOR_WHITE);
}

void clock_draw_hours(int h) {
    fill_rect(clock_x, clock_y, FONT_W*2, FONT_H, COLOR_BLACK);
    char hh[3] = { '0'+(h/10), '0'+(h%10), 0 };
    draw_string(clock_x, clock_y, hh, COLOR_WHITE);
}

void clock_draw_minutes(int m) {
    int x = clock_x + 3*FONT_W;
    fill_rect(x, clock_y, FONT_W*2, FONT_H, COLOR_BLACK);
    char mm[3] = { '0'+(m/10), '0'+(m%10), 0 };
    draw_string(x, clock_y, mm, COLOR_WHITE);
}

static inline int days_in_month(int year, int month) {
    static const int dm[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int d = dm[(month-1+12)%12];
    int leap = ((year%4==0) && (year%100!=0)) || (year%400==0);
    if (month == 2 && leap) d = 29;
    return d;
}

void clamp_day_month_y(int* day, int* month, int year) {
    if (*month < 1) {
        *month = 12;
    } else if (*month > 12) {
        *month = 1;
    }
    int maxd = days_in_month(year, *month);
    if (*day < 1) {
        *day = maxd;
    } else if (*day > maxd) {
        *day = 1;
    }
}

void clamp_time(int *h, int *m) {
    if (*h < 0)  *h = 23;
    if (*h > 23) *h = 0;
    if (*m < 0)  *m = 59;
    if (*m > 59) *m = 0;
}