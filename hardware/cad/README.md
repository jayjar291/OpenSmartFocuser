# CAD

This folder stores the mechanical design files for the OpenSmartFocuser assembly.

## Folder Structure

```text
cad/
├── 3mf/   # Printable model exports
├── step/  # Assembly and fit-check models
└── pcb mount.dxf  # 2D reference drawing for the PCB mount
```

## File Types

- `3mf/` contains printable parts for enclosure and accessory checks.
- `step/` contains solid models for CAD import, fit checks, and assembly review.
- `pcb mount.dxf` is the 2D drawing used for mount or cut reference work.

## Use Case

Use this folder when you need to:

- review the physical assembly before printing or machining.
- verify clearances between the drawtube, housing, and mounted electronics.
- export or update models for enclosure and mechanical integration.

When mechanical dimensions change, update the source CAD files first and then regenerate the derived exports.