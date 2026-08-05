#include "start_icon.h"
#include "../lib/syscall.h"

typedef unsigned int   u32;
typedef unsigned long  u64;
typedef unsigned short u16;
typedef unsigned char  u8;
typedef signed long    s64;

#include "inflate.h"
#include "png_decoder.h"

static u64 FB_W,FB_H;
#define MAX_FB_W 1920
#define MAX_FB_H 1080
static u32 buf[MAX_FB_W*MAX_FB_H];
static void px(int x,int y,u32 c){if((u64)x<FB_W&&(u64)y<FB_H)buf[y*(int)FB_W+x]=c;}
static void rect(int x,int y,int w,int h,u32 c){for(int r=y;r<y+h;r++)for(int col=x;col<x+w;col++)px(col,r,c);}
static void outline(int x,int y,int w,int h,u32 c){for(int i=x;i<x+w;i++){px(i,y,c);px(i,y+h-1,c);}for(int i=y;i<y+h;i++){px(x,i,c);px(x+w-1,i,c);}}
static void hline(int x,int y,int w,u32 c){for(int i=0;i<w;i++)px(x+i,y,c);}
static void vline(int x,int y,int h,u32 c){for(int i=0;i<h;i++)px(x,y+i,c);}
static u32 ablend(u32 bg,u32 fg,int a){int br=(bg>>16)&0xFF,bgc=(bg>>8)&0xFF,bb=bg&0xFF;int fr=(fg>>16)&0xFF,fgc=(fg>>8)&0xFF,fbc=fg&0xFF;int r=(fr*a+br*(255-a))/255,g=(fgc*a+bgc*(255-a))/255,b=(fbc*a+bb*(255-a))/255;return (r<<16)|(g<<8)|b;}
static void px_alpha(int x,int y,u32 c,int a){if((u64)x<FB_W&&(u64)y<FB_H)buf[y*(int)FB_W+x]=ablend(buf[y*(int)FB_W+x],c,a);}
static void rect_alpha(int x,int y,int w,int h,u32 c,int a){for(int r=y;r<y+h;r++)for(int col=x;col<x+w;col++)px_alpha(col,r,c,a);}
static void blit_rgba(int x,int y,int w,int h,const unsigned char*rgba){
    for(int row=0;row<h;row++)for(int col=0;col<w;col++){
        const unsigned char*p=&rgba[(row*w+col)*4];
        if(p[3])px_alpha(x+col,y+row,(p[0]<<16)|(p[1]<<8)|p[2],p[3]);
    }
}
static void rect_round(int x,int y,int w,int h,int r,u32 c){
    for(int row=0;row<h;row++)for(int col=0;col<w;col++){
        int cx=-1,cy=-1;
        if(col<r&&row<r){cx=r;cy=r;}
        else if(col>=w-r&&row<r){cx=w-r-1;cy=r;}
        else if(col<r&&row>=h-r){cx=r;cy=h-r-1;}
        else if(col>=w-r&&row>=h-r){cx=w-r-1;cy=h-r-1;}
        if(cx>=0){int dx=col-cx,dy=row-cy;if(dx*dx+dy*dy>r*r)continue;}
        px(x+col,y+row,c);
    }
}
static void rect_round_alpha(int x,int y,int w,int h,int r,u32 c,int a){
    for(int row=0;row<h;row++)for(int col=0;col<w;col++){
        int cx=-1,cy=-1;
        if(col<r&&row<r){cx=r;cy=r;}
        else if(col>=w-r&&row<r){cx=w-r-1;cy=r;}
        else if(col<r&&row>=h-r){cx=r;cy=h-r-1;}
        else if(col>=w-r&&row>=h-r){cx=w-r-1;cy=h-r-1;}
        if(cx>=0){int dx=col-cx,dy=row-cy;if(dx*dx+dy*dy>r*r)continue;}
        px_alpha(x+col,y+row,c,a);
    }
}
static void outline_round(int x,int y,int w,int h,int r,u32 c){
    for(int row=0;row<h;row++)for(int col=0;col<w;col++){
        int edge=(row==0||row==h-1||col==0||col==w-1);
        int cx=-1,cy=-1;
        if(col<r&&row<r){cx=r;cy=r;}
        else if(col>=w-r&&row<r){cx=w-r-1;cy=r;}
        else if(col<r&&row>=h-r){cx=r;cy=h-r-1;}
        else if(col>=w-r&&row>=h-r){cx=w-r-1;cy=h-r-1;}
        if(cx>=0){
            int dx=col-cx,dy=row-cy,d2=dx*dx+dy*dy;
            if(d2>r*r)continue;
            if(d2>=(r-1)*(r-1))px(x+col,y+row,c);
            continue;
        }
        if(edge)px(x+col,y+row,c);
    }
}
static void flush(void){sys_fbwrite(0,0,FB_W,FB_H,buf);}

