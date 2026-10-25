#ifndef CARD_CATALOGUE_H
#define CARD_CATALOGUE_H
/* card_catalogue - a machine's games from a folder of the card, for the
 * Android board.
 *
 * On the ESP32 boards the MSX's cartridges and the Spectrum's snapshots
 * and tapes are built into the firmware (msx_carts.c, spectrum_snap.c,
 * spectrum_tapes.c), so a game starts with no card in the slot. On
 * Android the card is a folder that is always there, so those files ask
 * this instead: the files in <card>/<folder> with the given extensions,
 * in alphabetical order, the one chosen remembered by its file name (a
 * position would move when a game is added), and read into memory when
 * a machine wants its bytes. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct card_catalogue {
    const char *folder;               /* under the card: "msx" */
    const char *const *exts;          /* ".rom", ..., NULL */
    const char *nvs_key;              /* the chosen file's name (15 characters at most) */
    /* filled in on first use */
    int count;                        /* -1 until listed */
    char (*names)[96];                /* shown: the file name without its extension */
    char (*files)[128];               /* the file name */
    int selected;                     /* -2 unread, -1 none */
    unsigned char *bytes;             /* the chosen file, once read */
    int size;
} card_catalogue;

#define CARD_CATALOGUE(folder, exts, key) {folder, exts, key, -1, 0, 0, -2, 0, 0}

int  card_count(card_catalogue *c);
const char *card_name(card_catalogue *c, int i);
int  card_selected(card_catalogue *c);
void card_select(card_catalogue *c, int i);          /* -1: none */
/* The chosen file's full path, or NULL. */
const char *card_path(card_catalogue *c);
/* The chosen file's bytes, read once and kept; NULL if none or unreadable. */
const unsigned char *card_bytes(card_catalogue *c, int *size);

#ifdef __cplusplus
}
#endif
#endif
