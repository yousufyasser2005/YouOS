#include <kernel/fat16.h>
#include <kernel/ata.h>
#include <kernel/vga.h>
#include <kernel/ata.h>

/* ── BPB values populated by fat16_init ─────────────────────── */
static fat16_bpb_t bpb;
static uint32_t fat_start_lba;
static uint32_t root_dir_lba;
static uint32_t data_start_lba;
static uint32_t sectors_per_cluster;
static uint32_t root_dir_sectors;
static uint8_t  initialized = 0;

/* ── Sector I/O helpers ──────────────────────────────────────── */

static int read_sector(uint32_t lba, void* buf) {
    return ata_read_sectors(lba, 1, buf);
}

static int write_sector(uint32_t lba, const void* buf) {
    return ata_write_sectors(lba, 1, buf);
}

/* ── FAT helpers ─────────────────────────────────────────────── */
static uint16_t fat_get(uint16_t cluster) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fat_start_lba + fat_offset / 512;
    uint32_t offset_in_sector = fat_offset % 512;
    uint8_t buf[512];
    read_sector(fat_sector, buf);
    return *(uint16_t*)(buf + offset_in_sector);
}

static void fat_set(uint16_t cluster, uint16_t value) {
    uint32_t fat_offset = cluster * 2;
    uint32_t fat_sector = fat_start_lba + fat_offset / 512;
    uint32_t offset_in_sector = fat_offset % 512;
    uint8_t buf[512];
    read_sector(fat_sector, buf);
    *(uint16_t*)(buf + offset_in_sector) = value;
    write_sector(fat_sector, buf);
    /* Write second FAT copy too */
    uint32_t fat2 = fat_sector + bpb.sectors_per_fat;
    write_sector(fat2, buf);
}

static uint16_t fat_alloc_cluster(void) {
    /* Scan FAT for a free cluster (value == 0) */
    uint32_t total_fat_sectors = bpb.sectors_per_fat;
    for (uint32_t s = 0; s < total_fat_sectors; s++) {
        uint8_t buf[512];
        read_sector(fat_start_lba + s, buf);
        uint16_t* entries = (uint16_t*)buf;
        for (int i = 0; i < 256; i++) {
            uint16_t cluster = (uint16_t)(s * 256 + i);
            if (cluster < 2) continue;
            if (entries[i] == 0) {
                entries[i] = 0xFFFF; /* mark end-of-chain */
                write_sector(fat_start_lba + s, buf);
                write_sector(fat_start_lba + s + bpb.sectors_per_fat, buf);
                return cluster;
            }
        }
    }
    return 0; /* disk full */
}

/* ── Cluster <-> LBA ─────────────────────────────────────────── */
static uint32_t cluster_to_lba(uint16_t cluster) {
    return data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster;
}

/* ── Simple string helpers ───────────────────────────────────── */
static int fat_strncmp(const char* a, const char* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return 1;
        if (!a[i]) return 0;
    }
    return 0;
}

static int fat_toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

/* Convert "filename.ext" → FAT 8.3 name (space-padded, uppercase) */
static void to_83(const char* name, char out[11]) {
    for (int i = 0; i < 11; i++) out[i] = ' ';
    int i = 0, j = 0;
    while (name[i] && name[i] != '.' && j < 8) {
        unsigned char c = (unsigned char)name[i++];
        /* FAT8.3: spaces and illegal chars become underscore */
        if (c == ' ' || c == '+' || c == ',' || c == ';' ||
            c == '=' || c == '[' || c == ']')
            c = '_';
        out[j++] = (char)fat_toupper(c);
    }
    while (name[i] && name[i] != '.') i++; /* skip rest of name */
    if (name[i] == '.') {
        i++;
        j = 8;
        while (name[i] && j < 11) {
            unsigned char c = (unsigned char)name[i++];
            if (c == ' ') c = '_';
            out[j++] = (char)fat_toupper(c);
        }
    }
}

