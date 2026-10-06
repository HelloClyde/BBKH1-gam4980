#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define H1_PROFILE 1
#define H1_PROFILE_HOST_TEST 1
uint32_t h1_profile_test_clock;
static uint32_t wall=100,raw;
uint32_t h1_profile_test_raw_tick(void) { return ++raw; }
#include "../src/platform/profile.c"
uint32_t h1_wall_clock(void) { return wall; }
void h1_diag(const char *format,...) { (void)format; }
void h1_diag_batch_begin(void) {}
void h1_diag_batch_end(void) {}
static void start(void) {
    frequency=32768;h1_profile_begin();h1_profile_start();
    assert(enabled && !fault && !depth);
}
static void core(unsigned ticks) {
    h1_profile_push(HP_CORE);h1_profile_test_clock+=ticks;
    h1_profile_pop();h1_profile_core_frame();
}
int main(void) {
    start();h1_profile_push(HP_OTHER);
    h1_profile_test_clock+=10;h1_profile_push(HP_INPUT);
    h1_profile_test_clock+=20;h1_profile_input(2,38);h1_profile_pop();
    core(548);core(549);core(547); /* One catch-up loop, three logic frames. */
    h1_profile_push(HP_VIDEO);h1_profile_test_clock+=30;
    h1_profile_video(3,397*8,0);h1_profile_pop();
    h1_profile_push(HP_SAVE);h1_profile_test_clock+=40;
    h1_profile_save(1);h1_profile_pop();
    h1_profile_test_clock+=5;h1_profile_pop();
    h1_profile_iteration(1,3,1);
    assert(current.frames==3 && current.iterations==1 && current.displayed==1);
    assert(current.core_sum==1644 && current.core_max==549 && current.over_budget==3);
    assert(current.ticks[HP_OTHER]==15 && current.ticks[HP_INPUT]==20);
    assert(current.ticks[HP_VIDEO]==30 && current.ticks[HP_SAVE]==40);
    assert(current.input_queries==38 && current.input_events==2 && current.video_rows==3);
    assert(current.video_pixels==397*8 && current.host_ticks==3 && current.dropped_ticks==1);
    assert(current.saves_written==1 && !current.saves_failed);
    assert(rtc_run_frames==3 && rtc_run_displayed==1 && !rtc_run_invalid);
    /* Empty host ticks contribute pacing/input costs without logic frames. */
    h1_profile_push(HP_WAIT);h1_profile_test_clock+=100;h1_profile_pop();
    h1_profile_iteration(0,1,0);assert(current.iterations==2 && current.frames==3);
    for(unsigned i=3;i<301;++i) core(100);
    assert(!window_count);h1_profile_iteration(0,8,0);
    assert(window_count==1 && windows[0].frames==301 && !current.frames);
    h1_profile_dump("pause");assert(!enabled && !depth);
    wall+=10;start();assert(rtc_run_frames==0); /* Menu time excluded. */
    h1_profile_push(HP_VIDEO);wall+=3;h1_profile_pop();
    assert(!enabled && fault==2); /* Long output/save stalls rejected at scope boundary. */
    h1_profile_dump("long_stall");
    start();for(unsigned i=0;i<17;++i)h1_profile_push(HP_CORE);
    assert(!enabled && fault==1);h1_profile_dump("scope_overflow");
    wall=0;frequency=0;h1_profile_begin();assert(!enabled && !prepared);
    wall=100;h1_profile_core_frame();wall=120;h1_profile_core_frame();
    assert(rtc_run_frames==2 && rtc_run_start==100 && rtc_run_last==120);
    h1_profile_dump("coarse_fallback");
    puts("PASS GAM exclusive costs, catch-up/empty ticks, video/input/save counters, 60 Hz budget, complete windows, pause/resume, long stalls and RTC fallback");
}
