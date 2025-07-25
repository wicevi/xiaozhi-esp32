#include "wifi_board.h"
#include "audio_codecs/es8311_audio_codec.h"
#include "application.h"
#include "display/lcd_display.h"
// #include "display/no_display.h"
#include "button.h"
#include "config.h"
#include "iot/thing_manager.h"

#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"

#if CONFIG_LCD_GC9A01_240X240
#include "esp_lcd_gc9a01.h"
#else
#include "esp_lcd_mipi_dsi.h"
#if CONFIG_LCD_TYPE_720_1280_7_INCH
#include "esp_lcd_ili9881c.h"
#elif (CONFIG_LCD_TYPE_800_1280_10_1_INCH || CONFIG_LCD_TYPE_800_1280_10_1_INCH_A)
#include "esp_lcd_jd9365_10_1.h"
#endif
#include "esp_lcd_touch_gt911.h"
#endif

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <esp_lvgl_port.h>
#include "home_ctrl.h"
#include "wic_camera.h"

#define TAG "WaveshareEsp32p4nano"

class CustomHomeCtrl : public HomeCtrl {
    public:
    CustomHomeCtrl() : HomeCtrl() {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << HOME_LAMP_CTRL_GPIO);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        ESP_ERROR_CHECK(gpio_set_level(HOME_LAMP_CTRL_GPIO, 0));
        lamp_state_ = false;
        ESP_LOGI(TAG, "HomeCtrl initialized with GPIO %d", HOME_LAMP_CTRL_GPIO);
    }
    void CtrlLamp(bool state) override {
        ESP_LOGI(TAG, "Setting lamp state to %d", state);
        gpio_set_level(HOME_LAMP_CTRL_GPIO, state ? 1 : 0);
        lamp_state_ = state;
    }
};

#if CONFIG_LCD_GC9A01_240X240
LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);
#else
LV_FONT_DECLARE(font_puhui_20_4);
LV_FONT_DECLARE(font_awesome_20_4);

class CustomBacklight : public Backlight {
public:
    CustomBacklight(i2c_master_bus_handle_t i2c_handle)
        : Backlight(), i2c_handle_(i2c_handle) {}

protected:
    i2c_master_bus_handle_t i2c_handle_;

    virtual void SetBrightnessImpl(uint8_t brightness) override {
        uint8_t i2c_address = 0x45;     // 7-bit address
#if CONFIG_LCD_TYPE_800_1280_10_1_INCH
        uint8_t reg = 0x86;
#elif (CONFIG_LCD_TYPE_800_1280_10_1_INCH_A || CONFIG_LCD_TYPE_720_1280_7_INCH)
        uint8_t reg = 0x96;
#endif
        uint8_t data[2] = {reg, brightness};

        i2c_master_dev_handle_t dev_handle;
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = i2c_address,
            .scl_speed_hz = 100000,
        };

        esp_err_t err = i2c_master_bus_add_device(i2c_handle_, &dev_cfg, &dev_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(err));
            return;
        }

        err = i2c_master_transmit(dev_handle, data, sizeof(data), -1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to transmit brightness: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Backlight brightness set to %u", brightness);
        }

        // i2c_master_bus_rm_device(dev_handle);
    }
};
#endif

class WaveshareEsp32p4nano : public WifiBoard {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    Button boot_button_;
    LcdDisplay *display_;
    CustomHomeCtrl *home_ctrl_;
    WicCamera *camera_;
#if !CONFIG_LCD_GC9A01_240X240
    CustomBacklight *backlight_;
#endif

#if CONFIG_LCD_GC9A01_240X240
    void InitializeSpi() {
        const spi_bus_config_t buscfg = GC9A01_PANEL_BUS_SPI_CONFIG(DISPLAY_CLK_PIN, DISPLAY_MOSI_PIN,
                                    DISPLAY_HEIGHT * 80 * LCD_BIT_PER_PIXEL / 8);
        ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }
#endif

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    static esp_err_t bsp_enable_dsi_phy_power(void) {
#if MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        // Turn on the power for MIPI DSI PHY, so it can go from "No Power" state to "Shutdown" state
        static esp_ldo_channel_handle_t phy_pwr_chan = NULL;
        esp_ldo_channel_config_t ldo_cfg = {
            .chan_id = MIPI_DSI_PHY_PWR_LDO_CHAN,
            .voltage_mv = MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
        };
        esp_ldo_acquire_channel(&ldo_cfg, &phy_pwr_chan);
        ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif // BSP_MIPI_DSI_PHY_PWR_LDO_CHAN > 0
        return ESP_OK;
    }

