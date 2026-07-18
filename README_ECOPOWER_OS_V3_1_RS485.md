# EcoPower OS v3.1 — Deye RS485 physical-layer diagnostics

This release adds the first real communications layer for the onboard RS485 port of the Waveshare ESP32-S3-Touch-LCD-7.

## Fixed hardware mapping

- UART1 TX: GPIO15
- UART1 RX: GPIO16
- 9600 baud, 8 data bits, no parity, 1 stop bit
- No DE/RE GPIO is used because the board switches RS485 transmit/receive direction automatically.

## What this release does

- Initializes the onboard RS485 UART.
- Sends a read-only Modbus RTU request every 2 seconds.
- Logs complete TX and RX frames in hexadecimal.
- Checks slave address, function, byte count and CRC16.
- Reports timeouts and Modbus exception responses.
- Keeps the dashboard demo active until the correct Deye register map is confirmed.

Default diagnostic request:

- Slave: 1
- Function: 0x03 (Read Holding Registers)
- Start register: 0
- Register count: 1

These values are intentionally configurable in `idf.py menuconfig` under:

`EcoPower OS -> Deye RS485 diagnostics`

## Expected monitor output

With no inverter connected:

```
EcoPower_Deye: RS485 UART initialized successfully
EcoPower_Deye: RS485 diagnostic started: UART1 TX=GPIO15 RX=GPIO16, 9600 8N1, slave=1
EcoPower_Deye: TX [8]: 01 03 00 00 00 01 84 0A
EcoPower_Deye: No Modbus response ...
```

With a valid Modbus response, an RX frame and decoded register value are printed.

## Wiring

- Waveshare A -> Deye RS485 A / 485+
- Waveshare B -> Deye RS485 B / 485-
- Waveshare GND -> Deye communication GND

If there is no response, swap A and B once. Only use the Deye BMS/Modbus communication port documented for external monitoring; do not connect to CAN.
