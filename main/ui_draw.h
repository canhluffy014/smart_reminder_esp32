#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <strings.h>
#include <time.h>
#include "display.h"
#include "reminders_store.h"
#include "time_utils.h"

typedef enum {
    UI_IDLE = 0,
    UI_MENU,
    UI_VIEW_LIST,      
    UI_EDIT_PICK,      
    UI_EDIT_SUBMENU,   
    UI_EDIT_CONTENT,   
    UI_EDIT_DATE,      
    UI_EDIT_TIME,      
    UI_ADD_CONTENT,    
    UI_ADD_DATE,       
    UI_ADD_TIME,       
    UI_DEL_PICK,        
    UI_VIEW_DETAIL
} UiState;

typedef enum { SEL_LEFT=0, SEL_RIGHT=1 } TwoSel;

typedef enum { SEL_HOUR = 0, SEL_MINUTE = 1 } FieldSel;

extern UiState ui_state;
extern const char* CONTENT_PRESETS[]; 
extern const int NUM_CONTENT_PRESETS;
extern int idle_x, idle_y;
extern uint32_t ui_epoch;
extern int preset_index; 
extern TwoSel two_sel;
extern int menu_index; 
extern int pick_index;    
extern FieldSel field_sel;
extern int submenu_index;

void show_alarm_feedback(const char *msg, uint16_t color);
void idle_draw_upcoming(const struct tm* now_local);
void draw_idle_screen_now(void);
void idle_clock_screen_init(void);
void ui_draw_menu(void);
void ui_draw_pick_list(const char *title);
void ui_draw_time_editor(const char *title, int h, int m, FieldSel sel, bool show_hint_cancel_save);
void ui_draw_list_content(const char *title);
void ui_draw_preset_list(const char *title);
void ui_draw_view_detail(void);
void ui_draw_edit_submenu(void);
void ui_draw_date_editor(const char *title, int day, int month, TwoSel sel);