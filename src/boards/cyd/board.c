/* board.c - heap facts about the CYD. See board.h. */
#include "board.h"
#include "esp_heap_caps.h"

unsigned int board_free_heap(void) {
    return (unsigned int)heap_caps_get_free_size(MALLOC_CAP_8BIT);
}

unsigned int board_largest_block(void) {
    return (unsigned int)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
}

void board_heap_report(void) { heap_caps_print_heap_info(MALLOC_CAP_8BIT); }
