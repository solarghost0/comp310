
// im borrowing this main file cause i know it works from our project

#include <stdint.h>
#include "interrupt.h"

/* keyboard_map is defined in keyboard.h which interrupt.c includes.
 * declare it here as extern to avoid a duplicate-definition link error. */
extern unsigned char keyboard_map[128];

#define INFO_TYPE_KERNEL_LOAD_ADDR 0x15
#define INFO_TYPE_CMD_LINE 1
#define INFO_TYPE_BOOTLOADER_NAME 2
#define INFO_TYPE_MEM_MAP 6
#define INFO_EFI_ENTRY_ADDRESS 9
#define INFO_TYPE_MEM_INFO 4
#define INFO_TYPE_BIOS_BOOT_DEVICE 5
#define INFO_TYPE_FRAMEBUFFER_INFO 8
#define INFO_TYPE_ACPI_OLD_RSDP 14
#define INFO_TYPE_DONE 0



// the magic field should contain this.
#define MULTIBOOT2_HEADER_MAGIC         0xe85250d6

#define MULTIBOOT2_FLAGS_VIDINFO        (1<<2)
#define MULTIBOOT2_HEADER_VIDINFO_TAG   5

// addresses of the partition entries in the mbr partition table.
#define PARTITION_ENTRY_1 (struct partitionEntry*)(0x7dbe)
#define PARTITION_ENTRY_2 (struct partitionEntry*)(0x7dce)
#define PARTITION_ENTRY_3 (struct partitionEntry*)(0x7dde)
#define PARTITION_ENTRY_4 (struct partitionEntry*)(0x7dee)



struct multiboot_header {
  /* Must be MULTIBOOT_MAGIC - see above.  */
  uint32_t magic;
  /* Feature flags.  */
  uint32_t flags;
  uint32_t length;
  /* The above fields plus this one must equal 0 mod 2^32. */
  uint32_t checksum;
};
struct multiboot_framebuffer_tag {
  uint16_t type;
  uint16_t flags;
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint32_t depth;
  uint32_t x;
};
struct multiboot_tag {
  uint16_t type;
  uint16_t flags;
  uint32_t size;
};

struct multiboot_header mbh  __attribute__((section(".multiboot1")))= {
    .magic = MULTIBOOT2_HEADER_MAGIC,
    .flags = 0, // tell grub we are adding info about video info
    .length = 16,
    .checksum = -(16+MULTIBOOT2_HEADER_MAGIC)
};
struct multiboot_framebuffer_tag  gfxtag __attribute__((section(".multiboot2")))= {
    .type = MULTIBOOT2_HEADER_VIDINFO_TAG,
    .flags = 1,
    .size = sizeof(struct multiboot_framebuffer_tag),
    .width = 1024,
    .height = 768,
    .depth = 32
};
struct multiboot_tag terminator_tag  __attribute__((section(".multiboot3")))= {
    .type = 0,
    .flags = 0,
    .size = sizeof(struct multiboot_tag)
};

uint32_t *framebuffer;
static uint32_t framebufferPitch;
static uint32_t framebufferWidth;
static uint32_t framebufferHeight;
static uint32_t framebufferBitsPerPixel;

/* off-screen back buffer. all drawing goes here; flip_buffer() then
 * copies it to the real framebuffer in one shot, eliminating the
 * visible black-flash between clear and redraw. */
static uint32_t back_buffer[1024 * 768];

uint32_t getFramebufferBitsPerPixel() {
    return framebufferBitsPerPixel;
}


void setFramebufferBitsPerPixel(uint8_t bpp) {
    framebufferBitsPerPixel = bpp;
}

void setFramebufferPitch(uint32_t pitch) {
    framebufferPitch = pitch;
}

uint32_t getFramebufferWidth() {
    return framebufferWidth;
}


void setFramebufferWidth(uint32_t width) {
    framebufferWidth = width;
}

uint32_t  getFramebufferHeight() {
    return framebufferHeight;
}
void setFramebufferHeight(uint32_t height) {
    framebufferHeight = height;
}

void setFramebufferAddress(void *base) {
    framebuffer = base;
}


/*
 * parseMultiboot2Info
 *
 * parses the multiboot 2 information structure to read relevant hardware
 * information.
 */
uint32_t *pMultibootInfo;
int parseMultiboot2Info() {
    uint32_t *p = pMultibootInfo + 2; // skip past initial tag
    unsigned int k = 0;
    unsigned int totalStructSize = *pMultibootInfo;

    while(p < (uint32_t*)((uint8_t*)pMultibootInfo + totalStructSize)){
        unsigned int type = *p;
        unsigned int size = *(p+1);
        switch(type){
        case INFO_TYPE_BOOTLOADER_NAME:
            break;
        case INFO_TYPE_CMD_LINE:
            break;
        case INFO_TYPE_KERNEL_LOAD_ADDR:
            break;
        case INFO_TYPE_MEM_MAP:
            for(k = 0; k < (size-16)/24; k++){
                unsigned int region_base = *(p+4+(k*6));
                unsigned int region_length = *(p+4+(k*6)+2);
                unsigned int region_type = *(p+4+(k*6)+4);
            }
            break;
        case INFO_TYPE_BIOS_BOOT_DEVICE:
            break;
        case INFO_TYPE_ACPI_OLD_RSDP: // unsupported tags...
        case INFO_TYPE_MEM_INFO:
        case INFO_EFI_ENTRY_ADDRESS:
            break;
        case INFO_TYPE_FRAMEBUFFER_INFO:
            setFramebufferAddress((void*)*(p+2));
            setFramebufferPitch(*(p+4));
            setFramebufferWidth(*(p+5));
            setFramebufferHeight(*(p+6));
            setFramebufferBitsPerPixel(*(p+7) & 0xff);
            break;
        case INFO_TYPE_DONE: // we are done reading the multiboot info struct when we get a type of 0
            return 0;
        default:
            break;
        }
        while(((uint32_t)size & ~7) != (uint32_t)size){ // round size up to the next 8-byte boundary
            size++;
        }
        p += (size/sizeof(unsigned int)); // skip p past the tag
    }
    return 0;
}


// drawPixel
// 
// draws one pixel on the screen at the given (x,y) location. the color is a
// 3-byte rgb color value. each byte controls the intensity of the red, green,
// and blue for the given pixel. for example, rgb for light orange is 0xE09667
//
//          ----- red intensity
//          |
//          |  ---- green intensity
//          |  |
//          |  |  --- blue intensity
//          |  |  |
//          /\ /\ /\
// color: 0xE0 96 67
//
// for example, to color a pixel light orange at location (5,5):
//
//   drawPixel(5,5,0xE09667);
//
void drawPixel(int x, int y, int color) {
    if (x < 0 || x >= (int)framebufferWidth || y < 0 || y >= (int)framebufferHeight)
        return;
    back_buffer[x + (y * (int)framebufferWidth)] = (uint32_t)color;
}

/* blit the completed back buffer to the real framebuffer in one burst.
 * using rep movsl (32-bit copy) so the visible screen is updated
 * atomically rather than pixel-by-pixel, preventing tearing. */
