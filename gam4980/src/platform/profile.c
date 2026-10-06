#include "profile.h"
#if H1_PROFILE
#include "h1_sdk.h"
#include "diagnostics.h"
#include <string.h>
#include <stdio.h>

/* TCU wall time includes IRQ/OS scheduling in the interrupted scope. */
#ifndef H1_PROFILE_HOST_TEST
#include "profile_clock.h"
#else
static int pc_start(void) { return 1; }
static void pc_stop(void) {}
static unsigned pc_fault;
#endif
typedef struct {
    uint64_t ticks[HP_COUNT], core_sum;
    unsigned calls[HP_COUNT], histogram[64], frames, displayed, over_budget;
    uint32_t core_max;
    unsigned loop_under_2ms, loop_over_25ms;
    unsigned iterations,host_ticks,dropped_ticks,input_events,input_queries;
    uint64_t video_pixels;
    unsigned video_rows,video_full;
    unsigned saves_written,saves_failed;
} profile_window;
static profile_window current, windows[12];
static unsigned stack[16], depth, enabled, window_count, omitted, frequency, fault;
static unsigned clock_attempted, prepared;
static uint32_t last, frame_rtc;
static uint32_t sample_rtc;
static uint32_t run_start_rtc, fault_previous_rtc, fault_current_rtc;
static unsigned fault_frame;
static uint32_t rtc_run_start,rtc_run_last;
static unsigned rtc_run_frames,rtc_run_displayed,rtc_run_invalid;
static uint64_t accounted;
static const char *names[HP_COUNT] = {"core_6502", "lcd_capture", "screen_output",
    "input_poll", "save_checkpoint", "pacing_wait", "loop_other"};