/* ── Root directory search ───────────────────────────────────── */
/* Returns 1 if found, fills *entry and *entry_lba, *entry_off */
static int root_find(const char* name83, fat16_dirent_t* entry,
                     uint32_t* entry_lba, uint32_t* entry_off)
{
    uint32_t entries_per_sector = 512 / sizeof(fat16_dirent_t);
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        uint8_t buf[512];
        read_sector(root_dir_lba + s, buf);
        fat16_dirent_t* dir = (fat16_dirent_t*)buf;
        for (uint32_t e = 0; e < entries_per_sector; e++) {
            if ((uint8_t)dir[e].name[0] == 0x00) return 0; /* no more */
            if ((uint8_t)dir[e].name[0] == 0xE5) continue; /* deleted */
            if (dir[e].attributes == FAT16_ATTR_LFN) continue;
            char entry_name[11];
            for (int k = 0; k < 11; k++) entry_name[k] = dir[e].name[k];
            if (!fat_strncmp(entry_name, name83, 11)) {
                *entry     = dir[e];
                *entry_lba = root_dir_lba + s;
                *entry_off = e * sizeof(fat16_dirent_t);
                return 1;
            }
        }
    }
    return 0;
}

/* Write a dirent back to disk */
static void root_write_entry(uint32_t lba, uint32_t off, fat16_dirent_t* e) {
    uint8_t buf[512];
    read_sector(lba, buf);
    fat16_dirent_t* slot = (fat16_dirent_t*)(buf + off);
    *slot = *e;
    write_sector(lba, buf);
}

/* Find an empty root dir slot */
static int root_alloc(uint32_t* out_lba, uint32_t* out_off) {
    uint32_t eps = 512 / sizeof(fat16_dirent_t);
    for (uint32_t s = 0; s < root_dir_sectors; s++) {
        uint8_t buf[512];
        read_sector(root_dir_lba + s, buf);
        fat16_dirent_t* dir = (fat16_dirent_t*)buf;
        for (uint32_t e = 0; e < eps; e++) {
            uint8_t first = (uint8_t)dir[e].name[0];
            if (first == 0x00 || first == 0xE5) {
                *out_lba = root_dir_lba + s;
                *out_off = e * sizeof(fat16_dirent_t);
                return 1;
            }
        }
    }
    return 0;
}

/* ── Open file descriptors ───────────────────────────────────── */
#define FAT16_MAX_FD 8

typedef struct {
    uint8_t  used;
    uint8_t  writable;
    uint16_t first_cluster;
    uint32_t file_size;
    uint32_t position;       /* byte offset within file */
    uint16_t cur_cluster;
    uint32_t cluster_offset; /* byte offset within cur_cluster */
    /* for write: where to update dirent on disk */
    uint32_t dirent_lba;
    uint32_t dirent_off;
} fat16_fd_t;

static fat16_fd_t fds[FAT16_MAX_FD];

/* ── Public API ──────────────────────────────────────────────── */

int fat16_init(void) {
    uint8_t buf[512];
    if (read_sector(0, buf) != 0) {
        vga_puts_color("  [!!] FAT16: failed to read sector 0\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }

    bpb = *(fat16_bpb_t*)buf;

    if (bpb.bytes_per_sector != 512) {
        vga_puts_color("  [!!] FAT16: unsupported sector size\n",
                       VGA_LIGHT_RED, VGA_BLACK);
        return -1;
    }

    fat_start_lba      = bpb.reserved_sectors;
    root_dir_lba       = fat_start_lba + bpb.num_fats * bpb.sectors_per_fat;
    root_dir_sectors   = (bpb.root_entry_count * 32 + 511) / 512;
    data_start_lba     = root_dir_lba + root_dir_sectors;
    sectors_per_cluster = bpb.sectors_per_cluster;

    for (int i = 0; i < FAT16_MAX_FD; i++) fds[i].used = 0;

    initialized = 1;
    vga_puts_color("  [OK] FAT16 filesystem mounted\n",
                   VGA_LIGHT_GREEN, VGA_BLACK);
    return 0;
}

int fat16_open(const char* path) {
    if (!initialized) return -1;

    /* Skip leading slash */
    if (path[0] == '/') path++;

    char name83[11];
    to_83(path, name83);

    fat16_dirent_t entry;
    uint32_t elba, eoff;
    if (!root_find(name83, &entry, &elba, &eoff)) return -1;
    if (entry.attributes & FAT16_ATTR_DIRECTORY) return -1;

    /* Find free fd */
    for (int i = 0; i < FAT16_MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i].used          = 1;
            fds[i].writable      = 0;
            fds[i].first_cluster = entry.first_cluster;
            fds[i].file_size     = entry.file_size;
            fds[i].position      = 0;
            fds[i].cur_cluster   = entry.first_cluster;
            fds[i].cluster_offset = 0;
            fds[i].dirent_lba    = elba;
            fds[i].dirent_off    = eoff;
            return i;
        }
    }
    return -1; /* no free fd */
}

