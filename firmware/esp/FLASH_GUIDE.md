# ESP32-S3 Build & Flash Guide

## Prerequisites (one-time setup)

1. **Install ESP-IDF** (already done if you've flashed before):
   ```
   C:\esp\v5.5.3\esp-idf\install.bat esp32s3
   ```

2. **USB driver**: Make sure your ESP32-S3 shows up in Device Manager under **Ports (COM & LPT)** when plugged in. If not:
   - Use a **data-capable** USB cable (not charge-only)
   - Install the CP210x or CH340 driver if you see an unrecognized device

## Every-time steps

### 1. Open a Command Prompt and activate ESP-IDF

```
C:\esp\v5.5.3\esp-idf\export.bat
```

You should see "Done! You can now compile ESP-IDF projects."

### 2. Navigate to the firmware folder

```
cd C:\2026_spring\ece1896\senior_design_repo\firmware\esp
```

### 3. Set the target chip (only needed once, or after a full clean)

```
idf.py set-target esp32s3
```

> Skip this if you've already set the target and haven't deleted the `build/` folder.

### 4. Build

```
idf.py build
```

First build takes several minutes. Subsequent builds are much faster (only recompiles changed files).

### 5. Plug in the ESP32-S3

- Connect via USB to your PC
- Check the COM port in **Device Manager > Ports (COM & LPT)** (e.g., COM5)

### 6. Flash

```
idf.py -p COM5 flash
```

Replace `COM5` with your actual port.

### 7. Monitor serial output

```
idf.py -p COM5 monitor
```

You should see:
```
=== Obstacle Belt BLE Test Firmware ===
BLE initialized. Open the app and tap Connect.
Advertising started - waiting for phone to connect...
```

Press **Ctrl+]** to exit the monitor.

### Shortcut: build + flash + monitor in one command

```
idf.py -p COM5 flash monitor
```

## Troubleshooting

| Problem | Fix |
|---------|-----|
| No COM port in Device Manager | Try a different USB cable (must be data-capable, not charge-only) |
| `export.bat` says Python env not found | Run `C:\esp\v5.5.3\esp-idf\install.bat esp32s3` again |
| Build error | Read the error message — usually a code issue in `main/main.c` |
| Phone app can't find belt | Make sure you're running the **dev build**, not Expo Go. BLE doesn't work in Expo Go |
| Flash fails | Make sure no other program (serial monitor, etc.) is using the COM port |

## File overview

```
firmware/esp/
  CMakeLists.txt          - Top-level project file (rarely changes)
  sdkconfig.defaults      - Default config (enables NimBLE BLE stack)
  sdkconfig               - Auto-generated full config (gitignored)
  main/
    CMakeLists.txt         - Registers source files (update when adding new .c files)
    main.c                 - All firmware code lives here for now
  build/                   - Compiled output (gitignored, ~1500 files)
```
