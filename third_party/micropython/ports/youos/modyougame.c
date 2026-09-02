// modyougame.c — pygame-inspired graphics module for YouOS MicroPython port
// Built on SYS_FBINFO / SYS_FBWRITE / SYS_KEYPOLL / SYS_TICKS / SYS_SLEEP

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objtuple.h"
#include "py/objlist.h"
#include "py/builtin.h"
#include "py/objarray.h"

#include "syscall.h"

// -----------------------------------------------------------------------------
// Color helper
// -----------------------------------------------------------------------------

static uint32_t mp_obj_to_color(mp_obj_t color) {
    if (mp_obj_is_int(color)) {
        return (uint32_t)mp_obj_get_int(color);
    }
    if (mp_obj_is_type(color, &mp_type_tuple)) {
        mp_obj_t *items;
        size_t len;
        mp_obj_tuple_get(color, &len, &items);
        if (len == 3) {
            uint32_t r = (uint32_t)mp_obj_get_int(items[0]) & 0xFF;
            uint32_t g = (uint32_t)mp_obj_get_int(items[1]) & 0xFF;
            uint32_t b = (uint32_t)mp_obj_get_int(items[2]) & 0xFF;
            return (r << 16) | (g << 8) | b;
        }
    }
    mp_raise_ValueError(MP_ERROR_TEXT("color must be int 0xRRGGBB or (r,g,b) tuple"));
    return 0; // never reached
}

// -----------------------------------------------------------------------------
// Surface type
// -----------------------------------------------------------------------------

typedef struct _yougame_surface_obj_t {
    mp_obj_base_t base;
    mp_int_t width;
    mp_int_t height;
    mp_obj_t buffer;  // bytearray of width*height*4 bytes
} yougame_surface_obj_t;

// Forward declaration — used in make_new before the type is fully defined
const mp_obj_type_t yougame_surface_type;

static mp_obj_t surface_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    (void)type;
    mp_arg_check_num(n_args, n_kw, 1, 1, false);
    mp_obj_t *items;
    size_t len;
    mp_obj_tuple_get(args[0], &len, &items);
    if (len != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("size must be (w, h)"));
    }
    mp_int_t w = mp_obj_get_int(items[0]);
    mp_int_t h = mp_obj_get_int(items[1]);
    if (w <= 0 || h <= 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("size must be positive"));
    }

    yougame_surface_obj_t *surf = m_new_obj(yougame_surface_obj_t);
    surf->base.type = &yougame_surface_type;
    surf->width = w;
    surf->height = h;
    byte *buf = m_new0(byte, w * h * 4);
    surf->buffer = mp_obj_new_bytearray(w * h * 4, buf);
    return MP_OBJ_FROM_PTR(surf);
}

static mp_obj_t surface_fill(mp_obj_t self_in, mp_obj_t color_in) {
    yougame_surface_obj_t *self = MP_OBJ_TO_PTR(self_in);
    uint32_t color = mp_obj_to_color(color_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer(self->buffer, &bufinfo, MP_BUFFER_RW);
    uint32_t *pixels = (uint32_t *)bufinfo.buf;
    for (mp_int_t i = 0; i < self->width * self->height; i++) {
        pixels[i] = color;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(surface_fill_obj, surface_fill);

static const mp_rom_map_elem_t surface_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_fill), MP_ROM_PTR(&surface_fill_obj) },
};
static MP_DEFINE_CONST_DICT(surface_locals_dict, surface_locals_dict_table);

MP_DEFINE_CONST_OBJ_TYPE(
    yougame_surface_type,
    MP_QSTR_Surface,
    MP_TYPE_FLAG_NONE,
    make_new, surface_make_new,
    locals_dict, &surface_locals_dict
);

// -----------------------------------------------------------------------------
// Display submodule
// -----------------------------------------------------------------------------

static mp_obj_t yougame_display_set_mode(mp_obj_t size_in) {
    return surface_make_new(&yougame_surface_type, 1, 0, &size_in);
}
static MP_DEFINE_CONST_FUN_OBJ_1(yougame_display_set_mode_obj, yougame_display_set_mode);

static mp_obj_t yougame_display_flip(mp_obj_t surf_in) {
    yougame_surface_obj_t *surf = MP_OBJ_TO_PTR(surf_in);
    mp_buffer_info_t bufinfo;
    mp_get_buffer(surf->buffer, &bufinfo, MP_BUFFER_RW);
    sys_fbwrite(0, 0, (uint64_t)surf->width, (uint64_t)surf->height, bufinfo.buf);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(yougame_display_flip_obj, yougame_display_flip);

static mp_obj_t yougame_display_get_size(void) {
    uint64_t info[5];
    if (sys_fbinfo(info) != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("fbinfo failed"));
    }
    mp_obj_t items[2] = {
        mp_obj_new_int((mp_int_t)info[1]),
        mp_obj_new_int((mp_int_t)info[2]),
    };
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(yougame_display_get_size_obj, yougame_display_get_size);

static const mp_rom_map_elem_t display_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_display) },
    { MP_ROM_QSTR(MP_QSTR_set_mode), MP_ROM_PTR(&yougame_display_set_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_flip), MP_ROM_PTR(&yougame_display_flip_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_size), MP_ROM_PTR(&yougame_display_get_size_obj) },
};
static MP_DEFINE_CONST_DICT(display_module_globals, display_module_globals_table);

