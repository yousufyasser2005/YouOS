#include "../lib/syscall.h"

typedef unsigned int   u32;
typedef unsigned long  u64;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef signed long    s64;

/* ── framebuffer ─────────────────────────────────────────────── */
static u64 FB_W, FB_H;
static u32 buf[1024 * 768];
static void px(int x,int y,u32 c){if((u64)x<FB_W&&(u64)y<FB_H)buf[y*1024+x]=c;}
static void rect(int x,int y,int w,int h,u32 c){for(int r=y;r<y+h;r++)for(int col=x;col<x+w;col++)px(col,r,c);}
static void outline(int x,int y,int w,int h,u32 c){for(int i=x;i<x+w;i++){px(i,y,c);px(i,y+h-1,c);}for(int i=y;i<y+h;i++){px(x,i,c);px(x+w-1,i,c);}}
static void hline(int x,int y,int w,u32 c){for(int i=0;i<w;i++)px(x+i,y,c);}
static void vline(int x,int y,int h,u32 c){for(int i=0;i<h;i++)px(x,y+i,c);}
static void flush(void){sys_fbwrite(0,0,1024,768,buf);}

/* ── font ────────────────────────────────────────────────────── */
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
    int idx=(u8)c-32;if(idx<0||idx>=96)idx=0;
    for(int r=0;r<16;r++){u8 b=font[idx][r];for(int col=0;col<8;col++)px(x+col,y+r,(b&(0x80>>col))?fg:bg);}
}
static void text(int x,int y,const char*s,u32 fg,u32 bg){while(*s){glyph(x,y,*s++,fg,bg);x+=8;}}
static void text_center(int cx,int y,const char*s,u32 fg,u32 bg){int l=0;const char*p=s;while(*p++)l++;text(cx-l*4,y,s,fg,bg);}
static int slen(const char*s){int n=0;while(s[n])n++;return n;}

