#include <kernel/syslog.h>
#include <kernel/fat16.h>
#include <stdint.h>

#define SYSLOG_SIZE 8192
#define SYSLOG_PATH "/disk/syslog.log"

static char slog_buf[SYSLOG_SIZE];
static int  slog_len=0;
static int  slog_fat_ready=0;

extern uint64_t scheduler_get_ticks(void);

static void sl_ap(const char*s){while(*s&&slog_len<SYSLOG_SIZE-1)slog_buf[slog_len++]=*s++;slog_buf[slog_len]=0;}
static void sl_uint(uint64_t v){
    char t[21];int i=20;t[20]=0;
    if(!v){sl_ap("0");return;}
    while(v){t[--i]='0'+v%10;v/=10;}sl_ap(t+i);
}
static void slog_flush(void){
    if(!slog_fat_ready||slog_len==0)return;
    int fd=fat16_create(SYSLOG_PATH);
    if(fd<0)return;
    fat16_write(fd,slog_buf,(uint32_t)slog_len);
    fat16_close(fd);
}

void syslog_init(void){slog_len=0;slog_buf[0]=0;slog_fat_ready=0;}

void syslog_ready(void){
    slog_fat_ready=1;
    /* read existing log */
    int fd=fat16_open(SYSLOG_PATH);
    if(fd>=0){
        int n=fat16_read(fd,slog_buf,SYSLOG_SIZE-1);
        fat16_close(fd);
        slog_len=n>0?n:0;
        slog_buf[slog_len]=0;
    }
    slog_flush();
}

void syslog_write(const char*tag,const char*msg){
    sl_ap("[");sl_ap(tag);sl_ap("][");
    sl_uint(scheduler_get_ticks());
    sl_ap("] ");sl_ap(msg);sl_ap("\n");
    slog_flush();
}

int syslog_read(void*buf,uint32_t size){
    if(slog_fat_ready){
        int fd=fat16_open(SYSLOG_PATH);
        if(fd>=0){
            int n=fat16_read(fd,slog_buf,SYSLOG_SIZE-1);
            fat16_close(fd);
            if(n>0){slog_len=n;slog_buf[n]=0;}
        }
    }
    uint32_t copy=((uint32_t)slog_len<size-1)?(uint32_t)slog_len:size-1;
    char*out=(char*)buf;
    for(uint32_t i=0;i<copy;i++)out[i]=slog_buf[i];
    out[copy]=0;
    return(int)copy;
}