static const mp_obj_module_t yougame_display_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&display_module_globals,
};

// -----------------------------------------------------------------------------
// Draw submodule
// -----------------------------------------------------------------------------

static mp_obj_t yougame_draw_rect(mp_obj_t surf_in, mp_obj_t color_in, mp_obj_t rect_in) {
    yougame_surface_obj_t *surf = MP_OBJ_TO_PTR(surf_in);
    uint32_t color = mp_obj_to_color(color_in);

    mp_obj_t *items;
    size_t len;
    mp_obj_tuple_get(rect_in, &len, &items);
    if (len != 4) {
        mp_raise_ValueError(MP_ERROR_TEXT("rect must be (x, y, w, h)"));
    }
    mp_int_t x = mp_obj_get_int(items[0]);
    mp_int_t y = mp_obj_get_int(items[1]);
    mp_int_t w = mp_obj_get_int(items[2]);
    mp_int_t h = mp_obj_get_int(items[3]);

    mp_buffer_info_t bufinfo;
    mp_get_buffer(surf->buffer, &bufinfo, MP_BUFFER_RW);
    uint32_t *pixels = (uint32_t *)bufinfo.buf;

    for (mp_int_t row = y; row < y + h; row++) {
        if (row < 0 || row >= surf->height) continue;
        for (mp_int_t col = x; col < x + w; col++) {
            if (col < 0 || col >= surf->width) continue;
            pixels[row * surf->width + col] = color;
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(yougame_draw_rect_obj, yougame_draw_rect);

static mp_obj_t yougame_draw_line(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    yougame_surface_obj_t *surf = MP_OBJ_TO_PTR(args[0]);
    uint32_t color = mp_obj_to_color(args[1]);

    mp_obj_t *sitems, *eitems;
    size_t slen, elen;
    mp_obj_tuple_get(args[2], &slen, &sitems);
    mp_obj_tuple_get(args[3], &elen, &eitems);
    if (slen != 2 || elen != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("points must be (x, y)"));
    }
    mp_int_t x0 = mp_obj_get_int(sitems[0]);
    mp_int_t y0 = mp_obj_get_int(sitems[1]);
    mp_int_t x1 = mp_obj_get_int(eitems[0]);
    mp_int_t y1 = mp_obj_get_int(eitems[1]);

    mp_buffer_info_t bufinfo;
    mp_get_buffer(surf->buffer, &bufinfo, MP_BUFFER_RW);
    uint32_t *pixels = (uint32_t *)bufinfo.buf;

    mp_int_t dx = x1 - x0;
    mp_int_t dy = y1 - y0;
    mp_int_t sx = (dx >= 0) ? 1 : -1;
    mp_int_t sy = (dy >= 0) ? 1 : -1;
    dx = (dx >= 0) ? dx : -dx;
    dy = (dy >= 0) ? dy : -dy;
    mp_int_t err = dx - dy;

    while (1) {
        if (x0 >= 0 && x0 < surf->width && y0 >= 0 && y0 < surf->height) {
            pixels[y0 * surf->width + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        mp_int_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(yougame_draw_line_obj, 4, 4, yougame_draw_line);

static mp_obj_t yougame_draw_circle(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    yougame_surface_obj_t *surf = MP_OBJ_TO_PTR(args[0]);
    uint32_t color = mp_obj_to_color(args[1]);

    mp_obj_t *items;
    size_t len;
    mp_obj_tuple_get(args[2], &len, &items);
    if (len != 2) {
        mp_raise_ValueError(MP_ERROR_TEXT("center must be (x, y)"));
    }
    mp_int_t cx = mp_obj_get_int(items[0]);
    mp_int_t cy = mp_obj_get_int(items[1]);
    mp_int_t r = mp_obj_get_int(args[3]);
    if (r < 0) r = 0;

    mp_buffer_info_t bufinfo;
    mp_get_buffer(surf->buffer, &bufinfo, MP_BUFFER_RW);
    uint32_t *pixels = (uint32_t *)bufinfo.buf;

    for (mp_int_t row = cy - r; row <= cy + r; row++) {
        if (row < 0 || row >= surf->height) continue;
        for (mp_int_t col = cx - r; col <= cx + r; col++) {
            if (col < 0 || col >= surf->width) continue;
            mp_int_t dx = col - cx;
            mp_int_t dy = row - cy;
            if (dx * dx + dy * dy <= r * r) {
                pixels[row * surf->width + col] = color;
            }
        }
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(yougame_draw_circle_obj, 4, 4, yougame_draw_circle);

static const mp_rom_map_elem_t draw_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_draw) },
    { MP_ROM_QSTR(MP_QSTR_rect), MP_ROM_PTR(&yougame_draw_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_line), MP_ROM_PTR(&yougame_draw_line_obj) },
    { MP_ROM_QSTR(MP_QSTR_circle), MP_ROM_PTR(&yougame_draw_circle_obj) },
};
static MP_DEFINE_CONST_DICT(draw_module_globals, draw_module_globals_table);

static const mp_obj_module_t yougame_draw_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&draw_module_globals,
};

// -----------------------------------------------------------------------------
// Time submodule
// -----------------------------------------------------------------------------

static mp_obj_t yougame_time_wait(mp_obj_t ms_in) {
    mp_int_t ms = mp_obj_get_int(ms_in);
    if (ms > 0) sys_sleep((uint64_t)ms);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(yougame_time_wait_obj, yougame_time_wait);

static mp_obj_t yougame_time_get_ticks(void) {
    return mp_obj_new_int((mp_int_t)sys_ticks());
}
static MP_DEFINE_CONST_FUN_OBJ_0(yougame_time_get_ticks_obj, yougame_time_get_ticks);

static const mp_rom_map_elem_t time_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_time) },
    { MP_ROM_QSTR(MP_QSTR_wait), MP_ROM_PTR(&yougame_time_wait_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_ticks), MP_ROM_PTR(&yougame_time_get_ticks_obj) },
};
static MP_DEFINE_CONST_DICT(time_module_globals, time_module_globals_table);

static const mp_obj_module_t yougame_time_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&time_module_globals,
};

// -----------------------------------------------------------------------------
// Event submodule
// -----------------------------------------------------------------------------

static mp_obj_t yougame_event_get(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    while (1) {
        int64_t ev = sys_keypoll();
        if (ev == 0) break;
        mp_obj_t event_dict = mp_obj_new_dict(2);
        mp_obj_dict_store(event_dict, MP_OBJ_NEW_QSTR(MP_QSTR_type), MP_OBJ_NEW_SMALL_INT(1)); // KEYDOWN
        mp_obj_dict_store(event_dict, MP_OBJ_NEW_QSTR(MP_QSTR_key), MP_OBJ_NEW_SMALL_INT((mp_int_t)ev));
        mp_obj_list_append(list, event_dict);
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(yougame_event_get_obj, yougame_event_get);

static const mp_rom_map_elem_t event_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_event) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&yougame_event_get_obj) },
};
static MP_DEFINE_CONST_DICT(event_module_globals, event_module_globals_table);

