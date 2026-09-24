#include "../khex.h"

int _start(const khex_api_t* api) {
    api->clear();
    api->set_color(10, 0);
    api->print("=== MATRIX DIGITAL RAIN ===\n");
    api->print("Press any key to exit...\n");
    api->delay(800);
    api->clear();

    uint8_t drops[80];
    uint8_t speed[80];

    for (int x = 0; x < 80; x++) {
        drops[x] = (uint8_t)(api->rand() % 25);
        speed[x] = (uint8_t)((api->rand() % 3) + 1);
    }

    uint32_t ticks = 0;
    while (!api->has_char()) {
        ticks++;

        for (int x = 0; x < 80; x++) {
            if ((ticks % speed[x]) != 0) continue;

            int y = drops[x];

            if (y > 0 && y <= 24) {
                char old_ch = (char)(33 + (api->rand() % 90));
                api->draw_char(y - 1, x, old_ch, 2); /* Dark Green */
            }
            if (y > 5 && y - 5 <= 24) {
                api->draw_char(y - 5, x, ' ', 0); /* Erase */
            }

            if (y < 25) {
                char ch = (char)(33 + (api->rand() % 90));
                api->draw_char(y, x, ch, 10); /* Bright Green Head */
            }

            drops[x]++;
            if (drops[x] >= 25 + (api->rand() % 8)) {
                drops[x] = 0;
                speed[x] = (uint8_t)((api->rand() % 3) + 1);
            }
        }

        api->delay(35);
    }

    /* Drain input key */
    api->getchar();
    api->clear();
    api->set_color(7, 0);
    return 0;
}
