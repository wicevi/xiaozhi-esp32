#pragma once

#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIC_CAM_SCCB_I2C_NUM        I2C_NUM_0
#define WIC_CAM_SCCB_FREQ           (100000)
#define WIC_CAM_SCCB_SCL_IO         (8)
#define WIC_CAM_SCCB_SDA_IO         (7)
#define WIC_CAM_CSI_LANE_BITRATE    (200)
#define WIC_CAM_CSI_LANE_NUM        (2)
#define WIC_CAM_CSI_BYTE_SWAP_EN    (true)
#define WIC_CAM_CSI_FMT_800_600     (1)
// #define WIC_CAM_CSI_FMT_800_1280    (1)
// #define WIC_CAM_CSI_FMT_1024_600    (0)

#define WIC_CAM_ISP_ENABLE          (1)
#if WIC_CAM_ISP_ENABLE
#define WIC_CAM_ISP_CLK_HZ          (80 * 1000 * 1000)
#define WIC_CAM_ISP_INPUT_SRC       ISP_INPUT_DATA_SOURCE_CSI
#define WIC_CAM_ISP_INPUT_COLOR     ISP_COLOR_RAW8
#define WIC_CAM_ISP_OUTPUT_COLOR    ISP_COLOR_RGB565
#define WIC_CAM_ISP_LINE_START_P    (false)
#define WIC_CAM_ISP_LINE_END_P      (false)
#endif

#if WIC_CAM_CSI_FMT_800_600
#define WIC_CAM_CSI_FMT_NAME        "MIPI_2lane_24Minput_RAW8_800x640_50fps"
#define WIC_CAM_CSI_HRES            (800)
#define WIC_CAM_CSI_VRES            (640)
#elif WIC_CAM_CSI_FMT_800_1280
#define WIC_CAM_CSI_FMT_NAME        "MIPI_2lane_24Minput_RAW8_800x1280_50fps"
#define WIC_CAM_CSI_HRES            (800)
#define WIC_CAM_CSI_VRES            (1280)
#elif WIC_CAM_CSI_FMT_1024_600
#define WIC_CAM_CSI_FMT_NAME        "MIPI_2lane_24Minput_RAW8_1024x600_30fps"
#define WIC_CAM_CSI_HRES            (1024)
#define WIC_CAM_CSI_VRES            (600)
#endif

#define WIC_CAM_BUF_NUM             (1)

typedef struct {
    void *data;
    size_t all_len;
    size_t recv_len;
    uint8_t is_free;
} wic_cam_img_buf_t;

esp_err_t wic_cam_sensor_init(i2c_master_bus_handle_t i2c_handle);
esp_err_t wic_cam_sensor_start(void);
esp_err_t wic_cam_sensor_recv_img_buf(wic_cam_img_buf_t **img_buf, uint32_t timeout);
void wic_cam_sensor_free_img_buf(wic_cam_img_buf_t *img_buf);
esp_err_t wic_cam_sensor_stop(void);
esp_err_t wic_cam_sensor_take_picture(wic_cam_img_buf_t *img_buf, uint32_t timeout);
void wic_cam_sensor_deinit(void);

#ifdef __cplusplus
}
#endif
