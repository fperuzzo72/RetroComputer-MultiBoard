/* ble_keyboard.cpp - BLE HID keyboard host (central) for FNK0103 MSX.
 *
 * Adapted from the connect/subscribe pattern in esp32beans/BLE_HID_Client
 * (MIT license, see /third_party_licenses/BLE_HID_Client.txt), simplified
 * and retargeted at the standard Boot Keyboard Input Report (0x2A22).
 *
 * Written against NimBLE-Arduino 2.x, which is the version that matches
 * Arduino-ESP32 3.x: on 1.x the firmware compiles but aborts during
 * NimBLEDevice::init() (see the note in platformio.ini).
 *
 * This file is only the transport. Everything about what the keys MEAN
 * belongs to whichever machine is built in, behind machine->hid_report();
 * this file knows nothing about MSX or Spectrum, and they know nothing
 * about BLE.
 */
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <cstring>
#include "ble_keyboard.h"
#include "machine.h"

static const char HID_SERVICE_UUID[]        = "1812";
static const char HID_BOOT_KBD_INPUT_UUID[] = "2a22"; /* Boot Keyboard Input Report */
static const char HID_PROTOCOL_MODE_UUID[]  = "2a4e"; /* 0 = boot protocol         */
static const char HID_REPORT_DATA_UUID[]    = "2a4d"; /* generic Report (fallback) */

static NimBLEAddress sTarget;
static volatile bool sHaveTarget = false;
static volatile bool sConnected  = false;

/* The keyboard we last talked to, remembered across reboots.
 *
 * A keyboard that has already been paired does not necessarily put the
 * HID service UUID in its advertisements when it comes back: a bonded
 * device often sends a short advertisement carrying little more than its
 * address. Matching only on "advertises service 0x1812" therefore ignores
 * exactly the device we want, which is why it only ever connected after
 * being put into pairing mode again. This has nothing to do with an SD
 * card - the bond itself lives in NVS, in flash. */
static Preferences sPrefs;
static NimBLEAddress sKnown;
static bool sHaveKnown = false;

static void rememberKeyboard(const NimBLEAddress &addr) {
    std::string str = addr.toString();
    sPrefs.begin("msxkbd", false);
    sPrefs.putString("addr", str.c_str());
    sPrefs.putUChar("type", addr.getType());
    sPrefs.end();
    sKnown = addr;
    sHaveKnown = true;
}

static void loadKnownKeyboard() {
    if (!sPrefs.begin("msxkbd", true)) return; /* nothing stored yet */
    String str = sPrefs.getString("addr", "");
    uint8_t type = sPrefs.getUChar("type", 0);
    sPrefs.end();
    if (str.length() >= 17) {
        sKnown = NimBLEAddress(std::string(str.c_str()), type);
        sHaveKnown = true;
        Serial.printf("BLE: will also answer to the keyboard it saw last, %s\n", str.c_str());
    }
}

/* Latest 8-byte boot keyboard report: [modifiers, reserved, key1..key6].
 * Written by the NimBLE host task, read by the emulation task, so it is
 * guarded - the two run on different cores. */
static portMUX_TYPE sReportMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t sReport[8] = {0};

/* Diagnostics. A keyboard that pairs and then types nothing looks exactly
 * like a keyboard that pairs and sends reports we throw away, and the only
 * way to tell is to look at the bytes. `k 1` on the serial console turns
 * the dump on. */
static volatile uint32_t sAdvSeen = 0;
static volatile uint32_t sNotifyCount = 0;
static volatile uint8_t  sLastLen = 0;
static volatile bool     sLogReports = false;

void ble_keyboard_log_reports(int on) { sLogReports = on ? true : false; }
unsigned long ble_keyboard_report_count(void) { return (unsigned long)sNotifyCount; }
unsigned long ble_keyboard_adverts_seen(void) { return (unsigned long)sAdvSeen; }

/* Stop or restart the scan. Scanning is not free: the radio and its
 * callbacks run on the other core, and this is here to be able to measure
 * what that costs the emulation rather than argue about it. */
void ble_keyboard_scan(int on) {
    if (on) NimBLEDevice::getScan()->start(0, false, true);
    else    NimBLEDevice::getScan()->stop();
}

