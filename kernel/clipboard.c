#include <kernel/clipboard.h>
#include <kernel/heap.h>

static char*    clip_data = 0;
static uint32_t clip_len  = 0;
static uint32_t clip_type = 0;
static uint32_t clip_seq  = 0;

static void clip_copy(char* d, const char* s, uint32_t n) { for (uint32_t i = 0; i < n; i++) d[i] = s[i]; }

int64_t clip_set(const void* data, uint32_t len, uint32_t type) {
    if (len > CLIP_MAX) return -1;
    if (len == 0 || !data) {                       /* clear */
        if (clip_data) { kfree(clip_data); clip_data = 0; }
        clip_len = 0; clip_type = 0;
        return (int64_t)++clip_seq;
    }
    char* n = (char*)kmalloc(len);                 /* allocate and fill the new buffer FIRST: a failure keeps the old contents */
    if (!n) return -1;
    clip_copy(n, (const char*)data, len);
    if (clip_data) kfree(clip_data);
    clip_data = n; clip_len = len; clip_type = type;
    return (int64_t)++clip_seq;
}

int64_t clip_get(void* buf, uint32_t max) {
    uint32_t n = clip_len < max ? clip_len : max;
    if (n && buf) clip_copy((char*)buf, clip_data, n);
    return (int64_t)clip_len;
}

void clip_info(uint32_t* len, uint32_t* type, uint32_t* seq) {
    if (len)  *len  = clip_len;
    if (type) *type = clip_type;
    if (seq)  *seq  = clip_seq;
}
