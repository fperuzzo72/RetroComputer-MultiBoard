#ifndef SD_MOUNT_H
#define SD_MOUNT_H
#ifdef __cplusplus
extern "C" {
#endif

/* Mounts the SD card (SPI: SCK18/MISO19/MOSI23/CS5, see README) at
 * /sdcard using ESP-IDF's FAT+VFS SD driver, so plain fopen()/fread()
 * calls inside the vendored fMSX core (which hardcodes /sdcard/msx/...
 * paths) work unmodified. Returns 1 on success, 0 if no card is present
 * (fMSX will then fall back to the embedded C-BIOS for the main BIOS, but
 * cartridge games still require a working SD card). */
int sd_mount_init(void);

/* Let the card go once the core has read what it needed off it.
 *
 * The FAT driver and its caches cost around 45kB, which on this board is
 * the difference between the BLE stack having room to accept a connection
 * and not. The emulator only touches the card while StartMSX() loads
 * BIOS and cartridge images; after that a cartridge machine does not read
 * its media again, so neither do we. Putting a different game in means
 * rebooting, which is also what it meant in 1985. */
void sd_unmount(void);

#ifdef __cplusplus
}
#endif
#endif
