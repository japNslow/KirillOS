#include "kgui.h"
#include "kguiq.h"
#include "kguid.h"
#include "mode13.h"
#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "kaint.h"
#include "kcommander.h"
#include "sound.h"
#include <stdint.h>

extern void kernel_execute_command(char* cmd);

static int kgui_exit_flag = 0;

static int kgui_streq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int kgui_str_starts_with(const char* s, const char* prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

/* ================================================================ */
/*                       App: Terminal State                        */
/* ================================================================ */
#define TERM_MAX_LINES 32
#define TERM_LINE_LEN  36
static char term_lines[TERM_MAX_LINES][TERM_LINE_LEN];
static int  term_line_count = 0;
static char term_input[TERM_LINE_LEN];
static int  term_input_len = 0;

static char term_out_line[TERM_LINE_LEN];
static int  term_out_len = 0;

static void term_add_line(const char* s) {
    if (term_line_count < TERM_MAX_LINES) {
        int i = 0;
        while (s && s[i] && i < TERM_LINE_LEN - 1) {
            term_lines[term_line_count][i] = s[i];
            i++;
        }
        term_lines[term_line_count][i] = 0;
        term_line_count++;
    } else {
        /* Scroll up */
        for (int i = 0; i < TERM_MAX_LINES - 1; i++) {
            for (int j = 0; j < TERM_LINE_LEN; j++) {
                term_lines[i][j] = term_lines[i + 1][j];
            }
        }
        int i = 0;
        while (s && s[i] && i < TERM_LINE_LEN - 1) {
            term_lines[TERM_MAX_LINES - 1][i] = s[i];
            i++;
        }
        term_lines[TERM_MAX_LINES - 1][i] = 0;
    }
}

static void kgui_term_char_hook(char c) {
    if (c == '\f') {
        term_line_count = 0;
        term_out_len = 0;
        term_out_line[0] = 0;
        return;
    }
    if (c == '\n') {
        term_out_line[term_out_len] = 0;
        term_add_line(term_out_line);
        term_out_len = 0;
        term_out_line[0] = 0;
        return;
    }
    if (c == '\r') {
        return;
    }
    if (c == '\b') {
        if (term_out_len > 0) {
            term_out_line[--term_out_len] = 0;
        }
        return;
    }
    if (c >= 32 && c <= 126) {
        if (term_out_len < TERM_LINE_LEN - 1) {
            term_out_line[term_out_len++] = c;
            term_out_line[term_out_len] = 0;
        } else {
            term_out_line[term_out_len] = 0;
            term_add_line(term_out_line);
            term_out_len = 0;
            term_out_line[term_out_len++] = c;
            term_out_line[term_out_len] = 0;
        }
    }
}

static void term_execute(void) {
    if (term_input_len == 0) return;

    /* 1. Echo user prompt */
    char echo_buf[TERM_LINE_LEN];
    echo_buf[0] = '>';
    echo_buf[1] = ' ';
    int el = 2;
    for (int i = 0; i < term_input_len && el < TERM_LINE_LEN - 1; i++) {
        echo_buf[el++] = term_input[i];
    }
    echo_buf[el] = 0;
    term_add_line(echo_buf);

    /* 2. Special GUI commands */
    if (kgui_str_starts_with(term_input, "wallpaper ") || kgui_str_starts_with(term_input, "wall ")) {
        char* fname = term_input + (term_input[1] == 'a' && term_input[2] == 'l' && term_input[3] == 'l' && term_input[4] == ' ' ? 5 : 10);
        while (*fname == ' ') fname++;
        if (*fname) {
            kguid_set_wallpaper(fname);
            term_add_line("Wallpaper updated!");
        } else {
            kguid_set_wallpaper("demo.bmp");
            term_add_line("Default wallpaper set");
        }
    } else if (kgui_streq(term_input, "exit gui") || kgui_streq(term_input, "quit gui")) {
        kgui_exit_flag = 1;
    } else if (kgui_streq(term_input, "exit") || kgui_streq(term_input, "quit")) {
        int f = kguid_get_focused_window();
        if (f != -1) kguid_close_window(f);
    } else if (kgui_str_starts_with(term_input, "kano ") || kgui_str_starts_with(term_input, "edit ")) {
        char* fname = term_input + (term_input[0] == 'k' ? 5 : 5);
        while (*fname == ' ') fname++;
        if (*fname) {
            kguid_create_window(fname, 45, 30, 175, 115, KGUID_APP_NOTEPAD, KGUI_ICON_NOTEPAD);
            term_add_line("Opened editor window.");
        }
    } else {
        /* 3. Execute directly in REAL KirillOS Kernel */
        term_out_len = 0;
        term_out_line[0] = 0;
        vga_set_output_hook(kgui_term_char_hook);

        kernel_execute_command(term_input);

        if (term_out_len > 0) {
            term_out_line[term_out_len] = 0;
            term_add_line(term_out_line);
            term_out_len = 0;
        }
        vga_set_output_hook(0);
    }

    term_input_len = 0;
    term_input[0] = 0;
}

/* ================================================================ */
/*                       App: Notepad State                         */
/* ================================================================ */
static char notepad_filename[KFS_NAME_MAX + 1] = "notes.txt";
static char notepad_text[128] = "Welcome to KGUI Notepad!\nType here...";
static int  notepad_len = 39;

/* ================================================================ */
/*                       App: Files Explorer State                  */
/* ================================================================ */
static int files_selected = 0;

/* ================================================================ */
/*                       App: Settings State                        */
/* ================================================================ */
static char settings_wall_input[KFS_NAME_MAX + 1] = "demo.bmp";

/* ================================================================ */
/*                     Window Client Drawing Dispatcher             */
/* ================================================================ */
static void draw_window_client(kgui_window_t* win) {
    int cx = win->x + 4;
    int cy = win->y + 18;
    int cw = win->w - 8;
    int ch = win->h - 22;

    switch (win->app_type) {
        case KGUID_APP_TERMINAL: {
            /* Black background */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_BLACK);
            /* Terminal lines: show most recent lines that fit above the prompt */
            int visible_slots = (ch - 14) / 9;
            if (visible_slots < 1) visible_slots = 1;
            int start_idx = 0;
            if (term_line_count > visible_slots) {
                start_idx = term_line_count - visible_slots;
            }
            int start_y = cy + 2;
            for (int i = start_idx; i < term_line_count; i++) {
                kguiq_draw_string_clipped(cx + 2, start_y, term_lines[i], KGUI_COLOR_LIGHT_GREEN, cx + cw - 2);
                start_y += 9;
            }
            /* Prompt and current input */
            if (start_y + 8 <= cy + ch) {
                mode13_draw_string(cx + 2, start_y, ">", KGUI_COLOR_WHITE);
                kguiq_draw_string_clipped(cx + 12, start_y, term_input, KGUI_COLOR_WHITE, cx + cw - 10);
                int cur_x = cx + 12 + term_input_len * 8;
                if (cur_x < cx + cw - 8) {
                    mode13_draw_line(cur_x, start_y, cur_x, start_y + 7, KGUI_COLOR_LIGHT_GREEN);
                }
            }
            break;
        }

        case KGUID_APP_NOTEPAD: {
            /* White background */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_WHITE);
            /* Mini toolbar */
            kguiq_draw_rect_fill(cx, cy, cw, 13, KGUI_3D_FACE);
            kguiq_draw_bevel(cx, cy, cw, 13, 0);
            kguiq_draw_button(cx + 2, cy + 1, 30, 10, "Save", 0, 0);
            kguiq_draw_button(cx + 34, cy + 1, 30, 10, "Open", 0, 0);
            kguiq_draw_string_clipped(cx + 70, cy + 2, notepad_filename, KGUI_COLOR_DARK_GREY, cx + cw - 2);

            /* Text lines */
            int ty = cy + 16;
            int tx = cx + 4;
            for (int i = 0; i < notepad_len; i++) {
                char c = notepad_text[i];
                if (c == '\n') {
                    ty += 9;
                    tx = cx + 4;
                    if (ty + 8 > cy + ch) break;
                } else {
                    if (tx + 8 <= cx + cw - 4) {
                        mode13_draw_char(tx, ty, c, KGUI_COLOR_BLACK);
                        tx += 8;
                    }
                }
            }
            break;
        }

        case KGUID_APP_FILES: {
            /* Light grey face */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_WHITE);
            int count = kfs_file_count();
            int fy = cy + 3;
            for (int i = 0; i < count; i++) {
                if (fy + 11 > cy + ch - 16) break;
                if (i == files_selected) {
                    kguiq_draw_rect_fill(cx + 2, fy - 1, cw - 4, 11, KGUI_COLOR_BLUE);
                }
                kguiq_draw_icon(cx + 4, fy, KGUI_ICON_FILE);
                uint8_t tc = (i == files_selected) ? KGUI_COLOR_WHITE : KGUI_COLOR_BLACK;
                kguiq_draw_string_clipped(cx + 18, fy + 1, kfs_name(i), tc, cx + cw - 4);
                fy += 11;
            }

            /* Bottom action buttons */
            int by = cy + ch - 14;
            kguiq_draw_rect_fill(cx, by, cw, 14, KGUI_3D_FACE);
            kguiq_draw_bevel(cx, by, cw, 14, 0);
            kguiq_draw_button(cx + 2, by + 1, 38, 12, "Open", 0, 0);
            kguiq_draw_button(cx + 42, by + 1, 38, 12, "Del", 0, 0);
            break;
        }

        case KGUID_APP_SETTINGS: {
            /* Grey background with controls */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_3D_FACE);
            mode13_draw_string(cx + 4, cy + 4, "Wallpaper Image:", KGUI_COLOR_BLACK);
            kguiq_draw_input(cx + 4, cy + 16, cw - 8, 13, settings_wall_input, 0, 0);

            kguiq_draw_button(cx + 4, cy + 33, 56, 12, "demo.bmp", 0, 0);
            kguiq_draw_button(cx + 64, cy + 33, 44, 12, "None", 0, 0);
            kguiq_draw_button(cx + 4, cy + 49, cw - 8, 13, "Apply Wallpaper", 0, 0);

            mode13_draw_string(cx + 4, cy + 66, "Current:", KGUI_COLOR_DARK_GREY);
            kguiq_draw_string_clipped(cx + 56, cy + 66, kguid_get_wallpaper(), KGUI_COLOR_BLUE, cx + cw - 2);
            break;
        }

        case KGUID_APP_SYSINFO: {
            /* Tech specs card */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_BLACK);
            mode13_draw_rect(cx, cy, cw, ch, KGUI_COLOR_CYAN);

            mode13_draw_string(cx + 4, cy + 3,  "KirillOS v0.2 GUI", KGUI_COLOR_YELLOW);
            mode13_draw_line(cx + 4, cy + 12, cx + cw - 5, cy + 12, KGUI_COLOR_DARK_GREY);

            mode13_draw_string(cx + 4, cy + 15, "CPU : x86 IA-32", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 24, "RAM : 128 MB", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 33, "VGA : Mode 13h (256c)", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 42, "Disk: ATA LBA28 KFS4", KGUI_COLOR_LIGHT_GREEN);
            mode13_draw_string(cx + 4, cy + 51, "Snd : AC'97 + PIT", KGUI_COLOR_LIGHT_GREEN);

            kguiq_draw_progressbar(cx + 4, cy + 63, cw - 8, 8, 85, KGUI_COLOR_BLUE);
            break;
        }

        case KGUID_APP_VIEWER: {
            /* Paint / Viewer placeholder with Open Kaint button */
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_DARK_GREY);
            mode13_draw_string(cx + 4, cy + 8, "Kaint Image Studio", KGUI_COLOR_WHITE);
            kguiq_draw_button(cx + 10, cy + 24, cw - 20, 16, "Launch Kaint", 0, 0);
            kguiq_draw_button(cx + 10, cy + 44, cw - 20, 16, "Commander", 0, 0);
            break;
        }

        default:
            kguiq_draw_rect_fill(cx, cy, cw, ch, KGUI_COLOR_LIGHT_GREY);
            break;
    }
}

