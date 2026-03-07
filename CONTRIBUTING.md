# Contributing to OpenSmartFocuser

Thank you for your interest in contributing!

## Areas of Contribution
| Area | Toolchain | Notes |
|------|-----------|-------|
| PCB Design | KiCad | Export Gerbers to `hardware/pcb/fabrication/` on changes |
| CAD / Mechanical | Fusion 360 | Export STEP → `cad/step/`, 3MF → `cad/3mf/` |
| Firmware | PlatformIO / VSCode | Copilot used for inline documentation |
| INDI Driver | C++ / CMake | Follow INDI driver coding standards |
| ASCOM Driver | C# / .NET | Follow ASCOM driver template conventions |
| Documentation | Markdown | Written with Claude AI assistance |

## Branch Strategy
- `main` — stable releases only
- `develop` — active development
- `feature/your-feature` — feature branches off `develop`

## Pull Request Checklist
- [ ] Firmware builds cleanly via `pio run`
- [ ] STEP and 3MF exports updated if CAD changed
- [ ] Fabrication outputs regenerated if PCB changed
- [ ] Documentation updated to reflect changes

## Code Documentation
Inline code documentation is maintained with GitHub Copilot. Ensure functions and classes have Doxygen-style comments.