/* ── math ────────────────────────────────────────────────────── */
static s64 isin(int deg){
    static const short t[]={0,17,34,52,69,87,104,121,139,156,173,190,207,224,241,258,275,292,309,325,342,358,374,390,406,422,438,453,469,484,499,515,529,544,559,573,587,601,615,629,642,656,669,681,694,707,719,731,743,754,766,777,788,798,809,819,829,838,848,857,866,874,882,891,898,906,913,920,927,933,939,945,951,956,961,965,970,974,978,981,984,987,990,992,994,996,997,998,999,999,1000,999,999,998,997,996,994,992,990,987,984,981,978,974,970,965,961,956,951,945,939,933,927,920,913,906,898,891,882,874,866,857,848,838,829,819,809,798,788,777,766,754,743,731,719,707,694,681,669,656,642,629,615,601,587,573,559,544,529,515,499,484,469,453,438,422,406,390,374,358,342,325,309,292,275,258,241,224,207,190,173,156,139,121,104,87,69,52,34,17};
    deg=((deg%360)+360)%360;if(deg<180)return t[deg];return -t[deg-180];
}
static s64 icos(int deg){return isin(deg+90);}
static void line_aa(int x0,int y0,int x1,int y1,u32 c){
    int dx=x1-x0,dy=y1-y0,steps=dx<0?-dx:dx;
    if((dy<0?-dy:dy)>steps)steps=(dy<0?-dy:dy);
    if(!steps){px(x0,y0,c);return;}
    for(int i=0;i<=steps;i++)px(x0+dx*i/steps,y0+dy*i/steps,c);
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
#define ORANGE   0xF78166
#define WHITE    0xFFFFFF
#define TBAR_H      40
#define PANEL_W     240
#define PANEL_X     (1024-PANEL_W)
#define TITLEBAR_H  28

static int in_box(int px2,int py,int x,int y,int w,int h){return px2>=x&&px2<x+w&&py>=y&&py<y+h;}

/* ═══════════════════════════════════════════════════════════════
 * WINDOW MANAGER
 * ═══════════════════════════════════════════════════════════════ */
#define MAX_WINDOWS  12
#define WIN_TERMINAL  0
#define WIN_ABOUT     1
#define WIN_FILES     2
#define WIN_TEXTVIEW  3   /* 3,4,5,... for multiple text viewers */

typedef struct {
    int  id;
    int  x,y,w,h;
    int  visible,minimized;
    int  z;
    char title[40];
    u32  accent;
} Win;

static Win  wins[MAX_WINDOWS];
static int  win_count=0;
static int  focused=-1;
static int  drag_win=-1,drag_ox=0,drag_oy=0;
static int  mouse_x=512,mouse_y=384,mouse_btn=0,prev_btn=0;

static int wm_new(int id,int x,int y,int w,int h,const char*title,u32 accent){
    if(win_count>=MAX_WINDOWS)return -1;
    int i=win_count++;
    wins[i].id=id;wins[i].x=x;wins[i].y=y;wins[i].w=w;wins[i].h=h;
    wins[i].visible=1;wins[i].minimized=0;wins[i].z=i;wins[i].accent=accent;
    int k=0;while(title[k]&&k<39){wins[i].title[k]=title[k];k++;}wins[i].title[k]=0;
    focused=i;return i;
}
static void wm_focus(int i){
    if(i<0||i>=win_count)return;
    int tz=wins[i].z;
    for(int j=0;j<win_count;j++)if(j!=i&&wins[j].z>tz)wins[j].z--;
    wins[i].z=win_count-1;focused=i;
}
static int wm_hit(int mx,int my){
    int best=-1,bestz=-1;
    for(int i=0;i<win_count;i++){
        if(!wins[i].visible)continue;
        int wh=wins[i].minimized?TITLEBAR_H:wins[i].h;
        if(in_box(mx,my,wins[i].x,wins[i].y,wins[i].w,wh)&&wins[i].z>bestz){bestz=wins[i].z;best=i;}
    }
    return best;
}
static int z_order[MAX_WINDOWS];
static void wm_sort(void){
    for(int i=0;i<win_count;i++)z_order[i]=i;
    for(int i=1;i<win_count;i++){int key=z_order[i],j=i-1;while(j>=0&&wins[z_order[j]].z>wins[key].z){z_order[j+1]=z_order[j];j--;}z_order[j+1]=key;}
}
static int wm_draw_frame(int i){
    Win*w=&wins[i];
    if(!w->visible)return 0;
    int foc=(i==focused);
    if(!w->minimized){rect(w->x+4,w->y+4,w->w,w->h,0x000000);rect(w->x,w->y,w->w,w->h,0x0D1117);}
    u32 tbar_bg=foc?0x1C2128:0x13161B;
    u32 bord=foc?w->accent:BORDER;
    rect(w->x,w->y,w->w,TITLEBAR_H,tbar_bg);
    hline(w->x,w->y+TITLEBAR_H,w->w,bord);
    outline(w->x,w->y,w->w,w->minimized?TITLEBAR_H:w->h,bord);
    if(foc)hline(w->x,w->y,w->w,w->accent);
    int hcl=in_box(mouse_x,mouse_y,w->x+8,w->y+7,14,14);
    int hmn=in_box(mouse_x,mouse_y,w->x+24,w->y+7,14,14);
    int hmx=in_box(mouse_x,mouse_y,w->x+40,w->y+7,14,14);
    rect(w->x+8, w->y+7,14,14,hcl?0xFF5F57:0xED4245);
    rect(w->x+24,w->y+7,14,14,hmn?0xFFBD2E:0xFAA61A);
    rect(w->x+40,w->y+7,14,14,hmx?0x28C840:0x57F287);
    text_center(w->x+w->w/2,w->y+6,w->title,foc?TEXT:DIM,tbar_bg);
    return !w->minimized;
}

/* ═══════════════════════════════════════════════════════════════
 * TERMINAL
 * ═══════════════════════════════════════════════════════════════ */
static char tlines[32][128];
static int  trow=0,tinput_len=0,cursor_blink=0;
static char tinput[128];

static void tprint(const char*s){
    if(trow>=32){for(int i=0;i<31;i++){int j=0;while(tlines[i+1][j]){tlines[i][j]=tlines[i+1][j];j++;}tlines[i][j]=0;}trow=31;}
    int j=0;while(*s&&j<127)tlines[trow][j++]=*s++;tlines[trow][j]=0;trow++;
}
static void tcmd(const char*cmd){
    char echo[134];echo[0]='$';echo[1]=' ';int i=0;while(cmd[i]&&i<126){echo[i+2]=cmd[i];i++;}echo[i+2]=0;tprint(echo);
    const char*help="help",*clr="clear",*abt="about",*sd="shutdown",*ls="ls";
    int mh=1,mc=1,ma=1,ms=1,ml=1;
    for(int k=0;help[k]||cmd[k];k++)if(help[k]!=cmd[k]){mh=0;break;}
    for(int k=0;clr[k]||cmd[k];k++) if(clr[k]!=cmd[k]) {mc=0;break;}
    for(int k=0;abt[k]||cmd[k];k++) if(abt[k]!=cmd[k]) {ma=0;break;}
    for(int k=0;sd[k]||cmd[k];k++)  if(sd[k]!=cmd[k])  {ms=0;break;}
    for(int k=0;ls[k]||cmd[k];k++)  if(ls[k]!=cmd[k])  {ml=0;break;}
    if(mh)tprint("Commands: help clear about ls shutdown");
    else if(mc){trow=0;for(int r=0;r<32;r++)tlines[r][0]=0;}
    else if(ma){tprint("YouOS v0.2 - WM + File Manager");tprint("x86_64 | FAT16 | ELF");}
    else if(ml)tprint("hello  cat  shell  fbtest  desktop");
    else if(ms){tprint("Shutting down...");flush();sys_shutdown();}
    else{char m[64];m[0]='?';m[1]=' ';int k=0;while(cmd[k]&&k<58){m[k+2]=cmd[k];k++;}m[k+2]=0;tprint(m);}
}
static void draw_terminal_content(int i){
    Win*w=&wins[i];
    int cx=w->x+8,cy=w->y+TITLEBAR_H+8;
    int max_rows=(w->h-TITLEBAR_H-32)/16;
    int start=trow>max_rows?trow-max_rows:0;
    for(int r=start;r<trow;r++){
        u32 fg=TEXT;if(tlines[r][0]=='$')fg=GREEN;else if(tlines[r][0]=='?')fg=RED;
        text(cx,cy+(r-start)*16,tlines[r],fg,0x0D1117);
    }
    int iy=w->y+w->h-24;
    hline(w->x,iy-2,w->w,BORDER);rect(w->x,iy,w->w,22,0x0A0D10);
    text(w->x+8,iy+3,"$ ",GREEN,0x0A0D10);text(w->x+24,iy+3,tinput,TEXT,0x0A0D10);
    if(cursor_blink<50)rect(w->x+24+tinput_len*8,iy+2,8,14,ACCENT);
}

/* ═══════════════════════════════════════════════════════════════
 * FILE MANAGER
 * ═══════════════════════════════════════════════════════════════ */

/* dirent layout must match fat16_entry_t: name[32] + u32 size + u8 is_dir */
typedef struct { char name[32]; unsigned int size; unsigned char is_dir; } Dirent;
#define MAX_FILES 64
static Dirent fm_entries[MAX_FILES];
static int    fm_count=0;
static int    fm_scroll=0;
static int    fm_hovered=-1;
static int    fm_loaded=0;

static void fm_load(void){
    fm_count=(int)sys_readdir(fm_entries,MAX_FILES);
    fm_scroll=0; fm_loaded=1;
}

/* format size: "1.2 KB" etc */
static void fmt_size(unsigned int sz, char*out){
    if(sz==0){out[0]='d';out[1]='i';out[2]='r';out[3]=0;return;}
    if(sz<1024){
        out[0]='0'+(sz/100)%10;out[1]='0'+(sz/10)%10;out[2]='0'+sz%10;out[3]=' ';out[4]='B';out[5]=0;
        /* trim leading zeros */
        int s=0;while(out[s]=='0'&&out[s+1]!=' ')s++;
        int d=0;while(out[s+d]){{out[d]=out[s+d];}d++;}out[d]=0;
        return;
    }
    unsigned int kb=sz/1024,fr=(sz%1024)*10/1024;
    int i=0;
    if(kb>=100){out[i++]='0'+kb/100;}
    if(kb>=10) {out[i++]='0'+(kb/10)%10;}
    out[i++]='0'+kb%10;out[i++]='.';out[i++]='0'+fr;
    out[i++]=' ';out[i++]='K';out[i++]='B';out[i]=0;
}

static void draw_files_content(int wi){
    Win*w=&wins[wi];
    if(!fm_loaded) fm_load();

    int x=w->x,y=w->y+TITLEBAR_H;
    int cw=w->w, ch=w->h-TITLEBAR_H;

    /* toolbar */
    rect(x,y,cw,28,0x161B22);
    hline(x,y+28,cw,BORDER);
    text(x+8,y+6,"/disk",ACCENT,0x161B22);
    /* refresh button */
    int rhov=in_box(mouse_x,mouse_y,x+cw-60,y+4,52,20);
    rect(x+cw-60,y+4,52,20,rhov?0x21262D:0x161B22);
    outline(x+cw-60,y+4,52,20,BORDER);
    text(x+cw-56,y+6,"Reload",rhov?TEXT:DIM,rhov?0x21262D:0x161B22);

    /* column headers */
    int hy=y+32;
    rect(x,hy,cw,18,0x13161B);
    hline(x,hy+18,cw,BORDER);
    text(x+28,hy+1,"Name",DIM,0x13161B);
    text(x+cw-90,hy+1,"Size",DIM,0x13161B);
    vline(x+cw-100,hy,18,BORDER);

    /* file rows */
    int row_y=hy+20;
    int row_h=22;
    int max_vis=(ch-72)/row_h;
    int start=fm_scroll;
    int end=start+max_vis;
    if(end>fm_count)end=fm_count;

    for(int i=start;i<end;i++){
        int ry=row_y+(i-start)*row_h;
        int hov=(i==fm_hovered);
        u32 rbg=hov?0x21262D:0x0D1117;
        rect(x,ry,cw,row_h,rbg);

        /* icon */
        u32 icol=fm_entries[i].is_dir?YELLOW:ACCENT;
        rect(x+8,ry+5,12,12,icol);
        /* darken icon */
        for(int r2=ry+6;r2<ry+16;r2++)for(int c2=x+9;c2<x+19;c2++){
            u32 col=buf[r2*1024+c2];u32 rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;
            buf[r2*1024+c2]=((rr/2)<<16)|((gg/2)<<8)|(bb/2);
        }
        /* name */
        text(x+26,ry+3,fm_entries[i].name,fm_entries[i].is_dir?YELLOW:TEXT,rbg);
        /* size */
        char szstr[16];fmt_size(fm_entries[i].size,szstr);
        int sl=slen(szstr);
        text(x+cw-96+(6-sl)*8,ry+3,szstr,DIM,rbg);

        hline(x,ry+row_h-1,cw,0x161B22);
    }

    /* scroll arrows if needed */
    if(fm_count>max_vis){
        rect(x+cw-16,y+50,14,14,fm_scroll>0?0x21262D:0x13161B);
        text(x+cw-14,y+51,fm_scroll>0?"^":"^",fm_scroll>0?TEXT:BORDER,fm_scroll>0?0x21262D:0x13161B);
        int can_dn=fm_scroll+max_vis<fm_count;
        rect(x+cw-16,y+66,14,14,can_dn?0x21262D:0x13161B);
        text(x+cw-14,y+67,"v",can_dn?TEXT:BORDER,can_dn?0x21262D:0x13161B);
    }

    /* status bar */
    int sb=w->y+w->h-20;
    rect(x,sb,cw,20,0x161B22);
    hline(x,sb,cw,BORDER);
    char stat[48];
    stat[0]='0'+fm_count/10;stat[1]='0'+fm_count%10;
    int si=fm_count>=10?2:1;if(fm_count<10)stat[0]='0'+fm_count,si=1;
    const char*suf=" items on /disk";int sf=0;while(suf[sf])stat[si++]=suf[sf++];stat[si]=0;
    text(x+8,sb+2,stat,DIM,0x161B22);
}

/* ═══════════════════════════════════════════════════════════════
 * TEXT VIEWER
 * ═══════════════════════════════════════════════════════════════ */
#define MAX_VIEWERS 4
typedef struct {
    char  filename[32];
    char  content[4096];
    int   content_len;
    int   scroll;        /* line scroll offset */
    int   win_idx;       /* index into wins[] */
} TextView;
static TextView viewers[MAX_VIEWERS];
static int      viewer_count=0;

static void tv_open(const char*name){
    /* don't open duplicates */
    for(int i=0;i<viewer_count;i++){
        int match=1;for(int k=0;name[k]||viewers[i].filename[k];k++)if(name[k]!=viewers[i].filename[k]){match=0;break;}
        if(match){wm_focus(viewers[i].win_idx);return;}
    }
    if(viewer_count>=MAX_VIEWERS)return;

    /* read file */
    char path[48];
    path[0]='/';path[1]='d';path[2]='i';path[3]='s';path[4]='k';path[5]='/';
    int k=0;while(name[k]&&k<40){path[6+k]=name[k];k++;}path[6+k]=0;

    int fd=sys_open(path,0);
    if(fd<0)return;

    TextView*tv=&viewers[viewer_count];
    int n=0;
    s64 r;
    while(n<4000&&(r=sys_fread(fd,tv->content+n,64))>0)n+=(int)r;
    tv->content[n]=0;tv->content_len=n;
    sys_close(fd);

    k=0;while(name[k]&&k<31){tv->filename[k]=name[k];k++;}tv->filename[k]=0;
    tv->scroll=0;

    /* build window title */
    char title[40];
    title[0]='"';k=0;while(name[k]&&k<36){title[1+k]=name[k];k++;}title[1+k]='"';title[2+k]=0;

    /* offset each viewer so they don't stack exactly */
    int ox=viewer_count*24, oy=viewer_count*24;
    int wi=wm_new(WIN_TEXTVIEW+viewer_count,160+ox,80+oy,520,380,title,ORANGE);
    tv->win_idx=wi;
    viewer_count++;
}

static void draw_textview_content(int wi){
    Win*w=&wins[wi];
    /* find which viewer this is */
    TextView*tv=0;
    for(int i=0;i<viewer_count;i++)if(viewers[i].win_idx==wi){tv=&viewers[i];break;}
    if(!tv)return;

    int x=w->x+8,y=w->y+TITLEBAR_H+8;
    int max_cols=(w->w-16)/8;
    int max_rows=(w->h-TITLEBAR_H-24)/16;
    int line=0,col=0,drawn=0;
    int ci=0;
    while(tv->content[ci]){
        char c=tv->content[ci++];
        if(c=='\r'){continue;}
        if(c=='\n'){line++;col=0;continue;}
        if(line>=tv->scroll&&drawn<max_rows*max_cols){
            int r=line-tv->scroll;
            if(r>=0&&r<max_rows&&col<max_cols){
                glyph(x+col*8,y+r*16,c,TEXT,0x0D1117);
            }
        }
        col++;
        if(col>=max_cols){col=0;line++;}
    }
    /* scroll hint */
    if(tv->scroll>0){
        text(w->x+w->w/2-20,w->y+TITLEBAR_H+2,"[scroll]",DIM,0x0D1117);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * ABOUT
 * ═══════════════════════════════════════════════════════════════ */
static void draw_about_content(int i){
    Win*w=&wins[i];int cx=w->x+w->w/2,y=w->y+TITLEBAR_H+20;
    text_center(cx,y,"YouOS",ACCENT,0x0D1117);y+=24;
    text_center(cx,y,"Version 0.2.0",TEXT,0x0D1117);y+=20;
    hline(w->x+20,y,w->w-40,BORDER);y+=12;
    text_center(cx,y,"Architecture: x86_64",DIM,0x0D1117);y+=18;
    text_center(cx,y,"Bootloader:   GRUB2 + Multiboot2",DIM,0x0D1117);y+=18;
    text_center(cx,y,"Filesystem:   FAT16 + initrd",DIM,0x0D1117);y+=18;
    text_center(cx,y,"Display:      1024x768x32bpp",DIM,0x0D1117);y+=18;
    text_center(cx,y,"Syscalls:     18",DIM,0x0D1117);y+=18;
    hline(w->x+20,y,w->w-40,BORDER);y+=12;
    text_center(cx,y,"Built from scratch in C + NASM",GREEN,0x0D1117);
}

/* ═══════════════════════════════════════════════════════════════
 * WALLPAPER + PANEL
 * ═══════════════════════════════════════════════════════════════ */
static void wallpaper(void){
    for(int y=0;y<768-TBAR_H;y++){
        u32 r=0x0D,g=0x11+(y*8)/768,b=0x17+(y*20)/768;
        u32 c=(r<<16)|(g<<8)|b;
        for(int x=0;x<PANEL_X;x++)px(x,y,c);
    }
    for(int y=0;y<768-TBAR_H;y+=80)for(int x=0;x<PANEL_X;x++)px(x,y,0x161B22);
    for(int x=0;x<PANEL_X;x+=80)for(int y=0;y<768-TBAR_H;y++)px(x,y,0x161B22);
}
static void draw_panel_bg(void){rect(PANEL_X,0,PANEL_W,768,PANEL_BG);vline(PANEL_X,0,768,BORDER);}
static void draw_analog_clock(int cx,int cy,int r,u64 secs){
    int hh=(secs/3600)%12,mm=(secs/60)%60,ss=secs%60;
    for(int deg=0;deg<360;deg+=2){px(cx+(int)(icos(deg)*r/1000),cy-(int)(isin(deg)*r/1000),BORDER);px(cx+(int)(icos(deg)*(r-1)/1000),cy-(int)(isin(deg)*(r-1)/1000),BORDER);}
    for(int dy=-r+2;dy<r-1;dy++)for(int dx=-r+2;dx<r-1;dx++)if(dx*dx+dy*dy<(r-2)*(r-2))px(cx+dx,cy+dy,0x161B22);
    for(int i=0;i<12;i++){int deg=i*30;line_aa(cx+(int)(icos(deg)*(r-4)/1000),cy-(int)(isin(deg)*(r-4)/1000),cx+(int)(icos(deg)*(r-10)/1000),cy-(int)(isin(deg)*(r-10)/1000),(i==0||i==3||i==6||i==9)?WHITE:DIM);}
    int hdeg=(hh*30+mm/2)-90;
    line_aa(cx,cy,cx+(int)(icos(hdeg)*(r-28)/1000),cy-(int)(isin(hdeg)*(r-28)/1000),WHITE);
    line_aa(cx,cy,cx+(int)(icos(hdeg+1)*(r-28)/1000),cy-(int)(isin(hdeg+1)*(r-28)/1000),WHITE);
    int mdeg=mm*6-90;
    line_aa(cx,cy,cx+(int)(icos(mdeg)*(r-18)/1000),cy-(int)(isin(mdeg)*(r-18)/1000),ACCENT);
    line_aa(cx,cy,cx+(int)(icos(mdeg+1)*(r-18)/1000),cy-(int)(isin(mdeg+1)*(r-18)/1000),ACCENT);
    line_aa(cx,cy,cx+(int)(icos(ss*6-90)*(r-14)/1000),cy-(int)(isin(ss*6-90)*(r-14)/1000),RED);
    rect(cx-3,cy-3,6,6,WHITE);
}
static const char*months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
static const int days_in_month[]={31,28,31,30,31,30,31,31,30,31,30,31};
static const char*day_names[]={"Mo","Tu","We","Th","Fr","Sa","Su"};
static void draw_calendar(int x,int y,int month,int year,int today){
    rect(x,y,PANEL_W-8,160,0x0D1117);outline(x,y,PANEL_W-8,160,BORDER);
    char hdr[16];const char*mn=months[month-1];int k=0;
    hdr[k++]=mn[0];hdr[k++]=mn[1];hdr[k++]=mn[2];hdr[k++]=' ';
    hdr[k++]='0'+year/1000;hdr[k++]='0'+(year/100)%10;hdr[k++]='0'+(year/10)%10;hdr[k++]='0'+year%10;hdr[k]=0;
    text_center(x+PANEL_W/2-4,y+4,hdr,TEXT,0x0D1117);hline(x,y+20,PANEL_W-8,BORDER);
    for(int d=0;d<7;d++)text(x+4+d*30,y+24,day_names[d],DIM,0x0D1117);
    hline(x,y+38,PANEL_W-8,0x21262D);
    int col=4,row=0,days=days_in_month[month-1];
    for(int d=1;d<=days;d++){
        int dx=x+4+col*30,dy=y+42+row*18;u32 fg=TEXT,bg2=0x0D1117;
        if(d==today){rect(dx-2,dy-1,18,16,ACCENT);fg=0x0D1117;bg2=ACCENT;}else if(col>=5)fg=RED;
        char ds[3];ds[0]=(d<10)?' ':'0'+d/10;ds[1]='0'+d%10;ds[2]=0;
        text(dx,dy,ds,fg,bg2);if(++col==7){col=0;row++;}
    }
}
static void draw_digital_clock(int x,int y,u64 secs){
    u64 hh=(secs/3600)%24,mm=(secs/60)%60,ss=secs%60;
    char t[9];t[0]='0'+hh/10;t[1]='0'+hh%10;t[2]=':';t[3]='0'+mm/10;t[4]='0'+mm%10;t[5]=':';t[6]='0'+ss/10;t[7]='0'+ss%10;t[8]=0;
    int cx=x+PANEL_W/2-4;
    for(int i=0;t[i];i++){int gx=cx-36+i*14;glyph(gx,y,t[i],ACCENT,PANEL_BG);glyph(gx,y+8,t[i],ACCENT,PANEL_BG);}
    text_center(cx,y+28,"Fri May 29 2026",DIM,PANEL_BG);
}
static void draw_stats(int x,int y){
    rect(x,y,PANEL_W-8,60,0x0D1117);outline(x,y,PANEL_W-8,60,BORDER);
    text(x+6,y+4,"System",DIM,0x0D1117);
    text(x+6,y+20,"CPU",DIM,0x0D1117);rect(x+36,y+22,150,8,0x21262D);rect(x+36,y+22,12,8,GREEN);text(x+190,y+20,"8%",DIM,0x0D1117);
    text(x+6,y+36,"MEM",DIM,0x0D1117);rect(x+36,y+38,150,8,0x21262D);rect(x+36,y+38,75,8,ACCENT);text(x+190,y+36,"50%",DIM,0x0D1117);
}

/* ── icons ───────────────────────────────────────────────────── */
typedef struct{int x,y;const char*name;u32 color;}Icon;
static Icon icons[]={{60,80,"Terminal",ACCENT},{60,180,"Files",GREEN},{60,280,"About",PURPLE}};
#define N_ICONS 3
static int icon_hovered=-1;
static void draw_icons(void){
    for(int i=0;i<N_ICONS;i++){
        Icon*ic=&icons[i];u32 bg=(i==icon_hovered)?0x21262D:BG;
        rect(ic->x-4,ic->y-4,72,72,bg);outline(ic->x-4,ic->y-4,72,72,i==icon_hovered?ACCENT:BORDER);
        rect(ic->x+10,ic->y+10,52,40,ic->color);
        for(int r=ic->y+12;r<ic->y+48;r++)for(int c2=ic->x+12;c2<ic->x+60;c2++){u32 col=buf[r*1024+c2];u32 rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;buf[r*1024+c2]=((rr/2)<<16)|((gg/2)<<8)|(bb/2);}
        int llen=0;const char*p=ic->name;while(*p++)llen++;
        text(ic->x+(64-llen*8)/2,ic->y+66,ic->name,TEXT,BG);
    }
}

/* ── menu ────────────────────────────────────────────────────── */
static int menu_open=0,menu_sel=-1;
static const char*menu_items[]={"Terminal","Files","About","Settings","Shutdown"};
#define N_MENU 5
static void draw_menu(void){
    if(!menu_open)return;
    int mx=4,my=768-TBAR_H-N_MENU*32-8;
    rect(mx,my,220,N_MENU*32+8,PANEL_BG);outline(mx,my,220,N_MENU*32+8,BORDER);
    text(mx+8,my+4,"Applications",DIM,PANEL_BG);
    u32 dots[]={ACCENT,GREEN,PURPLE,YELLOW,RED};
    for(int i=0;i<N_MENU;i++){
        int iy=my+20+i*32;u32 bg=(i==menu_sel)?0x21262D:PANEL_BG;
        rect(mx+2,iy,216,28,bg);rect(mx+8,iy+8,12,12,dots[i]);text(mx+26,iy+6,menu_items[i],TEXT,bg);
    }
}

/* ── taskbar ─────────────────────────────────────────────────── */
static void draw_taskbar(u64 secs){
    int ty=768-TBAR_H;rect(0,ty,1024,TBAR_H,TASKBAR);hline(0,ty,1024,BORDER);
    int hs=in_box(mouse_x,mouse_y,4,ty+4,80,TBAR_H-8);
    u32 sbg=menu_open?ACCENT:(hs?0x2D333B:0x21262D);
    rect(4,ty+4,80,TBAR_H-8,sbg);outline(4,ty+4,80,TBAR_H-8,menu_open?0x79C0FF:BORDER);
    text(12,ty+12,"  YouOS",menu_open?0x0D1117:ACCENT,sbg);
    int bx=92;
    for(int i=0;i<win_count;i++){
        if(!wins[i].visible)continue;
        int foc=(i==focused);u32 bbg=foc?0x21262D:0x13161B;
        rect(bx,ty+4,112,TBAR_H-8,bbg);outline(bx,ty+4,112,TBAR_H-8,foc?wins[i].accent:BORDER);
        if(foc)hline(bx,ty+TBAR_H-2,112,wins[i].accent);
        /* truncate title to fit */
        char tshort[13];int k=0;while(wins[i].title[k]&&k<12){tshort[k]=wins[i].title[k];k++;}
        if(slen(wins[i].title)>12){tshort[11]='.';tshort[12]='.';k=13;}
        tshort[k]=0;
        text(bx+8,ty+12,tshort,foc?TEXT:DIM,bbg);
        bx+=120;
    }
    u64 hh=(secs/3600)%24,mm=(secs/60)%60,ss=secs%60;
    char clk[9];clk[0]='0'+hh/10;clk[1]='0'+hh%10;clk[2]=':';clk[3]='0'+mm/10;clk[4]='0'+mm%10;clk[5]=':';clk[6]='0'+ss/10;clk[7]='0'+ss%10;clk[8]=0;
    text(1024-PANEL_W-80,ty+12,clk,TEXT,TASKBAR);
}

/* ── cursor ──────────────────────────────────────────────────── */
static void draw_cursor(int mx,int my){
    static const u8 C[15][10]={{2,0,0,0,0,0,0,0,0,0},{2,2,0,0,0,0,0,0,0,0},{2,1,2,0,0,0,0,0,0,0},{2,1,1,2,0,0,0,0,0,0},{2,1,1,1,2,0,0,0,0,0},{2,1,1,1,1,2,0,0,0,0},{2,1,1,1,1,1,2,0,0,0},{2,1,1,1,1,1,1,2,0,0},{2,1,1,1,1,1,2,0,0,0},{2,1,1,2,1,1,1,2,0,0},{2,2,0,0,2,1,1,2,0,0},{0,0,0,0,2,1,1,2,0,0},{0,0,0,0,2,1,2,0,0,0},{0,0,0,0,2,2,0,0,0,0},{0,0,0,0,0,0,0,0,0,0}};
    for(int r=0;r<15;r++)for(int c=0;c<10;c++){u8 v=C[r][c];if(v==1)px(mx+c,my+r,0xFFFFFF);else if(v==2)px(mx+c,my+r,0x000000);}
}

/* ═══════════════════════════════════════════════════════════════
 * OPEN HELPERS
 * ═══════════════════════════════════════════════════════════════ */
static int find_win(int id){for(int i=0;i<win_count;i++)if(wins[i].id==id)return i;return -1;}
static void open_terminal(void){int i=find_win(WIN_TERMINAL);if(i>=0){wins[i].visible=1;wins[i].minimized=0;wm_focus(i);}else wm_new(WIN_TERMINAL,130,60,600,500,"Terminal",ACCENT);}
static void open_about(void){int i=find_win(WIN_ABOUT);if(i>=0){wins[i].visible=1;wins[i].minimized=0;wm_focus(i);}else wm_new(WIN_ABOUT,280,150,420,280,"About YouOS",PURPLE);}
static void open_files(void){int i=find_win(WIN_FILES);if(i>=0){wins[i].visible=1;wins[i].minimized=0;wm_focus(i);}else{wm_new(WIN_FILES,100,80,560,420,"File Manager",GREEN);fm_loaded=0;}}

/* ═══════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════ */
int main(void){
    u64 info[5];
    if(sys_fbinfo(info)!=0)return 1;
    FB_W=info[1];FB_H=info[2];
    open_terminal();
    tprint("YouOS Desktop v0.3");
    tprint("File manager ready — click Files icon");
    tprint("Type 'help' for commands");
    u64 last_ticks=0;

    while(1){
        u64 ticks=sys_ticks();
        u64 secs=ticks/100;

        /* mouse */
        unsigned long long mstate[3];
        sys_mouseread(mstate);
        mouse_x=(int)mstate[0];mouse_y=(int)mstate[1];mouse_btn=(int)mstate[2];
        int btn_down=(mouse_btn&1)&&!(prev_btn&1);
        int btn_up  =!(mouse_btn&1)&&(prev_btn&1);

        if(btn_up&&drag_win>=0)drag_win=-1;

        if(drag_win>=0){
            Win*w=&wins[drag_win];
            w->x=mouse_x-drag_ox;w->y=mouse_y-drag_oy;
            if(w->x<-(w->w-50))w->x=-(w->w-50);
            if(w->x>PANEL_X-50)w->x=PANEL_X-50;
            if(w->y<0)w->y=0;
            if(w->y>768-TBAR_H-TITLEBAR_H)w->y=768-TBAR_H-TITLEBAR_H;
        }

        if(btn_down&&drag_win<0){
            int hit=wm_hit(mouse_x,mouse_y);
            if(hit>=0&&hit!=focused)wm_focus(hit);

            if(hit>=0){
                Win*w=&wins[hit];
                /* close */
                if(in_box(mouse_x,mouse_y,w->x+8,w->y+7,14,14)){
                    w->visible=0;
                    /* if text viewer, free slot */
                    for(int i=0;i<viewer_count;i++)if(viewers[i].win_idx==hit){
                        for(int j=i;j<viewer_count-1;j++)viewers[j]=viewers[j+1];
                        viewer_count--;break;
                    }
                    focused=-1;int bz=-1;
                    for(int i=0;i<win_count;i++)if(wins[i].visible&&wins[i].z>bz){bz=wins[i].z;focused=i;}
                    goto click_done;
                }
                /* minimize */
                if(in_box(mouse_x,mouse_y,w->x+24,w->y+7,14,14)){w->minimized=!w->minimized;goto click_done;}
                /* maximize */
                if(in_box(mouse_x,mouse_y,w->x+40,w->y+7,14,14)){
                    if(w->w<700){w->x=20;w->y=20;w->w=750;w->h=700;}
                    else{w->x=100;w->y=60;w->w=560;w->h=420;}
                    goto click_done;
                }
                /* drag */
                if(in_box(mouse_x,mouse_y,w->x+60,w->y,w->w-60,TITLEBAR_H)){drag_win=hit;drag_ox=mouse_x-w->x;drag_oy=mouse_y-w->y;goto click_done;}

                /* file manager interactions */
                if(w->id==WIN_FILES&&!w->minimized){
                    int fy=w->y+TITLEBAR_H;
                    /* reload button */
                    if(in_box(mouse_x,mouse_y,w->x+w->w-60,fy+4,52,20)){fm_load();goto click_done;}
                    /* scroll up */
                    if(in_box(mouse_x,mouse_y,w->x+w->w-16,fy+50,14,14)&&fm_scroll>0){fm_scroll--;goto click_done;}
                    /* scroll down */
                    int max_vis=(w->h-TITLEBAR_H-72)/22;
                    if(in_box(mouse_x,mouse_y,w->x+w->w-16,fy+66,14,14)&&fm_scroll+max_vis<fm_count){fm_scroll++;goto click_done;}
                    /* file row click */
                    int row_y=fy+72;
                    int start=fm_scroll;
                    for(int fi=start;fi<fm_count;fi++){
                        int ry=row_y+(fi-start)*22;
                        if(in_box(mouse_x,mouse_y,w->x,ry,w->w,22)){
                            if(!fm_entries[fi].is_dir){
                                /* check if .txt */
                                char*n=fm_entries[fi].name;
                                int nl=slen(n);
                                if(nl>4&&n[nl-4]=='.'&&n[nl-3]=='t'&&n[nl-2]=='x'&&n[nl-1]=='t'){
                                    tv_open(n);
                                } else {
                                    tprint("Can only open .txt files");
                                    int ti=find_win(WIN_TERMINAL);
                                    if(ti>=0){wins[ti].visible=1;wins[ti].minimized=0;wm_focus(ti);}
                                }
                            }
                            goto click_done;
                        }
                    }
                }

                /* text viewer scroll via arrow keys handled below */
                goto click_done;
            }

            /* taskbar buttons */
            int ty2=768-TBAR_H,bx=92;
            for(int i=0;i<win_count;i++){
                if(!wins[i].visible)continue;
                if(in_box(mouse_x,mouse_y,bx,ty2+4,112,TBAR_H-8)){
                    if(i==focused)wins[i].minimized=!wins[i].minimized;
                    else{wins[i].minimized=0;wm_focus(i);}
                    goto click_done;
                }
                bx+=120;
            }
            /* start button */
            if(in_box(mouse_x,mouse_y,4,768-TBAR_H+4,80,TBAR_H-8)){menu_open=!menu_open;goto click_done;}
            /* menu */
            if(menu_open){
                int mx2=4,my2=768-TBAR_H-N_MENU*32-8;
                for(int i=0;i<N_MENU;i++){
                    int iy=my2+20+i*32;
                    if(in_box(mouse_x,mouse_y,mx2+2,iy,216,28)){
                        menu_open=0;
                        if(i==0)open_terminal();
                        else if(i==1)open_files();
                        else if(i==2)open_about();
                        else if(i==4){tprint("Shutting down...");flush();sys_shutdown();}
                        goto click_done;
                    }
                }
                menu_open=0;goto click_done;
            }
            /* desktop icons */
            for(int i=0;i<N_ICONS;i++){
                Icon*ic=&icons[i];
                if(in_box(mouse_x,mouse_y,ic->x-4,ic->y-4,72,72)){
                    if(i==0)open_terminal();else if(i==1)open_files();else if(i==2)open_about();
                    goto click_done;
                }
            }
        }
        click_done:;

        /* keyboard */
        s64 ch=sys_keypoll();
        if(ch>0&&focused>=0){
            /* terminal */
            if(wins[focused].id==WIN_TERMINAL){
                char c=(char)ch;
                if(c=='\n'||c=='\r'){tinput[tinput_len]=0;if(tinput_len>0)tcmd(tinput);tinput_len=0;tinput[0]=0;}
                else if((c=='\b'||c==127)&&tinput_len>0)tinput[--tinput_len]=0;
                else if(c>=32&&c<127&&tinput_len<120){tinput[tinput_len++]=c;tinput[tinput_len]=0;}
            }
            /* text viewer scroll */
            if(wins[focused].id>=WIN_TEXTVIEW){
                for(int i=0;i<viewer_count;i++){
                    if(viewers[i].win_idx==focused){
                        if((char)ch=='k'||ch==72)viewers[i].scroll=viewers[i].scroll>0?viewers[i].scroll-1:0;
                        if((char)ch=='j'||ch==80)viewers[i].scroll++;
                        break;
                    }
                }
            }
        }

        /* hover */
        icon_hovered=-1;
        for(int i=0;i<N_ICONS;i++){Icon*ic=&icons[i];if(in_box(mouse_x,mouse_y,ic->x-4,ic->y-4,72,72))icon_hovered=i;}
        menu_sel=-1;
        if(menu_open){int mx2=4,my2=768-TBAR_H-N_MENU*32-8;for(int i=0;i<N_MENU;i++){int iy=my2+20+i*32;if(in_box(mouse_x,mouse_y,mx2+2,iy,216,28))menu_sel=i;}}
        /* file manager hover */
        fm_hovered=-1;
        int fi2=find_win(WIN_FILES);
        if(fi2>=0&&wins[fi2].visible&&!wins[fi2].minimized){
            Win*wf=&wins[fi2];
            int row_y=wf->y+TITLEBAR_H+72,start=fm_scroll;
            for(int i=start;i<fm_count;i++){int ry=row_y+(i-start)*22;if(in_box(mouse_x,mouse_y,wf->x,ry,wf->w,22))fm_hovered=i;}
        }

        cursor_blink=(cursor_blink+1)%100;
        prev_btn=mouse_btn;
        if(ticks-last_ticks<1&&ch<=0&&cursor_blink!=0&&cursor_blink!=50)continue;
        last_ticks=ticks;

        /* draw */
        wallpaper();draw_icons();draw_panel_bg();
        int px2=PANEL_X+4;
        draw_analog_clock(PANEL_X+PANEL_W/2,95,80,secs);
        draw_digital_clock(px2,182,secs);
        draw_calendar(px2,220,5,2026,29);
        draw_stats(px2,392);
        wm_sort();
        for(int si=0;si<win_count;si++){
            int i=z_order[si];if(!wins[i].visible)continue;
            int db=wm_draw_frame(i);if(!db)continue;
            if(wins[i].id==WIN_TERMINAL)draw_terminal_content(i);
            else if(wins[i].id==WIN_ABOUT)draw_about_content(i);
            else if(wins[i].id==WIN_FILES)draw_files_content(i);
            else draw_textview_content(i);
        }
        draw_taskbar(secs);draw_menu();draw_cursor(mouse_x,mouse_y);
        flush();sys_yield();
    }
    return 0;
}
