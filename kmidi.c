#include "kmidi.h"
#include "keyboard.h"
#include "kirillfs.h"
#include "sound.h"
#include "vga.h"

#define MAX_SONG_NOTES 128

typedef struct {
    uint16_t freq;       /* Hz, 0 = REST */
    uint16_t duration;   /* ms */
    char     name[6];    /* e.g. "C4", "D#4", "REST" */
} kmidi_note_t;

static kmidi_note_t song[MAX_SONG_NOTES];
static int          note_count = 0;
static int          cursor = 0;
static int          dirty = 0;
static char         current_file[32];

/* ================================================================ */
/*                     Note Frequency Database                      */
/* ================================================================ */
typedef struct {
    char     key_char;
    const char* name;
    uint16_t freq;
} key_note_map_t;

/* Piano keys mapping:
 * Octave 3 (Z..M): Z=C3, S=C#3, X=D3, D=D#3, C=E3, V=F3, G=F#3, B=G3, H=G#3, N=A3, J=A#3, M=B3
 * Octave 4 (Q..U): Q=C4, 2=C#4, W=D4, 3=D#4, E=E4, R=F4, 5=F#4, T=G4, 6=G#4, Y=A4, 7=A#4, U=B4, I=C5
 */
static const key_note_map_t KEY_MAP[] = {
    /* Octave 3 */
    { 'z', "C3",  131 }, { 's', "C#3", 139 },
    { 'x', "D3",  147 }, { 'd', "D#3", 156 },
    { 'c', "E3",  165 },
    { 'v', "F3",  175 }, { 'g', "F#3", 185 },
    { 'b', "G3",  196 }, { 'h', "G#3", 208 },
    { 'n', "A3",  220 }, { 'j', "A#3", 233 },
    { 'm', "B3",  247 },

    /* Octave 4 & 5 */
    { 'q', "C4",  262 }, { '2', "C#4", 277 },
    { 'w', "D4",  294 }, { '3', "D#4", 311 },
    { 'e', "E4",  330 },
    { 'r', "F4",  349 }, { '5', "F#4", 370 },
    { 't', "G4",  392 }, { '6', "G#4", 415 },
    { 'y', "A4",  440 }, { '7', "A#4", 466 },
    { 'u', "B4",  494 }, { 'i', "C5",  523 },
    { '9', "D5",  587 }, { 'o', "E5",  659 },
    { 'p', "G5",  784 }
};

#define KEY_MAP_COUNT (sizeof(KEY_MAP) / sizeof(KEY_MAP[0]))

static const key_note_map_t* find_note_by_key(char c) {
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    for (size_t i = 0; i < KEY_MAP_COUNT; i++) {
        if (KEY_MAP[i].key_char == c) return &KEY_MAP[i];
    }
    return 0;
}

static uint16_t find_freq_by_name(const char* name) {
    for (size_t i = 0; i < KEY_MAP_COUNT; i++) {
        const char* a = KEY_MAP[i].name;
        const char* b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return KEY_MAP[i].freq;
    }
    return 0;
}

/* ================================================================ */
/*                       String / Parsing Helpers                   */
/* ================================================================ */
static void str_copy(char* dest, const char* src, int max_len) {
    int i = 0;
    while (src[i] && i < max_len - 1) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
}

