#include "h1_sdk.h"
#include "timing.h"
#include "diagnostics.h"
#include <stdint.h>

extern uint32_t h1_wall_clock(void);
static unsigned cached_hz;
unsigned gam_host_tick_hz(void)
{
    if(cached_hz)return cached_hz;
    uint32_t rtc=h1_wall_clock(),raw=h1_raw_tick_80hz(),raw_begin=raw;
    uint32_t reference_rtc=0,reference_raw=0;
    unsigned iterations=0,observed=0,elapsed=0;
    const char *failure="RTC_unavailable";
    if(rtc)for(;;) {
        uint32_t next=h1_wall_clock(),tick=h1_raw_tick_80hz();
        if(!next||next<rtc){failure="RTC_changed";break;}
        if(next!=rtc) {
            if(!reference_rtc){reference_rtc=next;reference_raw=tick;}
            else {
                elapsed=next-reference_rtc;
                if(elapsed==1u)observed=tick-reference_raw;
                else failure="RTC_interval_invalid";
                break;
            }
            rtc=next;
        }
        if(tick-raw_begin>=240u){failure="raw_timeout";break;}
        if(++iterations>=128000000u){failure="iteration_limit";break;}
    }
    /* A tick concurrent with the RTC edge gives at most one count of phase
     * uncertainty. Normalize known 40/60/80 rates within two counts. */
    unsigned hz=observed;
    if(hz>=38&&hz<=42)hz=40;
    else if(hz>=58&&hz<=62)hz=60;
    else if(hz>=78&&hz<=82)hz=80;
    int valid=elapsed==1u&&hz>=20&&hz<=200;
    if(elapsed==1u&&!valid)failure="raw_rate_invalid";
    cached_hz=valid?hz:80;
    h1_diag("PACE_CLOCK source=RTC host_hz=%u observed_hz=%u rtc_seconds=%u valid=%u reason=%s",cached_hz,observed,elapsed,valid,valid?"measured":failure);
    return cached_hz;
}
