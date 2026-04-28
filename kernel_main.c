/**
 * kernel_main.c — Kernel entry point
 *
 * Called by the bootloader (e.g. GRUB via a multiboot-compliant stub in boot.s).
 * Responsible for early hardware init, subsystem bring-up, and the idle loop.
 */
 
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
 
/* ─── VGA text-mode console ─────────────────────────────────────────────── */
 
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   ((volatile uint16_t *)0xB8000)
 
typedef enum {
    COLOR_BLACK   = 0,
    COLOR_BLUE    = 1,
    COLOR_GREEN   = 2,
    COLOR_CYAN    = 3,
    COLOR_RED     = 4,
    COLOR_MAGENTA = 5,
    COLOR_BROWN   = 6,
    COLOR_LGRAY   = 7,
    COLOR_DGRAY   = 8,
    COLOR_LBLUE   = 9,
    COLOR_LGREEN  = 10,
    COLOR_LCYAN   = 11,
    COLOR_LRED    = 12,
    COLOR_LMAG    = 13,
    COLOR_YELLOW  = 14,
    COLOR_WHITE   = 15,
} vga_color_t;
 
static inline uint8_t  vga_attr(vga_color_t fg, vga_color_t bg) { return (uint8_t)((bg << 4) | fg); }
static inline uint16_t vga_entry(char c, uint8_t attr)          { return (uint16_t)c | ((uint16_t)attr << 8); }
 
static size_t  term_col  = 0;
static size_t  term_row  = 0;
static uint8_t term_attr = 0;
 
static void term_init(void) {
    term_col  = 0;
    term_row  = 0;
    term_attr = vga_attr(COLOR_LGRAY, COLOR_BLACK);
 
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_ADDR[y * VGA_WIDTH + x] = vga_entry(' ', term_attr);
}
 
static void term_scroll(void) {
    for (size_t y = 1; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            VGA_ADDR[(y - 1) * VGA_WIDTH + x] = VGA_ADDR[y * VGA_WIDTH + x];
 
    for (size_t x = 0; x < VGA_WIDTH; x++)
        VGA_ADDR[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', term_attr);
 
    term_row = VGA_HEIGHT - 1;
}
 
static void term_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        if (++term_row == VGA_HEIGHT) term_scroll();
        return;
    }
    VGA_ADDR[term_row * VGA_WIDTH + term_col] = vga_entry(c, term_attr);
    if (++term_col == VGA_WIDTH) {
        term_col = 0;
        if (++term_row == VGA_HEIGHT) term_scroll();
    }
}
 
static void term_write(const char *s) {
    while (*s) term_putchar(*s++);
}
 
static void term_set_color(vga_color_t fg, vga_color_t bg) {
    term_attr = vga_attr(fg, bg);
}
 
/* ─── Minimal I/O port helpers ───────────────────────────────────────────── */
 
static inline void     outb(uint16_t port, uint8_t val)  { __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port)); }
static inline uint8_t  inb (uint16_t port)               { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
 
/* ─── PIC (8259) — remap IRQs above CPU exceptions ──────────────────────── */
 
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20
 
static void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);
 
    outb(PIC1_CMD,  0x11);  /* init, cascade */
    outb(PIC2_CMD,  0x11);
    outb(PIC1_DATA, offset1);
    outb(PIC2_DATA, offset2);
    outb(PIC1_DATA, 0x04);  /* master: IRQ2 → slave */
    outb(PIC2_DATA, 0x02);  /* slave: cascade id */
    outb(PIC1_DATA, 0x01);  /* 8086 mode */
    outb(PIC2_DATA, 0x01);
 
    outb(PIC1_DATA, mask1); /* restore saved masks */
    outb(PIC2_DATA, mask2);
}
 
/* ─── Multiboot info (basic) ─────────────────────────────────────────────── */
 
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;   /* kilobytes below 1 MB  */
    uint32_t mem_upper;   /* kilobytes above 1 MB  */
    /* ... rest of the struct omitted for brevity ... */
};
 
/* ─── Kernel main ────────────────────────────────────────────────────────── */
 
/**
 * kernel_main — called from boot.s after the stack is set up.
 *
 * @param magic   Multiboot magic number (should be 0x2BADB002).
 * @param mbi     Pointer to the multiboot info structure.
 */
void kernel_main(uint32_t magic, struct multiboot_info *mbi) {
 
    /* 1. Console */
    term_init();
 
    term_set_color(COLOR_LGREEN, COLOR_BLACK);
    term_write("kernel_main: booting\n");
    term_set_color(COLOR_LGRAY, COLOR_BLACK);
 
    /* 2. Verify multiboot */
    if (magic != 0x2BADB002) {
        term_set_color(COLOR_LRED, COLOR_BLACK);
        term_write("PANIC: invalid multiboot magic\n");
        goto halt;
    }
 
    /* 3. Report memory (if multiboot flags bit 0 set) */
    if (mbi && (mbi->flags & 0x1)) {
        term_write("Memory: lower=");
        /* (a real kernel would print numbers; omitted for brevity) */
        term_write("?KB  upper=?KB\n");
    }
 
    /* 4. Remap PIC so hardware IRQs don't overlap CPU exceptions */
    pic_remap(0x20, 0x28);
    term_write("PIC: remapped IRQ0-15 → INT 0x20-0x2F\n");
 
    /*
     * 5. TODO: Additional init stages, e.g.:
     *    gdt_init();
     *    idt_init();
     *    paging_init();
     *    heap_init();
     *    vfs_init();
     *    process_init();
     *    __asm__ volatile("sti");   // enable interrupts
     */
 
    term_set_color(COLOR_LCYAN, COLOR_BLACK);
    term_write("Kernel ready. Entering idle loop.\n");
    term_set_color(COLOR_LGRAY, COLOR_BLACK);
 
halt:
    /* Idle loop — a real kernel would `hlt` inside an interrupt-enabled loop */
    for (;;)
        __asm__ volatile("hlt");
}
 

