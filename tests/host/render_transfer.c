#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static uint64_t fake_ticks = 0;
uint64_t host_sc(uint64_t n,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e){
    (void)a;(void)b;(void)c;(void)d;(void)e;
    if (n == 15) return fake_ticks;      /* SYS_TICKS */
    return 0;
}
#define main desktop_real_main
#include "user/src/desktop.c"
#undef main

static void dump(const char* path, int x0, int y0, int w, int h) {
    FILE* f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) {
        uint32_t c = buf[y * (int)FB_W + x]; fputc((c >> 16) & 255, f); fputc((c >> 8) & 255, f); fputc(c & 255, f);
    }
    fclose(f);
}
static void scene(const char* name, int state, unsigned long long done, unsigned long long total,
                  unsigned fd, unsigned ft, unsigned long long elapsed, const char* err, const char* nm) {
    FB_W = 1280; FB_H = 720;
    for (int i = 0; i < 1280 * 720; i++) buf[i] = 0x1B2330;
    win_count = 0; focused = -1;
    int i = wm_new(WIN_TRANSFER, 200, 120, 420, 190, "Copying", GREEN);
    wins[i].z = 0; focused = i; wins[i].anim = 0;
    memset(&ft_job, 0, sizeof ft_job);
    ft_job.state = state; ft_job.done_bytes = done; ft_job.total_bytes = total;
    ft_job.done_files = fd; ft_job.total_files = ft; ft_job.fd = -1;
    ft_tsc_per_ms = 0;                     /* use the tick fallback clock in this harness */
    fake_ticks = elapsed; ft_job.t_start = 0; ft_job.err = err;
    ft_job.chunk_len = 49152; ft_job.last_chunk_ms = 31;
    int k = 0; while (nm[k] && k < 39) { xfer_name[k] = nm[k]; k++; } xfer_name[k] = 0;
    xfer_win = i;
    wm_draw_frame(i); draw_transfer_content(i);
    char p[64]; snprintf(p, sizeof p, "/tmp/rh/%s.ppm", name);
    dump(p, 180, 100, 460, 230);
}
static void scene_del(const char* name, int state, unsigned done, unsigned total, int known, const char* nm, const char* err) {
    FB_W = 1280; FB_H = 720;
    for (int i = 0; i < 1280 * 720; i++) buf[i] = 0x1B2330;
    win_count = 0; focused = -1;
    int i = wm_new(WIN_TRANSFER, 200, 120, 420, 190, "Deleting", GREEN);
    wins[i].z = 0; focused = i; wins[i].anim = 0;
    memset(&ft_job, 0, sizeof ft_job);
    ft_job.kind = FTK_DELETE; ft_job.state = state; ft_job.done_items = done; ft_job.total_items = total;
    ft_job.total_known = known; ft_job.fd = -1; ft_job.err = err;
    int k = 0; while (nm[k] && k < 39) { xfer_name[k] = nm[k]; k++; } xfer_name[k] = 0;
    xfer_win = i;
    wm_draw_frame(i); draw_transfer_content(i);
    char p[64]; snprintf(p, sizeof p, "/tmp/rh/%s.ppm", name);
    dump(p, 180, 100, 460, 230);
}
int main(void) {
    scene_del("deleting", FTJ_COPY, 70, 113, 1, "old_project_files", "");
    scene_del("deleting_big", FTJ_COPY, 137, 1, 0, "huge_music_library", "");
    scene_del("delete_failed", FTJ_ERROR, 12, 40, 1, "protected_folder", "cannot delete (no access?)");
    scene("copying", FTJ_COPY, 12400000, 20000000, 1, 4, 700, "", "big_video_file_with_a_long_name.mp4");
    scene("scanning", FTJ_SCAN, 0, 0, 0, 0, 20, "", "my_music_folder");
    scene("error", FTJ_ERROR, 7000000, 9000000, 0, 1, 900, "write failed (disk full or no access)", "nine.bin");
    scene("start", FTJ_COPY, 100000, 20000000, 0, 1, 10, "", "x.bin");
    printf("rendered\n");
    return 0;
}
