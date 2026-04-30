#include <stdint.h>
#include "interrupt.h"
#include "keyboard.h"
extern void puts(const char *s);
struct idt_entry idt_entries[256];
struct idt_ptr   idt_ptr;
struct tss_entry tss_ent;

void outb (uint16_t _port, uint8_t val) {
    __asm__ __volatile__ ("outb %0, %1" : : "a" (val),  "dN" (_port) );
}

uint8_t inb (uint16_t _port) {
    uint8_t rv;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (rv) : "dN" (_port));
    return rv;
}


void memset(char *s, char c, unsigned int n) {
    for(int k = 0; k < n ; k++) {
        s[k] = c;
    }
}

void tss_flush (uint16_t tss) {
  asm("ltr %0" : :"a"(tss));
}


struct gdt_entry_bits gdt[] = {{
    .limit_low = 0,
    .base_low = 0,
    .accessed = 0,
    .read_write = 0,
    .conforming_expand_down = 0,
    .code = 0,
    .always_1 = 0,
    .DPL = 0,
    .present = 0,
    .limit_high = 0,
    .available = 0,
    .always_0 = 0,
    .big = 0,
    .gran = 0,
    .base_high = 0
},{
    .limit_low = 0xffff,
    .base_low = 0,
    .accessed = 0,
    .read_write = 1,
    .conforming_expand_down = 0,
    .code = 1,
    .always_1 = 1,
    .DPL = 0,
    .present = 1,
    .limit_high = 0xf,
    .available = 0,
    .always_0 = 0,
    .big = 1,
    .gran = 1,
    .base_high = 0
},{
    .limit_low = 0xffff,
    .base_low = 0,
    .accessed = 0,
    .read_write = 1,
    .conforming_expand_down = 0,
    .code = 0,
    .always_1 = 1,
    .DPL = 0,
    .present = 1,
    .limit_high = 0xf,
    .available = 0,
    .always_0 = 0,
    .big = 1,
    .gran = 1,
    .base_high = 0
},{
    .limit_low = 0xffff,
    .base_low = 0,
    .accessed = 0,
    .read_write = 1,
    .conforming_expand_down = 0,
    .code = 1,
    .always_1 = 1,
    .DPL = 3,
    .present = 1,
    .limit_high = 0xf,
    .available = 0,
    .always_0 = 0,
    .big = 1,
    .gran = 1,
    .base_high = 0
},{
    .limit_low = 0xffff,
    .base_low = 0,
    .accessed = 0,
    .read_write = 1,
    .conforming_expand_down = 0,
    .code = 0,
    .always_1 = 1,
    .DPL = 3,
    .present = 1,
    .limit_high = 0xf,
    .available = 0,
    .always_0 = 0,
    .big = 1,
    .gran = 1,
    .base_high = 0
},{
    .accessed = 1,
    .read_write = 0,
    .conforming_expand_down = 0,
    .code = 1,
    .always_1 = 0,
    .DPL = 3,
    .present = 1,
    .limit_high = (sizeof(struct tss_entry) & 0xF0000)>>16,
    .available = 0,
    .always_0 = 0,
    .big = 0,
    .gran = 0,
}
};

struct seg_desc gdt_desc = { .sz = sizeof(gdt)-1, .addr = (uint32_t)(&gdt[0]) };


void load_gdt() {


    asm("cli\n"
        "lgdt [gdt_desc]\n"
        "ljmp $0x8,$gdt_flush\n"
"gdt_flush:\n"
        "mov %%eax, 0x10\n"
        "mov %%ds, %%eax\n"
        "mov %%ss, %%eax\n"
        "mov %%es, %%eax\n"
        "mov %%fs, %%eax\n"
        "mov %%gs, %%eax\n" : : : "eax");

}



