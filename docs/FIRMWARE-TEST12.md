# MODELROGS TEMPO – TEST 12

## File

`firmware/Modelrogs_Tempo_TEST12.cpp`

## Changes in this test version

### Wi-Fi setup screen

When the device cannot connect to Wi-Fi, the firmware keeps the `WIFI KO` screen visible instead of allowing the normal status display to overwrite it. The screen provides guidance for connecting to the `MODELROGS Tempo` configuration access point and opening the configuration page.

### Display settings without reboot

Brightness and day/night settings are saved and applied immediately. Changing these display settings alone should not restart the device.

### Wi-Fi credential changes

A restart is retained when Wi-Fi credentials are changed, so the device can reconnect using the new network settings.

## Validation status

This source file was structurally edited, but a successful compile against the exact board package and libraries has not been confirmed here. Treat it as a test build, not a production release.

## Suggested hardware test plan

1. Back up the currently installed firmware.
2. Boot with no Wi-Fi credentials and confirm that `WIFI KO` remains visible.
3. Connect to the device's configuration access point and open its configuration page.
4. Enter valid Wi-Fi credentials, save, and confirm that the device restarts and connects.
5. Change normal brightness and confirm that the change takes effect without a reboot.
6. Change night brightness and the day/night schedule and confirm that the changes take effect without a reboot.
7. Confirm that the scheduled day/night transition still works after saving.
8. Record the board revision, build environment, compile result and test result before promoting this version.

## Recovery

Keep a known-good firmware binary and the original source available before flashing a test build. If the test firmware does not boot or connect, restore the known-good firmware using the device's supported recovery/flashing procedure.
