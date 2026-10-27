// Host build: no internal/PSRAM split on a PC - the RAM guard never holds anything back.
#include "Driver/ram_guard.h"
void        ram_guard_init() {}
void        ram_guard_poll() {}
bool        ram_tight() { return false; }
RamInfo     ram_info() { return RamInfo { 64u << 20, 16u << 20, 64u << 20, 64u << 20, 16u << 20, 0, 0, 0, true }; }
uint32_t    psram_total_kb() { return 8192; }
const char* psram_problem() { return ""; }
std::string ram_report() { return "host build"; }
std::string ram_crash_report() { return ""; }
