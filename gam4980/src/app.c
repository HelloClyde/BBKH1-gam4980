#include "h1_sdk.h"
#include "gam4980_core.h"
#include "platform/diagnostics.h"
#include "platform/file_selector.h"
#include "platform/frontend.h"
#include "platform/save.h"
#include "platform/gui_context.h"
#include "platform/game_video.h"
#include "platform/video.h"
#include "platform/profile.h"
#include "platform/timing.h"
#include "platform/game_input.h"
#include "platform/touch.h"
#include "platform/menu_font.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef H1_TEST_FRAMES
#define H1_TEST_FRAMES 0
#endif
static uint16_t canvas[480*272];
static unsigned scale=1,theme,frames,view,cursor,redraw=1;
static unsigned host_tick_hz=80;
static unsigned held[45],ages[45];
static uint8_t alias_owner[256];
static int action,save_enabled,capture=-1,config_enabled,pause_pending,game_ended;
/* Keep input evidence in RAM; flushing per key stalls NAND-backed firmware. */
typedef struct {unsigned serial,frame;int code,key,core;unsigned page,source;} input_record;
static input_record input_history[64];
static unsigned input_serial,input_dumped;
static void input_trace(unsigned source,int code,int key,int core)
{
    unsigned serial=++input_serial;
    input_history[(serial-1)%64]=(input_record){serial,frames,code,key,core,view,source};
}
static void input_dump(void)
{
    if(input_dumped==input_serial)return;
    unsigned first=input_dumped+1;
    if(input_serial-first>=64)first=input_serial-63;
    h1_diag_batch_begin();
    for(unsigned serial=first;serial<=input_serial;++serial) {
        const input_record *r=&input_history[(serial-1)%64];
        h1_diag("INPUT_TRACE serial=%u frame=%u source=%u event=%d physical=%d core=%d view=%u",
                r->serial,r->frame,r->source,r->code,r->key,r->core,r->page);
    }
    h1_diag_batch_end();input_dumped=input_serial;
}
static void game_key(unsigned key,unsigned physical,unsigned source)
{
    input_trace(source,0,(int)physical,(int)key);
    if(!game_ended)gam4980_key_down((uint8_t)key);
}
static save_store store;
static save_store config_store;
typedef struct {uint32_t magic,version;uint8_t mapping[45],padding[3];} key_config;
static key_config config;
typedef struct {uint32_t magic,version;uint8_t keys[4];} shortcut_config;
static shortcut_config shortcuts;
static save_store shortcut_store;
static int shortcut_enabled;
static unsigned shortcut_slot,shortcut_page;
static gam4980_buffers_t buffers;
static void mapping_load(const char *);
static void mapping_save(void);
static void shortcuts_load(const char *);
static const char *shortcut_name(unsigned);

