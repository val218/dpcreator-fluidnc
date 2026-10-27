#include "Platform.h"
#include "Driver/fluidnc_uart.h"

#include "UartTypes.h"

#include "Capture.h"

#include <sstream>
#include <algorithm>
#include "HuanyangSimulator.h"
#include "NutsBolts.h"  // get_ms()

class InputPin;
void uart_register_input_pin(uint32_t uart_num, pinnum_t pinnum, InputPin* object) {}

VFDSimulator* vfd_simulator[MAX_N_UARTS];

// Host build: a UART can be a real or pseudo terminal. When the file
// native_uart<N> exists, it names the device (e.g. /dev/pts/7, written by a
// test's simulated Modbus slave), and an RS485 (half duplex) uartN talks to
// it instead of the built-in Huanyang VFD simulator.
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fstream>
struct UartFds {
    int fd[MAX_N_UARTS];
    UartFds() {
        for (auto& f : fd) {
            f = -1;
        }
    }
    int& operator[](uint32_t i) { return fd[i]; }
};
static UartFds uart_fd;

static int uart_open_device(uint32_t uart_num) {
    std::ifstream f("native_uart" + std::to_string(uart_num));
    std::string   path;
    if (!f || !std::getline(f, path) || path.empty()) {
        return -1;
    }
    int fd = open(path.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        return -1;
    }
    struct termios t;
    if (tcgetattr(fd, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(fd, TCSANOW, &t);
    }
    return fd;
}

inline std::string uart_key(uint32_t uart_num) {
    std::ostringstream key;
    key << "uart." << uart_num;

    return key.str();
}

void uart_discard_input(uint32_t uart_num) {
    if (uart_num < MAX_N_UARTS && uart_fd[uart_num] >= 0) {
        tcflush(uart_fd[uart_num], TCIFLUSH);
    }
}

void uart_init(uint32_t uart_num) {}

int uart_buflen(uint32_t uart_num) {
    if (uart_num < MAX_N_UARTS && uart_fd[uart_num] >= 0) {
        int n = 0;
        ioctl(uart_fd[uart_num], FIONREAD, &n);
        return n;
    }
    auto key = uart_key(uart_num);
    return int(Inputs::instance().size(key));
}

extern int inchar();

std::vector<uint8_t> vfd_output;
uint32_t             vfd_ms;

int uart_read(uint32_t uart_num, uint8_t* buf, uint32_t len, uint32_t timeout_ms) {
    if (uart_num < MAX_N_UARTS && uart_fd[uart_num] >= 0) {
        // like ESP-IDF's uart_read_bytes: until len bytes or the timeout
        uint32_t got = 0, start = get_ms();
        while (got < len) {
            int32_t left = int32_t(timeout_ms) - int32_t(get_ms() - start);
            if (left <= 0 && got) {
                break;
            }
            struct pollfd p = { uart_fd[uart_num], POLLIN, 0 };
            if (poll(&p, 1, left > 0 ? left : 0) <= 0) {
                break;
            }
            int r = read(uart_fd[uart_num], buf + got, len - got);
            if (r <= 0) {
                break;
            }
            got += r;
        }
        return int(got);
    }
    if (vfd_simulator[uart_num]) {
        if (vfd_output.size()) {
            auto copylen = std::min((size_t)len, vfd_output.size());
            std::copy(vfd_output.begin(), vfd_output.begin() + copylen, buf);
            vfd_output.erase(vfd_output.begin(), vfd_output.begin() + copylen);
            return copylen;
        }
        return 0;
    }
    auto        key = uart_key(uart_num);
    auto        count = Inputs::instance().read(key, buf, len);
    return int(count);
}

int uart_write(uint32_t uart_num, const uint8_t* buf, size_t len) {
    if (uart_num < MAX_N_UARTS && uart_fd[uart_num] >= 0) {
        return int(write(uart_fd[uart_num], buf, len));
    }
    if (vfd_simulator[uart_num]) {
        int32_t this_ms = get_ms();
        vfd_simulator[uart_num]->update((int32_t)this_ms - (int32_t)vfd_ms);
        vfd_ms     = this_ms;
        vfd_output = vfd_simulator[uart_num]->processModbusMessage(std::vector<uint8_t> { buf, buf + len });

        return 0;
    }

    auto key = uart_key(uart_num);
    Inputs::instance().append(key, buf, len);
    return int(len);
}

void uart_mode(uint32_t uart_num, uint32_t baud, UartData dataBits, UartParity parity, UartStop stopBits) {}

bool uart_half_duplex(uint32_t uart_num) {
    if (uart_num < MAX_N_UARTS && uart_fd[uart_num] < 0) {
        uart_fd[uart_num] = uart_open_device(uart_num);
    }
    if (uart_num < MAX_N_UARTS && uart_fd[uart_num] >= 0) {
        return false;  // a terminal (e.g. a simulated Modbus slave on a pty)
    }
    if (vfd_simulator[uart_num]) {
        delete vfd_simulator[uart_num];
        vfd_simulator[uart_num] = nullptr;
    }
    vfd_simulator[uart_num] = new VFDSimulator();
    vfd_ms                  = get_ms();

    return false;
}

void uart_xon(uint32_t uart_num) {}

void uart_xoff(uint32_t uart_num) {}

void uart_sw_flow_control(uint32_t uart_num, bool on, uint32_t xon_threshold, uint32_t xoff_threshold) {}

bool uart_pins(uint32_t uart_num, pinnum_t tx_pin, pinnum_t rx_pin, pinnum_t rts_pin, pinnum_t cts_pin) {
    return false;
}

int uart_bufavail(uint32_t uart_num) {
    return 128 - uart_buflen(uart_num);
}

bool uart_wait_output(uint32_t uart_num, uint32_t timeout_ms) {
    return true;
}