static void notifyCB(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
    (void)isNotify;
    sNotifyCount++;
    sLastLen = (uint8_t)len;

    if (sLogReports) {
        Serial.printf("HID %s len %u:", chr->getUUID().toString().c_str(), (unsigned)len);
        for (size_t i = 0; i < len && i < 16; i++) Serial.printf(" %02X", data[i]);

        /* 0x01 in a key slot is ErrorRollOver: the keyboard is telling us
         * it cannot say which keys are down, because the combination
         * being held is one its matrix cannot resolve. Worth naming, or
         * it reads as just another number and the fault gets blamed on
         * the emulator. */
        /* Name the modifier bits. Which of them a small keyboard's thumb
         * keys send depends on whether it is in its Mac or its Windows
         * mode, and that decides what they can be mapped to. */
        if (len >= 1 && data[0]) {
            static const char *kMod[8] = {
                "LCtrl", "LShift", "LAlt", "LGUI",
                "RCtrl", "RShift", "AltGr", "RGUI"
            };
            Serial.print("   [");
            for (int b = 0, first = 1; b < 8; b++)
                if (data[0] & (1 << b)) {
                    if (!first) Serial.print(" ");
                    Serial.print(kMod[b]);
                    first = 0;
                }
            Serial.print("]");
        }

        int rollover = 0, held = 0;
        for (size_t i = 2; i < len && i < 16; i++) {
            if (data[i] == 0x01) rollover = 1;
            else if (data[i] != 0x00) held++;
        }
        if (rollover) Serial.print("   <- keyboard cannot report this combination");
        else if (held) Serial.printf("   (%d key%s down)", held, held == 1 ? "" : "s");
        Serial.println();
    }

    /* A boot keyboard report is [modifiers, reserved, key1..key6]. Some
     * keyboards notify a 9-byte report whose first byte is the HID Report
     * ID; the rest is the same eight bytes. Anything shorter is some other
     * report (consumer keys, a mouse) and is not ours. */
    const uint8_t *p = data;
    if (len == 9) { p = data + 1; len = 8; }
    if (len < 8) return;

    portENTER_CRITICAL(&sReportMux);
    memcpy(sReport, p, 8);
    portEXIT_CRITICAL(&sReportMux);
}

class KbdScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *dev) override {
        /* The keyboard we bonded with last time gets in on its address
         * alone, whatever it chooses to advertise. */
        if (sHaveKnown && dev->getAddress() == sKnown) {
            Serial.printf("BLE: the keyboard from last time is back (%s), connecting\n",
                          dev->getAddress().toString().c_str());
            sTarget = dev->getAddress();
            sHaveTarget = true;
            NimBLEDevice::getScan()->stop();
            return;
        }
        /* Accept on EITHER the advertised HID service or an appearance
         * that says keyboard. A device that has already been bonded often
         * advertises neither its services nor a stable address when it
         * comes back - it uses a resolvable private address that changes
         * every time - so insisting on the service UUID is what made it
         * connect only while in pairing mode. */
        const uint16_t appearance = dev->getAppearance();
        const bool saysHid = dev->haveServiceUUID() &&
                             dev->isAdvertisingService(NimBLEUUID(HID_SERVICE_UUID));
        const bool saysKeyboard = (appearance == 0x3C1);
        if (!saysHid && !saysKeyboard) {
            /* Counted rather than printed: printing from the scan
             * callback put UART traffic in the middle of the emulation's
             * frame budget. `h` on the console reports the count. */
            sAdvSeen++;
            return;
        }
        /* A HID service that is explicitly something other than a
         * keyboard (a mouse, say) is not ours. */
        if (appearance != 0x3C1 && appearance != 0x3C0 && appearance != 0) return;

        Serial.printf("BLE: HID device %s (appearance 0x%04X), connecting\n",
                      dev->getAddress().toString().c_str(), appearance);
        sTarget = dev->getAddress();
        sHaveTarget = true;
        NimBLEDevice::getScan()->stop();
    }
};

class KbdClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *c) override {
        c->updateConnParams(12, 12, 0, 150);
    }
    void onDisconnect(NimBLEClient *c, int reason) override {
        (void)c; (void)reason;
        sConnected = false;
        Serial.printf("BLE: disconnected (reason %d), scanning again\n", reason);
        portENTER_CRITICAL(&sReportMux);
        memset(sReport, 0, sizeof(sReport));
        portEXIT_CRITICAL(&sReportMux);
        NimBLEDevice::getScan()->start(0, false, true);
    }
    void onPassKeyEntry(NimBLEConnInfo &connInfo) override {
        NimBLEDevice::injectPassKey(connInfo, 0);
    }
    void onConfirmPasskey(NimBLEConnInfo &connInfo, uint32_t pin) override {
        (void)pin;
        NimBLEDevice::injectConfirmPasskey(connInfo, true);
    }
};

static KbdClientCallbacks sClientCB;