uint32_t crc32_bytes(const void *data,size_t n)
{
    const uint8_t *p=data; uint32_t c=~0u;
    while(n--) { c^=*p++;for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1))); }
    return ~c;
}
static void rect(int x,int y,int w,int h,uint16_t c)
{
    for(int r=y;r<y+h&&r<272;++r)for(int col=x;col<x+w&&col<480;++col)
        if(r>=0&&col>=0)canvas[r*480+col]=c;
}
static void text(const char *s,int x,int y,uint16_t c)
{
    while(*s&&x<470) {
        unsigned cp=(uint8_t)*s++;
        if(cp>=0xe0) {cp=(cp&15)<<12;cp|=((uint8_t)*s++&63)<<6;cp|=(uint8_t)*s++&63;}
        else if(cp>=0xc0){cp=(cp&31)<<6;cp|=(uint8_t)*s++&63;}
        unsigned advance=cp<128?10:20;
        for(unsigned i=0;i<MENU_GLYPH_COUNT;++i)if(menu_glyphs[i].code==cp) {
            advance=menu_glyphs[i].advance;
            for(unsigned r=0;r<21;++r)for(unsigned col=0;col<18;++col)
                if(menu_glyphs[i].rows[r]&(1u<<col))rect(x+col,y+r,1,1,c);
            break;
        }
        x+=(int)advance;
    }
}
static int present(void) { return gba_game_present(canvas,0); }
static void loading(unsigned percent,const char *stage)
{
    rect(0,0,480,272,0x0843);text("GAM4980",192,65,0xffff);text(stage,50,100,0xbdf7);
    rect(50,137,380,14,0x2947);rect(50,137,(int)(380*percent/100),14,0x05b4);
    present();
}
static int read_file(const char *path,void *data,unsigned exact,unsigned percent)
{
    h1_diag("READ_BEGIN path=%s size=%u",path,exact);
    h1_file *f=h1_fopen(path,"rb");if(!f)return 0;
    int size=h1_fseek(f,0,H1_SEEK_END),ok=size==(int)exact&&h1_fseek(f,0,H1_SEEK_SET)==0;
    unsigned done=0;
    while(ok&&done<exact) {
        unsigned n=exact-done;if(n>65536)n=65536;
        if(h1_fread((uint8_t *)data+done,1,n,f)!=n){ok=0;break;}
        done+=n;loading(percent+done*20/exact,"正在加载运行文件");
    }
    if(h1_fclose(f)!=0)ok=0;
    h1_diag("READ_END bytes=%u ok=%d",done,ok);return ok;
}
static int load_game(const char *path)
{
    uint8_t header[GAM4980_GAME_HEADER_SIZE]={0};
    h1_file *f=h1_fopen(path,"rb");if(!f)return 0;
    int size=h1_fseek(f,0,H1_SEEK_END),ok=size>=(int)GAM4980_GAME_HEADER_SIZE&&size<=(int)GAM4980_GAME_MAX_SIZE;
    if(ok)ok=h1_fseek(f,0,H1_SEEK_SET)==0&&h1_fread(header,1,sizeof header,f)==sizeof header;
    /* Let the original core interpret banked entry/data addresses. */
    if(ok)ok=h1_fseek(f,0,H1_SEEK_SET)==0;
    unsigned done=0;
    while(ok&&done<(unsigned)size) {
        unsigned n=(unsigned)size-done;if(n>65536)n=65536;
        if(h1_fread(gam4980_game_storage()+done,1,n,f)!=n){ok=0;break;}
        done+=n;loading(60+done*30/(unsigned)size,"正在加载游戏");
    }
    if(h1_fclose(f)!=0)ok=0;
    if(!ok||gam4980_load_game_header(header,(uint32_t)size)<=0)return 0;
    int saved=save_load(&store,path,gam4980_save_data(),GAM4980_SAVE_SIZE);
    save_enabled=saved>=0;
    h1_diag("GAME_READY size=%d save=%d crc32=%08X",size,saved,crc32_bytes(gam4980_game_storage(),(unsigned)size));
    if(!save_enabled)h1_message_box(0,"Save damaged; saving disabled.","GAM4980",0);
    mapping_load(path);
    shortcuts_load(path);
    gam4980_save_mark_clean();return 1;
}
static void checkpoint(void)
{
    if(!save_enabled||!gam4980_save_dirty())return;
    h1_profile_push(HP_SAVE);
    int rc=save_checkpoint(&store,gam4980_save_data(),GAM4980_SAVE_SIZE);
    if(rc>=0)gam4980_save_mark_clean();
    h1_profile_pop();
    h1_profile_save(rc);
#if !H1_PROFILE
    h1_diag("SAVE_CHECKPOINT status=%d",rc);
#endif
    if(rc<0)h1_message_box(0,"Cannot write save. Keep the device powered on.","GAM4980",0);
}
static int game_screen(void)
{
    static volatile uint32_t *last_buffer;
    volatile uint32_t *buffer=gba_game_buffer(0);
    if(!buffer)return -1;
    int force=redraw||buffer!=last_buffer;
    if(force) {
        rect(0,0,480,272,0x0843);
        rect(0,240,480,32,0x2947);
        for(unsigned i=0;i<4;++i){text(shortcut_name(i),i*120+18,246,0xffff);if(i)rect(i*120,240,1,32,0x632c);}
        for(unsigned i=0;i<480u*272u;++i) {
            uint32_t p=canvas[i];
            buffer[i]=((p&0xf800u)<<8)|((p&0x07e0u)<<5)|((p&0x001fu)<<3);
        }
    }
    gam_video_draw(buffer,gam4980_packed_frame(),scale,
                   gam4980_lcd_background_color(),gam4980_lcd_foreground_color(),force);
    last_buffer=buffer;
    return 0;
}
typedef struct {const char *label; uint8_t key;} function_key;
static const function_key functions[]={
    {"菜单",1},{"中英",2},{"英中",3},{"清除",4},{"对话",5},{"下载",6},
    {"发音",7},{"输入",32},{"帮助",41},{"搜索",42},{"插入",43},{"修改",44},
    {"1",8},{"2",9},{"3",10},{"4",11},{"5",12},{"6",13},
    {"7",14},{"8",15},{"9",48},{"0",49},{"删除",45},{"退出",46}};
