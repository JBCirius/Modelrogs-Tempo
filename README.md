<div align="center">

# MODELROGS

### A family of firmware projects for smart displays

**MODELROGS is the umbrella project bringing together independent firmware experiences for compatible display devices.**

</div>

---

## About MODELROGS

MODELROGS is the name of the project family. It brings together different firmware projects designed to make compact smart displays useful, configurable and easy to consult.

**MODELROGS TEMPO is the first firmware project in the family.** It focuses on displaying information related to the EDF Tempo electricity offer, alongside useful everyday information.

**MODELROGS DISPLAY is another project in the same family**, with its own purpose, firmware, documentation and release cycle. It is not a child or a derivative of TEMPO. TEMPO and DISPLAY are two projects at the same level under the MODELROGS name.

## The MODELROGS projects

```text
MODELROGS  ·  Project family / umbrella name
│
├── MODELROGS TEMPO    ·  First firmware project
│
└── MODELROGS DISPLAY  ·  Separate firmware project
```

| Project | Description | Repository |
|---|---|---|
| **MODELROGS TEMPO** | Firmware for a GeekMagic display, focused on EDF Tempo information, with additional display features. | This repository |
| **MODELROGS DISPLAY** | A separate firmware project in the MODELROGS family. | [Sqweedly/modelrogs-display](https://github.com/Sqweedly/modelrogs-display) |

The projects share the MODELROGS identity, but each can evolve independently. Neither project is the parent of the other.

## MODELROGS TEMPO

MODELROGS TEMPO is the first project released under the MODELROGS name. It is developed for compatible GeekMagic display hardware and aims to show key electricity and everyday information at a glance.

Depending on the firmware version, the display can show:

- The EDF Tempo colour for today and tomorrow.
- Relevant electricity tariff information.
- Weather and temperatures.
- Time, date and configured location.

The exact features depend on the firmware version and target hardware. Consult the version-specific documentation before installation.

## Firmware files

The `firmware/` directory contains the source code and the supplied flashable binary.

### TEST 12 — development build

- Source: [`Modelrogs_Tempo_TEST12.cpp`](firmware/Modelrogs_Tempo_TEST12.cpp)
- Binary: [`MODELROGS-Tempo_12.0.bin`](firmware/bin/MODELROGS-Tempo_12.0.bin)

The TEST 12 source includes these changes:

1. **Wi-Fi setup screen:** when the device cannot connect to Wi-Fi, the `WIFI KO` screen remains visible and guides the user to connect to the `MODELROGS Tempo` configuration access point.
2. **Display settings without reboot:** brightness and day/night settings are saved and applied immediately. A restart is only requested when Wi-Fi credentials are changed.

See [TEST 12 notes](docs/FIRMWARE-TEST12.md) and the [first installation and basic setup guide](docs/INSTALLATION.md).

> **Version correspondence:** the supplied binary is named `12.0`, while the source is identified as `TEST12`. Confirm that this binary was built from this exact source revision and for the intended GeekMagic hardware before treating them as a matched release.

> **Development status:** TEST 12 has not yet been confirmed as successfully compiled with the exact board package and libraries, nor fully validated on target hardware. Do not treat it as a stable release.

## Documentation

| Document | Description |
|---|---|
| [First installation and basic setup](docs/INSTALLATION.md) | Wi-Fi setup, city, brightness, night mode and troubleshooting after firmware upload. |
| [TEST 12 firmware notes](docs/FIRMWARE-TEST12.md) | Changes, validation status and suggested hardware tests. |
| [MODELROGS project family](docs/ARCHITECTURE.md) | Explains the MODELROGS umbrella and the equal status of TEMPO and DISPLAY. |
| [Changelog](CHANGELOG.md) | Repository change history. |

## Contributing and feedback

Feedback, test results and improvement ideas are welcome. When reporting an issue, include the firmware version or test identifier, the GeekMagic device model and hardware revision if known, steps to reproduce the issue, and relevant logs or screenshots.

## License and reuse

No open-source license has been assigned to this repository yet. Until an explicit license is added, do not assume that the source may be reused, modified or redistributed. All rights remain with the respective rights holder.

---

**MODELROGS** · One project family, multiple independent firmware projects.