static void flip_buffer(void) {
    uint32_t count = framebufferWidth * framebufferHeight;
    uint32_t *src  = back_buffer;
    uint32_t *dst  = framebuffer;
    asm volatile (
        "rep movsl\n\t"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

/* utility helpers */

static int game_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* writes the decimal representation of n (non-negative) into buf.
 * buf must be at least 12 bytes. */
static void itoa(int n, char *buf) {
    char tmp[12];
    int i = 0;
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (n > 0) { tmp[i++] = (char)('0' + n % 10); n /= 10; }
    int j;
    for (j = 0; j < i; j++) buf[j] = tmp[i - 1 - j];
    buf[j] = '\0';
}

/* 8x8 bitmap font
 * each glyph is 8 rows; each row is 8 pixels (bit 7 is leftmost).
 * glyphs are indexed as: 0 is space, 1-10 are '0'-'9', 11-36 are 'a'-'z',
 * 37 is ':'
 */

static const uint8_t font_glyphs[39][8] = {
    /* 0: space */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 1: '0' */
    {0x38,0x44,0x4C,0x54,0x64,0x44,0x38,0x00},
    /* 2: '1' */
    {0x10,0x30,0x10,0x10,0x10,0x10,0x38,0x00},
    /* 3: '2' */
    {0x38,0x44,0x04,0x18,0x20,0x40,0x7C,0x00},
    /* 4: '3' */
    {0x38,0x44,0x04,0x18,0x04,0x44,0x38,0x00},
    /* 5: '4' */
    {0x08,0x18,0x28,0x48,0x7C,0x08,0x08,0x00},
    /* 6: '5' */
    {0x7C,0x40,0x78,0x04,0x04,0x44,0x38,0x00},
    /* 7: '6' */
    {0x1C,0x20,0x40,0x78,0x44,0x44,0x38,0x00},
    /* 8: '7' */
    {0x7C,0x04,0x08,0x10,0x20,0x20,0x20,0x00},
    /* 9: '8' */
    {0x38,0x44,0x44,0x38,0x44,0x44,0x38,0x00},
    /* 10: '9' */
    {0x38,0x44,0x44,0x3C,0x04,0x08,0x30,0x00},
    /* 11: 'a' */
    {0x00,0x00,0x38,0x04,0x3C,0x44,0x3C,0x00},
    /* 12: 'b' */
    {0x40,0x40,0x78,0x44,0x44,0x44,0x78,0x00},
    /* 13: 'c' */
    {0x00,0x00,0x3C,0x40,0x40,0x40,0x3C,0x00},
    /* 14: 'd' */
    {0x04,0x04,0x3C,0x44,0x44,0x44,0x3C,0x00},
    /* 15: 'e' */
    {0x00,0x00,0x38,0x44,0x7C,0x40,0x3C,0x00},
    /* 16: 'f' */
    {0x1C,0x20,0x20,0x78,0x20,0x20,0x20,0x00},
    /* 17: 'g' */
    {0x00,0x00,0x3C,0x44,0x44,0x3C,0x04,0x38},
    /* 18: 'h' */
    {0x40,0x40,0x78,0x44,0x44,0x44,0x44,0x00},
    /* 19: 'i' */
    {0x10,0x00,0x30,0x10,0x10,0x10,0x38,0x00},
    /* 20: 'j' */
    {0x08,0x00,0x18,0x08,0x08,0x48,0x30,0x00},
    /* 21: 'k' */
    {0x40,0x44,0x48,0x70,0x48,0x44,0x44,0x00},
    /* 22: 'l' */
    {0x30,0x10,0x10,0x10,0x10,0x10,0x38,0x00},
    /* 23: 'm' */
    {0x00,0x00,0x68,0x54,0x54,0x44,0x44,0x00},
    /* 24: 'n' */
    {0x00,0x00,0x78,0x44,0x44,0x44,0x44,0x00},
    /* 25: 'o' */
    {0x00,0x00,0x38,0x44,0x44,0x44,0x38,0x00},
    /* 26: 'p' */
    {0x00,0x00,0x78,0x44,0x44,0x78,0x40,0x40},
    /* 27: 'q' */
    {0x00,0x00,0x3C,0x44,0x44,0x3C,0x04,0x04},
    /* 28: 'r' */
    {0x00,0x00,0x5C,0x60,0x40,0x40,0x40,0x00},
    /* 29: 's' */
    {0x00,0x00,0x3C,0x40,0x38,0x04,0x3C,0x00},
    /* 30: 't' */
    {0x20,0x20,0x7C,0x20,0x20,0x24,0x18,0x00},
    /* 31: 'u' */
    {0x00,0x00,0x44,0x44,0x44,0x4C,0x34,0x00},
    /* 32: 'v' */
    {0x00,0x00,0x44,0x44,0x44,0x28,0x10,0x00},
    /* 33: 'w' */
    {0x00,0x00,0x44,0x44,0x54,0x54,0x28,0x00},
    /* 34: 'x' */
    {0x00,0x00,0x44,0x28,0x10,0x28,0x44,0x00},
    /* 35: 'y' */
    {0x00,0x00,0x44,0x44,0x3C,0x04,0x38,0x00},
    /* 36: 'z' */
    {0x00,0x00,0x7C,0x08,0x10,0x20,0x7C,0x00},
    /* 37: ':' */
    {0x00,0x10,0x10,0x00,0x10,0x10,0x00,0x00},
    /* 38: '+' */
    {0x00,0x10,0x10,0x7C,0x10,0x10,0x00,0x00},
};

static const uint8_t *getGlyph(char c) {
    if (c == ' ')             return font_glyphs[0];
    if (c >= '0' && c <= '9') return font_glyphs[1 + (c - '0')];
    if (c >= 'a' && c <= 'z') return font_glyphs[11 + (c - 'a')];
    if (c == ':')             return font_glyphs[37];
    if (c == '+')             return font_glyphs[38];
    return font_glyphs[0]; /* unknown - blank */
}

/* drawing primitives */

/* arek - moved these 2 out of game helpers for drawLine */
static int abs_int(int v) { return v < 0 ? -v : v; }
static int max_int(int a, int b) { return a > b ? a : b; }

/* draw a character scaled 2x (each pixel becomes a 2x2 block)
*  arek - while skipping over background pixels. */
static void drawChar(int x, int y, char c, uint32_t fg) {
    const uint8_t *glyph = getGlyph(c);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            /* skip 0s */
            if (glyph[row] & (uint8_t)(0x80u >> col)) {
                drawPixel(x + col*2,     y + row*2,     (int)fg);
                drawPixel(x + col*2 + 1, y + row*2,     (int)fg);
                drawPixel(x + col*2,     y + row*2 + 1, (int)fg);
                drawPixel(x + col*2 + 1, y + row*2 + 1, (int)fg);
            }
        }
    }
}

/* each 2x-scaled character occupies 17 pixels wide (16 plus 1 gap). */
#define CHAR_W 17
#define CHAR_H 16

static void drawString(int x, int y, const char *s, uint32_t fg) {
    while (*s) {
        drawChar(x, y, *s, fg);
        x += CHAR_W;
        s++;
    }
}

/* filled circle using bresenham's midpoint algorithm (horizontal span fill). */
static void fillCircle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        int i;
        for (i = cx - x; i <= cx + x; i++) { drawPixel(i, cy + y, (int)color); drawPixel(i, cy - y, (int)color); }
        for (i = cx - y; i <= cx + y; i++) { drawPixel(i, cy + x, (int)color); drawPixel(i, cy - x, (int)color); }
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

