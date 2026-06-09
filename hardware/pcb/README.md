# PCB

This folder contains the KiCad project and related fabrication assets for the OpenSmartFocuser controller board.

## Folder Structure

```text
pcb/
├── OpenSmartFocuser/  # KiCad project source files
├── fabrication/       # Fabrication export guidance and output files
```

## Contents

- `OpenSmartFocuser/` contains the schematic, board layout, symbols, footprints, and project metadata.
- `fabrication/` is reserved for Gerber, drill, BOM, and pick-and-place outputs.

## Use Case

Use this folder when you need to:

- edit the schematic or PCB layout in KiCad.
- review net names, footprints, and placement.
- generate manufacturer-ready fabrication files.

Keep fabrication exports separate from the source project so the design files remain easy to review and version.