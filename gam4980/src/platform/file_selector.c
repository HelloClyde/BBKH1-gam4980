#include "h1_sdk.h"
#include "file_selector.h"
#include "diagnostics.h"
#include "gui_context.h"
#include <stdint.h>
#include <string.h>

/* Recovered from H1 BB虚拟机.bda, calls 0x83C016F0 / 0x83C01814.
 * The caller passes (initial_directory, "bin", output_path) and ignores v0.
 * Its stack reserves 264 bytes between output_path and the next local.
 * Kept project-local; exercised with the complete H1 V1.41 firmware.
 */
typedef void (*h1_file_selector_fn)(const char *, const char *, char *);

static int selector_complete(int result)
{
    if (result <= 0) {
        /* V1.41 enters game display mode even when the picker is cancelled.
         * Closing an unopened window does not restore desktop redraw. Pair
         * open/close before returning to the launcher; no game is loaded. */
        h1_diag("SELECTOR_EXIT_RESTORE_BEGIN status=%d", result);
        int restored = gba_gui_open();
        if (restored) gba_gui_close();
        h1_diag("SELECTOR_EXIT_RESTORE_END restored=%d", restored);
        if (!restored) return -1;
    }
    return result;
}

int h1_rom_path_valid(const char *path, size_t n)
{
    size_t i;
    const char *ext;
    if (n < 6 || n > H1_ROM_PATH_MAX - 4) return 0; /* room for .s0 and NUL */
    if (!((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) ||
        path[1] != ':' || (path[2] != '\\' && path[2] != '/')) return 0;
    for (i = 0; i < n; ++i) if ((uint8_t)path[i] < 32) return 0;
    ext = path + n - 4;
    return ext[0] == '.' && (ext[1] == 'g' || ext[1] == 'G') &&
        (ext[2] == 'a' || ext[2] == 'A') && (ext[3] == 'm' || ext[3] == 'M');
}
void h1_rom_directory(const char *path, char *directory, size_t capacity)
{
    size_t i, end = 0;
    for (i = 0; path[i]; ++i) {
        uint8_t c = (uint8_t)path[i], next = (uint8_t)path[i + 1];
        /* A GBK trail byte can be 0x5c. It is not a path separator. */
        if (c >= 0x81 && c <= 0xfe && next >= 0x40 && next <= 0xfe && next != 0x7f) { ++i; continue; }
        if (c == '\\' || c == '/') end = i + 1;
    }
    if (!capacity) return;
    if (!end || end >= capacity) { directory[0] = 0; return; }
    memcpy(directory, path, end); directory[end] = 0;
}
int h1_select_rom(const char *directory, char *path, size_t capacity)
{
    /* Larger than BBVM's 264-byte buffer; validate against its path range. */
    char output[512];
    void *table;
    h1_file_selector_fn fn;
    size_t n;
    if (!path || !capacity) return -1;
    path[0] = 0;
    table = h1_runtime_table(H1_RUNTIME_GUI_TABLE_SLOT);
    if (!table) return -1;
    fn = (h1_file_selector_fn)h1_runtime_entry(table, H1_GUI_FILE_SELECTOR_OFFSET);
    h1_diag("SELECTOR_API table=%p offset=%u fn=%p", table, H1_GUI_FILE_SELECTOR_OFFSET, fn);
    if (!fn) return -1;
    memset(output, 0, sizeof(output));
    h1_diag("SELECTOR_CALL_BEGIN");
    /* V1.41 parser at 0x8003C01C splits extension names on semicolons. */
    fn(directory, "gam", output);
    h1_diag("SELECTOR_CALL_END");
    for (n = 0; n < sizeof(output) && output[n]; ++n) {}
    if (!n) return selector_complete(0);
    if (n == sizeof(output) || n >= capacity || !h1_rom_path_valid(output, n)) return selector_complete(-1);
    memcpy(path, output, n + 1);
    return 1;
}
