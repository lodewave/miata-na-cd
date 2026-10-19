# Hardware

KiCad **schematic** for the Miata NA head-unit module. This folder holds the
schematic only — the PCB layout and fabrication (Gerbers, BOM, assembly) are
done elsewhere and are not kept here.

```
hardware/
├── kicad/      KiCad schematic project + project-local symbol library
└── pdf/        Schematic exported to PDF (viewable on GitHub without KiCad)
```

## Working with the KiCad files

- Designed in **KiCad** (note the version here once confirmed).
- Tracked source: `NACD.kicad_pro`, the `*.kicad_sch` sheets, the `sym-lib-table`,
  and the project-local symbol library in `libs/lcsc/symbols/`.
- Opening the project in KiCad with no `.kicad_pcb` is fine — the PCB editor
  just starts from an empty board. No footprint library (`fp-lib-table`,
  `*.pretty/`) is kept here, since footprints are only needed for layout.
- Backups, auto-saves, lock files and `fp-info-cache` are ignored by the
  top-level `.gitignore`.
- Export a schematic PDF to `pdf/` when the schematic changes, so it's readable
  here without opening KiCad.

### Portability note

`sym-lib-table` uses the `${KIPRJMOD}` variable for its path, not an absolute
home-directory path, so the project opens correctly when cloned on another
machine.

## Pin assignments

The authoritative pin map lives in the firmware
([`firmware/primary_esp32s3/board.h`](../firmware/primary_esp32s3/board.h)) and
the wiring tables in [`firmware/README.md`](../firmware/README.md). Keep the
schematic and `board.h` in step.
