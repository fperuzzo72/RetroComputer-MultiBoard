#ifndef MSX_CARTS_H
#define MSX_CARTS_H
#ifdef __cplusplus
extern "C" {
#endif

/* The cartridges built into this firmware. See msx_carts.c. */
int         msx_cart_count_get(void);
const char *msx_cart_name(int i);       /* -1 gives "no cartridge" */
int         msx_cart_selected(void);    /* -1 = boot into BASIC */
void        msx_cart_select(int i);     /* remembered in NVS */
void        msx_cart_list(void);

const unsigned char *msx_cart_image(void);
int                  msx_cart_size(void);
#ifdef RETRO_BOARD_ANDROID
/* The chosen cartridge's file on the card, or NULL. Android only. */
const char *msx_cart_path(void);
#endif

#ifdef __cplusplus
}
#endif
#endif