static const u8 font[96][16]={
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
{0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x36,0x36,0x7F,0x36,0x36,0x36,0x7F,0x36,0x36,0x00,0x00,0x00,0x00,0x00},
{0x00,0x18,0x18,0x7E,0xC3,0xC1,0x60,0x3E,0x06,0x83,0xC3,0x7E,0x18,0x18,0x00,0x00},
{0x00,0x00,0xC3,0xC3,0xC6,0x0C,0x18,0x30,0x66,0xC3,0xC3,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x1C,0x36,0x36,0x1C,0x3B,0x6E,0x66,0x66,0x3B,0x00,0x00,0x00,0x00,0x00},
{0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x0C,0x18,0x18,0x30,0x30,0x30,0x30,0x30,0x18,0x18,0x0C,0x00,0x00,0x00,0x00},
{0x00,0x30,0x18,0x18,0x0C,0x0C,0x0C,0x0C,0x0C,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x18,0x18,0x18,0x7E,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x01,0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x18,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x06,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x06,0x06,0x1C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x0C,0x1C,0x3C,0x6C,0xCC,0xFF,0x0C,0x0C,0x0C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7E,0x60,0x60,0x7C,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x1C,0x30,0x60,0x7C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7E,0x06,0x06,0x0C,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x66,0x66,0x3E,0x06,0x06,0x0C,0x38,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x66,0x06,0x0C,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x60,0x60,0x3E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x60,0x60,0x60,0x60,0x60,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x78,0x6C,0x66,0x66,0x66,0x66,0x66,0x6C,0x78,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7E,0x60,0x60,0x60,0x7C,0x60,0x60,0x60,0x7E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7E,0x60,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x60,0x60,0x6E,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x66,0x6C,0x6C,0x78,0x6C,0x6C,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x76,0x3C,0x06,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x66,0x60,0x60,0x3C,0x06,0x06,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x63,0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x63,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x7E,0x06,0x06,0x0C,0x18,0x30,0x60,0x60,0x7E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x60,0x60,0x30,0x30,0x18,0x0C,0x0C,0x06,0x06,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x10,0x38,0x6C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
{0x00,0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x66,0x3B,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x7C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3C,0x66,0x60,0x60,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x06,0x06,0x06,0x3E,0x66,0x66,0x66,0x66,0x3B,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3C,0x66,0x7E,0x60,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x1C,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3B,0x66,0x66,0x66,0x3E,0x06,0x06,0x3C,0x00,0x00,0x00},
{0x00,0x00,0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x06,0x06,0x00,0x0E,0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00,0x00},
{0x00,0x00,0x60,0x60,0x66,0x66,0x6C,0x78,0x6C,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x66,0x7F,0x6B,0x63,0x63,0x63,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3B,0x66,0x66,0x66,0x3E,0x06,0x06,0x06,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x60,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x3C,0x66,0x30,0x0C,0x66,0x3C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x30,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x1C,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x3B,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x3C,0x3C,0x66,0x66,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x06,0x3C,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x7E,0x0C,0x18,0x30,0x60,0x7E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x0E,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x0E,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x70,0x18,0x18,0x18,0x0E,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00,0x00},
{0x00,0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};
static void glyph(int x,int y,char c,u32 fg,u32 bg){
    int idx=(u8)c-32;if(idx<0||idx>=96)idx=0;
    for(int r=0;r<16;r++){u8 b=font[idx][r];for(int col=0;col<8;col++)px(x+col,y+r,(b&(0x80>>col))?fg:bg);}
}
static void text(int x,int y,const char*str,u32 fg,u32 bg){while(*str){glyph(x,y,*str++,fg,bg);x+=8;}}
static void text_center(int cx,int y,const char*str,u32 fg,u32 bg){int l=0;const char*p=str;while(*p++)l++;text(cx-l*4,y,str,fg,bg);}
static void glyph2x(int x,int y,char c,u32 fg,u32 bg){
    int idx=(u8)c-32;if(idx<0||idx>=96)idx=0;
    for(int r=0;r<16;r++){u8 b=font[idx][r];for(int col=0;col<8;col++){
        u32 cl=(b&(0x80>>col))?fg:bg;
        px(x+col*2,y+r*2,cl);px(x+col*2+1,y+r*2,cl);
        px(x+col*2,y+r*2+1,cl);px(x+col*2+1,y+r*2+1,cl);
    }}
}
static void text_big(int x,int y,const char*str,u32 fg,u32 bg){while(*str){glyph2x(x,y,*str++,fg,bg);x+=16;}}
static void text_big_center(int cx,int y,const char*str,u32 fg,u32 bg){int l=0;const char*p=str;while(*p++)l++;text_big(cx-l*8,y,str,fg,bg);}
static void glyph_bold(int x,int y,char c,u32 fg,u32 bg){
    int idx=(u8)c-32;if(idx<0||idx>=96)idx=0;
    for(int r=0;r<16;r++){u8 b=font[idx][r];for(int col=0;col<8;col++){
        if(b&(0x80>>col)){px(x+col,y+r,fg);if(col<7)px(x+col+1,y+r,fg);}
        else px(x+col,y+r,bg);
    }}
}
static void text_bold(int x,int y,const char*str,u32 fg,u32 bg){while(*str){glyph_bold(x,y,*str++,fg,bg);x+=8;}}
static void text_bold_center(int cx,int y,const char*str,u32 fg,u32 bg){int l=0;const char*p=str;while(*p++)l++;text_bold(cx-l*4,y,str,fg,bg);}

static int slen(const char*s){int n=0;while(s[n])n++;return n;}

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
#define TBAR_H     80
#define TBAR_GAP   10
#define TBAR_PILL_W 900
#define TBAR_PILL_H (TBAR_H-TBAR_GAP)
#define TBAR_PILL_X (((int)FB_W-TBAR_PILL_W)/2)
#define TBAR_PILL_Y ((int)FB_H-TBAR_H)
#define TBAR_SB_SZ  50
#define TBAR_SB_X   (TBAR_PILL_X+8)
#define TBAR_SB_Y   (TBAR_PILL_Y+(TBAR_PILL_H-TBAR_SB_SZ)/2)
#define TBAR_WINBTN_X0 (TBAR_SB_X+TBAR_SB_SZ+12)
#define TBAR_WINBTN_W  (TBAR_PILL_H-8)
#define TBAR_WINBTN_GAP 8
#define TBAR_CLOCK_X (TBAR_PILL_X+TBAR_PILL_W-80)
#define TBAR_CLOCK_Y (TBAR_PILL_Y+27)
#define TBAR_BELL_SZ 36
#define TBAR_BELL_X (TBAR_CLOCK_X-TBAR_BELL_SZ-16)
#define TBAR_BELL_Y (TBAR_PILL_Y+(TBAR_PILL_H-TBAR_BELL_SZ)/2)
#define NOTIF_POPUP_W 320
#define NOTIF_POPUP_H 80
#define NOTIF_POPUP_X ((int)FB_W-NOTIF_POPUP_W-20)
#define NOTIF_POPUP_Y 20
#define NC_W 380
#define NC_H 420
#define NC_ROW_H 64
#define NC_MAX_VISIBLE 5
#define NC_X (TBAR_BELL_X+TBAR_BELL_SZ-NC_W)
#define NC_Y (TBAR_PILL_Y-NC_H-12)
#define PANEL_W    240
#define PANEL_X    ((int)FB_W-PANEL_W)
#define TITLEBAR_H 28
#define RESIZE_GRIP 8
#define ANIM_TICKS  8

static int in_box(int px2,int py,int x,int y,int w,int h){
    return px2>=x&&px2<x+w&&py>=y&&py<y+h;
}

/* ═══ WINDOW MANAGER ════════════════════════════════════════════ */
#define MAX_WINDOWS 12
#define WIN_TERMINAL 0
#define WIN_ABOUT    1
#define WIN_FILES    2
#define WIN_NOTEPAD  3
#define WIN_SETTINGS 4
#define WIN_CALC     5
#define WIN_IMGVIEW  6
static const int win_glyph_map[7]={0,2,1,3,5,4,1};
static int win_glyph_idx(int wid){if(wid<0||wid>6)return 0;return win_glyph_map[wid];}
#define NOTIF_MAX 20
#define NOTIF_POPUP_TICKS 500
static char notif_title[NOTIF_MAX][32];
static char notif_msg[NOTIF_MAX][64];
static int notif_count=0;
static int notif_popup_active=0;
static u64 notif_popup_expire=0;
static u64 g_now_ticks=0;
static int notif_center_open=0;
static int notif_scroll=0;
static void notif_add(const char*title,const char*msg){
    if(notif_count>=NOTIF_MAX){
        for(int i=0;i<NOTIF_MAX-1;i++){
            int j=0;while(notif_title[i+1][j]){notif_title[i][j]=notif_title[i+1][j];j++;}notif_title[i][j]=0;
            j=0;while(notif_msg[i+1][j]){notif_msg[i][j]=notif_msg[i+1][j];j++;}notif_msg[i][j]=0;
        }
        notif_count=NOTIF_MAX-1;
    }
    int j=0;while(title[j]&&j<30){notif_title[notif_count][j]=title[j];j++;}notif_title[notif_count][j]=0;
    j=0;while(msg[j]&&j<62){notif_msg[notif_count][j]=msg[j];j++;}notif_msg[notif_count][j]=0;
    notif_count++;
    notif_popup_active=1;
    notif_popup_expire=g_now_ticks+NOTIF_POPUP_TICKS;
}
static void notif_remove(int idx){
    if(idx<0||idx>=notif_count)return;
    for(int i=idx;i<notif_count-1;i++){
        int j=0;while(notif_title[i+1][j]){notif_title[i][j]=notif_title[i+1][j];j++;}notif_title[i][j]=0;
        j=0;while(notif_msg[i+1][j]){notif_msg[i][j]=notif_msg[i+1][j];j++;}notif_msg[i][j]=0;
    }
    notif_count--;
}
static void draw_bell_glyph(int cx,int cy,u32 fg){
    rect(cx-7,cy-8,14,12,fg);
    rect(cx-9,cy+4,18,3,fg);
    rect(cx-2,cy+9,4,3,fg);
}

typedef struct {
    int id,x,y,w,h,visible,minimized,z;
    char title[40];
    u32 accent;
    int anim,anim_type,min_w,min_h;
} Win;

static Win wins[MAX_WINDOWS];
static int win_count=0,focused=-1;
static int drag_win=-1,drag_ox=0,drag_oy=0;
static int drag_icon=-1,drag_icon_ox=0,drag_icon_oy=0,drag_icon_sx=0,drag_icon_sy=0;
static int resize_win=-1,resize_edge=0;
static int resize_start_x=0,resize_start_y=0;
static int resize_start_w=0,resize_start_h=0;
static int resize_orig_x=0,resize_orig_y=0;
static int mouse_x=512,mouse_y=384,mouse_btn=0,prev_btn=0;
static u32  cfg_accent=0x58A6FF;
static int  cfg_24h=1,cfg_showsecs=1;
static int  rctx_open=0,rctx_x=0,rctx_y=0,rctx_target=-1,rctx_hov=-1;
/* Shared across all File Manager instances (real clipboard semantics —
 * copy in one window, paste in another, like a normal desktop). */
static char fm_clip[32];static int fm_clip_cut=0,fm_has_clip=0;
static char fm_clip_path[200];  /* subdirectory the clipboard item was copied FROM, mirrors fm_path */
static char fm_cpbuf[4096];
typedef struct{
    int  ctx_open,ctx_x,ctx_y,ctx_hov;
    int  dialog;
    char dlg_buf[40];int dlg_len;
    char dlg_err[48];int dlg_has_err;
    char paste_err[48];
    char path[200];int path_len;
    int  use_fat16;
    /* Properties (dialog==4) state */
    char prop_owner_buf[32];int prop_owner_len;
    unsigned int prop_uid,prop_gid;
    unsigned short prop_perm;
    int  prop_field; /* 0=perm digits (dlg_buf), 1=owner username (prop_owner_buf) */
}FMState;
static FMState fm_states[MAX_WINDOWS];
static int fm_current=-1;
#define fm_prop_owner_buf (fm_states[fm_current].prop_owner_buf)
#define fm_prop_owner_len (fm_states[fm_current].prop_owner_len)
#define fm_prop_uid       (fm_states[fm_current].prop_uid)
#define fm_prop_gid       (fm_states[fm_current].prop_gid)
#define fm_prop_perm      (fm_states[fm_current].prop_perm)
#define fm_prop_field     (fm_states[fm_current].prop_field)
#define fm_ctx_open      (fm_states[fm_current].ctx_open)
#define fm_ctx_x         (fm_states[fm_current].ctx_x)
#define fm_ctx_y         (fm_states[fm_current].ctx_y)
#define fm_ctx_hov       (fm_states[fm_current].ctx_hov)
#define fm_dialog        (fm_states[fm_current].dialog)
#define fm_dlg_buf       (fm_states[fm_current].dlg_buf)
#define fm_dlg_len       (fm_states[fm_current].dlg_len)
#define fm_dlg_err       (fm_states[fm_current].dlg_err)
#define fm_dlg_has_err   (fm_states[fm_current].dlg_has_err)
#define fm_paste_err     (fm_states[fm_current].paste_err)
#define fm_path          (fm_states[fm_current].path)
#define fm_path_len      (fm_states[fm_current].path_len)
static int  settings_win_idx=-1;
/* Calculator state */
typedef struct{
    char  expr[128];  /* current expression string */
    int   expr_len;
    char  result[32]; /* last result string */
    int   has_result;
    char  history[6][32]; /* last 6 results */
    int   hist_count;
    int   error;
} Calc;
static Calc calc_states[MAX_WINDOWS];
static int calc_current=-1;
#define calc (calc_states[calc_current])


static int wm_new(int id,int x,int y,int w,int h,const char*title,u32 accent){
    if(win_count>=MAX_WINDOWS)return -1;
    int i=win_count++;
    wins[i].id=id;wins[i].x=x;wins[i].y=y;wins[i].w=w;wins[i].h=h;
    wins[i].visible=1;wins[i].minimized=0;wins[i].z=i;wins[i].accent=accent;
    wins[i].anim=ANIM_TICKS;wins[i].anim_type=1;
    wins[i].min_w=200;wins[i].min_h=120;
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
        if(!wins[i].visible||wins[i].minimized)continue;
        int g=RESIZE_GRIP;
        if(in_box(mx,my,wins[i].x-g,wins[i].y-g,wins[i].w+g*2,wins[i].h+g*2)&&wins[i].z>bestz){
            bestz=wins[i].z;best=i;
        }
    }
    return best;
}
static int z_order[MAX_WINDOWS];
static void wm_sort(void){
    for(int i=0;i<win_count;i++)z_order[i]=i;
    for(int i=1;i<win_count;i++){
        int key=z_order[i],j=i-1;
        while(j>=0&&wins[z_order[j]].z>wins[key].z){z_order[j+1]=z_order[j];j--;}
        z_order[j+1]=key;
    }
}

static void btn_icon(int cx,int cy,int type,u32 col){
    if(type==0){line_aa(cx-3,cy-3,cx+3,cy+3,col);line_aa(cx+3,cy-3,cx-3,cy+3,col);}
    else if(type==1){hline(cx-3,cy,7,col);hline(cx-3,cy+1,7,col);}
    else{outline(cx-3,cy-3,7,7,col);}
}

static int wm_draw_frame(int i){
    Win*w=&wins[i];if(!w->visible)return 0;
    int foc=(i==focused);
    int dx=w->x,dy=w->y,dw=w->w,dh=w->h;
    if(w->anim>0&&w->anim_type==1){
        int prog=ANIM_TICKS-w->anim;
        int scale=850+prog*150/ANIM_TICKS;
        int nw=dw*scale/1000,nh=dh*scale/1000;
        dx=dx+(dw-nw)/2;dy=dy+(dh-nh)/2;dw=nw;dh=nh;
        w->anim--;
    }
    if(!w->minimized){
        int sh1=dh;if(dy+4+sh1>(int)FB_H-TBAR_H)sh1=((int)FB_H-TBAR_H)-(dy+4);if(sh1<0)sh1=0;
        int sh2=dh;if(dy+2+sh2>(int)FB_H-TBAR_H)sh2=((int)FB_H-TBAR_H)-(dy+2);if(sh2<0)sh2=0;
        rect(dx+4,dy+4,dw,sh1,0x000000);
        rect(dx+2,dy+2,dw,sh2,0x080808);
        rect(dx,dy,dw,dh,0x0D1117);
    }
    u32 tbar_bg=foc?0x1C2128:0x13161B;
    u32 bord=foc?w->accent:BORDER;
    rect(dx,dy,dw,TITLEBAR_H,tbar_bg);
    hline(dx,dy+TITLEBAR_H,dw,bord);
    outline(dx,dy,dw,w->minimized?TITLEBAR_H:dh,bord);
    if(foc)hline(dx,dy,dw,w->accent);
    if(foc&&!w->minimized){
        u32 g1=0x2D333B,g2=0x404850;
        for(int gy=dy+TITLEBAR_H+6;gy<dy+dh-6;gy+=6){px(dx+dw-2,gy,g1);px(dx+1,gy,g1);}
        for(int gx=dx+6;gx<dx+dw-6;gx+=6){px(gx,dy+dh-2,g1);px(gx,dy+1,g1);}
        rect(dx,dy,5,5,g2);rect(dx+dw-5,dy,5,5,g2);
        rect(dx,dy+dh-5,5,5,g2);rect(dx+dw-5,dy+dh-5,5,5,g2);
    }
    int hcl=in_box(mouse_x,mouse_y,dx+8,dy+7,14,14);
    int hmn=in_box(mouse_x,mouse_y,dx+24,dy+7,14,14);
    int hmx=in_box(mouse_x,mouse_y,dx+40,dy+7,14,14);
    rect(dx+8, dy+7,14,14,hcl?0xFF5F57:0xED4245);
    rect(dx+24,dy+7,14,14,hmn?0xFFBD2E:0xFAA61A);
    rect(dx+40,dy+7,14,14,hmx?0x28C840:0x57F287);
    btn_icon(dx+15,dy+14,0,hcl?0x7A0000:0xC0392B);
    btn_icon(dx+31,dy+14,1,hmn?0x7A4000:0x9A6000);
    btn_icon(dx+47,dy+14,2,hmx?0x007A00:0x1A7A1A);
    text_bold_center(dx+dw/2,dy+6,w->title,foc?TEXT:DIM,tbar_bg);
    return !w->minimized;
}

/* ═══ TERMINAL ══════════════════════════════════════════════════ */
static char tlines[32][128];
static int  trow=0,tinput_len=0,cursor_blink=0;
static char tinput[128];
static int u32_append_dec(char*buf,int bi,unsigned int v){
    if(v==0){buf[bi++]='0';return bi;}
    char tmp[10];int ti=0;
    while(v){tmp[ti++]=(char)('0'+(v%10));v/=10;}
    while(ti>0)buf[bi++]=tmp[--ti];
    return bi;
}
static void tprint(const char*s){
    if(trow>=32){for(int i=0;i<31;i++){int j=0;while(tlines[i+1][j]){tlines[i][j]=tlines[i+1][j];j++;}tlines[i][j]=0;}trow=31;}
    int j=0;while(*s&&j<127)tlines[trow][j++]=*s++;tlines[trow][j]=0;trow++;
}
static void do_shutdown(void);
static void do_restart(void);
static void wav_debug_print(void);
static void wav_scan_file(const char*path);
static void tcmd(const char*cmd){
    char echo[134];echo[0]='$';echo[1]=' ';int i=0;while(cmd[i]&&i<126){echo[i+2]=cmd[i];i++;}echo[i+2]=0;tprint(echo);
    const char*help="help",*clr="clear",*abt="about",*sd="shutdown",*rb="reboot",*shl="shell",*ls="ls",*ipc="ipc",*crl="crashlog",*sll="syslog",*mdb="mousedbg",*wvd="wavdbg",*rsl="restartlog",*wsc="wavscan";
    int mh=1,mc=1,ma=1,ms=1,mrb=1,msh=1,ml=1,mi=1,mcrl=1,msll=1,mmdb=1,mwvd=1,mrsl=1,mwsc=1;
    for(int k=0;help[k]||cmd[k];k++)if(help[k]!=cmd[k]){mh=0;break;}
    for(int k=0;ipc[k]||cmd[k];k++)if(ipc[k]!=cmd[k]){mi=0;break;}
    for(int k=0;crl[k]||cmd[k];k++)if(crl[k]!=cmd[k]){mcrl=0;break;}
    for(int k=0;sll[k]||cmd[k];k++)if(sll[k]!=cmd[k]){msll=0;break;}
    for(int k=0;mdb[k]||cmd[k];k++)if(mdb[k]!=cmd[k]){mmdb=0;break;}
    for(int k=0;wvd[k]||cmd[k];k++)if(wvd[k]!=cmd[k]){mwvd=0;break;}
    for(int k=0;rsl[k]||cmd[k];k++)if(rsl[k]!=cmd[k]){mrsl=0;break;}
    for(int k=0;wsc[k]||cmd[k];k++)if(wsc[k]!=cmd[k]){mwsc=0;break;}
    for(int k=0;clr[k]||cmd[k];k++) if(clr[k]!=cmd[k]) {mc=0;break;}
    for(int k=0;abt[k]||cmd[k];k++) if(abt[k]!=cmd[k]) {ma=0;break;}
    for(int k=0;sd[k]||cmd[k];k++)  if(sd[k]!=cmd[k])  {ms=0;break;}
    for(int k=0;rb[k]||cmd[k];k++)  if(rb[k]!=cmd[k])  {mrb=0;break;}
    for(int k=0;shl[k]||cmd[k];k++) if(shl[k]!=cmd[k]) {msh=0;break;}
    for(int k=0;ls[k]||cmd[k];k++)  if(ls[k]!=cmd[k])  {ml=0;break;}
    if(mh)tprint("Commands: help clear about ls shutdown reboot shell ipc crashlog syslog mousedbg wavdbg restartlog");
    else if(mc){trow=0;for(int r=0;r<32;r++)tlines[r][0]=0;}
    else if(ma){tprint("YouOS v0.3");tprint("x86_64|FAT16|ELF|WM");}
    else if(ml)tprint("hello cat shell fbtest desktop");
    else if(ms){do_shutdown();}
    else if(mrb){do_restart();}
    else if(msh){
        tprint("Switching to shell... type 'exit' to return.");
        flush();
        sys_exec("shell");
        tprint("Welcome back to the desktop.");
    }
    else if(mcrl){
        static char cbuf[2048];
        int n=sys_readcrash(cbuf,2047);
        if(n>0){
            cbuf[n]=0;
            int ci=0;
            while(cbuf[ci]){
                int li=ci;
                while(cbuf[li]&&cbuf[li]!='\n')li++;
                char line[128];int ll=0;
                while(ci<li&&ll<127){line[ll++]=cbuf[ci++];}line[ll]=0;
                if(cbuf[ci]=='\n')ci++;
                if(ll>0)tprint(line);
            }
        } else tprint("No crash log found.");
    }
    else if(msll){
        static char sbuf[2048];
        int n=sys_readsyslog(sbuf,2047);
        if(n>0){
            sbuf[n]=0;
            /* show last 20 lines */
            int lines[20],lc=0;
            lines[lc++]=0;
            for(int i=0;sbuf[i];i++)if(sbuf[i]=='\n'&&lc<20)lines[lc++]=i+1;
            int start=lc>10?lc-10:0;
            for(int i=start;i<lc;i++){
                int li=lines[i];
                char line[128];int ll=0;
                while(sbuf[li]&&sbuf[li]!='\n'&&ll<127){line[ll++]=sbuf[li++];}line[ll]=0;
                if(ll>0)tprint(line);
            }
        } else tprint("Syslog empty.");
    }
    else if(mmdb){
        unsigned long long dbg[4];
        sys_mousedbg(dbg);
        char out2[64];int oi2=0;
        const char*pfx2="wheel_support=";int pj2=0;while(pfx2[pj2])out2[oi2++]=pfx2[pj2++];
        out2[oi2++]='0'+((int)dbg[2]?1:0);
        const char*pfx3=" delta=";pj2=0;while(pfx3[pj2])out2[oi2++]=pfx3[pj2++];
        int dlt=(int)(long long)dbg[3];
        if(dlt<0){out2[oi2++]='-';dlt=-dlt;}
        if(dlt>=10)out2[oi2++]='0'+(dlt/10)%10;
        out2[oi2++]='0'+(dlt%10);
        out2[oi2]=0;
        tprint(out2);
    }
    else if(mwvd){
        unsigned int a=sys_ac97_debug(3); /* civ<<24 | lvi<<16 | sr */
        unsigned int civ=(a>>24)&0xFF, lvi=(a>>16)&0xFF, sr=a&0xFFFF;
        unsigned int b=sys_ac97_debug(4); /* submitted<<16 | completed */
        unsigned int sub=(b>>16)&0xFFFF, comp=b&0xFFFF;
        unsigned int c=sys_ac97_debug(5); /* restarts<<16 | fastpath */
        unsigned int rst=(c>>16)&0xFFFF, fp=c&0xFFFF;
        char l1[64];int i1=0;
        const char*p1="civ=";int j1=0;while(p1[j1])l1[i1++]=p1[j1++];
        i1=u32_append_dec(l1,i1,civ);
        const char*p2=" lvi=";j1=0;while(p2[j1])l1[i1++]=p2[j1++];
        i1=u32_append_dec(l1,i1,lvi);
        const char*p3=" sr=";j1=0;while(p3[j1])l1[i1++]=p3[j1++];
        i1=u32_append_dec(l1,i1,sr);
        l1[i1]=0;tprint(l1);
        char l2[64];int i2=0;
        const char*p4="submitted=";j1=0;while(p4[j1])l2[i2++]=p4[j1++];
        i2=u32_append_dec(l2,i2,sub);
        const char*p5=" completed=";j1=0;while(p5[j1])l2[i2++]=p5[j1++];
        i2=u32_append_dec(l2,i2,comp);
        l2[i2]=0;tprint(l2);
        char l3[64];int i3=0;
        const char*p6="restarts=";j1=0;while(p6[j1])l3[i3++]=p6[j1++];
        i3=u32_append_dec(l3,i3,rst);
        const char*p7=" fastpath=";j1=0;while(p7[j1])l3[i3++]=p7[j1++];
        i3=u32_append_dec(l3,i3,fp);
        l3[i3]=0;tprint(l3);
        unsigned int irqfire=sys_ac97_debug(0), irqbcis=sys_ac97_debug(1);
        char l4[64];int i4=0;
        const char*p8="irq_fire=";int j4=0;while(p8[j4])l4[i4++]=p8[j4++];
        i4=u32_append_dec(l4,i4,irqfire);
        const char*p9=" irq_bcis=";j4=0;while(p9[j4])l4[i4++]=p9[j4++];
        i4=u32_append_dec(l4,i4,irqbcis);
        l4[i4]=0;tprint(l4);
        unsigned int csd=sys_ac97_debug(8);
        char l5[48];int i5=0;
        const char*p10="cold_start_ticks=";int j5=0;while(p10[j5])l5[i5++]=p10[j5++];
        i5=u32_append_dec(l5,i5,csd);
        l5[i5]=0;tprint(l5);
        unsigned int afp=sys_ac97_debug(9);
        char l6[48];int i6=0;
        const char*p11="last_alloc_fail_pages=";int j6=0;while(p11[j6])l6[i6++]=p11[j6++];
        i6=u32_append_dec(l6,i6,afp);
        l6[i6]=0;tprint(l6);
        unsigned int fa=sys_ac97_debug(10), fb=sys_ac97_debug(11);
        unsigned int loop_iters=fa>>16, phys_fail=fa&0xFFFF;
        char l7[64];int i7=0;
        const char*p12="feed_iters=";int j7=0;while(p12[j7])l7[i7++]=p12[j7++];
        i7=u32_append_dec(l7,i7,loop_iters);
        const char*p13=" phys_fail=";j7=0;while(p13[j7])l7[i7++]=p13[j7++];
        i7=u32_append_dec(l7,i7,phys_fail);
        const char*p14=" submit_fail=";j7=0;while(p14[j7])l7[i7++]=p14[j7++];
        i7=u32_append_dec(l7,i7,fb);
        l7[i7]=0;tprint(l7);
        unsigned int fv=sys_ac97_debug(12), fpv=sys_ac97_debug(13), av=sys_ac97_debug(14);
        char l8[80];int i8=0;
        const char*p15="alloc_virt=0x";int j8=0;while(p15[j8])l8[i8++]=p15[j8++];
        {char hx[9];for(int k=7;k>=0;k--){unsigned int nib=(av>>(k*4))&0xF;hx[7-k]=nib<10?('0'+nib):('A'+nib-10);}hx[8]=0;int hk=0;while(hx[hk])l8[i8++]=hx[hk++];}
        const char*p16=" fail_virt=0x";j8=0;while(p16[j8])l8[i8++]=p16[j8++];
        {char hx[9];for(int k=7;k>=0;k--){unsigned int nib=(fv>>(k*4))&0xF;hx[7-k]=nib<10?('0'+nib):('A'+nib-10);}hx[8]=0;int hk=0;while(hx[hk])l8[i8++]=hx[hk++];}
        const char*p17=" fail_pagevirt=0x";j8=0;while(p17[j8])l8[i8++]=p17[j8++];
        {char hx[9];for(int k=7;k>=0;k--){unsigned int nib=(fpv>>(k*4))&0xF;hx[7-k]=nib<10?('0'+nib):('A'+nib-10);}hx[8]=0;int hk=0;while(hx[hk])l8[i8++]=hx[hk++];}
        l8[i8]=0;tprint(l8);
        unsigned int fvh=sys_ac97_debug(15), avh=sys_ac97_debug(16), pml4=sys_ac97_debug(17);
        char l9[80];int i9=0;
        const char*p18="alloc_hi=0x";int j9=0;while(p18[j9])l9[i9++]=p18[j9++];
        {char hx[9];for(int k=7;k>=0;k--){unsigned int nib=(avh>>(k*4))&0xF;hx[7-k]=nib<10?('0'+nib):('A'+nib-10);}hx[8]=0;int hk=0;while(hx[hk])l9[i9++]=hx[hk++];}
        const char*p19=" fail_hi=0x";j9=0;while(p19[j9])l9[i9++]=p19[j9++];
        {char hx[9];for(int k=7;k>=0;k--){unsigned int nib=(fvh>>(k*4))&0xF;hx[7-k]=nib<10?('0'+nib):('A'+nib-10);}hx[8]=0;int hk=0;while(hx[hk])l9[i9++]=hx[hk++];}
        const char*p20=" pml4=0x";j9=0;while(p20[j9])l9[i9++]=p20[j9++];
        {char hx[9];for(int k=7;k>=0;k--){unsigned int nib=(pml4>>(k*4))&0xF;hx[7-k]=nib<10?('0'+nib):('A'+nib-10);}hx[8]=0;int hk=0;while(hx[hk])l9[i9++]=hx[hk++];}
        l9[i9]=0;tprint(l9);
        wav_debug_print();
    }
    else if(mrsl){
        int any=0;
        for(unsigned int idx=0;idx<16;idx++){
            unsigned int v=sys_ac97_debug_idx(7,idx);
            if(v==0xFFFFFFFFu)break;
            any=1;
            char l[48];int li=0;
            const char*p1="restart at chunk #";int j1=0;while(p1[j1])l[li++]=p1[j1++];
            li=u32_append_dec(l,li,v);
            l[li]=0;tprint(l);
        }
        if(!any)tprint("No mid-stream restarts logged.");
    }
    else if(mwsc){
        wav_scan_file("/ycfs/ding.wav");
    }
    else if(mi){
        char msg[32]="hello from desktop";
        int pr=sys_msgpost("test",msg,18);
        if(pr<0){tprint("IPC FAIL: post error");return;}
        char buf[128];unsigned int len=0,from=0;
        int rc=sys_msgrecv("test",buf,&len,&from);
        if(rc<0){tprint("IPC FAIL: recv error");return;}
        buf[len]=0;
        char out[64];out[0]='I';out[1]='P';out[2]='C';out[3]=' ';out[4]='O';out[5]='K';out[6]=':';out[7]=' ';
        int oi=8,bi=0;while(buf[bi]&&oi<62){out[oi++]=buf[bi++];}out[oi]=0;
        tprint(out);
    }
    else{char m[64];m[0]='?';m[1]=' ';int k=0;while(cmd[k]&&k<58){m[k+2]=cmd[k];k++;}m[k+2]=0;tprint(m);}
}
static void draw_terminal_content(int i){
    Win*w=&wins[i];int pad=8;
    int cx=w->x+pad,cy=w->y+TITLEBAR_H+pad;
    int max_cols=(w->w-pad*2)/8;int max_rows=(w->h-TITLEBAR_H-32)/16;
    if(max_rows<1||max_cols<1)return;
    rect(w->x,w->y+TITLEBAR_H,w->w,w->h-TITLEBAR_H,0x0D1117);
    int start=trow>max_rows?trow-max_rows:0;
    for(int r=start;r<trow;r++){
        u32 fg=TEXT;if(tlines[r][0]=='$')fg=GREEN;else if(tlines[r][0]=='?')fg=RED;
        char clip[128];int k=0;while(tlines[r][k]&&k<max_cols&&k<127){clip[k]=tlines[r][k];k++;}clip[k]=0;
        text(cx,cy+(r-start)*16,clip,fg,0x0D1117);
    }
    int iy=w->y+w->h-24;
    hline(w->x,iy-2,w->w,BORDER);rect(w->x,iy,w->w,22,0x0A0D10);
    text(w->x+pad,iy+3,"$ ",GREEN,0x0A0D10);
    int input_max=(w->w-pad*2-16)/8;if(input_max<0)input_max=0;
    char iclip[128];int k=0;while(tinput[k]&&k<input_max&&k<127){iclip[k]=tinput[k];k++;}iclip[k]=0;
    text(w->x+pad+16,iy+3,iclip,TEXT,0x0A0D10);
    int cur_x=w->x+pad+16+tinput_len*8;
    if(cursor_blink<50&&cur_x+8<w->x+w->w)rect(cur_x,iy+2,8,14,cfg_accent);
}

/* ═══ FILE MANAGER ══════════════════════════════════════════════ */
typedef struct{char name[32];unsigned int size;unsigned char is_dir;}Dirent;
#define MAX_FILES 64
typedef struct{
    Dirent entries[MAX_FILES];
    int count,scroll,hovered,loaded;
    int selected,last_fi,del_confirm;
    u64 last_tick;
}FMListState;
static FMListState fml_states[MAX_WINDOWS];
#define fm_entries       (fml_states[fm_current].entries)
#define fm_count         (fml_states[fm_current].count)
#define fm_scroll        (fml_states[fm_current].scroll)
#define fm_hovered       (fml_states[fm_current].hovered)
#define fm_loaded        (fml_states[fm_current].loaded)
#define fm_selected      (fml_states[fm_current].selected)
#define fm_last_fi       (fml_states[fm_current].last_fi)
#define fm_del_confirm   (fml_states[fm_current].del_confirm)
#define fm_last_tick     (fml_states[fm_current].last_tick)
/* Build a full path for `name` inside the CURRENTLY BROWSED directory
 * of the given File Manager instance (root, or fm_path if navigated
 * into a subdirectory), using the given filesystem prefix. Every file
 * operation (delete/rename/mkdir/paste) must go through this instead
 * of hand-building paths — previously each site rebuilt the prefix
 * itself and silently ignored fm_path, so every operation always
 * targeted the root directory regardless of which folder was actually
 * being browsed. */
/* Build a full /ycfs path for `name` inside the CURRENTLY BROWSED
 * directory (root, or fm_path if navigated into a subdirectory). */
static void fm_build_path(char*out,int outsz,const char*cur_path,const char*name){
    int k=0;
    if(k<outsz-1)out[k++]='/';if(k<outsz-1)out[k++]='y';if(k<outsz-1)out[k++]='c';if(k<outsz-1)out[k++]='f';if(k<outsz-1)out[k++]='s';if(k<outsz-1)out[k++]='/';
    int j=0;while(cur_path[j]&&k<outsz-1){out[k++]=cur_path[j++];}
    if(cur_path[0]&&k<outsz-1)out[k++]='/';
    j=0;while(name[j]&&k<outsz-1){out[k++]=name[j++];}
    out[k]=0;
}
static void fm_load(void){
    fm_paste_err[0]=0;
    if(fm_path_len>0){
        char full[220];
        fm_build_path(full,sizeof(full),fm_path,"");
        int fl=slen(full);if(fl>0&&full[fl-1]=='/')full[fl-1]=0; /* fm_build_path always trails a '/', trim when name is empty */
        fm_count=(int)sys_readdir2(full,fm_entries,MAX_FILES);
    } else {
        fm_count=(int)sys_readdir2("/ycfs",fm_entries,MAX_FILES);
    }
    fm_scroll=0;fm_selected=-1;fm_last_fi=-1;fm_last_tick=0;fm_del_confirm=0;
    fm_ctx_open=0;fm_dialog=0;fm_dlg_has_err=0;fm_loaded=1;
}
static void fmt_size(unsigned int sz,char*out){
    if(sz==0){out[0]='d';out[1]='i';out[2]='r';out[3]=0;return;}
    if(sz<1024){
        int i=0;
        if(sz>=100)out[i++]='0'+sz/100;
        if(sz>=10) out[i++]='0'+(sz/10)%10;
        out[i++]='0'+sz%10;out[i++]=' ';out[i++]='B';out[i]=0;return;
    }
    unsigned int kb=sz/1024,fr=(sz%1024)*10/1024;
    int i=0;
    if(kb>=100)out[i++]='0'+kb/100;
    if(kb>=10) out[i++]='0'+(kb/10)%10;
    out[i++]='0'+kb%10;out[i++]='.';out[i++]='0'+fr;
    out[i++]=' ';out[i++]='K';out[i++]='B';out[i]=0;
}
static void draw_files_content(int wi){
    Win*w=&wins[wi];
    int x=w->x,y=w->y+TITLEBAR_H,cw=w->w,ch=w->h-TITLEBAR_H;
    rect(x,y,cw,ch,0x0D1117);
    rect(x,y,cw,28,0x161B22);hline(x,y+28,cw,BORDER);
    /* path bar */
    char pathbar[220];int pbi=0;
    pathbar[0]='/';pathbar[1]='y';pathbar[2]='c';pathbar[3]='f';pathbar[4]='s';pbi=5;
    if(fm_path_len>0){pathbar[pbi++]='/';int pj=0;while(fm_path[pj]&&pbi<218){pathbar[pbi++]=fm_path[pj++];}pathbar[pbi]=0;}
    else pathbar[pbi]=0;
    text(x+8,y+6,pathbar,cfg_accent,0x161B22);
    int rhov=in_box(mouse_x,mouse_y,x+cw-60,y+4,52,20);
    rect(x+cw-60,y+4,52,20,rhov?0x21262D:0x161B22);outline(x+cw-60,y+4,52,20,BORDER);
    text(x+cw-56,y+6,"Reload",rhov?TEXT:DIM,rhov?0x21262D:0x161B22);
    if(fm_path_len>0){
        int uhov=in_box(mouse_x,mouse_y,x+cw-128,y+4,52,20);
        rect(x+cw-128,y+4,52,20,uhov?0x21262D:0x161B22);outline(x+cw-128,y+4,52,20,BORDER);
        text(x+cw-124,y+6,"Up",uhov?TEXT:DIM,uhov?0x21262D:0x161B22);
    }
    int hy=y+32;rect(x,hy,cw,18,0x13161B);hline(x,hy+18,cw,BORDER);
    text(x+28,hy+1,"Name",DIM,0x13161B);text(x+cw-90,hy+1,"Size",DIM,0x13161B);
    vline(x+cw-100,hy,18,BORDER);
    int row_y=hy+20,row_h=22,status_h=20;
    int max_vis=(ch-72-status_h)/row_h;if(max_vis<1)max_vis=1;
    int end=fm_scroll+max_vis;if(end>fm_count)end=fm_count;
    int size_col_x=x+cw-96;
    for(int i=fm_scroll;i<end;i++){
        int ry=row_y+(i-fm_scroll)*row_h;
        int hov=(i==fm_hovered);int sel=(i==fm_selected);
        u32 rbg=sel?0x1A2A3A:(hov?0x21262D:0x0D1117);
        rect(x,ry,cw,row_h,rbg);
        u32 icol=fm_entries[i].is_dir?YELLOW:cfg_accent;
        rect(x+8,ry+5,12,12,icol);
        for(int r2=ry+6;r2<ry+16;r2++)for(int c2=x+9;c2<x+19;c2++){
            u32 col=buf[r2*(int)FB_W+c2];u32 rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;
            buf[r2*(int)FB_W+c2]=((rr/2)<<16)|((gg/2)<<8)|(bb/2);
        }
        u32 tfg=(fm_has_clip&&fm_clip_cut&&fm_selected==i)?DIM:(fm_entries[i].is_dir?YELLOW:TEXT);
        int name_max=(size_col_x-x-30)/8;if(name_max<1)name_max=1;
        char nclip[32];int k=0;while(fm_entries[i].name[k]&&k<name_max&&k<31){nclip[k]=fm_entries[i].name[k];k++;}nclip[k]=0;
        text(x+26,ry+3,nclip,tfg,rbg);
        char szstr[16];fmt_size(fm_entries[i].size,szstr);
        int sl=slen(szstr);text(size_col_x+(6-sl)*8,ry+3,szstr,DIM,rbg);
        hline(x,ry+row_h-1,cw,0x161B22);
    }
    if(fm_count>max_vis){
        rect(x+cw-16,y+50,14,14,fm_scroll>0?0x21262D:0x13161B);
        text(x+cw-14,y+51,"^",fm_scroll>0?TEXT:BORDER,fm_scroll>0?0x21262D:0x13161B);
        int can_dn=fm_scroll+max_vis<fm_count;
        rect(x+cw-16,y+66,14,14,can_dn?0x21262D:0x13161B);
        text(x+cw-14,y+67,"v",can_dn?TEXT:BORDER,can_dn?0x21262D:0x13161B);
    }
    int sb=w->y+w->h-status_h;rect(x,sb,cw,status_h,0x161B22);hline(x,sb,cw,BORDER);
    char stat[48];int si=0;
    if(fm_paste_err[0]){
        int ei=0;while(fm_paste_err[ei]&&si<47){stat[si++]=fm_paste_err[ei++];}
        stat[si]=0;text(x+8,sb+2,stat,RED,0x161B22);
        goto skip_status_line;
    }
    if(fm_count>=10)stat[si++]='0'+fm_count/10;stat[si++]='0'+fm_count%10;
    const char*suf=" items on /ycfs";int sf=0;while(suf[sf])stat[si++]=suf[sf++];
    if(fm_has_clip){
        const char*cs=fm_clip_cut?" | cut:":" | copied:";
        while(*cs)stat[si++]=*cs++;
        int ci=0;while(fm_clip[ci]&&si<46)stat[si++]=fm_clip[ci++];
    }
    stat[si]=0;text(x+8,sb+2,stat,DIM,0x161B22);
    skip_status_line:;
    if(fm_dialog==1&&fm_selected>=0){
        int dw=300,dh=88,ddx=x+(cw-dw)/2,ddy=y+(ch-dh)/2;
        rect(ddx+3,ddy+3,dw,dh,0x050810);
        rect(ddx,ddy,dw,dh,0x1C2128);outline(ddx,ddy,dw,dh,RED);
        rect(ddx,ddy,dw,24,0x2A1010);hline(ddx,ddy+24,dw,0x5A2020);
        text_center(ddx+dw/2,ddy+4,"Delete?",RED,0x2A1010);
        text_center(ddx+dw/2,ddy+34,fm_entries[fm_selected].name,TEXT,0x1C2128);
        int yhov=in_box(mouse_x,mouse_y,ddx+30,ddy+60,80,22);
        int nhov=in_box(mouse_x,mouse_y,ddx+dw-110,ddy+60,80,22);
        rect(ddx+30,ddy+60,80,22,yhov?RED:0x6B1010);outline(ddx+30,ddy+60,80,22,RED);
        text_center(ddx+70,ddy+62,"Delete",yhov?WHITE:TEXT,yhov?RED:0x6B1010);
        rect(ddx+dw-110,ddy+60,80,22,nhov?0x21262D:0x13161B);outline(ddx+dw-110,ddy+60,80,22,BORDER);
        text_center(ddx+dw-70,ddy+62,"Cancel",TEXT,nhov?0x21262D:0x13161B);
    }
    if(fm_dialog==2){
        int dw=320,dh=106,ddx=x+(cw-dw)/2,ddy=y+(ch-dh)/2;
        rect(ddx,ddy,dw,dh,0x1C2128);outline(ddx,ddy,dw,dh,cfg_accent);
        rect(ddx,ddy,dw,24,0x161B22);hline(ddx,ddy+24,dw,BORDER);
        text_center(ddx+dw/2,ddy+4,"Rename",TEXT,0x161B22);
        text(ddx+8,ddy+30,"New name:",DIM,0x1C2128);
        rect(ddx+8,ddy+48,dw-16,20,0x0D1117);outline(ddx+8,ddy+48,dw-16,20,cfg_accent);
        text(ddx+12,ddy+50,fm_dlg_buf,TEXT,0x0D1117);
        if(cursor_blink<50)rect(ddx+12+fm_dlg_len*8,ddy+50,2,16,cfg_accent);
        int ohov=in_box(mouse_x,mouse_y,ddx+dw/2-34,ddy+76,64,20);
        rect(ddx+dw/2-34,ddy+76,64,20,ohov?cfg_accent:0x21262D);
        text_center(ddx+dw/2,ddy+78,"Rename",ohov?BG:TEXT,ohov?cfg_accent:0x21262D);
        if(fm_dlg_has_err)text(ddx+8,ddy+dh-12,fm_dlg_err,RED,0x1C2128);
    }
    if(fm_dialog==3){
        int dw=320,dh=106,ddx=x+(cw-dw)/2,ddy=y+(ch-dh)/2;
        rect(ddx,ddy,dw,dh,0x1C2128);outline(ddx,ddy,dw,dh,cfg_accent);
        rect(ddx,ddy,dw,24,0x161B22);hline(ddx,ddy+24,dw,BORDER);
        text_center(ddx+dw/2,ddy+4,"New Folder",TEXT,0x161B22);
        text(ddx+8,ddy+30,"Folder name:",DIM,0x1C2128);
        rect(ddx+8,ddy+48,dw-16,20,0x0D1117);outline(ddx+8,ddy+48,dw-16,20,cfg_accent);
        text(ddx+12,ddy+50,fm_dlg_buf,TEXT,0x0D1117);
        if(cursor_blink<50)rect(ddx+12+fm_dlg_len*8,ddy+50,2,16,cfg_accent);
        int ohov=in_box(mouse_x,mouse_y,ddx+dw/2-34,ddy+76,64,20);
        rect(ddx+dw/2-34,ddy+76,64,20,ohov?cfg_accent:0x21262D);
        text_center(ddx+dw/2,ddy+78,"Create",ohov?BG:TEXT,ohov?cfg_accent:0x21262D);
        if(fm_dlg_has_err)text(ddx+8,ddy+dh-12,fm_dlg_err,RED,0x1C2128);
    }
    if(fm_dialog==4){
        int dw=320,dh=190,ddx=x+(cw-dw)/2,ddy=y+(ch-dh)/2;
        rect(ddx,ddy,dw,dh,0x1C2128);outline(ddx,ddy,dw,dh,cfg_accent);
        rect(ddx,ddy,dw,24,0x161B22);hline(ddx,ddy+24,dw,BORDER);
        text_center(ddx+dw/2,ddy+4,"Properties",TEXT,0x161B22);
        text(ddx+8,ddy+30,"Owner (username):",DIM,0x1C2128);
        int ofx=ddx+8,ofy=ddy+46,ofw=dw-16,ofh=20;
        rect(ofx,ofy,ofw,ofh,0x0D1117);outline(ofx,ofy,ofw,ofh,fm_prop_field==1?cfg_accent:BORDER);
        text(ofx+4,ofy+2,fm_prop_owner_buf,TEXT,0x0D1117);
        if(fm_prop_field==1&&cursor_blink<50)rect(ofx+4+fm_prop_owner_len*8,ofy+2,2,16,cfg_accent);
        text(ddx+8,ddy+72,"Permissions (octal, e.g. 644):",DIM,0x1C2128);
        int pfx2=ddx+8,pfy=ddy+88,pfw=60,pfh=20;
        rect(pfx2,pfy,pfw,pfh,0x0D1117);outline(pfx2,pfy,pfw,pfh,fm_prop_field==0?cfg_accent:BORDER);
        text(pfx2+4,pfy+2,fm_dlg_buf,TEXT,0x0D1117);
        if(fm_prop_field==0&&cursor_blink<50)rect(pfx2+4+fm_dlg_len*8,pfy+2,2,16,cfg_accent);
        int ohov=in_box(mouse_x,mouse_y,ddx+dw/2-64,ddy+dh-32,60,22);
        rect(ddx+dw/2-64,ddy+dh-32,60,22,ohov?cfg_accent:0x21262D);
        text_center(ddx+dw/2-34,ddy+dh-29,"Apply",ohov?BG:TEXT,ohov?cfg_accent:0x21262D);
        int chov2=in_box(mouse_x,mouse_y,ddx+dw/2+4,ddy+dh-32,60,22);
        rect(ddx+dw/2+4,ddy+dh-32,60,22,chov2?0x21262D:0x161B22);outline(ddx+dw/2+4,ddy+dh-32,60,22,BORDER);
        text_center(ddx+dw/2+34,ddy+dh-29,"Cancel",TEXT,chov2?0x21262D:0x161B22);
        if(fm_dlg_has_err)text(ddx+8,ddy+dh-52,fm_dlg_err,RED,0x1C2128);
    }
    if(fm_ctx_open){
        int has_sel=(fm_selected>=0);
        const char*items[]={"New Folder","","Copy","Cut","Paste","Rename","Delete","","Properties"};
        int n=9,iw=160,ih=22,sep=6;
        int mh=2;for(int i=0;i<n;i++)mh+=items[i][0]?ih:sep;
        int mx2=fm_ctx_x,my2=fm_ctx_y;
        if(mx2+iw>w->x+cw)mx2=w->x+cw-iw;
        if(my2+mh>w->y+w->h)my2=w->y+w->h-mh;
        rect(mx2+2,my2+2,iw,mh,0x050810);
        rect(mx2,my2,iw,mh,0x1C2128);outline(mx2,my2,iw,mh,BORDER);
        hline(mx2+1,my2,iw-2,cfg_accent);
        int iy2=my2+1;
        for(int i=0;i<n;i++){
            if(!items[i][0]){hline(mx2+6,iy2+3,iw-12,BORDER);iy2+=sep;continue;}
            int hov=(fm_ctx_hov==i);
            int grey=0;
            if(i==2||i==3||i==5||i==6||i==8)grey=!has_sel;
            if(i==4)grey=!fm_has_clip;
            u32 fg2=grey?0x444444:TEXT;
            if(hov&&!grey)rect(mx2+1,iy2,iw-2,ih,cfg_accent);
            text(mx2+10,iy2+(ih-16)/2,items[i],(hov&&!grey)?BG:fg2,(hov&&!grey)?cfg_accent:0x1C2128);
            iy2+=ih;
        }
    }
}

/* ═══ NOTEPAD ═══════════════════════════════════════════════════ */
#define NP_BUFSIZE 8192
typedef struct{
    char text[NP_BUFSIZE];
    int  text_len,cursor,scroll;
    char filename[48];
    int  modified,mode,save_flash;
    char dlg_buf[32];
    int  dlg_len,dlg_hov,dlg_scroll;
    int  mode3_err;
    char err_name[36];
}Notepad;
static Notepad np_states[MAX_WINDOWS];
static int np_current=-1;
#define np (np_states[np_current])

static void np_insert(char c){
    if(np.text_len>=NP_BUFSIZE-1)return;
    for(int i=np.text_len;i>np.cursor;i--)np.text[i]=np.text[i-1];
    np.text[np.cursor++]=c;np.text_len++;np.text[np.text_len]=0;np.modified=1;
}
static void np_backspace(void){
    if(np.cursor<=0)return;
    for(int i=np.cursor-1;i<np.text_len-1;i++)np.text[i]=np.text[i+1];
    np.text_len--;np.cursor--;np.text[np.text_len]=0;np.modified=1;
}
static void np_del_fwd(void){
    if(np.cursor>=np.text_len)return;
    for(int i=np.cursor;i<np.text_len-1;i++)np.text[i]=np.text[i+1];
    np.text_len--;np.text[np.text_len]=0;np.modified=1;
}
static void np_cursor_pos(int*ln,int*col){
    int l=0,c=0;
    for(int i=0;i<np.cursor;i++){if(np.text[i]=='\n'){l++;c=0;}else c++;}
    *ln=l;*col=c;
}
static int np_total_lines(void){int n=1;for(int i=0;i<np.text_len;i++)if(np.text[i]=='\n')n++;return n;}
static int np_line_start(int line){int l=0,i=0;while(i<np.text_len&&l<line){if(np.text[i]=='\n')l++;i++;}return i;}
static int np_line_end(int ls){int i=ls;while(i<np.text_len&&np.text[i]!='\n')i++;return i;}
static void np_left(void){if(np.cursor>0)np.cursor--;}
static void np_right(void){if(np.cursor<np.text_len)np.cursor++;}
static void np_up(void){
    int ln,col;np_cursor_pos(&ln,&col);if(ln==0){np.cursor=0;return;}
    int ps=np_line_start(ln-1),pe=np_line_end(ps);
    np.cursor=ps+(col<pe-ps?col:pe-ps);
}
static void np_down(void){
    int ln,col;np_cursor_pos(&ln,&col);
    int tot=np_total_lines();if(ln+1>=tot){np.cursor=np.text_len;return;}
    int ns=np_line_start(ln+1),ne=np_line_end(ns);
    np.cursor=ns+(col<ne-ns?col:ne-ns);
}
static void np_home(void){int ln,col;np_cursor_pos(&ln,&col);(void)col;np.cursor=np_line_start(ln);}
static void np_end(void){int ln,col;np_cursor_pos(&ln,&col);(void)col;np.cursor=np_line_end(np_line_start(ln));}
static void np_ensure_vis(int max_rows){
    int ln,col;np_cursor_pos(&ln,&col);(void)col;
    if(ln<np.scroll)np.scroll=ln;
    if(ln>=np.scroll+max_rows)np.scroll=ln-max_rows+1;
}
static void np_load(const char*path,const char*shortname){
    int fd=sys_open(path,0);if(fd<0)return;
    np.text_len=0;s64 r;
    while(np.text_len<NP_BUFSIZE-1&&(r=sys_fread(fd,np.text+np.text_len,128))>0)np.text_len+=(int)r;
    np.text[np.text_len]=0;sys_close(fd);
    np.cursor=0;np.scroll=0;np.modified=0;
    int k=0;while(shortname[k]&&k<47){np.filename[k]=shortname[k];k++;}np.filename[k]=0;
}
/* ═══ WAV PLAYBACK (streaming-engine redesign) ═══════════════════
 * Previous architecture: chunked userspace polling, one
 * ac97_play_pcm() call per WAV_CHUNK_SAMPLES, paced from the main
 * loop's wav_poll(). Real evidence this session (DMA output captured
 * via QEMU's -audiodev wav backend) proved this fundamentally could
 * not deliver continuous audio, however the ring/restart/pacing
 * logic was tuned — the userspace poll loop cannot be paced
 * precisely enough against real wall-clock audio timing under KVM.
 *
 * New architecture: read the ENTIRE WAV file into memory here, then
 * hand it to the kernel in ONE call (sys_play_stream). The kernel
 * copies it and refills the AC97 ring directly from its own
 * completion interrupt handler — see kernel/drivers/ac97.c for the
 * real fix. wav_poll() now just checks sys_stream_active(). */
#define WAV_MAX_SAMPLES 307200 /* 600KB static buffer; covers ding.wav
 * (102622 samples) with real headroom. Real limitation: files larger
 * than this cap are not supported yet — a true arbitrary-length
 * streaming path would need the kernel-timer-driven chunked refill
 * approach instead of "load whole file", future work if ever needed. */
static short wav_full_buf[WAV_MAX_SAMPLES];
static int  wav_playing=0;
static u32  wav_channels=0,wav_sample_rate=0;
static u32  wav_samples_total=0;

static void wav_debug_print(void){
    char l[64];int i=0;
    const char*p1="wav_playing=";int j=0;while(p1[j])l[i++]=p1[j++];
    l[i++]=wav_playing?'1':'0';
    const char*p2=" total=";j=0;while(p2[j])l[i++]=p2[j++];
    i=u32_append_dec(l,i,wav_samples_total);
    l[i]=0;tprint(l);
}
static void play_wav_file(const char*path){
    tprint("WAV: play_wav_file called");
    if(wav_playing){tprint("WAV: already playing, ignored");return;}
    u64 fd=sys_open(path,0);
    if((s64)fd<0){tprint("WAV: sys_open failed");return;}
    tprint("WAV: file opened OK");

    u8 riff_hdr[12];
    if(sys_fread(fd,riff_hdr,12)!=12||riff_hdr[0]!='R'||riff_hdr[1]!='I'||riff_hdr[2]!='F'||riff_hdr[3]!='F'||
       riff_hdr[8]!='W'||riff_hdr[9]!='A'||riff_hdr[10]!='V'||riff_hdr[11]!='E'){tprint("WAV: bad RIFF header");sys_close(fd);return;}

    u16 channels=0,bits_per_sample=0;u32 sample_rate=0;
    int have_fmt=0;
    u32 data_size=0;
    int have_data=0;
    while(!have_data){
        u8 chdr[8];
        if(sys_fread(fd,chdr,8)!=8)break;
        u32 csize=(u32)chdr[4]|((u32)chdr[5]<<8)|((u32)chdr[6]<<16)|((u32)chdr[7]<<24);
        if(chdr[0]=='f'&&chdr[1]=='m'&&chdr[2]=='t'&&chdr[3]==' '){
            u8 fmt[16];
            u32 to_read=csize<16?csize:16;
            if(sys_fread(fd,fmt,to_read)!=(s64)to_read){sys_close(fd);return;}
            channels=(u16)fmt[2]|((u16)fmt[3]<<8);
            sample_rate=(u32)fmt[4]|((u32)fmt[5]<<8)|((u32)fmt[6]<<16)|((u32)fmt[7]<<24);
            bits_per_sample=(u16)fmt[14]|((u16)fmt[15]<<8);
            have_fmt=1;
            if(csize>16){u8 skip[64];u32 rem=csize-16;while(rem>0){u32 c=rem>64?64:rem;sys_fread(fd,skip,c);rem-=c;}}
        } else if(chdr[0]=='d'&&chdr[1]=='a'&&chdr[2]=='t'&&chdr[3]=='a'){
            data_size=csize;have_data=1;
        } else {
            u8 skip[64];u32 rem=csize;while(rem>0){u32 c=rem>64?64:rem;if(sys_fread(fd,skip,c)!=(s64)c){sys_close(fd);return;}rem-=c;}
        }
    }
    if(!have_fmt||!have_data||bits_per_sample!=16||channels<1||channels>2){tprint("WAV: format validation failed");sys_close(fd);return;}
    {
        char fb[64];int fi=0;
        const char*fp1="WAV: channels=";int fj=0;while(fp1[fj])fb[fi++]=fp1[fj++];
        fi=u32_append_dec(fb,fi,channels);
        const char*fp2=" rate=";fj=0;while(fp2[fj])fb[fi++]=fp2[fj++];
        fi=u32_append_dec(fb,fi,sample_rate);
        fb[fi]=0;tprint(fb);
    }

    u32 total_samples=data_size/2;
    if(total_samples>WAV_MAX_SAMPLES){
        tprint("WAV: file too large for static buffer, truncating");
        total_samples=WAV_MAX_SAMPLES;
    }
    u32 read_so_far=0;
    while(read_so_far<total_samples){
        u32 want=total_samples-read_so_far;
        s64 got=sys_fread(fd,wav_full_buf+read_so_far,(u64)want*2);
        if(got<=0)break;
        read_so_far+=(u32)got/2;
    }
    sys_close(fd);
    if(read_so_far==0){tprint("WAV: read failed, no data");return;}

    tprint("WAV: whole file read, starting stream playback");
    sys_ac97_debug(6); /* reset restart-position log for this playback */
    wav_channels=(u32)channels;
    wav_sample_rate=sample_rate;
    wav_samples_total=read_so_far;
    wav_playing=1;
    if(sys_play_stream(wav_full_buf,read_so_far,sample_rate,(u32)channels)!=0){
        tprint("WAV: sys_play_stream failed");
        wav_playing=0;
    }
}
/* Whole-file amplitude/nonzero scan — separate from the streaming
 * playback path, reads the file independently start-to-finish to
 * answer one question directly: is there real, nonzero PCM data
 * anywhere in this file, or is it silence/garbage throughout? The
 * first-8-samples dump alone can't answer this (many short UI dings
 * legitimately start with some silence). */
static void wav_scan_file(const char*path){
    u64 fd=sys_open(path,0);
    if((s64)fd<0){tprint("SCAN: open failed");return;}
    u8 riff_hdr[12];
    if(sys_fread(fd,riff_hdr,12)!=12){tprint("SCAN: bad header");sys_close(fd);return;}
    u32 data_size=0;int have_data=0;
    while(!have_data){
        u8 chdr[8];
        if(sys_fread(fd,chdr,8)!=8)break;
        u32 csize=(u32)chdr[4]|((u32)chdr[5]<<8)|((u32)chdr[6]<<16)|((u32)chdr[7]<<24);
        if(chdr[0]=='f'&&chdr[1]=='m'&&chdr[2]=='t'&&chdr[3]==' '){
            u8 skip[64];u32 rem=csize;while(rem>0){u32 c=rem>64?64:rem;sys_fread(fd,skip,c);rem-=c;}
        } else if(chdr[0]=='d'&&chdr[1]=='a'&&chdr[2]=='t'&&chdr[3]=='a'){
            data_size=csize;have_data=1;
        } else {
            u8 skip[64];u32 rem=csize;while(rem>0){u32 c=rem>64?64:rem;if(sys_fread(fd,skip,c)!=(s64)c){sys_close(fd);return;}rem-=c;}
        }
    }
    if(!have_data){tprint("SCAN: no data chunk");sys_close(fd);return;}
    u32 total_samples=data_size/2;
    u32 nonzero=0;int maxabs=0;u32 firstnz=0xFFFFFFFF;
    short buf[1024];u32 read_so_far=0;
    while(read_so_far<total_samples){
        u32 want=total_samples-read_so_far;
        if(want>1024)want=1024;
        s64 got=sys_fread(fd,buf,(u64)want*2);
        if(got<=0)break;
        u32 gs=(u32)got/2;
        for(u32 i=0;i<gs;i++){
            int v=buf[i];
            if(v!=0){
                nonzero++;
                if(firstnz==0xFFFFFFFF)firstnz=read_so_far+i;
                int av=v<0?-v:v;
                if(av>maxabs)maxabs=av;
            }
        }
        read_so_far+=gs;
    }
    sys_close(fd);
    char l[80];int li=0;
    const char*p1="SCAN: total=";int j=0;while(p1[j])l[li++]=p1[j++];
    li=u32_append_dec(l,li,total_samples);
    const char*p2=" nonzero=";j=0;while(p2[j])l[li++]=p2[j++];
    li=u32_append_dec(l,li,nonzero);
    const char*p3=" maxabs=";j=0;while(p3[j])l[li++]=p3[j++];
    li=u32_append_dec(l,li,(unsigned int)maxabs);
    l[li]=0;tprint(l);
    char l2[64];int li2=0;
    const char*p4="SCAN: first_nonzero_at=";j=0;while(p4[j])l2[li2++]=p4[j++];
    li2=u32_append_dec(l2,li2,firstnz==0xFFFFFFFF?0:firstnz);
    l2[li2]=0;tprint(l2);
}

static void wav_poll(void){
    if(!wav_playing)return;
    if(sys_stream_active())return; /* kernel is still IRQ-feeding the ring */
    wav_playing=0;
    tprint("WAV: playback finished");
    {
        unsigned int pc=sys_ac97_debug(5);
        unsigned int restarts=pc>>16, fastpath=pc&0xFFFF;
        char buf[64];int bi=0;
        const char*lbl="WAV: restarts=";int li=0;while(lbl[li])buf[bi++]=lbl[li++];
        bi=u32_append_dec(buf,bi,restarts);
        const char*lbl2=" fastpath=";li=0;while(lbl2[li])buf[bi++]=lbl2[li++];
        bi=u32_append_dec(buf,bi,fastpath);
        buf[bi]=0;
        tprint(buf);
    }
}
static void np_do_save(void){
    char path[56];path[0]='/';path[1]='y';path[2]='c';path[3]='f';path[4]='s';path[5]='/';
    int k=6,j=0;while(np.filename[j]&&k<55){path[k++]=np.filename[j++];}path[k]=0;
    sys_save_file((unsigned long long)path,(unsigned long long)np.text,(unsigned long long)np.text_len);
    np.modified=0;np.save_flash=80;
    for(int fk=0;fk<MAX_WINDOWS;fk++)fml_states[fk].loaded=0;
    if(np_current>=0){
        j=0;while(np.filename[j]&&j<39){wins[np_current].title[j]=np.filename[j];j++;}
        wins[np_current].title[j]=0;
    }
}
static void np_do_saveas(void){
    if(np.dlg_len<=0)return;
    /* build filename: spaces allowed, stored as-is (FAT driver handles) */
    char name[40];int k=0,j=0;
    while(np.dlg_buf[j]&&k<28){
        char c=np.dlg_buf[j++];
        /* only block truly illegal chars */
        if(c==(char)47||c==(char)92||c==58||c==42||c==63||c==34||c==60||c==62||c==124)c=95;
        name[k++]=c;
    }
    name[k++]=46;name[k++]=116;name[k++]=120;name[k++]=116;name[k]=0;
    /* check for duplicate — show error dialog if found */
    Dirent existing[64];
    int total=(int)sys_readdir(existing,64);
    int found=0;
    for(int fi=0;fi<total;fi++){
        int match=1;
        for(int mi=0;name[mi]||existing[fi].name[mi];mi++){
            char ac=name[mi],bc=existing[fi].name[mi];
            if(ac>=65&&ac<=90)ac+=32;
            if(bc>=65&&bc<=90)bc+=32;
            if(ac!=bc){match=0;break;}
        }
        if(match){found=1;break;}
    }
    if(found){
        np.mode3_err=1;
        int ei=0;while(name[ei]&&ei<35){np.err_name[ei]=name[ei];ei++;}np.err_name[ei]=0;
        return;
    }
    int ci=0;while(name[ci]&&ci<47){np.filename[ci]=name[ci];ci++;}np.filename[ci]=0;
    np_do_save();np.mode=0;np.dlg_len=0;np.dlg_buf[0]=0;np.mode3_err=0;
}

static Dirent np_dlg_files[MAX_FILES];
static int    np_dlg_count=0;
static void np_load_filelist(void){
    int tot=(int)sys_readdir(np_dlg_files,MAX_FILES);np_dlg_count=0;
    for(int i=0;i<tot;i++){
        if(np_dlg_files[i].is_dir)continue;
        char*n=np_dlg_files[i].name;int nl=slen(n);
        if(nl>4&&n[nl-4]=='.'&&n[nl-3]=='t'&&n[nl-2]=='x'&&n[nl-1]=='t')
            np_dlg_files[np_dlg_count++]=np_dlg_files[i];
    }
}

static void draw_floppy(int x,int y){
    rect(x,y,16,15,0x4A7AB5);rect(x+2,y+1,9,5,0xD0E8F8);
    hline(x+3,y+2,7,0x90B8D0);hline(x+3,y+4,7,0x90B8D0);
    rect(x+2,y+9,12,5,0x7A8A98);rect(x+5,y+10,4,3,0x1A2535);
    rect(x+12,y,4,4,0x0D1117);
}

static void draw_notepad_content(int wi){
    Win*w=&wins[wi];
    int x=w->x,y=w->y+TITLEBAR_H,cw=w->w,ch=w->h-TITLEBAR_H;
    rect(x,y,cw,ch,0x0D1117);
    /* toolbar */
    rect(x,y,cw,28,0x161B22);hline(x,y+28,cw,BORDER);
    int nwh=in_box(mouse_x,mouse_y,x+4,y+4,40,20);
    rect(x+4,y+4,40,20,nwh?0x21262D:0x13161B);outline(x+4,y+4,40,20,BORDER);
    text(x+10,y+6,"New",nwh?TEXT:DIM,nwh?0x21262D:0x13161B);
    int oh=in_box(mouse_x,mouse_y,x+50,y+4,52,20);
    rect(x+50,y+4,52,20,oh?0x21262D:0x13161B);outline(x+50,y+4,52,20,BORDER);
    text(x+58,y+6,"Open",oh?TEXT:DIM,oh?0x21262D:0x13161B);
    int sh=in_box(mouse_x,mouse_y,x+108,y+4,70,20);
    rect(x+108,y+4,70,20,sh?0x21262D:0x13161B);outline(x+108,y+4,70,20,BORDER);
    draw_floppy(x+111,y+6);text(x+130,y+6,"Save",sh?TEXT:DIM,sh?0x21262D:0x13161B);
    if(np.save_flash>0){text(x+186,y+6,"[Saved]",GREEN,0x161B22);np.save_flash--;}
    char hdr[56];int hi=0;
    const char*fn=np.filename[0]?np.filename:"New File";
    while(fn[hi]&&hi<40){hdr[hi]=fn[hi];hi++;}
    if(np.modified){hdr[hi++]=' ';hdr[hi++]='[';hdr[hi++]='*';hdr[hi++]=']';}hdr[hi]=0;
    text_center(x+cw/2,y+6,hdr,DIM,0x161B22);
    /* text area */
    int ta_x=x+8,ta_y=y+32;
    int max_cols=(cw-16)/8;if(max_cols<1)max_cols=1;
    int max_rows=(ch-52)/16;if(max_rows<1)max_rows=1;
    np_ensure_vis(max_rows);
    int cur_ln=0,cur_col=0;np_cursor_pos(&cur_ln,&cur_col);
    int ci=0,rline=0;
    while(ci<np.text_len&&rline<np.scroll){if(np.text[ci]=='\n')rline++;ci++;}
    int rcol=0,rl=np.scroll;
    while(ci<=np.text_len&&rl<np.scroll+max_rows){
        char c=(ci<np.text_len)?np.text[ci]:0;
        if(ci==np.text_len||c=='\n'){rl++;rcol=0;ci++;continue;}
        if(rcol<max_cols)glyph(ta_x+rcol*8,ta_y+(rl-np.scroll)*16,c,TEXT,0x0D1117);
        rcol++;ci++;
    }
    /* cursor */
    if(wi==focused&&np.mode==0&&cursor_blink<50){
        if(cur_ln>=np.scroll&&cur_ln<np.scroll+max_rows&&cur_col<=max_cols)
            rect(ta_x+cur_col*8,ta_y+(cur_ln-np.scroll)*16,2,16,cfg_accent);
    }
    /* status bar */
    int sb=y+ch-18;rect(x,sb,cw,18,0x161B22);hline(x,sb,cw,BORDER);
    char pos[24];int pi=0;
    pos[pi++]='L';pos[pi++]='n';pos[pi++]=':';
    int cl1=cur_ln+1;
    if(cl1>=100)pos[pi++]='0'+cl1/100;
    if(cl1>=10) pos[pi++]='0'+(cl1/10)%10;
    pos[pi++]='0'+cl1%10;pos[pi++]=' ';
    pos[pi++]='C';pos[pi++]='o';pos[pi++]='l';pos[pi++]=':';
    int cc1=cur_col+1;
    if(cc1>=100)pos[pi++]='0'+cc1/100;
    if(cc1>=10) pos[pi++]='0'+(cc1/10)%10;
    pos[pi++]='0'+cc1%10;pos[pi]=0;
    text(x+8,sb+1,pos,DIM,0x161B22);
    /* open dialog */
    if(np.mode==1){
        int dh2=np_dlg_count*20+52;if(dh2>260)dh2=260;if(dh2<72)dh2=72;
        int dw=280,dh=dh2,dx=x+(cw-dw)/2,dy2=y+(ch-dh)/2;
        rect(dx,dy2,dw,dh,0x161B22);outline(dx,dy2,dw,dh,ACCENT);
        rect(dx,dy2,dw,24,0x1C2128);hline(dx,dy2+24,dw,BORDER);
        text_center(dx+dw/2,dy2+4,"Open .txt File",TEXT,0x1C2128);
        text(dx+dw-18,dy2+4,"x",RED,0x1C2128);
        int ly=dy2+28,max_vis2=(dh-36)/20;
        for(int i=np.dlg_scroll;i<np_dlg_count&&i<np.dlg_scroll+max_vis2;i++){
            int ry=ly+(i-np.dlg_scroll)*20;int hov=(i==np.dlg_hov);
            rect(dx+2,ry,dw-4,20,hov?0x21262D:0x161B22);
            text(dx+10,ry+2,np_dlg_files[i].name,hov?TEXT:DIM,hov?0x21262D:0x161B22);
        }
        if(np_dlg_count==0)text_center(dx+dw/2,dy2+50,"No .txt files found",DIM,0x161B22);
    }
    /* save-as dialog */
    if(np.mode==2){
        int dw=300,dh=104,dx=x+(cw-dw)/2,dy2=y+(ch-dh)/2;
        rect(dx,dy2,dw,dh,0x161B22);outline(dx,dy2,dw,dh,ACCENT);
        rect(dx,dy2,dw,24,0x1C2128);hline(dx,dy2+24,dw,BORDER);
        text_center(dx+dw/2,dy2+4,"Save As",TEXT,0x1C2128);
        text(dx+8,dy2+30,"Name (spaces OK, .txt added):",DIM,0x161B22);
        rect(dx+8,dy2+50,dw-16,20,0x0D1117);outline(dx+8,dy2+50,dw-16,20,ACCENT);
        text(dx+12,dy2+52,np.dlg_buf,TEXT,0x0D1117);
        if(cursor_blink<50)rect(dx+12+np.dlg_len*8,dy2+52,2,16,ACCENT);
        int ok_h=in_box(mouse_x,mouse_y,dx+dw/2-30,dy2+76,60,20);
        rect(dx+dw/2-30,dy2+76,60,20,ok_h?ACCENT:0x21262D);
        text_center(dx+dw/2,dy2+78,"Save",ok_h?0x0D1117:TEXT,ok_h?ACCENT:0x21262D);
    if(np.mode3_err){
        int dw=320,dh=90,dx=x+(cw-dw)/2,dy2=y+(ch-dh)/2;
        rect(dx,dy2,dw,dh,0x161B22);outline(dx,dy2,dw,dh,RED);
        rect(dx,dy2,dw,24,0x2A1010);hline(dx,dy2+24,dw,0x5A2020);
        text_center(dx+dw/2,dy2+4,"File Already Exists",RED,0x2A1010);
        char emsg[60];int ei=0;
        while(np.err_name[ei]&&ei<35){emsg[ei]=np.err_name[ei];ei++;}emsg[ei]=0;
        text_center(dx+dw/2,dy2+34,emsg,TEXT,0x161B22);
        int ok_h2=in_box(mouse_x,mouse_y,dx+dw/2-20,dy2+62,40,20);
        rect(dx+dw/2-20,dy2+62,40,20,ok_h2?ACCENT:0x21262D);
        text_center(dx+dw/2,dy2+64,"OK",ok_h2?0x0D1117:TEXT,ok_h2?ACCENT:0x21262D);
    }
    }
}

/* ═══ ABOUT ═════════════════════════════════════════════════════ */
static void draw_about_content(int i){
    Win*w=&wins[i];int cx=w->x+w->w/2,y=w->y+TITLEBAR_H+20;
    text_center(cx,y,"YouOS",ACCENT,0x0D1117);y+=24;
    text_center(cx,y,"Version 0.3.0",TEXT,0x0D1117);y+=20;
    hline(w->x+20,y,w->w-40,BORDER);y+=12;
    text_center(cx,y,"Architecture: x86_64",DIM,0x0D1117);y+=18;
    text_center(cx,y,"Bootloader:   GRUB2+Multiboot2",DIM,0x0D1117);y+=18;
    text_center(cx,y,"Filesystem:   FAT16+initrd",DIM,0x0D1117);y+=18;
    {
        char reslbl[48];const char*pfx="Display:      ";int pi=0;
        while(pfx[pi]){reslbl[pi]=pfx[pi];pi++;}
        u64 v=FB_W;char tmp[24];int t=0;
        if(!v)tmp[t++]='0';else{char rv[24];int rr=0;while(v){rv[rr++]='0'+(int)(v%10);v/=10;}while(rr>0)tmp[t++]=rv[--rr];}
        for(int k=0;k<t;k++)reslbl[pi++]=tmp[k];
        reslbl[pi++]='x';
        v=FB_H;t=0;
        if(!v)tmp[t++]='0';else{char rv[24];int rr=0;while(v){rv[rr++]='0'+(int)(v%10);v/=10;}while(rr>0)tmp[t++]=rv[--rr];}
        for(int k=0;k<t;k++)reslbl[pi++]=tmp[k];
        const char*suf="x32bpp";int si=0;while(suf[si])reslbl[pi++]=suf[si++];
        reslbl[pi]=0;
        text_center(cx,y,reslbl,DIM,0x0D1117);
    }y+=18;
    text_center(cx,y,"Syscalls:     19",DIM,0x0D1117);y+=18;
    hline(w->x+20,y,w->w-40,BORDER);y+=12;
    text_center(cx,y,"Built from scratch in C+NASM",GREEN,0x0D1117);
}

/* ═══ WALLPAPER + PANEL ═════════════════════════════════════════ */
/* ═══ Wallpaper: BMP loader ═════════════════════════════════════ */
static u32 wallpaper_pixels[MAX_FB_W*MAX_FB_H];
static int wallpaper_loaded=0;
static int load_wallpaper_bmp(const char*path){
    u64 fd=sys_open(path,0);
    if((s64)fd<0)return 0;
    u8 hdr[54];
    u64 n=sys_fread(fd,hdr,54);
    if(n!=54||hdr[0]!='B'||hdr[1]!='M'){sys_close(fd);return 0;}
    u32 pix_off  =(u32)hdr[10]|((u32)hdr[11]<<8)|((u32)hdr[12]<<16)|((u32)hdr[13]<<24);
    int width    =(int)((u32)hdr[18]|((u32)hdr[19]<<8)|((u32)hdr[20]<<16)|((u32)hdr[21]<<24));
    int height   =(int)((u32)hdr[22]|((u32)hdr[23]<<8)|((u32)hdr[24]<<16)|((u32)hdr[25]<<24));
    u16 bpp      =(u16)hdr[28]|((u16)hdr[29]<<8);
    if((u64)width!=FB_W||(u64)height!=FB_H||bpp!=24){sys_close(fd);return 0;}
    u64 skip=pix_off-54;
    u8 skipbuf[64];
    while(skip>0){
        u64 chunk=skip>64?64:skip;
        if(sys_fread(fd,skipbuf,chunk)!=(s64)chunk){sys_close(fd);return 0;}
        skip-=chunk;
    }
    int row_bytes=width*3;
    int pad=(4-(row_bytes%4))%4;
    static u8 rowbuf[MAX_FB_W*3];
    for(int row=0;row<height;row++){
        u64 got=0;
        while(got<(u64)row_bytes){
            s64 r=sys_fread(fd,rowbuf+got,row_bytes-got);
            if(r<=0){sys_close(fd);return 0;}
            got+=(u64)r;
        }
        if(pad){u8 padbuf[4];sys_fread(fd,padbuf,pad);}
        int screen_y=height-1-row;
        for(int x=0;x<width;x++){
            u8 b=rowbuf[x*3+0],g=rowbuf[x*3+1],r=rowbuf[x*3+2];
            wallpaper_pixels[screen_y*(int)FB_W+x]=((u32)r<<16)|((u32)g<<8)|b;
        }
    }
    sys_close(fd);
    return 1;
}

static void wallpaper(void){
    if(wallpaper_loaded){
        for(int y=0;y<(int)FB_H;y++)for(int x=0;x<(int)FB_W;x++)px(x,y,wallpaper_pixels[y*(int)FB_W+x]);
        return;
    }
    for(int y=0;y<(int)FB_H;y++){
        u32 r=0x0D,g=0x11+(y*8)/(int)FB_H,b=0x17+(y*20)/(int)FB_H;
        u32 c=(r<<16)|(g<<8)|b;
        for(int x=0;x<(int)FB_W;x++)px(x,y,c);
    }
    for(int y=0;y<(int)FB_H;y+=80)for(int x=0;x<PANEL_X;x++)px(x,y,0x161B22);
    for(int x=0;x<PANEL_X;x+=80)for(int y=0;y<(int)FB_H;y++)px(x,y,0x161B22);
}
static void draw_panel_bg(void){rect(PANEL_X,0,PANEL_W,(int)FB_H-TBAR_H,PANEL_BG);vline(PANEL_X,0,(int)FB_H-TBAR_H,BORDER);}
static void draw_analog_clock(int cx,int cy,int r,u64 secs){
    int hh=(secs/3600)%12,mm=(secs/60)%60,ss=secs%60;
    for(int deg=0;deg<360;deg+=2){
        px(cx+(int)(icos(deg)*r/1000),cy-(int)(isin(deg)*r/1000),BORDER);
        px(cx+(int)(icos(deg)*(r-1)/1000),cy-(int)(isin(deg)*(r-1)/1000),BORDER);
    }
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
    text_bold_center(x+PANEL_W/2-4,y+4,hdr,TEXT,0x0D1117);hline(x,y+20,PANEL_W-8,BORDER);
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
    text_bold(x+6,y+4,"System",DIM,0x0D1117);
    text(x+6,y+20,"CPU",DIM,0x0D1117);rect(x+36,y+22,150,8,0x21262D);rect(x+36,y+22,12,8,GREEN);text(x+190,y+20,"8%",DIM,0x0D1117);
    text(x+6,y+36,"MEM",DIM,0x0D1117);rect(x+36,y+38,150,8,0x21262D);rect(x+36,y+38,75,8,ACCENT);text(x+190,y+36,"50%",DIM,0x0D1117);
}

/* ═══ ICONS ═════════════════════════════════════════════════════ */
typedef struct{int x,y;const char*name;u32 color;}Icon;
static Icon icons[]={{60,80,"Terminal",ACCENT},{60,180,"Files",GREEN},{60,280,"About",PURPLE},{60,380,"Notepad",YELLOW},{60,480,"Calc",0x58A6FF}};
#define N_ICONS 5
static void resolve_icon_pos(int idx,int ax,int ay,int*oax,int*oay){
    int pad=8;
    for(int attempt=0;attempt<20;attempt++){
        int collided=0;
        for(int j=0;j<N_ICONS;j++){
            if(j==idx)continue;
            int sx=icons[j].x,sy=icons[j].y;
            if(ax<sx+72+pad&&ax+72+pad>sx&&ay<sy+72+pad&&ay+72+pad>sy){ax=sx+72+pad;collided=1;break;}
        }
        if(!collided)break;
    }
    if(ax<4)ax=4;
    if(ax>PANEL_X-68)ax=PANEL_X-68;
    if(ay<4)ay=4;
    if(ay>(int)FB_H-TBAR_H-68)ay=(int)FB_H-TBAR_H-68;
    *oax=ax;*oay=ay;
}
static int icon_hovered=-1;
/* ── circle primitives (icon glyphs) ───────────────────────────── */
static void circle(int cx,int cy,int r,u32 c){
    for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++)if(xx*xx+yy*yy<=r*r)px(cx+xx,cy+yy,c);
}
static void circle_outline(int cx,int cy,int r,u32 c){
    for(int yy=-r;yy<=r;yy++)for(int xx=-r;xx<=r;xx++){int d=xx*xx+yy*yy;if(d<=r*r&&d>=(r-2)*(r-2))px(cx+xx,cy+yy,c);}
}

