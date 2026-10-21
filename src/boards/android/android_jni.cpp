/* android_jni.cpp - the Android board's platform side: the Surface, the
 * keys, the menu and the log, between RetroActivity (Kotlin) and
 * retro_board.c.
 *
 * C++ only because JNI is. It must not include MSX.h or Z80.h (see the
 * C/C++ wall in CLAUDE.md): it talks to the machines through machine.h and
 * retro_board.h, both plain C.
 *
 * The picture: a thread of its own looks at the board's picture every
 * 15ms and posts to the Surface only when its sequence number has moved,
 * scaled by the largest whole factor that fits (nearest pixel, so the
 * machine's pixels stay square and sharp) and centred on its border
 * colour. On e-ink every post is a refresh; an unchanged frame costs
 * nothing this way. The window is asked for at its own size, as the
 * Android CrossPoint learned: a buffer of any other size is resampled by
 * the compositor, and a resampled pixel picture is a blurred one.
 */
#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

extern "C" {
#include "machine.h"
#include "retro_board.h"
#include "selector.h"
}

#define TAG "Retro"

static JavaVM *g_vm;
static jclass g_native;   /* org.retrocomputer.multiboard.RetroNative */

/* --- stdout and stderr into logcat ---------------------------------------
 * The machines say what they are doing with printf (the MSX's ROMs, the
 * sound's rate, the cartridge loaded). On Android that goes nowhere unless
 * it is carried over. */
static void *log_pump(void *arg)
{
    const int fd = (int)(intptr_t)arg;
    char buf[1024];
    std::string line;
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                __android_log_write(ANDROID_LOG_INFO, TAG, line.c_str());
                line.clear();
            } else {
                line += buf[i];
            }
        }
    }
    return nullptr;
}

static void route_stdio_to_logcat()
{
    int p[2];
    if (pipe(p) != 0) return;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(p[1], 1);
    dup2(p[1], 2);
    pthread_t t;
    pthread_create(&t, nullptr, log_pump, (void *)(intptr_t)p[0]);
    pthread_detach(t);
}

/* --- calls into Kotlin, from any thread ------------------------------------- */

static void call_static(const char *name)
{
    JNIEnv *env = nullptr;
    bool attached = false;
    if (g_vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jmethodID m = env->GetStaticMethodID(g_native, name, "()V");
    if (m) env->CallStaticVoidMethod(g_native, m);
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
}

static void menu_hook() { call_static("onMenu"); }
static void restart_hook() { call_static("onRestart"); }

/* --- the picture ------------------------------------------------------------ */

static std::mutex win_lock;
static ANativeWindow *win;
static std::atomic<bool> win_new{false};

static uint32_t rgbx(uint16_t c)
{
    const uint32_t r = ((c >> 11) & 31) * 255 / 31, g = ((c >> 5) & 63) * 255 / 63, b = (c & 31) * 255 / 31;
    return 0xFF000000u | (b << 16) | (g << 8) | r;   /* RGBX_8888, little endian */
}

static void present(const uint16_t *pic, int w, int h, uint16_t border)
{
    std::lock_guard<std::mutex> lock(win_lock);
    if (!win) return;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(win, &buf, nullptr) != 0) return;
    const int W = buf.width, H = buf.height;
    int k = W / w < H / h ? W / w : H / h;
    if (k < 1) k = 1;
    const int x0 = (W - w * k) / 2, y0 = (H - h * k) / 2;
    const uint32_t bc = rgbx(border);
    uint32_t *px = static_cast<uint32_t *>(buf.bits);
    static uint32_t lut[65536];
    static bool lut_ok;
    if (!lut_ok) { for (int i = 0; i < 65536; i++) lut[i] = rgbx((uint16_t)i); lut_ok = true; }
    for (int y = 0; y < H; y++) {
        uint32_t *row = px + (size_t)y * buf.stride;   /* stride in pixels */
        const int sy = (y - y0) / k;
        if (y < y0 || sy >= h) { for (int x = 0; x < W; x++) row[x] = bc; continue; }
        const uint16_t *src = pic + (size_t)sy * w;
        int x = 0;
        for (; x < x0 && x < W; x++) row[x] = bc;
        for (int sx = 0; sx < w && x < W; sx++) {
            const uint32_t c = lut[src[sx]];
            for (int r = 0; r < k && x < W; r++) row[x++] = c;
        }
        for (; x < W; x++) row[x] = bc;
    }
    ANativeWindow_unlockAndPost(win);
}

