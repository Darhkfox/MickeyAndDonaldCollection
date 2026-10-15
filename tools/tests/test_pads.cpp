// Unit test for resolve_player_pad (src/app/settings.cpp): which controller slot each player uses.
// Build + run: tools\tests\run_test_pads.bat
#include "../../src/app/settings.h"
#include <cstdio>

static int failures = 0;

static void check(const char* what, int p1_choice, int p2_choice, const char* conn, int want1, int want2) {
    bool c[8];
    for (int i = 0; i < 8; i++) c[i] = conn[i] == '1';
    const int idx[2] = {p1_choice, p2_choice};
    const int got1 = resolve_player_pad(idx, c, 8, 0), got2 = resolve_player_pad(idx, c, 8, 1);
    const bool ok = got1 == want1 && got2 == want2;
    if (!ok) failures++;
    printf("%-4s %-62s P1 %2d P2 %2d (want %2d %2d)\n", ok ? "ok" : "FAIL", what, got1, got2, want1, want2);
}

int main() {
    //       description                                                 P1   P2   slots 1-8   P1  P2
    check("no pads",                                                     -1, -1, "00000000", -1, -1);
    check("one Xbox pad: player 1 only",                                 -1, -1, "10000000",  0, -1);
    check("two Xbox pads",                                               -1, -1, "11000000",  0,  1);
    check("Xbox pad + other USB pad",                                    -1, -1, "10001000",  0,  4);
    check("two other USB pads",                                          -1, -1, "00001100",  4,  5);
    check("Xbox pad in slot 2 only (slot 1 empty) + USB pad",            -1, -1, "01001000",  1,  4);
    check("P1 set to slot 2, P2 Auto: P2 gets slot 1, not P1's pad",      1, -1, "11000000",  1,  0);
    check("P2 set to slot 1, P1 Auto: P1 gets slot 2, not P2's pad",     -1,  0, "11000000",  1,  0);
    check("both set by hand",                                             4,  0, "10001000",  4,  0);
    check("P1 set to an unplugged slot, P2 Auto takes the first pad",     2, -1, "11000000",  2,  0);
    check("P2 set to slot 5, one Xbox pad: P1 Auto keeps slot 1",        -1,  4, "10001000",  0,  4);
    check("three pads",                                                  -1, -1, "11100000",  0,  1);
    printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
    return failures ? 1 : 0;
}