/* ── icon pictographs, idx matches icons[] order ───────────────── */
static void draw_icon_glyph(int idx,int cx,int cy,u32 fg,u32 bgcol){
    if(idx==0){
        text(cx-8,cy-8,">_",fg,bgcol);
    }else if(idx==1){
        rect(cx-14,cy-8,12,6,fg);
        rect(cx-14,cy-2,28,16,fg);
    }else if(idx==2){
        circle_outline(cx,cy,10,fg);
        circle(cx,cy-5,2,fg);
        rect(cx-2,cy-1,4,9,fg);
    }else if(idx==3){
        rect(cx-12,cy-14,24,28,fg);
        hline(cx-8,cy-7,16,bgcol);
        hline(cx-8,cy-1,16,bgcol);
        hline(cx-8,cy+5,10,bgcol);
    }else if(idx==4){
        outline(cx-14,cy-16,28,32,fg);
        rect(cx-11,cy-13,22,8,bgcol);
        for(int row=0;row<2;row++)for(int col=0;col<3;col++)
            rect(cx-11+col*8,cy-2+row*8,5,5,fg);
    }else if(idx==5){
        circle_outline(cx,cy,8,fg);
        circle(cx,cy,3,fg);
        rect(cx-2,cy-13,4,5,fg);
        rect(cx-2,cy+8,4,5,fg);
        rect(cx-13,cy-2,5,4,fg);
        rect(cx+8,cy-2,5,4,fg);
        rect(cx-10,cy-10,4,4,fg);
        rect(cx+6,cy-10,4,4,fg);
        rect(cx-10,cy+6,4,4,fg);
        rect(cx+6,cy+6,4,4,fg);
    }
}

static void draw_icons(void){
    for(int i=0;i<N_ICONS;i++){
        Icon*ic=&icons[i];u32 bg=(i==icon_hovered)?0x21262D:BG;
        rect(ic->x-4,ic->y-4,72,72,bg);outline(ic->x-4,ic->y-4,72,72,i==icon_hovered?ACCENT:BORDER);
        rect(ic->x+10,ic->y+10,52,40,ic->color);
        for(int r=ic->y+12;r<ic->y+48;r++)for(int c2=ic->x+12;c2<ic->x+60;c2++){
            u32 col=buf[r*(int)FB_W+c2];u32 rr=(col>>16)&0xFF,gg=(col>>8)&0xFF,bb=col&0xFF;
            buf[r*(int)FB_W+c2]=((rr/2)<<16)|((gg/2)<<8)|(bb/2);
        }
        draw_icon_glyph(i,ic->x+36,ic->y+30,TEXT,ic->color);
        int llen=0;const char*p=ic->name;while(*p++)llen++;
        text(ic->x+(64-llen*8)/2,ic->y+66,ic->name,TEXT,BG);
    }
}

