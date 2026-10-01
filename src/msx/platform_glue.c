/* platform_glue.c
 *
 * Implements the "TO BE WRITTEN BY USER" platform hooks that fMSX's core
 * (lib/fmsx_core/fMSX/MSX.c) calls into: machine init/teardown, keyboard
 * polling, joystick/mouse (stubbed - no joystick on this build), disk
 * access (stubbed - C-BIOS doesn't support floppy anyway, see README),
 * and sound (stubbed for now, see README "Known limitations").
 *
 * Plain C, compiled as part of the fmsx_core library alongside MSX.c so
 * it shares C linkage with it (KBD_SET/KBD_RES are macros operating on
 * KeyState[], defined in MSX.c).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "MSX.h"
#include "Sound.h"
#include "ble_keyboard.h"
#include "msx_keys.h"
#include "selector.h"
#include "display.h"
#include "machine.h"
#include "msx_display.h"
#include "msx_beeper.h"
#include "audio.h"
#include "msx_bridge.h"

/* fMSX/MSX.c (LoadFile(), CMOS handling) references this extern global;
 * the reference-platform ports (odroidGo/files.c) define it as their SD
 * working directory. We do the same, matching our SD layout. */
char *fullCurrentDir = "/sdcard/msx/games";

extern volatile int MSXMemoryClaimed;   /* msx_bridge.c */
extern volatile unsigned int MSXFrames; /* msx_bridge.c */

int InitMachine(void) {
    return InitVideo();
}

void TrashMachine(void) {
    TrashVideo();
}

/** Keyboard() ************************************************/
/** Called once per frame, at scanline 192, which is also when */
/** the BIOS scans the matrix. ble_keyboard_poll() services the */
/** BLE connection and hands over the latest HID report;        */
/** msx_keys_frame() turns that into this machine's matrix.     */
/*****************************************************************/
/* ---- the frame profiler, see lib/fmsx_core/fMSX/msx_prof.h ---- */

#include "esp_timer.h"
#include "msx_prof.h"

static long long sProf[3];
static long long sProfFrames;

long long msx_prof_now(void) { return esp_timer_get_time(); }
void msx_prof(int slot, long long since) {
    sProf[slot] += esp_timer_get_time() - since;
}

void msx_prof_report(unsigned long *videoUs, unsigned long *soundUs,
                     unsigned long *loopUs, unsigned long *frames) {
    *frames  = (unsigned long)sProfFrames;
    *videoUs = (unsigned long)sProf[0];
    *soundUs = (unsigned long)sProf[1];
    *loopUs  = (unsigned long)sProf[2];
    sProf[0] = sProf[1] = sProf[2] = sProfFrames = 0;
}

static void pace_note(int64_t now, int64_t behind_us);