void write_tss(struct gdt_entry_bits *g) {
    uint32_t base = (uint32_t) &tss_ent;
    uint32_t limit = base + sizeof(struct tss_entry);
    extern uint32_t stack_top;

    g->limit_low = limit & 0xFFFF;
    g->base_low = base & 0xFFFFFF;
    g->accessed = 1;
    g->read_write = 0;
    g->conforming_expand_down = 0;
    g->code = 1;
    g->always_1 = 0;
    g->DPL = 3;
    g->present = 1;
    g->limit_high = (limit & 0xF0000)>>16;
    g->available = 0;
    g->always_0 = 0;
    g->big = 0;
    g->gran = 0;
    g->base_high = (base & 0xFF000000)>>24;

    memset((char*)&tss_ent, 0, sizeof(tss_ent));

    extern int _end_stack;

    tss_ent.ss0  = 16;
    tss_ent.esp0 = (uint32_t)&_end_stack;
    tss_ent.cs   = 0x0b;
    tss_ent.ss = tss_ent.ds = tss_ent.es = tss_ent.fs = tss_ent.gs = 0x13;

    tss_flush(0x2b);
}






void PIC_sendEOI(unsigned char irq) {
	if(irq >= 8) {
		outb(PIC_2_COMMAND,PIC_EOI);
    }
	outb(PIC_1_COMMAND,PIC_EOI);
}

void IRQ_set_mask(unsigned char IRQline) {
    uint16_t port;
    uint8_t value;
 
    if(IRQline < 8) {
        port = PIC_1_DATA;
    } else {
        port = PIC_2_DATA;
        IRQline -= 8;
    }
    value = inb(port) | (1 << IRQline);
    outb(port, value);        
}
 
void IRQ_clear_mask(unsigned char IRQline) {
    uint16_t port;
    uint8_t value;
 
    if(IRQline < 8) {
        port = PIC_1_DATA;
    } else {
        port = PIC_2_DATA;
        IRQline -= 8;
    }
    value = inb(port) & ~(1 << IRQline);
    outb(port, value);        
}

void idt_flush(struct idt_ptr *idt){
    asm("lidt %0\n"
        :
        : "m"(*idt)
        :);
}


__attribute__((interrupt)) void divide_error_handler(struct interrupt_frame* frame)
{
    asm("cli");
    while(1);
}

__attribute__((interrupt)) void debug_exception_handler(struct interrupt_frame* frame)
{
    asm("cli");
}

__attribute__((interrupt)) void breakpoint_exception_handler(struct interrupt_frame* frame)
{
    asm("cli");
}

__attribute__((interrupt)) void overflow_handler(struct interrupt_frame* frame)
{
    asm("cli");
}

__attribute__((interrupt)) void bound_check_handler(struct interrupt_frame* frame)
{
    asm("cli");
}

__attribute__((interrupt)) void invalid_opcode_handler(struct interrupt_frame* frame)
{
    asm("cli");
}

__attribute__((interrupt)) void coprocessor_not_available_handler(struct interrupt_frame* frame)
{
    asm("cli");
}

__attribute__((interrupt)) void double_fault_handler(struct interrupt_frame* frame, uint32_t error_code)
{
    asm("cli");
    puts("*** DOUBLE FAULT ***\n");
    while(1);
}


__attribute__((interrupt)) void coprocessor_segment_overrun_handler(struct interrupt_frame* frame)
{
    asm("cli");
}


__attribute__((interrupt)) void invalid_tss_handler(struct interrupt_frame* frame, uint32_t error_code)
{
    asm("cli");
    puts("*** INVALID TSS ***\n");
    while(1);
}


__attribute__((interrupt)) void segment_not_present_handler(struct interrupt_frame* frame, uint32_t error_code)
{
    asm("cli");
    puts("*** SEGMENT NOT PRESENT ***\n");
    while(1);
}


__attribute__((interrupt)) void stack_exception_handler(struct interrupt_frame* frame, uint32_t error_code)
{
    asm("cli");
    puts("*** STACK EXCEPTION ***\n");
    while(1);
}


__attribute__((interrupt)) void general_protection_handler(struct interrupt_frame* frame, uint32_t error_code)
{
    asm("cli");
    puts("*** GENERAL PROTECTION FAULT ***\n");
    while(1);
}

