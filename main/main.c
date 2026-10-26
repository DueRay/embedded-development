#include <stdio.h>

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define POT_GPIO          6
#define ADC_ATTEN         ADC_ATTEN_DB_12
#define ADC_BITWIDTH      ADC_BITWIDTH_12
#define ADC_RAW_MAX       4095
#define VREF_MV           3300
#define READ_PERIOD_MS    100

void app_main(void)
{
    adc_unit_t unit;
    adc_channel_t channel;
    ESP_ERROR_CHECK(adc_oneshot_io_to_channel(POT_GPIO, &unit, &channel));

    adc_oneshot_unit_handle_t adc;
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc, channel, &chan_cfg));

    adc_cali_handle_t cali;
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = channel,
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &cali));

    while (1) {
        int raw = 0;
        int u_cali = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc, channel, &raw));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali, raw, &u_cali));

        float u_manual = (float)raw * VREF_MV / ADC_RAW_MAX;

        printf(">raw:%d\n", raw);
        printf(">u_manual:%.1f§mV\n", u_manual);
        printf(">u_cali:%d§mV\n", u_cali);
        if (u_cali > 0) {
            printf(">error:%.2f§%%\n", (u_manual - u_cali) / u_cali * 100.0f);
        }

        vTaskDelay(pdMS_TO_TICKS(READ_PERIOD_MS));
    }
}
