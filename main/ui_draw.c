#define COLOR_PENDING   COLOR_RED
#define COLOR_COMPLETED COLOR_GREEN
#define COLOR_REPEAT    COLOR_YELLOW
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <strings.h>
#include <time.h>
#include "display.h"
#include "reminders_store.h"
#include "time_utils.h"

const char* CONTENT_PRESETS[] = {
    "BAO THUC", "HOP SANG", "HOP CHIEU", "TAP THE DUC",
    "UONG THUOC", "NHAC HANH LY", "GOI DIEN", "KHOI HANH",
    "DI CHO", "DON TRE", "NHAC LAM VIEC", "NHAC SINH NHAT"
};

const int NUM_CONTENT_PRESETS = (int)(sizeof(CONTENT_PRESETS)/sizeof(CONTENT_PRESETS[0]));

static bool idle_screen_inited = false;
static int center_x(const char *s) { return (TFT_WIDTH - (int)strlen(s)*FONT_W)/2; }
int idle_x = 0, idle_y = 0;
uint32_t ui_epoch = 0;
int preset_index  = 0;  
TwoSel two_sel = SEL_LEFT;
int menu_index = 0;           
FieldSel field_sel = SEL_HOUR;
int submenu_index = 0; 
UiState ui_state = UI_IDLE;

static inline int status_rank(const char* s) {
    if (!s) return 2;
    if (strncmp(s, "pending", 7)  == 0) return 1;
    if (strncmp(s, "repeat",  6)  == 0) return 0;
    return 2;
}

static const char* status_label(const char* s) {
    if (!s) return "";
    if (!strncmp(s, "pending",   7)) return "PENDING";
    if (!strncmp(s, "completed", 9)) return "COMPLETED";
    if (!strncmp(s, "repeat",    6)) return "REPEAT";
    return "";
}

static inline uint16_t status_color(const char* s) {
    if (!s) return COLOR_WHITE;
    if (!strncmp(s, "pending",   7)) return COLOR_PENDING;
    if (!strncmp(s, "completed", 9)) return COLOR_COMPLETED;
    if (!strncmp(s, "repeat",    6)) return COLOR_REPEAT;
    return COLOR_WHITE;
}

void show_alarm_feedback(const char *msg, uint16_t color) {
    if (!msg) return;
    fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
    int x = (TFT_WIDTH  - (int)strlen(msg)*FONT_W)/2;
    int y = (TFT_HEIGHT - FONT_H)/2;
    if (x < 0) x = 0;
    draw_string(x, y, msg, color);
}

