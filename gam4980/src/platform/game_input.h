#ifndef H1_GAME_INPUT_H
#define H1_GAME_INPUT_H
#include "h1_sdk.h"

/* H1's physical keyboard has a second left/right pair. The SDK's legacy
 * LEFT_ALT / RIGHT_ALT names for matrix 26/28 do not describe these keycaps. */
#define GAM_H1_KEY_KEYBOARD_LEFT 26u
#define GAM_H1_KEY_KEYBOARD_RIGHT 28u

static inline unsigned h1_menu_direction(unsigned key)
{
    return key==GAM_H1_KEY_KEYBOARD_LEFT?H1_KEY_LEFT:
           key==GAM_H1_KEY_KEYBOARD_RIGHT?H1_KEY_RIGHT:key;
}

/* GUI+0x9D8 is used by BBVM, Mission and Thunder to query simultaneous keys.
 * It accepts the native game scancode, not the 1..42 event matrix identifier.
 * V1.41's own scan table supplies this conversion (Q=16, D=32, K=37). */
static inline unsigned h1_game_key_code(unsigned key)
{
    static const uint8_t codes[43]={0,16,17,18,19,20,21,22,30,31,32,33,34,35,36,
        104,44,45,46,47,48,49,109,57,1,28,105,108,106,23,24,25,37,38,86,
        106,50,103,111,28,105,1,42};
    return key>=1 && key<=42 ? codes[key] : 0;
}
static inline int h1_game_code_down(unsigned code)
{
    typedef int (*function_type)(unsigned);
    function_type fn=(function_type)h1_runtime_entry(h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT),0x9d8u);
    return code && fn ? fn(code)!=0 : 0;
}
#endif