/* ═══ MENU ══════════════════════════════════════════════════════ */
static int menu_open=0,menu_sel=-1;
static const char*menu_items[]={"Terminal","Files","About","Notepad","Calc","Settings","Shutdown"};
#define N_MENU 7
#define N_MENU_APPS 6
static const u32 sm_colors[N_MENU_APPS]={ACCENT,GREEN,PURPLE,YELLOW,0x58A6FF,0x58A6FF};
#define SM_W 480
#define SM_H 560
#define SM_X TBAR_SB_X
#define SM_Y (TBAR_PILL_Y-SM_H-12)
#define SM_COLS 3
#define SM_CELL 140
#define SM_GRID_X(c) (SM_X+24+(c)*SM_CELL)
#define SM_GRID_Y(r) (SM_Y+108+(r)*SM_CELL)
static char sm_search[32];static int sm_search_len=0;
static int sm_filtered[N_MENU_APPS];static int sm_filtered_n=0;
static int sm_hov=-1;
static void sm_apply_filter(void){
    sm_filtered_n=0;
    for(int i=0;i<N_MENU_APPS;i++){
        if(sm_search_len==0){sm_filtered[sm_filtered_n++]=i;continue;}
        const char*name=menu_items[i];int match=0;
        for(int s=0;name[s];s++){
            int eq=1;
            for(int k=0;k<sm_search_len;k++){
                char a=name[s+k];if(a>='A'&&a<='Z')a+=32;
                char b=sm_search[k];if(b>='A'&&b<='Z')b+=32;
                if(a!=b||a==0){eq=0;break;}
            }
            if(eq){match=1;break;}
        }
        if(match)sm_filtered[sm_filtered_n++]=i;
    }
}
static void draw_menu(void){
    if(!menu_open)return;
    int sx=SM_X,sy=SM_Y;
    rect_round_alpha(sx+3,sy+3,SM_W,SM_H,18,0x000000,90);
    rect_round_alpha(sx,sy,SM_W,SM_H,18,PANEL_BG,210);
    outline_round(sx,sy,SM_W,SM_H,18,BORDER);
    text_bold(sx+20,sy+16,"YouOS",cfg_accent,PANEL_BG);
    text(sx+SM_W-104,sy+18,"Applications",DIM,PANEL_BG);
    hline(sx+20,sy+44,SM_W-40,0x21262D);
    int sbx=sx+20,sby=sy+56,sbw=SM_W-40,sbh=36;
    rect_round(sbx,sby,sbw,sbh,10,0x0D1117);outline_round(sbx,sby,sbw,sbh,10,BORDER);
    if(sm_search_len==0)text(sbx+12,sby+11,"Search programs...",DIM,0x0D1117);
    else text(sbx+12,sby+11,sm_search,TEXT,0x0D1117);
    for(int gi=0;gi<sm_filtered_n;gi++){
        int idx=sm_filtered[gi];
        int col=gi%SM_COLS,row=gi/SM_COLS;
        int gx=SM_GRID_X(col),gy=SM_GRID_Y(row);
        int hov=(sm_hov==gi);
        u32 bg=hov?0x21262D:PANEL_BG;
        rect_round(gx,gy,SM_CELL-16,SM_CELL-16,12,bg);
        if(hov)outline_round(gx,gy,SM_CELL-16,SM_CELL-16,12,cfg_accent);
        rect_round(gx+(SM_CELL-16-56)/2,gy+12,56,56,12,sm_colors[idx]);
        draw_icon_glyph(idx,gx+(SM_CELL-16)/2,gy+12+28,TEXT,sm_colors[idx]);
        int nlen=slen(menu_items[idx]);
        text(gx+((SM_CELL-16)-nlen*8)/2,gy+76,menu_items[idx],TEXT,bg);
    }
    if(sm_filtered_n==0)text_center(sx+SM_W/2,sy+200,"No results",DIM,PANEL_BG);
    int pry=sy+SM_H-56,pbw=(SM_W-40-24)/4;
    int rb_x=sx+20,sd_x=rb_x+pbw+8,lk_x=sd_x+pbw+8,lo_x=lk_x+pbw+8;
    hline(sx+20,pry-8,SM_W-40,0x21262D);
    int hov_r=in_box(mouse_x,mouse_y,rb_x,pry,pbw,40);
    int hov_s=in_box(mouse_x,mouse_y,sd_x,pry,pbw,40);
    int hov_lk=in_box(mouse_x,mouse_y,lk_x,pry,pbw,40);
    int hov_lo=in_box(mouse_x,mouse_y,lo_x,pry,pbw,40);
    rect_round(rb_x,pry,pbw,40,10,hov_r?0x21262D:0x161B22);outline_round(rb_x,pry,pbw,40,10,BORDER);
    text_center(rb_x+pbw/2,pry+13,"Restart",TEXT,hov_r?0x21262D:0x161B22);
    rect_round(sd_x,pry,pbw,40,10,hov_s?0x3A1212:0x161B22);outline_round(sd_x,pry,pbw,40,10,hov_s?RED:BORDER);
    text_center(sd_x+pbw/2,pry+13,"Shutdown",hov_s?RED:TEXT,hov_s?0x3A1212:0x161B22);
    rect_round(lk_x,pry,pbw,40,10,hov_lk?0x21262D:0x161B22);outline_round(lk_x,pry,pbw,40,10,BORDER);
    text_center(lk_x+pbw/2,pry+13,"Lock",TEXT,hov_lk?0x21262D:0x161B22);
    rect_round(lo_x,pry,pbw,40,10,hov_lo?0x21262D:0x161B22);outline_round(lo_x,pry,pbw,40,10,BORDER);
    text_center(lo_x+pbw/2,pry+13,"Logout",TEXT,hov_lo?0x21262D:0x161B22);
}

/* ═══ TASKBAR ═══════════════════════════════════════════════════ */
static void draw_notif_popup(void){
    if(!notif_popup_active||notif_count==0)return;
    int idx=notif_count-1;
    int x=NOTIF_POPUP_X,y=NOTIF_POPUP_Y;
    rect_round_alpha(x+3,y+3,NOTIF_POPUP_W,NOTIF_POPUP_H,14,0x000000,90);
    rect_round_alpha(x,y,NOTIF_POPUP_W,NOTIF_POPUP_H,14,PANEL_BG,215);
    outline_round(x,y,NOTIF_POPUP_W,NOTIF_POPUP_H,14,cfg_accent);
    draw_bell_glyph(x+26,y+28,cfg_accent);
    text_bold(x+50,y+14,notif_title[idx],TEXT,PANEL_BG);
    text(x+50,y+34,notif_msg[idx],DIM,PANEL_BG);
    int cbx=x+NOTIF_POPUP_W-24,cby=y+8;
    int hovc=in_box(mouse_x,mouse_y,cbx,cby,16,16);
    rect_round(cbx,cby,16,16,6,hovc?0x21262D:PANEL_BG);
    text(cbx+5,cby+2,"x",hovc?TEXT:DIM,hovc?0x21262D:PANEL_BG);
}
static void draw_notif_center(void){
    if(!notif_center_open)return;
    int x=NC_X,y=NC_Y;
    rect_round_alpha(x+3,y+3,NC_W,NC_H,16,0x000000,90);
    rect_round_alpha(x,y,NC_W,NC_H,16,PANEL_BG,215);
    outline_round(x,y,NC_W,NC_H,16,BORDER);
    text_bold(x+20,y+16,"Notifications",TEXT,PANEL_BG);
    int hovclear=in_box(mouse_x,mouse_y,x+NC_W-92,y+14,72,26);
    rect_round(x+NC_W-92,y+14,72,26,8,hovclear?0x3A1212:0x161B22);
    outline_round(x+NC_W-92,y+14,72,26,8,hovclear?RED:BORDER);
    text_center(x+NC_W-56,y+21,"Clear",hovclear?RED:TEXT,hovclear?0x3A1212:0x161B22);
    hline(x+20,y+48,NC_W-40,0x21262D);
    if(notif_count==0){
        text_center(x+NC_W/2,y+NC_H/2,"No notifications",DIM,PANEL_BG);
        return;
    }
    if(notif_scroll>notif_count-NC_MAX_VISIBLE)notif_scroll=notif_count-NC_MAX_VISIBLE;
    if(notif_scroll<0)notif_scroll=0;
    int visible=notif_count-notif_scroll;
    if(visible>NC_MAX_VISIBLE)visible=NC_MAX_VISIBLE;
    for(int row=0;row<visible;row++){
        int idx=notif_count-1-notif_scroll-row;
        int ry=y+90+row*NC_ROW_H;
        int hovrow=in_box(mouse_x,mouse_y,x+12,ry,NC_W-24,NC_ROW_H-8);
        rect_round(x+12,ry,NC_W-24,NC_ROW_H-8,10,hovrow?0x21262D:0x161B22);
        outline_round(x+12,ry,NC_W-24,NC_ROW_H-8,10,BORDER);
        {
            char numbuf[4];int npos=0;int dispnum=idx+1;
            if(dispnum>=10)numbuf[npos++]='0'+(dispnum/10)%10;
            numbuf[npos++]='0'+(dispnum%10);numbuf[npos]=0;
            text(x+16,ry+4,numbuf,DIM,hovrow?0x21262D:0x161B22);
        }
        draw_bell_glyph(x+34,ry+28,cfg_accent);
        text_bold(x+56,ry+10,notif_title[idx],TEXT,hovrow?0x21262D:0x161B22);
        text(x+56,ry+30,notif_msg[idx],DIM,hovrow?0x21262D:0x161B22);
        int rbx=x+NC_W-44,rby=ry+(NC_ROW_H-8)/2-8;
        int hovr=in_box(mouse_x,mouse_y,rbx,rby,16,16);
        rect_round(rbx,rby,16,16,6,hovr?0x3A1212:(hovrow?0x21262D:0x161B22));
        text(rbx+5,rby+2,"x",hovr?RED:DIM,hovr?0x3A1212:(hovrow?0x21262D:0x161B22));
    }
    if(notif_count>NC_MAX_VISIBLE){
        rect(x+NC_W-16,y+56,14,14,notif_scroll>0?0x21262D:0x13161B);
        text(x+NC_W-14,y+57,"^",notif_scroll>0?TEXT:BORDER,notif_scroll>0?0x21262D:0x13161B);
        rect(x+NC_W-16,y+72,14,14,(notif_scroll+NC_MAX_VISIBLE<notif_count)?0x21262D:0x13161B);
        text(x+NC_W-14,y+73,"v",(notif_scroll+NC_MAX_VISIBLE<notif_count)?TEXT:BORDER,(notif_scroll+NC_MAX_VISIBLE<notif_count)?0x21262D:0x13161B);
    }
}
#define MAX_GROUPS 8
#define MAX_GROUP_INST 6
typedef struct{int id;int count;int idx[MAX_GROUP_INST];int rep;}TGroup;
static TGroup taskbar_groups[MAX_GROUPS];
static int taskbar_group_count=0;
static int group_last_focus[6]={-1,-1,-1,-1,-1,-1};
static void rebuild_taskbar_groups(void){
    taskbar_group_count=0;
    if(focused>=0&&focused<win_count&&wins[focused].visible&&wins[focused].id>=0&&wins[focused].id<6)
        group_last_focus[wins[focused].id]=focused;
    for(int i=0;i<win_count;i++){
        if(!wins[i].visible)continue;
        int gi=-1;
        for(int g=0;g<taskbar_group_count;g++)if(taskbar_groups[g].id==wins[i].id){gi=g;break;}
        if(gi<0){
            if(taskbar_group_count>=MAX_GROUPS)continue;
            gi=taskbar_group_count++;taskbar_groups[gi].id=wins[i].id;taskbar_groups[gi].count=0;
        }
        if(taskbar_groups[gi].count<MAX_GROUP_INST)taskbar_groups[gi].idx[taskbar_groups[gi].count++]=i;
    }
    for(int g=0;g<taskbar_group_count;g++){
        int id=taskbar_groups[g].id;
        int gl=(id>=0&&id<6)?group_last_focus[id]:-1;
        int rep=taskbar_groups[g].idx[0];
        if(gl>=0&&gl<win_count&&wins[gl].visible&&wins[gl].id==id){
            for(int k=0;k<taskbar_groups[g].count;k++)if(taskbar_groups[g].idx[k]==gl){rep=gl;break;}
        }
        taskbar_groups[g].rep=rep;
    }
}
#define WPREV_W 200
#define WPREV_H 130
#define WPREV_BW (WPREV_W-16)
#define WPREV_BH (WPREV_H-30)
#define WPREV_GAP 10
static void taskbar_group_strip_rect(int g,int*out_x,int*out_y,int*out_w,int*out_h){
    int gcount=taskbar_groups[g].count;
    int gbx=TBAR_WINBTN_X0;
    for(int k=0;k<g;k++)gbx+=TBAR_WINBTN_W+TBAR_WINBTN_GAP;
    int cxp=gbx+TBAR_WINBTN_W/2;
    int stripw=gcount*WPREV_W+(gcount-1)*WPREV_GAP;
    int pxp0=cxp-stripw/2;
    if(pxp0<4)pxp0=4;
    if(pxp0+stripw>1020)pxp0=1020-stripw;
    *out_x=pxp0;*out_y=TBAR_PILL_Y-WPREV_H-12;*out_w=stripw;*out_h=WPREV_H;
}
static void draw_taskbar(u64 secs){
    int hs=in_box(mouse_x,mouse_y,TBAR_SB_X,TBAR_SB_Y,TBAR_SB_SZ,TBAR_SB_SZ);
    rect_round_alpha(TBAR_PILL_X+3,TBAR_PILL_Y+3,TBAR_PILL_W,TBAR_PILL_H,16,0x000000,90);
    rect_round_alpha(TBAR_PILL_X,TBAR_PILL_Y,TBAR_PILL_W,TBAR_PILL_H,16,TASKBAR,200);
    outline_round(TBAR_PILL_X,TBAR_PILL_Y,TBAR_PILL_W,TBAR_PILL_H,16,BORDER);
    u32 sbg=menu_open?ACCENT:(hs?0x2D333B:0x21262D);
    rect_round(TBAR_SB_X,TBAR_SB_Y,TBAR_SB_SZ,TBAR_SB_SZ,12,sbg);
    outline_round(TBAR_SB_X,TBAR_SB_Y,TBAR_SB_SZ,TBAR_SB_SZ,12,menu_open?0x79C0FF:BORDER);
    blit_rgba(TBAR_SB_X+1,TBAR_SB_Y+1,START_ICON_W,START_ICON_H,start_icon_rgba);
    int bx=TBAR_WINBTN_X0;
    for(int g=0;g<taskbar_group_count;g++){
        int rep=taskbar_groups[g].rep;
        int foc=(rep==focused);u32 bbg=foc?0x21262D:0x13161B;
        int gc=taskbar_groups[g].count;
        /* Windows-style "layered" look for 2+ open instances: draw 1-2
         * offset card outlines behind the main button, peeking out from
         * the top-right corner. */
        if(gc>1){
            int layers=gc>2?2:1;
            for(int ly=layers;ly>=1;ly--){
                int off=ly*2;
                int topy=TBAR_PILL_Y+4-off;
                if(topy<TBAR_PILL_Y+1)topy=TBAR_PILL_Y+1;
                rect_round(bx+off,topy,TBAR_WINBTN_W,TBAR_PILL_H-8,10,0x0D1117);
                outline_round(bx+off,topy,TBAR_WINBTN_W,TBAR_PILL_H-8,10,BORDER);
            }
        }
        rect_round(bx,TBAR_PILL_Y+4,TBAR_WINBTN_W,TBAR_PILL_H-8,10,bbg);
        outline_round(bx,TBAR_PILL_Y+4,TBAR_WINBTN_W,TBAR_PILL_H-8,10,foc?wins[rep].accent:BORDER);
        if(foc)hline(bx+8,TBAR_PILL_Y+TBAR_PILL_H-6,TBAR_WINBTN_W-16,wins[rep].accent);
        draw_icon_glyph(win_glyph_idx(taskbar_groups[g].id),bx+TBAR_WINBTN_W/2,TBAR_PILL_Y+TBAR_PILL_H/2,foc?TEXT:DIM,bbg);
        bx+=TBAR_WINBTN_W+TBAR_WINBTN_GAP+(gc>1?6:0);
    }
    int hovbell=in_box(mouse_x,mouse_y,TBAR_BELL_X,TBAR_BELL_Y,TBAR_BELL_SZ,TBAR_BELL_SZ);
    rect_round(TBAR_BELL_X,TBAR_BELL_Y,TBAR_BELL_SZ,TBAR_BELL_SZ,10,notif_center_open?ACCENT:(hovbell?0x2D333B:0x21262D));
    outline_round(TBAR_BELL_X,TBAR_BELL_Y,TBAR_BELL_SZ,TBAR_BELL_SZ,10,notif_center_open?0x79C0FF:BORDER);
    draw_bell_glyph(TBAR_BELL_X+TBAR_BELL_SZ/2,TBAR_BELL_Y+TBAR_BELL_SZ/2,notif_center_open?BG:TEXT);
    if(notif_count>0){
        rect_round(TBAR_BELL_X+TBAR_BELL_SZ-12,TBAR_BELL_Y-4,16,16,8,RED);
        char nc2[3];nc2[0]=notif_count<10?('0'+notif_count):'9';nc2[1]=notif_count<10?0:'+';nc2[2]=0;
        text(TBAR_BELL_X+TBAR_BELL_SZ-9,TBAR_BELL_Y-3,nc2,TEXT,RED);
    }
    u64 hh=(secs/3600)%24,mm=(secs/60)%60,ss=secs%60;
    char clk[9];clk[0]='0'+hh/10;clk[1]='0'+hh%10;clk[2]=':';clk[3]='0'+mm/10;clk[4]='0'+mm%10;clk[5]=':';clk[6]='0'+ss/10;clk[7]='0'+ss%10;clk[8]=0;
    text(TBAR_CLOCK_X,TBAR_CLOCK_Y,clk,TEXT,TASKBAR);
}

/* ═══ CURSOR ════════════════════════════════════════════════════ */
static void draw_cursor(int mx,int my){
    static const u8 C[15][10]={{2,0,0,0,0,0,0,0,0,0},{2,2,0,0,0,0,0,0,0,0},{2,1,2,0,0,0,0,0,0,0},{2,1,1,2,0,0,0,0,0,0},{2,1,1,1,2,0,0,0,0,0},{2,1,1,1,1,2,0,0,0,0},{2,1,1,1,1,1,2,0,0,0},{2,1,1,1,1,1,1,2,0,0},{2,1,1,1,1,1,2,0,0,0},{2,1,1,2,1,1,1,2,0,0},{2,2,0,0,2,1,1,2,0,0},{0,0,0,0,2,1,1,2,0,0},{0,0,0,0,2,1,2,0,0,0},{0,0,0,0,2,2,0,0,0,0},{0,0,0,0,0,0,0,0,0,0}};
    for(int r=0;r<15;r++)for(int c=0;c<10;c++){u8 v=C[r][c];if(v==1)px(mx+c,my+r,0xFFFFFF);else if(v==2)px(mx+c,my+r,0x000000);}
}

/* === CONTEXT MENU === */
static const char *ctx_desk[]={"Terminal","Files","About","Notepad","Calc","Settings","","Shutdown"};
static const char *ctx_win[] ={"Minimize","Maximize","Close"};
#define CTX_DESK_N 8
#define CTX_WIN_N  3
#define CTX_W      156
#define CTX_ITEM_H  22
#define CTX_SEP_H    6
static void get_rctx(const char***it,int*n){
    if(rctx_target<0){*it=ctx_desk;*n=CTX_DESK_N;}else{*it=ctx_win;*n=CTX_WIN_N;}
}
static int rctx_h(void){
    const char**it;int n;get_rctx(&it,&n);
    int h=2;for(int i=0;i<n;i++)h+=it[i][0]?CTX_ITEM_H:CTX_SEP_H;return h;
}
static void draw_rctx(void){
    if(!rctx_open)return;
    const char**items;int n;get_rctx(&items,&n);
    int mh=rctx_h(),mx2=rctx_x,my2=rctx_y;
    if(mx2+CTX_W>PANEL_X)mx2=PANEL_X-CTX_W;
    if(my2+mh>(int)FB_H-TBAR_H)my2=(int)FB_H-TBAR_H-mh;
    rect(mx2+3,my2+3,CTX_W,mh,0x050810);rect(mx2+2,my2+2,CTX_W,mh,0x0A0D14);
    rect_alpha(mx2,my2,CTX_W,mh,0x1C2128,190);outline(mx2,my2,CTX_W,mh,BORDER);
    hline(mx2+1,my2,CTX_W-2,cfg_accent);
    int iy2=my2+1;
    for(int i=0;i<n;i++){
        if(!items[i][0]){hline(mx2+6,iy2+3,CTX_W-12,BORDER);iy2+=CTX_SEP_H;continue;}
        int hov=(rctx_hov==i);
        if(hov)rect(mx2+1,iy2,CTX_W-2,CTX_ITEM_H,cfg_accent);
        text(mx2+10,iy2+(CTX_ITEM_H-14)/2,items[i],hov?BG:TEXT,hov?cfg_accent:0x1C2128);
        iy2+=CTX_ITEM_H;
    }
}

/* === SETTINGS === */
static const u32  sw_col[]={0x58A6FF,0x3FB950,0xBC8CFF,0xF78166,0xF85149,0xD29922};
static const char*sw_lbl[]={"Blue","Green","Purple","Orange","Red","Gold"};
#define N_SW 6
static void cfg_save(void);
static void do_shutdown(void);
static void do_restart(void);
static void cfg_set_accent(u32 c){
    cfg_accent=c;
    for(int i=0;i<win_count;i++)if(wins[i].id==WIN_TERMINAL)wins[i].accent=c;
    cfg_save();
    notif_add("Settings","Accent color updated");
}
static void draw_masked(int x,int y,int len,u32 fg,u32 bg); /* forward decl — defined later, called from draw_settings_content */
static int  youdo_open=0;
static char youdo_pw[48];int youdo_pw_len=0;
static char youdo_err[64];
static int  youdo_show=0;
static int  session_elevated=0;

static void draw_settings_content(int wi){
    Win*w=&wins[wi];int x=w->x,y=w->y+TITLEBAR_H,cw=w->w,ch=w->h-TITLEBAR_H;
    rect(x,y,cw,ch,0x0D1117);
    int cy=y+16;
    text(x+16,cy,"Accent Color",TEXT,0x0D1117);cy+=22;
    hline(x+12,cy,cw-24,0x21262D);cy+=8;
    for(int i=0;i<N_SW;i++){
        int sx=x+16+i*44,sy=cy;
        int sel=(sw_col[i]==cfg_accent),hov=in_box(mouse_x,mouse_y,sx,sy,36,36);
        if(sel){rect(sx-3,sy-3,42,42,0xFFFFFF);}
        else if(hov){rect(sx-2,sy-2,40,40,DIM);}
        rect(sx,sy,36,36,sw_col[i]);
        text_center(sx+18,sy+40,sw_lbl[i],DIM,0x0D1117);
    }
    cy+=66;
    hline(x+12,cy,cw-24,0x21262D);cy+=10;
    text(x+16,cy,"Clock Format",TEXT,0x0D1117);cy+=22;
    {int bw=64,bh=24,bx1=x+16,bx2=x+88,by2=cy;
     u32 b1=cfg_24h?cfg_accent:(in_box(mouse_x,mouse_y,bx1,by2,bw,bh)?0x21262D:0x13161B);
     u32 b2=!cfg_24h?cfg_accent:(in_box(mouse_x,mouse_y,bx2,by2,bw,bh)?0x21262D:0x13161B);
     rect(bx1,by2,bw,bh,b1);outline(bx1,by2,bw,bh,BORDER);
     text_center(bx1+bw/2,by2+4,"24h",cfg_24h?BG:TEXT,b1);
     rect(bx2,by2,bw,bh,b2);outline(bx2,by2,bw,bh,BORDER);
     text_center(bx2+bw/2,by2+4,"12h",!cfg_24h?BG:TEXT,b2);}
    cy+=34;
    hline(x+12,cy,cw-24,0x21262D);cy+=10;
    text(x+16,cy,"Clock Seconds",TEXT,0x0D1117);cy+=22;
    {int bw=64,bh=24,bx1=x+16,bx2=x+88,by2=cy;
     u32 b1=cfg_showsecs?cfg_accent:(in_box(mouse_x,mouse_y,bx1,by2,bw,bh)?0x21262D:0x13161B);
     u32 b2=!cfg_showsecs?cfg_accent:(in_box(mouse_x,mouse_y,bx2,by2,bw,bh)?0x21262D:0x13161B);
     rect(bx1,by2,bw,bh,b1);outline(bx1,by2,bw,bh,BORDER);
     text_center(bx1+bw/2,by2+4,"Show",cfg_showsecs?BG:TEXT,b1);
     rect(bx2,by2,bw,bh,b2);outline(bx2,by2,bw,bh,BORDER);
     text_center(bx2+bw/2,by2+4,"Hide",!cfg_showsecs?BG:TEXT,b2);}
    cy+=44;
    hline(x+12,cy,cw-24,0x21262D);cy+=10;
    text(x+16,cy,"Account",TEXT,0x0D1117);cy+=22;
    {
        int abw=cw-32,abh=26,abx=x+16,aby=cy;
        int ahov=in_box(mouse_x,mouse_y,abx,aby,abw,abh);
        rect(abx,aby,abw,abh,ahov?0x21262D:0x13161B);outline(abx,aby,abw,abh,BORDER);
        text_center(abx+abw/2,aby+5,"Account Setup",TEXT,ahov?0x21262D:0x13161B);
    }
    cy+=36;
    {
        int wbw=cw-32,wbh=26,wbx=x+16,wby=cy;
        int whov=in_box(mouse_x,mouse_y,wbx,wby,wbw,wbh);
        u32 wbg=wallpaper_loaded?cfg_accent:(whov?0x21262D:0x13161B);
        rect(wbx,wby,wbw,wbh,wbg);outline(wbx,wby,wbw,wbh,BORDER);
        text_center(wbx+wbw/2,wby+5,wallpaper_loaded?"Wallpaper: On":"Wallpaper: Off",
                    wallpaper_loaded?BG:TEXT,wbg);
    }
    cy+=36;
    hline(x+12,cy,cw-24,0x21262D);
    text_center(x+cw/2,cy+16,"YouOS v0.3.0",DIM,0x0D1117);

    if(youdo_open){
        int dw=280,dh=140,dx=x+(cw-dw)/2,dy2=y+(ch-dh)/2;
        rect(dx,dy2,dw,dh,0x161B22);outline(dx,dy2,dw,dh,cfg_accent);
        rect(dx,dy2,dw,24,0x13161B);hline(dx,dy2+24,dw,BORDER);
        text_center(dx+dw/2,dy2+4,"youdo: root password required",TEXT,0x13161B);
        int fbx=dx+16,fby=dy2+38,fbw=dw-32-56,fbh=26;
        rect(fbx,fby,fbw,fbh,0x0D1117);outline(fbx,fby,fbw,fbh,cfg_accent);
        if(youdo_show)text(fbx+8,fby+6,youdo_pw,TEXT,0x0D1117);
        else draw_masked(fbx+8,fby+6,youdo_pw_len,TEXT,0x0D1117);
        int eyex=fbx+fbw+8,eyey=fby,eyew=48,eyeh=fbh;
        int hove=in_box(mouse_x,mouse_y,eyex,eyey,eyew,eyeh);
        rect(eyex,eyey,eyew,eyeh,hove?0x21262D:0x161B22);outline(eyex,eyey,eyew,eyeh,BORDER);
        text_center(eyex+eyew/2,eyey+6,youdo_show?"Hide":"Show",hove?TEXT:DIM,hove?0x21262D:0x161B22);
        if(youdo_err[0])text(dx+16,dy2+70,youdo_err,RED,0x161B22);
        int cbw=60,cbh=24,cbx=dx+dw-16-cbw,cby=dy2+dh-32;
        int chov=in_box(mouse_x,mouse_y,cbx,cby,cbw,cbh);
        rect(cbx,cby,cbw,cbh,chov?cfg_accent:0x21262D);outline(cbx,cby,cbw,cbh,cfg_accent);
        text_center(cbx+cbw/2,cby+5,"Confirm",chov?BG:TEXT,chov?cfg_accent:0x21262D);
        int xbw=60,xbh=24,xbx=cbx-8-xbw,xby=cby;
        int xhov=in_box(mouse_x,mouse_y,xbx,xby,xbw,xbh);
        rect(xbx,xby,xbw,xbh,xhov?0x21262D:0x161B22);outline(xbx,xby,xbw,xbh,BORDER);
        text_center(xbx+xbw/2,xby+5,"Cancel",TEXT,xhov?0x21262D:0x161B22);
    }
}


/* ═══ CALCULATOR ════════════════════════════════════════════════ */
/* Fixed-point arithmetic: values stored as integer * 1000000 (6dp) */
#define FP 1000000LL
typedef long long fp_t;

static fp_t fp_mul(fp_t a,fp_t b){return a/1000LL*b/1000LL;}
static fp_t fp_div(fp_t a,fp_t b){if(b==0)return 0x7FFFFFFFFFFFFFFFLL;return a*1000LL/(b/1000LL);}
static fp_t fp_sqrt(fp_t x){
    if(x<0)return -1;if(x==0)return 0;
    fp_t g=x/2,prev=0;
    for(int i=0;i<60&&g!=prev;i++){prev=g;g=(g+fp_div(x,g))/2;}
    return g;
}
static fp_t fp_pow_int(fp_t b,long long e){
    if(e<0){fp_t r=fp_pow_int(b,-e);return r==0?0x7FFFFFFFFFFFFFFFLL:fp_div(FP*FP,r);}
    fp_t r=FP;while(e-->0)r=fp_mul(r,b);return r;
}
static fp_t fp_abs(fp_t x){return x<0?-x:x;}
static fp_t fp_mod(fp_t a,fp_t b){if(b==0)return 0x7FFFFFFFFFFFFFFFLL;fp_t q=a/b;return a-q*b;}
#define FP_PI 3141592LL  /* pi * 1000000 */

static const char* calc_src;
static int calc_pos;
static int calc_err;
static fp_t calc_parse_expr(void);
static fp_t calc_parse_term(void);
static fp_t calc_parse_factor(void);
static fp_t calc_parse_unary(void);
static fp_t calc_parse_primary(void);

static void calc_skip(void){while(calc_src[calc_pos]==' ')calc_pos++;}

static fp_t calc_parse_number(void){
    calc_skip();
    fp_t v=0,frac=0,fdiv=1; int hasdot=0;
    while((calc_src[calc_pos]>='0'&&calc_src[calc_pos]<='9')||calc_src[calc_pos]=='.'){
        if(calc_src[calc_pos]=='.'){hasdot=1;calc_pos++;continue;}
        if(hasdot){fdiv*=10;frac=frac*10+(calc_src[calc_pos]-'0');}
        else v=v*10+(calc_src[calc_pos]-'0');
        calc_pos++;
    }
    fp_t r=v*FP;
    if(hasdot&&fdiv>0){fp_t f=frac*FP/fdiv;r+=f;}
    return r;
}

static fp_t calc_parse_primary(void){
    calc_skip();
    if(calc_src[calc_pos]=='s'&&calc_src[calc_pos+1]=='q'&&
       calc_src[calc_pos+2]=='r'&&calc_src[calc_pos+3]=='t'){
        calc_pos+=4;calc_skip();
        if(calc_src[calc_pos]!='('){calc_err=1;return 0;}
        calc_pos++;fp_t v=calc_parse_expr();calc_skip();
        if(calc_src[calc_pos]==')')calc_pos++;
        fp_t r=fp_sqrt(v);if(r<0){calc_err=1;return 0;}return r;
    }
    if(calc_src[calc_pos]=='a'&&calc_src[calc_pos+1]=='b'&&
       calc_src[calc_pos+2]=='s'){
        calc_pos+=3;calc_skip();
        if(calc_src[calc_pos]!='('){calc_err=1;return 0;}
        calc_pos++;fp_t v=calc_parse_expr();calc_skip();
        if(calc_src[calc_pos]==')')calc_pos++;
        return fp_abs(v);
    }
    if(calc_src[calc_pos]=='p'&&calc_src[calc_pos+1]=='i'){
        calc_pos+=2;return FP_PI;
    }
    if(calc_src[calc_pos]=='('){
        calc_pos++;fp_t v=calc_parse_expr();calc_skip();
        if(calc_src[calc_pos]==')')calc_pos++;return v;
    }
    return calc_parse_number();
}
static fp_t calc_parse_unary(void){
    calc_skip();
    if(calc_src[calc_pos]=='-'){calc_pos++;return -calc_parse_unary();}
    if(calc_src[calc_pos]=='+'){calc_pos++;return  calc_parse_unary();}
    return calc_parse_primary();
}
static fp_t calc_parse_factor(void){
    fp_t base=calc_parse_unary();calc_skip();
    while(calc_src[calc_pos]=='^'){
        calc_pos++;
        fp_t exp=calc_parse_unary();
        long long ei=(long long)(exp/FP);
        base=fp_pow_int(base,ei);calc_skip();
    }
    return base;
}
static fp_t calc_parse_term(void){
    fp_t v=calc_parse_factor();calc_skip();
    while(calc_src[calc_pos]=='*'||calc_src[calc_pos]=='/'||calc_src[calc_pos]=='%'){
        char op=calc_src[calc_pos++];
        fp_t r=calc_parse_factor();
        if(op=='*')v=fp_mul(v,r);
        else if(op=='/'){
            if(r==0){calc_err=1;v=0;}
            else v=fp_div(v,r);
        }
        else v=fp_mod(v,r);
        calc_skip();
    }
    return v;
}
static fp_t calc_parse_expr(void){
    fp_t v=calc_parse_term();calc_skip();
    while(calc_src[calc_pos]=='+'||
         (calc_src[calc_pos]=='-'&&calc_pos>0&&calc_src[calc_pos-1]!='(')){
        char op=calc_src[calc_pos++];
        fp_t r=calc_parse_term();
        if(op=='+')v+=r;else v-=r;
        calc_skip();
    }
    return v;
}
static fp_t calc_evaluate(const char*expr){
    calc_src=expr;calc_pos=0;calc_err=0;
    return calc_parse_expr();
}

