/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "input.h"
#include "timer.h"

static volatile unsigned long *bios_ticks = (unsigned long *)MK_FP(0x40, 0x6C);

/* Fast mode (timer_hook): channel 0 runs at 4 x 18.2 Hz; the BIOS handler
   still gets every 4th interrupt. */
#define FAST_DIV 16384u

static void (__interrupt __far *old08)(void);
static void (*tick_hook)(void);
static volatile unsigned long fast_ticks;
static volatile unsigned char sub_tick, in_hook;
static unsigned char isr_stack[640];

/* The hook runs on its own stack in DGROUP (SS = DS as the compiler
   assumes), with interrupts enabled. */
void stack_enter(unsigned seg, unsigned top);
#pragma aux stack_enter = \
    "mov bx, ss" \
    "mov cx, sp" \
    "mov ss, ax" \
    "mov sp, dx" \
    "push bx" \
    "push cx" \
    parm [ax] [dx] modify [bx cx];
void stack_leave(void);
#pragma aux stack_leave = \
    "pop cx" \
    "pop bx" \
    "mov ss, bx" \
    "mov sp, cx" \
    modify [bx cx];

static void __interrupt __far timer_isr(void)
{
    fast_ticks++;
    if (--sub_tick == 0) {
        sub_tick = 4;
        (*old08)();                 /* BIOS clock; it sends the EOI */
    } else {
        outp(0x20, 0x20);
    }
    if (tick_hook && !in_hook) {
        in_hook = 1;
        stack_enter(FP_SEG(isr_stack), FP_OFF(isr_stack) + sizeof isr_stack);
        _enable();
        tick_hook();
        _disable();
        stack_leave();
        in_hook = 0;
    }
}

static void pit_divisor(unsigned div)
{
    outp(0x43, 0x34);           /* channel 0, lo/hi, mode 2 */
    outp(0x40, div & 0xFF);
    outp(0x40, div >> 8);
}

void timer_init(void)
{
    _disable();
    pit_divisor(0);
    _enable();
}

void timer_hook(void (*fn)(void))
{
    if (fn && !old08) {
        old08 = _dos_getvect(8);
        _disable();
        fast_ticks = *bios_ticks << 2;
        sub_tick = 4;
        tick_hook = fn;
        _dos_setvect(8, timer_isr);
        pit_divisor(FAST_DIV);
        _enable();
    } else if (!fn && old08) {
        _disable();
        pit_divisor(0);
        _dos_setvect(8, old08);
        old08 = 0;
        tick_hook = 0;
        _enable();
    } else {
        tick_hook = fn;
    }
}

void timer_shutdown(void)
{
    timer_hook(0);
    _disable();
    outp(0x43, 0x36);           /* channel 0, lo/hi, mode 3 (BIOS default) */
    outp(0x40, 0);
    outp(0x40, 0);
    _enable();
}

unsigned long timer_read(void)
{
    unsigned long ticks;
    unsigned lo, hi, count, irr;

    _disable();
    outp(0x43, 0x00);           /* latch channel 0 */
    lo = inp(0x40);
    hi = inp(0x40);
    if (old08) {
        ticks = fast_ticks;
        outp(0x20, 0x0A);       /* read the PIC's request register */
        irr = inp(0x20);
        _enable();
        count = (hi << 8) | lo;
        /* Counter wrapped but its interrupt is still pending. */
        if ((irr & 1) && count > FAST_DIV / 2)
            ticks++;
        return (ticks << 14) + (FAST_DIV - count);
    }
    ticks = *bios_ticks;
    _enable();
    count = (hi << 8) | lo;
    return (ticks << 16) + (unsigned)(0u - count);
}

unsigned long timer_ms(unsigned long counts)
{
    return counts / 1193UL;
}

int g_pace_log;

/* Resident, not in the battle core: the core has no room (build.ps1). */
void timer_pace(unsigned long t0, unsigned ms, int what)
{
    unsigned long lim = ms * 1193UL;

    while (timer_read() - t0 < lim)
        ;
    if (g_pace_log && what)
        log_printf("PACE %c %u %lu", what, ms, (timer_read() - t0) / 1193UL);
}