/* circle outline using bresenham's midpoint algorithm. */
static void drawCircle(int cx, int cy, int r, uint32_t color) {
    int x = r, y = 0, err = 0;
    while (x >= y) {
        drawPixel(cx + x, cy + y, (int)color); drawPixel(cx + y, cy + x, (int)color);
        drawPixel(cx - y, cy + x, (int)color); drawPixel(cx - x, cy + y, (int)color);
        drawPixel(cx - x, cy - y, (int)color); drawPixel(cx - y, cy - x, (int)color);
        drawPixel(cx + y, cy - x, (int)color); drawPixel(cx + x, cy - y, (int)color);
        y++;
        err += 1 + 2 * y;
        if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
    }
}

/* draw a line using Bresenham's line algorithm */
static void drawLine(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = abs_int(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs_int(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        drawPixel(x0, y0, (int)color);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* filled triangle using a scan-line algorithm.
 * vertices are sorted by y before rasterising. */
static void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t color) {
    /* sort so that y0 <= y1 <= y2 */
    int t;
    if (y0 > y1) { t=y0;y0=y1;y1=t; t=x0;x0=x1;x1=t; }
    if (y0 > y2) { t=y0;y0=y2;y2=t; t=x0;x0=x2;x2=t; }
    if (y1 > y2) { t=y1;y1=y2;y2=t; t=x1;x1=x2;x2=t; }

    int dy02 = y2 - y0;
    int dy01 = y1 - y0;
    int dy12 = y2 - y1;
    int y;
    for (y = y0; y <= y2; y++) {
        /* x on the long edge (y0->y2) */
        int xa = (dy02 > 0) ? x0 + (y - y0) * (x2 - x0) / dy02 : x0;
        /* x on the short edges (y0->y1 then y1->y2) */
        int xb;
        if (y <= y1)
            xb = (dy01 > 0) ? x0 + (y - y0) * (x1 - x0) / dy01 : x0;
        else
            xb = (dy12 > 0) ? x1 + (y - y1) * (x2 - x1) / dy12 : x1;
        if (xa > xb) { t = xa; xa = xb; xb = t; }
        int x;
        for (x = xa; x <= xb; x++)
            drawPixel(x, y, (int)color);
    }
}

/* game constants and data */

#define SCREEN_W     1024
#define SCREEN_H     768
#define CENTER_X     (SCREEN_W / 2)
#define CENTER_Y     (SCREEN_H / 2)

#define PLAYER_RADIUS   22
#define ENEMY_RADIUS    18

/* fixed-point scale factor for positions and velocities (1.0 is 256). */
#define FP_SCALE        256
/* enemy speed in fixed-point units (128/256 is 0.5 px per game-tick).
 * increase TICK_DIVIDER to slow the game; increase ENEMY_SPEED to
 * make enemies faster. */
#define ENEMY_SPEED     128

/* how many main-loop iterations between game ticks.
 * no longer used - frame rate is now governed by the pit timer.
 * kept for reference only. */
/* #define TICK_DIVIDER    1 */

#define MAX_ENEMIES     8
#define WORD_MAX_LEN    12
#define NUM_WORDS       40

/* colors (0xRRGGBB) */
#define COL_BLACK    0x000000u
#define COL_WHITE    0xFFFFFFu
#define COL_PLAYER   0x2266DDu   /* blue */
#define COL_ENEMY    0xDD3333u   /* red */
#define COL_FOCUSED  0xDD7700u   /* orange - currently-targeted enemy */
#define COL_TYPED    0x009944u   /* green - already-typed part of word */
#define COL_UNTYPED  0x111111u   /* near-black - remaining part of word */
#define COL_HUD      0x333333u   /* dark gray - hud text */
#define COL_BG       0xF8F8F8u   /* off-white background */
#define COL_GAMEOVER 0xCC0000u   /* dark red - game-over message */
#define COL_PROJ        0xFF1111u   /* bright red - player projectiles */
#define COL_ENEMY_TRI   0xFF7700u   /* orange - triangle enemy */
#define COL_ENEMY_SQR   0x9933CCu   /* purple - square enemy */
#define COL_BAR_BG   0x661111u
#define COL_BAR_FG   0x00CC44u

/* start-screen colours */
#define COL_DARKBG    0x080818u   /* near-black blue – start screen background */
#define COL_PANEL     0x0E0E2Au   /* dark panel fill */
#define COL_PANELBRD  0x1A2255u   /* panel border */
#define COL_LOGO      0x00EECCu   /* bright teal – main logo colour */
#define COL_LOGO_SHD  0x005544u   /* dark teal – logo drop-shadow */
#define COL_SUBTITLE  0x8899FFu   /* soft blue – subtitle text */
#define COL_SEL_BG    0xFFCC00u   /* gold – selected difficulty cell bg */
#define COL_SEL_FG    0x000000u   /* black – selected difficulty cell text */
#define COL_UNSEL_BG  0x1A1A3Cu   /* dark – unselected difficulty cell bg */
#define COL_UNSEL_FG  0x4455AAu   /* dim blue – unselected text */
#define COL_DIFF_LBL  0xFFFFCCu   /* cream – difficulty description */
#define COL_HINT_TXT  0x334477u   /* muted blue – keyboard hint */
#define COL_DECO_A    0x1A3088u   /* decoration ring A */
#define COL_DECO_B    0x0D1844u   /* decoration ring B */
#define COL_HLINE     0x223399u   /* horizontal separator */

/* ---- game state machine ---- */
#define STATE_START    0
#define STATE_PLAYING  1
#define STATE_GAMEOVER 2

static int game_state = STATE_START;

/* ---- difficulty configuration ---- */
/*  index 0 = difficulty 1  …  index 4 = difficulty 5  */
static const int32_t diff_speed[5]  = { 64, 96, 128, 192, 128 };
static const int     diff_health[5] = { 100, 100, 100, 100, 200 };
static const char * const diff_labels[5] = {
    "speed: +",
    "speed: ++",
    "speed: +++",
    "speed: ++++",
    "speed: +++ and enemy health doubled"
};

static int selected_difficulty = 3; /* 1-5, default is difficulty 3 (current baseline speed) */
#define DEFAULT_DIFFICULTY 3

/* runtime values written by initGame() from the chosen difficulty */
static int32_t current_enemy_speed      = 128;
static int     current_enemy_max_health = 100;

/* enemy type identifiers */
#define ENEMY_TYPE_NORMAL   0   /* circle - standard enemy */
#define ENEMY_TYPE_TRIANGLE 1   /* triangle - 2x speed, normal health */
#define ENEMY_TYPE_SQUARE   2   /* square   - normal speed, 2x health */

#define PROJECTILE_DAMAGE  25
#define VOLLEY_SIZE        4
#define VOLLEY_INTERVAL    3
#define MAX_PROJECTILES    32
#define MAX_VOLLEYS        8
#define PROJECTILE_SPEED   4096

static const char * const word_list[NUM_WORDS] = {
    "cat",      "dog",      "sun",      "fly",      "sky",
    "fox",      "owl",      "fire",     "wave",     "snow",
    "rock",     "jump",     "rain",     "bear",     "wolf",
    "hawk",     "moon",     "storm",    "night",    "river",
    "ghost",    "flame",    "stone",    "spark",    "frost",
    "blade",    "castle",   "hunter",   "forest",   "shadow",
    "wizard",   "bridge",   "phantom",  "thunder",  "crystal",
    "warrior",  "journey",  "emperor",  "starfish",  "guardian"
};

struct Enemy {
    int      active;           /* 0 = inactive, 1 = active, 2 = dying */
    int32_t  fx, fy;           /* position scaled by FP_SCALE */
    int32_t  vx, vy;           /* velocity scaled by FP_SCALE (per tick) */
    int      x,  y;            /* integer render position */
    char     word[WORD_MAX_LEN + 1];
    int      typed;            /* chars correctly typed so far */
    int      health;
    int      max_health;       /* per-enemy max health (varies by type) */
    int      type;             /* ENEMY_TYPE_* */
    int      death_frame;      /* for explosion animation when dying */
};

struct Projectile {
    int      active;
    int32_t  fx, fy;
    int32_t  vx, vy;
    int      target;
};

struct Volley {
    int active;
    int target;
    int remaining;
    int timer;
};

/* ---- floating background shapes ---- */
#define MAX_BG_SHAPES 12

struct BgShape {
    int32_t fx, fy;   /* fixed-point position (scaled by FP_SCALE) */
    int32_t vx, vy;   /* fixed-point velocity per tick */
    int     size;     /* radius (circle) or half-side (rect) in pixels */
    int     shape;    /* 0 = filled circle, 1 = filled rect */
    uint32_t color;   /* very subtle grey, close to COL_BG */
};

static struct BgShape bg_shapes[MAX_BG_SHAPES];
static int bg_shapes_inited = 0;

static struct Enemy      enemies[MAX_ENEMIES];
static struct Projectile projectiles[MAX_PROJECTILES];
static struct Volley     volleys[MAX_VOLLEYS];
static int           player_health = 5;
static int           score         = 0;
static int           focused_enemy = -1; /* index of enemy being typed, or -1 */
static uint32_t      rand_state    = 12345u; /* arbitrary initial seed for LCG */
static uint32_t      spawn_timer   = 0;
static uint32_t      spawn_interval = 120; /* ticks between enemy spawns */

/* game helpers */

static uint32_t rand_next(void) {
    rand_state = rand_state * 1664525u + 1013904223u;
    return rand_state;
}

static void clearScreen(void) {
    uint32_t total = framebufferWidth * framebufferHeight;
    for (uint32_t i = 0; i < total; i++)
        back_buffer[i] = COL_BG;
}

/* fill the entire back buffer with a given colour (used for the start screen). */
static void fillScreen(uint32_t color) {
    uint32_t total = framebufferWidth * framebufferHeight;
    for (uint32_t i = 0; i < total; i++)
        back_buffer[i] = color;
}

/* draw a filled rectangle. */
static void drawRect(int rx, int ry, int rw, int rh, uint32_t color) {
    int dx, dy;
    for (dy = 0; dy < rh; dy++)
        for (dx = 0; dx < rw; dx++)
            drawPixel(rx + dx, ry + dy, (int)color);
}

/* draw a character at 4× scale for the large logo title.
 * each font pixel becomes a 4×4 block; gap between chars is 2px. */
#define LOGO_SCALE  4
#define LOGO_CHAR_W (8 * LOGO_SCALE + 2)   /* 34 px per char including gap */
#define LOGO_CHAR_H (8 * LOGO_SCALE)        /* 32 px tall */

static void drawCharLarge(int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = getGlyph(c);
    int row, col, dy, dx;
    for (row = 0; row < 8; row++) {
        for (col = 0; col < 8; col++) {
            uint32_t color = (glyph[row] & (uint8_t)(0x80u >> col)) ? fg : bg;
            for (dy = 0; dy < LOGO_SCALE; dy++)
                for (dx = 0; dx < LOGO_SCALE; dx++)
                    drawPixel(x + col * LOGO_SCALE + dx,
                              y + row * LOGO_SCALE + dy, (int)color);
        }
    }
}

static void drawStringLarge(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        drawCharLarge(x, y, *s, fg, bg);
        x += LOGO_CHAR_W;
        s++;
    }
}

/* ---- start screen ---- */

/* draw the start screen to the back buffer and flip it. */
static void drawStartScreen(void) {
    int i;

    /* dark starfield background */
    fillScreen(COL_DARKBG);

    /* scattered star dots (deterministic positions derived from fixed seeds) */
    {
        uint32_t st = 0xDEADBEEFu;
        for (i = 0; i < 120; i++) {
            st = st * 1664525u + 1013904223u;
            int sx = (int)(st % (uint32_t)SCREEN_W);
            st = st * 1664525u + 1013904223u;
            int sy = (int)(st % (uint32_t)SCREEN_H);
            uint32_t brightness = 0x222222u + ((st & 0x3Fu) * 0x020202u);
            drawPixel(sx, sy, (int)brightness);
        }
    }

    /* central panel */
    int panel_w = 700, panel_h = 500;
    int panel_x = (SCREEN_W - panel_w) / 2;
    int panel_y = (SCREEN_H - panel_h) / 2 - 20;

    /* panel shadow (offset by 6 px) */
    drawRect(panel_x + 6, panel_y + 6, panel_w, panel_h, 0x040410u);
    /* panel fill */
    drawRect(panel_x, panel_y, panel_w, panel_h, COL_PANEL);
    /* panel border – four edges, 2 px wide */
    drawRect(panel_x,               panel_y,               panel_w, 2,       COL_PANELBRD);
    drawRect(panel_x,               panel_y + panel_h - 2, panel_w, 2,       COL_PANELBRD);
    drawRect(panel_x,               panel_y,               2,       panel_h, COL_PANELBRD);
    drawRect(panel_x + panel_w - 2, panel_y,               2,       panel_h, COL_PANELBRD);

    /* decorative corner accents (small filled squares) */
    drawRect(panel_x + 6,               panel_y + 6,               6, 6, COL_DECO_A);
    drawRect(panel_x + panel_w - 12,    panel_y + 6,               6, 6, COL_DECO_A);
    drawRect(panel_x + 6,               panel_y + panel_h - 12,    6, 6, COL_DECO_A);
    drawRect(panel_x + panel_w - 12,    panel_y + panel_h - 12,    6, 6, COL_DECO_A);

    /* decorative circles behind logo */
    drawCircle(SCREEN_W / 2,       panel_y + 80, 55, COL_DECO_B);
    drawCircle(SCREEN_W / 2,       panel_y + 80, 52, COL_DECO_A);
    drawCircle(SCREEN_W / 2 - 120, panel_y + 80, 28, COL_DECO_B);
    drawCircle(SCREEN_W / 2 + 120, panel_y + 80, 28, COL_DECO_B);

    /* ----- logo "glyphica" at 4× scale, centered ---- */
    /* "glyphica" = 8 chars; total width = 8 * LOGO_CHAR_W - 2 (no trailing gap) */
    const char *title = "glyphica";
    int title_len = 8;
    int title_w = title_len * LOGO_CHAR_W - 2;
    int title_x = (SCREEN_W - title_w) / 2;
    int title_y = panel_y + 48;

    /* drop-shadow (offset +3,+3) */
    drawStringLarge(title_x + 3, title_y + 3, title, COL_LOGO_SHD, COL_PANEL);
    /* main logo colour */
    drawStringLarge(title_x, title_y, title, COL_LOGO, COL_PANEL);

    /* thin horizontal separator under title */
    int sep_y = title_y + LOGO_CHAR_H + 12;
    drawRect(panel_x + 40, sep_y, panel_w - 80, 1, COL_HLINE);
    drawRect(panel_x + 40, sep_y + 2, panel_w - 80, 1, COL_DECO_B);

    /* subtitle "type to survive" (2× scale, centered) */
    const char *sub = "type to survive";
    int sub_len = 15;
    int sub_w = sub_len * CHAR_W - 1;
    int sub_x = (SCREEN_W - sub_w) / 2;
    int sub_y = sep_y + 10;
    drawString(sub_x, sub_y, sub, COL_SUBTITLE);

    /* ----- difficulty slider ----- */
    int slider_y   = sub_y + CHAR_H + 36;

    /* label "difficulty:" */
    const char *dlbl = "difficulty:";
    int dlbl_w = 11 * CHAR_W - 1;
    int dlbl_x = (SCREEN_W - dlbl_w) / 2;
    drawString(dlbl_x, slider_y, dlbl, COL_SUBTITLE);

    /* five cells, each 60 px wide × 36 px tall, 10 px gap */
    int cell_w = 60, cell_h = 36, cell_gap = 10;
    int cells_total = 5 * cell_w + 4 * cell_gap;
    int cell_start_x = (SCREEN_W - cells_total) / 2;
    int cell_y = slider_y + CHAR_H + 12;

    for (i = 0; i < 5; i++) {
        int cx = cell_start_x + i * (cell_w + cell_gap);
        int is_sel = (i + 1 == selected_difficulty);
        uint32_t cbg = is_sel ? COL_SEL_BG   : COL_UNSEL_BG;
        uint32_t cfg = is_sel ? COL_SEL_FG   : COL_UNSEL_FG;

        /* cell fill */
        drawRect(cx, cell_y, cell_w, cell_h, cbg);
        /* cell border */
        drawRect(cx, cell_y,              cell_w, 1, is_sel ? 0xFFEE44u : COL_DECO_B);
        drawRect(cx, cell_y + cell_h - 1, cell_w, 1, is_sel ? 0xFFEE44u : COL_DECO_B);
        drawRect(cx,              cell_y, 1, cell_h, is_sel ? 0xFFEE44u : COL_DECO_B);
        drawRect(cx + cell_w - 1, cell_y, 1, cell_h, is_sel ? 0xFFEE44u : COL_DECO_B);

        /* digit centred in cell */
        int digit_x = cx + (cell_w - CHAR_W) / 2;
        int digit_y = cell_y + (cell_h - CHAR_H) / 2;
        char digit_str[2] = { (char)('1' + i), '\0' };
        drawString(digit_x, digit_y, digit_str, cfg);

        /* selection triangle above selected cell */
        if (is_sel) {
            int tri_cx = cx + cell_w / 2;
            int tri_y  = cell_y - 10;
            int t;
            for (t = 0; t < 8; t++)
                drawRect(tri_cx - t, tri_y + t, 1 + t * 2, 1, COL_SEL_BG);
        }
    }

    /* ----- difficulty description below slider ----- */
    const char *desc = diff_labels[selected_difficulty - 1];
    int desc_len = game_strlen(desc);
    int desc_w   = desc_len * CHAR_W - 1;
    int desc_x   = (SCREEN_W - desc_w) / 2;
    int desc_y   = cell_y + cell_h + 18;
    /* clear the description row first (in case previous text was longer) */
    drawRect(panel_x + 4, desc_y - 2, panel_w - 8, CHAR_H + 4, COL_PANEL);
    drawString(desc_x, desc_y, desc, COL_DIFF_LBL);

    /* ----- keyboard hints ----- */
    int hint_y = desc_y + CHAR_H + 28;
    const char *h1 = "left right  select difficulty";
    int h1_len = game_strlen(h1);
    int h1_x   = (SCREEN_W - h1_len * CHAR_W + 1) / 2;
    drawString(h1_x, hint_y, h1, COL_HINT_TXT);

    const char *h2 = "enter  start game";
    int h2_len = game_strlen(h2);
    int h2_x   = (SCREEN_W - h2_len * CHAR_W + 1) / 2;
    drawString(h2_x, hint_y + CHAR_H + 6, h2, COL_HINT_TXT);

    flip_buffer();
}

/* initialise (or reset) all gameplay state and apply the chosen difficulty. */
static void spawnEnemy(void); /* forward declaration */
static void initGame(void) {
    int i;
    current_enemy_speed      = diff_speed[selected_difficulty - 1];
    current_enemy_max_health = diff_health[selected_difficulty - 1];

    player_health = 5;
    score         = 0;
    focused_enemy = -1;
    spawn_timer   = 0;
    spawn_interval = 120;

    for (i = 0; i < MAX_ENEMIES;     i++) enemies[i].active     = 0;
    for (i = 0; i < MAX_PROJECTILES; i++) projectiles[i].active = 0;
    for (i = 0; i < MAX_VOLLEYS;     i++) volleys[i].active     = 0;

    spawnEnemy(); /* spawn first enemy immediately */
}

static void assignWord(struct Enemy *e) {
    const char *w = word_list[rand_next() % NUM_WORDS];
    int k;
    for (k = 0; w[k] && k < WORD_MAX_LEN; k++)
        e->word[k] = w[k];
    e->word[k] = '\0';
    e->typed = 0;
}

/* spawn a new enemy at a random point on one of the four screen edges,
 * with velocity directed toward the screen center. 
 * arek - updated math for enemy spawns to have them smoothly move
 * onto the screen */
static void spawnEnemy(void) {
    int slot = -1;
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) { slot = i; break; }
    }
    if (slot == -1) return; /* all slots occupied */
    int sx, sy;
    int edge = (int)(rand_next() % 4);
    int padding = 40;
    switch (edge) {
        case 0: /* top */
            sx = (int)(rand_next() % (SCREEN_W - 2 * ENEMY_RADIUS)) + ENEMY_RADIUS;
            sy = -padding; 
            break;
        case 1: /* right */
            sx = SCREEN_W + padding;
            sy = (int)(rand_next() % (SCREEN_H - 2 * padding)) + ENEMY_RADIUS;
            break;
        case 2: /* bottom */
            sx = (int)(rand_next() % (SCREEN_W - 2 * ENEMY_RADIUS)) + ENEMY_RADIUS;
            sy = SCREEN_H + padding;
            break;
        default: /* left */
            sx = -padding;
            sy = (int)(rand_next() % (SCREEN_H - 2 * padding)) + ENEMY_RADIUS;
            break;
    }

    /* compute fixed-point velocity toward center using chebyshev normalization
     * so the dominant axis moves at exactly ENEMY_SPEED pixels/tick. */
    int dx    = CENTER_X - sx;
    int dy    = CENTER_Y - sy;
    int dmax  = max_int(abs_int(dx), abs_int(dy));

    /* 10% chance to spawn a special enemy; split 50/50 between triangle and square */
    int enemy_type = ENEMY_TYPE_NORMAL;
    if (rand_next() % 10 == 0)
        enemy_type = (rand_next() % 2 == 0) ? ENEMY_TYPE_TRIANGLE : ENEMY_TYPE_SQUARE;

    /* triangle moves at 2× speed; all other types use the baseline speed */
    int32_t speed = (enemy_type == ENEMY_TYPE_TRIANGLE)
                    ? current_enemy_speed * 2
                    : current_enemy_speed;

    int32_t vx = (dmax > 0) ? (int32_t)(dx * speed / dmax) : 0;
    int32_t vy = (dmax > 0) ? (int32_t)(dy * speed / dmax) : speed;

    /* square has twice the normal max health; triangle and normal share the baseline */
    int max_hp = (enemy_type == ENEMY_TYPE_SQUARE)
                 ? current_enemy_max_health * 2
                 : current_enemy_max_health;

    struct Enemy *e = &enemies[slot];
    e->active     = 1;
    e->fx         = (int32_t)sx * FP_SCALE;
    e->fy         = (int32_t)sy * FP_SCALE;
    e->vx         = vx;
    e->vy         = vy;
    e->x          = sx;
    e->y          = sy;
    e->health     = max_hp;
    e->max_health = max_hp;
    e->type       = enemy_type;
    assignWord(e);
}

