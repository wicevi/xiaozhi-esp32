#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_sccb_intf.h"
#include "esp_sccb_i2c.h"
#include "esp_cam_ctlr_csi.h"
#include "esp_cam_ctlr.h"
#include "esp_cam_sensor.h"
#include "esp_cam_sensor_detect.h"
#include "driver/isp.h"
#include "wic_cam_sensor.h"

static const char *TAG = "wic_cam_sensor";
static i2c_master_bus_handle_t i2c_bus_handle = NULL;
static esp_cam_ctlr_handle_t cam_handle = NULL;
static isp_proc_handle_t isp_proc = NULL;
static wic_cam_img_buf_t cam_img_buf[WIC_CAM_BUF_NUM] = {0};
static QueueHandle_t cam_img_queue = NULL;

static wic_cam_img_buf_t *get_free_cam_img_buf(void)
{
    for (int i = 0; i < WIC_CAM_BUF_NUM; i++) {
        if (cam_img_buf[i].is_free) {
            cam_img_buf[i].is_free = 0;
            return &cam_img_buf[i];
        }
    }
    return NULL;
}

static wic_cam_img_buf_t *get_cam_img_buf(void *buf_ptr)
{
    for (int i = 0; i < WIC_CAM_BUF_NUM; i++) {
        if (cam_img_buf[i].data == buf_ptr) {
            return &cam_img_buf[i];
        }
    }
    return NULL;
}

void wic_cam_sensor_free_img_buf(wic_cam_img_buf_t *img_buf)
{
    img_buf->is_free = 1;
}

static bool wic_cam_request_new_buffer(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    wic_cam_img_buf_t *img_buf = get_free_cam_img_buf();

    if (img_buf != NULL) {
        trans->buffer = img_buf->data;
        trans->buflen = img_buf->all_len;
    } else {
        trans->buffer = NULL;
        trans->buflen = 0;   
    }
    return false;
}

static bool wic_cam_finished_trans(esp_cam_ctlr_handle_t handle, esp_cam_ctlr_trans_t *trans, void *user_data)
{
    wic_cam_img_buf_t *img_buf = get_cam_img_buf(trans->buffer);
    if (img_buf == NULL) return false;

    img_buf->recv_len = trans->received_size;
    xQueueSendFromISR(cam_img_queue, &img_buf, NULL);
    return false;
}