static void *present_loop(void *)
{
    static uint16_t pic[216 * 320];
    uint32_t shown = 0;
    for (;;) {
        int w, h;
        uint16_t border;
        const uint32_t seq = retro_board_picture(nullptr, &w, &h, &border);
        if (seq != shown || win_new.exchange(false)) {
            retro_board_picture(pic, &w, &h, &border);
            present(pic, w, h, border);
            shown = seq;
        }
        usleep(15000);
    }
    return nullptr;
}

/* --- keys --------------------------------------------------------------------
 * Android key codes (android/keycodes.h) to HID usages. The physical key,
 * not the character: the machines apply their own layout, US-International
 * dead keys included, as they do on the boards. */
static uint8_t usage_of(int kc)
{
    if (kc >= 29 && kc <= 54) return (uint8_t)(0x04 + kc - 29);      /* A..Z */
    if (kc >= 8 && kc <= 16) return (uint8_t)(0x1E + kc - 8);        /* 1..9 */
    if (kc >= 131 && kc <= 142) return (uint8_t)(0x3A + kc - 131);   /* F1..F12 */
    switch (kc) {
    case 7: return 0x27;                 /* 0 */
    case 66: return 0x28;                /* ENTER */
    case 111: return 0x29;               /* ESCAPE */
    case 67: return 0x2A;                /* DEL (backspace) */
    case 61: return 0x2B;                /* TAB */
    case 62: return 0x2C;                /* SPACE */
    case 69: return 0x2D;                /* MINUS */
    case 70: return 0x2E;                /* EQUALS */
    case 71: return 0x2F;                /* [ */
    case 72: return 0x30;                /* ] */
    case 73: return 0x31;                /* backslash */
    case 74: return 0x33;                /* ; */
    case 75: return 0x34;                /* ' */
    case 68: return 0x35;                /* ` */
    case 55: return 0x36;                /* , */
    case 56: return 0x37;                /* . */
    case 76: return 0x38;                /* / */
    case 115: return 0x39;               /* CAPS LOCK */
    case 120: return 0x46;               /* SYSRQ / PrintScreen */
    case 116: return 0x47;               /* SCROLL LOCK */
    case 121: return 0x48;               /* BREAK / Pause */
    case 124: return 0x49;               /* INSERT */
    case 122: return 0x4A;               /* HOME */
    case 92: return 0x4B;                /* PAGE UP */
    case 112: return 0x4C;               /* FORWARD DEL */
    case 123: return 0x4D;               /* END */
    case 93: return 0x4E;                /* PAGE DOWN */
    case 22: return 0x4F;                /* RIGHT */
    case 21: return 0x50;                /* LEFT */
    case 20: return 0x51;                /* DOWN */
    case 19: return 0x52;                /* UP */
    case 143: return 0x53;               /* NUM LOCK */
    case 154: return 0x54;               /* NUMPAD / */
    case 155: return 0x55;               /* NUMPAD * */
    case 156: return 0x56;               /* NUMPAD - */
    case 157: return 0x57;               /* NUMPAD + */
    case 160: return 0x58;               /* NUMPAD ENTER */
    case 145: case 146: case 147: case 148: case 149:
    case 150: case 151: case 152: case 153:
        return (uint8_t)(0x59 + kc - 145);   /* NUMPAD 1..9 */
    case 144: return 0x62;               /* NUMPAD 0 */
    case 158: return 0x63;               /* NUMPAD . */
    case 113: return 0xE0;               /* CTRL LEFT */
    case 59: return 0xE1;                /* SHIFT LEFT */
    case 57: return 0xE2;                /* ALT LEFT */
    case 117: return 0xE3;               /* META LEFT */
    case 114: return 0xE4;               /* CTRL RIGHT */
    case 60: return 0xE5;                /* SHIFT RIGHT */
    case 58: return 0xE6;                /* ALT RIGHT (AltGr) */
    case 118: return 0xE7;               /* META RIGHT */
    }
    return 0;
}