/* move enemies one tick toward the center; check player collision. 
 * arek - and also update dying animation by 1 tick */
static void updateEnemies(void) {
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        struct Enemy *e = &enemies[i];
/* state 1: active and moving */
        if (e->active == 1) {
            e->fx += e->vx;
            e->fy += e->vy;
            e->x   = (int)(e->fx / FP_SCALE);
            e->y   = (int)(e->fy / FP_SCALE);

            /* check for collision with player turret */
            int dx = e->x - CENTER_X;
            int dy = e->y - CENTER_Y;
            int dist_sq  = dx * dx + dy * dy;
            int min_dist = PLAYER_RADIUS + ENEMY_RADIUS;

            if (dist_sq <= min_dist * min_dist) {
                e->active = 0;
                player_health--;
            }
        } 
/* state 2: dying animation (triggered by updateProjectiles) */
        else if (e->active == 2) {
            e->death_frame++;

            if (e->death_frame > 10) {
                e->active = 0; 
            }
        }
    }
}

/* process one typed character from the player. */
static void triggerVolley(int enemy_idx) {
    int i;
    for (i = 0; i < MAX_VOLLEYS; i++) {
        if (!volleys[i].active) {
            volleys[i].active    = 1;
            volleys[i].target    = enemy_idx;
            volleys[i].remaining = VOLLEY_SIZE;
            volleys[i].timer     = 0;
            return;
        }
    }
}

