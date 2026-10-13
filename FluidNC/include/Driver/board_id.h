#pragma once

// Factory-programmed chip ID (base MAC from eFuse) as 12 upper-case hex
// digits. Stable across reflashes; used by $Board/ID to identify the board,
// for example when a dpCREATOR WebUI licence is installed on it.
const char* board_id();