    void InitializeLCD() {
#if CONFIG_LCD_GC9A01_240X240
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        const esp_lcd_panel_io_spi_config_t io_config = GC9A01_PANEL_IO_SPI_CONFIG(DISPLAY_CS_PIN, DISPLAY_DC_PIN, NULL, NULL);
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_handle_t panel = nullptr;
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = DISPLAY_RST_PIN,
        #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
            .color_space = ESP_LCD_COLOR_SPACE_BGR,
        #else
            .rgb_endian = LCD_RGB_ENDIAN_BGR,
        #endif
            .bits_per_pixel = LCD_BIT_PER_PIXEL,
        };
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        esp_lcd_panel_disp_on_off(panel, true);
    #if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        ESP_ERROR_CHECK(esp_lcd_panel_disp_off(panel, false));
    #else
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    #endif
    display_ = new SpiLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                    {
                                        .text_font = &font_puhui_16_4,
                                        .icon_font = &font_awesome_16_4,
                                        .emoji_font = font_emoji_64_init(),
                                    });
#else
        bsp_enable_dsi_phy_power();
        esp_lcd_panel_io_handle_t io = NULL;
        esp_lcd_panel_handle_t disp_panel = NULL;
    
        esp_lcd_dsi_bus_handle_t mipi_dsi_bus = NULL;
    #if CONFIG_LCD_TYPE_720_1280_7_INCH
        esp_lcd_dsi_bus_config_t bus_config = ILI9881C_PANEL_BUS_DSI_2CH_CONFIG();
    #elif (CONFIG_LCD_TYPE_800_1280_10_1_INCH || CONFIG_LCD_TYPE_800_1280_10_1_INCH_A)
        esp_lcd_dsi_bus_config_t bus_config = JD9365_PANEL_BUS_DSI_2CH_CONFIG();
    #endif
        esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus);

        ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
        // we use DBI interface to send LCD commands and parameters
    #if CONFIG_LCD_TYPE_720_1280_7_INCH
        esp_lcd_dbi_io_config_t dbi_config = ILI9881C_PANEL_IO_DBI_CONFIG();
    #elif (CONFIG_LCD_TYPE_800_1280_10_1_INCH || CONFIG_LCD_TYPE_800_1280_10_1_INCH_A)
        esp_lcd_dbi_io_config_t dbi_config = JD9365_PANEL_IO_DBI_CONFIG();
    #endif
        esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io);

    #if CONFIG_LCD_TYPE_720_1280_7_INCH
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 80,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = 720,
                .v_size = 1280,
                .hsync_pulse_width = 50,
                .hsync_back_porch = 239,
                .hsync_front_porch = 33,
                .vsync_pulse_width = 30,
                .vsync_back_porch = 20,
                .vsync_front_porch = 2,
            },
            .flags = {
                .use_dma2d = true,
            },
        };
        ili9881c_vendor_config_t vendor_config = {
            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
        };
        const esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = -1,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_ili9881c(io, &panel_config, &disp_panel);
    #elif (CONFIG_LCD_TYPE_800_1280_10_1_INCH || CONFIG_LCD_TYPE_800_1280_10_1_INCH_A)
        esp_lcd_dpi_panel_config_t dpi_config = {
            .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
            .dpi_clock_freq_mhz = 80,
            .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
            .num_fbs = 1,
            .video_timing = {
                .h_size = 800,
                .v_size = 1280,
                .hsync_pulse_width = 20,
                .hsync_back_porch = 20,
                .hsync_front_porch = 40,
                .vsync_pulse_width = 10,
                .vsync_back_porch = 4,
                .vsync_front_porch = 30,
            },
            .flags = {
                .use_dma2d = true,
            },
        };


        jd9365_vendor_config_t vendor_config = {

            .mipi_config = {
                .dsi_bus = mipi_dsi_bus,
                .dpi_config = &dpi_config,
                .lane_num = 2,
            },
            .flags = {
                .use_mipi_interface = 1,
            },
        };

        const esp_lcd_panel_dev_config_t lcd_dev_config = {
            .reset_gpio_num = PIN_NUM_LCD_RST,
            .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
            .bits_per_pixel = 16,
            .vendor_config = &vendor_config,
        };
        esp_lcd_new_panel_jd9365(io, &lcd_dev_config, &disp_panel);
    #endif
        esp_lcd_panel_reset(disp_panel);
        esp_lcd_panel_init(disp_panel);

        display_ = new MipiLcdDisplay(io, disp_panel, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                                       DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY,
                                       {
                                           .text_font = &font_puhui_20_4,
                                           .icon_font = &font_awesome_20_4,
                                           .emoji_font = font_emoji_64_init(),
                                       });
        backlight_ = new CustomBacklight(codec_i2c_bus_);
        backlight_->RestoreBrightness();