static void updateVolleys(void) {
    int i;
    for (i = 0; i < MAX_VOLLEYS; i++) {
        struct Volley *v = &volleys[i];
        if (!v->active) continue;
        if (!enemies[v->target].active) { v->active = 0; continue; }
        if (v->timer > 0) { v->timer--; continue; }

        struct Enemy *e = &enemies[v->target];
        int dx = e->x - CENTER_X;
        int dy = e->y - CENTER_Y;
        int dmax = max_int(abs_int(dx), abs_int(dy));
        int32_t vx = (dmax > 0) ? (int32_t)(dx * PROJECTILE_SPEED / dmax) : 0;
        int32_t vy = (dmax > 0) ? (int32_t)(dy * PROJECTILE_SPEED / dmax) : 0;

        int j;
        for (j = 0; j < MAX_PROJECTILES; j++) {
            if (!projectiles[j].active) {
                projectiles[j].active = 1;
                projectiles[j].fx     = (int32_t)CENTER_X * FP_SCALE;
                projectiles[j].fy     = (int32_t)CENTER_Y * FP_SCALE;
                projectiles[j].vx     = vx;
                projectiles[j].vy     = vy;
                projectiles[j].target = v->target;
                break;
            }
        }

        v->remaining--;
        if (v->remaining <= 0)
            v->active = 0;
        else
            v->timer = VOLLEY_INTERVAL;
    }
}

