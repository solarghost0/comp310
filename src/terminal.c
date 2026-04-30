#include <stdint.h>
#include <stdarg.h>

// Video memory starts at 0xB8000
#define VIDEO_MEMORY 0xB8000
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define COLOR 0x07  // Gray text on black background

// Global variables to track cursor position
int x = 0;
int y = 0;

void putc(int data) {
    unsigned short *vram = (unsigned short*)VIDEO_MEMORY;
    
    // Handle newline character
    if (data == '\n') {
        x = 0;
        y++;
    } else {
        // Calculate the position in video memory
        int position = y * SCREEN_WIDTH + x;
        
        // Write character and color to video memory
        // Lower byte is ASCII, upper byte is color
        vram[position] = (COLOR << 8) | (data & 0xFF);
        
        // Move to next position
        x++;
        
        // Check if we reached end of line
        if (x >= SCREEN_WIDTH) {
            x = 0;
            y++;
        }
    }
    
    // Check if we need to scroll
    if (y >= SCREEN_HEIGHT) {
        // Scroll the screen up
        for (int row = 0; row < SCREEN_HEIGHT - 1; row++) {
            for (int col = 0; col < SCREEN_WIDTH; col++) {
                int src_pos = (row + 1) * SCREEN_WIDTH + col;
                int dst_pos = row * SCREEN_WIDTH + col;
                vram[dst_pos] = vram[src_pos];
            }
        }
        
        // Clear the last row
        for (int col = 0; col < SCREEN_WIDTH; col++) {
            int pos = (SCREEN_HEIGHT - 1) * SCREEN_WIDTH + col;
            vram[pos] = (COLOR << 8) | ' ';
        }
        
        // Move cursor back to last row
        y = SCREEN_HEIGHT - 1;
    }
}

void puts(const char *str) {
    while (*str) {
        putc(*str);
        str++;
    }
}

static void print_uint(unsigned int value, int base, int uppercase) {
    char buf[32];
    int i = 0;
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (value == 0) {
        putc('0');
        return;
    }

    while (value > 0) {
        buf[i++] = digits[value % base];
        value /= base;
    }

    while (i > 0) {
        putc(buf[--i]);
    }
}

static void print_int(int value) {
    if (value < 0) {
        putc('-');
        print_uint((unsigned int)(-(value + 1)) + 1, 10, 0);
    } else {
        print_uint((unsigned int)value, 10, 0);
    }
}

int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int count = 0;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            // Handle optional width/precision like %.8s
            int precision = -1;
            if (*fmt == '.') {
                fmt++;
                precision = 0;
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
            switch (*fmt) {
                case 'd': {
                    int val = va_arg(args, int);
                    print_int(val);
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    print_uint(val, 10, 0);
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    print_uint(val, 16, 0);
                    break;
                }
                case 'X': {
                    unsigned int val = va_arg(args, unsigned int);
                    print_uint(val, 16, 1);
                    break;
                }
                case 's': {
                    const char *s = va_arg(args, const char *);
                    if (s == 0) s = "(null)";
                    int i = 0;
                    while (*s && (precision < 0 || i < precision)) {
                        putc(*s);
                        s++;
                        i++;
                    }
                    break;
                }
                case 'c': {
                    int c = va_arg(args, int);
                    putc(c);
                    break;
                }
                case '%':
                    putc('%');
                    break;
                default:
                    putc('%');
                    putc(*fmt);
                    break;
            }
        } else {
            putc(*fmt);
        }
        fmt++;
        count++;
    }

    va_end(args);
    return count;
}