#endif
    }

#if !CONFIG_LCD_GC9A01_240X240
    void InitializeTouch()
    {
        esp_lcd_touch_handle_t tp;
        esp_lcd_touch_config_t tp_cfg = {
            .x_max = DISPLAY_WIDTH,
            .y_max = DISPLAY_HEIGHT,
            .rst_gpio_num = GPIO_NUM_NC,
            .int_gpio_num = GPIO_NUM_NC,
            .levels = {
                .reset = 0,
                .interrupt = 0,
            },
            .flags = {
                .swap_xy = 0,
                .mirror_x = 0,
                .mirror_y = 0,
            },
        };
        esp_lcd_panel_io_handle_t tp_io_handle = NULL;
        esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
        tp_io_config.scl_speed_hz = 100 * 1000;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(codec_i2c_bus_, &tp_io_config, &tp_io_handle));
        ESP_LOGI(TAG, "Initialize touch controller");
        ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_gt911(tp_io_handle, &tp_cfg, &tp));
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lv_display_get_default(),
            .handle = tp,
        };
        lvgl_port_add_touch(&touch_cfg);
        ESP_LOGI(TAG, "Touch panel initialized successfully");
    }
#endif
    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState(); });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        auto &thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
    }

public:
    WaveshareEsp32p4nano() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeCodecI2c();
        InitializeIot();
        camera_ = new WicCamera(codec_i2c_bus_);
    #if CONFIG_LCD_GC9A01_240X240
        InitializeSpi();
    #endif
        InitializeLCD();
    #if !CONFIG_LCD_GC9A01_240X240
        InitializeTouch();
    #endif
        InitializeButtons();
        home_ctrl_ = new CustomHomeCtrl();
    }

    virtual AudioCodec *GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(codec_i2c_bus_, I2C_NUM_1, AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                                            AUDIO_I2S_GPIO_MCLK, AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN,
                                            AUDIO_CODEC_PA_PIN, AUDIO_CODEC_ES8311_ADDR);
        return &audio_codec;
    }

    virtual Display *GetDisplay() override {
        return display_;
    }

    virtual Backlight *GetBacklight() override {
    #if CONFIG_LCD_GC9A01_240X240
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    #else
        return backlight_;
    #endif
    }

    virtual HomeCtrl *GetHomeCtrl() override {
        return home_ctrl_;
    }

    virtual Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(WaveshareEsp32p4nano);