static void updateProjectiles(void) {
    int i;
    for (i = 0; i < MAX_PROJECTILES; i++) {
        struct Projectile *p = &projectiles[i];
        if (!p->active) continue;

        p->fx += p->vx;
        p->fy += p->vy;
        int px = (int)(p->fx / FP_SCALE);
        int py = (int)(p->fy / FP_SCALE);

        if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H) {
            p->active = 0;
            continue;
        }

        if (enemies[p->target].active) {
            struct Enemy *e = &enemies[p->target];
            int dx = px - e->x;
            int dy = py - e->y;
            if (dx * dx + dy * dy <= ENEMY_RADIUS * ENEMY_RADIUS) {
                e->health -= PROJECTILE_DAMAGE;
                p->active = 0;
                if (e->health <= 0) {
                    e->active = 2;
                    e-> death_frame = 0;
                    score++;
                    if (focused_enemy == p->target) focused_enemy = -1;
                    int j;
                    for (j = 0; j < MAX_VOLLEYS; j++) {
                        if (volleys[j].active && volleys[j].target == p->target)
                            volleys[j].active = 0;
                    }
                }
            }
        } else {
            p->active = 0;
        }
    }
}

static void handleChar(char c) {
    int i;
    int advanced_existing = 0;

    /* check for matches & advance existing matches, 
    looking only at enemies that the player has already 
    started typing. */
    for (i = 0; i < MAX_ENEMIES; i++) {
        struct Enemy *e = &enemies[i];
        if (!e->active || e->typed == 0) continue;

        if (e->word[e->typed] == c) {
            e->typed++;
            advanced_existing = 1;
            if (e->word[e->typed] == '\0') {
                triggerVolley(i);
                assignWord(e);
            }
        } else {
            /* enemy matched but next char didn't match - 
            reset progress on this enemy. */
            e->typed = 0;
        }
    }
    /* Start new matches if we didn't advance any existing words above
     * which prevents words from one enemy into bleeding into another.
     ex. typing "t" in frost would bleed into "t" for thunder */
    if (!advanced_existing) {
        for (i = 0; i < MAX_ENEMIES; i++) {
            struct Enemy *e = &enemies[i];
            if (!e->active || e->typed > 0) continue;

            if (e->word[0] == c) {
                e->typed = 1;
                if (e->word[1] == '\0') {
                    triggerVolley(i);
                    assignWord(e);
                }
            }
        }
    }
}

/* initialise floating background shapes with deterministic positions and
 * velocities derived from a fixed seed so the pattern is always the same. */