void idle_draw_upcoming(const struct tm* now_local) {
    if (!now_local) return;
    const int base_y = idle_y + FONT_H + 6 + 12;  
    const int line_h = 12;
    struct tm now_tm = *now_local;
    now_tm.tm_sec = 0;
    time_t now_ts = mktime(&now_tm);  
	typedef struct { int idx; int rank; int days_until; int min_in_day; } Slot;
	Slot top[3] = { {-1,99,INT_MAX,INT_MAX}, {-1,99,INT_MAX,INT_MAX}, {-1,99,INT_MAX,INT_MAX} };
	int now_total_min = now_local->tm_hour*60 + now_local->tm_min;
	xSemaphoreTake(reminders_mutex, portMAX_DELAY);
	for (int i = 0; i < num_reminders; i++) {
    	const Reminder *r = &reminders[i];
    	if (strncmp(r->status, "completed", 9) == 0) continue; 
    	int rank = status_rank(r->status); 
    	int days_until = 0;
    	int min_in_day = 0;
    	if (strncmp(r->status, "repeat", 6) == 0) {
        	int ev_total_min = r->hour*60 + r->minute;
        	if (ev_total_min >= now_total_min) {
            	days_until = 0;
            	min_in_day = ev_total_min - now_total_min;
        	} else {
            	days_until = 1;
            	min_in_day = ev_total_min + 1440 - now_total_min;
        	}
    	} else {
        	int y,m,d; parse_date(r->date, &y,&m,&d);
        	struct tm ev = *now_local;
        	ev.tm_year = y - 1900; ev.tm_mon = m - 1; ev.tm_mday = d;
        	ev.tm_hour = r->hour;  ev.tm_min = r->minute; ev.tm_sec = 0;
        	time_t now_ts = mktime((struct tm*)now_local);
        	time_t ev_ts  = mktime(&ev);
        	long delta_min = (long)((ev_ts - now_ts)/60);
        	if (delta_min < 0) continue;             
        	days_until = (int)(delta_min / 1440);    
        	min_in_day = (int)(delta_min % 1440);   
    	}
    	for (int k=0; k<3; k++) {
        	if (rank < top[k].rank ||
        	(rank == top[k].rank && (days_until < top[k].days_until ||
           	(days_until == top[k].days_until && min_in_day < top[k].min_in_day)))) {
            	for (int t=2; t>k; t--) top[t] = top[t-1];
            	top[k].idx = i; top[k].rank = rank;
            	top[k].days_until = days_until; top[k].min_in_day = min_in_day;
            	break;
        	}
    	}
	}
	xSemaphoreGive(reminders_mutex);
	fill_rect(0, base_y - 2, TFT_WIDTH, line_h*3 + 4, COLOR_BLACK);
	for (int row = 0; row < 3; row++) {
    	int y = base_y + row * line_h;
    	if (top[row].idx < 0) continue;
    	Reminder r;
    	xSemaphoreTake(reminders_mutex, portMAX_DELAY);
    	r = reminders[top[row].idx];
    	xSemaphoreGive(reminders_mutex);
    	char hhmm[6]; fmt_time(r.hour, r.minute, hhmm);
    	const char* st = status_label(r.status);
    	char st_bracket[16]; snprintf(st_bracket, sizeof(st_bracket), "[%s]", st);
    	int status_len = (int)strlen(st_bracket);
    	int max_chars     = TFT_WIDTH / FONT_W;   
    	int fixed_prefix  = 6;                    
    	int avail_content = max_chars - fixed_prefix - 1 - status_len;
    	if (avail_content < 0) avail_content = 0;
    	char content_cut[32];
    	snprintf(content_cut, sizeof(content_cut), "%.*s", avail_content, r.content);
    	char prefix[64];
    	snprintf(prefix, sizeof(prefix), "%s %s ", hhmm, content_cut);
    	fill_rect(0, y, TFT_WIDTH, line_h, COLOR_BLACK);
    	draw_string(4, y, prefix, COLOR_WHITE);
    	int sx = 4 + (int)strlen(prefix) * FONT_W;
    	draw_string(sx, y, st_bracket, status_color(r.status));
	}
}

void draw_idle_screen_now(void) {
    time_t now; struct tm ti;
    time(&now); localtime_r(&now, &ti);
    fill_screen(COLOR_BLACK);
    const char *title = "THOI GIAN HIEN TAI";
    draw_string((TFT_WIDTH - (int)strlen(title)*FONT_W)/2, 20, title, COLOR_GREEN);
    idle_x = (TFT_WIDTH  - 5 * FONT_W)/2;   
    idle_y = (TFT_HEIGHT - FONT_H)/2 - 6;
    char hhmm[6]; fmt_time(ti.tm_hour, ti.tm_min, hhmm);
    draw_string(idle_x, idle_y, hhmm, COLOR_WHITE);
    char datebuf[11];
    fmt_date(ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday, datebuf);
    int dx = (TFT_WIDTH - (int)strlen(datebuf)*FONT_W)/2;
    draw_string(dx, idle_y + FONT_H + 6, datebuf, COLOR_YELLOW);
}

void idle_clock_screen_init(void) {
    fill_screen(COLOR_BLACK);
    const char *title = "THOI GIAN HIEN TAI";
    draw_string(center_x(title), 20, title, COLOR_GREEN);
    clock_x = (TFT_WIDTH - 5*FONT_W)/2;            
    clock_y = (TFT_HEIGHT - FONT_H)/2;
    idle_screen_inited = true;
}

