
#include <utility>

#include <lvgl_tft/ssd1306.h>

#include "volume_display.h"

namespace {
constexpr uint16_t kNumBufferRows = 20;
constexpr uint16_t kDisplayWidth = 64;
constexpr uint16_t kDisplayHeight = 128;
constexpr uint32_t kNumBufferPixels = kDisplayWidth * kNumBufferRows;
}  // namespace

VolumeDisplay::VolumeDisplay(i2c::Master master)
    : i2c_master_(std::move(master)),
      disp_driver_(nullptr),
      display_buf_1_(new lv_color_t[kDisplayWidth * kNumBufferRows]),
      display_buf_2_(new lv_color_t[kDisplayWidth * kNumBufferRows]) {}

bool VolumeDisplay::Initialize() {
  // The lvgl_esp32_drivers library initializes the one configured
  // display when lvgl_driver_init() is called. We need to manually
  // initialize supplemental drivers. Maybe that library can support
  // more than one display type in the future. Bug added at
  // https://github.com/lvgl/lvgl_esp32_drivers/issues/31
  ssd1306_init();

  lv_disp_buf_init(&disp_buf_, display_buf_1_.get(), display_buf_2_.get(),
                   kNumBufferPixels);

  lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);

  disp_drv.flush_cb = ssd1306_flush;
  disp_drv.buffer = &disp_buf_;
  disp_driver_ = lv_disp_drv_register(&disp_drv);
  if (!disp_driver_)
    return false;

  return true;
}