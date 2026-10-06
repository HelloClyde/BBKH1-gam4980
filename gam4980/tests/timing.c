#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
uint32_t h1_raw_tick_80hz(void);
#include "../src/platform/timing.c"
static unsigned rate,missing,stopped;
static uint32_t reads;
uint32_t h1_wall_clock(void) { ++reads;return missing?0:100+(stopped?0:reads/10000); }
uint32_t h1_raw_tick_80hz(void) { return (uint32_t)(((uint64_t)reads*rate/10000u)+0xfffffff0u); }
void h1_diag(const char *format,...) { (void)format; }
static void check(unsigned r,unsigned expected) {
    rate=r;reads=0;cached_hz=0;assert(gam_host_tick_hz()==expected);
    unsigned before=reads;assert(gam_host_tick_hz()==expected&&reads==before);
    unsigned phase=0,frames=0;
    for(unsigned tick=0;tick<expected;++tick) {
        phase+=60;while(phase>=expected){phase-=expected;++frames;}
    }
    assert(frames==60&&phase==0); /* Correct 40 Hz -> alternating 1/2 frames. */
}
int main(void) {
    check(40,40);check(80,80);check(60,60);check(39,40);check(81,80);
    check(25,25);check(100,100);check(5,80);check(250,80);
    missing=1;check(40,80);missing=0;stopped=1;check(40,80);
    assert(reads<100000); /* Stopped RTC exits via raw tick deadline. */
    puts("PASS RTC tick calibration: 40/60/80 Hz, phase uncertainty, raw wrap, caching, missing/stopped RTC, invalid rates and exact 60 Hz scheduling");
}
