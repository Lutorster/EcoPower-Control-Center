#pragma once

/* Waveshare ESP32-S3-Touch-LCD-7 onboard RS485 pins. */
#define ECOPOWER_RS485_UART_PORT       1
#define ECOPOWER_RS485_TX_GPIO         16
#define ECOPOWER_RS485_RX_GPIO         15

/* Initial Deye communication defaults. */
#define ECOPOWER_DEYE_BAUD_RATE        9600
#define ECOPOWER_DEYE_SLAVE_ADDRESS    1
#define ECOPOWER_DEYE_TIMEOUT_MS       500


/*
 * Initial read-only register scan range.
 * Change only after reviewing the monitor output.
 */
#define ECOPOWER_DEYE_SCAN_START              0U
#define ECOPOWER_DEYE_SCAN_END              255U
#define ECOPOWER_DEYE_SCAN_BLOCK_SIZE         8U
#define ECOPOWER_DEYE_SCAN_PAUSE_MS         250U
#define ECOPOWER_DEYE_SCAN_MAX_FAILURES       5U
