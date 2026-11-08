# First installation and basic setup

This guide covers the first configuration of MODELROGS TEMPO TEST 12 **after the firmware has been successfully uploaded to the GeekMagic device**.

> **Important — development build:** TEST 12 has not yet been confirmed as compiled with the exact target board package and libraries, nor fully validated on hardware. Keep a copy of the firmware currently installed and a recovery method before testing. The `.cpp` file is source code; it is not, by itself, a ready-to-flash firmware binary.

## 1. Choose and upload the firmware

The repository provides two files:

- **Firmware binary:** [`MODELROGS-Tempo_12.0.bin`](../firmware/bin/MODELROGS-Tempo_12.0.bin) — the file intended for a compatible firmware-update or flashing tool.
- **Source code:** [`Modelrogs_Tempo_TEST12.cpp`](../firmware/Modelrogs_Tempo_TEST12.cpp) — source for developers; do not upload this `.cpp` file as firmware.

The exact upload procedure depends on the GeekMagic hardware revision and the supported flashing method. The configuration page also contains a **Firmware** section with a **MISE À JOUR FIRMWARE** button; use it only if the device is already running compatible TEMPO firmware and the binary is confirmed for that target.

> **Version check before installation:** the supplied binary is named `12.0`, while the source is identified as `TEST12`. The repository does not yet establish that the binary was built from this exact source revision or validate its compatibility with every GeekMagic hardware revision. Confirm both before installing. Keep a known-good firmware and recovery method available.

Once a compatible firmware has been uploaded and the device has restarted, continue with the Wi-Fi setup below.

## 2. Connect to the configuration access point

At startup, TEMPO creates a Wi-Fi access point with these settings:

| Setting | Value |
|---|---|
| Wi-Fi network name (SSID) | `MODELROGS Tempo` |
| Access-point password | None — the network is currently open |
| Configuration address | `http://192.168.4.1` |

1. Power on the GeekMagic device.
2. If it cannot connect to a previously saved Wi-Fi network, its screen displays `WIFI KO` and instructions to connect to `MODELROGS Tempo`.
3. On a phone or computer, open the Wi-Fi network list and connect to **MODELROGS Tempo**. No password is requested by the current TEST 12 source.
4. If a captive-portal page does not open automatically, open a browser and enter **http://192.168.4.1**.

The device's configuration access point is started at boot. The same local configuration page is available through that access point.

> **Security note:** the configuration access point is currently open (no Wi-Fi password). Use it only in a trusted environment. Access-point password configuration is not documented as an available setting in TEST 12.

## 3. Configure the home Wi-Fi network

On the MODELROGS TEMPO page, find the **Wi-Fi** section:

1. Enter the name of the home Wi-Fi network in **SSID**. Network names are case-sensitive.
2. Enter the corresponding Wi-Fi password.
3. Save the settings using **ENREGISTRER LES PARAMÈTRES**.

The device restarts when the Wi-Fi SSID or password has changed, then attempts to connect using the new credentials. After a successful connection, the screen displays `WIFI OK` and the device's local IP address.

If the password field is left empty, TEST 12 keeps the password already stored in the device. On a first installation, enter the password if the home network requires one.

If the connection fails, the device keeps the `WIFI KO` setup screen. Reconnect to the `MODELROGS Tempo` access point and revisit `http://192.168.4.1` to correct the settings.

## 4. Set the city for weather information

In the **Météo** section:

1. Enter the city name in **Ville**.
2. The default city on a fresh configuration is **Manosque**.
3. Save the settings.

The firmware uses the configured city for its weather lookup. An internet connection is required for online weather data.

## 5. Set screen brightness and night mode

In the **Affichage écran** section, the current TEST 12 page provides:

| Setting | Description | Default on a fresh configuration |
|---|---|---:|
| Luminosité normale | Screen brightness during normal hours | 100% |
| Activer le Mode Nuit | Enables or disables the scheduled night mode | Enabled |
| Luminosité nuit | Screen brightness during night mode | 20% |
| Début du Mode Nuit | Time when night brightness starts | 22:00 |
| Fin du Mode Nuit | Time when normal brightness resumes | 07:00 |

Brightness ranges from **0% to 100%**. A value of 0% switches the backlight off.

The night period can cross midnight (for example, 22:00 to 07:00). Automatic day/night switching requires the device to have a valid synchronized time.

After changing these display settings, click **ENREGISTRER LES PARAMÈTRES**. TEST 12 saves and applies brightness and night-mode changes immediately; these changes do not require a reboot.

## 6. Check the device status

The top of the configuration page displays available status information, including:

- Wi-Fi connection state, network name and device IP address;
- internet connectivity;
- time synchronization state;
- Tempo information for today and tomorrow, when available;
- last RTE update status;
- current brightness and night-mode schedule.

Internet access is required for online services. Time synchronization is required for time-dependent features such as the automatic night schedule.

## 7. Firmware update page

The configuration page includes a **Firmware** section with a **MISE À JOUR FIRMWARE** button. This opens the device's firmware-update interface. Use it only with a firmware binary built for the exact device and firmware target. Do not upload the `.cpp` source file to this interface.

## Troubleshooting

### The `MODELROGS Tempo` network is not visible

- Restart the GeekMagic device and wait for startup to complete.
- Refresh the Wi-Fi network list on the phone or computer.
- Confirm that the device is powered and running the TEMPO firmware.

### The configuration page does not open

- Confirm that the phone or computer is connected to `MODELROGS Tempo`.
- Enter `http://192.168.4.1` directly in the browser's address bar.
- Temporarily disable mobile data or automatic network switching if the phone abandons the access point because it has no internet connection.

### The device does not connect to the home Wi-Fi

- Check the SSID spelling and password (including uppercase/lowercase characters).
- Confirm that the Wi-Fi network is available where the device is installed.
- Reconnect to the `MODELROGS Tempo` access point and correct the settings.

### Brightness changes do not seem to take effect

- Check that the new values were saved.
- Check whether the current time falls inside the configured night period.
- Confirm that time synchronization is available; automatic day/night switching depends on valid time.

---

**Document status:** based on the MODELROGS TEMPO TEST 12 source currently in this repository. Hardware-specific flashing instructions remain to be validated and documented.