esp_err_t wic_cam_sensor_init(i2c_master_bus_handle_t i2c_handle)
{
    int enable_flag = 0;
    esp_err_t ret = ESP_OK;
    esp_sccb_io_handle_t sccb_io_handle = NULL;
    esp_cam_sensor_device_t *cam = NULL;
    esp_cam_sensor_format_array_t cam_fmt_array = {0};
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .scl_io_num = WIC_CAM_SCCB_SCL_IO,
        .sda_io_num = WIC_CAM_SCCB_SDA_IO,
        .i2c_port = WIC_CAM_SCCB_I2C_NUM,
        .flags.enable_internal_pullup = true
    };
    esp_cam_sensor_config_t cam_config = {
        .sccb_handle = sccb_io_handle,
        .reset_pin = -1,
        .pwdn_pin = -1,
        .xclk_pin = -1,
        .sensor_port = ESP_CAM_SENSOR_MIPI_CSI,
    };
    sccb_i2c_config_t i2c_config = {
        .scl_speed_hz = WIC_CAM_SCCB_FREQ,
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    };
    esp_cam_ctlr_csi_config_t csi_config = {
        .ctlr_id = 0,
        .h_res = WIC_CAM_CSI_HRES,
        .v_res = WIC_CAM_CSI_VRES,
        .lane_bit_rate_mbps = WIC_CAM_CSI_LANE_BITRATE,
        .input_data_color_type = CAM_CTLR_COLOR_RAW8,
        .output_data_color_type = CAM_CTLR_COLOR_RGB565,
        .data_lane_num = WIC_CAM_CSI_LANE_NUM,
        .byte_swap_en = WIC_CAM_CSI_BYTE_SWAP_EN,
        .queue_items = 1,
    };
    esp_isp_processor_cfg_t isp_config = {
        .clk_hz = WIC_CAM_ISP_CLK_HZ,
        .input_data_source = WIC_CAM_ISP_INPUT_SRC,
        .input_data_color_type = WIC_CAM_ISP_INPUT_COLOR,
        .output_data_color_type = WIC_CAM_ISP_OUTPUT_COLOR,
        .has_line_start_packet = WIC_CAM_ISP_LINE_START_P,
        .has_line_end_packet = WIC_CAM_ISP_LINE_END_P,
        .h_res = WIC_CAM_CSI_HRES,
        .v_res = WIC_CAM_CSI_VRES,
    };
    esp_cam_ctlr_evt_cbs_t cbs = {
        .on_get_new_trans = wic_cam_request_new_buffer,
        .on_trans_finished = wic_cam_finished_trans,
    };
    
    if (cam_handle) {
        ESP_LOGE(TAG, "Camera controller already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    cam_img_queue = xQueueCreate(WIC_CAM_BUF_NUM, sizeof(wic_cam_img_buf_t *));
    if (cam_img_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create camera image queue");
        ret = ESP_ERR_NO_MEM;
        goto wic_cam_sensor_init_failed;
    }
    for (int i = 0; i < WIC_CAM_BUF_NUM; i++) {
        cam_img_buf[i].all_len = WIC_CAM_CSI_HRES * WIC_CAM_CSI_VRES * 2;
        cam_img_buf[i].data = heap_caps_malloc(cam_img_buf[i].all_len, MALLOC_CAP_CACHE_ALIGNED | MALLOC_CAP_SPIRAM);
        if (cam_img_buf[i].data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for camera transaction buffer");
            ret = ESP_ERR_NO_MEM;
            goto wic_cam_sensor_init_failed;
        }
        ret = esp_cache_msync((void *)cam_img_buf[i].data, cam_img_buf[i].all_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to sync cache for camera transaction buffer: %s", esp_err_to_name(ret));
            goto wic_cam_sensor_init_failed;
        }
        wic_cam_sensor_free_img_buf(&cam_img_buf[i]);
    }

    if (i2c_handle) i2c_bus_handle = i2c_handle;
    else {
        ret = i2c_new_master_bus(&bus_config, &i2c_bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create I2C bus: %s", esp_err_to_name(ret));
            goto wic_cam_sensor_init_failed;
        }
    }
        
    for (esp_cam_sensor_detect_fn_t *p = &__esp_cam_sensor_detect_fn_array_start; p < &__esp_cam_sensor_detect_fn_array_end; ++p) {
        i2c_config.device_address = p->sccb_addr;
        ret = sccb_new_i2c_io(i2c_bus_handle, &i2c_config, &cam_config.sccb_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create SCCB I2C IO: %s", esp_err_to_name(ret));
            goto wic_cam_sensor_init_failed;
        }

        cam = (*(p->detect))(&cam_config);
        if (cam) {
            if (p->port != ESP_CAM_SENSOR_MIPI_CSI) {
                esp_sccb_del_i2c_io(cam_config.sccb_handle);
                ret = ESP_ERR_INVALID_ARG;
                ESP_LOGE(TAG, "detect a camera sensor with mismatched interface");
                goto wic_cam_sensor_init_failed;
            }
            break;
        }
        esp_sccb_del_i2c_io(cam_config.sccb_handle);
    }
    if (!cam) {
        ret = ESP_ERR_NOT_FOUND;
        ESP_LOGE(TAG, "No compatible camera sensor found");
        goto wic_cam_sensor_init_failed;
    }

    ret = esp_cam_sensor_query_format(cam, &cam_fmt_array);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to query camera sensor formats: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }
    for (int i = 0; i < cam_fmt_array.count; i++) {
        if (!strcmp(cam_fmt_array.format_array[i].name, WIC_CAM_CSI_FMT_NAME)) {
            ret = esp_cam_sensor_set_format(cam, (const esp_cam_sensor_format_t *)&(cam_fmt_array.format_array[i].name));
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set camera sensor format: %s", esp_err_to_name(ret));
                goto wic_cam_sensor_init_failed;
            }
            enable_flag = 1;
            ESP_LOGI(TAG, "fmt[%d].name:%s", i, cam_fmt_array.format_array[i].name);
            break;
        }
    }

    if (!enable_flag) {
        ret = ESP_ERR_NOT_SUPPORTED;
        ESP_LOGE(TAG, "No compatible camera sensor format found");
        goto wic_cam_sensor_init_failed;
    }

    ret = esp_cam_sensor_ioctl(cam, ESP_CAM_SENSOR_IOC_S_STREAM, &enable_flag);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera sensor stream: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }

    ret = esp_cam_new_csi_ctlr(&csi_config, &cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create CSI controller: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }

    ret = esp_cam_ctlr_register_event_callbacks(cam_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register camera controller event callbacks: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }

    ret = esp_cam_ctlr_enable(cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable camera controller: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }
    
    ret = esp_isp_new_processor(&isp_config, &isp_proc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ISP processor: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }
    ret = esp_isp_enable(isp_proc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable ISP processor: %s", esp_err_to_name(ret));
        goto wic_cam_sensor_init_failed;
    }
    
    return ESP_OK;