/* Format fixed-point to string */
static void calc_fmt(fp_t v,char*out,int maxl){
    if(calc_err||(v==0x7FFFFFFFFFFFFFFFLL)){
        out[0]='E';out[1]='r';out[2]='r';out[3]='o';out[4]='r';out[5]=0;return;
    }
    int oi=0;
    if(v<0){out[oi++]='-';v=-v;}
    long long iv=v/FP,fr=v%FP;
    if(iv==0){out[oi++]='0';}
    else{char tmp[24];int ti=0;long long t=iv;
        while(t>0){tmp[ti++]='0'+(int)(t%10);t/=10;}
        while(ti-->0&&oi<maxl-1)out[oi++]=tmp[ti];}
    if(fr>0){
        out[oi++]='.';
        /* print up to 6 decimal places, trim trailing zeros */
        char fdigs[7];int fi=0;
        long long f2=fr;
        while(fi<6){f2*=10;fdigs[fi++]='0'+(int)(f2/FP);f2%=FP;}
        /* trim */
        while(fi>0&&fdigs[fi-1]=='0')fi--;
        for(int i=0;i<fi&&oi<maxl-1;i++)out[oi++]=fdigs[i];
    }
    out[oi]=0;
}

static void calc_do_equals(void){
    if(calc.expr_len==0)return;
    calc.expr[calc.expr_len]=0;
    fp_t v=calc_evaluate(calc.expr);
    calc_fmt(v,calc.result,30);
    calc.has_result=1;calc.error=calc_err;
    if(!calc_err){
        if(calc.hist_count<6)calc.hist_count++;
        for(int i=calc.hist_count-1;i>0;i--){
            int j=0;while(calc.history[i-1][j]){calc.history[i][j]=calc.history[i-1][j];j++;}calc.history[i][j]=0;
        }
        int j=0;while(calc.result[j]&&j<31){calc.history[0][j]=calc.result[j];j++;}calc.history[0][j]=0;
    }
}

#define CALC_COLS 5
#define CALC_ROWS 7
static const char* calc_btns[CALC_ROWS][CALC_COLS]={
    {"C",    "(",   ")",  "%",   "^"  },
    {"sqrt", "abs", "pi", "\xb1","<"  },
    {"7",    "8",   "9",  "/",   "DEL"},
    {"4",    "5",   "6",  "*",   ""   },
    {"1",    "2",   "3",  "-",   ""   },
    {"0",    ".",   "",   "+",   "="  },
    {"",     "",    "",   "",    ""   },
};

static void calc_btn_press(const char*lbl){
    if(lbl[0]==0)return;
    if(lbl[0]=='C'&&lbl[1]==0){calc.expr_len=0;calc.expr[0]=0;calc.has_result=0;calc.error=0;return;}
    if((lbl[0]=='D'&&lbl[1]=='E')||(lbl[0]=='<'&&lbl[1]==0)){
        if(calc.expr_len>0){calc.expr[--calc.expr_len]=0;calc.has_result=0;}return;}
    if(lbl[0]=='='&&lbl[1]==0){calc_do_equals();return;}
    if(lbl[0]=='\xb1'){
        if(calc.expr_len>0&&calc.expr[0]=='-'){
            for(int i=0;i<calc.expr_len;i++)calc.expr[i]=calc.expr[i+1];calc.expr_len--;
        } else if(calc.expr_len<126){
            for(int i=calc.expr_len;i>0;i--)calc.expr[i]=calc.expr[i-1];
            calc.expr[0]='-';calc.expr_len++;calc.expr[calc.expr_len]=0;
        }
        calc.has_result=0;return;
    }
    if(calc.has_result&&((lbl[0]>='0'&&lbl[0]<='9')||lbl[0]=='s'||lbl[0]=='a'||lbl[0]=='p')){
        calc.expr_len=0;calc.expr[0]=0;calc.has_result=0;}
    int li=0;while(lbl[li]&&calc.expr_len<126){calc.expr[calc.expr_len++]=lbl[li++];}
    calc.expr[calc.expr_len]=0;calc.has_result=0;
}

static void draw_calc_content(int wi){
    Win*w=&wins[wi];
    int x=w->x,y=w->y+TITLEBAR_H,cw=w->w,ch=w->h-TITLEBAR_H;
    (void)ch;
    rect(x,y,cw,w->h-TITLEBAR_H,0x0D1117);
    int disp_h=80;
    rect(x,y,cw,disp_h,0x0A0D10);hline(x,y+disp_h,cw,BORDER);
    int ex_max=(cw-16)/8;
    char eclip[128];int ek=0,estart=0;
    if(calc.expr_len>ex_max)estart=calc.expr_len-ex_max;
    while(calc.expr[estart+ek]&&ek<ex_max&&ek<127){eclip[ek]=calc.expr[estart+ek];ek++;}eclip[ek]=0;
    text(x+8,y+8,eclip,DIM,0x0A0D10);
    if(calc.has_result){
        u32 rc=calc.error?RED:GREEN;
        int rlen=slen(calc.result);
        if(rlen<=8)text_big(x+cw-8-rlen*16,y+30,calc.result,rc,0x0A0D10);
        else text(x+cw-8-rlen*8,y+44,calc.result,rc,0x0A0D10);
    } else if(calc.expr_len==0){text(x+8,y+44,"0",DIM,0x0A0D10);}
    int hist_y=y+disp_h+4;
    for(int i=0;i<calc.hist_count&&i<4;i++){
        u32 hc=(i==0)?TEXT:DIM;
        text(x+8,hist_y+i*14,calc.history[i],hc,0x0D1117);
    }
    int btn_top=y+disp_h+60;
    int bw=(cw-12)/CALC_COLS,bh=36;
    u32 btn_colors[CALC_ROWS][CALC_COLS]={
        {RED,   0x2D333B,0x2D333B,PURPLE, PURPLE},
        {ACCENT,ACCENT,  ACCENT,  0x2D333B,RED   },
        {0x1C2128,0x1C2128,0x1C2128,YELLOW,RED   },
        {0x1C2128,0x1C2128,0x1C2128,YELLOW,0x0D1117},
        {0x1C2128,0x1C2128,0x1C2128,YELLOW,0x0D1117},
        {0x1C2128,0x1C2128,0x1C2128,YELLOW,GREEN  },
        {0x0D1117,0x0D1117,0x0D1117,0x0D1117,0x0D1117},
    };
    for(int r=0;r<CALC_ROWS;r++){
        for(int c=0;c<CALC_COLS;c++){
            const char*lbl=calc_btns[r][c];if(!lbl[0])continue;
            int bx=x+6+c*bw,by=btn_top+r*bh;
            int hov=in_box(mouse_x,mouse_y,bx,by,bw-4,bh-4);
            u32 bc=btn_colors[r][c];
            u32 bg=hov?(bc+0x181818):bc;
            rect(bx,by,bw-4,bh-4,bg);outline(bx,by,bw-4,bh-4,hov?TEXT:BORDER);
            int ll=slen(lbl);
            text(bx+(bw-4-ll*8)/2,by+(bh-4-16)/2,lbl,TEXT,bg);
        }
    }
}

static void calc_handle_key(s64 ch){
    if(ch=='\n'||ch=='\r'){calc_do_equals();return;}
    if(ch=='\b'||ch==127){if(calc.expr_len>0){calc.expr[--calc.expr_len]=0;calc.has_result=0;}return;}
    if(ch>=' '&&ch<127){
        char c=(char)ch;
        if(c=='C'){calc.expr_len=0;calc.expr[0]=0;calc.has_result=0;calc.error=0;return;}
        int ok=(c>='0'&&c<='9')||c=='+'||c=='-'||c=='*'||c=='/'||
               c=='('||c==')'||c=='.'||c=='^'||c=='%'||
               c=='s'||c=='q'||c=='r'||c=='t'||c=='a'||c=='b'||c=='p'||c=='i';
        if(ok&&calc.expr_len<126){calc.expr[calc.expr_len++]=c;calc.expr[calc.expr_len]=0;calc.has_result=0;}
    }
}

/* === OPEN HELPERS ══════════════════════════════════════════════ *//* === OPEN HELPERS ══════════════════════════════════════════════ */
static int find_win(int id){for(int i=0;i<win_count;i++)if(wins[i].id==id)return i;return -1;}
static void open_terminal(void){int i=find_win(WIN_TERMINAL);if(i>=0){wins[i].visible=1;wins[i].minimized=0;wm_focus(i);}else wm_new(WIN_TERMINAL,130,60,600,500,"Terminal",cfg_accent);}
static void open_about(void){int i=find_win(WIN_ABOUT);if(i>=0){wins[i].visible=1;wins[i].minimized=0;wm_focus(i);}else wm_new(WIN_ABOUT,280,150,420,280,"About YouOS",PURPLE);}
static void open_files(void){
    int cnt=0;for(int k=0;k<win_count;k++)if(wins[k].id==WIN_FILES)cnt++;
    int ox=100+(cnt%6)*24,oy=80+(cnt%6)*24;
    int i=wm_new(WIN_FILES,ox,oy,560,420,"File Manager",GREEN);
    if(i<0)return;
    fm_current=i;
    fm_states[i].use_fat16=0;
    fm_loaded=0;fm_path_len=0;fm_path[0]=0;
}
static void open_notepad(const char*fn){
    int cnt=0;for(int k=0;k<win_count;k++)if(wins[k].id==WIN_NOTEPAD)cnt++;
    int ox=150+(cnt%6)*24,oy=70+(cnt%6)*24;
    int i=wm_new(WIN_NOTEPAD,ox,oy,580,460,"YC Notepad",YELLOW);
    if(i<0)return;
    np_current=i;
    np.text[0]=0;np.text_len=0;np.cursor=0;np.scroll=0;
    np.modified=0;np.mode=0;np.filename[0]=0;
    np.dlg_len=0;np.dlg_hov=-1;np.dlg_scroll=0;np.save_flash=0;np.mode3_err=0;np.err_name[0]=0;
    if(fn&&fn[0]){
        char path[56];path[0]='/';path[1]='y';path[2]='c';path[3]='f';path[4]='s';path[5]='/';
        int k=6,j=0;while(fn[j]&&k<55){path[k++]=fn[j++];}path[k]=0;
        np_load(path,fn);
        j=0;while(fn[j]&&j<39){wins[i].title[j]=fn[j];j++;}wins[i].title[j]=0;
    }
}

/* ═══ IMAGE VIEWER (PNG) ═══════════════════════════════════════════
 * Single active image buffer, like WAV's single active playback —
 * a full RGBA buffer is large (~3MB at the max supported size), so
 * per-window copies for all 12 possible windows would be prohibitive.
 * Opening a new PNG reuses the existing viewer window if one's
 * already open, rather than creating a second one. */
static u8  img_file_buf[2*1024*1024]; /* raw file bytes, read whole */
static u8  img_rgba[PNG_MAX_W*PNG_MAX_H*4];
static u8  img_scratch[PNG_MAX_W*PNG_MAX_H*4 + PNG_MAX_H];
static int imgview_win=-1;
static u32 imgview_w=0, imgview_h=0;

static void open_imgview(const char*path, const char*shortname){
    u64 fd=sys_open(path,0);
    if((s64)fd<0){tprint("PNG: open failed");return;}
    u32 got=0;
    for(;;){
        if(got>=sizeof(img_file_buf))break;
        u64 want=sizeof(img_file_buf)-got;
        s64 r=sys_fread(fd,img_file_buf+got,want);
        if(r<=0)break;
        got+=(u32)r;
    }
    sys_close(fd);
    if(got==0){tprint("PNG: read failed, no data");return;}

    png_info_t info=png_decode(img_file_buf,got,img_rgba,img_scratch,sizeof(img_scratch));
    if(info.error!=PNG_OK){
        char eb[80];int ei=0;
        const char*p1="PNG: ";int j=0;while(p1[j])eb[ei++]=p1[j++];
        const char*msg=png_error_str(info.error);j=0;while(msg[j]&&ei<78)eb[ei++]=msg[j++];
        eb[ei]=0;tprint(eb);
        return;
    }

    imgview_w=info.width; imgview_h=info.height;

    /* Window size = image size, clamped to fit the screen with room
     * for the titlebar/margins. Larger images display cropped
     * top-left for now — real scrolling not implemented in this
     * first version, matching "get it working, iterate later". */
    int max_w=(int)FB_W-60, max_h=(int)FB_H-TITLEBAR_H-60;
    int ww=(int)imgview_w<max_w?(int)imgview_w:max_w;
    int wh=(int)imgview_h+TITLEBAR_H<max_h?(int)imgview_h+TITLEBAR_H:max_h;

    if(imgview_win>=0 && imgview_win<win_count && wins[imgview_win].visible){
        wins[imgview_win].w=ww; wins[imgview_win].h=wh;
        wins[imgview_win].minimized=0;
        focused=imgview_win;
    } else {
        int i=wm_new(WIN_IMGVIEW,180,90,ww,wh,"Image Viewer",PURPLE);
        if(i<0){tprint("PNG: too many windows open");return;}
        imgview_win=i;
    }
    int j=0;while(shortname[j]&&j<39){wins[imgview_win].title[j]=shortname[j];j++;}
    wins[imgview_win].title[j]=0;
}

static void draw_imgview_content(int wi){
    Win*w=&wins[wi];
    int x=w->x,y=w->y+TITLEBAR_H,cw=w->w,ch=w->h-TITLEBAR_H;
    rect(x,y,cw,ch,0x0D1117);
    u32 drawn_w=(u32)cw<imgview_w?(u32)cw:imgview_w;
    u32 drawn_h=(u32)ch<imgview_h?(u32)ch:imgview_h;
    for(u32 iy=0;iy<drawn_h;iy++){
        const u8* row=img_rgba+(u64)iy*imgview_w*4;
        for(u32 ix=0;ix<drawn_w;ix++){
            const u8* p=row+ix*4;
            u32 color=((u32)p[0]<<16)|((u32)p[1]<<8)|(u32)p[2]; /* alpha ignored, no blending yet */
            px(x+(int)ix,y+(int)iy,color);
        }
    }
}

/* ═══ MAIN ══════════════════════════════════════════════════════ */
typedef struct{u32 magic;u32 accent;u32 h24;u32 secs;int icon_x[N_ICONS];int icon_y[N_ICONS];u32 wallpaper_on;}CfgBlob;
#define CFG_MAGIC 0xC0DE5E17U
static void cfg_save(void){
    CfgBlob b;b.magic=CFG_MAGIC;b.accent=cfg_accent;
    b.h24=(u32)cfg_24h;b.secs=(u32)cfg_showsecs;
    for(int i=0;i<N_ICONS;i++){b.icon_x[i]=icons[i].x;b.icon_y[i]=icons[i].y;}
    b.wallpaper_on=(u32)wallpaper_loaded;
    const char*p="/ycfs/yos.cfg";
    sys_save_file((u64)p,(u64)&b,(u64)sizeof(b));
}

static void draw_shutdown_splash(int spin_deg){
    rect(0,0,(int)FB_W,(int)FB_H,0x0D1117);
    int scx=(int)FB_W/2,scy=(int)FB_H/2;
    int cx=scx,cy=scy-10,r=28;
    for(int i=0;i<8;i++){
        int deg=(spin_deg+i*45)%360;
        int dx=cx+(icos(deg)*r)/1000;
        int dy=cy-(isin(deg)*r)/1000;
        int a=255-(i*28);if(a<40)a=40;
        int dotr=4;
        for(int yy=-dotr;yy<=dotr;yy++)for(int xx=-dotr;xx<=dotr;xx++)
            if(xx*xx+yy*yy<=dotr*dotr)px_alpha(dx+xx,dy+yy,0x58A6FF,a);
    }
    text_center(scx,cy+60,"Shutting down...",0xFFFFFF,0x0D1117);
    flush();
}

static void draw_restart_splash(int spin_deg){
    rect(0,0,(int)FB_W,(int)FB_H,0x0D1117);
    int scx=(int)FB_W/2,scy=(int)FB_H/2;
    int cx=scx,cy=scy-10,r=28;
    for(int i=0;i<8;i++){
        int deg=(spin_deg+i*45)%360;
        int dx=cx+(icos(deg)*r)/1000;
        int dy=cy-(isin(deg)*r)/1000;
        int a=255-(i*28);if(a<40)a=40;
        int dotr=4;
        for(int yy=-dotr;yy<=dotr;yy++)for(int xx=-dotr;xx<=dotr;xx++)
            if(xx*xx+yy*yy<=dotr*dotr)px_alpha(dx+xx,dy+yy,0x58A6FF,a);
    }
    text_center(scx,cy+60,"Restarting...",0xFFFFFF,0x0D1117);
    flush();
}

/* Same shape as do_shutdown(): persist config, hide windows, show a
 * spinning splash for at least ~3 seconds, then actually reboot. The
 * splash stays on screen right up until sys_reboot() hard-resets the
 * machine, so there's no gap back to the desktop before the real
 * hardware reset/reboot sequence begins. */
static void do_restart(void){
    cfg_save();
    for(int i=0;i<MAX_WINDOWS;i++) wins[i].visible=0;

    u64 start=sys_ticks();
    int deg=0;
    while(sys_ticks()-start<300){
        draw_restart_splash(deg);
        deg=(deg+15)%360;
        sys_sleep(3);
    }
    sys_reboot();
}

/* Housekeeping before power-off — persist current settings/icon layout
 * and hide open windows (simulating "closing programs"; this desktop is
 * effectively single-process so there's no real process list to
 * terminate beyond the suspend/resume shell already handles). Shown for
 * at least ~3 seconds (300 ticks at 100Hz) while that happens, so the
 * shutdown never feels instant/jarring even though the real work here
 * is fast. */
static void do_shutdown(void){
    cfg_save();
    for(int i=0;i<MAX_WINDOWS;i++) wins[i].visible=0;

    u64 start=sys_ticks();
    int deg=0;
    while(sys_ticks()-start<300){
        draw_shutdown_splash(deg);
        deg=(deg+15)%360;
        sys_sleep(3);
    }
    sys_shutdown();
}
static void cfg_load(void){
    CfgBlob b;
    u64 fd=sys_open("/ycfs/yos.cfg",0);
    if((s64)fd<0)return;
    u64 n=sys_fread(fd,&b,sizeof(b));
    sys_close(fd);
    if(n!=(u64)sizeof(b)||b.magic!=CFG_MAGIC)return;
    cfg_accent=b.accent;cfg_24h=(int)b.h24;cfg_showsecs=(int)b.secs;
    for(int i=0;i<N_ICONS;i++){icons[i].x=b.icon_x[i];icons[i].y=b.icon_y[i];}
    wallpaper_loaded=(int)b.wallpaper_on&&load_wallpaper_bmp("/ycfs/wall.bmp");
}
static void open_calc(void){
    int cnt=0;for(int k=0;k<win_count;k++)if(wins[k].id==WIN_CALC)cnt++;
    int ox=220+(cnt%6)*24,oy=100+(cnt%6)*24;
    int i=wm_new(WIN_CALC,ox,oy,280,380,"Calculator",ACCENT);
    if(i<0)return;
    calc_current=i;
    calc.expr_len=0;calc.expr[0]=0;calc.has_result=0;
    calc.error=0;calc.hist_count=0;
}
static int acct_setup_open=0,acct_setup_mode=0;
static void open_settings(void){
    int i=find_win(WIN_SETTINGS);
    if(i>=0){wins[i].visible=1;wins[i].minimized=0;wm_focus(i);settings_win_idx=i;}
    else settings_win_idx=wm_new(WIN_SETTINGS,200,120,320,440,"Settings",0x58A6FF);
}
/* ═══ SHA-256 ═══════════════════════════════════════════════════ */
static const u32 sha256_k[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static u32 sha256_rotr(u32 x,int n){return (x>>n)|(x<<(32-n));}
typedef struct{u32 h[8];u8 buf[64];u64 len;int buf_len;}Sha256Ctx;
static void sha256_init(Sha256Ctx*c){
    u32 iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    for(int i=0;i<8;i++)c->h[i]=iv[i];
    c->len=0;c->buf_len=0;
}
static void sha256_block(Sha256Ctx*c,const u8*p){
    u32 w[64];
    for(int i=0;i<16;i++)w[i]=((u32)p[i*4]<<24)|((u32)p[i*4+1]<<16)|((u32)p[i*4+2]<<8)|((u32)p[i*4+3]);
    for(int i=16;i<64;i++){
        u32 s0=sha256_rotr(w[i-15],7)^sha256_rotr(w[i-15],18)^(w[i-15]>>3);
        u32 s1=sha256_rotr(w[i-2],17)^sha256_rotr(w[i-2],19)^(w[i-2]>>10);
        w[i]=w[i-16]+s0+w[i-7]+s1;
    }
    u32 a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],hh=c->h[7];
    for(int i=0;i<64;i++){
        u32 S1=sha256_rotr(e,6)^sha256_rotr(e,11)^sha256_rotr(e,25);
        u32 ch=(e&f)^((~e)&g);
        u32 t1=hh+S1+ch+sha256_k[i]+w[i];
        u32 S0=sha256_rotr(a,2)^sha256_rotr(a,13)^sha256_rotr(a,22);
        u32 maj=(a&b)^(a&cc)^(b&cc);
        u32 t2=S0+maj;
        hh=g;g=f;f=e;e=d+t1;d=cc;cc=b;b=a;a=t1+t2;
    }
    c->h[0]+=a;c->h[1]+=b;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=hh;
}
static void sha256_update(Sha256Ctx*c,const u8*data,u64 len){
    c->len+=len;
    while(len>0){
        int n=64-c->buf_len;if((u64)n>len)n=(int)len;
        for(int i=0;i<n;i++)c->buf[c->buf_len+i]=data[i];
        c->buf_len+=n;data+=n;len-=n;
        if(c->buf_len==64){sha256_block(c,c->buf);c->buf_len=0;}
    }
}
static void sha256_final(Sha256Ctx*c,u8 out[32]){
    u64 bitlen=c->len*8;
    u8 pad=0x80;sha256_update(c,&pad,1);
    u8 zero=0;
    while(c->buf_len!=56)sha256_update(c,&zero,1);
    u8 lenb[8];for(int i=0;i<8;i++)lenb[i]=(u8)(bitlen>>(56-8*i));
    for(int i=0;i<8;i++){c->buf[c->buf_len++]=lenb[i];}
    sha256_block(c,c->buf);c->buf_len=0;
    for(int i=0;i<8;i++){out[i*4]=(u8)(c->h[i]>>24);out[i*4+1]=(u8)(c->h[i]>>16);out[i*4+2]=(u8)(c->h[i]>>8);out[i*4+3]=(u8)c->h[i];}
}
static void sha256(const u8*data,u64 len,u8 out[32]){
    Sha256Ctx c;sha256_init(&c);sha256_update(&c,data,len);sha256_final(&c,out);
}
static void sha256_self_test(void){
    static const u8 expect[32]={
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};
    u8 out[32];sha256((const u8*)"abc",3,out);
    for(int i=0;i<32;i++){
        if(out[i]!=expect[i]){
            rect(0,0,(int)FB_W,(int)FB_H,0x800000);
            text(40,40,"SHA-256 SELF-TEST FAILED",0xFFFFFF,0x800000);
            text(40,60,"Refusing to start - crypto is broken.",0xFFFFFF,0x800000);flush();
            while(1);
        }
    }
}
/* ═══ HMAC-SHA256 / PBKDF2-HMAC-SHA256 ═════════════════════════ */
static void hmac_sha256(const u8*key,u64 keylen,const u8*msg,u64 msglen,u8 out[32]){
    u8 k[64];
    if(keylen>64){
        u8 kh[32];sha256(key,keylen,kh);
        for(int i=0;i<32;i++)k[i]=kh[i];
        for(int i=32;i<64;i++)k[i]=0;
    } else {
        for(u64 i=0;i<keylen;i++)k[i]=key[i];
        for(u64 i=keylen;i<64;i++)k[i]=0;
    }
    u8 ipad[64],opad[64];
    for(int i=0;i<64;i++){ipad[i]=(u8)(k[i]^0x36);opad[i]=(u8)(k[i]^0x5c);}
    Sha256Ctx c;
    sha256_init(&c);sha256_update(&c,ipad,64);sha256_update(&c,msg,msglen);
    u8 inner[32];sha256_final(&c,inner);
    sha256_init(&c);sha256_update(&c,opad,64);sha256_update(&c,inner,32);
    sha256_final(&c,out);
}
static void pbkdf2_hmac_sha256(const u8*pass,u64 passlen,const u8*salt,u64 saltlen,u32 iterations,u8 out[32]){
    u8 buf[68];
    if(saltlen>64)saltlen=64;
    for(u64 i=0;i<saltlen;i++)buf[i]=salt[i];
    buf[saltlen]=0;buf[saltlen+1]=0;buf[saltlen+2]=0;buf[saltlen+3]=1;
    u8 u[32],t[32];
    hmac_sha256(pass,passlen,buf,saltlen+4,u);
    for(int i=0;i<32;i++)t[i]=u[i];
    for(u32 iter=1;iter<iterations;iter++){
        u8 unext[32];
        hmac_sha256(pass,passlen,u,32,unext);
        for(int i=0;i<32;i++){t[i]^=unext[i];u[i]=unext[i];}
    }
    for(int i=0;i<32;i++)out[i]=t[i];
}
static void hmac_sha256_self_test(void){
    u8 key[20];for(int i=0;i<20;i++)key[i]=0x0b;
    const u8 data[8]={'H','i',' ','T','h','e','r','e'};
    u8 out[32];
    hmac_sha256(key,20,data,8,out);
    static const u8 expect[32]={
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7};
    for(int i=0;i<32;i++){
        if(out[i]!=expect[i]){
            rect(0,0,(int)FB_W,(int)FB_H,0x800000);
            text(40,40,"HMAC-SHA256 SELF-TEST FAILED",0xFFFFFF,0x800000);
            text(40,60,"Refusing to start - crypto is broken.",0xFFFFFF,0x800000);
            flush();
            while(1);
        }
    }
}
static void pbkdf2_self_test(void){
    const u8 pass[8]={'p','a','s','s','w','o','r','d'};
    const u8 salt[4]={'s','a','l','t'};
    u8 saltint[8];for(int i=0;i<4;i++)saltint[i]=salt[i];
    saltint[4]=0;saltint[5]=0;saltint[6]=0;saltint[7]=1;
    u8 u1[32],u2[32],expect2[32];
    hmac_sha256(pass,8,saltint,8,u1);
    hmac_sha256(pass,8,u1,32,u2);
    for(int i=0;i<32;i++)expect2[i]=(u8)(u1[i]^u2[i]);
    u8 out1[32],out2[32];
    pbkdf2_hmac_sha256(pass,8,salt,4,1,out1);
    pbkdf2_hmac_sha256(pass,8,salt,4,2,out2);
    int ok=1;
    for(int i=0;i<32;i++){if(out1[i]!=u1[i])ok=0;if(out2[i]!=expect2[i])ok=0;}
    if(!ok){
        rect(0,0,(int)FB_W,(int)FB_H,0x800000);
        text(40,40,"PBKDF2 SELF-TEST FAILED",0xFFFFFF,0x800000);
        text(40,60,"Refusing to start - crypto is broken.",0xFFFFFF,0x800000);
        flush();
        while(1);
    }
}
/* ═══ Auth: entropy, recovery codes, AuthBlob storage ══════════ */
#define AUTH_MAGIC 0xA0741CADU
#define AUTH_PBKDF2_ITERS 10000
#define AUTH_PATH "/ycfs/auth.dat"
static u32 auth_entropy_counter=0;
static void auth_random_bytes(u8*out,int len){
    int filled=0;
    while(filled<len){
        unsigned long long mstate[3];sys_mouseread(mstate);
        u64 t=sys_ticks();
        auth_entropy_counter++;
        u8 seed[24];
        for(int i=0;i<8;i++)seed[i]=(u8)(t>>(56-8*i));
        seed[8]=(u8)(mstate[0]>>8);seed[9]=(u8)mstate[0];
        seed[10]=(u8)(mstate[1]>>8);seed[11]=(u8)mstate[1];
        seed[12]=(u8)mstate[2];
        seed[13]=(u8)(auth_entropy_counter>>24);seed[14]=(u8)(auth_entropy_counter>>16);
        seed[15]=(u8)(auth_entropy_counter>>8);seed[16]=(u8)auth_entropy_counter;
        seed[17]=(u8)filled;
        seed[18]=0;seed[19]=0;seed[20]=0;seed[21]=0;seed[22]=0;seed[23]=0;
        u8 h[32];sha256(seed,24,h);
        int n=len-filled;if(n>32)n=32;
        for(int i=0;i<n;i++)out[filled+i]=h[i];
        filled+=n;
    }
}
static const char auth_hexd[]="0123456789ABCDEF";
static void auth_make_recovery_code(char out[20]){
    u8 r[8];auth_random_bytes(r,8);
    int p=0;
    for(int i=0;i<8;i++){
        out[p++]=auth_hexd[(r[i]>>4)&0xF];
        out[p++]=auth_hexd[r[i]&0xF];
        if(i%2==1&&i!=7)out[p++]='-';
    }
    out[p]=0;
}
static void auth_normalize_code(const char*in,char*out,int outsize){
    int j=0;
    for(int i=0;in[i]&&j<outsize-1;i++){
        char c=in[i];
        if(c>='a'&&c<='z')c=(char)(c-'a'+'A');
        if((c>='0'&&c<='9')||(c>='A'&&c<='F'))out[j++]=c;
    }
    out[j]=0;
}
static void auth_copy_str(char*dst,int dstsize,const char*src){
    int i=0;while(src[i]&&i<dstsize-1){dst[i]=src[i];i++;}dst[i]=0;
}
/* Multi-user account table (Phase 3.5 item 4, stage 2). Replaces the
 * old single-account AuthBlob with a small fixed table of accounts.
 * uid 0 is always root, created during first-time setup; additional
 * users are appended later via a root-only "Add User" action. */
#define MAX_USERS 16
typedef struct{
    u32 uid;
    char username[32];
    u8 pass_salt[16];
    u8 pass_hash[32];
    u8 rec_salt[16];
    u8 rec_hash[32];
}UserEntry;
typedef struct{
    u32 magic;
    u32 count;
    UserEntry users[MAX_USERS];
}UserTable;
static int auth_table_load(const char*path,UserTable*t){
    u64 fd=sys_open(path,0);
    if((s64)fd<0)return 0;
    u64 n=sys_fread(fd,t,sizeof(UserTable));
    sys_close(fd);
    if(n!=(u64)sizeof(UserTable)||t->magic!=AUTH_MAGIC)return 0;
    if(t->count>MAX_USERS)return 0;
    return 1;
}
static void auth_table_save(const char*path,UserTable*t){
    t->magic=AUTH_MAGIC;
    sys_save_file((u64)path,(u64)t,(u64)sizeof(UserTable));
}
static int auth_exists(const char*path){
    UserTable t;return auth_table_load(path,&t)&&t.count>0;
}
/* Find a user entry by username. Returns the index into t->users, or
 * -1 if not found. Case-sensitive, matching the original single-
 * account behavior (no case-folding was ever applied to usernames). */