static void initBgShapes(void) {
    uint32_t st = 0xCAFEBABEu;
    int i;
    for (i = 0; i < MAX_BG_SHAPES; i++) {
        st = st * 1664525u + 1013904223u;
        bg_shapes[i].fx = (int32_t)((int)(st % (uint32_t)SCREEN_W) * FP_SCALE);
        st = st * 1664525u + 1013904223u;
        bg_shapes[i].fy = (int32_t)((int)(st % (uint32_t)SCREEN_H) * FP_SCALE);

        /* speed: 16..48 fixed-point units/tick (~0.06 to 0.19 px per frame) */
        st = st * 1664525u + 1013904223u;
        int32_t spd = (int32_t)(16 + (int)(st % 33u));

        /* diagonal direction (NE / SE / SW / NW) */
        st = st * 1664525u + 1013904223u;
        int dir = (int)(st % 4u);
        bg_shapes[i].vx = (dir <= 1) ? spd : -spd;
        bg_shapes[i].vy = (dir == 0 || dir == 3) ? -spd : spd;

        /* size: 15..50 px */
        st = st * 1664525u + 1013904223u;
        bg_shapes[i].size = 15 + (int)(st % 36u);

        /* shape: 2-in-3 chance of circle, 1-in-3 of rect */
        st = st * 1664525u + 1013904223u;
        bg_shapes[i].shape = ((int)(st % 3u) == 0) ? 1 : 0;

        /* colour: very subtle grey, intensity 0xEA..0xF4 against the 0xF8 bg */
        st = st * 1664525u + 1013904223u;
        uint32_t g = 0xEAu + (st % 0x0Bu);
        bg_shapes[i].color = (g << 16) | (g << 8) | g;
    }
    bg_shapes_inited = 1;
}

/* advance each shape one tick and wrap it around screen edges. */
static void updateBgShapes(void) {
    int i;
    for (i = 0; i < MAX_BG_SHAPES; i++) {
        bg_shapes[i].fx += bg_shapes[i].vx;
        bg_shapes[i].fy += bg_shapes[i].vy;
        int bx = (int)(bg_shapes[i].fx / FP_SCALE);
        int by = (int)(bg_shapes[i].fy / FP_SCALE);
        int s  = bg_shapes[i].size;

        if (bx > SCREEN_W + s)      bg_shapes[i].fx = (int32_t)(-s) * FP_SCALE;
        else if (bx < -s)           bg_shapes[i].fx = (int32_t)(SCREEN_W + s) * FP_SCALE;
        if (by > SCREEN_H + s)      bg_shapes[i].fy = (int32_t)(-s) * FP_SCALE;
        else if (by < -s)           bg_shapes[i].fy = (int32_t)(SCREEN_H + s) * FP_SCALE;
    }
}

/* paint the shapes onto the back buffer (no outlines – shapes are fills only). */
static void drawBgShapes(void) {
    int i;
    for (i = 0; i < MAX_BG_SHAPES; i++) {
        int bx = (int)(bg_shapes[i].fx / FP_SCALE);
        int by = (int)(bg_shapes[i].fy / FP_SCALE);
        int s  = bg_shapes[i].size;
        if (bg_shapes[i].shape == 0)
            fillCircle(bx, by, s, bg_shapes[i].color);
        else
            drawRect(bx - s, by - s, s * 2, s * 2, bg_shapes[i].color);
    }
}

/* render the full game frame. */
static void render(void) {
    clearScreen();

    /* ---- floating background shapes ---- */
    if (!bg_shapes_inited) initBgShapes();
    updateBgShapes();
    drawBgShapes();

    /* ---- player ---- */
    fillCircle(CENTER_X, CENTER_Y, PLAYER_RADIUS, COL_PLAYER);
    drawCircle(CENTER_X, CENTER_Y, PLAYER_RADIUS, COL_BLACK);

    /* ---- enemies ---- */
    int i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        struct Enemy *e = &enemies[i];
        if (!e->active) continue;

        if (e->active == 1) {
            /* pick base colour by type; override to COL_FOCUSED when player is typing */
            uint32_t base_col;
            if      (e->type == ENEMY_TYPE_TRIANGLE) base_col = COL_ENEMY_TRI;
            else if (e->type == ENEMY_TYPE_SQUARE)   base_col = COL_ENEMY_SQR;
            else                                     base_col = COL_ENEMY;
            uint32_t col = (e->typed > 0) ? COL_FOCUSED : base_col;

            if (e->type == ENEMY_TYPE_TRIANGLE) {
                /* triangle with the tip pointing toward the player (CENTER_X, CENTER_Y). */
                int tdx = CENTER_X - e->x;
                int tdy = CENTER_Y - e->y;
                int tdmax = max_int(abs_int(tdx), abs_int(tdy));
                int nx, ny;
                if (tdmax > 0) {
                    nx = tdx * ENEMY_RADIUS / tdmax;
                    ny = tdy * ENEMY_RADIUS / tdmax;
                } else {
                    nx = ENEMY_RADIUS; ny = 0;
                }
                /* tip vertex */
                int tx = e->x + nx, ty = e->y + ny;
                /* base centre (opposite side) */
                int bx = e->x - nx, by = e->y - ny;
                /* perpendicular offset for the two base corners */
                int px2 = -ny, py2 = nx;
                int p2x = bx + px2, p2y = by + py2;
                int p3x = bx - px2, p3y = by - py2;

                fillTriangle(tx, ty, p2x, p2y, p3x, p3y, col);
                /* outline */
                drawLine(tx, ty, p2x, p2y, COL_BLACK);
                drawLine(tx, ty, p3x, p3y, COL_BLACK);
                drawLine(p2x, p2y, p3x, p3y, COL_BLACK);
            } else if (e->type == ENEMY_TYPE_SQUARE) {
                int half = ENEMY_RADIUS;
                drawRect(e->x - half, e->y - half, half * 2, half * 2, col);
                /* outline */
                drawLine(e->x - half, e->y - half, e->x + half, e->y - half, COL_BLACK);
                drawLine(e->x + half, e->y - half, e->x + half, e->y + half, COL_BLACK);
                drawLine(e->x + half, e->y + half, e->x - half, e->y + half, COL_BLACK);
                drawLine(e->x - half, e->y + half, e->x - half, e->y - half, COL_BLACK);
            } else {
                /* normal circle enemy */
                fillCircle(e->x, e->y, ENEMY_RADIUS, col);
                drawCircle(e->x, e->y, ENEMY_RADIUS, COL_BLACK);
            }

            int bar_total  = 40;
            int bar_x      = e->x - bar_total / 2;
            int bar_y      = e->y - ENEMY_RADIUS - 8;
            int bar_filled = (e->max_health > 0) ? (bar_total * e->health) / e->max_health : 0;
            int bx, by;
            for (bx = bar_x; bx < bar_x + bar_total; bx++)
                for (by = bar_y; by < bar_y + 4; by++)
                    drawPixel(bx, by, (int)((bx < bar_x + bar_filled) ? COL_BAR_FG : COL_BAR_BG));

            /* draw the word below the enemy circle.
            * already-typed chars are shown in COL_TYPED; remaining in COL_UNTYPED. */
            int wlen   = game_strlen(e->word);
            int word_w = wlen * CHAR_W - 1;
            int wx     = e->x - word_w / 2;
            int wy     = e->y + ENEMY_RADIUS + 4;

            int k;
            for (k = 0; e->word[k]; k++) {
                uint32_t fg = (k < e->typed) ? COL_TYPED : COL_UNTYPED;
                drawChar(wx + k * CHAR_W, wy, e->word[k], fg);
            }
        }
        else if (e->active == 2) {
        /* DYING STATE: Draw the shrinking circle and 10 radiating lines */
            int r = ENEMY_RADIUS - (e->death_frame * 2);
            if (r > 0) drawCircle(e->x, e->y, r, COL_FOCUSED);

            /* directions for 10 sparks scaled for 256*/
            static const int32_t dx[10] = {256, 207, 79, -79, -207, -256, -207, -79, 79, 207};
            static const int32_t dy[10] = {0, 150, 243, 243, 150, 0, -150, -243, -243, -150};
            
            for (int j = 0; j < 10; j++) {
                /* sparks start at the current shrinking radius and fly outward */
                int start_dist = r + 1;
                int end_dist   = start_dist + (e->death_frame * 3);

                int x0 = e->x + (dx[j] * start_dist / 256);
                int y0 = e->y + (dy[j] * start_dist / 256);
                int x1 = e->x + (dx[j] * end_dist / 256);
                int y1 = e->y + (dy[j] * end_dist / 256);

                drawLine(x0, y0, x1, y1, COL_FOCUSED);
            }
        }
    }

    /* ---- projectiles ---- */
    for (i = 0; i < MAX_PROJECTILES; i++) {
        struct Projectile *p = &projectiles[i];
        if (!p->active) continue;
        int px = (int)(p->fx / FP_SCALE);
        int py = (int)(p->fy / FP_SCALE);
        fillCircle(px, py, 3, COL_PROJ);
    }

    /* ---- hud: health and score ---- */
    char buf[12];

    drawString(8, 8, "hp:", COL_HUD);
    itoa(player_health, buf);
    drawString(8 + 3 * CHAR_W, 8, buf, COL_HUD);

    drawString(8, 8 + CHAR_H + 4, "score:", COL_HUD);
    itoa(score, buf);
    drawString(8 + 6 * CHAR_W, 8 + CHAR_H + 4, buf, COL_HUD);

    /* ---- game-over message ---- */
    if (player_health <= 0) {
        drawString(CENTER_X - 4 * CHAR_W, CENTER_Y - CHAR_H,
                   "game over", COL_GAMEOVER);
        drawString(CENTER_X - 8 * CHAR_W, CENTER_Y + 4,
                   "press enter to return", COL_HUD);
    }

    /* present the completed frame to the screen. */
    flip_buffer();
}

