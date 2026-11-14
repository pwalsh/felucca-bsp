/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Paul Walsh */
/* The SMK-37's encoders and faders (boards/smk37/fm1_input.h smk__encoders, smk__analog) on the host: the matrix
 * written as the scan leaves it (fm1_in.raw, 1 = closed) and the fader readings handed in one at a time, the
 * header's own fm1_input_init with the chip's register pages mapped (Linux mmap, as tests/audio_test.c).
 *  - a detent counts from the first one after power-on, each one as it lands (the stock 3-state window lost the
 *    first and was a detent late after it: fm1-emu, 2026-11-14), in both directions, from any rest state; a
 *    transition lost or two states in a frame still count the way of travel; jitter on the detent counts none;
 *    a knob parked a long time off the state it started in rests there (SMK_REST_FRAMES)
 *  - a fader moved slowly reaches the value its reading settles in, up and down (a +-1 dead band on the value
 *    stopped one short); a reading on a band's edge does not flicker; the ends reachable
 *
 *   sh tests/run_smk37_input_test.sh */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#if defined(__linux__)
#include <sys/mman.h>
#include "fm1_input.h"

static int fails;
static void want(const char *what, int ok)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

static int map(uintptr_t at, size_t n)
{
    void *p = mmap((void *)at, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return p == (void *)at;
}

/* ---- encoders: a state is A << 1 | B; + (B leads) goes 0 1 3 2 0, - goes 0 2 3 1 0 (the tables 0x4182 / 0x2814) */
static const uint8_t PLUS[4] = {1, 3, 0, 2}, MINUS[4] = {2, 0, 3, 1};   /* the next state from each */

static void put(uint32_t e, uint32_t s)
{
    uint32_t i;
    for (i = 0; i < 2; i++) {
        uint32_t c = SMK_ENCC[e][i], bit = 1u << (c & 15u), on = i == 0 ? s >> 1 & 1u : s & 1u;
        fm1_in.raw[c >> 4] = (uint8_t)(on ? fm1_in.raw[c >> 4] | bit : fm1_in.raw[c >> 4] & ~bit);
    }
}
static void frames(uint32_t n)
{
    while (n--) {
        smk__encoders();
        fm1_in.frames++;
    }
}
static uint32_t state(uint32_t e)
{
    return (uint32_t)(smk__closed(SMK_ENCC[e][0]) << 1 | smk__closed(SMK_ENCC[e][1]));
}
static void step(uint32_t e, int dir)              /* one transition, held 3 frames (a slow hand) */
{
    uint32_t s = state(e);
    put(e, dir > 0 ? PLUS[s] : MINUS[s]);
    frames(3);
}
static void detent(uint32_t e, int dir)
{
    int i;
    for (i = 0; i < 4; i++)
        step(e, dir);
}
static int take(uint32_t e)
{
    int s = fm1_in.enc_steps[e];
    fm1_in.enc_steps[e] = 0;
    return s;
}
static void power_on(uint32_t rest)                /* every encoder resting in REST */
{
    uint32_t e;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    fm1_input_init();
    for (e = 0; e < FM1_NENC; e++)
        put(e, rest);
    frames(5);
}

static void encoders(void)
{
    uint32_t rest, e, i, late = 0, wrong = 0;
    int n;
    for (rest = 0; rest < 4; rest++) {
        power_on(rest);
        detent(0, 1);
        n = take(0);
        char what[120];
        snprintf(what, sizeof what, "resting in state %u at power-on: the first detent + counts 1 (got %d)", rest, n);
        want(what, n == 1);
    }
    power_on(0);
    for (e = 0; e < FM1_NENC; e++)
        for (i = 0; i < 5; i++) {
            int d = i & 1 ? 1 : -1;
            detent(e, d);
            n = take(e);
            late += n == 0;
            wrong += n != 0 && n != d;
        }
    want("each of the 8 encoders: every detent counted as it lands, the way it turned (5 each, - and +)",
         !late && !wrong);
    power_on(0);
    for (i = 0; i < 6; i++)
        detent(3, -1);
    want("6 detents - in a row: -6", take(3) == -6);

    power_on(0);
    step(1, 1);                                    /* 0 -> 1 */
    put(1, 2);                                     /* 1 -> 2 in one frame: state 3 never seen */
    frames(3);
    step(1, 1);                                    /* 2 -> 0 */
    want("a state missed mid-detent (a two-state jump): counts the way of travel, +1", take(1) == 1);

    power_on(0);
    detent(2, -1);
    take(2);
    put(2, 3);                                     /* fast: 0 -> 3 straight after a click - */
    frames(1);
    step(2, -1);                                   /* 3 -> 1 */
    step(2, -1);                                   /* 1 -> 0 */
    want("a fast turn: a two-state jump off the detent right after a click goes its way, -1", take(2) == -1);

    power_on(0);
    for (i = 0; i < 20; i++) {
        step(4, 1);
        step(4, -1);
    }
    want("jitter on the detent (0 1 0 1 ...): no steps", take(4) == 0);

    power_on(0);
    step(5, 1);
    step(5, 1);                                    /* parked in state 3 */
    frames(SMK_REST_FRAMES + 10);
    take(5);
    detent(5, 1);
    n = take(5);
    detent(5, -1);
    want("parked off its power-on state for SMK_REST_FRAMES: rests there, a detent each way counts 1 each",
         n == 1 && take(5) == -1);
}

/* ---- faders: a reading at slot a * 3 + 2 (a < 4) is fader SMK_FADMUX[a]; one call, one reading */
static uint32_t changes;
static void read_fader(uint32_t raw, uint32_t n)
{
    while (n--) {
        uint8_t was = fm1_in.fader[1];
        smk__analog(0 * 3 + 2, raw);               /* mux address 0: fader 1 */
        changes += fm1_in.fader[1] != was;
    }
}
static void move(uint32_t *raw, uint32_t to)       /* one count at a time, 4 readings each: slower than the filter */
{
    while (*raw != to) {
        *raw = *raw < to ? *raw + 1u : *raw - 1u;
        read_fader(*raw, 4);
    }
    read_fader(to, 200);
}

static void faders(void)
{
    uint32_t raw = 404, f, bad = 0;
    power_on(0);
    read_fader(raw, 1);
    want("the first reading seeds the value (404: 50)", fm1_in.fader[1] == 50);
    for (f = 51; f <= 70; f++) {                   /* up, a value at a time, to the middle of its band */
        move(&raw, f * 8u + 4u);
        bad += fm1_in.fader[1] != f;
    }
    want("moved slowly up to 51 .. 70 (a reading in the middle of each band): each value reached", !bad);
    move(&raw, 61 * 8 + 4);
    want("and slowly back down from 70 to 61: 61", fm1_in.fader[1] == 61);
    bad = 0;
    for (f = 60; f >= 40; f--) {
        move(&raw, f * 8u + 4u);
        bad += fm1_in.fader[1] != f;
    }
    want("down a value at a time to 60 .. 40: each value reached", !bad);
    move(&raw, 40 * 8);
    changes = 0;
    for (f = 0; f < 200; f++)
        read_fader(f & 2 ? 40 * 8 + 2 : 40 * 8 - 2, 1); /* noise in pairs (through the median) across 39 | 40 */
    want("a reading on a band's edge, +-2 counts of noise (318 318 322 322 ...): changes at most once", changes <= 1);
    move(&raw, 1023);
    want("all the way up: 127", fm1_in.fader[1] == 127);
    move(&raw, 0);
    want("all the way down: 0", fm1_in.fader[1] == 0);
}

int main(void)
{
    /* the registers fm1_input_init writes: SPI2, ADC, the LED commons (0x11000..0x16FFF), the GPIO ports, IOMAP */
    if (!map(0x11000u, 0x6000u) || !map(0x50000u, 0x2000u)) {
        puts("  FAIL  the register pages did not map");
        return 1;
    }
    encoders();
    faders();
    printf("SMK-37 input: %s\n", fails ? "FAILED" : "all passed");
    return fails != 0;
}
#else
int main(void)
{
    puts("SMK-37 input: the fixture needs Linux mmap (tests/run_smk37_input_test.sh runs it in the host image)");
    return 0;
}
#endif