static const mp_obj_module_t yougame_event_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&event_module_globals,
};

// -----------------------------------------------------------------------------
// Top-level yougame module
// -----------------------------------------------------------------------------

static const mp_rom_map_elem_t yougame_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_yougame) },
    { MP_ROM_QSTR(MP_QSTR_Surface), MP_ROM_PTR(&yougame_surface_type) },
    { MP_ROM_QSTR(MP_QSTR_display), MP_ROM_PTR(&yougame_display_module) },
    { MP_ROM_QSTR(MP_QSTR_draw), MP_ROM_PTR(&yougame_draw_module) },
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&yougame_time_module) },
    { MP_ROM_QSTR(MP_QSTR_event), MP_ROM_PTR(&yougame_event_module) },

    // Event type constants
    { MP_ROM_QSTR(MP_QSTR_QUIT), MP_ROM_INT(0) },
    { MP_ROM_QSTR(MP_QSTR_KEYDOWN), MP_ROM_INT(1) },
    { MP_ROM_QSTR(MP_QSTR_KEYUP), MP_ROM_INT(2) },

    // Key constants (match sys_keypoll return values)
    { MP_ROM_QSTR(MP_QSTR_K_UP), MP_ROM_INT(1001) },
    { MP_ROM_QSTR(MP_QSTR_K_DOWN), MP_ROM_INT(1002) },
    { MP_ROM_QSTR(MP_QSTR_K_LEFT), MP_ROM_INT(1003) },
    { MP_ROM_QSTR(MP_QSTR_K_RIGHT), MP_ROM_INT(1004) },
    { MP_ROM_QSTR(MP_QSTR_K_HOME), MP_ROM_INT(1005) },
    { MP_ROM_QSTR(MP_QSTR_K_END), MP_ROM_INT(1006) },
    { MP_ROM_QSTR(MP_QSTR_K_DELETE), MP_ROM_INT(1007) },
    { MP_ROM_QSTR(MP_QSTR_K_PGUP), MP_ROM_INT(1008) },
    { MP_ROM_QSTR(MP_QSTR_K_PGDN), MP_ROM_INT(1009) },
};
static MP_DEFINE_CONST_DICT(yougame_module_globals, yougame_module_globals_table);

const mp_obj_module_t yougame_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&yougame_module_globals,
};

MP_REGISTER_EXTENSIBLE_MODULE(MP_QSTR_yougame, yougame_module);
