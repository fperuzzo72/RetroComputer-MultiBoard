/* rtc.c - NOT UPSTREAM: the Mac Plus's real-time clock (the 343-0042-B
 * chip), which umac leaves out: its three VIA lines were ignored, the Mac
 * read zeros, and its clock said 1904 every time it started.
 *
 * The chip holds a 32-bit count of seconds since midnight, 1 January 1904,
 * local time, and 20 bytes of parameter RAM. The Mac talks to it serially
 * on VIA port B: bit 2 low selects it, bit 1 is the clock, bit 0 the data,
 * most significant bit first, a bit taken on each rising edge of the clock.
 * The first byte is a command, z a a a a a 0 1 with z = 1 for a read:
 *
 *   z00000 01 .. z00011 01   seconds, bytes 0 (low) to 3
 *   z00100 01 .. z00111 01   the same four again (the ROM reads the time
 *                            through both and compares)
 *   0011 0001                test register (written only)
 *   0011 0101                write-protect register (written only)
 *   z010aa 01                parameter RAM 0x10-0x13
 *   z1aaaa 01                parameter RAM 0x00-0x0F
 *
 * A write's data byte follows; for a read the Mac turns the data line round
 * and clocks eight more times while the chip drives it. Here the chip puts
 * a bit out on the clock's falling edge and moves to the next on the rising
 * one, so the Mac finds the bit there whichever edge it reads after.
 *
 * The seconds come from the host's own clock, as local time (the Mac has no
 * time zones), plus whatever offset the Mac set when someone changed the
 * time in its Control Panel. (Source: Inside Macintosh III, "The Real-Time
 * Clock"; Mini vMac's RTC emulation for the behaviour.)
 */
#include "umac_rtc.h"

#include <time.h>

#define MAC_EPOCH_OFFSET 2082844800u   /* 1904-01-01 to 1970-01-01, in seconds */

static uint8_t pram[20];
static int32_t offset;          /* the Mac's time minus the host's */
static uint8_t write_protect;
void (*umac_rtc_pram_written)(void);

static uint8_t last_port = 0x07;
static uint8_t shift;           /* bits coming in */
static int count;               /* of the current byte */
static int sending;             /* the chip is driving the data line */
static uint8_t out_byte;
static uint8_t out_bit = 1;
static int have_cmd;
static uint8_t cmd;

uint8_t *umac_rtc_pram(void) { return pram; }
uint8_t umac_rtc_data(void) { return out_bit; }

/* The host's local time, as seconds since 1904. */
static uint32_t host_seconds(void)
{
    const time_t now = time(NULL);
    struct tm g;
    gmtime_r(&now, &g);
    g.tm_isdst = -1;
    const time_t as_if_local = mktime(&g);     /* now, read as local: now - zone offset */
    const long zone = (long)(now - as_if_local);
    return (uint32_t)((int64_t)now + zone + MAC_EPOCH_OFFSET);
}

static uint32_t mac_seconds(void) { return host_seconds() + (uint32_t)offset; }

/* Which register a command names: 0-3 seconds, 4 test, 5 write-protect,
 * 0x10+n parameter RAM n, -1 none. */
static int reg_of(uint8_t c)
{
    if ((c & 0x03) != 0x01) return -1;
    const uint8_t a = (uint8_t)((c >> 2) & 0x1F);
    if (a < 8) return a & 3;                                /* seconds, both addresses */
    if (a == 0x0C) return 4;
    if (a == 0x0D) return 5;
    if ((a & 0x1C) == 0x08) return 0x10 + 0x10 + (a & 3);   /* PRAM 0x10-0x13 */
    if (a & 0x10) return 0x10 + (a & 0x0F);                 /* PRAM 0x00-0x0F */
    return -1;
}

static uint8_t read_reg(int r)
{
    if (r >= 0 && r < 4) return (uint8_t)(mac_seconds() >> (8 * r));
    if (r >= 0x10) return pram[r - 0x10];
    return 0;
}

static void write_reg(int r, uint8_t v)
{
    if (r == 5) { write_protect = v & 0x80; return; }
    if (r == 4 || write_protect) return;
    if (r >= 0 && r < 4) {
        uint32_t s = mac_seconds();
        s = (s & ~(0xFFu << (8 * r))) | ((uint32_t)v << (8 * r));
        offset = (int32_t)(s - host_seconds());
        return;
    }
    if (r >= 0x10) {
        pram[r - 0x10] = v;
        if (umac_rtc_pram_written) umac_rtc_pram_written();
    }
}

static void reset(void)
{
    count = 0;
    shift = 0;
    sending = 0;
    have_cmd = 0;
    out_bit = 1;
}

#ifdef UMAC_RTC_DEBUG
#include <stdio.h>
#endif

void umac_rtc_port(uint8_t portb)
{
#ifdef UMAC_RTC_DEBUG
    if ((portb ^ last_port) & 7) fprintf(stderr, "RTC port %d%d%d (en clk data) sending=%d count=%d\n", (portb >> 2) & 1, (portb >> 1) & 1, portb & 1, sending, count);
#endif
    const uint8_t was = last_port;
    last_port = portb;
    if (portb & 0x04) {             /* not selected */
        if (!(was & 0x04)) reset();
        return;
    }
    if (was & 0x04) reset();        /* just selected */

    const int rising = !(was & 0x02) && (portb & 0x02);
    const int falling = (was & 0x02) && !(portb & 0x02);

    if (sending) {
        if (falling) out_bit = (out_byte >> (7 - count)) & 1;
        /* the Mac reads the last bit after this edge: leave it on the line */
        if (rising && ++count == 8) { sending = 0; count = 0; shift = 0; }
        return;
    }
    if (!rising) return;
    shift = (uint8_t)((shift << 1) | (portb & 1));
    if (++count < 8) return;
    count = 0;
    if (!have_cmd) {
        cmd = shift;
#ifdef UMAC_RTC_DEBUG
        fprintf(stderr, "RTC cmd %02X reg %d\n", cmd, reg_of(cmd));
#endif
        if (cmd & 0x80) {           /* a read: the chip answers */
            out_byte = read_reg(reg_of(cmd));
            out_bit = (out_byte >> 7) & 1;
            sending = 1;
        } else {
            have_cmd = 1;           /* a write: its data byte next */
        }
    } else {
        write_reg(reg_of(cmd), shift);
        have_cmd = 0;
    }
}
