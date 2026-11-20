#define _POSIX_C_SOURCE 200809L // poll under -std=c99

#define INCLUDE_VIRT_IMPLEMENTATION
#include "virt.h"
#define INCLUDE_WIIMOTE_IMPLEMENTATION
#include "wiimote.h"

#include <assert.h>
#include <poll.h>
#include <stdio.h>

// remote num in binary on its LEDs, read right to left: LED 4 is bit 0
static unsigned char
remote_leds(int num)
{
        unsigned char leds = 0;
        if (num & 1) leds |= WR_LED_4;
        if (num & 2) leds |= WR_LED_3;
        if (num & 4) leds |= WR_LED_2;
        if (num & 8) leds |= WR_LED_1;
        return leds;
}

// 4 LEDs show the number in binary, 0 is skipped so every remote has at
// least 1 LED on: numbers 1 to 15
#define MAX_REMOTES 16

int
main()
{
        wr_socks socks[MAX_REMOTES];
        wr_state st[MAX_REMOTES];
        unsigned held[MAX_REMOTES]; // keys down in the last report
        struct pollfd fds[MAX_REMOTES];
        for (int i = 0; i < MAX_REMOTES; i++) {
                fds[i].fd     = -1; // a free slot, which poll ignores
                fds[i].events = POLLIN;
        }

        printf("Press 1+2 on the Wiimotes, scanning...\n");
        wr_found found[WR_SCAN_MAX];
        int n     = wr_scan(found, WR_SCAN_MAX);
        int alive = 0;
        for (int i = 0; i < n && alive < MAX_REMOTES - 1; i++) {
                int slot = alive + 1;
                if (wr_init(found[i].mac, &socks[slot]) < 0) {
                        fprintf(stderr, "%s: could not connect, skipped\n", found[i].mac);
                        continue;
                }
                wr_state_init(&st[slot]);
                held[slot] = WR_KEY_NONE;
                wr_set_leds(socks[slot], remote_leds(slot));
                fds[slot].fd = socks[slot].data;
                printf("[%d] %s  %s\n", slot, found[i].mac, found[i].name);
                alive++;
        }
        if (alive == 0) {
                fprintf(stderr, "no Wiimote found\n");
                return 1;
        }

        virt_dev kb = virt_kb_init("Wii slide controller");
        assert(kb >= 0);
        virt_kb_ensure_connection();

        printf("Reading\n");

        // all remotes drive the same keyboard: A is Right, B is Left, on
        // press only. A closed one is dropped until none is left
        while (alive > 0) {
                if (poll(fds, MAX_REMOTES, -1) < 0) {
                        perror("poll");
                        break;
                }
                for (int i = 1; i < MAX_REMOTES; i++) {
                        if (fds[i].fd < 0 || !fds[i].revents) continue;
                        int r = wr_read(socks[i], &st[i]);
                        if (r < 0) {
                                printf("[%d] disconnected\n", i);
                                wr_fini(socks[i]);
                                fds[i].fd = -1;
                                alive--;
                                continue;
                        }
                        if (r == 0) continue;
                        unsigned pressed = st[i].keys & ~held[i];
                        held[i]          = st[i].keys;
                        if (pressed & WR_KEY_A) virt_kb_tap(kb, KEY_RIGHT);
                        if (pressed & WR_KEY_B) virt_kb_tap(kb, KEY_LEFT);
                }
        }

        for (int i = 1; i < MAX_REMOTES; i++)
                if (fds[i].fd >= 0) wr_fini(socks[i]);
        virt_kb_fini(kb);

        return 0;
}