/* pit-based frame-rate limiter
 *
 * programs the 8253/8254 programmable interval timer (pit) channel 0
 * to fire at TARGET_FPS hz. wait_for_pit_tick() polls the pic's
 * interrupt request register (irr) until irq0 is asserted, then
 * acknowledges it with an end-of-interrupt (eoi) command. because
 * interrupts are never enabled (no sti), the pending bit just sits in
 * the irr until we clear it - no handler required.
 *
 * result: the game loop is capped at roughly TARGET_FPS frames per second
 * regardless of how fast the cpu/emulator is, eliminating the
 * thousands-of-fps flicker that caused the stuttering.
 */

#define PIT_CH0_PORT  0x40
#define PIT_CMD_PORT  0x43
#define TARGET_FPS    30u
#define PIT_BASE_HZ   1193182u

static void init_pit(void) {
    uint16_t divisor = (uint16_t)(PIT_BASE_HZ / TARGET_FPS); /* ~39772 */
    /* channel 0, mode 3 (square wave), 16-bit binary. */
    outb(PIT_CMD_PORT, 0x36);
    outb(PIT_CH0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_PORT, (uint8_t)(divisor >> 8));
    /* unmask irq0 (pit) and irq1 (keyboard) in the pic1 imr so the
     * irr latch is updated when the timer fires.
     * bits: 0 is irq0/pit, 1 is irq1/keyboard (0 means unmasked). */
    outb(0x21, inb(0x21) & (uint8_t)~(0x01u | 0x02u));
}

/* spin until pit irq0 appears in the pic1 interrupt request register,
 * then send eoi to reset the latch for the next tick. */
static void wait_for_pit_tick(void) {
    for (;;) {
        outb(0x20, 0x0A);            /* ocw3: read irr */
        if (inb(0x20) & 0x01) break; /* irq0 pending - tick arrived */
        /* yield the speculative execution pipeline while spinning.
         * on real hardware this reduces power; in qemu it avoids
         * hammering the port-i/o path on every iteration. */
        asm volatile("pause");
    }
    outb(0x20, 0x20);                /* non-specific eoi to pic1 */
}

// arek - moved this code out of main to make it a bit neater
void updateGameState(){
    if (player_health > 0) {
        /* spawn new enemies on a timer. */
        spawn_timer++;
        if (spawn_timer >= spawn_interval) {
            spawn_timer = 0;
            spawnEnemy();
        }

        updateVolleys();
        updateProjectiles();
        updateEnemies();
    }
}

void handleInput(){
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;

    uint8_t scancode = inb(0x60);

    /* PS/2 arrow keys are prefixed with the 0xE0 escape byte.
     * We use a static flag to remember we just saw the prefix. */
    /* handleInput() is only ever called from the single-threaded main loop,
     * so this static flag is safe without explicit synchronisation. */
    static int extended_key = 0;

    if (scancode == 0xE0) {
        extended_key = 1;
        return;
    }

    /* bit 7 set means key-release; ignore (but clear extended flag). */
    if (scancode & 0x80) {
        extended_key = 0;
        return;
    }

    if (extended_key) {
        extended_key = 0;
        /* 0x4B = left arrow, 0x4D = right arrow (extended scancodes) */
        if (game_state == STATE_START) {
            if (scancode == 0x4Bu && selected_difficulty > 1)
                selected_difficulty--;
            else if (scancode == 0x4Du && selected_difficulty < 5)
                selected_difficulty++;
        }
        return;
    }

    /* normal (non-extended) key */
    char c = (char)keyboard_map[scancode];

    if (game_state == STATE_START) {
        if (c == '\n') {                  /* Enter – start the game */
            initGame();
            game_state = STATE_PLAYING;
        }
        return;
    }

    if (game_state == STATE_GAMEOVER) {
        if (c == '\n') {                  /* Enter – return to start screen */
            game_state = STATE_START;
        }
        return;
    }

    /* STATE_PLAYING: pass typed letters to the game logic */
    if (c >= 'a' && c <= 'z')
        handleChar(c);
}


/* kernel entry point */

void main() {

    /* important: this asm must remain the very first statement of main().
     * grub places the multiboot info pointer in ebx before calling us. */
    asm("mov %%ebx,%0"
        : "=r"(pMultibootInfo)
        :
        :);

    parseMultiboot2Info();

    /* set up the pic (required before init_pit so the irr latching works)
     * and program pit channel 0 for roughly TARGET_FPS hz. */
    remap_pic();
    init_pit();

    /* ---- main game loop ---- */
    while (1) {

        /* poll ps/2 keyboard before the frame gate so keystrokes are
         * never delayed by more than one tick. */
        handleInput();

        /* block until the pit fires (roughly TARGET_FPS hz).
         * this caps the frame rate so the screen updates at a smooth,
         * human-friendly rate instead of as fast as the cpu allows. */
        wait_for_pit_tick();

        if (game_state == STATE_START) {
            drawStartScreen();
        } else {
            /* STATE_PLAYING or STATE_GAMEOVER */
            if (game_state == STATE_PLAYING && player_health <= 0)
                game_state = STATE_GAMEOVER;

            updateGameState(); /* spawn enemies & update their positions */
            render();          /* draws to back buffer, then flips to framebuffer */
        }
    }
}
