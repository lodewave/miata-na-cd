# Mechanical — 3D-printed enclosure

Housing for the head-unit module.

```
mechanical/
├── stl/        Printable meshes (ready to slice)
└── source/     Editable CAD source (STEP, and the native format, e.g. .f3d / FreeCAD)
```

## Notes

- Commit **both** the STL and an editable source. STLs are meshes and are hard
  to change; STEP (or the native CAD file) is what you'd actually edit later.
- GitHub renders STL files in a 3D viewer in the browser.
- Typical housing files (a few MB) commit fine as-is. Only reach for Git LFS if
  a single file approaches ~50 MB (GitHub warns at 50 MB, blocks at 100 MB).
- Note the printer, material and any insert/screw sizes here once the design is
  added, so it can be reprinted.
