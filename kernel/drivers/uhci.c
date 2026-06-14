/* Minimal UHCI + USB HID boot-class driver
 * Keyboard (boot protocol) + Mouse (3-button relative)
 * Polling via timer IRQ every 10 ticks (~100ms)
 */
#include <stdint.h>
#include <kernel/uhci.h>
#include <kernel/keyboard.h>
#include <kernel/mouse.h>

/* ── Port I/O ────────────────────────────────────────────────── */
static inline void     outb(uint16_t p,uint8_t  v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static inline void     outw(uint16_t p,uint16_t v){__asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p));}
static inline void     outl(uint16_t p,uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static inline uint8_t  inb (uint16_t p){uint8_t  v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static inline uint16_t inw (uint16_t p){uint16_t v;__asm__ volatile("inw %1,%0":"=a"(v):"Nd"(p));return v;}
static void udelay(int n){for(volatile int i=0;i<n*50;i++);}

/* ── PCI config space ────────────────────────────────────────── */
static uint32_t pci_r32(uint8_t b,uint8_t d,uint8_t f,uint8_t o){
    uint32_t a=0x80000000|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(o&0xFC);
    __asm__ volatile("outl %0, %1"::"a"(a),"Nd"((uint16_t)0xCF8));
    uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"((uint16_t)0xCFC));return v;
}
static void pci_w32(uint8_t b,uint8_t d,uint8_t f,uint8_t o,uint32_t v){
    uint32_t a=0x80000000|((uint32_t)b<<16)|((uint32_t)d<<11)|((uint32_t)f<<8)|(o&0xFC);
    __asm__ volatile("outl %0, %1"::"a"(a),"Nd"((uint16_t)0xCF8));
    __asm__ volatile("outl %0, %1"::"a"(v),"Nd"((uint16_t)0xCFC));
}

/* ── UHCI register offsets ───────────────────────────────────── */
#define UCMD    0x00u
#define USTS    0x02u
#define UINTR   0x04u
#define UFRNUM  0x06u
#define UFLBASE 0x08u
#define USOF    0x0Cu
#define UP1     0x10u
#define UP2     0x12u
#define UCMD_RS    (1u<<0)
#define UCMD_HCRST (1u<<1)
#define UCMD_GRST  (1u<<2)
#define UCMD_MAXP  (1u<<7)
#define UP_CCS (1u<<0)
#define UP_CSC (1u<<1)
#define UP_PED (1u<<2)
#define UP_PEDC (1u<<3)
#define UP_RST (1u<<9)

/* ── UHCI structures ─────────────────────────────────────────── */
typedef struct __attribute__((packed,aligned(16))){
    volatile uint32_t lnk,ctrl,tok,buf; uint32_t sw[4];
} TD;
typedef struct __attribute__((packed,aligned(16))){
    volatile uint32_t head,elem;
} QH;

#define TD_TERM  1u
#define QH_SEL   2u
#define DEPTH    4u
#define TD_ACT  (1u<<23)
#define TD_STLL (1u<<22)
#define TD_DBER (1u<<21)
#define TD_BABB (1u<<20)
#define TD_ERR3 (3u<<27)
#define PID_SETUP 0x2Du
#define PID_IN    0x69u
#define PID_OUT   0xE1u

static inline uint32_t mktok(uint8_t pid,uint8_t dev,uint8_t ep,uint8_t tog,uint16_t ml){
    uint32_t mf=(ml==0)?0x7FFu:(uint32_t)((ml-1)&0x7FFu);
    return (mf<<21)|((uint32_t)tog<<19)|((uint32_t)(ep&0xF)<<15)|((uint32_t)(dev&0x7F)<<8)|pid;
}

/* ── USB setup packet ────────────────────────────────────────── */
typedef struct __attribute__((packed,aligned(4))){
    uint8_t bmRT,bReq; uint16_t wVal,wIdx,wLen;
} SETUP;

/* ── DMA memory ──────────────────────────────────────────────── */
static uint32_t fl[1024] __attribute__((aligned(4096)));
static QH ctrl_qh        __attribute__((aligned(16)));
static TD ctl[8]         __attribute__((aligned(16)));
static SETUP pkt         __attribute__((aligned(4)));
static uint8_t dbuf[256] __attribute__((aligned(4)));

/* ── HID device state ────────────────────────────────────────── */
typedef struct{int on,kbd;uint8_t addr,ep,mpkt,tog,prev[8];}HDEV;
static HDEV hd[2];
static uint8_t caps=0;
static uint16_t base=0;

/* ── HID ASCII maps ──────────────────────────────────────────── */
static const uint8_t hmap[256]={
    [0x04]='a',[0x05]='b',[0x06]='c',[0x07]='d',[0x08]='e',[0x09]='f',
    [0x0A]='g',[0x0B]='h',[0x0C]='i',[0x0D]='j',[0x0E]='k',[0x0F]='l',
    [0x10]='m',[0x11]='n',[0x12]='o',[0x13]='p',[0x14]='q',[0x15]='r',
    [0x16]='s',[0x17]='t',[0x18]='u',[0x19]='v',[0x1A]='w',[0x1B]='x',
    [0x1C]='y',[0x1D]='z',
    [0x1E]='1',[0x1F]='2',[0x20]='3',[0x21]='4',[0x22]='5',
    [0x23]='6',[0x24]='7',[0x25]='8',[0x26]='9',[0x27]='0',
    [0x28]='\n',[0x29]=27,[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='-',[0x2E]='=',[0x2F]='[',[0x30]=']',[0x31]='\\',
    [0x33]=';',[0x34]='\'',[0x35]='`',[0x36]=',',[0x37]='.',[0x38]='/',
};
static const uint8_t hmsh[256]={
    [0x04]='A',[0x05]='B',[0x06]='C',[0x07]='D',[0x08]='E',[0x09]='F',
    [0x0A]='G',[0x0B]='H',[0x0C]='I',[0x0D]='J',[0x0E]='K',[0x0F]='L',
    [0x10]='M',[0x11]='N',[0x12]='O',[0x13]='P',[0x14]='Q',[0x15]='R',
    [0x16]='S',[0x17]='T',[0x18]='U',[0x19]='V',[0x1A]='W',[0x1B]='X',
    [0x1C]='Y',[0x1D]='Z',
    [0x1E]='!',[0x1F]='@',[0x20]='#',[0x21]='$',[0x22]='%',
    [0x23]='^',[0x24]='&',[0x25]='*',[0x26]='(',[0x27]=')',
    [0x28]='\n',[0x29]=27,[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='_',[0x2E]='+',[0x2F]='{',[0x30]='}',[0x31]='|',
    [0x33]=':',[0x34]='"',[0x35]='~',[0x36]='<',[0x37]='>',[0x38]='?',
};

/* ── Find UHCI via PCI class scan ────────────────────────────── */
static uint16_t find_uhci(void){
    for(int b=0;b<8;b++)for(int d=0;d<32;d++)for(int f=0;f<8;f++){
        if((pci_r32(b,d,f,0)&0xFFFF)==0xFFFF)continue;
        uint32_t cl=pci_r32(b,d,f,8);
        if(((cl>>24)&0xFF)==0x0C&&((cl>>16)&0xFF)==0x03&&((cl>>8)&0xFF)==0x00){
            pci_w32(b,d,f,4,pci_r32(b,d,f,4)|0x05); /* I/O + bus master */
            return(uint16_t)(pci_r32(b,d,f,0x20)&0xFFFC);
        }
    }
    return 0;
}

/* ── Synchronous control transfer ────────────────────────────── */
static int ctrl_xfer(uint8_t dev,SETUP*s,void*data,int len,int dir){
    int ti=0;
    /* SETUP TD */
    ctl[ti].lnk=(uint32_t)(uintptr_t)&ctl[ti+1]|DEPTH;
    ctl[ti].ctrl=TD_ACT|TD_ERR3;
    ctl[ti].tok=mktok(PID_SETUP,dev,0,0,8);
    ctl[ti].buf=(uint32_t)(uintptr_t)s; ti++;
    /* DATA TDs */
    if(len>0&&data){
        uint8_t*p=(uint8_t*)data,tog=1; int rem=len;
        while(rem>0){
            int ch=rem>8?8:rem;
            ctl[ti].lnk=(uint32_t)(uintptr_t)&ctl[ti+1]|DEPTH;
            ctl[ti].ctrl=TD_ACT|TD_ERR3;
            ctl[ti].tok=mktok(dir?PID_IN:PID_OUT,dev,0,tog,(uint16_t)ch);
            ctl[ti].buf=(uint32_t)(uintptr_t)p;
            p+=ch;rem-=ch;tog^=1;ti++;
        }
    }
    /* STATUS TD */
    ctl[ti].lnk=TD_TERM;
    ctl[ti].ctrl=TD_ACT|TD_ERR3;
    ctl[ti].tok=mktok((dir||len==0)?PID_OUT:PID_IN,dev,0,1,0);
    ctl[ti].buf=0;
    int last=ti;
    ctrl_qh.elem=(uint32_t)(uintptr_t)&ctl[0];
    int to=500000;
    while((ctl[last].ctrl&TD_ACT)&&--to)for(volatile int d=0;d<5;d++);
    ctrl_qh.elem=TD_TERM;
    if(!to)return -1;
    if(ctl[last].ctrl&(TD_STLL|TD_DBER|TD_BABB))return -1;
    return 0;
}

/* ── Single IN transfer (polling) ────────────────────────────── */
static int in_xfer(HDEV*h,uint8_t*buf){
    ctl[0].lnk=TD_TERM;
    ctl[0].ctrl=TD_ACT|TD_ERR3;
    ctl[0].tok=mktok(PID_IN,h->addr,h->ep,h->tog,h->mpkt);
    ctl[0].buf=(uint32_t)(uintptr_t)buf;
    ctrl_qh.elem=(uint32_t)(uintptr_t)&ctl[0];
    int done=0;
    for(int i=0;i<80000;i++){
        if(!(ctl[0].ctrl&TD_ACT)){done=1;break;}
        for(volatile int d=0;d<5;d++);
    }
    if(!done)ctl[0].ctrl&=~TD_ACT;
    ctrl_qh.elem=TD_TERM;
    if(!done)return 0;
    if(ctl[0].ctrl&(TD_STLL|TD_DBER|TD_BABB))return -1;
    uint32_t af=ctl[0].ctrl&0x7FFu;
    h->tog^=1;
    return(af==0x7FFu)?0:(int)(af+1);
}

/* ── USB request helpers ─────────────────────────────────────── */
static int get_desc(uint8_t d,uint8_t t,uint8_t i,int l){
    pkt.bmRT=0x80;pkt.bReq=6;pkt.wVal=(uint16_t)(((uint16_t)t<<8)|i);
    pkt.wIdx=0;pkt.wLen=(uint16_t)l;
    return ctrl_xfer(d,&pkt,dbuf,l,1);
}
static void set_addr(uint8_t na){
    pkt.bmRT=0;pkt.bReq=5;pkt.wVal=na;pkt.wIdx=0;pkt.wLen=0;
    ctrl_xfer(0,&pkt,0,0,0);
}
static void set_cfg(uint8_t d,uint8_t c){
    pkt.bmRT=0;pkt.bReq=9;pkt.wVal=c;pkt.wIdx=0;pkt.wLen=0;
    ctrl_xfer(d,&pkt,0,0,0);
}
static void set_proto(uint8_t d,uint8_t iface){
    pkt.bmRT=0x21;pkt.bReq=0x0B;pkt.wVal=0;pkt.wIdx=iface;pkt.wLen=0;
    ctrl_xfer(d,&pkt,0,0,0);
}
static void set_idle(uint8_t d,uint8_t iface){
    pkt.bmRT=0x21;pkt.bReq=0x0A;pkt.wVal=0;pkt.wIdx=iface;pkt.wLen=0;
    ctrl_xfer(d,&pkt,0,0,0);
}

/* ── Port reset + enumeration ────────────────────────────────── */
static void reset_port(uint16_t pr){
    outw(base+pr,UP_RST); udelay(60000);
    outw(base+pr,inw(base+pr)&~UP_RST); udelay(3000);
    outw(base+pr,UP_CSC|UP_PEDC|UP_PED); udelay(1000);
}
static void enum_port(uint16_t pr,uint8_t na,int slot){
    if(!(inw(base+pr)&UP_CCS))return;
    reset_port(pr);
    if(!(inw(base+pr)&UP_PED))return;
    /* Get 8 bytes of device descriptor at addr 0 */
    if(get_desc(0,1,0,8)<0)return;
    udelay(2000); set_addr(na); udelay(2000);
    /* Get full device descriptor */
    if(get_desc(na,1,0,18)<0)return;
    /* Get config descriptor */
    if(get_desc(na,2,0,255)<0)return;
    /* Parse for HID interface + interrupt IN endpoint */
    uint16_t tot=(uint16_t)(dbuf[2]|(uint16_t)dbuf[3]<<8);
    if(tot>255)tot=255;
    uint8_t iface=0,ep=0,mpkt=8,kbd=0; int found=0;
    for(int i=0;i<(int)tot;){
        uint8_t bl=dbuf[i],bt=dbuf[i+1]; if(!bl)break;
        if(bt==4&&dbuf[i+5]==3&&dbuf[i+6]==1){
            iface=dbuf[i+2]; kbd=(dbuf[i+7]==1)?1:0; found=1;
        }
        if(bt==5&&found&&(dbuf[i+2]&0x80)){
            ep=dbuf[i+2]&0x0F; mpkt=dbuf[i+4]; if(mpkt>8)mpkt=8; break;
        }
        i+=bl;
    }
    if(!found||!ep)return;
    udelay(2000); set_cfg(na,1);
    udelay(2000); set_proto(na,iface);
    udelay(1000); set_idle(na,iface);
    udelay(1000);
    hd[slot].on=1; hd[slot].kbd=kbd;
    hd[slot].addr=na; hd[slot].ep=ep;
    hd[slot].mpkt=mpkt; hd[slot].tog=0;
    for(int i=0;i<8;i++)hd[slot].prev[i]=0;
}

/* ── HID report processing ───────────────────────────────────── */
static void proc_kbd(HDEV*h,uint8_t*r,int n){
    if(n<3)return;
    uint8_t mod=r[0];
    uint8_t sh=(mod&0x22)?1:0,ct=(mod&0x11)?1:0,al=(mod&0x44)?1:0;
    for(int i=2;i<8&&i<n;i++){
        uint8_t k=r[i]; if(!k)continue;
        int seen=0; for(int j=2;j<8;j++)if(h->prev[j]==k){seen=1;break;}
        if(seen)continue;
        if(k==0x39){caps^=1;continue;}
        key_event_t e={0};
        e.pressed=1;e.shift=sh;e.ctrl=ct;e.alt=al;e.capslock=caps;
        switch(k){
            case 0x4F:e.scancode=0x4D;break; /* Right */
            case 0x50:e.scancode=0x4B;break; /* Left  */
            case 0x51:e.scancode=0x50;break; /* Down  */
            case 0x52:e.scancode=0x48;break; /* Up    */
            case 0x4C:e.scancode=0x53;break; /* Del   */
            case 0x4A:e.scancode=0x47;break; /* Home  */
            case 0x4D:e.scancode=0x4F;break; /* End   */
            case 0x4B:e.scancode=0x49;break; /* PgUp  */
            case 0x4E:e.scancode=0x51;break; /* PgDn  */
            default:{uint8_t up=sh^caps;e.ascii=(char)(up?hmsh[k]:hmap[k]);}
        }
        keyboard_inject(&e);
    }
    for(int i=0;i<8;i++)h->prev[i]=(i<n)?r[i]:0;
}
static void proc_mouse(uint8_t*r,int n){
    if(n<3)return;
    mouse_update_usb_delta((int)(int8_t)r[1],(int)(int8_t)r[2],r[0]&0x07);
}

/* ── Public API ──────────────────────────────────────────────── */
void uhci_init(void){
    base=find_uhci(); if(!base)return;
    /* Reset */
    outw(base+UCMD,UCMD_GRST); udelay(15000);
    outw(base+UCMD,0);          udelay(5000);
    outw(base+UCMD,UCMD_HCRST); udelay(5000);
    outw(base+UCMD,0);
    outw(base+USTS,0x3F);
    outw(base+UINTR,0);
    outb(base+USOF,0x40);
    /* Frame list: all entries point to ctrl_qh */
    ctrl_qh.head=TD_TERM; ctrl_qh.elem=TD_TERM;
    uint32_t qa=(uint32_t)(uintptr_t)&ctrl_qh|QH_SEL;
    for(int i=0;i<1024;i++)fl[i]=qa;
    outl(base+UFLBASE,(uint32_t)(uintptr_t)fl);
    outw(base+UFRNUM,0);
    outw(base+UCMD,UCMD_RS|UCMD_MAXP);
    udelay(5000);
    /* Enumerate both ports */
    enum_port(UP1,1,0); udelay(5000);
    enum_port(UP2,2,1);
}

void uhci_poll(void){
    if(!base)return;
    uint8_t buf[8]={0};
    for(int s=0;s<2;s++){
        if(!hd[s].on)continue;
        int n=in_xfer(&hd[s],buf);
        if(n>0){
            if(hd[s].kbd)proc_kbd(&hd[s],buf,n);
            else          proc_mouse(buf,n);
        }
    }
}
