#include "video.h"
#include "profile.h"
#include <string.h>

static uint8_t previous[96][20];
static uint8_t repeats[159];
static uint16_t row_begin[97];
static uint32_t expanded[397];
static volatile uint32_t *last_destination;
static unsigned last_scale=~0u;
static uint16_t last_background,last_foreground;

static uint32_t rgb32(uint16_t p)
{
    return ((p&0xf800u)<<8)|((p&0x07e0u)<<5)|((p&0x001fu)<<3);
}

void gam_video_draw(volatile uint32_t *destination,const uint8_t *packed,
                    unsigned scale,uint16_t background,uint16_t foreground,int force)
{
    unsigned width=scale==1?397:scale==2?318:159;
    unsigned height=scale==1?240:scale==2?192:96;
    unsigned x0=(480-width)/2,y0=(240-height)/2;
    unsigned rows=0,pixels=0;
    uint32_t colors[2]={rgb32(background),rgb32(foreground)};
    if(last_scale!=scale) {
        for(unsigned x=0;x<159;++x)
            repeats[x]=(uint8_t)(((x+1)*width+158)/159-(x*width+158)/159);
        for(unsigned y=0;y<=96;++y)row_begin[y]=(uint16_t)((y*height+95)/96);
        force=1;
    }
    if(destination!=last_destination||background!=last_background||foreground!=last_foreground)force=1;
    for(unsigned y=0;y<96;++y) {
        const uint8_t *row=packed+y*20;
        if(!force&&memcmp(row,previous[y],20)==0)continue;
        ++rows;pixels+=width*(row_begin[y+1]-row_begin[y]);
        memcpy(previous[y],row,20);
        unsigned output=0;
        for(unsigned x=0;x<159;++x) {
            uint32_t color=colors[(row[x>>3]>>(7-(x&7)))&1];
            unsigned n=repeats[x];
            expanded[output++]=color;
            if(n>=2)expanded[output++]=color;
            if(n==3)expanded[output++]=color;
        }
        for(unsigned r=row_begin[y];r<row_begin[y+1];++r) {
            volatile uint32_t *out=destination+(y0+r)*480+x0;
            unsigned x=0;
            for(;x+4<=width;x+=4) {
                out[x]=expanded[x];out[x+1]=expanded[x+1];
                out[x+2]=expanded[x+2];out[x+3]=expanded[x+3];
            }
            for(;x<width;++x)out[x]=expanded[x];
        }
    }
    last_destination=destination;last_scale=scale;
    last_background=background;last_foreground=foreground;
    h1_profile_video(rows,pixels,force!=0);
}