int fat16_read(int fd, void* buf, uint32_t size) {
    if (fd < 0 || fd >= FAT16_MAX_FD || !fds[fd].used) return -1;
    fat16_fd_t* f = &fds[fd];

    uint8_t* dst = (uint8_t*)buf;
    uint32_t bytes_read = 0;
    uint32_t cluster_size = sectors_per_cluster * 512;

    while (bytes_read < size && f->position < f->file_size) {
        if (f->cur_cluster == 0 || f->cur_cluster >= FAT16_EOC) break;

        /* How many bytes remain in current cluster? */
        uint32_t in_cluster = cluster_size - f->cluster_offset;
        uint32_t can_read   = size - bytes_read;
        uint32_t left_file  = f->file_size - f->position;
        if (can_read > in_cluster) can_read = in_cluster;
        if (can_read > left_file)  can_read = left_file;

        /* Read sectors of current cluster */
        uint32_t lba = cluster_to_lba(f->cur_cluster);
        uint32_t sector_in_cluster = f->cluster_offset / 512;
        uint32_t offset_in_sector  = f->cluster_offset % 512;

        uint8_t sbuf[512];
        while (can_read > 0) {
            read_sector(lba + sector_in_cluster, sbuf);
            uint32_t chunk = 512 - offset_in_sector;
            if (chunk > can_read) chunk = can_read;
            for (uint32_t k = 0; k < chunk; k++)
                dst[bytes_read + k] = sbuf[offset_in_sector + k];
            bytes_read        += chunk;
            f->position       += chunk;
            f->cluster_offset += chunk;
            can_read          -= chunk;
            sector_in_cluster  = f->cluster_offset / 512;
            offset_in_sector   = 0;
        }

        /* Advance to next cluster if we exhausted this one */
        if (f->cluster_offset >= cluster_size) {
            f->cur_cluster    = fat_get(f->cur_cluster);
            f->cluster_offset = 0;
        }
    }
    return (int)bytes_read;
}

int fat16_write(int fd, const void* buf, uint32_t size) {
    if (fd < 0 || fd >= FAT16_MAX_FD || !fds[fd].used) return -1;
    fat16_fd_t* f = &fds[fd];
    if (!f->writable) return -1;

    const uint8_t* src = (const uint8_t*)buf;
    uint32_t bytes_written = 0;
    uint32_t cluster_size  = sectors_per_cluster * 512;

    while (bytes_written < size) {
        /* Allocate first cluster if needed */
        if (f->cur_cluster == 0 || f->cur_cluster >= FAT16_EOC) {
            uint16_t nc = fat_alloc_cluster();
            if (!nc) break; /* disk full */
            if (f->first_cluster == 0) {
                f->first_cluster = nc;
                f->cur_cluster   = nc;
            } else {
                /* Find last cluster and link */
                uint16_t c = f->first_cluster;
                while (fat_get(c) < FAT16_EOC) c = fat_get(c);
                fat_set(c, nc);
                f->cur_cluster = nc;
            }
            f->cluster_offset = 0;
        }

        uint32_t in_cluster = cluster_size - f->cluster_offset;
        uint32_t can_write  = size - bytes_written;
        if (can_write > in_cluster) can_write = in_cluster;

        uint32_t lba = cluster_to_lba(f->cur_cluster);
        uint32_t sec = f->cluster_offset / 512;
        uint32_t off = f->cluster_offset % 512;

        while (can_write > 0) {
            uint8_t sbuf[512];
            read_sector(lba + sec, sbuf);
            uint32_t chunk = 512 - off;
            if (chunk > can_write) chunk = can_write;
            for (uint32_t k = 0; k < chunk; k++)
                sbuf[off + k] = src[bytes_written + k];
            write_sector(lba + sec, sbuf);
            bytes_written     += chunk;
            f->position       += chunk;
            f->cluster_offset += chunk;
            if (f->position > f->file_size) f->file_size = f->position;
            can_write -= chunk;
            sec = f->cluster_offset / 512;
            off = 0;
        }

        /* Advance cluster */
        if (f->cluster_offset >= cluster_size) {
            uint16_t next = fat_get(f->cur_cluster);
            if (next >= FAT16_EOC) next = 0; /* will alloc next iter */
            f->cur_cluster    = next;
            f->cluster_offset = 0;
        }
    }

    /* Update dirent with new size and first cluster */
    uint8_t dbuf[512];
    read_sector(f->dirent_lba, dbuf);
    fat16_dirent_t* de = (fat16_dirent_t*)(dbuf + f->dirent_off);
    de->file_size     = f->file_size;
    de->first_cluster = f->first_cluster;
    write_sector(f->dirent_lba, dbuf);

    return (int)bytes_written;
}

