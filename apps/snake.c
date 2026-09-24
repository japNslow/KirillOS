#include "../khex.h"

#define BOARD_TOP    1
#define BOARD_BOTTOM 22
#define BOARD_LEFT   1
#define BOARD_RIGHT  78
#define MAX_SNAKE    128

typedef struct {
    uint8_t r;
    uint8_t c;
} point_t;

int _start(const khex_api_t* api) {
    api->clear();

    /* Draw border */
    for (int col = BOARD_LEFT; col <= BOARD_RIGHT; col++) {
        api->draw_char(BOARD_TOP, col, '-', 11);
        api->draw_char(BOARD_BOTTOM, col, '-', 11);
    }
    for (int row = BOARD_TOP; row <= BOARD_BOTTOM; row++) {
        api->draw_char(row, BOARD_LEFT, '|', 11);
        api->draw_char(row, BOARD_RIGHT, '|', 11);
    }
    api->draw_char(BOARD_TOP, BOARD_LEFT, '+', 11);
    api->draw_char(BOARD_TOP, BOARD_RIGHT, '+', 11);
    api->draw_char(BOARD_BOTTOM, BOARD_LEFT, '+', 11);
    api->draw_char(BOARD_BOTTOM, BOARD_RIGHT, '+', 11);

    /* Header */
    api->set_cursor(0, 2);
    api->set_color(14, 0);
    api->print("KHEX RETRO SNAKE | WASD / Arrows to Move | Q to Quit | Score: 0");

    point_t snake[MAX_SNAKE];
    int len = 4;
    int dir = 3; /* 0=Up, 1=Down, 2=Left, 3=Right */
    uint32_t score = 0;

    int start_r = 10;
    int start_c = 20;
    for (int i = 0; i < len; i++) {
        snake[i].r = (uint8_t)start_r;
        snake[i].c = (uint8_t)(start_c - i);
        api->draw_char(snake[i].r, snake[i].c, i == 0 ? 'O' : 'o', 10);
    }

    /* Spawn initial food */
    uint8_t food_r = 10;
    uint8_t food_c = 40;
    api->draw_char(food_r, food_c, '*', 12); /* Red food */

    while (1) {
        /* Non-blocking input */
        while (api->has_char()) {
            char key = api->poll_char();
            if (key == 'w' || key == 'W' || key == (char)0x80) { if (dir != 1) dir = 0; }
            else if (key == 's' || key == 'S' || key == (char)0x81) { if (dir != 0) dir = 1; }
            else if (key == 'a' || key == 'A' || key == (char)0x82) { if (dir != 3) dir = 2; }
            else if (key == 'd' || key == 'D' || key == (char)0x83) { if (dir != 2) dir = 3; }
            else if (key == 'q' || key == 'Q' || key == 27) {
                api->set_cursor(23, 2);
                api->print("Quitting game...\n");
                return 0;
            }
        }

        /* Calculate next head */
        int next_r = snake[0].r;
        int next_c = snake[0].c;
        if (dir == 0) next_r--;
        else if (dir == 1) next_r++;
        else if (dir == 2) next_c--;
        else if (dir == 3) next_c++;

        /* Check wall collision */
        if (next_r <= BOARD_TOP || next_r >= BOARD_BOTTOM ||
            next_c <= BOARD_LEFT || next_c >= BOARD_RIGHT) {
            break; /* Game Over */
        }

        /* Check self collision */
        int self_hit = 0;
        for (int i = 0; i < len; i++) {
            if (snake[i].r == next_r && snake[i].c == next_c) {
                self_hit = 1;
                break;
            }
        }
        if (self_hit) break;

        /* Check food collision */
        int ate = (next_r == food_r && next_c == food_c);

        if (ate) {
            score += 10;
            api->beep(988, 40);
            if (len < MAX_SNAKE - 1) len++;

            /* Spawn new food */
            food_r = (uint8_t)(BOARD_TOP + 1 + (api->rand() % (BOARD_BOTTOM - BOARD_TOP - 1)));
            food_c = (uint8_t)(BOARD_LEFT + 1 + (api->rand() % (BOARD_RIGHT - BOARD_LEFT - 1)));
            api->draw_char(food_r, food_c, '*', 12);

            /* Update score display */
            api->set_cursor(0, 62);
            api->set_color(14, 0);
            char sbuf[10]; int sl = 0; uint32_t tmp = score;
            if (tmp == 0) sbuf[sl++] = '0';
            while (tmp > 0) { sbuf[sl++] = (char)('0' + (tmp % 10)); tmp /= 10; }
            while (sl > 0) api->putchar(sbuf[--sl]);
            api->print("  ");
        } else {
            /* Erase old tail */
            api->draw_char(snake[len - 1].r, snake[len - 1].c, ' ', 0);
        }

        /* Move body */
        for (int i = len - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0].r = (uint8_t)next_r;
        snake[0].c = (uint8_t)next_c;

        /* Redraw head and body */
        api->draw_char(snake[1].r, snake[1].c, 'o', 10);
        api->draw_char(snake[0].r, snake[0].c, 'O', 14);

        api->delay(110);
    }

    /* Game Over */
    api->beep(220, 250);
    api->set_cursor(11, 30);
    api->set_color(15, 4); /* White on Red */
    api->print("  *** GAME OVER ***  ");
    api->set_cursor(13, 30);
    api->set_color(14, 0);
    api->print("Final Score: ");
    char sbuf[10]; int sl = 0; uint32_t tmp = score;
    if (tmp == 0) sbuf[sl++] = '0';
    while (tmp > 0) { sbuf[sl++] = (char)('0' + (tmp % 10)); tmp /= 10; }
    while (sl > 0) api->putchar(sbuf[--sl]);

    api->set_cursor(15, 27);
    api->set_color(7, 0);
    api->print("Press any key to exit...");
    api->getchar();

    api->clear();
    return 0;
}
