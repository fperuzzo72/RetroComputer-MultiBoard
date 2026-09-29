#ifndef BOARD_H
#define BOARD_H
#ifdef __cplusplus
extern "C" {
#endif

/* Heap facts about this board, which no machine should have to know.
 *
 * The second number is the one that decides things here: the ESP32's DRAM
 * comes in a few fixed regions and only one of them is large enough for
 * the emulated machine's RAM, so the total free is far less interesting
 * than the largest single block. See docs/MEMORY.md. */
unsigned int board_free_heap(void);
unsigned int board_largest_block(void);

/* Print the region map. */
void board_heap_report(void);

#ifdef __cplusplus
}
#endif
#endif
