#ifndef GAM_TIMING_H
#define GAM_TIMING_H
/* Firmware/SDK naming does not establish the tick rate. Measure against RTC.
 * Calibration is cached for this application session, before game timing. */
unsigned gam_host_tick_hz(void);
#endif