extern uint32_t h1_wall_clock(void);
uint32_t h1_profile_now(void)
{
    if (enabled) {
        uint32_t rtc=h1_wall_clock();
        if (!rtc || rtc<sample_rtc || rtc-sample_rtc>=2u) {
            fault_previous_rtc=sample_rtc;fault_current_rtc=rtc;
            fault_frame=rtc_run_frames;fault=2;enabled=0;return last;
        }
        sample_rtc=rtc;
    }
#ifdef H1_PROFILE_HOST_TEST
    extern uint32_t h1_profile_test_clock;
    return h1_profile_test_clock;
#else
    uint32_t value=pc_now();
    if (pc_fault) { enabled=0;fault=3; }
    return value;
#endif
}
static uint32_t raw_tick(void)
{
#ifdef H1_PROFILE_HOST_TEST
    extern uint32_t h1_profile_test_raw_tick(void);
    return h1_profile_test_raw_tick();
#else
    return h1_raw_tick_80hz();
#endif
}
typedef struct {
    uint32_t rtc_start,rtc_end,counter_start,counter_end,raw_start,raw_end;
    unsigned iterations;
    const char *outcome;
} clock_observation;
static void wait_rtc_edge(clock_observation *p)
{
    memset(p,0,sizeof *p);
    p->rtc_start=h1_wall_clock();p->counter_start=h1_profile_now();
    p->raw_start=p->raw_end=raw_tick();
    for (;;) {
        p->rtc_end=h1_wall_clock();p->counter_end=h1_profile_now();
        if (pc_fault) { p->outcome="counter_read_fault";break; }
        if (p->rtc_end!=p->rtc_start) { p->outcome="RTC_edge";break; }
        if (p->counter_end-p->counter_start>=65536u) { p->outcome="counter_timeout";break; }
        ++p->iterations;
        if (!(p->iterations&255u)) p->raw_end=raw_tick();
        if (p->raw_end-p->raw_start>=240u) { p->outcome="raw_tick_timeout";break; }
        /* Last resort when both hardware and firmware clocks are stopped.
         * This bound is deliberately much larger than the old 4M iterations. */
        if (p->iterations>=128000000u) { p->outcome="iteration_limit";break; }
    }
    p->raw_end=raw_tick();
}
static void log_observation(const char *phase, const clock_observation *p)
{
    h1_diag("PROFILE_CLOCK_PROBE phase=%s rtc_start=%u rtc_end=%u counter_start=%u counter_end=%u counter_delta=%u raw_ticks=%u iterations=%u outcome=%s",phase,p->rtc_start,p->rtc_end,p->counter_start,p->counter_end,p->counter_end-p->counter_start,p->raw_end-p->raw_start,p->iterations,p->outcome);
}
static void log_tcu_state(const char *stage)
{
#ifndef H1_PROFILE_HOST_TEST
    h1_diag("PROFILE_TCU stage=%s owned=%u enabled=%08X stopped=%08X flags=%08X mask=%08X",stage,pc_owned,pc_read(0x10,1),pc_read(0x1c,4),pc_read(0x20,4),pc_read(0x30,4));
    if (pc_owned) {
        /* Only read channel registers while its clock is supplied. */
        h1_diag("PROFILE_TCU_REG stage=%s full=%u half=%u counter=%u control=%04X saved_full=%u saved_half=%u saved_counter=%u saved_control=%04X",stage,pc_read(PC_BASE,2),pc_read(PC_BASE+4,2),pc_read(PC_BASE+8,2),pc_read(PC_BASE+12,2),pc_saved[0],pc_saved[1],pc_saved[2],pc_saved[3]);
    }
#else
    (void)stage;
#endif
}
static void log_tcu_reads(const char *stage)
{
#ifndef H1_PROFILE_HOST_TEST
    h1_diag("PROFILE_TCU_READS stage=%s fault=%u retries=%u backwards=%u wraps=%u deferred=%u terminal=%u period=%u previous=%u total=%u max_delta=%u bad_a=%u bad_b=%u bad_previous=%u",stage,pc_fault,pc_retries,pc_backwards,pc_wraps,pc_deferred,pc_terminal,PC_PERIOD,pc_previous,pc_total,pc_max_delta,pc_bad_a,pc_bad_b,pc_bad_previous);
#else
    (void)stage;
#endif
}
void h1_profile_begin(void)
{
    uint32_t tick, elapsed, observed;
    clock_observation align,measure;
    enabled = depth = window_count = omitted = fault = prepared = 0;
    run_start_rtc=fault_previous_rtc=fault_current_rtc=fault_frame=0;
    rtc_run_start=rtc_run_last=rtc_run_frames=rtc_run_displayed=rtc_run_invalid=0;
    memset(&current, 0, sizeof current);
    accounted=0;
    tick=h1_wall_clock();
    if (!tick) { h1_diag("PROFILE_CLOCK ready=0 reason=RTC_unavailable");return; }
    if (!frequency && clock_attempted) {
        h1_diag("PROFILE_CLOCK ready=0 reason=previous_clock_failure");return;
    }
    if (!pc_start()) {
        log_tcu_state("busy");h1_diag("PROFILE_CLOCK ready=0 reason=TCU5_busy");return;
    }
    frame_rtc=tick;
    if (frequency) {
        h1_diag("PROFILE_CLOCK source=TCU5_RTC hz=%u resolution_us=31 ready=1 reused=1",frequency);
        prepared=1;pc_stop();
        return;
    }
    clock_attempted=1;
    log_tcu_state("started");
    wait_rtc_edge(&align);
    if (pc_fault || align.rtc_end==align.rtc_start || !align.rtc_end) {
        log_observation("align",&align);log_tcu_state("align_failed");
        log_tcu_reads("align_failed");
        pc_stop();h1_diag("PROFILE_CLOCK ready=0 reason=%s",pc_fault ? "TCU_read_failed" : "RTC_alignment_failed");return;
    }
    /* Do not write logs between the two edges: storage latency would shorten
     * the reference second and falsely classify a healthy counter as slow. */
    wait_rtc_edge(&measure);
    elapsed=measure.rtc_end-measure.rtc_start;
    observed=elapsed && elapsed<3 ? (measure.counter_end-measure.counter_start)/elapsed : 0;
    log_observation("align",&align);log_observation("measure",&measure);
    frequency=!pc_fault && observed>=32000u && observed<=33500u ? 32768u : 0;
    prepared=frequency!=0;
    h1_diag("PROFILE_CLOCK source=TCU5_RTC hz=%u observed_hz=%u calibration_RTC_seconds=%u resolution_us=31 ready=%u",frequency,observed,elapsed,prepared);
    if (!prepared) log_tcu_state("frequency_failed");
    log_tcu_reads(prepared ? "calibrated" : "frequency_failed");
    pc_stop();
}
void h1_profile_start(void)
{
    if (!prepared) return;
    prepared=0;
    /* Audio/loading/file logs can take several seconds after calibration.
     * Reacquire a fresh counter only at the game-loop boundary: a baseline
     * read alone cannot recover multiple unobserved 16-bit wraps. No IO here. */
    if (!pc_start()) { fault=4;return; }
    frame_rtc=run_start_rtc=h1_wall_clock();
    if (!frame_rtc) { fault=2;pc_stop();return; }
    sample_rtc=frame_rtc;enabled=1;last=h1_profile_now();
}
static void charge(uint32_t now)
{
    if (depth) current.ticks[stack[depth-1]] += (uint32_t)(now-last);
    last = now;
}
void h1_profile_push(unsigned kind)
{
    if (!enabled) return;
    charge(h1_profile_now());
    if (!enabled) return;
    if (depth == 16 || kind >= HP_COUNT) { enabled=0;fault=1;return; }
    stack[depth++] = kind; ++current.calls[kind];
}
void h1_profile_pop(void)
{
    if (!enabled) return;
    charge(h1_profile_now());
    if (!enabled) return;
    if (!depth) { enabled=0;fault=1;return; }
    --depth;
}
static void archive(void)
{
    if (!current.iterations && !current.frames) return;
    if (window_count == 12) {
        memmove(windows, windows+1, 11*sizeof *windows);
        --window_count; ++omitted;
    }
    windows[window_count++] = current;
    memset(&current, 0, sizeof current);
    accounted=0;
}
void h1_profile_core_frame(void)
{
    unsigned ms;
    uint32_t rtc=h1_wall_clock();
    /* Independent coarse measurement survives a failed high-resolution clock.
     * Frame endpoints exclude loading, calibration, menu and log writes. */
    if (!rtc || (rtc_run_frames && rtc<rtc_run_last)) rtc_run_invalid=1;
    if (!rtc_run_frames) rtc_run_start=rtc;
    rtc_run_last=rtc;++rtc_run_frames;
    if (!enabled) return;
    /* Reject possible multiple 16-bit wraps after a long unsampled stall. */
    if (frame_rtc && (!rtc || rtc-frame_rtc>=2u)) {
        fault_previous_rtc=frame_rtc;fault_current_rtc=rtc;
        fault_frame=rtc_run_frames;fault=2;enabled=0;return;
    }
    frame_rtc=rtc;
    uint32_t core_ticks=(uint32_t)(current.ticks[HP_CORE]-current.core_sum);
    ++current.frames;
    current.core_sum += core_ticks;
    if (core_ticks > current.core_max) current.core_max = core_ticks;
    if ((uint64_t)core_ticks*60u > frequency) ++current.over_budget;
    ms = (unsigned)((uint64_t)core_ticks*1000u/frequency);
    ++current.histogram[ms < 63 ? ms : 63];
}
void h1_profile_iteration(unsigned displayed, unsigned host_ticks, unsigned dropped)
{
    rtc_run_displayed+=displayed;
    if (!enabled) return;
    uint64_t sum=0;
    for (unsigned k=0;k<HP_COUNT;++k) sum+=current.ticks[k];
    uint64_t delta=sum-accounted;accounted=sum;
    if (delta*1000u < (uint64_t)frequency*2u) ++current.loop_under_2ms;
    if (delta*1000u > (uint64_t)frequency*25u) ++current.loop_over_25ms;
    ++current.iterations;current.displayed+=displayed;
    current.host_ticks+=host_ticks;current.dropped_ticks+=dropped;
    /* Archive complete host iterations, including their final LCD/output work.
     * A catch-up iteration may cross 300 frames; never split its costs. */
    if (current.frames>=300) archive();
}
void h1_profile_input(unsigned events,unsigned queries)
{
    if (!enabled) return;
    current.input_events+=events;current.input_queries+=queries;
}
void h1_profile_video(unsigned rows,unsigned pixels,unsigned full)
{
    if (!enabled) return;
    current.video_rows+=rows;current.video_pixels+=pixels;current.video_full+=full;
}
void h1_profile_save(int status)
{
    if (!enabled) return;
    current.saves_written+=status>0;current.saves_failed+=status<0;
}
static unsigned microseconds(uint64_t ticks) { return frequency ? (unsigned)(ticks*1000000u/frequency) : 0; }
void h1_profile_dump(const char *reason)
{
    uint32_t dump_tick=raw_tick(),dump_rtc=h1_wall_clock();
    unsigned dumped;
    if (depth && !fault) { fault=1;enabled=0; }
    archive();
    pc_stop(); /* Release before menu/file IO and on every normal exit. */
    enabled=0;
    dumped=window_count;
    h1_diag_batch_begin();
    log_tcu_reads(reason);
    h1_diag("PROFILE_RUN_BOUNDARY reason=%s start_rtc=%u fault_frame=%u fault_previous_rtc=%u fault_current_rtc=%u",reason,run_start_rtc,fault_frame,fault_previous_rtc,fault_current_rtc);
    h1_diag("PROFILE_RTC_RUN reason=%s frames=%u displayed=%u interval_frames=%u rtc_start=%u rtc_end=%u seconds=%u resolution_seconds=1 valid=%u",reason,rtc_run_frames,rtc_run_displayed,rtc_run_frames?rtc_run_frames-1:0,rtc_run_start,rtc_run_last,rtc_run_last>=rtc_run_start?rtc_run_last-rtc_run_start:0,rtc_run_frames>1 && !rtc_run_invalid);
    h1_diag("PROFILE_BEGIN reason=%s windows=%u omitted=%u hz=%u fault=%u exclusive=1",reason,window_count,omitted,frequency,fault);
    for (unsigned w=0;w<window_count;++w) {
        profile_window *p=&windows[w]; uint64_t sum=0;
        unsigned cumulative=0, p95=0;
        for (unsigned k=0;k<HP_COUNT;++k) sum+=p->ticks[k];
        for (;p95<63;++p95) { cumulative+=p->histogram[p95]; if (cumulative*100u >= p->frames*95u) break; }
        h1_diag("PROFILE_WINDOW index=%u frames=%u displayed=%u elapsed_us=%u core_mean_us=%u core_max_us=%u core_p95_bin_ms=%u over_16_667ms=%u loop_under_2ms=%u loop_over_25ms=%u",w+omitted,p->frames,p->displayed,microseconds(sum),microseconds(p->frames?p->core_sum/p->frames:0),microseconds(p->core_max),p95,p->over_budget,p->loop_under_2ms,p->loop_over_25ms);
        h1_diag("PROFILE_GAM_DETAIL window=%u iterations=%u host_ticks=%u dropped_ticks=%u input_events=%u input_queries=%u video_rows=%u video_pixels=%llu full_redraws=%u saves_written=%u saves_failed=%u",w+omitted,p->iterations,p->host_ticks,p->dropped_ticks,p->input_events,p->input_queries,p->video_rows,(unsigned long long)p->video_pixels,p->video_full,p->saves_written,p->saves_failed);
        char bins[320];size_t used=0;
        for(unsigned i=0;i<64;++i)used+=(size_t)snprintf(bins+used,sizeof bins-used,"%s%u",i?",":"",p->histogram[i]);
        h1_diag("PROFILE_CORE_HIST window=%u bins=%s overflow_bin_ms=63",w+omitted,bins);
        for (unsigned k=0;k<HP_COUNT;++k)
            h1_diag("PROFILE_COST window=%u name=%s ticks=%llu us=%u calls=%u",w+omitted,names[k],(unsigned long long)p->ticks[k],microseconds(p->ticks[k]),p->calls[k]);
    }
    h1_diag("PROFILE_END"); window_count=0;
    h1_diag_batch_end();
    h1_diag("PROFILE_DUMP_IO reason=%s windows=%u raw_ticks=%u rtc_seconds=%u",reason,dumped,raw_tick()-dump_tick,h1_wall_clock()-dump_rtc);
}
#endif
