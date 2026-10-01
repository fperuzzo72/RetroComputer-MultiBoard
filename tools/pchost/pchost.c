/* pchost - the PC (src/pc/pc_core.c and lib/pc8086) on the development
 * machine, in real time. Prints the text screen.
 *
 *   PC_ROOT=dir ./pchost /pc/c.img SECONDS ["text to type\n"]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_core.h"
#include "pc_text.h"

static void dump(void)
{
    pc_screen s;
    pc_core_screen(&s);
    printf("--- mode %02X%s cursor %d,%d, %u instr/s\n", s.mode, s.graphics ? " (graphics)" : "",
           s.cursor_row, s.cursor_col, pc_core_ips());
    if (s.graphics) return;
    for (int r = 0; r < s.rows; r++) {
        char line[81];
        int n = 0;
        for (int c = 0; c < s.cols; c++) {
            uint8_t ch = s.cells[(r * s.cols + c) * 2];
            line[n++] = (ch >= 32 && ch < 127) ? ch : (ch ? '.' : ' ');
        }
        while (n && line[n - 1] == ' ') n--;
        line[n] = 0;
        printf("%s\n", line);
    }
}

#include "pc_keys.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: pchost /path/c.img SECONDS [text]\n"); return 2; }
    if (!pc_core_init(argv[1], NULL)) { fprintf(stderr, "pc: %s\n", pc_core_error()); return 1; }
    const int secs = atoi(argv[2]);
    const char *type = argc > 3 ? argv[3] : NULL;
    for (int t = 0; t < secs * 10; t++) {
        pc_core_run(100000);
        if (type && *type && t >= secs * 10 / 2) pc_keys_type(*type++);
    }
    dump();
    if (getenv("PEEK")) {
        /* PEEK=seg:off:len,... hex dumps of emulated memory */
        extern uint8_t mem_read_byte(uint32_t);
        char *spec = strdup(getenv("PEEK"));
        for (char *p = strtok(spec, ","); p; p = strtok(NULL, ",")) {
            unsigned seg, off, len;
            if (sscanf(p, "%x:%x:%x", &seg, &off, &len) != 3) continue;
            printf("%04X:%04X ", seg, off);
            for (unsigned i = 0; i < len; i++) printf("%02X ", mem_read_byte(seg * 16 + off + i));
            printf("\n");
        }
    }
    if (getenv("PBM")) {
        /* the 1-bit picture the board would get, as a PBM */
        static uint8_t fb[PC_FB_W / 8 * PC_FB_H];
        pc_text_render(fb);
        FILE *f = fopen(getenv("PBM"), "wb");
        fprintf(f, "P4\n%d %d\n", PC_FB_W, PC_FB_H);
        fwrite(fb, 1, sizeof fb, f);
        fclose(f);
    }
    return 0;
}
