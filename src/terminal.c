/* terminal.c — VGA text-mode terminal driver */

#define VIDEO_MEM  ((volatile unsigned short *)0xB8000)
#define VGA_COLS   80
#define VGA_ROWS   25          /* rows 0-24; scroll when y reaches 25 */
#define VGA_COLOR  0x07        /* light grey on black */

/* Cursor position — next character will land here */
static int x = 0;
static int y = 0;

/* ---------- low-level helpers ---------- */

/* Write one cell directly at (col, row). */
static void write_cell(int col, int row, char ch)
{
    int offset = row * VGA_COLS + col;
    VIDEO_MEM[offset] = (unsigned short)(VGA_COLOR << 8) | (unsigned char)ch;
}

/* Scroll all rows up by one, blank the last row. */
static void scroll(void)
{
    int row, col;

    /* Copy row n+1 → row n */
    for (row = 0; row < VGA_ROWS - 1; row++)
        for (col = 0; col < VGA_COLS; col++)
            VIDEO_MEM[row * VGA_COLS + col] =
                VIDEO_MEM[(row + 1) * VGA_COLS + col];

    /* Blank the last row */
    for (col = 0; col < VGA_COLS; col++)
        write_cell(col, VGA_ROWS - 1, ' ');

    /* Keep cursor on the last row */
    y = VGA_ROWS - 1;
}

/* ---------- public API ---------- */

void putc(int data)
{
    char ch = (char)data;

    if (ch == '\n') {
        /* Newline: move to the start of the next row */
        x = 0;
        y++;
    } else if (ch == '\r') {
        x = 0;
    } else if (ch == '\b') {
        /* Backspace: erase previous character if possible */
        if (x > 0) {
            x--;
            write_cell(x, y, ' ');
        }
    } else {
        write_cell(x, y, ch);
        x++;

        /* Wrap at the right edge */
        if (x >= VGA_COLS) {
            x = 0;
            y++;
        }
    }

    /* Scroll if we've gone past the bottom */
    if (y >= VGA_ROWS)
        scroll();
}

void puts(const char *str)
{
    while (*str)
        putc(*str++);
}