static void print_num(int val) {
    char s[10];
    int len = 0;
    if (val == 0) { vga_putchar('0'); return; }
    while (val > 0) {
        s[len++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (len > 0) vga_putchar(s[--len]);
}

/* ================================================================ */
/*                       Visual Tracker Display                     */
/* ================================================================ */
static void draw_piano(const char* active_note) {
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write(" Piano: ");

    /* White keys: C D E F G A B C D E */
    static const char* const keys[] = {
        "C3", "D3", "E3", "F3", "G3", "A3", "B3",
        "C4", "D4", "E4", "F4", "G4", "A4", "B4", "C5"
    };

    for (int i = 0; i < 15; i++) {
        int is_active = (active_note[0] == keys[i][0] && active_note[1] == keys[i][1]);
        if (is_active) {
            vga_set_color(VGA_BLACK, VGA_YELLOW);
        } else {
            vga_set_color(VGA_BLACK, VGA_WHITE);
        }
        vga_putchar('[');
        vga_write(keys[i]);
        vga_putchar(']');
        vga_set_color(VGA_BLACK, VGA_BLACK);
        vga_putchar(' ');
    }
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_newline();
}

static void redraw_kmidi(void) {
    vga_clear();

    /* Header */
    vga_set_color(VGA_BLACK, VGA_LIGHT_MAGENTA);
    vga_write(" KMIDI v0.2 - KirillOS MIDI Tracker & Composer ");
    vga_set_color(VGA_BLACK, VGA_MAGENTA);
    vga_write(" | ");
    vga_write(current_file);
    vga_write(" | Notes: ");
    print_num(note_count);
    if (dirty) {
        vga_set_color(VGA_WHITE, VGA_LIGHT_RED);
        vga_write(" [MODIFIED] ");
    }
    for (int i = 0; i < 15; i++) vga_putchar(' ');
    vga_newline();

    /* Visual Piano */
    const char* active = (cursor < note_count) ? song[cursor].name : "";
    draw_piano(active);

    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write("-------------------------------------------------------------------------------\n");

    /* Tracker Column Header */
    vga_set_color(VGA_YELLOW, VGA_BLACK);
    vga_write(" Step | Note  | Frequency | Duration | Visual Bar\n");
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write(" -----+-------+-----------+----------+----------------------------------------\n");

    /* Step Sequencer Table (13 steps shown) */
    int start_step = cursor - 6;
    if (start_step < 0) start_step = 0;
    if (start_step + 12 > note_count && note_count > 12) start_step = note_count - 12;

    for (int i = 0; i < 13; i++) {
        int step = start_step + i;
        if (step > note_count) {
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_write("   ~\n");
            continue;
        }

        int is_cursor = (step == cursor);

        if (is_cursor) {
            vga_set_color(VGA_BLACK, VGA_LIGHT_CYAN);
            vga_write(" >");
        } else {
            vga_set_color(VGA_DARK_GREY, VGA_BLACK);
            vga_write("  ");
        }

        /* Step number */
        vga_putchar('0' + (step + 1) / 100);
        vga_putchar('0' + ((step + 1) / 10) % 10);
        vga_putchar('0' + (step + 1) % 10);
        vga_write(" | ");

        if (step == note_count) {
            /* End of song marker */
            if (is_cursor) {
                vga_set_color(VGA_BLACK, VGA_LIGHT_GREEN);
                vga_write("[ADD NEW NOTE]                             \n");
            } else {
                vga_set_color(VGA_DARK_GREY, VGA_BLACK);
                vga_write("--- end of sequence ---\n");
            }
            vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
            continue;
        }

        /* Note name */
        if (song[step].freq == 0) {
            vga_set_color(is_cursor ? VGA_BLACK : VGA_DARK_GREY, is_cursor ? VGA_LIGHT_CYAN : VGA_BLACK);
            vga_write("REST ");
        } else {
            vga_set_color(is_cursor ? VGA_BLACK : VGA_LIGHT_GREEN, is_cursor ? VGA_LIGHT_CYAN : VGA_BLACK);
            vga_write(song[step].name);
            int len = 0; while (song[step].name[len]) len++;
            while (len++ < 5) vga_putchar(' ');
        }

        vga_set_color(is_cursor ? VGA_BLACK : VGA_LIGHT_GREY, is_cursor ? VGA_LIGHT_CYAN : VGA_BLACK);
        vga_write(" | ");

        /* Frequency */
        if (song[step].freq > 0) {
            print_num(song[step].freq);
            vga_write(" Hz   ");
            if (song[step].freq < 1000) vga_putchar(' ');
        } else {
            vga_write("  ---     ");
        }

        vga_write(" | ");

        /* Duration */
        print_num(song[step].duration);
        vga_write(" ms  ");
        if (song[step].duration < 100) vga_putchar(' ');

        vga_write(" | ");

        /* Visual bar */
        vga_set_color(is_cursor ? VGA_BLACK : VGA_LIGHT_CYAN, is_cursor ? VGA_LIGHT_CYAN : VGA_BLACK);
        vga_putchar('[');
        int bars = song[step].duration / 40;
        if (bars > 20) bars = 20;
        for (int b = 0; b < bars; b++) vga_putchar('=');
        for (int b = bars; b < 20; b++) vga_putchar(' ');
        vga_putchar(']');

        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        vga_newline();
    }

    /* Controls Bar */
    vga_set_color(VGA_DARK_GREY, VGA_BLACK);
    vga_write("-------------------------------------------------------------------------------\n");
    vga_set_color(VGA_BLACK, VGA_LIGHT_GREY);
    vga_write(" [Space] Play Song | [Enter] Note | [Q..U, Z..M] Piano Keys | [+/-] Length\n");
    vga_write(" [R] REST | [Ins] Insert | [Del] Delete | [^S] Save | [^Q]/[ESC] Exit        ");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ================================================================ */
/*                       Playback Routine                           */
/* ================================================================ */
static void play_step(int idx) {
    if (idx >= note_count) return;
    if (song[idx].freq > 0) {
        sound_soft_note(song[idx].freq, song[idx].duration);
    } else {
        /* Silence / REST */
        sound_note(0, song[idx].duration);
    }
}

static void play_full_song(void) {
    for (int i = 0; i < note_count; i++) {
        cursor = i;
        redraw_kmidi();
        play_step(i);

        /* Check if user wants to cancel playback */
        if (keyboard_has_char()) {
            char c = keyboard_poll();
            if (c == ' ' || c == KEY_ESC || c == KEY_QUIT) break;
        }
    }
    redraw_kmidi();
}

/* ================================================================ */
/*                      File I/O (.kmidi)                           */
/* ================================================================ */
static void save_kmidi_file(void) {
    char out_buf[512];
    int pos = 0;

    for (int i = 0; i < note_count && pos < 480; i++) {
        const char* n = song[i].name;
        while (*n && pos < 500) out_buf[pos++] = *n++;
        out_buf[pos++] = ':';

        char d_str[8]; int d_len = 0; int val = song[i].duration;
        if (val == 0) d_str[d_len++] = '0';
        while (val > 0) { d_str[d_len++] = (char)('0' + (val % 10)); val /= 10; }
        while (d_len > 0) out_buf[pos++] = d_str[--d_len];

        out_buf[pos++] = ' ';
    }
    if (pos > 0) out_buf[pos - 1] = 0;
    else out_buf[0] = 0;

    if (kfs_write(current_file, out_buf) == KFS_OK) {
        dirty = 0;
        redraw_kmidi();
    } else {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("\nSave failed: file too large or KirillFS error\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    }
}

static void load_kmidi_data(const char* data, int size) {
    note_count = 0;
    int i = 0;

    while (i < size && note_count < MAX_SONG_NOTES) {
        while (i < size && (data[i] == ' ' || data[i] == '\n' || data[i] == '\r')) i++;
        if (i >= size) break;

        /* Read note name until ':' */
        char nname[8];
        int nlen = 0;
        while (i < size && data[i] != ':' && data[i] != ' ' && data[i] != '\n' && nlen < 6) {
            nname[nlen++] = data[i++];
        }
        nname[nlen] = 0;

        uint16_t dur = 200;
        if (i < size && data[i] == ':') {
            i++;
            uint32_t val = 0;
            while (i < size && data[i] >= '0' && data[i] <= '9') {
                val = val * 10 + (uint32_t)(data[i++] - '0');
            }
            if (val > 0) dur = (uint16_t)val;
        }

        uint16_t frq = 0;
        if (nname[0] == 'R' && nname[1] == 'E' && nname[2] == 'S' && nname[3] == 'T') {
            frq = 0;
        } else {
            frq = find_freq_by_name(nname);
        }

        song[note_count].freq = frq;
        song[note_count].duration = dur;
        str_copy(song[note_count].name, nname, 6);
        note_count++;
    }
}

/* ================================================================ */
/*                       Interactive Editor                         */
/* ================================================================ */
void kmidi_open(const char* filename) {
    str_copy(current_file, filename, 30);
    cursor = 0;
    dirty = 0;
    note_count = 0;

    const char* data;
    int size = 0;
    if (kfs_read(filename, &data, &size) == KFS_OK) {
        load_kmidi_data(data, size);
    } else {
        kfs_touch(filename);
    }

    redraw_kmidi();

    for (;;) {
        char c = keyboard_getchar();

        if (c == KEY_QUIT || c == KEY_ESC) {
            if (dirty) {
                vga_set_color(VGA_WHITE, VGA_LIGHT_RED);
                vga_write("\nUnsaved changes! Press Ctrl+S to save, or Ctrl+Q to discard.\n");
                vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
                char confirm = keyboard_getchar();
                if (confirm == KEY_QUIT || confirm == KEY_ESC) {
                    vga_clear();
                    return;
                }
                if (confirm == KEY_SAVE) save_kmidi_file();
                redraw_kmidi();
            } else {
                vga_clear();
                return;
            }
        }
        else if (c == KEY_SAVE) {
            save_kmidi_file();
        }
        else if (c == ' ') {
            play_full_song();
        }
        else if (c == KEY_ENTER) {
            play_step(cursor);
        }
        else if (c == KEY_UP) {
            if (cursor > 0) cursor--;
            redraw_kmidi();
        }
        else if (c == KEY_DOWN) {
            if (cursor < note_count) cursor++;
            redraw_kmidi();
        }
        else if (c == '+' || c == '=') {
            if (cursor < note_count && song[cursor].duration < 2000) {
                song[cursor].duration += 50;
                dirty = 1;
                redraw_kmidi();
            }
        }
        else if (c == '-' || c == '_') {
            if (cursor < note_count && song[cursor].duration > 50) {
                song[cursor].duration -= 50;
                dirty = 1;
                redraw_kmidi();
            }
        }
        else if (c == 'r' || c == 'R') {
            /* Insert REST */
            if (cursor < note_count) {
                song[cursor].freq = 0;
                str_copy(song[cursor].name, "REST", 6);
            } else if (note_count < MAX_SONG_NOTES) {
                song[note_count].freq = 0;
                song[note_count].duration = 200;
                str_copy(song[note_count].name, "REST", 6);
                cursor = ++note_count;
            }
            dirty = 1;
            redraw_kmidi();
        }
        else if (c == KEY_INS) {
            if (note_count < MAX_SONG_NOTES) {
                for (int i = note_count; i > cursor; i--) song[i] = song[i - 1];
                song[cursor].freq = 440;
                song[cursor].duration = 200;
                str_copy(song[cursor].name, "A4", 6);
                note_count++;
                dirty = 1;
                redraw_kmidi();
            }
        }
        else if (c == KEY_DEL || c == KEY_BACKSPACE) {
            if (cursor < note_count) {
                for (int i = cursor; i < note_count - 1; i++) song[i] = song[i + 1];
                note_count--;
                dirty = 1;
                redraw_kmidi();
            } else if (c == KEY_BACKSPACE && cursor > 0) {
                cursor--;
                for (int i = cursor; i < note_count - 1; i++) song[i] = song[i + 1];
                note_count--;
                dirty = 1;
                redraw_kmidi();
            }
        }
        else {
            /* Check if keyboard key plays a piano note! */
            const key_note_map_t* m = find_note_by_key(c);
            if (m) {
                if (cursor < note_count) {
                    song[cursor].freq = m->freq;
                    str_copy(song[cursor].name, m->name, 6);
                } else if (note_count < MAX_SONG_NOTES) {
                    song[note_count].freq = m->freq;
                    song[note_count].duration = 200;
                    str_copy(song[note_count].name, m->name, 6);
                    cursor = ++note_count;
                }
                dirty = 1;
                redraw_kmidi();
                /* Play note instantly */
                sound_soft_note(m->freq, 120);
            }
        }
    }
}

/* ================================================================ */
/*                       CLI Direct Player                          */
/* ================================================================ */
void kmidi_play_file(const char* filename) {
    const char* data;
    int size = 0;

    if (kfs_read(filename, &data, &size) != KFS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("kmidi: file '");
        vga_write(filename);
        vga_write("' not found\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return;
    }

    load_kmidi_data(data, size);

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("Playing '");
    vga_write(filename);
    vga_write("' (");
    print_num(note_count);
    vga_write(" notes) [Press Space/ESC to stop]...\n");

    for (int i = 0; i < note_count; i++) {
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        vga_write(" [");
        vga_write(song[i].name);
        vga_write("] ");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

        play_step(i);

        if (keyboard_has_char()) {
            char c = keyboard_poll();
            if (c == ' ' || c == KEY_ESC || c == KEY_QUIT) {
                vga_write("\n[Playback interrupted]\n");
                return;
            }
        }
    }
    vga_newline();
    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("Finished playing.\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* ================================================================ */
/*                     Preloaded Demo Songs                         */
/* ================================================================ */
void kmidi_init_default_songs(void) {
    /* 1. Tetris / Korobeiniki theme */
    if (kfs_touch("tetris.kmidi") == KFS_OK) {
        kfs_write("tetris.kmidi",
            "E5:250 B4:125 C5:125 D5:250 C5:125 B4:125 A4:250 A4:125 C5:125 E5:250 "
            "D5:125 C5:125 B4:375 C5:125 D5:250 E5:250 C5:250 A4:250 A4:250 REST:125 "
            "D5:250 F5:125 A5:250 G5:125 F5:125 E5:375 C5:125 E5:250 D5:125 C5:125 "
            "B4:250 B4:125 C5:125 D5:250 E5:250 C5:250 A4:250 A4:250");
    }

    /* 2. Super Mario Bros theme intro */
    if (kfs_touch("mario.kmidi") == KFS_OK) {
        kfs_write("mario.kmidi",
            "E5:150 E5:150 REST:100 E5:150 REST:100 C5:150 E5:200 G5:300 REST:150 G4:300 "
            "C5:200 REST:100 G4:200 REST:100 E4:200 REST:100 A4:150 B4:150 A#4:150 A4:150 "
            "G4:150 E5:150 G5:150 A5:200 F5:150 G5:150 E5:150 C5:150 D5:150 B4:200");
    }

    /* 3. KirillOS Chiptune demo */
    if (kfs_touch("demo.kmidi") == KFS_OK) {
        kfs_write("demo.kmidi",
            "C4:120 E4:120 G4:120 C5:120 G4:120 E4:120 "
            "D4:120 F4:120 A4:120 D5:120 A4:120 F4:120 "
            "E4:120 G4:120 B4:120 E5:120 B4:120 G4:120 "
            "C5:350");
    }
}
