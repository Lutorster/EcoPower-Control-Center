# EcoPower OS v3.0

Clean foundation based on the proven v2.3 hardware/SD/LVGL project.

## What changed

- Stable FatFS LFN configuration retained (`CONFIG_FATFS_LFN_HEAP=y`).
- Only LVGL fonts already enabled in sdkconfig are used: 12, 16 and 20 px.
- Dashboard logic is split into reusable widgets:
  - `ValueLabel`
  - `FlowDot`
- Central `EnergyData` model separates UI from future Deye and JK BMS drivers.
- Demo data source can be disabled when real RS485 data is connected.
- Public model API:
  - `ecopower_energy_model_set()`
  - `ecopower_energy_model_enable_demo(false)`
- SD asset path remains `/sdcard/assets/dashboard.png`.

## Build

```powershell
idf.py set-target esp32s3
idf.py fullclean
idf.py build
idf.py flash monitor
```

Expected log:

```text
EcoPower: EcoPower OS 3.0.0 starting
EcoPower_SD: File ready: /sdcard/assets/dashboard.png
EcoPower_Dashboard: EcoPower OS v3 widgets ready; demo=on
EcoPower: EcoPower OS 3.0.0 UI started
```

## Next integration step

The Deye Modbus/RS485 task will populate `EnergyData`, call
`ecopower_energy_model_set(&data)`, and disable the demo source.