void Keyboard(void) {
    sProfFrames++;
    /* First frame: the machine is up and everything it needed off the heap
     * is claimed, so whatever else wants a big block (the BLE stack) can
     * stop waiting. */
    MSXMemoryClaimed = 1;
    MSXFrames++;

    /* The keyboard services itself on its own task now; this hook only
     * turns the latest report into this machine's key matrix. */
    msx_keys_frame();
    msx_beeper_frame();

    selector_poll_open();

    /* While the selector is open the machine stands still. Blocking here
     * is the pause: this hook is called once a frame, so not returning
     * from it is exactly "the machine is not running". */
    if (selector_active()) {
        int chosen = -1;
        msx_beeper_silence();
        while (selector_active()) {
            int e = selector_frame();
            if (e >= 0) chosen = e;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        /* The chooser painted the whole 480x320 panel; this machine only
         * ever draws its own 256x192 in the middle of it, so without this
         * the tiles stay in the surround for as long as the game runs. */
        display_fill_panel(0);
        if (chosen >= 0) machine->switch_to(chosen);
    }

    /* Hand the core back for a tick, once per frame. The emulation task
     * and the video task both sit at priority 5 on core 1 and neither
     * sleeps on its own, so without this the Arduino loop task never runs
     * and the idle task never gets to feed the watchdog. One millisecond
     * out of a 16.7ms frame is a price worth paying for that.
     *
     * And no faster than 60 frames a second, the Hotbit's own rate. On the
     * CYD the machine never gets there - pushing pixels to the panel holds
     * it to 20-37 - so this only ever waits on a board that does not
     * draw at the machine's pace, like the Paper Mono. A frame that comes
     * in late resets the schedule rather than letting the next ones race
     * to catch up. */
    {
        static int64_t next;
        const int64_t now = esp_timer_get_time();
        pace_note(now, next ? now - next : 0);
        if (!next || now - next > 100000) next = now;
        next += (msx_hz() == 50 ? 20000 : 16667) * 100 / msx_speed();
        const int64_t wait = next - now;
        if (wait > 1000) vTaskDelay(pdMS_TO_TICKS((wait + 999) / 1000));
        else vTaskDelay(1);
    }
}

/** Joystick()/Mouse() ****************************************/
/** No physical joystick/mouse wired up on this build. Games   */
/** that support keyboard cursor-key control still work fine   */
/** via the BLE keyboard's arrow keys + space (fire).           */
/*****************************************************************/
unsigned int Joystick(void) { return 0; }
unsigned int Mouse(byte N) { (void)N; return 0; }

/** Floppy disk ***********************************************/
/** DiskPresent()/DiskRead()/DiskWrite() are already provided  */
/** by the core itself, in fMSX/Patch.c - defining them here   */
/** too is a duplicate symbol at link time. There is no floppy  */
/** on this build either way.                                   */
/*****************************************************************/

/* `v` on the console: frames in each of the last 60 wall-clock seconds,
 * and how far behind its schedule the machine got in each, to see a
 * speed-up rather than guess at it. */
#define PACE_SECS 60
static uint16_t pace_frames[PACE_SECS];
static int32_t  pace_behind_ms[PACE_SECS];
static int pace_head, pace_count;

static void pace_note(int64_t now, int64_t behind_us)
{
    static int64_t sec_start;
    static unsigned frames;
    static int64_t worst;
    if (!sec_start) sec_start = now;
    frames++;
    if (behind_us > worst) worst = behind_us;
    if (now - sec_start >= 1000000) {
        pace_frames[pace_head] = (uint16_t)frames;
        pace_behind_ms[pace_head] = (int32_t)(worst / 1000);
        pace_head = (pace_head + 1) % PACE_SECS;
        if (pace_count < PACE_SECS) pace_count++;
        frames = 0;
        worst = 0;
        sec_start = now;
    }
}

void msx_pace_report(void)
{
    printf("frames a second, oldest first (most behind schedule, ms):\n");
    for (int i = 0; i < pace_count; i++) {
        const int k = (pace_head - pace_count + i + PACE_SECS) % PACE_SECS;
        printf("%3u(%ld)%s", pace_frames[k], (long)pace_behind_ms[k], (i % 10 == 9) ? "\n" : " ");
    }
    printf("\n");
}

/** PlayAllSound() **********************************************/
/** Hand the core's mixed PSG/SCC/OPLL output to the DAC. fMSX   */
/** calls this once per frame with the number of microseconds    */
/** that frame covered; RenderAndPlayAudio() turns that into     */
/** samples and calls WriteAudio() in src/audio_glue.c.          */
/*******************************************************************/
/* Mixing the PSG, SCC and OPLL for a frame's worth of samples is real CPU
 * work on a core that is already emulating a Z80 and pushing pixels: it
 * costs about a quarter of the frame rate. Worth having, worth being able
 * to turn off. */
int MSXSoundOn = 1;

void PlayAllSound(int uSec) {
    if (!MSXSoundOn) return;
#ifdef AUDIO_PULLS
    /* The board's audio task mixes at its own rate (boards/eink/audio.c). */
    (void)uSec;
    return;
#endif
    RenderAndPlayAudio((unsigned int)((long long)uSec * GetSndRate() / 1000000));
}