__attribute__((interrupt)) void page_fault_handler(struct interrupt_frame* frame, uint32_t error_code)
{
    asm("cli");
    puts("*** PAGE FAULT ***\n");
    while(1);
}


__attribute__((interrupt)) void coprocessor_error_handler(struct interrupt_frame* frame)
{
    asm("cli");
    while(1);
}

__attribute__((interrupt)) void stub_isr(struct interrupt_frame* frame)
{
    PIC_sendEOI(0);
}

__attribute__((interrupt)) void pit_handler(struct interrupt_frame* frame)
{
    PIC_sendEOI(0);
}


__attribute__((interrupt)) void keyboard_handler(struct interrupt_frame* frame)
{
	uint8_t scancode;
	scancode = inb(0x60);
    if (scancode < 128) {
		extern void putc(int data);
		char ascii = keyboard_map[scancode];
        if (ascii != 0) {
            putc(ascii);
		}
	}
	PIC_sendEOI(1);
}


__attribute__((interrupt)) void syscall_handler(struct interrupt_frame* frame)
{
    asm("cli");
    while(1);
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
   idt_entries[num].base_lo = base & 0xFFFF;
   idt_entries[num].base_hi = (base >> 16) & 0xFFFF;

   idt_entries[num].sel     = sel;
   idt_entries[num].always0 = 0;
   idt_entries[num].flags   = flags;
}

void init_idt() {
    int i;


    extern struct gdt_entry_bits gdt[];
    write_tss(&gdt[5]);

    idt_ptr.limit = sizeof(struct idt_entry) * 256 -1;
    idt_ptr.base  = (uint32_t)&idt_entries;

    memset((char*)&idt_entries, 0, sizeof(struct idt_entry)*256);

    for(i = 0; i < 256; i++){
        idt_set_gate( i, (uint32_t)stub_isr, 0x08, 0x8E);
    }
    idt_set_gate(0, (uint32_t)divide_error_handler, 0x08, 0x8e);
    idt_set_gate(1, (uint32_t)debug_exception_handler, 0x08, 0x8e);
    idt_set_gate(2, (uint32_t)breakpoint_exception_handler, 0x08, 0x8e);
    idt_set_gate(3, (uint32_t)overflow_handler, 0x08, 0x8e);
    idt_set_gate(4, (uint32_t)overflow_handler, 0x08, 0x8e);
    idt_set_gate(5, (uint32_t)bound_check_handler, 0x08, 0x8e);
    idt_set_gate(6, (uint32_t)invalid_opcode_handler, 0x08, 0x8e);
    idt_set_gate(7, (uint32_t)coprocessor_not_available_handler, 0x08, 0x8e);
    idt_set_gate(8, (uint32_t)double_fault_handler, 0x08, 0x8e);
    idt_set_gate(10, (uint32_t)invalid_tss_handler, 0x08, 0x8e);
    idt_set_gate(11, (uint32_t)segment_not_present_handler, 0x08, 0x8e);
    idt_set_gate(12, (uint32_t)stack_exception_handler, 0x08, 0x8e);

    idt_set_gate(13, (uint32_t)general_protection_handler, 0x08, 0x8e);
    idt_set_gate(14, (uint32_t)page_fault_handler, 0x08, 0x8e);

    idt_set_gate(0x21, (uint32_t)keyboard_handler,0x08, 0x8e);
    idt_set_gate(0x80, (uint32_t)syscall_handler,0x08, 0xee);
    idt_set_gate(32,   (uint32_t)pit_handler, 0x08, 0x8e);
    idt_flush(&idt_ptr);
}

void remap_pic(void)
{
    outb(PIC_1_CTRL, 0x11);
    outb(PIC_2_CTRL, 0x11);

    outb(PIC_1_DATA, 0x20);
    outb(PIC_2_DATA, 0x28);

    outb(PIC_1_DATA, 0x00);
    outb(PIC_2_DATA, 0x00);

    outb(PIC_1_DATA, 0x01);
    outb(PIC_2_DATA, 0x01);
    outb(0x21 , 0xff);
    outb(0xA1 , 0xff);
    outb(0x21, 0xfd);
}
