# Documentation Guide

This directory contains two kinds of documents:

- long-lived project documentation
- process documentation for recent design and implementation work

## Start Here

If you are new to the repository, read in this order:

1. [README.md](/E:/project/cpp/minic/README.md)
2. [project-status-overview.md](/E:/project/cpp/minic/docs/project-status-overview.md)
3. [pe-coff-linker-support.md](/E:/project/cpp/minic/docs/pe-coff-linker-support.md)
4. [minic-relocation-matrix.md](/E:/project/cpp/minic/docs/minic-relocation-matrix.md)

## Long-Lived Documentation

These files describe the project as it exists today and should stay useful over time.

- [README.md](/E:/project/cpp/minic/README.md)
  Main project entry point: build, run, test, supported subset.
- [project-status-overview.md](/E:/project/cpp/minic/docs/project-status-overview.md)
  Current overall maturity, strongest areas, weakest areas, next likely focus.
- [pe-coff-linker-support.md](/E:/project/cpp/minic/docs/pe-coff-linker-support.md)
  Current PE/COFF linker model, boundaries, trace output, and regression entry points.
- [minic-relocation-matrix.md](/E:/project/cpp/minic/docs/minic-relocation-matrix.md)
  Which relocation shapes the current `minic` pipeline can generate today, what is already supported, and which future C features will force new relocation work.

## Process Documentation

These files capture how recent work was designed, planned, or tracked. They are useful for development history and rationale, but they are not the best first stop for understanding the current project state.

### Specs

- [2026-04-27-minic-types-multifile-linker-design.md](/E:/project/cpp/minic/docs/superpowers/specs/2026-04-27-minic-types-multifile-linker-design.md)
- [2026-04-28-pe-coff-linker-teaching-design.md](/E:/project/cpp/minic/docs/superpowers/specs/2026-04-28-pe-coff-linker-teaching-design.md)

### Plans And Progress

- [2026-04-27-minic-types-multifile-linker-plan.md](/E:/project/cpp/minic/docs/superpowers/plans/2026-04-27-minic-types-multifile-linker-plan.md)
- [2026-04-28-bss-validation-progress.md](/E:/project/cpp/minic/docs/superpowers/plans/2026-04-28-bss-validation-progress.md)

## Testing Entry Points

Fastest regression commands:

```powershell
ctest --preset phase-current
ctest --preset bss
```

These map to the test cases declared in [CMakeLists.txt](/E:/project/cpp/minic/CMakeLists.txt).