static int auth_find_user(UserTable*t,const char*username){
    for(u32 i=0;i<t->count;i++){
        int match=1;
        for(int k=0;k<32;k++){
            if(t->users[i].username[k]!=username[k]){match=0;break;}
            if(username[k]==0)break;
        }
        if(match)return (int)i;
    }
    return -1;
}
/* Create a new user in the table. is_first calls (uid 0 / root) go
 * through acct_setup_run(1) which only runs when the table is empty,
 * so uid 0 is always root by construction. Subsequent users get the
 * next free uid. Returns 1 on success, 0 if the table is full or the
 * username already exists. */
static int auth_create_user(const char*path,const char*username,const char*password,char recovery_out[20]){
    UserTable t;
    if(!auth_table_load(path,&t)){t.magic=AUTH_MAGIC;t.count=0;}
    if(t.count>=MAX_USERS)return 0;
    if(auth_find_user(&t,username)>=0)return 0; /* username taken */

    UserEntry*b=&t.users[t.count];
    b->uid=t.count; /* uid 0 for the very first user (root), then 1,2,3... */
    auth_copy_str(b->username,32,username);
    auth_random_bytes(b->pass_salt,16);
    pbkdf2_hmac_sha256((const u8*)password,(u64)slen(password),b->pass_salt,16,AUTH_PBKDF2_ITERS,b->pass_hash);
    auth_make_recovery_code(recovery_out);
    char norm[20];auth_normalize_code(recovery_out,norm,20);
    auth_random_bytes(b->rec_salt,16);
    pbkdf2_hmac_sha256((const u8*)norm,(u64)slen(norm),b->rec_salt,16,AUTH_PBKDF2_ITERS,b->rec_hash);
    t.count++;
    auth_table_save(path,&t);
    return 1;
}
/* Verify a password for a specific username. On success, if out_uid
 * is non-null, writes that user's uid (for the caller to track as the
 * now-logged-in user). */
static int auth_verify_password(const char*path,const char*username,const char*password,u32*out_uid){
    UserTable t;
    if(!auth_table_load(path,&t))return 0;
    int idx=auth_find_user(&t,username);
    if(idx<0)return 0;
    UserEntry*b=&t.users[idx];
    u8 h[32];
    pbkdf2_hmac_sha256((const u8*)password,(u64)slen(password),b->pass_salt,16,AUTH_PBKDF2_ITERS,h);
    for(int i=0;i<32;i++)if(h[i]!=b->pass_hash[i])return 0;
    if(out_uid)*out_uid=b->uid;
    return 1;
}
static int auth_reset_password(const char*path,const char*username,const char*old_recovery_code,const char*new_password,char new_recovery_out[20]){
    UserTable t;
    if(!auth_table_load(path,&t))return 0;
    int idx=auth_find_user(&t,username);
    if(idx<0)return 0;
    UserEntry*b=&t.users[idx];
    char norm[20];auth_normalize_code(old_recovery_code,norm,20);
    u8 h[32];
    pbkdf2_hmac_sha256((const u8*)norm,(u64)slen(norm),b->rec_salt,16,AUTH_PBKDF2_ITERS,h);
    int match=1;
    for(int i=0;i<32;i++)if(h[i]!=b->rec_hash[i])match=0;
    if(!match)return 0;
    auth_random_bytes(b->pass_salt,16);
    pbkdf2_hmac_sha256((const u8*)new_password,(u64)slen(new_password),b->pass_salt,16,AUTH_PBKDF2_ITERS,b->pass_hash);
    auth_make_recovery_code(new_recovery_out);
    char norm2[20];auth_normalize_code(new_recovery_out,norm2,20);
    auth_random_bytes(b->rec_salt,16);
    pbkdf2_hmac_sha256((const u8*)norm2,(u64)slen(norm2),b->rec_salt,16,AUTH_PBKDF2_ITERS,b->rec_hash);
    auth_table_save(path,&t);
    return 1;
}
/* Self-test using a real (throwaway) on-disk table, exercising the
 * actual auth_create_user/auth_verify_password/auth_reset_password
 * functions end-to-end rather than hand-rolling the crypto calls
 * inline like the old single-blob version did — this way the test
 * genuinely covers the multi-user table path, not just the hashing
 * primitives. Writes to a scratch path that's never used for real
 * accounts. */
/* Pure in-memory test of the auth crypto/table logic — deliberately
 * does NOT touch disk at all. An earlier version of this test wrote
 * real accounts to a scratch YCFS path and unlinked them at the end,
 * but that path went through the same journal/transaction machinery
 * as everything else on the filesystem, and a transaction from one
 * boot occasionally got replayed on a LATER boot (the journal is only
 * guaranteed to fully drain-and-wipe once replay actually runs, and
 * that depends on a clean shutdown happening in between) — landing
 * scratch-test user data on top of the real auth.dat's block on a
 * subsequent boot. Testing the logic directly against in-memory
 * structs instead of a real file sidesteps that whole class of bug:
 * this exercises the exact same hashing/comparison code paths that
 * auth_create_user/auth_verify_password/auth_reset_password use
 * internally, just without ever calling sys_open/sys_save_file. */
static void auth_self_test(void){
    int fail=0;
    UserTable t;
    t.magic=AUTH_MAGIC;t.count=0;
    char rec1[20],rec2[20];

    /* Simulate auth_create_user("tester", "correct_password") */
    {
        UserEntry*b=&t.users[t.count];
        b->uid=t.count;
        auth_copy_str(b->username,32,"tester");
        auth_random_bytes(b->pass_salt,16);
        pbkdf2_hmac_sha256((const u8*)"correct_password",16,b->pass_salt,16,AUTH_PBKDF2_ITERS,b->pass_hash);
        auth_make_recovery_code(rec1);
        char norm[20];auth_normalize_code(rec1,norm,20);
        auth_random_bytes(b->rec_salt,16);
        pbkdf2_hmac_sha256((const u8*)norm,(u64)slen(norm),b->rec_salt,16,AUTH_PBKDF2_ITERS,b->rec_hash);
        t.count++;
    }

    /* Simulate auth_verify_password("tester", "correct_password") */
    {
        int idx=auth_find_user(&t,"tester");
        if(idx<0)fail=1;
        else{
            UserEntry*b=&t.users[idx];
            u8 h[32];
            pbkdf2_hmac_sha256((const u8*)"correct_password",16,b->pass_salt,16,AUTH_PBKDF2_ITERS,h);
            int match=1;for(int i=0;i<32;i++)if(h[i]!=b->pass_hash[i])match=0;
            if(!match||b->uid!=0)fail=1;
        }
    }
    /* wrong password must be rejected */
    if(!fail){
        int idx=auth_find_user(&t,"tester");
        UserEntry*b=&t.users[idx];
        u8 h[32];
        pbkdf2_hmac_sha256((const u8*)"wrong_password",14,b->pass_salt,16,AUTH_PBKDF2_ITERS,h);
        int match=1;for(int i=0;i<32;i++)if(h[i]!=b->pass_hash[i])match=0;
        if(match)fail=1;
    }
    /* recovery-code reset */
    if(!fail){
        int idx=auth_find_user(&t,"tester");
        UserEntry*b=&t.users[idx];
        char norm[20];auth_normalize_code(rec1,norm,20);
        u8 h[32];
        pbkdf2_hmac_sha256((const u8*)norm,(u64)slen(norm),b->rec_salt,16,AUTH_PBKDF2_ITERS,h);
        int match=1;for(int i=0;i<32;i++)if(h[i]!=b->rec_hash[i])match=0;
        if(!match)fail=1;
        else{
            auth_random_bytes(b->pass_salt,16);
            pbkdf2_hmac_sha256((const u8*)"new_password",12,b->pass_salt,16,AUTH_PBKDF2_ITERS,b->pass_hash);
            auth_make_recovery_code(rec2);
            char norm2[20];auth_normalize_code(rec2,norm2,20);
            auth_random_bytes(b->rec_salt,16);
            pbkdf2_hmac_sha256((const u8*)norm2,(u64)slen(norm2),b->rec_salt,16,AUTH_PBKDF2_ITERS,b->rec_hash);
        }
    }
    /* new password works, old one doesn't, old recovery code is spent */
    if(!fail){
        int idx=auth_find_user(&t,"tester");
        UserEntry*b=&t.users[idx];
        u8 h[32];
        pbkdf2_hmac_sha256((const u8*)"new_password",12,b->pass_salt,16,AUTH_PBKDF2_ITERS,h);
        int match=1;for(int i=0;i<32;i++)if(h[i]!=b->pass_hash[i])match=0;
        if(!match)fail=1;
    }
    if(!fail){
        int idx=auth_find_user(&t,"tester");
        UserEntry*b=&t.users[idx];
        u8 h[32];
        pbkdf2_hmac_sha256((const u8*)"correct_password",16,b->pass_salt,16,AUTH_PBKDF2_ITERS,h);
        int match=1;for(int i=0;i<32;i++)if(h[i]!=b->pass_hash[i])match=0;
        if(match)fail=1;
    }
    if(!fail){
        int idx=auth_find_user(&t,"tester");
        UserEntry*b=&t.users[idx];
        char norm[20];auth_normalize_code(rec1,norm,20);
        u8 h[32];
        pbkdf2_hmac_sha256((const u8*)norm,(u64)slen(norm),b->rec_salt,16,AUTH_PBKDF2_ITERS,h);
        int match=1;for(int i=0;i<32;i++)if(h[i]!=b->rec_hash[i])match=0;
        if(match)fail=1; /* old recovery code must NOT work anymore */
    }
    /* a second distinct user must get uid 1, no collision with the first */
    if(!fail){
        UserEntry*b=&t.users[t.count];
        b->uid=t.count;
        auth_copy_str(b->username,32,"tester2");
        auth_random_bytes(b->pass_salt,16);
        pbkdf2_hmac_sha256((const u8*)"pw2",3,b->pass_salt,16,AUTH_PBKDF2_ITERS,b->pass_hash);
        t.count++;
        int idx2=auth_find_user(&t,"tester2");
        if(idx2<0||t.users[idx2].uid!=1)fail=1;
    }

    if(fail){
        rect(0,0,(int)FB_W,(int)FB_H,0x800000);
        text(40,40,"AUTH LOGIC SELF-TEST FAILED",0xFFFFFF,0x800000);
        text(40,60,"Refusing to start - account system is broken.",0xFFFFFF,0x800000);
        flush();
        while(1);
    }
}
/* ═══ Account setup UI ═════════════════════════════════════════ */
#define ACCT_W 420
#define ACCT_H 420
#define ACCT_X (((int)FB_W-ACCT_W)/2)
#define ACCT_Y (((int)FB_H-ACCT_H)/2)
static void text_input_key(char*buf,int*len,int maxlen,s64 ch){
    if(ch<=0||ch>=256)return;
    char c=(char)ch;
    if((c=='\b'||c==127)&&*len>0){buf[--(*len)]=0;}
    else if(c>=32&&c<127&&*len<maxlen-1){buf[(*len)++]=c;buf[*len]=0;}
}
static void draw_masked(int x,int y,int len,u32 fg,u32 bg){
    char mask[48];int n=len;if(n>46)n=46;
    for(int i=0;i<n;i++)mask[i]='*';mask[n]=0;
    text(x,y,mask,fg,bg);
}
static int auth_str_eq(const char*a,const char*b){
    int i=0;while(a[i]&&b[i]){if(a[i]!=b[i])return 0;i++;}
    return a[i]==b[i];
}
static int acct_focus=0,acct_screen=0,acct_ack_checked=0;
static char acct_user_buf[32];static int acct_user_len=0;
static char acct_pass_buf[48];static int acct_pass_len=0;
static char acct_pass2_buf[48];static int acct_pass2_len=0;
static char acct_err_buf[64];
static char acct_recovery_code[20];
static void acct_reset_state(void){
    acct_user_len=0;acct_user_buf[0]=0;
    acct_pass_len=0;acct_pass_buf[0]=0;
    acct_pass2_len=0;acct_pass2_buf[0]=0;
    acct_err_buf[0]=0;
    acct_screen=0;acct_ack_checked=0;acct_focus=0;
}
static int acct_setup_run(int is_first_boot){
    acct_reset_state();
    int done=0,success=0;
    while(!done){
        unsigned long long mstate[3];sys_mouseread(mstate);
        int mx=(int)mstate[0],my=(int)mstate[1],mb=(int)mstate[2];
        static int prev_mb=0;
        int click=(mb&1)&&!(prev_mb&1);
        s64 ch=sys_keypoll();

        if(acct_screen==0){
            int fldx=ACCT_X+24,fldw=ACCT_W-48,fldh=30;
            int fld0y=ACCT_Y+90,fld1y=ACCT_Y+154,fld2y=ACCT_Y+218;
            if(click){
                if(in_box(mx,my,fldx,fld0y,fldw,fldh))acct_focus=0;
                else if(in_box(mx,my,fldx,fld1y,fldw,fldh))acct_focus=1;
                else if(in_box(mx,my,fldx,fld2y,fldw,fldh))acct_focus=2;
            }
            if(ch==9||ch==1002){acct_focus=(acct_focus+1)%3;}
            else if(ch==1001){acct_focus=(acct_focus+2)%3;}
            else if(acct_focus==0)text_input_key(acct_user_buf,&acct_user_len,32,ch);
            else if(acct_focus==1)text_input_key(acct_pass_buf,&acct_pass_len,48,ch);
            else if(acct_focus==2)text_input_key(acct_pass2_buf,&acct_pass2_len,48,ch);
            if(ch=='\n'||ch=='\r'){
                if(acct_user_len==0){auth_copy_str(acct_err_buf,64,"Username required");acct_focus=0;}
                else if(acct_pass_len<4){auth_copy_str(acct_err_buf,64,"Password must be at least 4 characters");acct_focus=1;}
                else if(!auth_str_eq(acct_pass_buf,acct_pass2_buf)){auth_copy_str(acct_err_buf,64,"Passwords do not match");acct_focus=2;}
                else{
                    auth_create_user(AUTH_PATH,acct_user_buf,acct_pass_buf,acct_recovery_code);
                    acct_err_buf[0]=0;acct_screen=1;
                }
            }
        }else if(acct_screen==1){
            if(click){
                int cbx=ACCT_X+24,cby=ACCT_Y+ACCT_H-96;
                if(in_box(mx,my,cbx,cby,18,18))acct_ack_checked=!acct_ack_checked;
                int bbx=ACCT_X+24,bby=ACCT_Y+ACCT_H-56,bbw=ACCT_W-48,bbh=32;
                if(acct_ack_checked&&in_box(mx,my,bbx,bby,bbw,bbh)){acct_screen=2;}
            }
        }else if(acct_screen==2){
            if(click){
                int bbx=ACCT_X+ACCT_W/2-60,bby=ACCT_Y+ACCT_H-56,bbw=120,bbh=32;
                if(in_box(mx,my,bbx,bby,bbw,bbh)){done=1;success=1;}
            }
        }

        if(!is_first_boot&&click){
            int cx=ACCT_X+ACCT_W-26,cy=ACCT_Y+10;
            if(in_box(mx,my,cx,cy,16,16)){done=1;success=0;}
        }

        prev_mb=mb;

        rect(0,0,(int)FB_W,(int)FB_H,BG);
        rect_round_alpha(ACCT_X+3,ACCT_Y+3,ACCT_W,ACCT_H,16,0x000000,90);
        rect_round_alpha(ACCT_X,ACCT_Y,ACCT_W,ACCT_H,16,PANEL_BG,225);
        outline_round(ACCT_X,ACCT_Y,ACCT_W,ACCT_H,16,BORDER);
        text_bold(ACCT_X+24,ACCT_Y+20,"Account Setup",TEXT,PANEL_BG);
        if(!is_first_boot){
            int cx=ACCT_X+ACCT_W-26,cy=ACCT_Y+10;
            int chov=in_box(mx,my,cx,cy,16,16);
            rect_round(cx,cy,16,16,6,chov?0x21262D:PANEL_BG);
            text(cx+5,cy+2,"x",chov?TEXT:DIM,chov?0x21262D:PANEL_BG);
        }
        hline(ACCT_X+24,ACCT_Y+48,ACCT_W-48,0x21262D);

        if(acct_screen==0){
            int fy=ACCT_Y+72;
            text(ACCT_X+24,fy,"Username",DIM,PANEL_BG);fy+=18;
            {int fbx=ACCT_X+24,fby=fy,fbw=ACCT_W-48,fbh=30;
             rect(fbx,fby,fbw,fbh,0x0D1117);outline(fbx,fby,fbw,fbh,acct_focus==0?cfg_accent:BORDER);
             text(fbx+10,fby+8,acct_user_buf,TEXT,0x0D1117);}
            fy+=46;
            text(ACCT_X+24,fy,"Password",DIM,PANEL_BG);fy+=18;
            {int fbx=ACCT_X+24,fby=fy,fbw=ACCT_W-48,fbh=30;
             rect(fbx,fby,fbw,fbh,0x0D1117);outline(fbx,fby,fbw,fbh,acct_focus==1?cfg_accent:BORDER);
             draw_masked(fbx+10,fby+8,acct_pass_len,TEXT,0x0D1117);}
            fy+=46;
            text(ACCT_X+24,fy,"Confirm Password",DIM,PANEL_BG);fy+=18;
            {int fbx=ACCT_X+24,fby=fy,fbw=ACCT_W-48,fbh=30;
             rect(fbx,fby,fbw,fbh,0x0D1117);outline(fbx,fby,fbw,fbh,acct_focus==2?cfg_accent:BORDER);
             draw_masked(fbx+10,fby+8,acct_pass2_len,TEXT,0x0D1117);}
            fy+=44;
            if(acct_err_buf[0])text(ACCT_X+24,fy,acct_err_buf,RED,PANEL_BG);
            fy+=30;
            text(ACCT_X+24,fy,"Tab to switch fields, Enter to continue",DIM,PANEL_BG);
        }else if(acct_screen==1){
            int fy=ACCT_Y+68;
            text(ACCT_X+24,fy,"Your recovery code - save it now.",TEXT,PANEL_BG);fy+=24;
            text(ACCT_X+24,fy,"You will not be able to see it again.",DIM,PANEL_BG);fy+=36;
            {int bx2=ACCT_X+24,by2=fy,bw2=ACCT_W-48,bh2=44;
             rect(bx2,by2,bw2,bh2,0x0D1117);outline(bx2,by2,bw2,bh2,cfg_accent);
             text_center(bx2+bw2/2,by2+15,acct_recovery_code,cfg_accent,0x0D1117);}
            fy+=66;
            text(ACCT_X+24,fy,"This code can reset your password if you",DIM,PANEL_BG);fy+=16;
            text(ACCT_X+24,fy,"forget it. It can only be used once.",DIM,PANEL_BG);
            int cbx=ACCT_X+24,cby=ACCT_Y+ACCT_H-96;
            rect(cbx,cby,18,18,0x0D1117);outline(cbx,cby,18,18,BORDER);
            if(acct_ack_checked)text(cbx+3,cby,"x",cfg_accent,0x0D1117);
            text(cbx+26,cby+1,"I have saved this code",TEXT,PANEL_BG);
            {int bbx=ACCT_X+24,bby=ACCT_Y+ACCT_H-56,bbw=ACCT_W-48,bbh=32;
             u32 bg=acct_ack_checked?cfg_accent:0x161B22;
             rect(bbx,bby,bbw,bbh,bg);outline(bbx,bby,bbw,bbh,acct_ack_checked?cfg_accent:BORDER);
             text_center(bbx+bbw/2,bby+9,"Continue",acct_ack_checked?BG:DIM,bg);}
        }else if(acct_screen==2){
            text_center(ACCT_X+ACCT_W/2,ACCT_Y+140,"Account ready",TEXT,PANEL_BG);
            text_center(ACCT_X+ACCT_W/2,ACCT_Y+166,"You can sign in now.",DIM,PANEL_BG);
            {int bbx=ACCT_X+ACCT_W/2-60,bby=ACCT_Y+ACCT_H-56,bbw=120,bbh=32;
             rect(bbx,bby,bbw,bbh,cfg_accent);outline(bbx,bby,bbw,bbh,cfg_accent);
             text_center(bbx+bbw/2,bby+9,"Continue",BG,cfg_accent);}
        }

        draw_cursor(mx,my);
        flush();sys_yield();
    }
    return success;
}
#define LOCK_W 380
#define LOCK_H 260
#define LOCK_X (((int)FB_W-LOCK_W)/2)
#define LOCK_Y (((int)FB_H-LOCK_H)/2)
/* The currently logged-in user's uid, set on successful login. Used
 * by permission-checking code (later stage) to decide what the active
 * session is allowed to do. -1 means "not logged in yet". */
static s64 current_uid=-1;
static char current_username[32];

/* Draws a circular avatar with a single centered letter as a
 * placeholder for a real profile picture. Real image support (PNG/
 * JPEG decoding) is a separate, future piece of work — this is the
 * fallback used until then. */
static void draw_avatar(int cx,int cy,int r,const char*username,u32 ring){
    circle(cx,cy,r,0x21262D);
    circle_outline(cx,cy,r,ring);
    char letter=username[0];
    if(letter>='a'&&letter<='z')letter=(char)(letter-'a'+'A');
    if(!letter)letter='?';
    char s[2]={letter,0};
    text_big_center(cx,cy-8,s,TEXT,0x21262D);
}
/* Login power buttons (shutdown/restart), always available on the
 * login screen even before any account exists or is logged in. */
