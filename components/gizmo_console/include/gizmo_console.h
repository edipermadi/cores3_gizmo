#pragma once

/**
 * @file
 * @brief Serial command console (linenoise REPL on the USB-Serial-JTAG port).
 *
 * Commands:
 *  - `screenshot`          active screen as zlib-compressed RGB565, base64 between
 *                          "SCREENSHOT BEGIN" / "SCREENSHOT END" lines
 *                          (decode with tools/gizmo.py or `make screenshot`)
 *  - `touch X Y [MS]`      simulated tap at X,Y held for MS milliseconds (default 100)
 *  - `swipe X1 Y1 X2 Y2 [MS]` simulated drag from X1,Y1 to X2,Y2 over MS ms (default 300)
 *  - `i2cscan`             list devices answering on the internal I2C bus
 *  - `help`                list commands
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Register commands and start the REPL task. Call after the UI is up. */
esp_err_t gizmo_console_start(void);

#ifdef __cplusplus
}
#endif