int fat16_create(const char* path) {
    if (!initialized) return -1;
    if (path[0] == '/') path++;

    char name83[11];
    to_83(path, name83);

    /* Check if already exists */
    fat16_dirent_t existing;
    uint32_t elba, eoff;
    if (root_find(name83, &existing, &elba, &eoff)) {
        /* File exists — open it for writing, truncate */
        for (int i = 0; i < FAT16_MAX_FD; i++) {
            if (!fds[i].used) {
                fds[i].used           = 1;
                fds[i].writable       = 1;
                fds[i].first_cluster  = 0; /* truncate */
                fds[i].file_size      = 0;
                fds[i].position       = 0;
                fds[i].cur_cluster    = 0;
                fds[i].cluster_offset = 0;
                fds[i].dirent_lba     = elba;
                fds[i].dirent_off     = eoff;
                /* zero out old chain */
                uint16_t c = existing.first_cluster;
                while (c >= 2 && c < FAT16_EOC) {
                    uint16_t next = fat_get(c);
                    fat_set(c, 0);
                    c = next;
                }
                /* update dirent */
                existing.first_cluster = 0;
                existing.file_size     = 0;
                root_write_entry(elba, eoff, &existing);
                return i;
            }
        }
        return -1;
    }

    /* Allocate new root dir entry */
    uint32_t new_lba, new_off;
    if (!root_alloc(&new_lba, &new_off)) return -1;

    fat16_dirent_t ne;
    for (int i = 0; i < 11; i++) ne.name[i] = name83[i];
    ne.attributes        = FAT16_ATTR_ARCHIVE;
    ne.reserved          = 0;
    ne.create_time_tenth = 0;
    ne.create_time       = 0;
    ne.create_date       = 0;
    ne.access_date       = 0;
    ne.first_cluster_high = 0;
    ne.write_time        = 0;
    ne.write_date        = 0;
    ne.first_cluster     = 0;
    ne.file_size         = 0;
    root_write_entry(new_lba, new_off, &ne);

    for (int i = 0; i < FAT16_MAX_FD; i++) {
        if (!fds[i].used) {
            fds[i].used           = 1;
            fds[i].writable       = 1;
            fds[i].first_cluster  = 0;
            fds[i].file_size      = 0;
            fds[i].position       = 0;
            fds[i].cur_cluster    = 0;
            fds[i].cluster_offset = 0;
            fds[i].dirent_lba     = new_lba;
            fds[i].dirent_off     = new_off;
            return i;
        }
    }
    return -1;
}

int fat16_close(int fd) {
    if (fd < 0 || fd >= FAT16_MAX_FD || !fds[fd].used) return -1;
    fds[fd].used = 0;
    return 0;
}

/* ── fat16_list: read root directory entries ─────────────────── */
int fat16_list(fat16_entry_t* entries, int max_entries) {
    fat16_dirent_t dir[16];
    int count = 0;
    uint32_t root_lba = bpb.reserved_sectors
                      + bpb.num_fats * bpb.sectors_per_fat;
    uint32_t root_sectors = (bpb.root_entry_count * 32
                             + bpb.bytes_per_sector - 1)
                            / bpb.bytes_per_sector;

    for (uint32_t sec = 0; sec < root_sectors && count < max_entries; sec++) {
        if (ata_read_sectors(root_lba + sec, 1, (uint8_t*)dir) != 0)
            break;
        for (int e = 0; e < 16 && count < max_entries; e++) {
            uint8_t first = (uint8_t)dir[e].name[0];
            if (first == 0x00) goto done;   /* no more entries */
            if (first == 0xE5) continue;    /* deleted */
            uint8_t attr = dir[e].attributes;
            if (attr == FAT16_ATTR_LFN)    continue;
            if (attr & FAT16_ATTR_VOLUME_ID) continue;
            if (attr & FAT16_ATTR_HIDDEN)  continue;
            if (attr & FAT16_ATTR_SYSTEM)  continue;

            /* Build display name: "NAME    EXT" → "name.ext" */
            fat16_entry_t* out = &entries[count];
            int ni = 0, k;
            for (k = 0; k < 8 && dir[e].name[k] != ' '; k++)
                out->name[ni++] = (dir[e].name[k] >= 'A' && dir[e].name[k] <= 'Z')
                                  ? dir[e].name[k] + 32 : dir[e].name[k];
            if (!(attr & FAT16_ATTR_DIRECTORY)) {
                int has_ext = 0;
                for (k = 0; k < 3; k++) if (dir[e].ext[k] != ' ') { has_ext=1; break; }
                if (has_ext) {
                    out->name[ni++] = '.';
                    for (k = 0; k < 3 && dir[e].ext[k] != ' '; k++)
                        out->name[ni++] = (dir[e].ext[k] >= 'A' && dir[e].ext[k] <= 'Z')
                                          ? dir[e].ext[k] + 32 : dir[e].ext[k];
                }
            }
            out->name[ni] = 0;
            out->size   = dir[e].file_size;
            out->is_dir = (attr & FAT16_ATTR_DIRECTORY) ? 1 : 0;
            count++;
        }
    }
done:
    return count;
}

