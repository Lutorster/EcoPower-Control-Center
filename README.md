# EcoPower Control Center v3.0

Target: Waveshare/Sunton ESP32-S3 7-inch RGB LCD, 800x480, GT911, ESP-IDF 5.4.4.

## SD card
Copy the `assets` folder from the SD archive to the root of the FAT32 card.
The final path must be:

`/assets/dashboard.png`

The PNG is exactly 800x480. Runtime zoom is not used.

## Build
Open ESP-IDF 5.4.4 PowerShell and run:

```powershell
cd C:\ESPHome\EcoPower-Control-Center
idf.py set-target esp32s3
idf.py fullclean
idf.py build
idf.py flash monitor
```

## Expected startup log

- `EcoPower_Boot: Boot screen shown`
- `EcoPower_SD: File ready: /sdcard/assets/dashboard.png`
- `EcoPower_ASSETS: Loaded PNG to PSRAM`
- `EcoPower_Dashboard: Dashboard PNG attached`
- `EcoPower_Dashboard: Dashboard shown`

## Important hardware detail
CH422G controls touch reset, backlight and SD enable. The SD driver preserves the working CH422G state used by this board.
