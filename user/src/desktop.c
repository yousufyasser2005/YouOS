#include "../lib/syscall.h"

typedef unsigned int   u32;
typedef unsigned long  u64;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef signed long    s64;

/* ── framebuffer ─────────────────────────────────────────────── */
static u64 FB_W, FB_H;
static u32 buf[1024 * 768];

static void px(int x, int y, u32 c) {
    if ((u64)x < FB_W && (u64)y < FB_H) buf[y*1024+x] = c;
}
static void rect(int x,int y,int w,int h,u32 c){
    for(int r=y;r<y+h;r++) for(int col=x;col<x+w;col++) px(col,r,c);
}
static void outline(int x,int y,int w,int h,u32 c){
    for(int i=x;i<x+w;i++){px(i,y,c);px(i,y+h-1,c);}
    for(int i=y;i<y+h;i++){px(x,i,c);px(x+w-1,i,c);}
}
static void hline(int x,int y,int w,u32 c){
    for(int i=0;i<w;i++) px(x+i,y,c);
}
static void vline(int x,int y,int h,u32 c){
    for(int i=0;i<h;i++) px(x,y+i,c);
}
static void flush(void){ sys_fbwrite(0,0,1024,768,buf); }

/* ── 8×16 font ───────────────────────────────────────────────── */
static const u8 font[96][16]={
{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0x18,0x18,0x18,0x18,0x18,0x18,0,0x18,0,0,0,0,0,0},
{0,0,0x6C,0x6C,0x6C,0,0,0,0,0,0,0,0,0,0,0},{0,0,0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0,0,0,0,0,0,0},
{0,0x18,0x7E,0xD8,0xD8,0x7C,0x1E,0x1B,0x1B,0x7E,0x18,0,0,0,0,0},{0,0,0x60,0x66,0x0C,0x18,0x30,0x66,0x06,0,0,0,0,0,0,0},
{0,0,0x38,0x6C,0x6C,0x38,0x76,0xDC,0xCC,0x76,0,0,0,0,0,0},{0,0,0x18,0x18,0x30,0,0,0,0,0,0,0,0,0,0,0},
{0,0,0x0C,0x18,0x30,0x30,0x30,0x30,0x18,0x0C,0,0,0,0,0,0},{0,0,0x30,0x18,0x0C,0x0C,0x0C,0x0C,0x18,0x30,0,0,0,0,0,0},
{0,0,0,0x66,0x3C,0xFF,0x3C,0x66,0,0,0,0,0,0,0,0},{0,0,0,0x18,0x18,0x7E,0x18,0x18,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0x18,0x18,0x30,0,0,0,0,0},{0,0,0,0,0,0x7E,0,0,0,0,0,0,0,0,0,0},
{0,0,0,0,0,0,0,0,0x18,0x18,0,0,0,0,0,0},{0,0,0x06,0x06,0x0C,0x18,0x30,0x60,0x60,0,0,0,0,0,0,0},
{0,0,0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0,0,0,0,0,0,0},{0,0,0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0,0,0,0,0,0,0},
{0,0,0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0,0,0,0,0,0,0},{0,0,0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0,0,0,0,0,0,0},{0,0,0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0,0,0,0,0,0,0},
{0,0,0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0,0,0,0,0,0,0},
{0,0,0,0x18,0x18,0,0,0x18,0x18,0,0,0,0,0,0,0},{0,0,0,0x18,0x18,0,0,0x18,0x18,0x30,0,0,0,0,0,0},
{0,0,0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0,0,0,0,0,0,0},{0,0,0,0,0x7E,0,0x7E,0,0,0,0,0,0,0,0,0},
{0,0,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x06,0x0C,0x18,0,0x18,0,0,0,0,0,0,0},
{0,0,0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0,0,0,0,0,0,0},{0,0,0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0,0,0,0,0,0,0},
{0,0,0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0,0,0,0,0,0,0},{0,0,0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0,0,0,0,0,0,0},
{0,0,0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0,0,0,0,0,0,0},{0,0,0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0,0,0,0,0,0,0},
{0,0,0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0,0,0,0,0,0,0},{0,0,0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0,0,0,0,0,0,0},
{0,0,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0,0,0,0,0,0,0},{0,0,0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0,0,0,0,0,0,0},
{0,0,0x66,0x76,0x7E,0x6E,0x66,0x66,0x66,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x66,0x66,0x6E,0x3C,0x06,0,0,0,0,0,0,0},
{0,0,0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0,0,0,0,0,0,0},{0,0,0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0,0,0},{0,0,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0,0,0,0,0,0,0},{0,0,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0,0,0,0,0,0,0},
{0,0,0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0,0,0,0,0,0,0},{0,0,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0,0,0,0,0,0,0},
{0,0,0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0,0,0,0,0,0,0},{0,0,0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0,0,0,0,0,0,0},
{0,0,0x60,0x60,0x30,0x18,0x0C,0x06,0x06,0,0,0,0,0,0,0},{0,0,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0,0,0,0,0,0,0},
{0,0,0x18,0x3C,0x66,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0xFF,0,0,0,0,0,0},
{0,0,0x18,0x18,0x0C,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0x3C,0x06,0x3E,0x66,0x3E,0,0,0,0,0,0,0},
{0,0,0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0,0,0,0,0,0,0},{0,0,0,0,0x3C,0x66,0x60,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0,0,0,0,0,0,0},{0,0,0,0,0x3C,0x66,0x7E,0x60,0x3C,0,0,0,0,0,0,0},
{0,0,0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0,0,0,0,0,0,0},{0,0,0,0,0x3E,0x66,0x66,0x3E,0x06,0x7C,0,0,0,0,0,0},
{0,0,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0,0,0,0,0,0,0},{0,0,0x18,0,0x38,0x18,0x18,0x18,0x3C,0,0,0,0,0,0,0},
{0,0,0x06,0,0x06,0x06,0x06,0x06,0x66,0x3C,0,0,0,0,0,0},{0,0,0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0,0,0,0,0,0,0},
{0,0,0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0,0,0,0,0,0,0},{0,0,0,0,0x66,0x7F,0x7F,0x6B,0x63,0,0,0,0,0,0,0},
{0,0,0,0,0x7C,0x66,0x66,0x66,0x66,0,0,0,0,0,0,0},{0,0,0,0,0x3C,0x66,0x66,0x66,0x3C,0,0,0,0,0,0,0},
{0,0,0,0,0x7C,0x66,0x66,0x7C,0x60,0x60,0,0,0,0,0,0},{0,0,0,0,0x3E,0x66,0x66,0x3E,0x06,0x06,0,0,0,0,0,0},
{0,0,0,0,0x7C,0x66,0x60,0x60,0x60,0,0,0,0,0,0,0},{0,0,0,0,0x3C,0x60,0x3C,0x06,0x7C,0,0,0,0,0,0,0},
{0,0,0x30,0x30,0x7C,0x30,0x30,0x30,0x1C,0,0,0,0,0,0,0},{0,0,0,0,0x66,0x66,0x66,0x66,0x3E,0,0,0,0,0,0,0},
{0,0,0,0,0x66,0x66,0x66,0x3C,0x18,0,0,0,0,0,0,0},{0,0,0,0,0x63,0x6B,0x7F,0x7F,0x36,0,0,0,0,0,0,0},
{0,0,0,0,0x66,0x3C,0x18,0x3C,0x66,0,0,0,0,0,0,0},{0,0,0,0,0x66,0x66,0x3E,0x06,0x7C,0,0,0,0,0,0,0},
{0,0,0,0,0x7E,0x0C,0x18,0x30,0x7E,0,0,0,0,0,0,0},{0,0,0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0,0,0,0,0,0,0},
{0,0,0x18,0x18,0x18,0,0x18,0x18,0x18,0,0,0,0,0,0,0},{0,0,0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0,0,0,0,0,0,0},
{0,0,0x76,0xDC,0,0,0,0,0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

static void glyph(int x,int y,char c,u32 fg,u32 bg){
    int idx=(u8)c-32; if(idx<0||idx>=96) idx=0;
    for(int r=0;r<16;r++){u8 b=font[idx][r];
        for(int col=0;col<8;col++) px(x+col,y+r,(b&(0x80>>col))?fg:bg);}
}
static void text(int x,int y,const char*s,u32 fg,u32 bg){
    while(*s){glyph(x,y,*s++,fg,bg);x+=8;}
}
static void text_center(int cx,int y,const char*s,u32 fg,u32 bg){
    int l=0; const char*p=s; while(*p++)l++;
    text(cx-l*4,y,s,fg,bg);
}

/* ── math helpers ────────────────────────────────────────────── */
static s64 isin(int deg){
    /* sin * 1000, degrees 0-359 */
    static const short t[]={0,17,34,52,69,87,104,121,139,156,173,190,207,224,241,258,275,292,309,325,342,358,374,390,406,422,438,453,469,484,499,515,529,544,559,573,587,601,615,629,642,656,669,681,694,707,719,731,743,754,766,777,788,798,809,819,829,838,848,857,866,874,882,891,898,906,913,920,927,933,939,945,951,956,961,965,970,974,978,981,984,987,990,992,994,996,997,998,999,999,1000,999,999,998,997,996,994,992,990,987,984,981,978,974,970,965,961,956,951,945,939,933,927,920,913,906,898,891,882,874,866,857,848,838,829,819,809,798,788,777,766,754,743,731,719,707,694,681,669,656,642,629,615,601,587,573,559,544,529,515,499,484,469,453,438,422,406,390,374,358,342,325,309,292,275,258,241,224,207,190,173,156,139,121,104,87,69,52,34,17};
    deg=((deg%360)+360)%360;
    if(deg<180) return t[deg];
    return -t[deg-180];
}
static s64 icos(int deg){ return isin(deg+90); }

static void line_aa(int x0,int y0,int x1,int y1,u32 c){
    int dx=x1-x0,dy=y1-y0;
    int steps=dx<0?-dx:dx;
    if((dy<0?-dy:dy)>steps) steps=(dy<0?-dy:dy);
    if(!steps){px(x0,y0,c);return;}
    for(int i=0;i<=steps;i++){
        px(x0+dx*i/steps, y0+dy*i/steps, c);
    }
}

/* ── colors ──────────────────────────────────────────────────── */
#define BG       0x0D1117
#define PANEL_BG 0x161B22
#define TASKBAR  0x0D1117
#define BORDER   0x30363D
#define TEXT     0xE6EDF3
#define DIM      0x8B949E
#define ACCENT   0x58A6FF
#define GREEN    0x3FB950
#define RED      0xF85149
#define YELLOW   0xD29922
#define PURPLE   0xBC8CFF
#define CYAN     0x39D353
#define WHITE    0xFFFFFF
#define TBAR_H   40
#define PANEL_W  240
#define PANEL_X  (1024-PANEL_W)

/* ── wallpaper ───────────────────────────────────────────────── */
static void wallpaper(void){
    /* Dark gradient */
    for(int y=0;y<768-TBAR_H;y++){
        u32 r=0x0D, g=0x11+(y*8)/768, b=0x17+(y*20)/768;
        u32 c=(r<<16)|(g<<8)|b;
        for(int x=0;x<PANEL_X;x++) px(x,y,c);
    }
    /* Subtle grid lines */
    for(int y=0;y<768-TBAR_H;y+=80)
        for(int x=0;x<PANEL_X;x++) px(x,y,0x161B22);
    for(int x=0;x<PANEL_X;x+=80)
        for(int y=0;y<768-TBAR_H;y++) px(x,y,0x161B22);
}

/* ── desktop icons ───────────────────────────────────────────── */
typedef struct { int x,y; const char* name; u32 color; } Icon;
static Icon icons[]={
    {60,80,"Terminal",0x58A6FF},
    {60,180,"Files",   0x3FB950},
    {60,280,"About",   0xBC8CFF},
};
#define N_ICONS 3
static int icon_hovered=-1;

static void draw_icon(int i){
    Icon*ic=&icons[i];
    u32 bg=(i==icon_hovered)?0x21262D:BG;
    /* Icon box */
    rect(ic->x-4,ic->y-4,72,72,bg);
    outline(ic->x-4,ic->y-4,72,72,
            i==icon_hovered?ACCENT:BORDER);
    /* Icon symbol */
    rect(ic->x+10,ic->y+10,52,40,ic->color);
    /* Darken inner */
    for(int r=ic->y+12;r<ic->y+48;r++)
        for(int c2=ic->x+12;c2<ic->x+60;c2++){
            u32 col=buf[r*1024+c2];
            u32 rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;
            buf[r*1024+c2]=((rr/2)<<16)|((gg/2)<<8)|(bb/2);
        }
    /* Label */
    int llen=0; const char*p=ic->name; while(*p++)llen++;
    text(ic->x+(64-llen*8)/2, ic->y+66, ic->name, TEXT, BG);
}

static void draw_icons(void){
    for(int i=0;i<N_ICONS;i++) draw_icon(i);
}

/* ── start menu ──────────────────────────────────────────────── */
static int menu_open=0;
static const char* menu_items[]={"Terminal","Files","About","Settings","Shutdown"};
#define N_MENU 5
static int menu_sel=-1;

static void draw_menu(void){
    if(!menu_open) return;
    int mx=4,my=768-TBAR_H-N_MENU*32-8;
    rect(mx,my,220,N_MENU*32+8,PANEL_BG);
    outline(mx,my,220,N_MENU*32+8,BORDER);
    text(mx+8,my+4,"Applications",DIM,PANEL_BG);
    for(int i=0;i<N_MENU;i++){
        int iy=my+20+i*32;
        u32 bg=(i==menu_sel)?0x21262D:PANEL_BG;
        rect(mx+2,iy,216,28,bg);
        /* Colored dot */
        u32 dots[]={ACCENT,GREEN,PURPLE,YELLOW,RED};
        rect(mx+8,iy+8,12,12,dots[i]);
        text(mx+26,iy+6,menu_items[i],TEXT,bg);
    }
}

/* ── taskbar ─────────────────────────────────────────────────── */
static int taskbar_running=1; /* desktop itself */

static void draw_taskbar(void){
    int ty=768-TBAR_H;
    rect(0,ty,1024,TBAR_H,TASKBAR);
    hline(0,ty,1024,BORDER);

    /* Start button */
    u32 sbg=menu_open?ACCENT:0x21262D;
    rect(4,ty+4,80,TBAR_H-8,sbg);
    outline(4,ty+4,80,TBAR_H-8,menu_open?0x79C0FF:BORDER);
    text(12,ty+12,"  YouOS",menu_open?0x0D1117:ACCENT,sbg);

    /* Running apps */
    rect(92,ty+4,120,TBAR_H-8,0x21262D);
    outline(92,ty+4,120,TBAR_H-8,ACCENT);
    hline(92,ty+TBAR_H-2,120,ACCENT); /* active indicator */
    text(100,ty+12,"Desktop",TEXT,0x21262D);

    /* Clock in taskbar */
    u64 ticks=sys_ticks();
    u64 secs=ticks/100;
    u64 hh=(secs/3600)%24, mm=(secs/60)%60, ss=secs%60;
    char clk[9];
    clk[0]='0'+hh/10; clk[1]='0'+hh%10; clk[2]=':';
    clk[3]='0'+mm/10; clk[4]='0'+mm%10; clk[5]=':';
    clk[6]='0'+ss/10; clk[7]='0'+ss%10; clk[8]=0;
    text(1024-PANEL_W-80, ty+12, clk, TEXT, TASKBAR);
}

/* ── right panel ─────────────────────────────────────────────── */
static void draw_panel_bg(void){
    rect(PANEL_X,0,PANEL_W,768,PANEL_BG);
    vline(PANEL_X,0,768,BORDER);
}

/* Analog clock */
static void draw_analog_clock(int cx,int cy,int r,u64 secs){
    int hh=(secs/3600)%12, mm=(secs/60)%60, ss=secs%60;

    /* Face */
    /* Draw circle approximation */
    for(int deg=0;deg<360;deg+=2){
        int x=cx+(int)(icos(deg)*r/1000);
        int y=cy-(int)(isin(deg)*r/1000);
        px(x,y,BORDER);
        int xi=cx+(int)(icos(deg)*(r-1)/1000);
        int yi=cy-(int)(isin(deg)*(r-1)/1000);
        px(xi,yi,BORDER);
    }
    /* Fill face */
    for(int dy=-r+2;dy<r-1;dy++)
        for(int dx=-r+2;dx<r-1;dx++)
            if(dx*dx+dy*dy<(r-2)*(r-2))
                px(cx+dx,cy+dy,0x161B22);

    /* Hour markers */
    for(int i=0;i<12;i++){
        int deg=i*30;
        int x1=cx+(int)(icos(deg)*(r-4)/1000);
        int y1=cy-(int)(isin(deg)*(r-4)/1000);
        int x2=cx+(int)(icos(deg)*(r-10)/1000);
        int y2=cy-(int)(isin(deg)*(r-10)/1000);
        u32 mc=(i==0||i==3||i==6||i==9)?WHITE:DIM;
        line_aa(x1,y1,x2,y2,mc);
    }

    /* Hour hand */
    int hdeg=(hh*30+mm/2)-90;
    line_aa(cx,cy,
            cx+(int)(icos(hdeg)*(r-28)/1000),
            cy-(int)(isin(hdeg)*(r-28)/1000),WHITE);
    line_aa(cx,cy,
            cx+(int)(icos(hdeg+1)*(r-28)/1000),
            cy-(int)(isin(hdeg+1)*(r-28)/1000),WHITE);

    /* Minute hand */
    int mdeg=mm*6-90;
    line_aa(cx,cy,
            cx+(int)(icos(mdeg)*(r-18)/1000),
            cy-(int)(isin(mdeg)*(r-18)/1000),ACCENT);
    line_aa(cx,cy,
            cx+(int)(icos(mdeg+1)*(r-18)/1000),
            cy-(int)(isin(mdeg+1)*(r-18)/1000),ACCENT);

    /* Second hand */
    int sdeg=ss*6-90;
    line_aa(cx,cy,
            cx+(int)(icos(sdeg)*(r-14)/1000),
            cy-(int)(isin(sdeg)*(r-14)/1000),RED);

    /* Center dot */
    rect(cx-3,cy-3,6,6,WHITE);
}

/* Calendar */
static const char* months[]={"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
static const int days_in_month[]={31,28,31,30,31,30,31,31,30,31,30,31};
static const char* day_names[]={"Mo","Tu","We","Th","Fr","Sa","Su"};

static void draw_calendar(int x,int y,int month,int year,int today){
    /* month: 1-12, today: day of month */
    rect(x,y,PANEL_W-8,160,0x0D1117);
    outline(x,y,PANEL_W-8,160,BORDER);

    /* Header */
    char hdr[16];
    const char* mn=months[month-1];
    int k=0;
    hdr[k++]=mn[0];hdr[k++]=mn[1];hdr[k++]=mn[2];
    hdr[k++]=' ';
    hdr[k++]='0'+year/1000; hdr[k++]='0'+(year/100)%10;
    hdr[k++]='0'+(year/10)%10; hdr[k++]='0'+year%10;
    hdr[k]=0;
    text_center(x+PANEL_W/2-4,y+4,hdr,TEXT,0x0D1117);
    hline(x,y+20,PANEL_W-8,BORDER);

    /* Day headers */
    for(int d=0;d<7;d++)
        text(x+4+d*30,y+24,day_names[d],DIM,0x0D1117);
    hline(x,y+38,PANEL_W-8,0x21262D);

    /* Calculate first weekday of month (simplified: assume Mon=0) */
    /* Using Zeller-like: Jan 2026 starts on Thu (3) */
    /* Approximate: use ticks-based calculation */
    int first_dow=3; /* Thursday for May 2026 roughly */
    if(month==5) first_dow=4; /* May 2026 starts on Friday */

    int days=days_in_month[month-1];
    int col=first_dow,row=0;
    for(int d=1;d<=days;d++){
        int dx=x+4+col*30;
        int dy=y+42+row*18;
        u32 fg=TEXT, bg2=0x0D1117;
        if(d==today){ rect(dx-2,dy-1,18,16,ACCENT); fg=0x0D1117; bg2=ACCENT; }
        else if(col>=5){ fg=0xF85149; } /* weekend */
        char ds[3]; ds[0]='0'+d/10; ds[1]='0'+d%10; ds[2]=0;
        if(d<10){ds[0]=' ';ds[1]='0'+d;}
        text(dx,dy,ds,fg,bg2);
        col++;
        if(col==7){col=0;row++;}
    }
}

/* Digital clock */
static void draw_digital_clock(int x,int y,u64 secs){
    u64 hh=(secs/3600)%24, mm=(secs/60)%60, ss=secs%60;
    char t[9];
    t[0]='0'+hh/10;t[1]='0'+hh%10;t[2]=':';
    t[3]='0'+mm/10;t[4]='0'+mm%10;t[5]=':';
    t[6]='0'+ss/10;t[7]='0'+ss%10;t[8]=0;
    /* Large digits */
    int cx=x+PANEL_W/2-4;
    /* Scale x2 by drawing twice */
    for(int i=0;t[i];i++){
        int gx=cx-36+i*14;
        glyph(gx,y,t[i],ACCENT,PANEL_BG);
        glyph(gx,y+8,t[i],ACCENT,PANEL_BG);
    }
    /* Date line */
    text_center(cx,y+28,"Fri May 29 2026",DIM,PANEL_BG);
}

/* System stats */
static void draw_stats(int x,int y){
    rect(x,y,PANEL_W-8,60,0x0D1117);
    outline(x,y,PANEL_W-8,60,BORDER);
    text(x+6,y+4,"System",DIM,0x0D1117);
    /* CPU bar */
    text(x+6,y+20,"CPU",DIM,0x0D1117);
    rect(x+36,y+22,150,8,0x21262D);
    rect(x+36,y+22,12,8,GREEN); /* ~8% */
    text(x+190,y+20,"8%",DIM,0x0D1117);
    /* MEM bar */
    text(x+6,y+36,"MEM",DIM,0x0D1117);
    rect(x+36,y+38,150,8,0x21262D);
    rect(x+36,y+38,75,8,ACCENT); /* ~50% */
    text(x+190,y+36,"50%",DIM,0x0D1117);
}

/* ── terminal window ─────────────────────────────────────────── */
#define TW_X 130
#define TW_Y 60
#define TW_W 600
#define TW_H 500
#define TW_PAD 8

static char tlines[32][128];
static int  trow=0;
static char tinput[128];
static int  tinput_len=0;
static int  term_visible=1;
static int  cursor_blink=0;

static void tprint(const char*s){
    if(trow>=32){
        for(int i=0;i<31;i++){
            int j=0; while(tlines[i+1][j]){tlines[i][j]=tlines[i+1][j];j;}
            tlines[i][j]=0;
        }
        trow=31;
    }
    int j=0; while(*s&&j<127) tlines[trow][j++]=*s++;
    tlines[trow][j]=0; trow++;
}

static void tcmd(const char*cmd){
    char echo[134]; echo[0]='$'; echo[1]=' ';
    int i=0; while(cmd[i]&&i<126){echo[i+2]=cmd[i];i++;} echo[i+2]=0;
    tprint(echo);
    /* Match commands */
    const char*help="help",*clr="clear",*abt="about",*sd="shutdown",*ls="ls";
    int mh=1,mc=1,ma=1,ms=1,ml=1;
    for(int k=0;help[k]||cmd[k];k++) if(help[k]!=cmd[k]){mh=0;break;}
    for(int k=0;clr[k]||cmd[k];k++) if(clr[k]!=cmd[k]){mc=0;break;}
    for(int k=0;abt[k]||cmd[k];k++) if(abt[k]!=cmd[k]){ma=0;break;}
    for(int k=0;sd[k]||cmd[k];k++) if(sd[k]!=cmd[k]){ms=0;break;}
    for(int k=0;ls[k]||cmd[k];k++) if(ls[k]!=cmd[k]){ml=0;break;}
    if(mh){ tprint("Commands: help clear about ls shutdown"); }
    else if(mc){ trow=0; for(int r=0;r<32;r++) tlines[r][0]=0; }
    else if(ma){ tprint("YouOS v0.1 - Built from scratch in C"); tprint("x86_64 | FAT16 | ELF | Framebuffer DE"); }
    else if(ml){ tprint("hello  cat  shell  fbtest  desktop"); }
    else if(ms){ tprint("Shutting down..."); flush(); sys_shutdown(); }
    else { char m[64]; m[0]='?'; m[1]=' ';
           int k=0; while(cmd[k]&&k<58){m[k+2]=cmd[k];k++;} m[k+2]=0;
           tprint(m); }
}

static void draw_terminal(void){
    if(!term_visible) return;
    /* Shadow */
    rect(TW_X+6,TW_Y+6,TW_W,TW_H,0x000000);
    /* Body */
    rect(TW_X,TW_Y,TW_W,TW_H,0x0D1117);
    /* Title bar */
    rect(TW_X,TW_Y,TW_W,28,0x161B22);
    hline(TW_X,TW_Y+28,TW_W,BORDER);
    /* Traffic lights */
    rect(TW_X+10,TW_Y+8,12,12,0xED4245);
    rect(TW_X+26,TW_Y+8,12,12,0xFAA61A);
    rect(TW_X+42,TW_Y+8,12,12,0x57F287);
    text(TW_X+TW_W/2-52,TW_Y+8,"YouOS Terminal",DIM,0x161B22);
    /* Border */
    outline(TW_X,TW_Y,TW_W,TW_H,BORDER);
    /* Lines */
    int cx=TW_X+TW_PAD, cy=TW_Y+32+TW_PAD;
    int max_rows=(TW_H-60)/16;
    int start=trow>max_rows?trow-max_rows:0;
    for(int i=start;i<trow;i++){
        u32 fg=TEXT;
        if(tlines[i][0]=='$') fg=GREEN;
        else if(tlines[i][0]=='?') fg=RED;
        text(cx,cy+(i-start)*16,tlines[i],fg,0x0D1117);
    }
    /* Input line */
    int iy=TW_Y+TW_H-24;
    hline(TW_X,iy-2,TW_W,BORDER);
    rect(TW_X,iy,TW_W,22,0x0A0D10);
    text(TW_X+TW_PAD,iy+3,"$ ",GREEN,0x0A0D10);
    text(TW_X+TW_PAD+16,iy+3,tinput,TEXT,0x0A0D10);
    /* Cursor */
    if(cursor_blink<50)
        rect(TW_X+TW_PAD+16+tinput_len*8,iy+2,8,14,ACCENT);
}

/* ── main ────────────────────────────────────────────────────── */
int main(void){
    u64 info[5];
    if(sys_fbinfo(info)!=0) return 1;
    FB_W=info[1]; FB_H=info[2];

    tprint("YouOS Desktop v0.1");
    tprint("Type 'help' for commands");

    u64 last_ticks=0;

    while(1){
        u64 ticks=sys_ticks();
        u64 secs=ticks/100;

        /* Non-blocking keyboard */
        s64 ch=sys_keypoll();
        if(ch>0){
            char c=(char)ch;
            if(c=='\n'||c=='\r'){
                tinput[tinput_len]=0;
                if(tinput_len>0) tcmd(tinput);
                tinput_len=0; tinput[0]=0;
            } else if((c=='\b'||c==127)&&tinput_len>0){
                tinput[--tinput_len]=0;
            } else if(c>=32&&c<127&&tinput_len<120){
                tinput[tinput_len++]=c; tinput[tinput_len]=0;
            }
        }

        /* Cursor blink */
        cursor_blink=(cursor_blink+1)%100;

        /* Only redraw every ~10ms to avoid flickering */
        if(ticks-last_ticks<1 && ch<=0 && cursor_blink!=0 && cursor_blink!=50)
            continue;
        last_ticks=ticks;

        /* Draw */
        wallpaper();
        draw_icons();
        draw_panel_bg();

        /* Panel content */
        int px2=PANEL_X+4;
        draw_analog_clock(PANEL_X+PANEL_W/2, 95, 80, secs);
        draw_digital_clock(px2, 182, secs);
        draw_calendar(px2, 220, 5, 2026, 29);
        draw_stats(px2, 392);

        /* Terminal window */
        draw_terminal();

        /* Taskbar on top */
        draw_taskbar();
        draw_menu();

        flush();

        sys_yield();
    }
    return 0;
}
