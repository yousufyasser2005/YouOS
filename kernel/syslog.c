#include <kernel/syslog.h>
#include <kernel/ycfs.h>
#include <stdint.h>

#define SYSLOG_SIZE 8192
#define SYSLOG_PATH "/ycfs/syslog.log"

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
    ycfs_savefile(SYSLOG_PATH, slog_buf, (uint32_t)slog_len);
}

void syslog_init(void){slog_len=0;slog_buf[0]=0;slog_fat_ready=0;}

void syslog_ready(void){
    slog_fat_ready=1;
    /* read existing log */
    int64_t n=ycfs_read_file(SYSLOG_PATH, slog_buf, SYSLOG_SIZE-1);
    slog_len=(n>0)?(int)n:0;
    slog_buf[slog_len]=0;
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
        int64_t n=ycfs_read_file(SYSLOG_PATH, slog_buf, SYSLOG_SIZE-1);
        if(n>0){slog_len=(int)n;slog_buf[n]=0;}
    }
    uint32_t copy=((uint32_t)slog_len<size-1)?(uint32_t)slog_len:size-1;
    char*out=(char*)buf;
    for(uint32_t i=0;i<copy;i++)out[i]=slog_buf[i];
    out[copy]=0;
    return(int)copy;
}