static bool connectToKeyboard() {
    NimBLEClient *client = NimBLEDevice::createClient();
    client->setClientCallbacks(&sClientCB, false);
    client->setConnectTimeout(7000);

    if (!client->connect(sTarget)) {
        Serial.println("BLE: connect failed");
        NimBLEDevice::deleteClient(client);
        return false;
    }

    NimBLERemoteService *hid = client->getService(HID_SERVICE_UUID);
    if (!hid) {
        Serial.println("BLE: connected but no HID service, dropping");
        client->disconnect();
        return false;
    }

    /* Best effort: ask for boot protocol. Not every device exposes this. */
    NimBLERemoteCharacteristic *proto = hid->getCharacteristic(HID_PROTOCOL_MODE_UUID);
    if (proto && proto->canWrite()) {
        uint8_t bootMode = 0x00;
        bool ok = proto->writeValue(&bootMode, 1, true);
        Serial.printf("BLE: asked for boot protocol: %s\n", ok ? "accepted" : "refused");
    } else {
        Serial.println("BLE: no writable Protocol Mode, device stays in report protocol");
    }

    /* Subscribe to the boot report AND to every generic report
     * characteristic, not one or the other.
     *
     * This is what stopped a paired keyboard from typing: plenty of
     * keyboards expose the Boot Keyboard Input Report and then never
     * notify on it, because they stay in Report Protocol mode and send
     * everything on 0x2A4D. The Protocol Mode write above is only a
     * request, and a device is free to ignore it or not expose it as
     * writable at all. Subscribing to the boot report alone therefore
     * gives a connection that is up, subscribed, and permanently silent.
     * Listening to both costs nothing: notifyCB keeps whatever is shaped
     * like a keyboard report and ignores the rest. */
    int subCount = 0;
    NimBLERemoteCharacteristic *bootKbd = hid->getCharacteristic(HID_BOOT_KBD_INPUT_UUID);
    if (bootKbd && bootKbd->canNotify() && bootKbd->subscribe(true, notifyCB)) {
        subCount++;
        Serial.println("BLE: subscribed to the boot keyboard report (0x2A22)");
    }

    const std::vector<NimBLERemoteCharacteristic *> &chars = hid->getCharacteristics(true);
    for (auto c : chars) {
        if (c->getUUID() == NimBLEUUID(HID_REPORT_DATA_UUID) && c->canNotify()) {
            if (c->subscribe(true, notifyCB)) {
                subCount++;
                Serial.printf("BLE: subscribed to a report characteristic (0x2A4D, handle %u)\n",
                              (unsigned)c->getHandle());
            }
        }
    }
    Serial.printf("BLE: %d notifying characteristic(s) subscribed\n", subCount);
    bool subscribed = subCount > 0;

    if (!subscribed) {
        Serial.println("BLE: no report characteristic to subscribe to, dropping");
        client->disconnect();
        return false;
    }

    sConnected = true;
    rememberKeyboard(sTarget);
    Serial.printf("BLE: keyboard connected, free heap %u\n", (unsigned)ESP.getFreeHeap());
    return true;
}

static void bleServiceTask(void *arg);

void ble_keyboard_init() {
    NimBLEDevice::init("FNK0103-MSX");
    NimBLEDevice::setSecurityAuth(true, false, true); /* bond, no MITM, secure connections */
    NimBLEDevice::setPower(9);

    loadKnownKeyboard();

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(new KbdScanCallbacks(), false);
    scan->setInterval(45);
    scan->setWindow(15);
    scan->setActiveScan(true);
    scan->start(0, false, true); /* scan until a HID device turns up */

    /* Core 0, low priority: the machine owns core 1 and must not be made
     * responsible for keeping the keyboard alive. */
    xTaskCreatePinnedToCore(bleServiceTask, "blesvc", 4096, NULL, 2, NULL, 0);
    Serial.println("BLE: scanning for a keyboard");
}

int ble_keyboard_connected() { return sConnected ? 1 : 0; }

void ble_keyboard_poll() {
    /* The connect handshake must not run inside the scan callback, which
     * is the NimBLE host task, so it runs here - and this is called from
     * a service task of our own rather than from the machine.
     *
     * It used to be called from the MSX's per-frame Keyboard() hook, and
     * that is why the keyboard never connected on the Spectrum: nothing
     * over there had any reason to know it was supposed to call this.
     * Servicing the keyboard is the board's job, not the machine's. */
    if (sHaveTarget && !sConnected) {
        sHaveTarget = false;
        if (!connectToKeyboard()) NimBLEDevice::getScan()->start(0, false, true);
    }

    uint8_t report[8];
    portENTER_CRITICAL(&sReportMux);
    memcpy(report, sReport, 8);
    portEXIT_CRITICAL(&sReportMux);

    machine->hid_report(report);
}

/* Pretend a key was pressed on the keyboard that is not there.
 *
 * It has to go in here rather than straight to the machine: this task
 * hands the machine its cached report every time round its loop, so a
 * report delivered any other way is overwritten within a millisecond.
 * That is a race an injected report loses almost always and wins just
 * often enough to look like it worked. */
void ble_keyboard_inject(const uint8_t report[8]) {
    portENTER_CRITICAL(&sReportMux);
    memcpy((void *)sReport, report, 8);
    portEXIT_CRITICAL(&sReportMux);
}

static void bleServiceTask(void *arg) {
    (void)arg;
    for (;;) {
        ble_keyboard_poll();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
