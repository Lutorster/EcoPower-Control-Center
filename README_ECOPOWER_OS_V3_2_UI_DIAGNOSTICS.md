# EcoPower OS v3.2 — UI stability and inverter diagnostics

Changes:

- Dashboard PNG is decoded only once at startup and stored as RGB565 in PSRAM.
- LVGL no longer decompresses the 800x480 PNG on every invalidated area.
- Dashboard animation interval reduced to 5 Hz and glow shadows removed.
- Dynamic value plates enlarged to prevent overlap with baked-in image values.
- Clock continues to advance from the application timer.
- Added INVERTER diagnostics page with three columns:
  - TX / sent Modbus frame
  - RX / received Modbus frame
  - decoded status and register values
- Deye diagnostic task moved to CPU0; LVGL remains isolated on CPU1.

Open the diagnostics page by touching the INVERTER tab. Use the DASHBOARD button to return.
