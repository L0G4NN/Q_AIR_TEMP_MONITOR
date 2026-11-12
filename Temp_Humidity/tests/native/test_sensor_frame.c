#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../../main/sensors/sensor_dht11.c"

static void check_frame(unsigned integer, unsigned decimal, float expected, esp_err_t result)
{
    uint8_t data[] = {53, 0, integer, decimal, 0};
    data[4] = (uint8_t)(data[0] + data[1] + data[2] + data[3]);
    dht11_reading_t reading = {123, 456, 789};
    assert(decode_frame(data, &reading) == result);
    if (result == ESP_OK) {
        assert(fabsf(reading.temperature_c - expected) < 0.001f);
        assert(fabsf(reading.temperature_f - (expected * 1.8f + 32)) < 0.001f);
        assert(reading.humidity_percent == 53);
    } else {
        assert(reading.temperature_c == 123 && reading.temperature_f == 456 && reading.humidity_percent == 789);
    }
}

int main(void)
{
    check_frame(24, 4, 24.4f, ESP_OK);
    check_frame(10, 0x81, -10.1f, ESP_OK); /* Aosong V1.3 example */
    check_frame(20, 0x80, -20.0f, ESP_OK);
    check_frame(60, 0, 60.0f, ESP_OK);
    check_frame(0, 0x81, -0.1f, ESP_OK);
    check_frame(20, 0x81, 0, ESP_ERR_INVALID_RESPONSE);
    check_frame(60, 9, 0, ESP_ERR_INVALID_RESPONSE);
    check_frame(24, 10, 0, ESP_ERR_INVALID_RESPONSE);
    check_frame(24, 0xff, 0, ESP_ERR_INVALID_RESPONSE);

    dht11_reading_t reading = {0};
    uint8_t invalid_crc[] = {53, 0, 24, 4, 0};
    assert(decode_frame(invalid_crc, &reading) == ESP_ERR_INVALID_CRC);
    uint8_t invalid_humidity[] = {100, 1, 24, 4, 129};
    assert(decode_frame(invalid_humidity, &reading) == ESP_ERR_INVALID_RESPONSE);
    uint8_t valid_humidity[] = {100, 0, 24, 4, 128};
    assert(decode_frame(valid_humidity, &reading) == ESP_OK && reading.humidity_percent == 100);
    puts("PASS: DHT11 signed values, exact range limits, invalid fractions, humidity and checksum");
    return 0;
}
