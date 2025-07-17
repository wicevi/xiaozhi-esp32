#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_GPIO_MCLK         GPIO_NUM_13
#define AUDIO_I2S_GPIO_WS           GPIO_NUM_10
#define AUDIO_I2S_GPIO_BCLK         GPIO_NUM_12
#define AUDIO_I2S_GPIO_DIN          GPIO_NUM_11
#define AUDIO_I2S_GPIO_DOUT         GPIO_NUM_9

#define AUDIO_CODEC_PA_PIN          GPIO_NUM_53
#define AUDIO_CODEC_I2C_SDA_PIN     GPIO_NUM_7
#define AUDIO_CODEC_I2C_SCL_PIN     GPIO_NUM_8
#define AUDIO_CODEC_ES8311_ADDR     ES8311_CODEC_DEFAULT_ADDR

#define BOOT_BUTTON_GPIO            GPIO_NUM_35

#define LCD_BIT_PER_PIXEL           (16)
#ifdef CONFIG_LCD_GC9A01_240X240
#define DISPLAY_SPI_HOST            SPI2_HOST
#define DISPLAY_BACKLIGHT_PIN       GPIO_NUM_NC
#define DISPLAY_MOSI_PIN            GPIO_NUM_4
#define DISPLAY_CLK_PIN             GPIO_NUM_5
#define DISPLAY_DC_PIN              GPIO_NUM_21
#define DISPLAY_RST_PIN             GPIO_NUM_20
#define DISPLAY_CS_PIN              GPIO_NUM_22
#define DISPLAY_WIDTH           240
#define DISPLAY_HEIGHT          240
#define DISPLAY_MIRROR_X        true
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER       LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT     false
#define DISPLAY_SPI_MODE        0

#else

#if CONFIG_LCD_TYPE_720_1280_7_INCH
    #define DISPLAY_WIDTH   720
    #define DISPLAY_HEIGHT  1280
#elif (CONFIG_LCD_TYPE_800_1280_10_1_INCH || CONFIG_LCD_TYPE_800_1280_10_1_INCH_A)
    #define DISPLAY_WIDTH   800
    #define DISPLAY_HEIGHT  1280
#endif

#define PIN_NUM_LCD_RST             GPIO_NUM_NC

#define DELAY_TIME_MS                   (3000)
#define LCD_MIPI_DSI_LANE_NUM           (2)    // 2 data lanes

#define MIPI_DSI_PHY_PWR_LDO_CHAN       (3)
#define MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV (2500)

#define DISPLAY_SWAP_XY     false
#define DISPLAY_MIRROR_X    false
#define DISPLAY_MIRROR_Y    false

#define DISPLAY_OFFSET_X    0
#define DISPLAY_OFFSET_Y    0

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

#endif

#endif // _BOARD_CONFIG_H_
