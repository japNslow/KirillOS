#include "../khex.h"

int _start(const khex_api_t* api) {
    api->clear();
    api->set_color(11, 0);
    api->print("=========================================\n");
    api->print("    GUESS THE NUMBER (1 - 100)           \n");
    api->print("=========================================\n");
    api->set_color(7, 0);
    api->print("I'm thinking of a number between 1 and 100.\n");
    api->print("Type your guess and press Enter (or 'q' to quit).\n\n");

    uint32_t secret = (api->rand() % 100) + 1;
    int attempts = 0;

    while (1) {
        api->set_color(14, 0);
        api->print("Your guess: ");
        api->set_color(15, 0);

        char buf[8];
        int len = 0;
        while (1) {
            char c = api->getchar();
            if (c == '\n') {
                buf[len] = 0;
                api->putchar('\n');
                break;
            } else if (c == '\b') {
                if (len > 0) {
                    len--;
                    api->putchar('\b');
                }
            } else if (c >= '0' && c <= '9' && len < 4) {
                buf[len++] = c;
                api->putchar(c);
            } else if (c == 'q' || c == 27) {
                api->print("\nQuitting game...\n");
                return 0;
            }
        }

        if (len == 0) continue;

        attempts++;
        uint32_t val = 0;
        for (int i = 0; i < len; i++) {
            val = val * 10 + (buf[i] - '0');
        }

        if (val == secret) {
            api->set_color(10, 0);
            api->print("\n*** CORRECT! YOU WON! ***\n");
            api->beep(523, 100);
            api->beep(659, 100);
            api->beep(784, 150);
            api->beep(1047, 300);
            api->print("Attempts: ");
            char num_buf[8]; int nl = 0; int tmp = attempts;
            while (tmp > 0) { num_buf[nl++] = '0' + (tmp % 10); tmp /= 10; }
            while (nl > 0) api->putchar(num_buf[--nl]);
            api->print("\nPress any key to exit...");
            api->getchar();
            return 0;
        } else if (val < secret) {
            api->set_color(12, 0);
            api->print("Too low! Try higher.\n\n");
            api->beep(220, 80);
        } else {
            api->set_color(12, 0);
            api->print("Too high! Try lower.\n\n");
            api->beep(440, 80);
        }
    }
}