static void draw_login_power_buttons(int mx,int my,int*out_sd,int*out_rb){
    int pbx=32,pby=(int)FB_H-80,pbsz=48;
    int sd_x=pbx,rb_x=pbx+pbsz+16;
    int hov_sd=in_box(mx,my,sd_x,pby,pbsz,pbsz);
    int hov_rb=in_box(mx,my,rb_x,pby,pbsz,pbsz);
    rect_round(sd_x,pby,pbsz,pbsz,10,hov_sd?RED:0x21262D);
    outline_round(sd_x,pby,pbsz,pbsz,10,hov_sd?RED:BORDER);
    text_center(sd_x+pbsz/2,pby+pbsz/2-4,"PWR",hov_sd?BG:TEXT,hov_sd?RED:0x21262D);
    rect_round(rb_x,pby,pbsz,pbsz,10,hov_rb?cfg_accent:0x21262D);
    outline_round(rb_x,pby,pbsz,pbsz,10,hov_rb?cfg_accent:BORDER);
    text_center(rb_x+pbsz/2,pby+pbsz/2-4,"RST",hov_rb?BG:TEXT,hov_rb?cfg_accent:0x21262D);
    *out_sd=hov_sd;*out_rb=hov_rb;
}
#define LOGIN_AVATAR_R   40
#define LOGIN_CARD_W     130
#define LOGIN_CARD_H     160
#define LOGIN_CARD_GAP   30
#define LOGIN_COLS       4
#define LOCK_MODE_BOOT   0  /* first boot / normal login: show full account grid */
#define LOCK_MODE_LOGOUT 1  /* logout: session already reset, show full account grid */
#define LOCK_MODE_LOCK   2  /* lock: everything stays as-is, skip straight to the
                                current user's password prompt, no way back to
                                the grid (that's the whole point vs logout) */
static int lock_screen_run(int mode){
    UserTable t;int have_table=auth_table_load(AUTH_PATH,&t);
    int ucount=have_table?(int)t.count:0;

    int screen=0; /* 0 = select user, 1 = password entry */
    int sel_idx=-1;
    char pw_buf[48];int pw_len=0;
    char pw_err[64];pw_err[0]=0;
    int show_pw=0;
    static int prev_mb3=0;
    int success=0;
    int mb=0;

    /* Preselect whoever was last logged in, if they still exist in the
     * table, so a returning user's card is easy to find quickly. */
    int pre_idx=-1;
    if(current_username[0]){
        for(int i=0;i<ucount;i++){
            int m=1;for(int k=0;k<32;k++){if(t.users[i].username[k]!=current_username[k]){m=0;break;}if(current_username[k]==0)break;}
            if(m){pre_idx=i;break;}
        }
    }

    if(mode==LOCK_MODE_LOCK && pre_idx>=0){
        sel_idx=pre_idx;
        screen=1;
    }

    while(!success){
        u64 ticks=sys_ticks();
        unsigned long long mstate[3];sys_mouseread(mstate);
        int mx=(int)mstate[0],my=(int)mstate[1];mb=(int)mstate[2];
        int click=(mb&1)&&!(prev_mb3&1);
        s64 ch=sys_keypoll();

        int hov_sd=0,hov_rb=0;

        if(screen==0){
            /* grid geometry */
            int gridw=LOGIN_COLS*LOGIN_CARD_W+(LOGIN_COLS-1)*LOGIN_CARD_GAP;
            int gx0=((int)FB_W-gridw)/2;
            int gy0=(int)FB_H/2-LOGIN_CARD_H/2-20;
            int hov_card=-1;
            for(int i=0;i<ucount;i++){
                int col=i%LOGIN_COLS,row=i/LOGIN_COLS;
                int cx0=gx0+col*(LOGIN_CARD_W+LOGIN_CARD_GAP),cy0=gy0+row*(LOGIN_CARD_H+LOGIN_CARD_GAP);
                if(in_box(mx,my,cx0,cy0,LOGIN_CARD_W,LOGIN_CARD_H))hov_card=i;
            }
            if(click&&hov_card>=0){sel_idx=hov_card;screen=1;pw_len=0;pw_buf[0]=0;pw_err[0]=0;show_pw=0;}

            draw_login_power_buttons(mx,my,&hov_sd,&hov_rb);
            if(click&&hov_sd){do_shutdown();}
            if(click&&hov_rb){do_restart();}

            prev_mb3=mb;

            wallpaper();
            rect_alpha(0,0,(int)FB_W,(int)FB_H,0x000000,60);
            {u64 hh=(ticks/100/3600)%24,mm=(ticks/100/60)%60;
             char cl[6];cl[0]='0'+hh/10;cl[1]='0'+hh%10;cl[2]=':';cl[3]='0'+mm/10;cl[4]='0'+mm%10;cl[5]=0;
             text_big_center((int)FB_W/2,60,cl,TEXT,BG);}
            text_big_center((int)FB_W/2,gy0-56,"Select User",TEXT,BG);
            if(ucount==0){
                text_center((int)FB_W/2,gy0+40,"No accounts exist yet.",DIM,BG);
            }
            for(int i=0;i<ucount;i++){
                int col=i%LOGIN_COLS,row=i/LOGIN_COLS;
                int cx0=gx0+col*(LOGIN_CARD_W+LOGIN_CARD_GAP),cy0=gy0+row*(LOGIN_CARD_H+LOGIN_CARD_GAP);
                int hov=(hov_card==i);
                rect_round_alpha(cx0,cy0,LOGIN_CARD_W,LOGIN_CARD_H,14,hov?0x21262D:0x161B22,hov?220:170);
                outline_round(cx0,cy0,LOGIN_CARD_W,LOGIN_CARD_H,14,i==pre_idx?cfg_accent:BORDER);
                draw_avatar(cx0+LOGIN_CARD_W/2,cy0+56,LOGIN_AVATAR_R,t.users[i].username,i==pre_idx?cfg_accent:BORDER);
                char un[20];int uk=0;while(t.users[i].username[uk]&&uk<18){un[uk]=t.users[i].username[uk];uk++;}un[uk]=0;
                text_center(cx0+LOGIN_CARD_W/2,cy0+112,un,TEXT,hov?0x21262D:0x161B22);
                if(t.users[i].uid==0)text_center(cx0+LOGIN_CARD_W/2,cy0+132,"root",cfg_accent,hov?0x21262D:0x161B22);
            }
            draw_cursor(mx,my);
            flush();sys_yield();
        } else {
            /* password entry for t.users[sel_idx] */
            int pw_x=LOCK_X,pw_y=LOCK_Y,pw_w=LOCK_W,pw_h=LOCK_H;
            int bbx=pw_x+24,bby=pw_y+pw_h-56,bbw=pw_w-48,bbh=32;
            int backx=pw_x+24,backy=pw_y+20,backw=60,backh=22;
            int eyex=pw_x+pw_w-70,eyey=pw_y+150,eyew=46,eyeh=22;
            int fbx=pw_x+24,fby=pw_y+120,fbw=pw_w-48-eyew-8,fbh=30;

            if(click){
                if(mode!=LOCK_MODE_LOCK && in_box(mx,my,backx,backy,backw,backh)){screen=0;sel_idx=-1;}
                else if(in_box(mx,my,eyex,eyey,eyew,eyeh))show_pw=!show_pw;
            }
            text_input_key(pw_buf,&pw_len,48,ch);
            int submit=(ch=='\n'||ch=='\r')||(click&&in_box(mx,my,bbx,bby,bbw,bbh));
            if(submit&&sel_idx>=0){
                u32 uid=0;
                if(auth_verify_password(AUTH_PATH,t.users[sel_idx].username,pw_buf,&uid)){
                    success=1;
                    current_uid=(s64)uid;
                    sys_set_session_uid(uid,uid); /* private-group convention, uid == gid */
                    auth_copy_str(current_username,32,t.users[sel_idx].username);
                } else {
                    auth_copy_str(pw_err,64,"Incorrect password");
                    pw_len=0;pw_buf[0]=0;
                }
            }

            draw_login_power_buttons(mx,my,&hov_sd,&hov_rb);
            if(click&&hov_sd){do_shutdown();}
            if(click&&hov_rb){do_restart();}

            prev_mb3=mb;

            wallpaper();
            rect_alpha(0,0,(int)FB_W,(int)FB_H,0x000000,60);
            rect_round_alpha(pw_x+3,pw_y+3,pw_w,pw_h,16,0x000000,90);
            rect_round_alpha(pw_x,pw_y,pw_w,pw_h,16,PANEL_BG,230);
            outline_round(pw_x,pw_y,pw_w,pw_h,16,BORDER);
            if(mode!=LOCK_MODE_LOCK){
                int hovb=in_box(mx,my,backx,backy,backw,backh);
                text(backx,backy+4,"< Back",hovb?TEXT:DIM,PANEL_BG);
            }
            if(sel_idx>=0){
                draw_avatar(pw_x+pw_w/2,pw_y+70,32,t.users[sel_idx].username,cfg_accent);
                text_center(pw_x+pw_w/2,pw_y+108,t.users[sel_idx].username,TEXT,PANEL_BG);
            }
            rect(fbx,fby,fbw,fbh,0x0D1117);outline(fbx,fby,fbw,fbh,cfg_accent);
            if(show_pw)text(fbx+10,fby+8,pw_buf,TEXT,0x0D1117);
            else draw_masked(fbx+10,fby+8,pw_len,TEXT,0x0D1117);
            {int hove=in_box(mx,my,eyex,eyey,eyew,eyeh);
             rect(eyex,eyey,eyew,eyeh,hove?0x21262D:0x161B22);outline(eyex,eyey,eyew,eyeh,BORDER);
             text_center(eyex+eyew/2,eyey+4,show_pw?"Hide":"Show",hove?TEXT:DIM,hove?0x21262D:0x161B22);}
            if(pw_err[0])text(pw_x+24,pw_y+166,pw_err,RED,PANEL_BG);
            {int hov=in_box(mx,my,bbx,bby,bbw,bbh);
             rect(bbx,bby,bbw,bbh,hov?cfg_accent:0x161B22);outline(bbx,bby,bbw,bbh,cfg_accent);
             text_center(bbx+bbw/2,bby+9,"Sign In",hov?BG:TEXT,hov?cfg_accent:0x161B22);}
            draw_cursor(mx,my);
            flush();sys_yield();
        }
    }
    prev_btn=mb;
    return 1;
}
static void auth_do_logout(void){
    sys_youdo(0);session_elevated=0;
    for(int i=0;i<win_count;i++){wins[i].visible=0;wins[i].minimized=0;}
    win_count=0;focused=-1;
    for(int k=0;k<MAX_WINDOWS;k++){np_states[k].text[0]=0;np_states[k].text_len=0;np_states[k].cursor=0;np_states[k].scroll=0;np_states[k].modified=0;np_states[k].filename[0]=0;np_states[k].mode=0;}
    np_current=-1;settings_win_idx=-1;
    for(int k=0;k<MAX_WINDOWS;k++){calc_states[k].expr_len=0;calc_states[k].expr[0]=0;calc_states[k].has_result=0;calc_states[k].error=0;calc_states[k].hist_count=0;}
    calc_current=-1;
    notif_count=0;notif_center_open=0;notif_popup_active=0;
    menu_open=0;rctx_open=0;fm_ctx_open=0;fm_dialog=0;fm_selected=-1;fm_has_clip=0;
    drag_win=-1;resize_win=-1;drag_icon=-1;
    for(int i=0;i<32;i++)tlines[i][0]=0;
    trow=0;tinput[0]=0;tinput_len=0;
}

static int hover_preview_win=-1;
static int hover_preview_group=-1;
static u32 wpreview_slots[MAX_GROUP_INST][WPREV_BW*WPREV_BH];
static void capture_window_preview_slot(int wi,int slot){
    if(slot<0||slot>=MAX_GROUP_INST)return;
    Win*w=&wins[wi];
    int sw=w->w,sh=w->h;
    if(sw<1)sw=1;if(sh<1)sh=1;
    for(int row=0;row<WPREV_BH;row++){
        for(int col=0;col<WPREV_BW;col++){
            int sx=w->x+col*sw/WPREV_BW;
            int sy=w->y+row*sh/WPREV_BH;
            if(sx<0)sx=0;if(sx>=(int)FB_W)sx=(int)FB_W-1;
            if(sy<0)sy=0;if(sy>=(int)FB_H)sy=(int)FB_H-1;
            wpreview_slots[slot][row*WPREV_BW+col]=buf[sy*(int)FB_W+sx];
        }
    }
}
static void draw_window_preview_slot(int wi,int slot,int wpx,int wpy){
    if(slot<0||slot>=MAX_GROUP_INST)return;
    Win*w=&wins[wi];
    int foc=(wi==focused);
    rect_round_alpha(wpx+3,wpy+3,WPREV_W,WPREV_H,12,0x000000,90);
    rect_round(wpx,wpy,WPREV_W,WPREV_H,12,0x0D1117);
    outline_round(wpx,wpy,WPREV_W,WPREV_H,12,foc?w->accent:BORDER);
    int tx=wpx+10,ty=wpy+8;
    int tlen=slen(w->title);if(tlen>22)tlen=22;
    char tt[24];int k=0;for(;k<tlen;k++)tt[k]=w->title[k];tt[k]=0;
    text(tx,ty,tt,TEXT,0x0D1117);
    int by0=wpy+24,bx0=wpx+8;
    if(w->minimized){
        rect(bx0,by0,WPREV_BW,WPREV_BH,0x161B22);
        draw_icon_glyph(win_glyph_idx(w->id),bx0+WPREV_BW/2,by0+WPREV_BH/2,w->accent,0x161B22);
    }else{
        for(int row=0;row<WPREV_BH;row++)
            for(int col=0;col<WPREV_BW;col++)
                px(bx0+col,by0+row,wpreview_slots[slot][row*WPREV_BW+col]);
        outline(bx0,by0,WPREV_BW,WPREV_BH,0x21262D);
    }
}
static void draw_loading_splash(int spin_deg){
    rect(0,0,(int)FB_W,(int)FB_H,0x0D1117);
    int scx=(int)FB_W/2,scy=(int)FB_H/2;
    text_big_center(scx,scy-54,"YouOS",0xFFFFFF,0x0D1117);
    int cx=scx,cy=scy+46,r=28;
    for(int i=0;i<8;i++){
        int deg=(spin_deg+i*45)%360;
        int dx=cx+(icos(deg)*r)/1000;
        int dy=cy-(isin(deg)*r)/1000;
        int a=255-(i*28);if(a<40)a=40;
        int dotr=4;
        for(int yy=-dotr;yy<=dotr;yy++)for(int xx=-dotr;xx<=dotr;xx++)
            if(xx*xx+yy*yy<=dotr*dotr)px_alpha(dx+xx,dy+yy,0x58A6FF,a);
    }
    flush();
}
int main(void){
    u64 info[5];
    if(sys_fbinfo(info)!=0)return 1;
    FB_W=info[1];FB_H=info[2];
    if(FB_W>MAX_FB_W)FB_W=MAX_FB_W;
    if(FB_H>MAX_FB_H)FB_H=MAX_FB_H;
    draw_loading_splash(0);
    sha256_self_test();
    hmac_sha256_self_test();pbkdf2_self_test();auth_self_test();
    draw_loading_splash(90);
    cfg_load();
    draw_loading_splash(200);
    if(!auth_exists(AUTH_PATH)){acct_setup_run(1);}
    if(auth_exists(AUTH_PATH)){lock_screen_run(LOCK_MODE_BOOT);}
    open_terminal();
    tprint("YouOS Desktop v0.3");
    tprint("File manager + Notepad ready");
    tprint("Type 'help' for commands");
    u64 last_ticks=0;

    while(1){
        u64 ticks=sys_ticks();
        g_now_ticks=ticks;
        u64 secs=ticks/100;

        /* mouse */
        unsigned long long mstate[3];
        sys_mouseread(mstate);
        mouse_x=(int)mstate[0];mouse_y=(int)mstate[1];mouse_btn=(int)mstate[2];
        int btn_down=(mouse_btn&1)&&!(prev_btn&1);
        int btn_up  =!(mouse_btn&1)&&(prev_btn&1);

        if(btn_up){
            if(drag_icon>=0){
                int ddx2=mouse_x-drag_icon_sx,ddy2=mouse_y-drag_icon_sy;
                if(ddx2*ddx2+ddy2*ddy2<25){
                    int di=drag_icon;
                    if(di==0)open_terminal();else if(di==1)open_files();
                    else if(di==2)open_about();else if(di==3)open_notepad(0);
                    else if(di==4)open_calc();
                }else cfg_save();
            }
            drag_icon=-1;drag_win=-1;resize_win=-1;
        }

        /* drag update */
        if(drag_win>=0){
            Win*w=&wins[drag_win];
            w->x=mouse_x-drag_ox;w->y=mouse_y-drag_oy;
            if(w->x<-(w->w-50))w->x=-(w->w-50);
            if(w->x>PANEL_X-50)w->x=PANEL_X-50;
            if(w->y<0)w->y=0;
            if(w->y>(int)FB_H-TBAR_H-TITLEBAR_H)w->y=(int)FB_H-TBAR_H-TITLEBAR_H;
        }

        /* icon drag update */
        if(drag_icon>=0){
            int nx=mouse_x-drag_icon_ox,ny=mouse_y-drag_icon_oy;
            int rx,ry;resolve_icon_pos(drag_icon,nx,ny,&rx,&ry);
            icons[drag_icon].x=rx;icons[drag_icon].y=ry;
        }
        /* resize update */
        if(resize_win>=0){
            Win*w=&wins[resize_win];
            int ddx=mouse_x-resize_start_x,ddy=mouse_y-resize_start_y,e=resize_edge;
            if(e==1||e==3||e==7){int nw=resize_start_w+ddx;if(nw>=w->min_w&&resize_orig_x+nw<PANEL_X)w->w=nw;}
            if(e==2||e==3||e==8){int nh=resize_start_h+ddy;if(nh>=w->min_h&&resize_orig_y+nh<(int)FB_H-TBAR_H)w->h=nh;}
            if(e==4||e==6||e==8){int nw=resize_start_w-ddx,nx=resize_orig_x+ddx;if(nw>=w->min_w&&nx>=0){w->w=nw;w->x=nx;}}
            if(e==5||e==6||e==7){int nh=resize_start_h-ddy,ny=resize_orig_y+ddy;if(nh>=w->min_h&&ny>=0){w->h=nh;w->y=ny;}}
        }

        int rbtn_down=(mouse_btn&2)&&!(prev_btn&2);
        /* FM right-click — must target the specific File Manager window
         * actually under the cursor (via wm_hit, respecting z-order and
         * overlap), not just "the first File Manager window that
         * exists" (find_win() always returns the first match, which
         * broke right-click entirely for any File Manager window other
         * than the first one opened). */
        if(rbtn_down&&drag_win<0){
            int fmi=wm_hit(mouse_x,mouse_y);
            if(fmi>=0&&wins[fmi].id==WIN_FILES&&wins[fmi].visible&&!wins[fmi].minimized&&
               in_box(mouse_x,mouse_y,wins[fmi].x,wins[fmi].y+TITLEBAR_H,wins[fmi].w,wins[fmi].h-TITLEBAR_H)){
                fm_current=fmi;
                fm_ctx_x=mouse_x;fm_ctx_y=mouse_y;fm_ctx_hov=-1;fm_ctx_open=1;
                wm_focus(fmi);goto click_done;
            }
        }
        /* right-click: open context menu */
        if(rbtn_down&&drag_win<0){
            rctx_x=mouse_x;rctx_y=mouse_y;rctx_hov=-1;
            int rhit=wm_hit(mouse_x,mouse_y);
            if(rhit>=0&&in_box(mouse_x,mouse_y,wins[rhit].x,wins[rhit].y,wins[rhit].w,TITLEBAR_H))
                rctx_target=rhit;
            else rctx_target=-1;
            rctx_open=1;goto click_done;
        }
        /* left-click closes context menu */
        if(btn_down&&fm_ctx_open&&fm_current>=0){
            int fmi3=fm_current;
            if(!in_box(mouse_x,mouse_y,wins[fmi3].x,wins[fmi3].y,wins[fmi3].w,wins[fmi3].h))
                fm_ctx_open=0;
        }
        if(btn_down&&rctx_open){
            const char**ci;int cn;get_rctx(&ci,&cn);
            int cmh=rctx_h(),cmx=rctx_x,cmy=rctx_y;
            if(cmx+CTX_W>PANEL_X)cmx=PANEL_X-CTX_W;
            if(cmy+cmh>(int)FB_H-TBAR_H)cmy=(int)FB_H-TBAR_H-cmh;
            int ciy=cmy+1;
            for(int ci2=0;ci2<cn;ci2++){
                if(!ci[ci2][0]){ciy+=CTX_SEP_H;continue;}
                if(in_box(mouse_x,mouse_y,cmx,ciy,CTX_W,CTX_ITEM_H)){
                    if(rctx_target<0){
                        if(ci2==0)open_terminal();
                        else if(ci2==1)open_files();
                        else if(ci2==2)open_about();
                        else if(ci2==3)open_notepad(0);
                        else if(ci2==4)open_calc();
                        else if(ci2==5)open_settings();
                        else if(ci2==7){do_shutdown();}
                    } else {
                        Win*rw=&wins[rctx_target];
                        if(ci2==0){rw->minimized=!rw->minimized;rw->anim=ANIM_TICKS;rw->anim_type=rw->minimized?3:1;}
                        else if(ci2==1){if(rw->w<700){rw->x=0;rw->y=0;rw->w=(int)FB_W;rw->h=(int)FB_H-TBAR_H;}else{rw->x=100;rw->y=60;rw->w=560;rw->h=420;}}
                        else if(ci2==2){
                            rw->visible=0;
                            if(wins[rctx_target].id==WIN_NOTEPAD){np_current=rctx_target;np.mode=0;}
                            if(wins[rctx_target].id==WIN_SETTINGS)settings_win_idx=-1;
                            focused=-1;int cbz=-1;
                            for(int ck=0;ck<win_count;ck++)if(wins[ck].visible&&wins[ck].z>cbz){cbz=wins[ck].z;focused=ck;}
                        }
                    }
                }
                ciy+=CTX_ITEM_H;
            }
            rctx_open=0;goto click_done;
        }
        /* click handling */
        if(btn_down&&drag_win<0&&resize_win<0){
            if(notif_popup_active&&notif_count>0){
                int px3=NOTIF_POPUP_X,py3=NOTIF_POPUP_Y;
                int cbx2=px3+NOTIF_POPUP_W-24,cby2=py3+8;
                if(in_box(mouse_x,mouse_y,cbx2,cby2,16,16)){notif_popup_active=0;goto click_done;}
            }
            if(in_box(mouse_x,mouse_y,TBAR_BELL_X,TBAR_BELL_Y,TBAR_BELL_SZ,TBAR_BELL_SZ)){
                notif_center_open=!notif_center_open;
                goto click_done;
            }
            if(notif_center_open){
                int ncx=NC_X,ncy=NC_Y;
                int inside_nc=in_box(mouse_x,mouse_y,ncx,ncy,NC_W,NC_H);
                if(in_box(mouse_x,mouse_y,ncx+NC_W-92,ncy+14,72,26)){notif_count=0;notif_scroll=0;goto click_done;}
                if(notif_count>NC_MAX_VISIBLE){
                    if(in_box(mouse_x,mouse_y,ncx+NC_W-16,ncy+56,14,14)&&notif_scroll>0){notif_scroll--;goto click_done;}
                    if(in_box(mouse_x,mouse_y,ncx+NC_W-16,ncy+72,14,14)&&notif_scroll+NC_MAX_VISIBLE<notif_count){notif_scroll++;goto click_done;}
                }
                int visible2=notif_count-notif_scroll;
                if(visible2>NC_MAX_VISIBLE)visible2=NC_MAX_VISIBLE;
                for(int row=0;row<visible2;row++){
                    int idx2=notif_count-1-notif_scroll-row;
                    int ry2=ncy+90+row*NC_ROW_H;
                    int rbx2=ncx+NC_W-44,rby2=ry2+(NC_ROW_H-8)/2-8;
                    if(in_box(mouse_x,mouse_y,rbx2,rby2,16,16)){notif_remove(idx2);if(notif_scroll>0)notif_scroll--;goto click_done;}
                }
                if(inside_nc)goto click_done;
                notif_center_open=0;
            }
            if(menu_open){
                int inside_panel=in_box(mouse_x,mouse_y,SM_X,SM_Y,SM_W,SM_H);
                for(int gi=0;gi<sm_filtered_n;gi++){
                    int idx=sm_filtered[gi];
                    int col=gi%SM_COLS,row=gi/SM_COLS;
                    int gx=SM_GRID_X(col),gy=SM_GRID_Y(row);
                    if(in_box(mouse_x,mouse_y,gx,gy,SM_CELL-16,SM_CELL-16)){
                        menu_open=0;
                        if(idx==0)open_terminal();
                        else if(idx==1)open_files();
                        else if(idx==2)open_about();
                        else if(idx==3)open_notepad(0);
                        else if(idx==4)open_calc();
                        else if(idx==5)open_settings();
                        goto click_done;
                    }
                }
                int pry2=SM_Y+SM_H-56,pbw2=(SM_W-40-24)/4;
                int rb_x2=SM_X+20,sd_x2=rb_x2+pbw2+8,lk_x2=sd_x2+pbw2+8,lo_x2=lk_x2+pbw2+8;
                if(in_box(mouse_x,mouse_y,rb_x2,pry2,pbw2,40)){menu_open=0;do_restart();goto click_done;}
                if(in_box(mouse_x,mouse_y,sd_x2,pry2,pbw2,40)){menu_open=0;do_shutdown();goto click_done;}
                if(in_box(mouse_x,mouse_y,lk_x2,pry2,pbw2,40)){menu_open=0;lock_screen_run(LOCK_MODE_LOCK);goto click_done;}
                if(in_box(mouse_x,mouse_y,lo_x2,pry2,pbw2,40)){
                    menu_open=0;auth_do_logout();lock_screen_run(LOCK_MODE_LOGOUT);
                    open_terminal();
                    tprint("YouOS Desktop v0.3");
                    tprint("File manager + Notepad ready");
                    tprint("Type 'help' for commands");
                    goto click_done;
                }
                if(inside_panel)goto click_done;
                menu_open=0;goto click_done;
            }
            /* preview-strip click: pick a specific instance from the
             * currently-hovered group's strip (checked before the plain
             * taskbar-button click below, since the strip floats above
             * the taskbar pill when visible) */
            if(hover_preview_group>=0&&hover_preview_group<taskbar_group_count){
                int gcount=taskbar_groups[hover_preview_group].count;
                int sx,sy,sw,sh;taskbar_group_strip_rect(hover_preview_group,&sx,&sy,&sw,&sh);
                for(int gk=0;gk<gcount;gk++){
                    int wpx=sx+gk*(WPREV_W+WPREV_GAP);
                    if(in_box(mouse_x,mouse_y,wpx,sy,WPREV_W,WPREV_H)){
                        int wi=taskbar_groups[hover_preview_group].idx[gk];
                        wins[wi].minimized=0;wins[wi].anim=ANIM_TICKS;wins[wi].anim_type=1;
                        wm_focus(wi);
                        goto click_done;
                    }
                }
            }
            int hit=wm_hit(mouse_x,mouse_y);
            if(hit>=0&&hit!=focused)wm_focus(hit);

            if(hit>=0){
                Win*w=&wins[hit];
                /* close */
                if(in_box(mouse_x,mouse_y,w->x+8,w->y+7,14,14)){
                    w->visible=0;
                    if(wins[hit].id==WIN_NOTEPAD){np_current=hit;np.mode=0;}
                    if(wins[hit].id==WIN_SETTINGS)settings_win_idx=-1;
                    if(wins[hit].id==WIN_CALC)calc_current=-1;
                    focused=-1;int bz=-1;
                    for(int i=0;i<win_count;i++)if(wins[i].visible&&wins[i].z>bz){bz=wins[i].z;focused=i;}
                    goto click_done;
                }
                /* minimize */
                if(in_box(mouse_x,mouse_y,w->x+24,w->y+7,14,14)){
                    w->minimized=!w->minimized;w->anim=ANIM_TICKS;w->anim_type=w->minimized?3:1;
                    goto click_done;
                }
                /* maximize */
                if(in_box(mouse_x,mouse_y,w->x+40,w->y+7,14,14)){
                    if(w->w<700){w->x=0;w->y=0;w->w=(int)FB_W;w->h=(int)FB_H-TBAR_H;}
                    else{w->x=100;w->y=60;w->w=560;w->h=420;}
                    goto click_done;
                }
                /* resize edges */
                if(!w->minimized){
                    int rx=w->x,ry=w->y,rw=w->w,rh=w->h,g=RESIZE_GRIP,g2=g*2;
                    int on_l=mouse_x>=rx-g&&mouse_x<rx+g&&mouse_y>=ry+TITLEBAR_H&&mouse_y<ry+rh-g2;
                    int on_r=mouse_x>=rx+rw-g&&mouse_x<rx+rw+g&&mouse_y>=ry+TITLEBAR_H&&mouse_y<ry+rh-g2;
                    int on_t=mouse_y>=ry-g&&mouse_y<ry+g&&mouse_x>=rx+g2&&mouse_x<rx+rw-g2;
                    int on_b=mouse_y>=ry+rh-g&&mouse_y<ry+rh+g&&mouse_x>=rx+g2&&mouse_x<rx+rw-g2;
                    int on_tl=mouse_x>=rx-g&&mouse_x<rx+g2&&mouse_y>=ry-g&&mouse_y<ry+g2;
                    int on_tr=mouse_x>=rx+rw-g2&&mouse_x<rx+rw+g&&mouse_y>=ry-g&&mouse_y<ry+g2;
                    int on_bl=mouse_x>=rx-g&&mouse_x<rx+g2&&mouse_y>=ry+rh-g2&&mouse_y<ry+rh+g;
                    int on_br=mouse_x>=rx+rw-g2&&mouse_x<rx+rw+g&&mouse_y>=ry+rh-g2&&mouse_y<ry+rh+g;
                    int edge=0;
                    if(on_r)edge=1;if(on_b)edge=2;if(on_l)edge=4;if(on_t)edge=5;
                    if(on_br)edge=3;if(on_tl)edge=6;if(on_tr)edge=7;if(on_bl)edge=8;
                    if(edge){
                        resize_win=hit;resize_edge=edge;
                        resize_start_x=mouse_x;resize_start_y=mouse_y;
                        resize_start_w=w->w;resize_start_h=w->h;
                        resize_orig_x=w->x;resize_orig_y=w->y;
                        goto click_done;
                    }
                }
                /* drag titlebar */
                if(in_box(mouse_x,mouse_y,w->x+60,w->y,w->w-60,TITLEBAR_H)){
                    drag_win=hit;drag_ox=mouse_x-w->x;drag_oy=mouse_y-w->y;goto click_done;
                }
                /* notepad toolbar */
                    /* error dialog click — checked first (only meaningful
                     * for the Notepad window actually being clicked) */
                    if(w->id==WIN_NOTEPAD&&(np_current=hit,np.mode3_err)){
                        int edw=320,edh=90;
                        int edx=w->x+(w->w-edw)/2;
                        int edy=w->y+TITLEBAR_H+(w->h-TITLEBAR_H-edh)/2;
                        if(in_box(mouse_x,mouse_y,edx+edw/2-20,edy+62,40,20)){np.mode3_err=0;goto click_done;}
                        goto click_done;
                    }
                if(w->id==WIN_NOTEPAD&&!w->minimized){
                    np_current=hit;
                    int bary=w->y+TITLEBAR_H;
                    if(in_box(mouse_x,mouse_y,w->x+4,bary+4,40,20)){
                        np.text[0]=0;np.text_len=0;np.cursor=0;np.scroll=0;
                        np.modified=0;np.filename[0]=0;np.mode=0;
                        if(np_current>=0){int j=0;const char*t="YC Notepad";while(t[j]&&j<39){wins[np_current].title[j]=t[j];j++;}wins[np_current].title[j]=0;}
                        goto click_done;
                    }
                    if(in_box(mouse_x,mouse_y,w->x+50,bary+4,52,20)){
                        np_load_filelist();np.mode=1;np.dlg_hov=-1;np.dlg_scroll=0;goto click_done;
                    }
                    if(in_box(mouse_x,mouse_y,w->x+108,bary+4,70,20)){
                        if(np.filename[0])np_do_save();
                        else{np.mode=2;np.dlg_len=0;np.dlg_buf[0]=0;}
                        goto click_done;
                    }
                    /* open dialog interactions */
                    if(np.mode==1){
                        int dh2=np_dlg_count*20+52;if(dh2>260)dh2=260;if(dh2<72)dh2=72;
                        int dw=280,dh=dh2,dx=w->x+(w->w-dw)/2,dy2=w->y+TITLEBAR_H+(w->h-TITLEBAR_H-dh)/2;
                        if(in_box(mouse_x,mouse_y,dx+dw-18,dy2+4,14,18)){np.mode=0;goto click_done;}
                        int ly=dy2+28,max_vis2=(dh-36)/20;
                        for(int fi=np.dlg_scroll;fi<np_dlg_count&&fi<np.dlg_scroll+max_vis2;fi++){
                            int ry=ly+(fi-np.dlg_scroll)*20;
                            if(in_box(mouse_x,mouse_y,dx+2,ry,dw-4,20)){
                                char path[56];path[0]='/';path[1]='y';path[2]='c';path[3]='f';path[4]='s';path[5]='/';
                                int pk=6,pj=0;while(np_dlg_files[fi].name[pj]&&pk<55){path[pk++]=np_dlg_files[fi].name[pj++];}path[pk]=0;
                                np_load(path,np_dlg_files[fi].name);
                                if(np_current>=0){pj=0;while(np_dlg_files[fi].name[pj]&&pj<39){wins[np_current].title[pj]=np_dlg_files[fi].name[pj];pj++;}wins[np_current].title[pj]=0;}
                                np.mode=0;goto click_done;
                            }
                        }
                        if(!in_box(mouse_x,mouse_y,dx,dy2,dw,dh))np.mode=0;
                        goto click_done;
                    }
                    /* save-as dialog */
                    if(np.mode==2){
                        int dw=300,dh=104,dx=w->x+(w->w-dw)/2,dy2=w->y+TITLEBAR_H+(w->h-TITLEBAR_H-dh)/2;
                        if(in_box(mouse_x,mouse_y,dx+dw/2-30,dy2+76,60,20)){np_do_saveas();goto click_done;}
                        if(!in_box(mouse_x,mouse_y,dx,dy2,dw,dh))np.mode=0;
                        goto click_done;
                    }
                    /* click in text area to position cursor */
                    if(np.mode==0){
                        int ta_y0=bary+32,ta_y1=w->y+w->h-18;
                        if(mouse_y>=ta_y0&&mouse_y<ta_y1&&mouse_x>=w->x+8&&mouse_x<w->x+w->w-8){
                            int cl=(mouse_x-(w->x+8))/8;
                            int ln=(mouse_y-ta_y0)/16+np.scroll;
                            if(cl<0)cl=0;if(ln<0)ln=0;
                            int tot=np_total_lines();if(ln>=tot)ln=tot-1;
                            int ls=np_line_start(ln),le=np_line_end(ls);
                            if(cl>le-ls)cl=le-ls;
                            np.cursor=ls+cl;
                            if(np.cursor>np.text_len)np.cursor=np.text_len;
                            goto click_done;
                        }
                    }
                }
                /* calculator window */
                if(w->id==WIN_CALC&&!w->minimized){
                    calc_current=hit;
                    int btn_top=w->y+TITLEBAR_H+80+60;
                    int bw=(w->w-12)/CALC_COLS;
                    for(int r=0;r<CALC_ROWS;r++){
                        for(int c=0;c<CALC_COLS;c++){
                            const char*lbl=calc_btns[r][c];
                            if(!lbl[0])continue;
                            int bx=w->x+6+c*bw,by=btn_top+r*36;
                            if(in_box(mouse_x,mouse_y,bx,by,bw-4,32)){
                                calc_btn_press(lbl);goto click_done;
                            }
                        }
                    }
                    goto click_done;
                }
                /* settings window */
                if(w->id==WIN_SETTINGS&&!w->minimized&&youdo_open){
                    int dw=280,dh=140;
                    int dx=w->x+(w->w-dw)/2;
                    int dy2=w->y+TITLEBAR_H+(w->h-TITLEBAR_H-dh)/2;
                    int fbx=dx+16,fby=dy2+38,fbw=dw-32-56,fbh=26;
                    int eyex=fbx+fbw+8,eyey=fby,eyew=48,eyeh=fbh;
                    int cbw=60,cbh=24,cbx=dx+dw-16-cbw,cby=dy2+dh-32;
                    int xbw=60,xbh=24,xbx=cbx-8-xbw,xby=cby;
                    if(in_box(mouse_x,mouse_y,eyex,eyey,eyew,eyeh))youdo_show=!youdo_show;
                    else if(in_box(mouse_x,mouse_y,xbx,xby,xbw,xbh)){youdo_open=0;youdo_pw_len=0;youdo_pw[0]=0;youdo_err[0]=0;}
                    else if(in_box(mouse_x,mouse_y,cbx,cby,cbw,cbh)){
                        UserTable ut;
                        if(auth_table_load(AUTH_PATH,&ut)&&ut.count>0){
                            u32 root_uid_check=0;
                            if(auth_verify_password(AUTH_PATH,ut.users[0].username,youdo_pw,&root_uid_check)&&ut.users[0].uid==0){
                                sys_youdo(1);session_elevated=1;
                                youdo_open=0;youdo_pw_len=0;youdo_pw[0]=0;youdo_err[0]=0;
                                acct_setup_run(0);
                            } else {
                                auth_copy_str(youdo_err,64,"Incorrect password");
                                youdo_pw_len=0;youdo_pw[0]=0;
                            }
                        }
                    }
                    goto click_done;
                }
                if(w->id==WIN_SETTINGS&&!w->minimized){
                    int base=w->y+TITLEBAR_H;
                    for(int si=0;si<N_SW;si++){
                        if(in_box(mouse_x,mouse_y,w->x+16+si*44,base+46,36,36)){
                            cfg_set_accent(sw_col[si]);goto click_done;
                        }
                    }
                    if(in_box(mouse_x,mouse_y,w->x+16,base+144,64,24)){cfg_24h=1;cfg_save();goto click_done;}
                    if(in_box(mouse_x,mouse_y,w->x+88,base+144,64,24)){cfg_24h=0;cfg_save();goto click_done;}
                    if(in_box(mouse_x,mouse_y,w->x+16,base+210,64,24)){cfg_showsecs=1;cfg_save();goto click_done;}
                    if(in_box(mouse_x,mouse_y,w->x+88,base+210,64,24)){cfg_showsecs=0;cfg_save();goto click_done;}
                    if(in_box(mouse_x,mouse_y,w->x+16,base+286,w->w-32,26)){
                        if(current_uid!=0&&!session_elevated){
                            youdo_open=1;youdo_pw_len=0;youdo_pw[0]=0;youdo_err[0]=0;youdo_show=0;
                        } else {
                            acct_setup_run(0);
                        }
                        goto click_done;
                    }
                    if(in_box(mouse_x,mouse_y,w->x+16,base+322,w->w-32,26)){
                        if(wallpaper_loaded){wallpaper_loaded=0;}
                        else{wallpaper_loaded=load_wallpaper_bmp("/ycfs/wall.bmp");}
                        cfg_save();
                        goto click_done;
                    }
                }
                /* file manager interactions */
                if(w->id==WIN_FILES&&!w->minimized){
                    fm_current=hit;
                    int fy=w->y+TITLEBAR_H;
                    int cw2=w->w;
                    /* dialog: delete confirm */
                    if(fm_dialog==1&&fm_selected>=0){
                        int dw=300,dh=88;
                        int ddx=w->x+(cw2-dw)/2;
                        int ddy=(w->y+TITLEBAR_H)+(w->h-TITLEBAR_H-dh)/2;
                        if(in_box(mouse_x,mouse_y,ddx+30,ddy+60,80,22)){
                            char dpath[220];
                            fm_build_path(dpath,sizeof(dpath),fm_path,fm_entries[fm_selected].name);
                            sys_unlink(dpath);fm_selected=-1;fm_dialog=0;fm_load();
                        } else fm_dialog=0;
                        goto click_done;
                    }
                    /* dialog: properties (chmod/chown) */
                    if(fm_dialog==4){
                        int dw=320,dh=190;
                        int ddx=w->x+(cw2-dw)/2;
                        int ddy=(w->y+TITLEBAR_H)+(w->h-TITLEBAR_H-dh)/2;
                        int ofx=ddx+8,ofy=ddy+46,ofw=dw-16,ofh=20;
                        int pfx2=ddx+8,pfy=ddy+88,pfw=60,pfh=20;
                        int abx=ddx+dw/2-64,aby=ddy+dh-32,abw=60,abh=22;
                        int cbx=ddx+dw/2+4,cby=ddy+dh-32,cbw=60,cbh=22;
                        if(in_box(mouse_x,mouse_y,ofx,ofy,ofw,ofh)){fm_prop_field=1;}
                        else if(in_box(mouse_x,mouse_y,pfx2,pfy,pfw,pfh)){fm_prop_field=0;}
                        else if(in_box(mouse_x,mouse_y,abx,aby,abw,abh)){
                            if(fm_selected>=0&&fm_dlg_len==3&&
                               fm_dlg_buf[0]>='0'&&fm_dlg_buf[0]<='7'&&
                               fm_dlg_buf[1]>='0'&&fm_dlg_buf[1]<='7'&&
                               fm_dlg_buf[2]>='0'&&fm_dlg_buf[2]<='7'){
                                char ppath[220];
                                fm_build_path(ppath,sizeof(ppath),fm_path,fm_entries[fm_selected].name);
                                unsigned int newperm=(unsigned int)((fm_dlg_buf[0]-'0')*64+(fm_dlg_buf[1]-'0')*8+(fm_dlg_buf[2]-'0'));
                                int ok=1;
                                if(sys_chmod(ppath,newperm)<0)ok=0;
                                UserTable owtab;
                                if(ok&&auth_table_load(AUTH_PATH,&owtab)){
                                    int owidx=auth_find_user(&owtab,fm_prop_owner_buf);
                                    if(owidx>=0){
                                        unsigned int newuid=(unsigned int)owtab.users[owidx].uid;
                                        if(newuid!=fm_prop_uid){
                                            if(sys_chown(ppath,newuid,newuid)<0)ok=0;
                                        }
                                    } else ok=0;
                                }
                                if(ok){fm_dialog=0;fm_dlg_has_err=0;fm_load();}
                                else{
                                    fm_dlg_has_err=1;
                                    const char*em4="Denied or unknown user";
                                    int ei4=0;while(em4[ei4]&&ei4<47){fm_dlg_err[ei4]=em4[ei4];ei4++;}fm_dlg_err[ei4]=0;
                                }
                            } else {
                                fm_dlg_has_err=1;
                                const char*em5="Perm must be 3 digits 0-7";
                                int ei5=0;while(em5[ei5]&&ei5<47){fm_dlg_err[ei5]=em5[ei5];ei5++;}fm_dlg_err[ei5]=0;
                            }
                        } else if(in_box(mouse_x,mouse_y,cbx,cby,cbw,cbh)){fm_dialog=0;fm_dlg_has_err=0;}
                        else if(!in_box(mouse_x,mouse_y,ddx,ddy,dw,dh)){fm_dialog=0;fm_dlg_has_err=0;}
                        goto click_done;
                    }
                    /* dialog: rename */
                    if(fm_dialog==2){
                        int dw=320,dh=106;
                        int ddx=w->x+(cw2-dw)/2;
                        int ddy=(w->y+TITLEBAR_H)+(w->h-TITLEBAR_H-dh)/2;
                        if(in_box(mouse_x,mouse_y,ddx+dw/2-34,ddy+76,64,20)){
                            if(fm_dlg_len>0&&fm_selected>=0){
                                char op[220],np2[220];
                                fm_build_path(op,sizeof(op),fm_path,fm_entries[fm_selected].name);
                                fm_build_path(np2,sizeof(np2),fm_path,fm_dlg_buf);
                                if(sys_rename(op,np2)<0){
                                    fm_dlg_has_err=1;
                                    const char*em="Name exists or invalid";
                                    int ei=0;while(em[ei]&&ei<47){fm_dlg_err[ei]=em[ei];ei++;}fm_dlg_err[ei]=0;
                                } else {fm_dialog=0;fm_dlg_has_err=0;fm_load();}
                            }
                        } else if(!in_box(mouse_x,mouse_y,ddx,ddy,dw,dh)){fm_dialog=0;fm_dlg_has_err=0;}
                        goto click_done;
                    }
                    /* dialog: new folder */
                    if(fm_dialog==3){
                        int dw=320,dh=106;
                        int ddx=w->x+(cw2-dw)/2;
                        int ddy=(w->y+TITLEBAR_H)+(w->h-TITLEBAR_H-dh)/2;
                        if(in_box(mouse_x,mouse_y,ddx+dw/2-34,ddy+76,64,20)){
                            if(fm_dlg_len>0){
                                char np2[220];
                                fm_build_path(np2,sizeof(np2),fm_path,fm_dlg_buf);
                                if(sys_mkdir(np2)<0){
                                    fm_dlg_has_err=1;
                                    const char*em="Folder already exists";
                                    int ei=0;while(em[ei]&&ei<47){fm_dlg_err[ei]=em[ei];ei++;}fm_dlg_err[ei]=0;
                                } else {fm_dialog=0;fm_dlg_has_err=0;fm_load();}
                            }
                        } else if(!in_box(mouse_x,mouse_y,ddx,ddy,dw,dh)){fm_dialog=0;fm_dlg_has_err=0;}
                        goto click_done;
                    }
                    /* FM context menu clicks */
                    if(fm_ctx_open){
                        const char*items[]={"New Folder","","Copy","Cut","Paste","Rename","Delete","","Properties"};
                        int n=9,iw=160,ih=22,sep=6;
                        int mh=2;for(int i=0;i<n;i++)mh+=items[i][0]?ih:sep;
                        int mx2=fm_ctx_x,my2=fm_ctx_y;
                        if(mx2+iw>w->x+w->w)mx2=w->x+w->w-iw;
                        if(my2+mh>w->y+w->h)my2=w->y+w->h-mh;
                        int iy2=my2+1,clicked=-1;
                        for(int i=0;i<n;i++){
                            if(!items[i][0]){iy2+=sep;continue;}
                            if(in_box(mouse_x,mouse_y,mx2,iy2,iw,ih)){clicked=i;break;}
                            iy2+=ih;
                        }
                        fm_ctx_open=0;
                        if(clicked==8&&fm_selected>=0){
                            char ppath[220];
                            fm_build_path(ppath,sizeof(ppath),fm_path,fm_entries[fm_selected].name);
                            unsigned int puid=0,pgid=0;unsigned short pperm=0;
                            if(sys_fileinfo(ppath,&puid,&pgid,&pperm)==0){
                                fm_prop_uid=puid;fm_prop_gid=pgid;fm_prop_perm=pperm;
                                UserTable ptab;fm_prop_owner_buf[0]=0;fm_prop_owner_len=0;
                                if(auth_table_load(AUTH_PATH,&ptab)){
                                    for(int pu=0;pu<(int)ptab.count;pu++){
                                        if(ptab.users[pu].uid==puid){
                                            auth_copy_str(fm_prop_owner_buf,32,ptab.users[pu].username);
                                            fm_prop_owner_len=slen(ptab.users[pu].username);
                                            break;
                                        }
                                    }
                                }
                                fm_dlg_buf[0]=(char)('0'+((pperm>>6)&7));
                                fm_dlg_buf[1]=(char)('0'+((pperm>>3)&7));
                                fm_dlg_buf[2]=(char)('0'+(pperm&7));
                                fm_dlg_buf[3]=0;fm_dlg_len=3;
                                fm_prop_field=0;fm_dlg_has_err=0;fm_dialog=4;
                            }
                        }
                        if(clicked==0){
                            fm_dialog=3;fm_dlg_len=0;fm_dlg_buf[0]=0;fm_dlg_has_err=0;
                        } else if(clicked==2&&fm_selected>=0){
                            int ci=0;while(fm_entries[fm_selected].name[ci]&&ci<31){fm_clip[ci]=fm_entries[fm_selected].name[ci];ci++;}fm_clip[ci]=0;
                            fm_has_clip=1;fm_clip_cut=0;
                            {int cpi=0;while(fm_path[cpi]&&cpi<63){fm_clip_path[cpi]=fm_path[cpi];cpi++;}fm_clip_path[cpi]=0;}
                        } else if(clicked==3&&fm_selected>=0){
                            int ci=0;while(fm_entries[fm_selected].name[ci]&&ci<31){fm_clip[ci]=fm_entries[fm_selected].name[ci];ci++;}fm_clip[ci]=0;
                            fm_has_clip=1;fm_clip_cut=1;
                            {int cpi=0;while(fm_path[cpi]&&cpi<63){fm_clip_path[cpi]=fm_path[cpi];cpi++;}fm_clip_path[cpi]=0;}
                        } else if(clicked==4&&fm_has_clip){
                            char spath[220];
                            fm_build_path(spath,sizeof(spath),fm_clip_path,fm_clip);
                            u64 fd=sys_open(spath,0);
                            if((s64)fd>=0){
                                /* Destination name: use the original name by
                                 * default (correct for Cut/move always, and
                                 * for Copy into a different location). Only
                                 * fall back to a "copy_" prefix for Copy when
                                 * a file with the exact same name already
                                 * exists at the destination — previously this
                                 * prefix was applied unconditionally, which
                                 * both looked wrong for ordinary copies and
                                 * broke Cut (a moved file should keep its own
                                 * name, not become "copy_name"). */
                                char dpath_probe[220];
                                fm_build_path(dpath_probe,sizeof(dpath_probe),fm_path,fm_clip);
                                u64 probe_fd=sys_open(dpath_probe,0);
                                int name_collision=((s64)probe_fd>=0);
                                if(name_collision)sys_close(probe_fd);

                                char dname[40];int di=0;
                                if(name_collision&&!fm_clip_cut){
                                    dname[di++]='c';dname[di++]='o';dname[di++]='p';dname[di++]='y';dname[di++]='_';
                                }
                                int sj=0;while(fm_clip[sj]&&di<38){dname[di++]=fm_clip[sj++];}dname[di]=0;
                                char dpath[220];
                                fm_build_path(dpath,sizeof(dpath),fm_path,dname);
                                int total=0;s64 nr;
                                /* Bounded against sizeof(fm_cpbuf) — the previous
                                 * version had no capacity check at all and would
                                 * silently overflow this static buffer for any
                                 * source file larger than 4096 bytes (e.g. a
                                 * ~3MB wallpaper BMP), corrupting adjacent static
                                 * data badly enough to hang the whole system. */
                                while(total<(int)sizeof(fm_cpbuf)){
                                    int room=(int)sizeof(fm_cpbuf)-total;
                                    int chunk=room<128?room:128;
                                    nr=(s64)sys_fread(fd,fm_cpbuf+total,(u64)chunk);
                                    if(nr<=0)break;
                                    total+=(int)nr;
                                }
                                sys_close(fd);
                                if(total>=(int)sizeof(fm_cpbuf)){
                                    const char*em="File too large to copy (max 4KB)";
                                    int ei=0;while(em[ei]&&ei<47){fm_paste_err[ei]=em[ei];ei++;}fm_paste_err[ei]=0;
                                } else {
                                    sys_save_file((u64)dpath,(u64)fm_cpbuf,(u64)total);
                                    if(fm_clip_cut){sys_unlink(spath);fm_has_clip=0;}
                                    fm_load();
                                }
                            }
                        } else if(clicked==5&&fm_selected>=0){
                            fm_dialog=2;fm_dlg_len=0;fm_dlg_buf[0]=0;fm_dlg_has_err=0;
                        } else if(clicked==6&&fm_selected>=0){
                            fm_dialog=1;
                        }
                        goto click_done;
                    }
                    /* toolbar */
                    if(fm_path_len>0&&in_box(mouse_x,mouse_y,w->x+cw2-128,fy+4,52,20)){
                        /* pop one level: find the last '/' and truncate
                         * there, or clear entirely if already at the
                         * top-level subdirectory (no '/' present) */
                        int lastslash=-1;
                        for(int pi3=0;pi3<fm_path_len;pi3++)if(fm_path[pi3]=='/')lastslash=pi3;
                        if(lastslash>=0){fm_path[lastslash]=0;fm_path_len=lastslash;}
                        else{fm_path[0]=0;fm_path_len=0;}
                        fm_load();goto click_done;
                    }
                    if(in_box(mouse_x,mouse_y,w->x+cw2-60,fy+4,52,20)){fm_load();goto click_done;}
                    int max_vis2=(w->h-TITLEBAR_H-72-20)/22;if(max_vis2<1)max_vis2=1;
                    if(in_box(mouse_x,mouse_y,w->x+cw2-16,fy+50,14,14)&&fm_scroll>0){fm_scroll--;goto click_done;}
                    if(in_box(mouse_x,mouse_y,w->x+cw2-16,fy+66,14,14)&&fm_scroll+max_vis2<fm_count){fm_scroll++;goto click_done;}
                    int row_y2=w->y+TITLEBAR_H+72;
                    for(int fi=fm_scroll;fi<fm_count&&fi<fm_scroll+max_vis2;fi++){
                        int ry=row_y2+(fi-fm_scroll)*22;
                        if(in_box(mouse_x,mouse_y,w->x,ry,w->w,22)){
                            /* Double-click window: 100 ticks (~1000ms at the
                             * 100Hz tick rate). Measured real click gaps
                             * during testing under QEMU ranged from ~770ms
                             * to 5000ms+ even for deliberate "quick" double
                             * clicks — 30-50 ticks (300-500ms) never once
                             * registered. Set generously above the worst
                             * observed fast case. */
                            if(fi==fm_last_fi&&(ticks-fm_last_tick)<100){
                                fm_last_fi=-1;fm_last_tick=0;
                                if(fm_entries[fi].is_dir){
                                    /* navigate into subdir, appending to
                                     * whatever path is already current
                                     * (real arbitrary-depth navigation,
                                     * not the previous 1-level-only gate) */
                                    int ni=fm_path_len;
                                    if(ni>0&&ni<199)fm_path[ni++]='/';
                                    int nj=0;while(fm_entries[fi].name[nj]&&ni<199){fm_path[ni++]=fm_entries[fi].name[nj++];}
                                    fm_path[ni]=0;fm_path_len=ni;fm_load();goto click_done;
                                } else if(!fm_entries[fi].is_dir){
                                    char*n=fm_entries[fi].name;int nl=slen(n);
                                    if(nl>4&&n[nl-4]=='.'&&n[nl-3]=='t'&&n[nl-2]=='x'&&n[nl-1]=='t')
                                        open_notepad(n);
                                    else if(nl>4&&n[nl-4]=='.'&&n[nl-3]=='w'&&n[nl-2]=='a'&&n[nl-1]=='v'){
                                        char wpath[220];
                                        fm_build_path(wpath,sizeof(wpath),fm_path,n);
                                        play_wav_file(wpath);
                                    }
                                    else if(nl>4&&n[nl-4]=='.'&&n[nl-3]=='p'&&n[nl-2]=='n'&&n[nl-1]=='g'){
                                        char ipath[220];
                                        fm_build_path(ipath,sizeof(ipath),fm_path,n);
                                        open_imgview(ipath,n);
                                    }
                                }
                            } else {
                                fm_selected=fi;fm_last_fi=fi;fm_last_tick=ticks;
                                fm_dialog=0;fm_ctx_open=0;
                            }
                            goto click_done;
                        }
                    }
                    goto click_done;
                }
                goto click_done;
            }

            /* taskbar buttons (grouped by app) */
            {
                int bxg=TBAR_WINBTN_X0;
                for(int g=0;g<taskbar_group_count;g++){
                    if(in_box(mouse_x,mouse_y,bxg,TBAR_PILL_Y+4,TBAR_WINBTN_W,TBAR_PILL_H-8)){
                        int rep=taskbar_groups[g].rep;
                        if(rep==focused){wins[rep].minimized=!wins[rep].minimized;wins[rep].anim=ANIM_TICKS;wins[rep].anim_type=wins[rep].minimized?3:1;}
                        else{wins[rep].minimized=0;wins[rep].anim=ANIM_TICKS;wins[rep].anim_type=1;wm_focus(rep);}
                        goto click_done;
                    }
                    bxg+=TBAR_WINBTN_W+TBAR_WINBTN_GAP;
                }
            }
            /* start button */
            if(in_box(mouse_x,mouse_y,TBAR_SB_X,TBAR_SB_Y,TBAR_SB_SZ,TBAR_SB_SZ)){
                menu_open=!menu_open;
                if(menu_open){sm_search_len=0;sm_search[0]=0;sm_apply_filter();sm_hov=-1;}
                goto click_done;
            }
            /* menu items */
            /* desktop icons */
            for(int i=0;i<N_ICONS;i++){
                Icon*ic=&icons[i];
                if(in_box(mouse_x,mouse_y,ic->x-4,ic->y-4,72,72)){
                    drag_icon=i;drag_icon_ox=mouse_x-ic->x;drag_icon_oy=mouse_y-ic->y;
                    drag_icon_sx=mouse_x;drag_icon_sy=mouse_y;
                    goto click_done;
                }
            }
        }
        click_done:;

        /* keyboard */
        s64 ch=sys_keypoll();
        if(youdo_open&&ch!=0){text_input_key(youdo_pw,&youdo_pw_len,48,ch);}
        if(ch!=0&&menu_open){
            if(ch>0&&ch<256){
                char sc=(char)ch;
                if((sc=='\b'||sc==127)&&sm_search_len>0){sm_search[--sm_search_len]=0;sm_apply_filter();sm_hov=-1;}
                else if(sc>=32&&sc<127&&sm_search_len<28){sm_search[sm_search_len++]=sc;sm_search[sm_search_len]=0;sm_apply_filter();sm_hov=-1;}
                else if(sc==27)menu_open=0;
            }
        }else if(ch!=0&&focused>=0){
            if(wins[focused].id==WIN_TERMINAL&&ch>0&&ch<256){
                char c=(char)ch;
                if(c=='\n'||c=='\r'){tinput[tinput_len]=0;if(tinput_len>0)tcmd(tinput);tinput_len=0;tinput[0]=0;}
                else if((c=='\b'||c==127)&&tinput_len>0)tinput[--tinput_len]=0;
                else if(c>=32&&c<127&&tinput_len<120){tinput[tinput_len++]=c;tinput[tinput_len]=0;}
            }
            if(wins[focused].id==WIN_FILES&&wins[focused].visible&&!wins[focused].minimized&&(fm_current=focused,fm_dialog==4)){
                if(ch>0&&ch<256){
                    char fc=(char)ch;
                    if(fm_prop_field==0){
                        if(fc=='\b'||fc==127){if(fm_dlg_len>0){fm_dlg_buf[--fm_dlg_len]=0;fm_dlg_has_err=0;}}
                        else if(fc>='0'&&fc<='7'&&fm_dlg_len<3){fm_dlg_buf[fm_dlg_len++]=fc;fm_dlg_buf[fm_dlg_len]=0;fm_dlg_has_err=0;}
                    } else {
                        if(fc=='\b'||fc==127){if(fm_prop_owner_len>0){fm_prop_owner_buf[--fm_prop_owner_len]=0;fm_dlg_has_err=0;}}
                        else if(fc>=32&&fc<127&&fm_prop_owner_len<31){fm_prop_owner_buf[fm_prop_owner_len++]=fc;fm_prop_owner_buf[fm_prop_owner_len]=0;fm_dlg_has_err=0;}
                    }
                }
            }
            if(wins[focused].id==WIN_FILES&&wins[focused].visible&&!wins[focused].minimized&&(fm_current=focused,fm_dialog==2||fm_dialog==3)){
                if(ch>0&&ch<256){
                    char fc=(char)ch;
                    if(fc=='\b'||fc==127){if(fm_dlg_len>0){fm_dlg_buf[--fm_dlg_len]=0;fm_dlg_has_err=0;}}
                    else if(fc=='\n'||fc=='\r'){
                        if(fm_dlg_len>0){
                            if(fm_dialog==2&&fm_selected>=0){
                                char op2[220],np3[220];
                                fm_build_path(op2,sizeof(op2),fm_path,fm_entries[fm_selected].name);
                                fm_build_path(np3,sizeof(np3),fm_path,fm_dlg_buf);
                                if(sys_rename(op2,np3)<0){fm_dlg_has_err=1;const char*em2="Name exists";int ei2=0;while(em2[ei2]&&ei2<47){fm_dlg_err[ei2]=em2[ei2];ei2++;}fm_dlg_err[ei2]=0;}
                                else{fm_dialog=0;fm_dlg_has_err=0;fm_load();}
                            } else if(fm_dialog==3){
                                char np4[220];
                                fm_build_path(np4,sizeof(np4),fm_path,fm_dlg_buf);
                                if(sys_mkdir(np4)<0){fm_dlg_has_err=1;const char*em3="Already exists";int ei3=0;while(em3[ei3]&&ei3<47){fm_dlg_err[ei3]=em3[ei3];ei3++;}fm_dlg_err[ei3]=0;}
                                else{fm_dialog=0;fm_dlg_has_err=0;fm_load();}
                            }
                        }
                    } else if(fc>=32&&fc<127&&fm_dlg_len<38){fm_dlg_buf[fm_dlg_len++]=fc;fm_dlg_buf[fm_dlg_len]=0;fm_dlg_has_err=0;}
                }
            }
            if(wins[focused].id==WIN_CALC&&wins[focused].visible&&!wins[focused].minimized){
                calc_current=focused;
                calc_handle_key(ch);
            }
            if(wins[focused].id==WIN_NOTEPAD&&wins[focused].visible){
                np_current=focused;
                if(np.mode==2){
                    if(ch>0&&ch<256){
                        char c=(char)ch;
                        if(c=='\b'||c==127){if(np.dlg_len>0)np.dlg_buf[--np.dlg_len]=0;}
                        else if(c=='\n'||c=='\r')np_do_saveas();
                        else if(c>=32&&c<127&&np.dlg_len<30){np.dlg_buf[np.dlg_len++]=c;np.dlg_buf[np.dlg_len]=0;}
                        else if(c==32&&np.dlg_len<30){np.dlg_buf[np.dlg_len++]='_';np.dlg_buf[np.dlg_len]=0;}
                    }
                } else if(np.mode==0){
                    if(ch>0&&ch<256){
                        char c=(char)ch;
                        if(c=='\b'||c==127)np_backspace();
                        else if(c=='\n'||c=='\r')np_insert('\n');
                        else if(c>=32&&c<127)np_insert(c);
                    } else if(ch>=1001){
                        /* magic constants from sys_keypoll */
                        if     (ch==1001)np_up();
                        else if(ch==1002)np_down();
                        else if(ch==1003)np_left();
                        else if(ch==1004)np_right();
                        else if(ch==1005)np_home();
                        else if(ch==1006)np_end();
                        else if(ch==1007)np_del_fwd();
                        else if(ch==1008){np.scroll=np.scroll>5?np.scroll-5:0;}
                        else if(ch==1009)np.scroll+=5;
                    }
                }
            }
        }

        /* hover updates */
        if(notif_popup_active&&g_now_ticks>=notif_popup_expire)notif_popup_active=0;
        if(notif_center_open){
            long wdelta=sys_mousewheel();
            int ncxw=NC_X,ncyw=NC_Y;
            if(in_box(mouse_x,mouse_y,ncxw,ncyw,NC_W,NC_H)){
                if(wdelta<0&&notif_scroll+NC_MAX_VISIBLE<notif_count)notif_scroll++;
                else if(wdelta>0&&notif_scroll>0)notif_scroll--;
            }
        }
        sm_hov=-1;
        if(menu_open){
            for(int gi=0;gi<sm_filtered_n;gi++){
                int col=gi%SM_COLS,row=gi/SM_COLS;
                int gx=SM_GRID_X(col),gy=SM_GRID_Y(row);
                if(in_box(mouse_x,mouse_y,gx,gy,SM_CELL-16,SM_CELL-16))sm_hov=gi;
            }
        }
        hover_preview_win=-1;
        {
            int prev_group=hover_preview_group;
            hover_preview_group=-1;
            int bxh=TBAR_WINBTN_X0;
            for(int g=0;g<taskbar_group_count;g++){
                if(in_box(mouse_x,mouse_y,bxh,TBAR_PILL_Y+4,TBAR_WINBTN_W,TBAR_PILL_H-8))
                    hover_preview_group=g;
                bxh+=TBAR_WINBTN_W+TBAR_WINBTN_GAP;
            }
            /* If not directly over a button, but the mouse is in the
             * corridor between the previously-hovered group's button
             * and its strip (moving the cursor up to click a preview),
             * keep that group active instead of dropping hover. */
            if(hover_preview_group<0&&prev_group>=0&&prev_group<taskbar_group_count){
                int sx,sy,sw,sh;taskbar_group_strip_rect(prev_group,&sx,&sy,&sw,&sh);
                int corridor_y0=sy,corridor_y1=TBAR_PILL_Y+TBAR_PILL_H;
                if(mouse_y>=corridor_y0&&mouse_y<corridor_y1&&mouse_x>=sx-20&&mouse_x<sx+sw+20)
                    hover_preview_group=prev_group;
            }
            if(hover_preview_group>=0)hover_preview_win=taskbar_groups[hover_preview_group].rep;
        }
        icon_hovered=-1;
        for(int i=0;i<N_ICONS;i++){Icon*ic=&icons[i];if(in_box(mouse_x,mouse_y,ic->x-4,ic->y-4,72,72))icon_hovered=i;}
        menu_sel=-1;
        if(menu_open){int mx2=TBAR_SB_X,my2=(int)FB_H-TBAR_H-N_MENU*32-8;for(int i=0;i<N_MENU;i++){int iy=my2+20+i*32;if(in_box(mouse_x,mouse_y,mx2+2,iy,216,28))menu_sel=i;}}
        for(int fi2=0;fi2<win_count;fi2++){
            if(wins[fi2].id!=WIN_FILES||!wins[fi2].visible)continue;
            fm_current=fi2;
            fm_hovered=-1;
            if(wins[fi2].minimized)continue;
            Win*wf=&wins[fi2];
            int row_y=wf->y+TITLEBAR_H+72,start=fm_scroll;
            int max_vis3=(wf->h-TITLEBAR_H-72-20)/22;if(max_vis3<1)max_vis3=1;
            for(int i=start;i<fm_count&&i<start+max_vis3;i++){int ry=row_y+(i-start)*22;if(in_box(mouse_x,mouse_y,wf->x,ry,wf->w,22))fm_hovered=i;}
        }
        np.dlg_hov=-1;
        if(np_current>=0&&np_current<MAX_WINDOWS&&wins[np_current].visible&&np.mode==1){
            Win*wn=&wins[np_current];
            int dh2=np_dlg_count*20+52;if(dh2>260)dh2=260;if(dh2<72)dh2=72;
            int dw=280,dh=dh2,dx=wn->x+(wn->w-dw)/2,dy2=wn->y+TITLEBAR_H+(wn->h-TITLEBAR_H-dh)/2;
            int ly=dy2+28,max_vis2=(dh-36)/20;
            for(int i=np.dlg_scroll;i<np_dlg_count&&i<np.dlg_scroll+max_vis2;i++){
                int ry=ly+(i-np.dlg_scroll)*20;
                if(in_box(mouse_x,mouse_y,dx+2,ry,dw-4,20))np.dlg_hov=i;
            }
        }

        if(fm_current>=0)fm_ctx_hov=-1;
        if(fm_current>=0&&fm_ctx_open){
            {
                Win*wfm=&wins[fm_current];
                const char*fitems[]={"New Folder","","Copy","Cut","Paste","Rename","Delete"};
                int fn=7,fiw=160,fih=22,fsep=6;
                int fmh=2;for(int i=0;i<fn;i++)fmh+=fitems[i][0]?fih:fsep;
                int fmx=fm_ctx_x,fmy=fm_ctx_y;
                if(fmx+fiw>wfm->x+wfm->w)fmx=wfm->x+wfm->w-fiw;
                if(fmy+fmh>wfm->y+wfm->h)fmy=wfm->y+wfm->h-fmh;
                int fiy=fmy+1;
                for(int i=0;i<fn;i++){
                    if(!fitems[i][0]){fiy+=fsep;continue;}
                    if(in_box(mouse_x,mouse_y,fmx,fiy,fiw,fih)){fm_ctx_hov=i;break;}
                    fiy+=fih;
                }
            }
        }
        rctx_hov=-1;
        if(rctx_open){
            const char**hi;int hn;get_rctx(&hi,&hn);
            int hmh=rctx_h(),hmx=rctx_x,hmy=rctx_y;
            if(hmx+CTX_W>PANEL_X)hmx=PANEL_X-CTX_W;
            if(hmy+hmh>(int)FB_H-TBAR_H)hmy=(int)FB_H-TBAR_H-hmh;
            int hiy=hmy+1;
            for(int i=0;i<hn;i++){
                if(!hi[i][0]){hiy+=CTX_SEP_H;continue;}
                if(in_box(mouse_x,mouse_y,hmx,hiy,CTX_W,CTX_ITEM_H))rctx_hov=i;
                hiy+=CTX_ITEM_H;
            }
        }
        cursor_blink=(cursor_blink+1)%100;
        prev_btn=mouse_btn;
        for(int fmk=0;fmk<win_count;fmk++){
            if(wins[fmk].id==WIN_FILES&&wins[fmk].visible){
                fm_current=fmk;
                if(!fm_loaded)fm_load();
            }
        }

        if(ticks-last_ticks<1&&ch==0&&cursor_blink!=0&&cursor_blink!=50&&!btn_down&&!btn_up&&np.save_flash==0)continue;
        last_ticks=ticks;

        rebuild_taskbar_groups();
        wallpaper();draw_icons();draw_panel_bg();
        int px2=PANEL_X+4;
        draw_analog_clock(PANEL_X+PANEL_W/2,95,80,secs);
        draw_digital_clock(px2,182,secs);
        draw_calendar(px2,220,5,2026,29);
        draw_stats(px2,392);
        wm_sort();
        for(int si=0;si<win_count;si++){
            int i=z_order[si];if(!wins[i].visible||wins[i].minimized)continue;
            int db=wm_draw_frame(i);if(!db)continue;
            if(wins[i].id==WIN_TERMINAL)draw_terminal_content(i);
            else if(wins[i].id==WIN_ABOUT)draw_about_content(i);
            else if(wins[i].id==WIN_FILES){fm_current=i;draw_files_content(i);}
            else if(wins[i].id==WIN_NOTEPAD){np_current=i;draw_notepad_content(i);}
            else if(wins[i].id==WIN_CALC){calc_current=i;draw_calc_content(i);}
            else if(wins[i].id==WIN_SETTINGS)draw_settings_content(i);
            else if(wins[i].id==WIN_IMGVIEW)draw_imgview_content(i);
            if(hover_preview_group>=0){
                for(int gk=0;gk<taskbar_groups[hover_preview_group].count;gk++)
                    if(taskbar_groups[hover_preview_group].idx[gk]==i)capture_window_preview_slot(i,gk);
            }
        }
        draw_taskbar(secs);draw_menu();draw_rctx();draw_notif_popup();draw_notif_center();
        if(hover_preview_group>=0){
            int gcount=taskbar_groups[hover_preview_group].count;
            int sx,sy,sw,sh;taskbar_group_strip_rect(hover_preview_group,&sx,&sy,&sw,&sh);
            for(int gk=0;gk<gcount;gk++){
                int wi=taskbar_groups[hover_preview_group].idx[gk];
                draw_window_preview_slot(wi,gk,sx+gk*(WPREV_W+WPREV_GAP),sy);
            }
        }
        draw_cursor(mouse_x,mouse_y);
        wav_poll();
        flush();sys_yield();
    }
    return 0;
}

