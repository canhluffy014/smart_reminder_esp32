#pragma once
#include "display.h"

extern int clock_x, clock_y;

void draw_line_text(int y, const char *text, uint16_t color);
void fmt_time(int h, int m, char *out5);
void fmt_date(int y, int m, int d, char out[11]);
void parse_date(const char* s, int* y, int* m, int* d);
void clock_draw_full(int h, int m);
void clock_draw_hours(int h);
void clock_draw_minutes(int m);
void clamp_day_month_y(int* day, int* month, int year);
void clamp_time(int *h, int *m);