static const char *key_names[45]={"-","Q","W","E","R","T","Y","U","A","S","D","F","G","H","J","上页",
    "Z","X","C","V","B","N","下页","空格","ESC","回车","键盘左","下","键盘右","I","O","P","K","L","符号","右","M","上","删除","确认","左","返回","SHIFT","FN","电源"};
static const char *scale_names[]={"原始大小","等比例放大","整数倍放大（2倍）"};
static const char *scale_short_names[]={"原始大小","等比例放大","整数倍放大"};
static const char *theme_names[]={"原色（灰白）","绿色","蓝色","黄色"};
static const char *theme_short_names[]={"原色","绿色","蓝色","黄色"};
static const char *core_key_names[60]={"-","菜单","中英","英中","清除","对话","下载","发音",
    "1","2","3","4","5","6","7","8","Q","W","E","R","T","Y","U","I",
    "A","S","D","F","G","H","J","K","输入","Z","X","C","V","B","N","M",
    "SHIFT","帮助","搜索","插入","修改","删除","退出","确认","9","0","O","P","L",
    "上","空格","左","下","右","上页","下页"};
static unsigned shortcut_options(void){unsigned n=59-shortcut_page*24;return n<24?n:24;}
static unsigned view_count(void){return view==1?6:view==3?4:view==4?5:view==5?3:view==6?6:view==7?shortcut_options()+3:26;}
static const char *shortcut_name(unsigned slot)
{
    unsigned key=shortcuts.keys[slot];return key>0&&key<60?core_key_names[key]:"-";
}
static void shortcuts_defaults(void)
{
    static const uint8_t defaults[4]={GAM4980_KEY_MENU,GAM4980_KEY_ENTER,GAM4980_KEY_EXIT,GAM4980_KEY_HELP};
    shortcuts.magic=0x314b5147;shortcuts.version=1;memcpy(shortcuts.keys,defaults,4);
}
static void shortcuts_load(const char *path)
{
    shortcuts_defaults();shortcut_config saved=shortcuts;
    int rc=save_load_aux(&shortcut_store,path,&saved,sizeof saved,".hot");
    shortcut_enabled=rc>=0;
    if(rc==1) {
        int valid=saved.magic==shortcuts.magic&&saved.version==1;
        for(unsigned i=0;i<4;++i)if(!saved.keys[i]||saved.keys[i]>=60)valid=0;
        if(valid)shortcuts=saved;else shortcut_enabled=0;
    }
    h1_diag("SHORTCUT_LOAD status=%d enabled=%d",rc,shortcut_enabled);
}
static void shortcuts_save(void)
{
    if(!shortcut_enabled)return;
    int rc=save_checkpoint(&shortcut_store,&shortcuts,sizeof shortcuts);
    h1_diag("SHORTCUT_SAVE status=%d",rc);
    if(rc<0)h1_message_box(0,"Cannot save shortcuts.","GAM4980",0);
}
static int mapping_reserved(unsigned k){return !k||k>44||k==H1_KEY_BACK||k==H1_KEY_ESCAPE||k==H1_KEY_POWER;}
static void mapping_defaults(void)
{
    static const int8_t defaults[45]={-1,16,17,18,19,20,21,22,24,25,26,27,28,29,30,58,
        33,34,35,36,37,38,59,54,46,47,55,56,57,23,50,51,31,52,32,57,39,53,45,47,55,-1,40,7,-1};
    memset(&config,0,sizeof config);config.magic=0x314d4147;config.version=2;
    for(unsigned k=1;k<45;++k)config.mapping[k]=(uint8_t)(defaults[k]+1);
}
static void mapping_load(const char *path)
{
    mapping_defaults();key_config saved=config;
    int rc=save_load_aux(&config_store,path,&saved,sizeof saved,".cfg");
    config_enabled=rc>=0;
    if(rc==1) {
        int valid=saved.magic==config.magic&&(saved.version==1||saved.version==2)&&!saved.mapping[0]&&
            saved.mapping[H1_KEY_ESCAPE]==GAM4980_KEY_EXIT+1&&!saved.mapping[H1_KEY_BACK]&&!saved.mapping[H1_KEY_POWER];
        for(unsigned k=1;k<45;++k)if(saved.mapping[k]>GAM4980_KEY_PAGE_DOWN+1)valid=0;
        if(valid) {
            config=saved;
            if(config.version==1) {
                /* Repair old defaults without losing other user bindings. */
                if(config.mapping[GAM_H1_KEY_KEYBOARD_LEFT]==GAM4980_KEY_MENU+1)
                    config.mapping[GAM_H1_KEY_KEYBOARD_LEFT]=GAM4980_KEY_LEFT+1;
                if(config.mapping[GAM_H1_KEY_KEYBOARD_RIGHT]==GAM4980_KEY_HELP+1)
                    config.mapping[GAM_H1_KEY_KEYBOARD_RIGHT]=GAM4980_KEY_RIGHT+1;
                config.version=2;mapping_save();
                h1_diag("MAPPING_MIGRATE from=1 to=2 keyboard_left=%d keyboard_right=%d",
                        (int)config.mapping[GAM_H1_KEY_KEYBOARD_LEFT]-1,(int)config.mapping[GAM_H1_KEY_KEYBOARD_RIGHT]-1);
            }
        } else config_enabled=0;
    }
    h1_diag("MAPPING_LOAD status=%d enabled=%d",rc,config_enabled);
    h1_diag("KEYMAP left=%d right=%d up=%d down=%d keyboard_left=%d keyboard_right=%d escape=%d back=%d",
            (int)config.mapping[H1_KEY_LEFT]-1,(int)config.mapping[H1_KEY_RIGHT]-1,
            (int)config.mapping[H1_KEY_UP]-1,(int)config.mapping[H1_KEY_DOWN]-1,
            (int)config.mapping[GAM_H1_KEY_KEYBOARD_LEFT]-1,(int)config.mapping[GAM_H1_KEY_KEYBOARD_RIGHT]-1,
            (int)config.mapping[H1_KEY_ESCAPE]-1,(int)config.mapping[H1_KEY_BACK]-1);
}
static void mapping_save(void)
{
    if(!config_enabled)return;
    int rc=save_checkpoint(&config_store,&config,sizeof config);
    h1_diag("MAPPING_SAVE status=%d",rc);
    if(rc<0)h1_message_box(0,"Cannot save key mapping.","GAM4980",0);
}
static unsigned mapping_host(unsigned target)
{
    for(unsigned k=1;k<45;++k)if(config.mapping[k]==target+1)return k;
    return 0;
}
static void menu_screen(void)
{
    rect(0,0,480,272,0x0843);
    text(view==1?(game_ended?"游戏已结束":"游戏已暂停"):view==3?"显示大小":view==4?"LCD 颜色":view==5?"按键设置":view==6?"底栏快捷键":view==7?"选择游戏按键":"功能键映射",18,8,0xffff);
    text(capture>=0?"请按实体键；返回或 ESC 取消":view>=3?"触摸选择，方向键／确认；返回菜单":"方向键选择，确认；返回设置",18,32,0xbdf7);
    if(view==1&&game_ended){char label[64];snprintf(label,sizeof label,"核心停止 PC=%04X；详情见日志",gam4980_shutdown_pc());rect(0,30,480,26,0x0843);text(label,18,32,0xbdf7);}
    const char *root[]={game_ended?"重新开始":"继续游戏","显示大小","LCD 颜色","按键设置","更换游戏","退出应用"};
    if(view==6) {
        for(unsigned i=0;i<6;++i) {
            int x=i<4?38:i==4?18:248,y=i<4?56+(int)i*34:202,w=i<4?404:214;
            rect(x,y,w,30,i==cursor?0x04b0:0x2947);
            char label[64];
            if(i<4)snprintf(label,sizeof label,"快捷键 %u：%s",i+1,shortcut_name(i));
            else snprintf(label,sizeof label,"%s",i==4?"恢复默认":"返回设置");
            text(label,x+8,y+4,0xffff);
        }
        present();return;
    }
    if(view==7) {
        unsigned options=shortcut_options();char page[16];snprintf(page,sizeof page,"%u / 3",shortcut_page+1);text(page,390,8,0xbdf7);
        for(unsigned i=0;i<options+3;++i) {
            int x=i<options?6+(int)(i%6)*79:6+(int)(i-options)*157;
            int y=i<options?62+(int)(i/6)*41:234,w=i<options?73:151,h=i<options?35:32;
            unsigned key=shortcut_page*24+i+1;
            rect(x,y,w,h,i==cursor?0x04b0:0x2947);
            text(i<options?core_key_names[key]:i==options?"上一页":i==options+1?"下一页":"返回设置",x+5,y+6,0xffff);
            if(i<options&&key==shortcuts.keys[shortcut_slot])rect(x,y,3,h,0x05b4);
        }
        present();return;
    }
    if(view==3||view==4) {
        unsigned options=view==3?3:GAM4980_LCD_THEME_COUNT,selected=view==3?scale:theme;
        for(unsigned i=0;i<=options;++i) {
            int y=56+(int)i*40;
            rect(38,y,404,34,i==cursor?0x04b0:0x2947);
            if(i==options)text("返回菜单",48,y+6,0xffff);
            else {
                text(view==3?scale_names[i]:theme_names[i],48,y+6,0xffff);
                if(view==4) {
                    static const uint16_t colors[]={0xd6da,0x96e1,0x3edd,0xf72c};
                    rect(300,y+8,24,18,colors[i]);
                }
                if(i==selected){rect(38,y,4,34,0x05b4);text("已选",366,y+6,0xffff);}
            }
        }
        present();return;
    }
    unsigned count=view_count();
    for(unsigned i=0;i<count;++i) {
        int x,y,w,h;
        if(view==1||view==5){x=18+(i%2)*230;y=64+(i/2)*62;w=214;h=48;}
        else if(i<24){x=6+(i%6)*79;y=62+(i/6)*41;w=73;h=35;}
        else{x=6+(i-24)*236;y=234;w=230;h=32;}
        rect(x,y,w,h,(int)i==capture?0x9a20:i==cursor?0x04b0:0x2947);
        if(view==5) {
            const char *settings[]={"实体按键映射","底栏快捷键","返回菜单"};text(settings[i],x+5,y+7,0xffff);
        } else if(view==1) {
            char label[64];const char *name=root[i];
            if(i==1){snprintf(label,sizeof label,"显示大小：%s",scale_short_names[scale]);name=label;}
            if(i==2){snprintf(label,sizeof label,"LCD 颜色：%s",theme_short_names[theme]);name=label;}
            text(name,x+5,y+7,0xffff);
        }
        else if(i>=24)text(i==24?"恢复默认":"返回设置",x+5,y+6,0xffff);
        else {
            text(functions[i].label,x+5,y,0xffff);
            text(key_names[mapping_host(functions[i].key)],x+5,y+16,0xbdf7);
        }
    }
    present();
}
static void open_menu(unsigned page)
{
    pause_pending=1;view=page;cursor=page==3?scale:page==4?theme:0;capture=-1;redraw=1;
}
static void activate(unsigned index)
{
    if(view==7) {
        unsigned options=shortcut_options();
        if(index<options) {
            shortcuts.keys[shortcut_slot]=(uint8_t)(shortcut_page*24+index+1);shortcuts_save();view=6;cursor=shortcut_slot;
        } else if(index<options+2) {shortcut_page=(shortcut_page+(index==options?2:1))%3;cursor=0;}
        else {view=6;cursor=shortcut_slot;}
    } else if(view==6) {
        if(index<4){shortcut_slot=index;shortcut_page=(shortcuts.keys[index]-1)/24;cursor=(shortcuts.keys[index]-1)%24;view=7;}
        else if(index==4){shortcuts_defaults();shortcuts_save();}
        else {view=5;cursor=1;}
    } else if(view==5) {
        if(index==0){view=2;cursor=0;capture=-1;}
        else if(index==1){view=6;cursor=0;}
        else {view=1;cursor=3;}
    } else if(view==3||view==4) {
        unsigned options=view==3?3:GAM4980_LCD_THEME_COUNT;
        if(index==options){cursor=view==3?1:2;view=1;}
        else if(index<options) {
            if(view==3)scale=index;
            else {theme=index;gam4980_set_lcd_theme(theme);}
            cursor=index;h1_diag("DISPLAY_SET scale=%u theme=%u",scale,theme);
        }
    } else if(view==2) {
        if(index<24)capture=(int)index;
        else if(index==24){mapping_defaults();mapping_save();}
        else {mapping_save();view=5;cursor=0;}
    } else switch(index) {
    case 0:if(game_ended)action=3;else view=0;break;
    case 1:view=3;cursor=scale;break;
    case 2:view=4;cursor=theme;break;
    case 3:view=5;cursor=0;break;
    case 4:action=1;break;
    case 5:action=2;break;
    }
    redraw=1;
}
static int map_key(unsigned key)
{
    return key<45?(int)config.mapping[key]-1:-1;
}
static void key_press(unsigned key,unsigned source)
{
    if(view) {
        if(capture>=0) {
            if(key==H1_KEY_BACK||key==H1_KEY_ESCAPE){capture=-1;redraw=1;return;}
            if(mapping_reserved(key))return;
            unsigned target=functions[capture].key;
            for(unsigned k=1;k<45;++k)if(!mapping_reserved(k)&&config.mapping[k]==target+1)config.mapping[k]=0;
            config.mapping[key]=(uint8_t)(target+1);capture=-1;mapping_save();redraw=1;
            h1_diag("MAPPING_SET physical=%u core=%u",key,target);return;
        }
        key=h1_menu_direction(key);
        unsigned count=view_count(),cols=view==1||view==5?2:view==2||view==7?6:1;
        if(key==H1_KEY_BACK||key==H1_KEY_ESCAPE){
            if(view==7){view=6;cursor=shortcut_slot;}
            else if(view==6){view=5;cursor=1;}
            else if(view==5){view=1;cursor=3;}
            else if(view==2){mapping_save();view=5;cursor=0;}
            else if(view==3||view==4){cursor=view==3?1:2;view=1;}
            else if(!game_ended)view=0;
            redraw=1;return;
        }
        if(key==H1_KEY_ENTER||key==H1_KEY_CONFIRM){activate(cursor);return;}
        if(key==H1_KEY_LEFT)cursor=(cursor+count-1)%count;
        if(key==H1_KEY_RIGHT)cursor=(cursor+1)%count;
        if(key==H1_KEY_UP)cursor=(cursor+count-cols)%count;
        if(key==H1_KEY_DOWN)cursor=(cursor+cols)%count;
        redraw=1;return;
    }
    if(key==H1_KEY_BACK){open_menu(1);return;}
    int mapped=map_key(key);if(mapped>=0){
        game_key((unsigned)mapped,key,source);
#if H1_TRACE || (H1_TEST_FRAMES && !H1_PROFILE)
        h1_diag("KEY physical=%u core=%d",key,mapped);
#endif
    }
}
static void poll_input(void)
{
    unsigned events=0,queries=0;
    for(unsigned i=0;i<128;++i) {
        int code=-1,key=-1;h1_event_fetch(&code,&key);
        if(code==-1&&key==-1)break;
        ++events;
        if(code==11) {
            int x,y;if(!h1_touch_position(&x,&y))continue;
            if(!view) {
                if(y>=0&&y<240)open_menu(1);
                else if(y>=240&&y<272&&x>=0&&x<480)game_key(shortcuts.keys[x/120],0,4);
            } else if((view==1||view==5)&&y>=64) {
                unsigned row=(y-64)/62,col=x>=248;
                unsigned index=row*2+col;
                if(row<3&&index<view_count()&&x>=18&&x<462&&(y-64)%62<48)activate(index);
            } else if((view==3||view==4)&&x>=38&&x<442&&y>=56) {
                unsigned index=(y-56)/40;
                if(index<view_count()&&(y-56)%40<34)activate(index);
            } else if(view==6) {
                if(x>=38&&x<442&&y>=56&&y<192&&(y-56)%34<30)activate((y-56)/34);
                else if(y>=202&&y<232&&((x>=18&&x<232)||(x>=248&&x<462)))activate(x<232?4:5);
            } else if(view==7&&y>=62&&x>=6) {
                unsigned options=shortcut_options();
                if(y>=234&&y<266&&x<471&&(x-6)%157<151){activate(options+(x-6)/157);continue;}
                unsigned row=(y-62)/41,col=(x-6)/79,index=row*6+col;
                if(row<4&&col<6&&index<options&&(y-62)%41<35&&(x-6)%79<73)activate(index);
            } else if(view==2&&y>=62&&x>=6) {
                if(capture>=0)continue;
                if(y>=234&&y<266){activate(x<236?24:25);continue;}
                unsigned row=(y-62)/41,col=(x-6)/79,index=row*6+col;
                if(col<6&&index<24&&(y-62)%41<35)activate(index);
            }
        }
        if((code==9||code==10)&&key>0&&key<=44) {
            input_trace(0,code,key,-1);
            unsigned native=h1_game_key_code((unsigned)key);
            if(code==9&&(native==1||native==28||native==105||native==106))alias_owner[native]=(uint8_t)key;
            if(code==10&&alias_owner[native]==key)alias_owner[native]=0;
            if(code==9&&!held[key])key_press(key,1);
            held[key]=code==9;ages[key]=0;
        }
    }
    /* Queue identifies Back/Escape and both direction pairs; query all other
       keys to preserve simultaneous presses in native game mode. */
    uint8_t sampled[256]={0},down_codes[256]={0};
    typedef int (*key_query)(unsigned);
    key_query query=(key_query)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x9d8u);
    for(unsigned key=1;key<=42;++key) {
        unsigned code=h1_game_key_code(key);
        if(!sampled[code]) {
            if(code&&query)++queries;
            down_codes[code]=(uint8_t)(code&&query&&query(code)!=0);sampled[code]=1;
        }
        unsigned down=down_codes[code];
        if(code==1||code==28||code==105||code==106)
            /* The game-query service may report Escape/Enter/directions but
             * not their Back/Confirm/keyboard-direction matrix aliases. Keep queue ownership
             * until key-up; otherwise a held Back becomes repeated presses. */
            down=alias_owner[code]?(alias_owner[code]==key&&held[key]):
                down&&key==(code==1?H1_KEY_ESCAPE:code==28?H1_KEY_ENTER:code==105?H1_KEY_LEFT:H1_KEY_RIGHT);
        if(down&&!held[key])key_press(key,2);
        if(down!=held[key])ages[key]=0;
        held[key]=down;
    }
    h1_profile_input(events,queries);
}
static void repeat_keys(void)
{
    for(unsigned key=1;key<=42;++key)if(held[key]&&key!=H1_KEY_BACK) {
        if(++ages[key]>=12&&(ages[key]-12)%3==0){int k=map_key(key);if(k>=0)game_key((unsigned)k,key,3);}
    }
}
static void release_buffers(void)
{
    gam4980_deinit();free(buffers.ram);free(buffers.flash);free(buffers.rom_8);free(buffers.rom_e);free(buffers.framebuffer);
    memset(&buffers,0,sizeof buffers);
}
static void profile_prepare(void)
{
#if H1_PROFILE
    h1_profile_begin();
    h1_diag("PROFILE_SETTINGS scale=%u theme=%u core_hz=60 host_hz=%u catchup_ticks=%u input=matrix",scale,theme,host_tick_hz,(host_tick_hz+9)/10);
    h1_diag("PROFILE_MEMORY ram=%p flash=%p rom8=%p rome=%p",buffers.ram,buffers.flash,buffers.rom_8,buffers.rom_e);
    h1_diag("PROFILE_SOC cpccr=%08X cppcr=%08X clkgr=%08X",*(volatile uint32_t *)0xb0000000u,*(volatile uint32_t *)0xb0000010u,*(volatile uint32_t *)0xb0000020u);
    h1_profile_start();
#endif
}
int h1_app_main(void)
{
    char path[260],directory[260]="A:\\gam4980\\";
    h1_diag_init();h1_diag("APP_BEGIN GAM4980 H1");
    for(;;) {
        int selected=action==3?1:h1_select_rom(directory,path,sizeof path);
        action=0;
        if(selected<=0){if(selected<0)h1_message_box(0,"Invalid GAM path or selector unavailable.","GAM4980",0);break;}
        h1_rom_directory(path,directory,sizeof directory);
        if(!gba_gui_open())break;
        loading(0,"正在申请内存");
        buffers.ram=malloc(GAM4980_RAM_SIZE);buffers.flash=malloc(GAM4980_FLASH_SIZE);
        buffers.rom_8=malloc(GAM4980_ROM_SIZE);buffers.rom_e=malloc(GAM4980_ROM_SIZE);
        buffers.framebuffer=malloc(160*96*2);
        int ready=buffers.ram&&buffers.flash&&buffers.rom_8&&buffers.rom_e&&buffers.framebuffer;
        if(!ready)h1_message_box(0,"Not enough memory (about 6.3 MiB required).","GAM4980",0);
        if(ready)ready=read_file("A:\\gam4980\\8.BIN",buffers.rom_8,GAM4980_ROM_SIZE,0)&&
                       read_file("A:\\gam4980\\E.BIN",buffers.rom_e,GAM4980_ROM_SIZE,20);
        if(ready) {
            loading(45,"正在初始化核心");h1_diag("CORE_INIT_BEGIN");
            int rc=gam4980_init(&buffers);h1_diag("CORE_INIT_END status=%d",rc);ready=rc>0;
            if(ready){gam4980_set_lcd_theme(theme);ready=load_game(path);}
        }
        if(!ready) {
            release_buffers();gba_gui_close();
            h1_message_box(0,"Load failed. Need 2 MiB 8.BIN / E.BIN in A:\\gam4980 and a valid .gam.","GAM4980",0);continue;
        }
        loading(100,"加载完成");
        host_tick_hz=gam_host_tick_hz();
        frames=view=cursor=action=0;capture=-1;redraw=1;pause_pending=game_ended=0;
        input_serial=input_dumped=0;
        memset(held,0,sizeof held);memset(ages,0,sizeof ages);
        memset(alias_owner,0,sizeof alias_owner);
        h1_diag("PROFILE_GAME path=%s",path);
        profile_prepare();
        uint32_t previous=h1_raw_tick_80hz();unsigned phase=0;
        int running=1;
        h1_profile_push(HP_OTHER);h1_profile_push(HP_WAIT);
        while(!action) {
            uint32_t now=h1_raw_tick_80hz(),elapsed=now-previous;
            if(!elapsed)continue;
            previous=now;
            if(running){h1_profile_pop();h1_profile_push(HP_INPUT);}
            poll_input();
            if(running)h1_profile_pop();
            if(pause_pending) {
                checkpoint();
                if(running){h1_profile_pop();h1_profile_iteration(0,elapsed,0);h1_profile_dump("pause");running=0;}
                h1_diag("PAUSE view=%u frames=%u",view,frames);pause_pending=0;
                input_dump();
            }
            if(action) {
                checkpoint();
                if(running){h1_profile_pop();h1_profile_iteration(0,elapsed,0);h1_profile_dump("exit");running=0;}
                break;
            }
            if(view){if(redraw){menu_screen();redraw=0;}phase=0;continue;}
            if(!running) {
                profile_prepare();running=1;previous=h1_raw_tick_80hz();phase=0;
                h1_profile_push(HP_OTHER);h1_profile_push(HP_WAIT);continue;
            }
            /* Measured host ticks -> 60 core frames/sec, including firmware
               whose "80 Hz" service actually ticks at 40 Hz. */
            unsigned limit=(host_tick_hz+9)/10;
            unsigned host_ticks=elapsed,dropped=elapsed>limit?elapsed-limit:0;
            if(elapsed>limit)elapsed=limit;
            phase+=elapsed*60;
            while(phase>=host_tick_hz) {
                phase-=host_tick_hz;
                h1_profile_push(HP_INPUT);repeat_keys();h1_profile_pop();
                h1_profile_push(HP_CORE);gam4980_step_frame();h1_profile_pop();
                h1_profile_core_frame();++frames;
                if(frames%600==0)checkpoint();
                if(gam4980_shutdown_requested()){game_ended=1;break;}
#if H1_TEST_FRAMES
                if(frames>=H1_TEST_FRAMES){action=2;break;}
#endif
            }
            h1_profile_push(HP_CAPTURE);int changed=gam4980_render_frame();h1_profile_pop();
            unsigned displayed=0;
            if(changed||redraw) {
                h1_profile_push(HP_VIDEO);int rc=game_screen();h1_profile_pop();
                redraw=rc<0;displayed=rc>=0;
            }
            if(action||game_ended)checkpoint();
            h1_profile_pop();h1_profile_iteration(displayed,host_ticks,dropped);
            if(game_ended) {
                h1_profile_dump("core_stop");running=0;phase=0;
                h1_diag("CORE_STOP pc=%04X frame=%u action=%d",gam4980_shutdown_pc(),frames,action);
                input_dump();open_menu(1);continue;
            }
            if(action){h1_profile_dump("exit");running=0;break;}
            h1_profile_push(HP_OTHER);h1_profile_push(HP_WAIT);
        }
        checkpoint();input_dump();h1_diag("STOP frames=%u action=%d ended=%d",frames,action,game_ended);
        release_buffers();gba_gui_close();
        if(action==2)break;
    }
    h1_diag("APP_END");return 0;
}
