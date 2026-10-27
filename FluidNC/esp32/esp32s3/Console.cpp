#include <sdkconfig.h>
#include <esp_idf_version.h>

#if defined(CONFIG_TINYUSB_CDC_ENABLED) && defined(IDFBUILD)
#    include "USBCDCChannel_IDF.h"
#else
#    include "USBCDCChannel.h"
#endif

USBCDCChannel CDCChannel(true);
static bool   cdc_on = false;  // the USB console was started

#include "UartChannel.h"

#include "Settings.h"
#include "NutsBolts.h"  // delay_ms

// This derived class overrides init() to setup the primary UART
class UartConsole : public UartChannel {
public:
    UartConsole() : UartChannel(0, true) {}
    void init() override {
        auto uart0 = new Uart(uint32_t(0));
        uart0->begin(BAUD_RATE, UartData::Bits8, UartStop::Bits1, UartParity::None);
        UartChannel::init(uart0);

        // Init CDC from NVS web setting (before config load so boot
        // diagnostics are visible on boards without a USB-serial chip).
        // If USB host is later configured, UsbHostUart::begin() tears
        // down TinyUSB and takes over the USB port.
        static auto* cdc_enable = new EnumSetting("USB CDC Enable", WEBSET, WG, NULL, "USBCDC/Enable", true, &onoffOptions);
        if (cdc_enable->get()) {
            CDCChannel.init();
            cdc_on = true;
            // Give a USB CDC terminal emulator time to enumerate and connect
            // before any startup/boot messages are sent, so boards with only
            // a USB CDC serial port (no separate USB-serial chip) don't lose
            // their early log output to a terminal that hasn't opened yet.
            delay_ms(1000);
        }
    }
};
UartConsole Uart0;
Channel&    Console = Uart0;

#include "Machine/MachineConfig.h"
#include "Logging.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// config.yaml has a uart0: section: a uart_channel (an I/O expander on J7)
// gets UART0 and the console stays on USB CDC only. Called once config.yaml
// is read, so the boot log up to here still went out on UART0.
bool console_release_uart0() {
    if (!cdc_on) {
        log_error("uart0: the USB console is off ($USBCDC/Enable=ON first), so UART0 stays the console");
        return false;
    }
    std::string user = "a uart_channel";
    for (size_t i = 1; i < MAX_N_UARTS; i++) {
        if (config->_uart_channels[i] && config->_uart_channels[i]->uart_num() == 0) {
            user = config->_uart_channels[i]->name();
        }
    }
    log_info("UART0 used by " << user << ", console on USB only");
    Uart0.release();
    // ESP-IDF's own log (Wi-Fi, drivers) would go out on UART0 into the
    // expander's data: off. (A crash dump still goes to UART0: the ROM does that.)
    esp_log_level_set("*", ESP_LOG_NONE);
    // A read or write that started before release() finishes inside the driver
    // (reads wait at most a few ms): give it time before the driver goes.
    vTaskDelay(pdMS_TO_TICKS(50));
    uart_driver_delete(UART_NUM_0);  // uart0: begin() installs it again with its own pins and baud
    return true;
}