void ui_draw_menu(void) {
    static uint32_t last_epoch = (uint32_t)-1;
    static int prev_idx = -1;
    if (last_epoch != ui_epoch) {
        fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
        draw_line_text( 4, "MENU CAI DAT", COLOR_GREEN);
        uint16_t sel = COLOR_YELLOW;
        draw_line_text(20, (menu_index==0)? "> XEM"   : "  XEM",   (menu_index==0)? sel:COLOR_WHITE);
        draw_line_text(32, (menu_index==1)? "> CHINH" : "  CHINH", (menu_index==1)? sel:COLOR_WHITE);
        draw_line_text(44, (menu_index==2)? "> THEM"  : "  THEM",  (menu_index==2)? sel:COLOR_WHITE);
        draw_line_text(56, (menu_index==3)? "> XOA"   : "  XOA",   (menu_index==3)? sel:COLOR_WHITE);
        draw_line_text(100,"OK:CHON  NEXT:LEN", COLOR_BLUE);
        draw_line_text(112,"BACK:XUONG  CANCEL:THOAT", COLOR_BLUE);
        prev_idx = menu_index; last_epoch = ui_epoch; return;
    }
    if (prev_idx!=menu_index) {
        uint16_t sel = COLOR_YELLOW;
        draw_line_text(20, (menu_index==0)? "> XEM"   : "  XEM",   (menu_index==0)? sel:COLOR_WHITE);
        draw_line_text(32, (menu_index==1)? "> CHINH" : "  CHINH", (menu_index==1)? sel:COLOR_WHITE);
        draw_line_text(44, (menu_index==2)? "> THEM"  : "  THEM",  (menu_index==2)? sel:COLOR_WHITE);
        draw_line_text(56, (menu_index==3)? "> XOA"   : "  XOA",   (menu_index==3)? sel:COLOR_WHITE);
        prev_idx = menu_index;
    }
}

void ui_draw_pick_list(const char *title) {
    static uint32_t last_epoch = (uint32_t)-1;
    static int prev_idx = -1;
    static int prev_base = -1;
    int base = (pick_index/6)*6;
    if (last_epoch != ui_epoch || base != prev_base) {
        fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
        draw_line_text(4, title, COLOR_GREEN);
        xSemaphoreTake(reminders_mutex, portMAX_DELAY);
        for (int i=0; i<6 && (base+i)<num_reminders; i++) {
            char tt[6], buf[16];
            fmt_time(reminders[base+i].hour, reminders[base+i].minute, tt);
            snprintf(buf, sizeof(buf), "%c %s", (base+i)==pick_index?'>':' ', tt);
            draw_line_text(20 + i*12, buf, ((base+i)==pick_index)? COLOR_GREEN : COLOR_WHITE);
        }
        xSemaphoreGive(reminders_mutex);
        draw_line_text(100, "OK:CHON  NEXT:LEN", COLOR_BLUE);
        draw_line_text(112, "BACK:XUONG", COLOR_BLUE);
        draw_line_text(124, "CANCEL:THOAT", COLOR_BLUE);
        prev_idx = pick_index;
        prev_base = base;
        last_epoch = ui_epoch;
        return;
    }
    if (prev_idx != pick_index) {
        int old_row = prev_idx - base;
        int new_row = pick_index - base;
        if (old_row >=0 && old_row < 6) {
            char tt[6], buf[16];
            xSemaphoreTake(reminders_mutex, portMAX_DELAY);
            fmt_time(reminders[prev_idx].hour, reminders[prev_idx].minute, tt);
            xSemaphoreGive(reminders_mutex);
            snprintf(buf, sizeof(buf), "  %s", tt);
            draw_line_text(20 + old_row*12, buf, COLOR_WHITE);
        }
        if (new_row >=0 && new_row < 6) {
            char tt[6], buf[16];
            xSemaphoreTake(reminders_mutex, portMAX_DELAY);
            fmt_time(reminders[pick_index].hour, reminders[pick_index].minute, tt);
            xSemaphoreGive(reminders_mutex);
            snprintf(buf, sizeof(buf), "> %s", tt);
            draw_line_text(20 + new_row*12, buf, COLOR_GREEN);
        }
        prev_idx = pick_index;
    }
}

