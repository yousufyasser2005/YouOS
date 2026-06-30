#pragma once

void mouse_init(void);
int  mouse_get_x(void);
int  mouse_get_y(void);
int  mouse_get_buttons(void);
void mouse_update_usb(void);
void mouse_set_debug_report(int len, int byte3);
int  mouse_get_debug_len(void);
int  mouse_get_debug_byte3(void);
int  mouse_get_wheel_delta(void);
int  mouse_has_wheel_support(void);