wic_cam_sensor_init_failed:
    if (cam_handle) {
        esp_cam_ctlr_del(cam_handle);
        cam_handle = NULL;
    }
    if (i2c_bus_handle) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
    }
    if (cam_img_queue) {
        xQueueReset(cam_img_queue);
        vQueueDelete(cam_img_queue);
        cam_img_queue = NULL;
    }
    for (int i = 0; i < WIC_CAM_BUF_NUM; i++) {
        if (cam_img_buf[i].data) {
            free(cam_img_buf[i].data);
            cam_img_buf[i].data = NULL;
        }
    }
    if (isp_proc) {
        esp_isp_del_processor(isp_proc);
        isp_proc = NULL;
    }
    return ret;
}

esp_err_t wic_cam_sensor_start(void)
{
    esp_err_t ret = ESP_OK;
    esp_cam_ctlr_trans_t ctlr_trans = {0};
    
    if (cam_handle == NULL) return ESP_ERR_INVALID_STATE;
    ctlr_trans.buffer = cam_img_buf[0].data;
    ctlr_trans.buflen = cam_img_buf[0].all_len;

    ret = esp_cam_ctlr_start(cam_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start camera controller: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_cam_ctlr_receive(cam_handle, &ctlr_trans, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive camera data: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t wic_cam_sensor_recv_img_buf(wic_cam_img_buf_t **img_buf, uint32_t timeout)
{
    if (img_buf == NULL) return ESP_ERR_INVALID_ARG;
    if (cam_img_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (xQueueReceive(cam_img_queue, img_buf, pdMS_TO_TICKS(timeout)) == pdTRUE) return ESP_OK;
    return ESP_ERR_TIMEOUT;
}

esp_err_t wic_cam_sensor_stop(void)
{
    esp_err_t ret = ESP_OK;
    wic_cam_img_buf_t *img_buf = NULL;
    if (cam_handle == NULL) return ESP_ERR_INVALID_STATE;

    ret = esp_cam_ctlr_stop(cam_handle);
    while (xQueueReceive(cam_img_queue, &img_buf, 0) == pdTRUE) {
        if (img_buf) wic_cam_sensor_free_img_buf(img_buf);
    }

    return ret;
}

void wic_cam_sensor_deinit(void)
{
    if (cam_handle) {
        esp_cam_ctlr_del(cam_handle);
        cam_handle = NULL;
    }
    if (i2c_bus_handle) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
    }
    if (cam_img_queue) {
        xQueueReset(cam_img_queue);
        vQueueDelete(cam_img_queue);
        cam_img_queue = NULL;
    }
    for (int i = 0; i < WIC_CAM_BUF_NUM; i++) {
        if (cam_img_buf[i].data) {
            free(cam_img_buf[i].data);
            cam_img_buf[i].data = NULL;
        }
    }
    if (isp_proc) {
        esp_isp_del_processor(isp_proc);
        isp_proc = NULL;
    }
}