void ui_draw_time_editor(const char *title, int h, int m, FieldSel sel, bool show_hint_cancel_save) {
    const int FW = FONT_W, FH = FONT_H;
    const int X0 = (TFT_WIDTH - 5*FW)/2;
    const int Y0 = 60;
    const int HOUR_X = X0, MIN_X = X0 + 3*FW;
    const int HOUR_W = 2*FW, MIN_W = 2*FW;
    static uint32_t last_epoch = (uint32_t)-1;
    static int  prev_h = -1, prev_m = -1;
    static FieldSel prev_sel = SEL_HOUR;
    static bool prev_hint = false;
    static char prev_title[24] = {0};
    if (last_epoch != ui_epoch || strcmp(prev_title, title)!=0 || prev_hint != show_hint_cancel_save) {
        fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
        draw_line_text(4, title, COLOR_GREEN);
        draw_line_text(96, "NEXT/BACK:+/-", COLOR_BLUE);
        draw_line_text(108,"OK:LUU TRUONG",  COLOR_BLUE);
        draw_line_text(120, show_hint_cancel_save ? "CANCEL:LUU & THOAT" : "CANCEL:THOAT", COLOR_BLUE);
        draw_string(X0 + 2*FW, Y0, ":", COLOR_WHITE);
        prev_h = -1; prev_m = -1; prev_sel = sel;
        strncpy(prev_title, title, sizeof(prev_title)-1);
        prev_hint = show_hint_cancel_save;
        last_epoch = ui_epoch;
    }

    uint16_t colH = (sel==SEL_HOUR)   ? COLOR_GREEN : COLOR_WHITE;
    uint16_t colM = (sel==SEL_MINUTE) ? COLOR_GREEN : COLOR_WHITE;
    if (h != prev_h || sel != prev_sel) {
        fill_rect(HOUR_X, Y0, HOUR_W, FH, COLOR_BLACK);
        char hh[3] = { (char)('0'+(h/10)), (char)('0'+(h%10)), 0 };
        draw_string(HOUR_X, Y0, hh, colH);
        prev_h = h;
    }
    if (m != prev_m || sel != prev_sel) {
        fill_rect(MIN_X, Y0, MIN_W, FH, COLOR_BLACK);
        char mm[3] = { (char)('0'+(m/10)), (char)('0'+(m%10)), 0 };
        draw_string(MIN_X, Y0, mm, colM);
        prev_m = m;
    }
    prev_sel = sel;
}

void ui_draw_list_content(const char *title) {
    static uint32_t last_epoch = (uint32_t)-1;
    static int prev_idx = -1;
    static int prev_base = -1;
    static int prev_count = -1;            
    int base = (pick_index/6)*6;
    if (last_epoch != ui_epoch || base != prev_base || prev_count != num_reminders) {
        fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
        draw_line_text(4, title, COLOR_GREEN);
        xSemaphoreTake(reminders_mutex, portMAX_DELAY);
        for (int i=0; i<6 && (base+i)<num_reminders; i++) {
            char line[20];
            const char* name = reminders[base+i].content;
            snprintf(line, sizeof(line), "%c %.16s", (base+i)==pick_index?'>':' ', name);
            draw_line_text(20 + i*12, line, ((base+i)==pick_index)? COLOR_YELLOW : COLOR_WHITE);
        }
        xSemaphoreGive(reminders_mutex);
        draw_line_text(100, "OK:CHON  NEXT:LEN", COLOR_BLUE);
        draw_line_text(112, "BACK:XUONG  CANCEL:THOAT", COLOR_BLUE);
        prev_idx   = pick_index;
        prev_base  = base;
        prev_count = num_reminders;        
        last_epoch = ui_epoch;
        return;
    }
    if (prev_idx != pick_index) {
        int old_row = prev_idx - base, new_row = pick_index - base;
        if (old_row>=0 && old_row<6) {
            char line[20];
            xSemaphoreTake(reminders_mutex, portMAX_DELAY);
            snprintf(line, sizeof(line), "  %.16s", reminders[prev_idx].content);
            xSemaphoreGive(reminders_mutex);
            draw_line_text(20 + old_row*12, line, COLOR_WHITE);
        }
        if (new_row>=0 && new_row<6) {
            char line[20];
            xSemaphoreTake(reminders_mutex, portMAX_DELAY);
            snprintf(line, sizeof(line), "> %.16s", reminders[pick_index].content);
            xSemaphoreGive(reminders_mutex);
            draw_line_text(20 + new_row*12, line, COLOR_YELLOW);
        }
        prev_idx = pick_index;
    }
}

