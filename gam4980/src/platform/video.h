#ifndef GAM_VIDEO_H
#define GAM_VIDEO_H
#include <stdint.h>
/* Draw only changed LCD rows into the current H1 RGB32 buffer. The caller
 * restores the border/toolbar and sets force after menus or window changes. */
void gam_video_draw(volatile uint32_t *destination, const uint8_t *packed,
                    unsigned scale, uint16_t background, uint16_t foreground,
                    int force);
#endif
