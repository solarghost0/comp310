/*
 * kernel_main.c - Basic Kernel Entry Point
 */

#include <stdint.h>
#include <stddef.h>

/* ─── VGA Text Mode ─────────────────────────────────────────── */

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   ((uint16_t *)0xB8000)

static size_t cursor_row = 0;
static size_t cursor_col = 0;

typedef enum {
    COLOR_BLACK   = 0,
    COLOR_WHITE   = 15,
    COLOR_CYAN    = 11,
    COLOR_GREEN   = 10,
    COLOR_RED     = 12,
} vga_color;

static inline uint8_t vga_entry_color(vga_color fg, vga_color bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void terminal_clear(void) {
    uint8_t color = vga_entry_color(COLOR_WHITE, COLOR_BLACK);
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_ADDR[y * VGA_WIDTH + x] = vga_entry(' ', color);
    cursor_row = 0;
    cursor_col = 0;
}

static void terminal_scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_ADDR[(y - 1) * VGA_WIDTH + x] = VGA_ADDR[y * VGA_WIDTH + x];

    uint8_t color = vga_entry_color(COLOR_WHITE, COLOR_BLACK);
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA_ADDR[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', color);
}

static void terminal_putchar(char c, uint8_t color) {
    if (c == '\n') {
        cursor_col = 0;
        if (++cursor_row == VGA_HEIGHT) {
            terminal_scroll();
            cursor_row = VGA_HEIGHT - 1;
        }
        return;
    }

    VGA_ADDR[cursor_row * VGA_WIDTH + cursor_col] = vga_entry(c, color);

    if (++cursor_col == VGA_WIDTH) {
        cursor_col = 0;
        if (++cursor_row == VGA_HEIGHT) {
            terminal_scroll();
            cursor_row = VGA_HEIGHT - 1;
        }
    }
}

static void terminal_write(const char *str, uint8_t color) {
    for (size_t i = 0; str[i] != '\0'; i++)
        terminal_putchar(str[i], color);
}

static void terminal_writeln(const char *str, uint8_t color) {
    terminal_write(str, color);
    terminal_putchar('\n', color);
}

/* ─── Kernel Main ───────────────────────────────────────────── */

void kernel_main(void) {
    terminal_clear();

    uint8_t header = vga_entry_color(COLOR_CYAN,  COLOR_BLACK);
    uint8_t normal = vga_entry_color(COLOR_WHITE, COLOR_BLACK);
    uint8_t ok     = vga_entry_color(COLOR_GREEN, COLOR_BLACK);

    terminal_writeln("========================================", header);
    terminal_writeln("         My Kernel  v0.1.0             ", header);
    terminal_writeln("========================================", header);
    terminal_putchar('\n', normal);

    terminal_writeln("Initializing kernel...", normal);
    terminal_write  ("  [", normal);
    terminal_write  (" OK ", ok);
    terminal_writeln("] VGA text mode", normal);
    terminal_write  ("  [", normal);
    terminal_write  (" OK ", ok);
    terminal_writeln("] Kernel loaded", normal);
    terminal_putchar('\n', normal);

    terminal_writeln("System ready.", normal);

    /* Halt the CPU */
    for (;;)
        __asm__ volatile ("hlt");
}