void ui_draw_preset_list(const char *title) {
    static uint32_t last_epoch = (uint32_t)-1;
    static int prev_idx = -1;
    static int prev_base = -1;
    int base = (preset_index/6)*6;
    if (last_epoch != ui_epoch || base != prev_base) {
        fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
        draw_line_text(4, title, COLOR_GREEN);
        for (int i=0; i<6 && (base+i)<NUM_CONTENT_PRESETS; i++) {
            char line[22];
            snprintf(line, sizeof(line), "%c %.16s", (base+i)==preset_index?'>':' ', CONTENT_PRESETS[base+i]);
            draw_line_text(20 + i*12, line, ((base+i)==preset_index)? COLOR_YELLOW : COLOR_WHITE);
        }
        draw_line_text(100, "OK:CHON  NEXT:LEN", COLOR_BLUE);
        draw_line_text(112, "BACK:XUONG  CANCEL:THOAT", COLOR_BLUE);
        prev_idx = preset_index;
        prev_base = base;
        last_epoch = ui_epoch;
        return;
    }
    if (prev_idx != preset_index) {
        int old_row = prev_idx - base, new_row = preset_index - base;
        if (old_row>=0 && old_row<6) {
            char line[22]; snprintf(line, sizeof(line), "  %.16s", CONTENT_PRESETS[prev_idx]);
            draw_line_text(20 + old_row*12, line, COLOR_WHITE);
        }
        if (new_row>=0 && new_row<6) {
            char line[22]; snprintf(line, sizeof(line), "> %.16s", CONTENT_PRESETS[preset_index]);
            draw_line_text(20 + new_row*12, line, COLOR_YELLOW);
        }
        prev_idx = preset_index;
    }
}

void ui_draw_view_detail(void) {
    static uint32_t last_epoch = (uint32_t)-1;
    static int      last_idx   = -1;
    if (last_epoch != ui_epoch || last_idx != pick_index) {
        fill_rect(0, 0, TFT_WIDTH, TFT_HEIGHT, COLOR_BLACK);
        draw_line_text(4, "CHI TIET", COLOR_GREEN);
        xSemaphoreTake(reminders_mutex, portMAX_DELAY);
        Reminder r = reminders[pick_index];
        xSemaphoreGive(reminders_mutex);
        draw_line_text(24, "NGAY:", COLOR_YELLOW);
        draw_string(60, 24, r.date, COLOR_WHITE);
        char hhmm[6]; fmt_time(r.hour, r.minute, hhmm);
        draw_line_text(36, "GIO:", COLOR_YELLOW);
        draw_string(60, 36, hhmm, COLOR_WHITE);
        draw_line_text(56, "NOI DUNG:", COLOR_YELLOW);
        char line[22]; 
        snprintf(line, sizeof(line), "%.20s", r.content);
        draw_string(4, 68, line, COLOR_WHITE);
        draw_line_text(100, "OK/CANCEL:QUAY LAI", COLOR_BLUE);
        last_idx   = pick_index;
        last_epoch = ui_epoch;
    }
}