/* ── fat16_stat ───────────────────────────────────────────────── */
int fat16_stat(const char* path, uint32_t* size_out, uint8_t* is_dir_out) {
    if (!initialized) return -1;
    if (path[0] == '/') path++;
    if (path[0]=='d'&&path[1]=='i'&&path[2]=='s'&&path[3]=='k'&&path[4]=='/') path+=5;
    char name83[11];
    to_83(path, name83);
    fat16_dirent_t entry; uint32_t elba, eoff;
    if (!root_find(name83, &entry, &elba, &eoff)) return -1;
    if (size_out)   *size_out   = entry.file_size;
    if (is_dir_out) *is_dir_out = (entry.attributes & FAT16_ATTR_DIRECTORY) ? 1 : 0;
    return 0;
}
/* ── fat16_unlink ─────────────────────────────────────────────── */
int fat16_unlink(const char* path) {
    if (!initialized) return -1;
    if (path[0] == '/') path++;
    if (path[0]=='d'&&path[1]=='i'&&path[2]=='s'&&path[3]=='k'&&path[4]=='/') path+=5;
    char name83[11];
    to_83(path, name83);
    fat16_dirent_t entry; uint32_t elba, eoff;
    if (!root_find(name83, &entry, &elba, &eoff)) return -1;
    if (entry.attributes & FAT16_ATTR_DIRECTORY) return -1;
    uint16_t cluster = entry.first_cluster;
    while (cluster >= 2 && cluster < 0xFFF8) {
        uint16_t next = fat_get(cluster);
        fat_set(cluster, 0);
        cluster = next;
    }
    uint8_t buf[512];
    read_sector(elba, buf);
    buf[eoff] = 0xE5;
    write_sector(elba, buf);
    return 0;
}
/* ── fat16_mkdir ──────────────────────────────────────────────── */
int fat16_mkdir(const char* path) {
    if (!initialized) return -1;
    if (path[0] == '/') path++;
    if (path[0]=='d'&&path[1]=='i'&&path[2]=='s'&&path[3]=='k'&&path[4]=='/') path+=5;
    char name83[11];
    to_83(path, name83);
    fat16_dirent_t existing; uint32_t elba2, eoff2;
    if (root_find(name83, &existing, &elba2, &eoff2)) return -1;
    uint16_t cluster = fat_alloc_cluster();
    if (cluster == 0) return -1;
    uint8_t zero[512];
    for (int i = 0; i < 512; i++) zero[i] = 0;
    uint32_t dlba = data_start_lba + (uint32_t)(cluster - 2) * sectors_per_cluster;
    for (uint32_t s = 0; s < sectors_per_cluster; s++) write_sector(dlba + s, zero);
    uint32_t out_lba, out_off;
    if (!root_alloc(&out_lba, &out_off)) { fat_set(cluster, 0); return -1; }
    uint8_t buf[512];
    read_sector(out_lba, buf);
    fat16_dirent_t* d = (fat16_dirent_t*)(buf + out_off);
    for (int i = 0; i < 8; i++) d->name[i] = name83[i];
    for (int i = 0; i < 3; i++) d->ext[i]  = name83[8 + i];
    d->attributes=FAT16_ATTR_DIRECTORY; d->reserved=0;
    d->create_time_tenth=0; d->create_time=0; d->create_date=0;
    d->access_date=0; d->first_cluster_high=0;
    d->write_time=0; d->write_date=0;
    d->first_cluster=cluster; d->file_size=0;
    write_sector(out_lba, buf);
    return 0;
}
