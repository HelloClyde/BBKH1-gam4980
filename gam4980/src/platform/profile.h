#ifndef GAM_H1_PROFILE_H
#define GAM_H1_PROFILE_H
#include <stdint.h>
#ifndef H1_PROFILE
#define H1_PROFILE 0
#endif
enum h1_profile_kind { HP_CORE, HP_CAPTURE, HP_VIDEO, HP_INPUT, HP_SAVE,
    HP_WAIT, HP_OTHER, HP_COUNT };
#if H1_PROFILE
uint32_t h1_profile_now(void);
void h1_profile_begin(void);
void h1_profile_start(void);
void h1_profile_push(unsigned kind);
void h1_profile_pop(void);
void h1_profile_core_frame(void);
void h1_profile_iteration(unsigned displayed, unsigned host_ticks, unsigned dropped);
void h1_profile_input(unsigned events, unsigned queries);
void h1_profile_video(unsigned rows, unsigned pixels, unsigned full);
void h1_profile_save(int status);
void h1_profile_dump(const char *reason);
#else
static inline uint32_t h1_profile_now(void) { return 0; }
static inline void h1_profile_begin(void) {}
static inline void h1_profile_start(void) {}
static inline void h1_profile_push(unsigned kind) { (void)kind; }
static inline void h1_profile_pop(void) {}
static inline void h1_profile_core_frame(void) {}
static inline void h1_profile_iteration(unsigned displayed,unsigned host_ticks,unsigned dropped)
{ (void)displayed;(void)host_ticks;(void)dropped; }
static inline void h1_profile_input(unsigned events,unsigned queries) { (void)events;(void)queries; }
static inline void h1_profile_video(unsigned rows,unsigned pixels,unsigned full)
{ (void)rows;(void)pixels;(void)full; }
static inline void h1_profile_save(int status) { (void)status; }
static inline void h1_profile_dump(const char *reason) { (void)reason; }
#endif
#endif