void ui_draw_edit_submenu(void) {
    static uint32_t last_epoch = (uint32_t)-1;
    static int prev = -1;
    if (last_epoch != ui_epoch) {
        fill_rect(0,0,TFT_WIDTH,TFT_HEIGHT,COLOR_BLACK);
        draw_line_text(4, "CHON TAC VU", COLOR_GREEN);
        draw_line_text(24, (submenu_index==0)? "> CHINH NOI DUNG":"  CHINH NOI DUNG",
                       (submenu_index==0)? COLOR_YELLOW:COLOR_WHITE);
        draw_line_text(36, (submenu_index==1)? "> CHINH NGAY":"  CHINH NGAY",
                       (submenu_index==1)? COLOR_YELLOW:COLOR_WHITE);
        draw_line_text(48, (submenu_index==2)? "> CHINH GIO":"  CHINH GIO",
                       (submenu_index==2)? COLOR_YELLOW:COLOR_WHITE);
        draw_line_text(100,"OK:CHON  BACK/NEXT:DI CHUYEN", COLOR_BLUE);
        draw_line_text(112,"CANCEL:QUAY LAI", COLOR_BLUE);
        prev = submenu_index; last_epoch = ui_epoch; return;
    }
    if (prev!=submenu_index) {
        draw_line_text(24, (submenu_index==0)? "> CHINH NOI DUNG":"  CHINH NOI DUNG",
                       (submenu_index==0)? COLOR_YELLOW:COLOR_WHITE);
        draw_line_text(36, (submenu_index==1)? "> CHINH NGAY":"  CHINH NGAY",
                       (submenu_index==1)? COLOR_YELLOW:COLOR_WHITE);
        draw_line_text(48, (submenu_index==2)? "> CHINH GIO":"  CHINH GIO",
                       (submenu_index==2)? COLOR_YELLOW:COLOR_WHITE);
        prev=submenu_index;
    }
}

void ui_draw_date_editor(const char *title, int day, int month, TwoSel sel) {
    const int FW = FONT_W, FH = FONT_H;
    const int X0 = (TFT_WIDTH - 5*FW)/2; 
    const int Y0 = 60;
    static uint32_t last_epoch = (uint32_t)-1;
    static int pd=-1, pm=-1; static TwoSel psel=SEL_LEFT;
    if (last_epoch != ui_epoch) {
        fill_rect(0,0,TFT_WIDTH,TFT_HEIGHT,COLOR_BLACK);
        draw_line_text(4, title, COLOR_GREEN);
        draw_line_text(96,"NEXT/BACK:+/-", COLOR_BLUE);
        draw_line_text(108,"OK:LUU TRUONG", COLOR_BLUE);
        draw_line_text(120,"CANCEL:LUU & THOAT", COLOR_BLUE);
        draw_string(X0 + 2*FW, Y0, "/", COLOR_WHITE);
        pd=-1; pm=-1; psel=sel; last_epoch=ui_epoch;
    }
    uint16_t cD = (sel==SEL_LEFT)? COLOR_YELLOW:COLOR_WHITE;
    uint16_t cM = (sel==SEL_RIGHT)?COLOR_YELLOW:COLOR_WHITE;

    if (day!=pd || sel!=psel) {
        char dd[3]={ '0'+(day/10),'0'+(day%10),0 };
        fill_rect(X0, Y0, 2*FW, FH, COLOR_BLACK);
        draw_string(X0, Y0, dd, cD);
        pd=day;
    }
    if (month!=pm || sel!=psel) {
        char mm[3]={ '0'+(month/10),'0'+(month%10),0 };
        int mx = X0 + 3*FW;
        fill_rect(mx, Y0, 2*FW, FH, COLOR_BLACK);
        draw_string(mx, Y0, mm, cM);
        pm=month;
    }
    psel=sel;
} 