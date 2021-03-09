#pragma once

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_WIFI_ONLINE
#define LV_ATTRIBUTE_IMG_WIFI_ONLINE
#endif

constexpr LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_WIFI_ONLINE uint8_t wifi_online_map[] = {
  0x00, 0x00, 0x00, 0xff, 	/*Color of index 0*/
  0xff, 0xff, 0xff, 0xff, 	/*Color of index 1*/
  0xfc, 0xfc, 0xfc, 0xff, 	/*Color of index 2*/
  0xe7, 0xe7, 0xe7, 0xff, 	/*Color of index 3*/

  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x04, 0x00, 0x00,
  0x00, 0x2d, 0xaa, 0x78, 0x00,
  0x0e, 0x29, 0x55, 0x68, 0xb0,
  0xf3, 0x5b, 0x00, 0xe5, 0xcf,
  0x69, 0xeb, 0x14, 0xeb, 0x69,
  0x16, 0x10, 0xaa, 0x04, 0x94,
  0x4a, 0x58, 0x96, 0x25, 0xa1,
  0x05, 0x96, 0xc3, 0x96, 0x50,
  0x00, 0x68, 0x69, 0x29, 0x00,
  0x0b, 0xf5, 0xaa, 0x5f, 0xe0,
  0x0e, 0x31, 0xbe, 0x4c, 0xb0,
  0x10, 0x21, 0xc3, 0x48, 0x04,
  0x00, 0x60, 0x82, 0x09, 0x00,
  0x00, 0xe9, 0x28, 0x6b, 0x00,
  0x00, 0x34, 0x3c, 0x1c, 0x00,
  0x00, 0x00, 0x96, 0x00, 0x00,
  0x00, 0x00, 0x69, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00,
};

constexpr lv_img_dsc_t wifi_online = {
    .header =
        {
            .cf = LV_IMG_CF_INDEXED_2BIT,
            .always_zero = 0,
            .reserved = 0,
            .w = 20,
            .h = 20,
        },
    .data_size = 116,
    .data = wifi_online_map,
};
