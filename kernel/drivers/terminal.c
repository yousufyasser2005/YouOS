#include <kernel/terminal.h>
#include <kernel/keyboard.h>
#include <kernel/vga.h>

void terminal_init(void) { keyboard_init(); }

void terminal_readline(char* buf, size_t max)
{
    size_t pos = 0;
    while (1) {
        char c = keyboard_getchar();
        if (c == '\n' || c == '\r') {
            buf[pos] = '\0';
            vga_putchar('\n');
            return;
        }
        if (c == '\b') {
            if (pos > 0) {
                pos--;
                vga_putchar('\b');
                vga_putchar(' ');
                vga_putchar('\b');
            }
            continue;
        }
        if (pos < max - 1) { buf[pos++] = c; vga_putchar(c); }
    }
}

typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,l)   __builtin_va_arg(v,l)

static void kp_puts(const char* s) { while(*s) vga_putchar(*s++); }
static void kp_uint(uint64_t v,int base,int up) {
    const char* d=up?"0123456789ABCDEF":"0123456789abcdef";
    char b[64]; int i=63; b[i]=0;
    if(!v){vga_putchar('0');return;}
    while(v){b[--i]=d[v%base];v/=base;}
    kp_puts(&b[i]);
}
static void kp_int(int64_t v){if(v<0){vga_putchar('-');v=-v;}kp_uint(v,10,0);}
void kprintf(const char* fmt,...){
    va_list a; va_start(a,fmt);
    while(*fmt){
        if(*fmt!='%'){vga_putchar(*fmt++);continue;}
        fmt++;
        switch(*fmt){
            case 's':kp_puts(va_arg(a,const char*));break;
            case 'c':vga_putchar((char)va_arg(a,int));break;
            case 'd':kp_int(va_arg(a,int));break;
            case 'u':kp_uint(va_arg(a,unsigned),10,0);break;
            case 'x':vga_putchar('0');vga_putchar('x');
                     kp_uint(va_arg(a,unsigned),16,0);break;
            case '%':vga_putchar('%');break;
            default:vga_putchar('%');vga_putchar(*fmt);break;
        }
        fmt++;
    }
    va_end(a);
}
