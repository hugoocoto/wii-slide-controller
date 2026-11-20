#define _POSIX_C_SOURCE 200809L // poll under -std=c99

#define INCLUDE_VIRT_IMPLEMENTATION
#include "virt.h"
#define INCLUDE_WIIMOTE_IMPLEMENTATION
#include "wiimote.h"

#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

// tilt below this (in g, 0.1 g is ~6 degrees) does nothing
#define TILT_DEADZONE 0.1f
// px per report at 1 g (remote on its side / pointing straight up)
#define TILT_MAX_SPEED 15.0f
// flip to -1 if a direction is reversed
#define TILT_SIGN_X 1
#define TILT_SIGN_Y -1

// tilt in g -> cursor speed in px per report, quadratic for precision
float
tilt_speed(float g)
{
        float mag = (g < 0 ? -g : g) - TILT_DEADZONE;
        if (mag <= 0) return 0;
        if (mag > 1 - TILT_DEADZONE) mag = 1 - TILT_DEADZONE;
        mag /= 1 - TILT_DEADZONE;
        float s = mag * mag * TILT_MAX_SPEED;
        return g < 0 ? -s : s;
}

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

// one connected Wiimote, numbered by its index in remotes[]
typedef struct remote {
        char mac[18];
        wr_socks socks;
        wr_state st;
        unsigned held;      // keys down in the last report
        float acc_x, acc_y; // sub-pixel leftovers
} remote;

// act on one report, only on keys that just went down (or up, for clicks);
// returns whether Home was pressed (rescan)
static int
handle_report(remote *rm, virt_dev kb, virt_dev mouse)
{
        unsigned pressed  = rm->st.keys & ~rm->held;
        unsigned released = rm->held & ~rm->st.keys;
        rm->held          = rm->st.keys;

        if (pressed & WR_KEY_A) {
                printf("Right\n");
                virt_kb_tap(kb, KEY_RIGHT);
        }

        if (pressed & WR_KEY_B) {
                printf("Left\n");
                virt_kb_tap(kb, KEY_LEFT);
        }

        if (pressed & WR_KEY_MINUS) {
                printf("Minus\n");
                int keys[] = { KEY_LEFTALT, KEY_SPACE };
                virt_kb_combo(kb, keys, sizeof(keys) / sizeof(keys[0]));
        }

        if (pressed & WR_KEY_PLUS) {
                printf("Plus\n");
                int keys[] = { KEY_LEFTALT, KEY_LEFTSHIFT, KEY_SPACE };
                virt_kb_combo(kb, keys, sizeof(keys) / sizeof(keys[0]));
        }

        // 1 left click, 2 right click, held while the key is (drag works)
        if (pressed & WR_KEY_1) virt_mouse_button(mouse, BTN_LEFT, 1);
        if (released & WR_KEY_1) virt_mouse_button(mouse, BTN_LEFT, 0);
        if (pressed & WR_KEY_2) virt_mouse_button(mouse, BTN_RIGHT, 1);
        if (released & WR_KEY_2) virt_mouse_button(mouse, BTN_RIGHT, 0);

        // tilt: roll left/right moves X, pointing up/down moves Y
        rm->acc_x += TILT_SIGN_X * tilt_speed(wr_accel_g(&rm->st, 0));
        rm->acc_y -= TILT_SIGN_Y * tilt_speed(wr_accel_g(&rm->st, 1));
        int dx = (int) rm->acc_x, dy = (int) rm->acc_y;
        rm->acc_x -= dx;
        rm->acc_y -= dy;
        if (dx || dy) virt_mouse_move(mouse, dx, dy);

        return (pressed & WR_KEY_HOME) != 0;
}

// a free slot has a negative fd, which poll ignores
static void
drop_remote(remote *remotes, struct pollfd *fds, int i)
{
        printf("[%d] disconnected\n", i);
        wr_fini(remotes[i].socks);
        fds[i].fd = -1;
}

