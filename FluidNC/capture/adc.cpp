// Host build: an analog input reads the number (0..4095) in native_gpio/adc<pin>,
// 0 when the file is missing.
#include "Driver/fluidnc_adc.h"

#include <fstream>
#include <string>

int adc_read_raw(pinnum_t gpio, int samples) {
    std::ifstream f("native_gpio/adc" + std::to_string(int(gpio)));
    int           v = 0;
    if (f >> v) {
        return v < 0 ? 0 : v > 4095 ? 4095 : v;
    }
    return 0;
}
