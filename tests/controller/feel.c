// Host-side check of the encoder feel curve for the Lights definition.
//   tests/run.sh                      (runs everything)
//   build/feel [clicks.txt]           with a file of recorded "dir dt_ms" lines, replays them
#include <stdio.h>
#include "devices.h"

static int spin(int start, int dir, int clicks, int spacing_ms)
{
    device_t d; device_init(&d, &DEVICE_LIGHTS, (device_val_t){ true, start });
    uint32_t t = 1000;
    for (int i = 0; i < clicks; i++) { device_turn(&d, dir, t); t += spacing_ms; }
    return d.requested.value;
}

static void replay(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return;
    device_t d; device_init(&d, &DEVICE_LIGHTS, (device_val_t){ true, 60 });
    int dir; unsigned dt; uint32_t t = 1000; int n = 0, last = 60;
    printf("\nreplay of %s (value(step) per click):\n", path);
    while (fscanf(f, "%d %u", &dir, &dt) == 2) {
        t += dt;
        device_turn(&d, dir, t);
        printf("%s%d(%+d)", n % 12 ? " " : "\n  ", d.requested.value, d.requested.value - last);
        last = d.requested.value; n++;
    }
    printf("\n");
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc > 1) { replay(argv[1]); return 0; }
    int failures = 0;
    const device_feel_t *feel = &DEVICE_LIGHTS.feel;

    printf("gain at rate: ");
    for (float r = 0; r <= 50; r += 10) printf("%.0fHz=%.2fx  ", r, device_gain_for_rate(feel, r));
    printf("\n\nTravel from 100%% going down (clicks @ spacing -> final):\n");
    const int spacings[] = { 200, 100, 50, 33, 25, 15 };
    const int counts[] = { 5, 10, 20, 30, 50 };
    printf("spacing  rate");
    for (int c = 0; c < 5; c++) printf("  %2d clicks", counts[c]);
    printf("\n");
    for (int s = 0; s < 6; s++) {
        printf("%4dms %4dHz", spacings[s], 1000 / spacings[s]);
        for (int c = 0; c < 5; c++) printf("  %3d->%3d ", 100, spin(100, -1, counts[c], spacings[s]));
        printf("\n");
    }

    // Properties the feel must keep (tuned on the device):
    if (spin(50, +1, 10, 200) != 60) { printf("FAIL: slow clicks must be exactly 1 unit each\n"); failures++; }
    if (device_gain_for_rate(feel, 0) != 1.0f) { printf("FAIL: gain at rest must be 1x\n"); failures++; }
    if (device_gain_for_rate(feel, 100) > feel->gain_max + 0.001f) { printf("FAIL: gain must not exceed gain_max\n"); failures++; }
    if (spin(100, -1, 20, 33) < 30) { printf("FAIL: 20 brisk clicks must not sweep most of the range\n"); failures++; }
    if (spin(0, -1, 5, 100) != 0 || spin(100, +1, 5, 100) != 100) { printf("FAIL: must clamp at the limits\n"); failures++; }
    printf(failures ? "feel: %d FAILED\n" : "feel: all passed\n", failures);
    return failures != 0;
}