// scan and connect the Wiimotes not connected yet, each in the lowest free
// slot; returns how many were added
static int
connect_remotes(remote *remotes, struct pollfd *fds)
{
        wr_found found[WR_SCAN_MAX];
        int n     = wr_scan(found, WR_SCAN_MAX);
        int added = 0;
        for (int i = 0; i < n; i++) {
                int slot = 0, known = 0;
                for (int j = 1; j < MAX_REMOTES; j++) {
                        if (fds[j].fd < 0) {
                                if (!slot) slot = j;
                        } else if (strcmp(remotes[j].mac, found[i].mac) == 0) {
                                known = 1;
                        }
                }
                if (known) continue;
                if (!slot) {
                        fprintf(stderr, "%s: no number left, skipped\n", found[i].mac);
                        continue;
                }

                remote *rm = &remotes[slot];
                if (wr_init(found[i].mac, &rm->socks) < 0) {
                        fprintf(stderr, "%s: could not connect, skipped\n", found[i].mac);
                        continue;
                }
                strcpy(rm->mac, found[i].mac);
                wr_state_init(&rm->st);
                rm->held  = WR_KEY_NONE;
                rm->acc_x = rm->acc_y = 0;
                wr_set_leds(rm->socks, remote_leds(slot));
                fds[slot].fd = rm->socks.data;
                printf("[%d] %s  %s\n", slot, found[i].mac, found[i].name);
                added++;
        }
        return added;
}

/*
 * Reports that queued up while scanning are stale: read them to keep the
 * keys current, without acting on them (held is left alone, so a key
 * released meanwhile still gets its release). Returns how many remotes
 * were found disconnected.
 */
static int
drain_remotes(remote *remotes, struct pollfd *fds)
{
        int dropped = 0;
        for (int i = 1; i < MAX_REMOTES; i++) {
                while (fds[i].fd >= 0 && poll(&fds[i], 1, 0) > 0) {
                        if (wr_read(remotes[i].socks, &remotes[i].st) < 0) {
                                drop_remote(remotes, fds, i);
                                dropped++;
                        }
                }
        }
        return dropped;
}

int
main()
{
        remote remotes[MAX_REMOTES];
        struct pollfd fds[MAX_REMOTES];
        for (int i = 0; i < MAX_REMOTES; i++) {
                fds[i].fd     = -1;
                fds[i].events = POLLIN;
        }

        printf("Press 1+2 on the Wiimotes, scanning...\n");
        int alive = connect_remotes(remotes, fds);
        if (alive == 0) {
                fprintf(stderr, "no Wiimote found\n");
                return 1;
        }

        virt_dev kb = virt_kb_init("Wiimando keyboard");
        assert(kb >= 0);
        virt_dev mouse = virt_mouse_init("Wiimando mouse");
        assert(mouse >= 0);
        virt_kb_ensure_connection();

        printf("Reading\n");

        // all remotes drive the same keyboard and mouse; a closed one is
        // dropped until none is left. Home on any of them rescans, which
        // blocks everything for the length of the inquiry
        while (alive > 0) {
                if (poll(fds, MAX_REMOTES, -1) < 0) {
                        perror("poll");
                        break;
                }
                int rescan = 0;
                for (int i = 1; i < MAX_REMOTES; i++) {
                        if (fds[i].fd < 0 || !fds[i].revents) continue;
                        int r = wr_read(remotes[i].socks, &remotes[i].st);
                        if (r < 0) {
                                drop_remote(remotes, fds, i);
                                alive--;
                        } else if (r > 0) {
                                rescan |= handle_report(&remotes[i], kb, mouse);
                        }
                }
                if (rescan) {
                        printf("Press 1+2 on the new Wiimotes, scanning...\n");
                        alive += connect_remotes(remotes, fds);
                        alive -= drain_remotes(remotes, fds);
                        printf("Reading\n");
                }
        }

        for (int i = 1; i < MAX_REMOTES; i++)
                if (fds[i].fd >= 0) wr_fini(remotes[i].socks);
        virt_kb_fini(kb);
        virt_mouse_fini(mouse);

        return 0;
}
