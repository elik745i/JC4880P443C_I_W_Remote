/**
 * @file music_player_list_ui.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "music_player_list_ui.h"
#if APP_MUSIC_PLAYER_ENABLE

#include <stdint.h>

#include "lvgl_input_helper.h"
#include "music_player_main_ui.h"

#include "music_player_ui.h"
#include "music_library.h"

/*********************
 *      DEFINES
 *********************/
#define PLAYLIST_ACTION_BUTTON_SIZE    42
#define PLAYLIST_MODAL_HEIGHT          280
#define PLAYLIST_ROW_HEIGHT            64
#define PLAYLIST_BROWSER_ROW_HEIGHT    58

#define ENCODE_BROWSER_ID(root, index) ((void *)(uintptr_t)((((uintptr_t)(root)) << 16) | ((uintptr_t)(index) & 0xFFFFU)))
#define DECODE_BROWSER_ROOT(value)     ((music_library_storage_root_t)(((uintptr_t)(value) >> 16) & 0xFFFFU))
#define DECODE_BROWSER_INDEX(value)    ((uint32_t)((uintptr_t)(value) & 0xFFFFU))

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_obj_t * add_list_btn(lv_obj_t *parent, uint32_t track_id);
static lv_obj_t * create_toolbar_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, void *user_data);
static void btn_click_event_cb(lv_event_t *e);
static void close_btn_event_cb(lv_event_t *e);
static void file_action_event_cb(lv_event_t *e);
static void folder_action_event_cb(lv_event_t *e);
static void link_action_event_cb(lv_event_t *e);
static void delete_action_event_cb(lv_event_t *e);
static void browser_row_event_cb(lv_event_t *e);
static void browser_add_event_cb(lv_event_t *e);
static void browser_up_event_cb(lv_event_t *e);
static void browser_root_tab_event_cb(lv_event_t *e);
static void browser_refresh_event_cb(lv_event_t *e);
static void browser_close_event_cb(lv_event_t *e);
static void url_close_event_cb(lv_event_t *e);
static void url_download_event_cb(lv_event_t *e);
static void delete_confirm_event_cb(lv_event_t *e);
static void url_keyboard_event_cb(lv_event_t *e);
static void download_timer_cb(lv_timer_t *t);
static void browser_add_timer_cb(lv_timer_t *t);
static void populate_list_contents(void);
static void open_browser_modal(music_library_browser_mode_t mode);
static void close_browser_modal(void);
static void refresh_browser_modal(void);
static void start_browser_refresh(bool force_reindex);
static void ensure_browser_busy_timer(void);
static void open_url_modal(void);
static void close_url_modal(void);
static void refresh_status_label(void);
static void show_browser_add_busy(const char *status);
static void hide_browser_add_busy(void);

/**********************
 *  STATIC VARIABLES
 **********************/
static lv_obj_t * list;
static lv_obj_t * playlist_list;
static lv_obj_t * status_label;
static lv_obj_t * browser_overlay;
static lv_obj_t * browser_title_label;
static lv_obj_t * browser_sd_root_button;
static lv_obj_t * browser_spiffs_root_button;
static lv_obj_t * browser_path_label;
static lv_obj_t * browser_list;
static lv_obj_t * browser_up_button;
static lv_obj_t * browser_status_label;
static lv_obj_t * browser_add_overlay;
static lv_obj_t * browser_add_status_label;
static lv_obj_t * url_overlay;
static lv_obj_t * url_status_label;
static lv_obj_t * url_textarea;
static lv_obj_t * url_keyboard;
static lv_timer_t * download_timer;
static lv_timer_t * browser_add_timer;
static bool playlist_content_populated;
static bool playlist_content_dirty;
static music_library_storage_root_t browser_active_root = MUSIC_LIBRARY_STORAGE_SD;
static const lv_font_t * font_small;
static const lv_font_t * font_medium;
static lv_style_t style_scrollbar;
static lv_style_t style_btn;
static lv_style_t style_btn_pr;
static lv_style_t style_btn_chk;
static lv_style_t style_btn_dis;
static lv_style_t style_title;
static lv_style_t style_artist;
static lv_style_t style_time;
LV_IMG_DECLARE(img_music_player_ui_btn_list_play);
LV_IMG_DECLARE(img_music_player_ui_btn_list_pause);

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