/* --- JNI ---------------------------------------------------------------------- */

extern "C" JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *)
{
    g_vm = vm;
    JNIEnv *env;
    if (vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    /* found here, on the main thread: FindClass from a native thread only
     * sees the system's classes */
    jclass c = env->FindClass("org/retrocomputer/multiboard/RetroNative");
    g_native = (jclass)env->NewGlobalRef(c);
    return JNI_VERSION_1_6;
}

#define FN(name) Java_org_retrocomputer_multiboard_RetroNative_##name

extern "C" JNIEXPORT void JNICALL FN(nativeStart)(JNIEnv *env, jclass, jstring dir)
{
    static bool started;
    if (started) return;
    started = true;
    route_stdio_to_logcat();
    const char *d = env->GetStringUTFChars(dir, nullptr);
    retro_board_set_storage(d);
    env->ReleaseStringUTFChars(dir, d);
    retro_board_menu_hook = menu_hook;
    retro_board_restart_hook = restart_hook;
    retro_board_start();
    pthread_t t;
    pthread_create(&t, nullptr, present_loop, nullptr);
    pthread_detach(t);
}

extern "C" JNIEXPORT void JNICALL FN(nativeSetSurface)(JNIEnv *env, jclass, jobject surface)
{
    std::lock_guard<std::mutex> lock(win_lock);
    if (win) { ANativeWindow_release(win); win = nullptr; }
    if (!surface) return;
    win = ANativeWindow_fromSurface(env, surface);
    if (!win) return;
    /* its own size: see the comment at the top */
    ANativeWindow_setBuffersGeometry(win, ANativeWindow_getWidth(win), ANativeWindow_getHeight(win),
                                     WINDOW_FORMAT_RGBX_8888);
    win_new = true;
}

/* true if the key is the machine's (consumed) */
extern "C" JNIEXPORT jboolean JNICALL FN(nativeKey)(JNIEnv *, jclass, jint keycode, jboolean down)
{
    const uint8_t u = usage_of(keycode);
    if (!u) return JNI_FALSE;
    retro_board_key(u, down ? 1 : 0);
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL FN(nativeOpenMenu)(JNIEnv *, jclass) { selector_open(); }
extern "C" JNIEXPORT void JNICALL FN(nativeMenuDone)(JNIEnv *, jclass, jint entry) { retro_board_menu_done(entry); }
extern "C" JNIEXPORT void JNICALL FN(nativeChooseMachine)(JNIEnv *, jclass, jint m, jint e) { retro_board_choose_machine(m, e); }

extern "C" JNIEXPORT jint JNICALL FN(nativeMachineCount)(JNIEnv *, jclass) { return machine_count; }
extern "C" JNIEXPORT jint JNICALL FN(nativeCurrentMachine)(JNIEnv *, jclass) { return machine_chosen_index(); }
extern "C" JNIEXPORT jstring JNICALL FN(nativeMachineName)(JNIEnv *env, jclass, jint m)
{
    return env->NewStringUTF(m >= 0 && m < machine_count ? machine_list[m]->name : "");
}
extern "C" JNIEXPORT jint JNICALL FN(nativeEntryCount)(JNIEnv *, jclass, jint m)
{
    return m >= 0 && m < machine_count ? machine_list[m]->entry_count() : 0;
}
extern "C" JNIEXPORT jstring JNICALL FN(nativeEntryName)(JNIEnv *env, jclass, jint m, jint e)
{
    return env->NewStringUTF(m >= 0 && m < machine_count ? machine_list[m]->entry_name(e) : "");
}
extern "C" JNIEXPORT jint JNICALL FN(nativeSelectedEntry)(JNIEnv *, jclass)
{
    return machine ? machine->selected_entry() : 0;
}
