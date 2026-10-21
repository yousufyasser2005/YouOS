#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
uint64_t host_sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e){(void)n;(void)a;(void)b;(void)c;(void)d;(void)e;return 0;}
#define main desktop_real_main
#include DESKTOP_SRC
#undef main

static int visible_count(void){int n=0;for(int i=0;i<win_count;i++)if(wins[i].visible)n++;return n;}
#ifdef OLD_CLOSE
static void close_win(int i){wins[i].visible=0;}          /* what the old X button did: hide only */
#else
static void close_win(int i){wm_close(i);}
#endif
static int open_kind(int kind){
    int before=visible_count();
    switch(kind){
        case 0: open_files(); break;
        case 1: open_calc(); break;
        case 2: open_notepad(0); break;
        case 3: open_new_pterm("hello"); break;
        case 4: if(media_win>=0&&media_win<win_count&&wins[media_win].visible){} else media_win=wm_new(WIN_MEDIAPLAYER,180,90,400,300,"Media Player",PURPLE); break;
    }
    return visible_count()>before;
}
int main(void){
    FB_W=1280;FB_H=720; int fails=0;
    /* 1. open and immediately close, many times, mixing window types */
    for(int round=0;round<100;round++){
        int kind=round%5;
        if(!open_kind(kind)){printf("FAIL: round %d (kind %d): window did NOT open (win_count=%d)\n",round,kind,win_count);fails++;break;}
        int w=focused; if(w<0||w>=win_count){printf("FAIL: no focused window after open\n");fails++;break;}
        close_win(w);
        if(win_count>MAX_WINDOWS){printf("FAIL: win_count %d > MAX\n",win_count);fails++;break;}
    }
    if(!fails)printf("ok: 100 open/close cycles across 5 window types, win_count stayed at %d\n",win_count);
#ifndef OLD_CLOSE
    /* 2. fill all 12 slots, the 13th must fail cleanly, closing one makes room again */
    for(int i=0;i<win_count;i++)if(wins[i].visible)close_win(i);
    int opened=0;for(int i=0;i<20;i++)if(open_kind(i%4))opened++;
    if(opened!=MAX_WINDOWS){printf("FAIL: expected %d windows to fit, got %d\n",MAX_WINDOWS,opened);fails++;}
    else printf("ok: exactly %d windows fit at once; extras refused\n",MAX_WINDOWS);
    close_win(focused);
    if(!open_kind(0)){printf("FAIL: could not open after closing one\n");fails++;}else printf("ok: opening works again after closing one\n");
    /* 3. every visible window has its own stacking order */
    int zs[MAX_WINDOWS],zn=0,dup=0;
    for(int i=0;i<win_count;i++)if(wins[i].visible){for(int k=0;k<zn;k++)if(zs[k]==wins[i].z)dup=1;zs[zn++]=wins[i].z;}
    if(dup){printf("FAIL: two visible windows share a z value\n");fails++;}else printf("ok: all %d visible windows have distinct z-order\n",zn);
    /* 4. a reused slot starts clean */
    for(int i=0;i<win_count;i++)if(wins[i].visible)close_win(i);
    open_files(); int a=focused; fm_states[a].dialog=3; fm_states[a].ctx_open=1; close_win(a);
    open_files(); int b=focused;
    if(b!=a||fm_states[b].dialog!=0||fm_states[b].ctx_open!=0){printf("FAIL: reused slot kept stale state (dialog=%d ctx_open=%d)\n",fm_states[b].dialog,fm_states[b].ctx_open);fails++;}
    printf("ok: reused slot %d starts with clean state\n",b);
    /* 5. the transfer window keeps its slot (it is reopened in place) */
    int t=wm_new(WIN_TRANSFER,10,10,420,190,"Copying",GREEN); wins[t].visible=0;
    if(find_win(WIN_TRANSFER)!=t){printf("FAIL: transfer window not findable after hide\n");fails++;}else printf("ok: transfer window slot kept for reopening\n");
#endif
    printf(fails?"RESULT: FAILED\n":"RESULT: PASSED\n"); return fails?1:0;
}