/* ================================================================ */
/*                       Main KGUI Event Loop                       */
/* ================================================================ */
void kgui_start(void) {
    /* 1. Enter Mode 13h (320x200 256 colors) */
    mode13_enter();
    mouse_init();
    mouse_set_position(160, 100);

    /* 2. Init Desktop & Window Manager */
    kguid_init();
    term_line_count = 0;
    term_add_line("KirillOS Terminal v1.0");
    term_add_line("Real Kernel Shell Ready");

    /* Create initial desktop windows */
    kguid_create_window("Terminal", 50, 16, 215, 130, KGUID_APP_TERMINAL, KGUI_ICON_TERMINAL);

    kgui_exit_flag = 0;
    int running = 1;
    int prev_btn_left = 0;
    int dragging = 0;
    int drag_win_id = -1;
    int drag_dx = 0, drag_dy = 0;
    int selected_shortcut = -1;
    int need_redraw = 1;
    uint32_t idle_ticks = 0;

    while (running && !kgui_exit_flag) {
        /* Poll hardware input */
        int mouse_moved = mouse_poll();
        char key = keyboard_poll();

        /* Global exit hotkey: Esc */
        if (key == KEY_ESC || kgui_exit_flag) {
            running = 0;
            break;
        }

        int btn_clicked = (mouse_btn_left && !prev_btn_left);
        int btn_released = (!mouse_btn_left && prev_btn_left);

        if (mouse_moved || btn_clicked || btn_released || dragging || (key != 0)) {
            need_redraw = 1;
        }
        idle_ticks++;
        if (idle_ticks >= 200000) {
            idle_ticks = 0;
            need_redraw = 1; /* periodic clock/tray refresh */
        }

        /* Handle Window Dragging */
        if (dragging && drag_win_id != -1) {
            if (mouse_btn_left) {
                kgui_window_t* win = kguid_get_window(drag_win_id);
                if (win && !win->maximized) {
                    win->x = mouse_x - drag_dx;
                    win->y = mouse_y - drag_dy;
                    if (win->x < -win->w + 30) win->x = -win->w + 30;
                    if (win->x > MODE13_WIDTH - 30) win->x = MODE13_WIDTH - 30;
                    if (win->y < 0) win->y = 0;
                    if (win->y > KGUID_DESKTOP_H - 16) win->y = KGUID_DESKTOP_H - 16;
                }
            } else {
                dragging = 0;
                drag_win_id = -1;
            }
        }

        /* Handle Mouse Click Events */
        if (btn_clicked) {
            /* 1. Click on Start Menu popup */
            if (kguid_is_start_menu_open()) {
                int item = kguid_start_menu_item_at(mouse_x, mouse_y);
                if (item != -1) {
                    kguid_set_start_menu(0);
                    switch (item) {
                        case 0: kguid_create_window("Terminal", 20, 20, 170, 115, KGUID_APP_TERMINAL, KGUI_ICON_TERMINAL); break;
                        case 1: kguid_create_window("Files", 40, 30, 160, 110, KGUID_APP_FILES, KGUI_ICON_FILES); break;
                        case 2: kguid_create_window("Notepad", 50, 35, 170, 110, KGUID_APP_NOTEPAD, KGUI_ICON_NOTEPAD); break;
                        case 3: kguid_create_window("Paint", 60, 40, 160, 95, KGUID_APP_VIEWER, KGUI_ICON_PAINT); break;
                        case 4: kguid_create_window("Display Settings", 45, 30, 160, 105, KGUID_APP_SETTINGS, KGUI_ICON_SETTINGS); break;
                        case 5: kguid_create_window("System Info", 70, 45, 165, 95, KGUID_APP_SYSINFO, KGUI_ICON_SYSINFO); break;
                        case 6: running = 0; break; /* Exit to Shell */
                        case 7: {
                            /* Reboot */
                            __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFE), "Nd"(0x64));
                            break;
                        }
                    }
                    continue;
                } else if (!kguiq_point_in_rect(mouse_x, mouse_y, 2, KGUID_TASKBAR_Y, 44, KGUID_TASKBAR_H)) {
                    kguid_set_start_menu(0);
                }
            }

            /* 2. Click on Taskbar */
            if (mouse_y >= KGUID_TASKBAR_Y) {
                if (kguiq_point_in_rect(mouse_x, mouse_y, 2, KGUID_TASKBAR_Y + 2, 54, 12)) {
                    kguid_toggle_start_menu();
                } else {
                    int clicked_wid = kguid_find_taskbar_window_at(mouse_x, mouse_y);
                    if (clicked_wid != -1) {
                        kgui_window_t* w = kguid_get_window(clicked_wid);
                        if (w) {
                            if (w->minimized || !w->active) {
                                kguid_focus_window(w->id);
                            } else {
                                kguid_minimize_window(w->id);
                            }
                        }
                    }
                }
                continue;
            }

            /* 3. Click on Window Titlebar */
            int title_btn = 0;
            int win_id = kguid_find_titlebar_at(mouse_x, mouse_y, &title_btn);
            if (win_id != -1) {
                kguid_focus_window(win_id);
                if (title_btn == 3) {
                    kguid_close_window(win_id);
                } else if (title_btn == 2) {
                    kguid_maximize_window(win_id);
                } else if (title_btn == 1) {
                    kguid_minimize_window(win_id);
                } else {
                    /* Start Dragging */
                    kgui_window_t* w = kguid_get_window(win_id);
                    if (w && !w->maximized) {
                        dragging = 1;
                        drag_win_id = win_id;
                        drag_dx = mouse_x - w->x;
                        drag_dy = mouse_y - w->y;
                    }
                }
                continue;
            }

            /* 4. Click inside Window Client Area */
            win_id = kguid_find_window_at(mouse_x, mouse_y);
            if (win_id != -1) {
                kguid_focus_window(win_id);
                kgui_window_t* w = kguid_get_window(win_id);
                int cx = w->x + 4;
                int cy = w->y + 18;
                int cw = w->w - 8;
                int ch = w->h - 22;

                if (w->app_type == KGUID_APP_SETTINGS) {
                    /* Buttons in Settings window */
                    if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 4, cy + 33, 56, 12)) {
                        kguid_set_wallpaper("demo.bmp");
                    } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 64, cy + 33, 44, 12)) {
                        kguid_set_wallpaper("-");
                    } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 4, cy + 49, cw - 8, 13)) {
                        kguid_set_wallpaper(settings_wall_input);
                    }
                } else if (w->app_type == KGUID_APP_VIEWER) {
                    if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 10, cy + 24, cw - 20, 16)) {
                        /* Launch Kaint full-screen and resume */
                        mouse_hide_cursor();
                        kaint_start("demo.bmp");
                        mode13_enter();
                        mouse_init();
                    } else if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 10, cy + 44, cw - 20, 16)) {
                        mouse_hide_cursor();
                        kcommander_start();
                        mode13_enter();
                        mouse_init();
                    }
                } else if (w->app_type == KGUID_APP_FILES) {
                    /* Files list selection */
                    int fy = cy + 3;
                    int count = kfs_file_count();
                    for (int i = 0; i < count; i++) {
                        if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 2, fy - 1, cw - 4, 11)) {
                            files_selected = i;
                            break;
                        }
                        fy += 11;
                    }
                    /* Open button */
                    int by = cy + ch - 14;
                    if (kguiq_point_in_rect(mouse_x, mouse_y, cx + 2, by + 1, 38, 12)) {
                        const char* fname = kfs_name(files_selected);
                        if (fname) {
                            if (fname[0] == 'd' && fname[1] == 'e' && fname[2] == 'm' && fname[3] == 'o') {
                                kguid_set_wallpaper(fname);
                            } else {
                                kguid_create_window(fname, 50, 30, 160, 110, KGUID_APP_NOTEPAD, KGUI_ICON_NOTEPAD);
                            }
                        }
                    }
                }
                continue;
            }

            /* 5. Click on Desktop Shortcuts */
            int sc = kguid_find_shortcut_at(mouse_x, mouse_y);
            if (sc != -1) {
                if (selected_shortcut == sc) {
                    /* Double-click / activate shortcut */
                    kgui_shortcut_t* item = kguid_get_shortcut(sc);
                    if (item) {
                        kguid_create_window(item->label, 30 + sc * 15, 25 + sc * 10, 165, 110, item->app_type, item->icon);
                    }
                    selected_shortcut = -1;
                } else {
                    selected_shortcut = sc;
                }
            } else {
                selected_shortcut = -1;
            }
        }

        if (btn_released) {
            dragging = 0;
            drag_win_id = -1;
        }
        prev_btn_left = mouse_btn_left;

        /* Handle Keyboard Input for Active Window */
        if (key != 0) {
            int focused_id = kguid_get_focused_window();
            kgui_window_t* focused_win = kguid_get_window(focused_id);
            if (focused_win) {
                if (focused_win->app_type == KGUID_APP_TERMINAL) {
                    if (key == '\n') {
                        term_execute();
                    } else if (key == '\b') {
                        if (term_input_len > 0) {
                            term_input[--term_input_len] = 0;
                        }
                    } else if (key >= 32 && key < 127) {
                        if (term_input_len < TERM_LINE_LEN - 1) {
                            term_input[term_input_len++] = key;
                            term_input[term_input_len] = 0;
                        }
                    }
                } else if (focused_win->app_type == KGUID_APP_NOTEPAD) {
                    if (key == '\b') {
                        if (notepad_len > 0) notepad_text[--notepad_len] = 0;
                    } else if (key == '\n') {
                        if (notepad_len < 126) {
                            notepad_text[notepad_len++] = '\n';
                            notepad_text[notepad_len] = 0;
                        }
                    } else if (key >= 32 && key < 127) {
                        if (notepad_len < 126) {
                            notepad_text[notepad_len++] = key;
                            notepad_text[notepad_len] = 0;
                        }
                    }
                } else if (focused_win->app_type == KGUID_APP_SETTINGS) {
                    int slen = 0;
                    while (settings_wall_input[slen]) slen++;
                    if (key == '\b') {
                        if (slen > 0) settings_wall_input[slen - 1] = 0;
                    } else if (key >= 32 && key < 127 && slen < KFS_NAME_MAX) {
                        settings_wall_input[slen] = key;
                        settings_wall_input[slen + 1] = 0;
                    }
                }
            }
        }

        /* ================= Redraw Screen Frame ================= */
        if (need_redraw) {
            need_redraw = 0;
            mouse_hide_cursor();

            /* 1. Desktop & Wallpaper */
            kguid_draw_desktop();

            /* 2. Desktop Shortcuts */
            kguid_draw_shortcuts(selected_shortcut);

            /* 3. Windows (in bottom-to-top z-order) */
            int z_count = kguid_get_zorder_count();
            for (int z = 0; z < z_count; z++) {
                int wid = kguid_get_zorder_window(z);
                kgui_window_t* w = kguid_get_window(wid);
                if (!w || w->minimized) continue;

                /* Draw 3D Window Frame & Titlebar */
                kguiq_draw_window_frame(w->x, w->y, w->w, w->h, w->title, w->active, 1, 1, 1);

                /* Draw Client Area */
                draw_window_client(w);
            }

            /* 4. Taskbar & Widgets */
            int focused_id = kguid_get_focused_window();
            kguid_draw_taskbar(mouse_btn_left && kguiq_point_in_rect(mouse_x, mouse_y, 2, KGUID_TASKBAR_Y, 54, 16), focused_id);

            /* 5. Start Menu if active */
            if (kguid_is_start_menu_open()) {
                int hovered_sm = kguid_start_menu_item_at(mouse_x, mouse_y);
                kguid_draw_start_menu(hovered_sm);
            }

            /* 6. Mouse Cursor */
            mouse_draw_cursor();
        } else {
            for (volatile int i = 0; i < 5000; i++) __asm__ volatile("nop");
        }
    }

    /* Clean exit to text console */
    mouse_hide_cursor();
    mouse_disable();
    mode13_exit();
}
