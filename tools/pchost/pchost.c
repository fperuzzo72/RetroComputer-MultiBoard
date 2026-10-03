/* pchost - the PC (src/pc/pc_core.c and lib/pc8086) on the development
 * machine, in real time. Prints the text screen.
 *
 *   PC_ROOT=dir [FLOPPY=1] ./pchost /pc/c.img SECONDS ["text to type\n"]
 *
 * HID="text" types as a BLE keyboard would, through pc_keys_report and its
 * dead keys, with a key table of its own (below) so that a mistake in
 * pc_keys.c's shows instead of cancelling out. CP=437 for that code page.
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

/* A US keyboard's keys, written independently of pc_keys.c: the character
 * each key types unshifted and shifted, by HID usage. */
static const struct { uint8_t usage; char plain, shifted; } us[] = {
    {0x04,'a','A'},{0x05,'b','B'},{0x06,'c','C'},{0x07,'d','D'},{0x08,'e','E'},{0x09,'f','F'},
    {0x0A,'g','G'},{0x0B,'h','H'},{0x0C,'i','I'},{0x0D,'j','J'},{0x0E,'k','K'},{0x0F,'l','L'},
    {0x10,'m','M'},{0x11,'n','N'},{0x12,'o','O'},{0x13,'p','P'},{0x14,'q','Q'},{0x15,'r','R'},
    {0x16,'s','S'},{0x17,'t','T'},{0x18,'u','U'},{0x19,'v','V'},{0x1A,'w','W'},{0x1B,'x','X'},
    {0x1C,'y','Y'},{0x1D,'z','Z'},{0x1E,'1','!'},{0x1F,'2','@'},{0x20,'3','#'},{0x21,'4','$'},
    {0x22,'5','%'},{0x23,'6','^'},{0x24,'7','&'},{0x25,'8','*'},{0x26,'9','('},{0x27,'0',')'},
    {0x28,'\r','\r'},{0x2C,' ',' '},{0x2D,'-','_'},{0x2E,'=','+'},{0x2F,'[','{'},{0x30,']','}'},
    {0x31,'\\','|'},{0x33,';',':'},{0x34,'\'','"'},{0x35,'`','~'},{0x36,',','<'},{0x37,'.','>'},
    {0x38,'/','?'},
};

static void hid_char(char c)
{
    for (unsigned i = 0; i < sizeof us / sizeof us[0]; i++) {
        if (us[i].plain != c && us[i].shifted != c) continue;
        uint8_t r[8] = {0};
        r[0] = us[i].plain == c ? 0 : 0x02;
        r[2] = us[i].usage;
        pc_keys_report(r);
        pc_core_run(20000);
        r[2] = 0;
        pc_keys_report(r);
        r[0] = 0;
        pc_keys_report(r);
        pc_core_run(20000);
        return;
    }
    fprintf(stderr, "pchost: no key for '%c'\n", c);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: pchost /path/c.img SECONDS [text]\n"); return 2; }
    /* FLOPPY=1: the image is A: (and boots), with no C: */
    const bool floppy = getenv("FLOPPY") != NULL;
    if (!(floppy ? pc_core_init(NULL, argv[1]) : pc_core_init(argv[1], NULL))) { fprintf(stderr, "pc: %s\n", pc_core_error()); return 1; }
    if (getenv("CP")) pc_keys_set_codepage(atoi(getenv("CP")));
    const int secs = atoi(argv[2]);
    const char *hid = getenv("HID");
    const char *type = argc > 3 ? argv[3] : NULL;
    for (int t = 0; t < secs * 10; t++) {
        pc_core_run(100000);
        if (type && *type && t >= secs * 10 / 2) pc_keys_type(*type++);
        if (hid && *hid && t >= secs * 10 / 2) hid_char(*hid++);
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