lv_obj_t * _music_player_list_ui_create(lv_obj_t * parent)
{
#if APP_MUSIC_PLAYER_LARGE
    font_small = &lv_font_montserrat_16;
    font_medium = &lv_font_montserrat_22;
#else
    font_small = &lv_font_montserrat_12;
    font_medium = &lv_font_montserrat_16;
#endif

    lv_style_init(&style_scrollbar);
    lv_style_set_width(&style_scrollbar,  4);
    lv_style_set_bg_opa(&style_scrollbar, LV_OPA_COVER);
    lv_style_set_bg_color(&style_scrollbar, lv_color_hex3(0xeee));
    lv_style_set_radius(&style_scrollbar, LV_RADIUS_CIRCLE);
    lv_style_set_pad_right(&style_scrollbar, 4);

    static const lv_coord_t grid_cols[] = {LV_GRID_CONTENT, LV_GRID_FR(1), LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
#if APP_MUSIC_PLAYER_LARGE
    static const lv_coord_t grid_rows[] = {35,  30, LV_GRID_TEMPLATE_LAST};
#else
    static const lv_coord_t grid_rows[] = {22,  17, LV_GRID_TEMPLATE_LAST};
#endif
    lv_style_init(&style_btn);
    lv_style_set_bg_opa(&style_btn, LV_OPA_TRANSP);
    lv_style_set_grid_column_dsc_array(&style_btn, grid_cols);
    lv_style_set_grid_row_dsc_array(&style_btn, grid_rows);
    lv_style_set_grid_row_align(&style_btn, LV_GRID_ALIGN_CENTER);
    lv_style_set_layout(&style_btn, LV_LAYOUT_GRID);
#if APP_MUSIC_PLAYER_LARGE
    lv_style_set_pad_right(&style_btn, 30);
#else
    lv_style_set_pad_right(&style_btn, 20);
#endif
    lv_style_init(&style_btn_pr);
    lv_style_set_bg_opa(&style_btn_pr, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn_pr,  lv_color_hex(0x4c4965));

    lv_style_init(&style_btn_chk);
    lv_style_set_bg_opa(&style_btn_chk, LV_OPA_COVER);
    lv_style_set_bg_color(&style_btn_chk, lv_color_hex(0x4c4965));

    lv_style_init(&style_btn_dis);
    lv_style_set_text_opa(&style_btn_dis, LV_OPA_40);
    lv_style_set_img_opa(&style_btn_dis, LV_OPA_40);

    lv_style_init(&style_title);
    lv_style_set_text_font(&style_title, font_medium);
    lv_style_set_text_color(&style_title, lv_color_hex(0xffffff));

    lv_style_init(&style_artist);
    lv_style_set_text_font(&style_artist, font_small);
    lv_style_set_text_color(&style_artist, lv_color_hex(0xb1b0be));

    lv_style_init(&style_time);
    lv_style_set_text_font(&style_time, font_medium);
    lv_style_set_text_color(&style_time, lv_color_hex(0xffffff));

    list = lv_obj_create(parent);
    lv_obj_set_size(list, LV_HOR_RES, LV_VER_RES - APP_MUSIC_PLAYER_HANDLE_SIZE);
    lv_obj_set_y(list, APP_MUSIC_PLAYER_HANDLE_SIZE);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x232038), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, 14, 0);
    lv_obj_set_style_pad_row(list, 10, 0);
    lv_obj_add_style(list, &style_scrollbar, LV_PART_SCROLLBAR);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    lv_obj_t * toolbar = lv_obj_create(list);
    lv_obj_set_width(toolbar, lv_pct(100));
    lv_obj_set_height(toolbar, 50);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(toolbar, 0, 0);
    lv_obj_set_style_pad_all(toolbar, 0, 0);
    lv_obj_set_style_pad_column(toolbar, 8, 0);
    lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(toolbar, LV_OBJ_FLAG_SCROLLABLE);

    create_toolbar_button(toolbar, LV_SYMBOL_FILE, file_action_event_cb, NULL);
    create_toolbar_button(toolbar, LV_SYMBOL_DIRECTORY, folder_action_event_cb, NULL);
    create_toolbar_button(toolbar, LV_SYMBOL_UPLOAD, link_action_event_cb, NULL);
    create_toolbar_button(toolbar, LV_SYMBOL_TRASH, delete_action_event_cb, NULL);
    create_toolbar_button(toolbar, LV_SYMBOL_CLOSE, close_btn_event_cb, NULL);

    lv_obj_t * separator = lv_obj_create(list);
    lv_obj_set_size(separator, lv_pct(100), 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(0x5A5775), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_60, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_radius(separator, 0, 0);
    lv_obj_clear_flag(separator, LV_OBJ_FLAG_SCROLLABLE);

    status_label = lv_label_create(list);
    lv_obj_set_width(status_label, lv_pct(100));
    lv_obj_set_style_text_font(status_label, font_small, 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xCFCBE9), 0);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);

    playlist_list = lv_obj_create(list);
    lv_obj_set_width(playlist_list, lv_pct(100));
    lv_obj_set_height(playlist_list, LV_VER_RES - APP_MUSIC_PLAYER_HANDLE_SIZE - 110);
    lv_obj_set_style_bg_color(playlist_list, lv_color_hex(0x1B182D), 0);
    lv_obj_set_style_bg_opa(playlist_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(playlist_list, 0, 0);
    lv_obj_set_style_radius(playlist_list, 22, 0);
    lv_obj_set_style_pad_all(playlist_list, 8, 0);
    lv_obj_set_style_pad_row(playlist_list, 8, 0);
    lv_obj_set_flex_flow(playlist_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(playlist_list, LV_SCROLLBAR_MODE_OFF);

    browser_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(browser_overlay);
    lv_obj_set_pos(browser_overlay, 0, APP_MUSIC_PLAYER_HANDLE_SIZE);
    lv_obj_set_size(browser_overlay, LV_HOR_RES, LV_VER_RES - APP_MUSIC_PLAYER_HANDLE_SIZE);
    lv_obj_set_style_bg_opa(browser_overlay, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(browser_overlay, lv_color_hex(0x0C0B16), 0);
    lv_obj_add_flag(browser_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(browser_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * browser_panel = lv_obj_create(browser_overlay);
    lv_obj_set_size(browser_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_align(browser_panel, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(browser_panel, lv_color_hex(0x232038), 0);
    lv_obj_set_style_bg_opa(browser_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(browser_panel, 0, 0);
    lv_obj_set_style_radius(browser_panel, 0, 0);
    lv_obj_set_style_pad_all(browser_panel, 12, 0);
    lv_obj_set_style_pad_row(browser_panel, 10, 0);
    lv_obj_set_flex_flow(browser_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(browser_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * browser_header = lv_obj_create(browser_panel);
    lv_obj_set_width(browser_header, lv_pct(100));
    lv_obj_set_height(browser_header, 40);
    lv_obj_set_style_bg_opa(browser_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(browser_header, 0, 0);
    lv_obj_set_style_pad_all(browser_header, 0, 0);
    lv_obj_set_flex_flow(browser_header, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(browser_header, 8, 0);
    lv_obj_clear_flag(browser_header, LV_OBJ_FLAG_SCROLLABLE);

    browser_title_label = lv_label_create(browser_header);
    lv_label_set_text(browser_title_label, "Add To Playlist");
    lv_obj_set_style_text_font(browser_title_label, font_medium, 0);
    lv_obj_set_style_text_color(browser_title_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_width(browser_title_label, 260);

    lv_obj_t * browser_close = lv_btn_create(browser_header);
    lv_obj_set_size(browser_close, 42, 34);
    lv_obj_set_style_radius(browser_close, 16, 0);
    lv_obj_set_style_bg_color(browser_close, lv_color_hex(0x49456A), 0);
    lv_obj_set_style_border_width(browser_close, 0, 0);
    lv_obj_add_event_cb(browser_close, browser_close_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(browser_close, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(browser_close, LV_OBJ_FLAG_FLOATING);
    lv_obj_t * browser_close_label = lv_label_create(browser_close);
    lv_label_set_text(browser_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(browser_close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(browser_close_label);

    lv_obj_t * browser_tabs = lv_obj_create(browser_panel);
    lv_obj_set_width(browser_tabs, lv_pct(100));
    lv_obj_set_height(browser_tabs, 44);
    lv_obj_set_style_bg_opa(browser_tabs, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(browser_tabs, 0, 0);
    lv_obj_set_style_pad_all(browser_tabs, 0, 0);
    lv_obj_set_style_pad_column(browser_tabs, 10, 0);
    lv_obj_set_flex_flow(browser_tabs, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(browser_tabs, LV_OBJ_FLAG_SCROLLABLE);

    browser_sd_root_button = lv_btn_create(browser_tabs);
    lv_obj_set_size(browser_sd_root_button, 126, 40);
    lv_obj_set_style_radius(browser_sd_root_button, 18, 0);
    lv_obj_set_style_border_width(browser_sd_root_button, 0, 0);
    lv_obj_add_event_cb(browser_sd_root_button, browser_root_tab_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)MUSIC_LIBRARY_STORAGE_SD);
    lv_obj_t * browser_sd_root_label = lv_label_create(browser_sd_root_button);
    lv_label_set_text(browser_sd_root_label, LV_SYMBOL_DRIVE " SD Card");
    lv_obj_set_style_text_color(browser_sd_root_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(browser_sd_root_label);

    browser_spiffs_root_button = lv_btn_create(browser_tabs);
    lv_obj_set_size(browser_spiffs_root_button, 126, 40);
    lv_obj_set_style_radius(browser_spiffs_root_button, 18, 0);
    lv_obj_set_style_border_width(browser_spiffs_root_button, 0, 0);
    lv_obj_add_event_cb(browser_spiffs_root_button, browser_root_tab_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)MUSIC_LIBRARY_STORAGE_SPIFFS);
    lv_obj_t * browser_spiffs_root_label = lv_label_create(browser_spiffs_root_button);
    lv_label_set_text(browser_spiffs_root_label, LV_SYMBOL_HOME " SPIFFS");
    lv_obj_set_style_text_color(browser_spiffs_root_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(browser_spiffs_root_label);

    lv_obj_t * browser_path_row = lv_obj_create(browser_panel);
    lv_obj_set_width(browser_path_row, lv_pct(100));
    lv_obj_set_height(browser_path_row, 42);
    lv_obj_set_style_bg_opa(browser_path_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(browser_path_row, 0, 0);
    lv_obj_set_style_pad_all(browser_path_row, 0, 0);
    lv_obj_set_style_pad_column(browser_path_row, 8, 0);
    lv_obj_set_flex_flow(browser_path_row, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(browser_path_row, LV_OBJ_FLAG_SCROLLABLE);

    browser_up_button = lv_btn_create(browser_path_row);
    lv_obj_set_size(browser_up_button, 40, 34);
    lv_obj_set_style_radius(browser_up_button, 16, 0);
    lv_obj_set_style_bg_color(browser_up_button, lv_color_hex(0x49456A), 0);
    lv_obj_set_style_border_width(browser_up_button, 0, 0);
    lv_obj_add_event_cb(browser_up_button, browser_up_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * browser_up_label = lv_label_create(browser_up_button);
    lv_label_set_text(browser_up_label, LV_SYMBOL_UP);
    lv_obj_set_style_text_color(browser_up_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(browser_up_label);

    lv_obj_t * browser_refresh_button = lv_btn_create(browser_path_row);
    lv_obj_set_size(browser_refresh_button, 40, 34);
    lv_obj_set_style_radius(browser_refresh_button, 16, 0);
    lv_obj_set_style_bg_color(browser_refresh_button, lv_color_hex(0x49456A), 0);
    lv_obj_set_style_border_width(browser_refresh_button, 0, 0);
    lv_obj_add_event_cb(browser_refresh_button, browser_refresh_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * browser_refresh_label = lv_label_create(browser_refresh_button);
    lv_label_set_text(browser_refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(browser_refresh_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(browser_refresh_label);

    browser_path_label = lv_label_create(browser_path_row);
    lv_obj_set_width(browser_path_label, LV_HOR_RES - 140);
    lv_obj_set_style_text_color(browser_path_label, lv_color_hex(0xCEC9E8), 0);
    lv_obj_set_style_text_font(browser_path_label, font_small, 0);
    lv_label_set_long_mode(browser_path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    browser_list = lv_obj_create(browser_panel);
    lv_obj_set_width(browser_list, lv_pct(100));
    lv_obj_set_style_bg_color(browser_list, lv_color_hex(0x18152A), 0);
    lv_obj_set_style_bg_opa(browser_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(browser_list, 0, 0);
    lv_obj_set_style_radius(browser_list, 18, 0);
    lv_obj_set_style_pad_all(browser_list, 8, 0);
    lv_obj_set_style_pad_row(browser_list, 8, 0);
    lv_obj_set_flex_flow(browser_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(browser_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_grow(browser_list, 1);

    browser_status_label = lv_label_create(browser_panel);
    lv_obj_set_width(browser_status_label, lv_pct(100));
    lv_obj_set_style_text_color(browser_status_label, lv_color_hex(0xCEC9E8), 0);
    lv_obj_set_style_text_font(browser_status_label, font_small, 0);
    lv_label_set_long_mode(browser_status_label, LV_LABEL_LONG_WRAP);

    browser_add_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(browser_add_overlay);
    lv_obj_set_size(browser_add_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(browser_add_overlay, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(browser_add_overlay, lv_color_hex(0x0C0B16), 0);
    lv_obj_add_flag(browser_add_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(browser_add_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * browser_add_panel = lv_obj_create(browser_add_overlay);
    lv_obj_set_size(browser_add_panel, 230, 150);
    lv_obj_center(browser_add_panel);
    lv_obj_set_style_bg_color(browser_add_panel, lv_color_hex(0x232038), 0);
    lv_obj_set_style_bg_opa(browser_add_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(browser_add_panel, 0, 0);
    lv_obj_set_style_radius(browser_add_panel, 22, 0);
    lv_obj_set_style_pad_all(browser_add_panel, 16, 0);
    lv_obj_set_style_pad_row(browser_add_panel, 10, 0);
    lv_obj_set_flex_flow(browser_add_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(browser_add_panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(browser_add_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * browser_add_spinner = lv_spinner_create(browser_add_panel, 1000, 90);
    lv_obj_set_size(browser_add_spinner, 56, 56);
    lv_obj_set_style_arc_width(browser_add_spinner, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(browser_add_spinner, lv_color_hex(0x49456A), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(browser_add_spinner, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(browser_add_spinner, lv_color_hex(0x5E72EB), LV_PART_INDICATOR | LV_STATE_DEFAULT);

    browser_add_status_label = lv_label_create(browser_add_panel);
    lv_obj_set_width(browser_add_status_label, lv_pct(100));
    lv_obj_set_style_text_font(browser_add_status_label, font_small, 0);
    lv_obj_set_style_text_color(browser_add_status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(browser_add_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(browser_add_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(browser_add_status_label, "Adding to playlist...");

    url_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(url_overlay);
    lv_obj_set_size(url_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(url_overlay, LV_OPA_70, 0);
    lv_obj_set_style_bg_color(url_overlay, lv_color_hex(0x0C0B16), 0);
    lv_obj_add_flag(url_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(url_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * url_panel = lv_obj_create(url_overlay);
    lv_obj_set_size(url_panel, LV_PCT(100), PLAYLIST_MODAL_HEIGHT + 36);
    lv_obj_align(url_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(url_panel, lv_color_hex(0x232038), 0);
    lv_obj_set_style_bg_opa(url_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(url_panel, 0, 0);
    lv_obj_set_style_radius(url_panel, 22, 0);
    lv_obj_set_style_pad_all(url_panel, 12, 0);
    lv_obj_set_style_pad_row(url_panel, 8, 0);
    lv_obj_set_flex_flow(url_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(url_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * url_header = lv_obj_create(url_panel);
    lv_obj_set_width(url_header, lv_pct(100));
    lv_obj_set_height(url_header, 34);
    lv_obj_set_style_bg_opa(url_header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(url_header, 0, 0);
    lv_obj_set_style_pad_all(url_header, 0, 0);
    lv_obj_set_flex_flow(url_header, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(url_header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t * url_title = lv_label_create(url_header);
    lv_label_set_text(url_title, "Download To Playlist");
    lv_obj_set_style_text_font(url_title, font_medium, 0);
    lv_obj_set_style_text_color(url_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_t * url_close = lv_btn_create(url_header);
    lv_obj_set_size(url_close, 42, 32);
    lv_obj_set_style_radius(url_close, 16, 0);
    lv_obj_set_style_bg_color(url_close, lv_color_hex(0x49456A), 0);
    lv_obj_set_style_border_width(url_close, 0, 0);
    lv_obj_add_event_cb(url_close, url_close_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(url_close, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(url_close, LV_OBJ_FLAG_FLOATING);
    lv_obj_t * url_close_label = lv_label_create(url_close);
    lv_label_set_text(url_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(url_close_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(url_close_label);

    url_textarea = lv_textarea_create(url_panel);
    lv_obj_set_width(url_textarea, lv_pct(100));
    lv_obj_set_height(url_textarea, 52);
    lv_textarea_set_one_line(url_textarea, true);
    lv_textarea_set_placeholder_text(url_textarea, "https://example.com/song.mp3");

    lv_obj_t * url_download_button = lv_btn_create(url_panel);
    lv_obj_set_width(url_download_button, lv_pct(100));
    lv_obj_set_height(url_download_button, 42);
    lv_obj_set_style_radius(url_download_button, 18, 0);
    lv_obj_set_style_bg_color(url_download_button, lv_color_hex(0x5E72EB), 0);
    lv_obj_set_style_border_width(url_download_button, 0, 0);
    lv_obj_add_event_cb(url_download_button, url_download_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * url_download_label = lv_label_create(url_download_button);
    lv_label_set_text(url_download_label, LV_SYMBOL_DOWNLOAD " Download And Add");
    lv_obj_set_style_text_color(url_download_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(url_download_label);

    url_status_label = lv_label_create(url_panel);
    lv_obj_set_width(url_status_label, lv_pct(100));
    lv_obj_set_style_text_color(url_status_label, lv_color_hex(0xCEC9E8), 0);
    lv_obj_set_style_text_font(url_status_label, font_small, 0);
    lv_label_set_long_mode(url_status_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(url_status_label, "The file will be downloaded to /sdcard/Downloads/Music.");

    url_keyboard = lv_keyboard_create(url_panel);
    lv_obj_set_width(url_keyboard, lv_pct(100));
    lv_obj_set_height(url_keyboard, 150);
    lv_keyboard_set_textarea(url_keyboard, url_textarea);
    jc4880_keyboard_install_case_behavior(url_keyboard);
    lv_obj_add_event_cb(url_keyboard, url_keyboard_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(url_keyboard, url_keyboard_event_cb, LV_EVENT_CANCEL, NULL);

    playlist_content_populated = false;
    playlist_content_dirty = true;
    refresh_status_label();

    return list;
}

void _music_player_list_ui_reload(void)
{
    if ((list == NULL) || (playlist_list == NULL)) {
        return;
    }

    lv_obj_clean(playlist_list);
    populate_list_contents();
    refresh_status_label();
    playlist_content_populated = true;
    playlist_content_dirty = false;
}

void _music_player_list_ui_open_add_file_browser(void)
{
    if (browser_overlay == NULL) {
        return;
    }

    open_browser_modal(MUSIC_LIBRARY_BROWSER_MODE_FILE);
}

void _music_player_list_ui_open_download_modal(void)
{
    if (url_overlay == NULL) {
        return;
    }

    open_url_modal();
}

void _music_player_list_ui_close(void)
{
    if (download_timer != NULL) {
        lv_timer_del(download_timer);
        download_timer = NULL;
    }
    if (browser_add_timer != NULL) {
        lv_timer_del(browser_add_timer);
        browser_add_timer = NULL;
    }
    if ((browser_overlay != NULL) && lv_obj_is_valid(browser_overlay)) {
        lv_obj_del(browser_overlay);
    }
    if ((browser_add_overlay != NULL) && lv_obj_is_valid(browser_add_overlay)) {
        lv_obj_del(browser_add_overlay);
    }
    if ((url_overlay != NULL) && lv_obj_is_valid(url_overlay)) {
        lv_obj_del(url_overlay);
    }

    list = NULL;
    playlist_list = NULL;
    status_label = NULL;
    browser_overlay = NULL;
    browser_title_label = NULL;
    browser_sd_root_button = NULL;
    browser_spiffs_root_button = NULL;
    browser_path_label = NULL;
    browser_list = NULL;
    browser_up_button = NULL;
    browser_status_label = NULL;
    browser_add_overlay = NULL;
    browser_add_status_label = NULL;
    url_overlay = NULL;
    url_status_label = NULL;
    url_textarea = NULL;
    url_keyboard = NULL;
    playlist_content_populated = false;
    playlist_content_dirty = true;

    lv_style_reset(&style_scrollbar);
    lv_style_reset(&style_btn);
    lv_style_reset(&style_btn_pr);
    lv_style_reset(&style_btn_chk);
    lv_style_reset(&style_btn_dis);
    lv_style_reset(&style_title);
    lv_style_reset(&style_artist);
    lv_style_reset(&style_time);
}

void _music_player_list_ui_btn_check(uint32_t track_id, bool state)
{
    if ((playlist_list == NULL) || (_music_player_ui_get_track_count() == 0)) {
        return;
    }

    lv_obj_t * btn = lv_obj_get_child(playlist_list, track_id);
    if (btn == NULL) {
        return;
    }

    lv_obj_t * icon = lv_obj_get_child(btn, 0);
    if (icon == NULL) {
        return;
    }

    if (state) {
        lv_obj_add_state(btn, LV_STATE_CHECKED);
        lv_img_set_src(icon, &img_music_player_ui_btn_list_pause);
        lv_obj_scroll_to_view(btn, LV_ANIM_ON);
    } else {
        lv_obj_clear_state(btn, LV_STATE_CHECKED);
        lv_img_set_src(icon, &img_music_player_ui_btn_list_play);
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

static lv_obj_t * create_toolbar_button(lv_obj_t *parent, const char *symbol, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t * button = lv_btn_create(parent);
    lv_obj_set_size(button, PLAYLIST_ACTION_BUTTON_SIZE, PLAYLIST_ACTION_BUTTON_SIZE);
    lv_obj_set_style_radius(button, 16, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x4F4A75), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_add_event_cb(button, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t * label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(label);
    return button;
}

static lv_obj_t * add_list_btn(lv_obj_t * parent, uint32_t track_id)
{
    uint32_t t = _music_player_ui_get_track_length(track_id);
    char time[32];
    lv_snprintf(time, sizeof(time), "%"LV_PRIu32":%02"LV_PRIu32, t / 60, t % 60);
    const char * title = _music_player_ui_get_title(track_id);
    const char * artist = _music_player_ui_get_artist(track_id);

    lv_obj_t * btn = lv_obj_create(parent);
    lv_obj_set_size(btn, lv_pct(100), PLAYLIST_ROW_HEIGHT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2640), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_set_style_pad_right(btn, 12, 0);
    lv_obj_set_style_pad_top(btn, 10, 0);
    lv_obj_set_style_pad_bottom(btn, 10, 0);

    lv_obj_add_style(btn, &style_btn, 0);
    lv_obj_add_style(btn, &style_btn_pr, LV_STATE_PRESSED);
    lv_obj_add_style(btn, &style_btn_chk, LV_STATE_CHECKED);
    lv_obj_add_style(btn, &style_btn_dis, LV_STATE_DISABLED);
    lv_obj_add_event_cb(btn, btn_click_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * icon = lv_img_create(btn);
    lv_img_set_src(icon, &img_music_player_ui_btn_list_play);
    lv_obj_set_grid_cell(icon, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, 0, 2);

    lv_obj_t * title_label = lv_label_create(btn);
    lv_label_set_text(title_label, title);
    lv_obj_set_grid_cell(title_label, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_add_style(title_label, &style_title, 0);
    lv_obj_set_width(title_label, lv_pct(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t * artist_label = lv_label_create(btn);
    lv_label_set_text(artist_label, artist);
    lv_obj_add_style(artist_label, &style_artist, 0);
    lv_obj_set_grid_cell(artist_label, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, 1, 1);
    lv_obj_set_width(artist_label, lv_pct(100));
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    lv_obj_t * time_label = lv_label_create(btn);
    lv_label_set_text(time_label, time);
    lv_obj_add_style(time_label, &style_time, 0);
    lv_obj_set_grid_cell(time_label, LV_GRID_ALIGN_END, 2, 1, LV_GRID_ALIGN_CENTER, 0, 2);

    return btn;
}

static void populate_list_contents(void)
{
    uint32_t track_id;
    for (track_id = 0; track_id < _music_player_ui_get_track_count(); track_id++) {
        add_list_btn(playlist_list, track_id);
    }

    if (_music_player_ui_get_track_count() == 0) {
        lv_obj_t *emptyLabel = lv_label_create(playlist_list);
        lv_label_set_text(emptyLabel, "Playlist is empty");
        lv_obj_add_style(emptyLabel, &style_title, 0);
        lv_obj_set_style_text_align(emptyLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(emptyLabel, lv_pct(100));

        lv_obj_t *hintLabel = lv_label_create(playlist_list);
        lv_label_set_text(hintLabel, "Use the top row to add a single file, a folder, or a download URL.");
        lv_obj_add_style(hintLabel, &style_artist, 0);
        lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(hintLabel, lv_pct(100));
    }

    if (_music_player_ui_get_track_count() > 0) {
        _music_player_list_ui_btn_check(0, false);
    }
}

static void btn_click_event_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);

    uint32_t idx = lv_obj_get_child_id(btn);
    _music_player_ui_play(idx);
}

static void close_btn_event_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    _music_player_main_ui_sync_state();
    _music_player_ui_close_browser();
}

static void file_action_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    open_browser_modal(MUSIC_LIBRARY_BROWSER_MODE_FILE);
}

static void folder_action_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    open_browser_modal(MUSIC_LIBRARY_BROWSER_MODE_FOLDER);
}

static void link_action_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    open_url_modal();
}

static void delete_action_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    static const char * buttons[] = {"Delete", "Cancel", ""};
    lv_obj_t * msgbox = lv_msgbox_create(NULL, "Delete Playlist", "Delete the entire playlist?", buttons, false);
    lv_obj_center(msgbox);
    lv_obj_add_event_cb(msgbox, delete_confirm_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void delete_confirm_event_cb(lv_event_t *e)
{
    lv_obj_t * msgbox = lv_event_get_current_target(e);
    const char * button = lv_msgbox_get_active_btn_text(msgbox);
    if ((button != NULL) && (strcmp(button, "Delete") == 0)) {
        _music_player_ui_pause();
        music_library_playlist_clear();
        playlist_content_dirty = true;
        _music_player_list_ui_reload();
        _music_player_main_ui_sync_state();
    }
    lv_msgbox_close_async(msgbox);
}

static void open_browser_modal(music_library_browser_mode_t mode)
{
    music_library_browser_open(mode);
    browser_active_root = MUSIC_LIBRARY_STORAGE_SD;
    if (browser_title_label != NULL) {
        lv_label_set_text(browser_title_label,
                          (mode == MUSIC_LIBRARY_BROWSER_MODE_FILE) ? "Add File To Playlist" : "Add Folder To Playlist");
    }
    refresh_browser_modal();
    lv_obj_clear_flag(browser_overlay, LV_OBJ_FLAG_HIDDEN);
    start_browser_refresh(false);
}

static void close_browser_modal(void)
{
    lv_obj_add_flag(browser_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void browser_close_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    close_browser_modal();
}

static void ensure_browser_busy_timer(void)
{
    if (browser_add_timer == NULL) {
        browser_add_timer = lv_timer_create(browser_add_timer_cb, 120, NULL);
    }
}

static void start_browser_refresh(bool force_reindex)
{
    const bool started = force_reindex
                             ? music_library_browser_reindex_current_async(browser_active_root)
                             : music_library_browser_refresh_async(browser_active_root);
    if (started) {
        show_browser_add_busy(music_library_browser_refresh_get_status());
        ensure_browser_busy_timer();
    } else if (browser_status_label != NULL) {
        lv_label_set_text(browser_status_label, music_library_browser_refresh_get_status());
    }
}

static void refresh_browser_modal(void)
{
    if ((browser_list == NULL) || (browser_path_label == NULL) || (browser_status_label == NULL)) {
        return;
    }

    lv_obj_set_style_bg_color(browser_sd_root_button,
                              (browser_active_root == MUSIC_LIBRARY_STORAGE_SD) ? lv_color_hex(0x5E72EB) : lv_color_hex(0x49456A),
                              0);
    lv_obj_set_style_bg_color(browser_spiffs_root_button,
                              (browser_active_root == MUSIC_LIBRARY_STORAGE_SPIFFS) ? lv_color_hex(0x5E72EB) : lv_color_hex(0x49456A),
                              0);

    lv_obj_clean(browser_list);
    lv_label_set_text(browser_path_label, music_library_browser_get_path(browser_active_root));

    const bool at_root = strcmp(music_library_browser_get_path(browser_active_root),
                                (browser_active_root == MUSIC_LIBRARY_STORAGE_SD) ? "/sdcard" : CONFIG_BSP_SPIFFS_MOUNT_POINT) == 0;
    if (at_root) {
        lv_obj_add_state(browser_up_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(browser_up_button, LV_STATE_DISABLED);
    }

    const uint32_t count = music_library_browser_get_count(browser_active_root);
    if (count == 0) {
        lv_obj_t * label = lv_label_create(browser_list);
        if (music_library_browser_refresh_get_state() == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING) {
            lv_label_set_text(label, "Loading items...");
        } else {
            lv_label_set_text(label,
                              music_library_browser_root_available(browser_active_root) ? "No supported items here"
                                                                                    : "Storage unavailable");
        }
        lv_obj_set_style_text_color(label, lv_color_hex(0xC7C2E8), 0);
        lv_obj_set_width(label, lv_pct(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    } else {
        for (uint32_t index = 0; index < count; ++index) {
            lv_obj_t * row = lv_btn_create(browser_list);
            lv_obj_set_width(row, lv_pct(100));
            lv_obj_set_height(row, PLAYLIST_BROWSER_ROW_HEIGHT);
            lv_obj_set_style_radius(row, 16, 0);
            lv_obj_set_style_bg_color(row, lv_color_hex(0x2C2844), 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_left(row, 10, 0);
            lv_obj_set_style_pad_right(row, 8, 0);
            lv_obj_set_style_pad_top(row, 8, 0);
            lv_obj_set_style_pad_bottom(row, 8, 0);
            lv_obj_set_style_pad_column(row, 8, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(row, browser_row_event_cb, LV_EVENT_CLICKED, ENCODE_BROWSER_ID(browser_active_root, index));

            lv_obj_t * icon = lv_label_create(row);
            lv_label_set_text(icon,
                              music_library_browser_entry_is_directory(browser_active_root, index) ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE);
            lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_width(icon, 20);

            lv_obj_t * text_col = lv_obj_create(row);
            lv_obj_remove_style_all(text_col);
            lv_obj_set_width(text_col, LV_PCT(100));
            lv_obj_set_flex_grow(text_col, 1);
            lv_obj_set_flex_flow(text_col, LV_FLEX_FLOW_COLUMN);
            lv_obj_clear_flag(text_col, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t * name = lv_label_create(text_col);
            lv_label_set_text(name, music_library_browser_get_name(browser_active_root, index));
            lv_obj_set_width(name, lv_pct(100));
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFF), 0);

            lv_obj_t * meta = lv_label_create(text_col);
            lv_label_set_text(meta, music_library_browser_get_meta(browser_active_root, index));
            lv_obj_set_width(meta, lv_pct(100));
            lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
            lv_obj_set_style_text_font(meta, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(meta, lv_color_hex(0xB8B3D8), 0);

            if (music_library_browser_entry_can_add(browser_active_root, index)) {
                lv_obj_t * add_button = lv_btn_create(row);
                lv_obj_set_size(add_button, 34, 30);
                lv_obj_set_style_radius(add_button, 12, 0);
                lv_obj_set_style_bg_color(add_button, lv_color_hex(0x5E72EB), 0);
                lv_obj_set_style_border_width(add_button, 0, 0);
                lv_obj_add_event_cb(add_button, browser_add_event_cb, LV_EVENT_CLICKED, ENCODE_BROWSER_ID(browser_active_root, index));
                lv_obj_t * add_label = lv_label_create(add_button);
                lv_label_set_text(add_label, LV_SYMBOL_PLUS);
                lv_obj_set_style_text_color(add_label, lv_color_hex(0xFFFFFF), 0);
                lv_obj_center(add_label);
            }
        }
    }

    lv_label_set_text(browser_status_label, music_library_get_last_message());
}

static void browser_row_event_cb(lv_event_t *e)
{
    const void * encoded = lv_event_get_user_data(e);
    const music_library_storage_root_t root = DECODE_BROWSER_ROOT(encoded);
    const uint32_t index = DECODE_BROWSER_INDEX(encoded);

    if (music_library_browser_entry_is_directory(root, index)) {
        browser_active_root = root;
        music_library_browser_enter_directory(root, index);
        refresh_browser_modal();
        start_browser_refresh(false);
    }
}

static void browser_add_event_cb(lv_event_t *e)
{
    const void * encoded = lv_event_get_user_data(e);
    const music_library_storage_root_t root = DECODE_BROWSER_ROOT(encoded);
    const uint32_t index = DECODE_BROWSER_INDEX(encoded);

    if (music_library_browser_add_entry_async(root, index)) {
        show_browser_add_busy(music_library_browser_add_get_status());
        ensure_browser_busy_timer();
    } else {
        lv_label_set_text(browser_status_label, music_library_get_last_message());
    }
}

static void browser_up_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    music_library_browser_navigate_up(browser_active_root);
    refresh_browser_modal();
    start_browser_refresh(false);
}

static void browser_root_tab_event_cb(lv_event_t *e)
{
    browser_active_root = (music_library_storage_root_t)(uintptr_t)lv_event_get_user_data(e);
    refresh_browser_modal();
    start_browser_refresh(false);
}

static void browser_refresh_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    start_browser_refresh(music_library_browser_get_mode() == MUSIC_LIBRARY_BROWSER_MODE_FOLDER);
}

static void open_url_modal(void)
{
    music_library_download_reset();
    lv_textarea_set_text(url_textarea, "");
    lv_label_set_text(url_status_label, "The file will be downloaded to /sdcard/Downloads/Music.");
    lv_obj_clear_flag(url_overlay, LV_OBJ_FLAG_HIDDEN);
    if (download_timer == NULL) {
        download_timer = lv_timer_create(download_timer_cb, 200, NULL);
    }
}

static void close_url_modal(void)
{
    music_library_download_reset();
    lv_obj_add_flag(url_overlay, LV_OBJ_FLAG_HIDDEN);
}

static void url_close_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    close_url_modal();
}

static void url_download_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!music_library_download_start(lv_textarea_get_text(url_textarea))) {
        lv_label_set_text(url_status_label, music_library_download_get_status());
    }
}

static void url_keyboard_event_cb(lv_event_t *e)
{
    LV_UNUSED(e);
}

static void download_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    const music_library_download_state_t state = music_library_download_get_state();
    const int32_t progress = music_library_download_get_progress();

    if (state == MUSIC_LIBRARY_DOWNLOAD_STATE_RUNNING) {
        if (progress >= 0) {
            char buffer[192] = {};
            lv_snprintf(buffer, sizeof(buffer), "%s", music_library_download_get_status());
            lv_label_set_text(url_status_label, buffer);
        } else {
            lv_label_set_text(url_status_label, music_library_download_get_status());
        }
        return;
    }

    if (state == MUSIC_LIBRARY_DOWNLOAD_STATE_COMPLETED) {
        lv_label_set_text(url_status_label, music_library_download_get_status());
        playlist_content_dirty = true;
        _music_player_list_ui_reload();
        _music_player_main_ui_sync_state();
        close_url_modal();
        return;
    }

    if (state == MUSIC_LIBRARY_DOWNLOAD_STATE_FAILED) {
        lv_label_set_text(url_status_label, music_library_download_get_status());
        music_library_download_reset();
        return;
    }
}

static void refresh_status_label(void)
{
    if (status_label != NULL) {
        lv_label_set_text(status_label, music_library_get_last_message());
    }
}

static void show_browser_add_busy(const char *status)
{
    if (browser_add_status_label != NULL) {
        lv_label_set_text(browser_add_status_label, (status != NULL) ? status : "Adding to playlist...");
    }
    if (browser_add_overlay != NULL) {
        lv_obj_clear_flag(browser_add_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_browser_add_busy(void)
{
    if (browser_add_overlay != NULL) {
        lv_obj_add_flag(browser_add_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void browser_add_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);

    const music_library_browser_add_state_t state = music_library_browser_add_get_state();
    const music_library_browser_refresh_state_t refresh_state = music_library_browser_refresh_get_state();
    const char *status = music_library_browser_add_get_status();

    if (refresh_state == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_RUNNING) {
        if (browser_add_status_label != NULL) {
            lv_label_set_text(browser_add_status_label, music_library_browser_refresh_get_status());
        }
        return;
    }

    if (state == MUSIC_LIBRARY_BROWSER_ADD_STATE_RUNNING) {
        if (browser_add_status_label != NULL) {
            lv_label_set_text(browser_add_status_label, status);
        }
        return;
    }

    hide_browser_add_busy();

    if (refresh_state == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_COMPLETED) {
        refresh_browser_modal();
        lv_label_set_text(browser_status_label, music_library_browser_refresh_get_status());
        music_library_browser_refresh_reset();
    } else if (refresh_state == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_FAILED) {
        lv_label_set_text(browser_status_label, music_library_browser_refresh_get_status());
        music_library_browser_refresh_reset();
    }

    if (state == MUSIC_LIBRARY_BROWSER_ADD_STATE_COMPLETED) {
        playlist_content_dirty = true;
        _music_player_list_ui_reload();
        _music_player_main_ui_sync_state();
        close_browser_modal();
    } else if ((state == MUSIC_LIBRARY_BROWSER_ADD_STATE_FAILED) && (browser_status_label != NULL)) {
        lv_label_set_text(browser_status_label, status);
    }

    music_library_browser_add_reset();

    if ((browser_add_timer != NULL) &&
        (music_library_browser_refresh_get_state() == MUSIC_LIBRARY_BROWSER_REFRESH_STATE_IDLE) &&
        (music_library_browser_add_get_state() == MUSIC_LIBRARY_BROWSER_ADD_STATE_IDLE)) {
        lv_timer_del(browser_add_timer);
        browser_add_timer = NULL;
    }
}

bool _music_player_list_ui_needs_reload(void)
{
    return (!playlist_content_populated) || playlist_content_dirty;
}
#endif /*APP_MUSIC_PLAYER_ENABLE*/
