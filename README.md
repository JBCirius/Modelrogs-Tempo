<div align="center">

# MODELROGS TEMPO

### The foundation of the MODELROGS display ecosystem

**One firmware foundation. Multiple ways to bring useful information to your screen.**

[Main project](#the-project) · [Firmware](#firmware) · [Project family](#the-modelrogs-family) · [Documentation](#documentation) · [Roadmap](#roadmap)

</div>

\---

## The project

**MODELROGS TEMPO** is the first project of the MODELROGS firmware ecosystem. It is being developed for compatible GeekMagic display hardware, with the goal of turning a compact screen into a configurable information display.

TEMPO is the shared foundation: firmware, common behaviour, configuration and project documentation are organised here. More specialised experiences can build on that foundation while keeping their own identity and release cycle.

> \*\*Project status: development.\*\* The source code currently published in this repository includes a TEST 12 build. It is not presented as a validated production release.

## What TEMPO is designed to provide

* **A configurable display experience** for compatible GeekMagic hardware.
* **Network configuration** through the device's Wi-Fi access point and configuration page.
* **Display controls**, including brightness and day/night behaviour.
* **A shared foundation** from which specialised MODELROGS projects can be developed.

The exact features available depend on the firmware version and the target hardware. Refer to the version-specific documentation before installing a build.

## The MODELROGS family

```text
MODELROGS
└── MODELROGS TEMPO  ·  firmware foundation
    MODELROGS DISPLAY  ·  Specialised derivative
```

|Project|Position|Purpose|
|-|-|-|
|**MODELROGS TEMPO**|Open|Shared firmware foundation, common features and overall project documentation.|
|**MODELROGS DISPLAY**|Open|A specialised project developed from TEMPO, with its own firmware and documentation.|

➡️ **Explore** [**MODELROGS DISPLAY**](https://github.com/Sqweedly/modelrogs-display)

The projects are kept in separate Git repositories. This makes it possible to evolve DISPLAY independently while keeping TEMPO as the central reference for the family.

## Firmware

The `firmware/` directory contains the source code and the supplied flashable firmware binary.

### TEST 12 — development build

The current source file is [`Modelrogs\_Tempo\_TEST12.cpp`](firmware/Modelrogs_Tempo_TEST12.cpp).  
A firmware binary is also provided: [`MODELROGS-Tempo\_12.0.bin`](firmware/bin/MODELROGS-Tempo_12.0.bin).

This test version includes two notable changes:

1. **Wi-Fi setup screen:** when the device cannot connect to Wi-Fi, the `WIFI KO` screen remains visible and guides the user to connect to the `MODELROGS Tempo` configuration access point.
2. **Display settings without reboot:** brightness and day/night settings are saved and applied immediately. A restart is only requested when Wi-Fi credentials are changed.

See [TEST 12 details and test procedure](docs/FIRMWARE-TEST12.md) and the [first installation guide](docs/INSTALLATION.md).

> \*\*Version correspondence:\*\* the supplied binary is named `12.0`, while the source is identified as `TEST12`. Confirm that this binary was built from this exact source revision and for the intended GeekMagic hardware before treating them as a matched release.

### Before installing

* Back up the firmware currently installed on the device.
* Keep a known-good firmware binary available for recovery.
* Confirm that the source matches your board and development environment.
* Compile and test on the target hardware before relying on the build.

**Validation note:** TEST 12 has been structurally edited, but a successful compile with the exact board package and libraries, followed by hardware validation, has not yet been confirmed. Do not treat it as a stable release.

## Documentation

|Document|Description|
|-|-|
|[Project architecture](docs/ARCHITECTURE.md)|Explains the TEMPO → DISPLAY relationship and repository responsibilities.|
|[First installation and basic setup](docs/INSTALLATION.md)|Wi-Fi setup, city, brightness, night mode and troubleshooting after firmware upload.|
|[TEST 12 firmware notes](docs/FIRMWARE-TEST12.md)|Describes the changes, validation status and suggested hardware tests.|
|[MODELROGS DISPLAY](projects/MODELROGS-DISPLAY.md)|Introduces the derivative project and links to its repository.|
|[Changelog](CHANGELOG.md)|Tracks repository changes.|

## Roadmap

The following items describe the direction of the project; they are not promises that a feature is already available.

* \[ ] Compile TEST 12 with the intended board package and library versions.
* \[ ] Complete the hardware test plan and document the tested device revision.
* \[ ] Consolidate installation and recovery instructions.
* \[ ] Establish a stable release and versioning process after validation.
* \[ ] Document which common TEMPO components are inherited by MODELROGS DISPLAY.

## Contributing and feedback

Bug reports, test results and improvement ideas are welcome. When reporting an issue, include:

* The firmware version or test identifier.
* The GeekMagic device model and hardware revision, if known.
* The steps required to reproduce the issue.
* Relevant serial logs, screenshots or observed behaviour.

Please identify clearly whether a report concerns TEMPO's shared behaviour or a DISPLAY-specific feature.

## License and reuse

No open-source license has been assigned to this repository yet. Until an explicit license is added, do not assume that the source may be reused, modified or redistributed. All rights remain with the respective rights holder.

\---

<div align="center">

**MODELROGS TEMPO**  
*The starting point for the MODELROGS display family.*

</div>

