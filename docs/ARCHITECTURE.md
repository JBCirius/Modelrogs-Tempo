# MODELROGS project architecture

## Product hierarchy

MODELROGS TEMPO is the parent project and the central firmware foundation. MODELROGS DISPLAY is a specialised derivative of TEMPO.

```text
MODELROGS
└── MODELROGS TEMPO  (main project / parent)
    └── MODELROGS DISPLAY  (specialised derivative)
```

This is a product and development relationship, not a GitHub-enforced repository hierarchy. The projects use separate repositories so they can have independent issues, releases, documentation and development cycles.

## Repository responsibilities

### modelrogs-tempo

- Main project presentation and documentation.
- Shared firmware foundation and common features.
- Development and test firmware versions.
- Overview of the MODELROGS project family.

### modelrogs-display

- DISPLAY-specific firmware and resources.
- DISPLAY-specific installation and configuration instructions.
- DISPLAY-specific releases and changelog.

When a change affects shared TEMPO behaviour, document it in the TEMPO repository and propagate it to DISPLAY where appropriate. Avoid copying a derivative's specific features into the parent firmware unless they are genuinely shared.

## Versioning guidance

Use clearly identifiable development labels for test builds (for example `TEST12`). Once a build has been compiled and validated on the target hardware, publish a stable semantic version (for example `v1.0.0`) and document the tested hardware and known limitations.
