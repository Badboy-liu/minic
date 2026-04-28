# PE/COFF Linker Teaching Design

Date: 2026-04-28

## Goal

Turn the current PE/COFF linker into a small teaching-oriented linker with:

- clearly documented support boundaries
- observable link steps
- reproducible teaching samples

This phase does not aim to make the linker broadly feature-complete. It aims to make the current model easy to explain, inspect, and extend safely.

## Why This Phase First

The project already has the most valuable teaching asset: a full pipeline from a small C subset to NASM assembly, COFF objects, and a self-written PE executable linker.

Recent `.bss` validation showed the next bottleneck clearly:

- the linker is capable enough to teach real concepts
- the supported boundary is still implicit
- failures and internal state are harder to inspect than they should be

Before adding more relocation types or richer import support, the linker should first become explicit about what it supports and transparent about what it is doing.

## Current Linker Model

The current linker is a minimal Windows AMD64 PE linker for NASM-generated COFF object files.

### Inputs

- one or more NASM-generated AMD64 COFF object files
- compiler-generated symbols for:
  - functions
  - global variables
  - entry point
- a fixed imported Windows API dependency:
  - `kernel32.dll!ExitProcess`

### Current Supported Concepts

- section merge for:
  - `.text`
  - `.data`
  - `.rdata`
  - `.bss`
- external symbol resolution across multiple compiler-generated objects
- generation of a minimal PE image
- a minimal import table for `ExitProcess`
- `REL32` relocation handling in `.text`
- correct section-relative addend preservation for NASM COFF references into `.bss`

### Current Intentional Limits

- no static libraries
- no linker script support
- no arbitrary COFF producer compatibility goal
- no general relocation coverage beyond the linker's current minimal model
- no broad Win32 import resolution system
- no resources, TLS, debug data, or exception unwind metadata
- no PE base relocation table for rebasing

## Phase Scope

This phase covers two workstreams:

1. document the linker's support boundary
2. add lightweight teaching-oriented trace output

It also includes a small amount of sample and verification organization so the documented model can be demonstrated repeatedly.

This phase does not include:

- new object file formats
- ELF linking
- broader import machinery
- broad relocation expansion
- language-front-end feature expansion unrelated to linker teaching

## Workstream A: Support Boundary Documentation

### Objective

Make the current linker model explicit in both top-level and detailed documentation.

### Deliverables

- `README.md` summary of what the linker supports
- one detailed PE/COFF linker support document under `docs/`
- explicit notes on supported and unsupported sections, symbols, relocations, and import behavior

### Required Content

The detailed document should answer:

- what kind of COFF objects are expected
- which sections are recognized and merged
- which sections are ignored or unsupported
- how external symbol resolution currently works
- what relocation shape is currently expected
- what import support exists
- what the executable image layout conceptually looks like
- what failure modes a user should expect when stepping outside the supported subset

### Documentation Style

The top-level README should stay concise and point readers to the deeper document.

The detailed document should optimize for teaching:

- use short, concrete sections
- prefer “supported / not supported / why” framing
- explain terms such as RVA, raw file offset, section merge, external symbol, and relocation in repo-specific language

## Workstream B: Teaching-Oriented Link Trace

### Objective

Add a lightweight linker trace mode that exposes the important internal steps without turning the linker into a binary dump tool.

### Proposed Interface

Add a CLI option:

- `--link-trace`

This option should enable human-readable linker diagnostics during the final link step.

### Trace Principles

- trace is for explanation, not exhaustive debugging
- output should be stable enough to reference in docs
- each line should help answer “what did the linker just do?”
- avoid giant raw hex dumps
- avoid leaking internal noise that would make the output hard to scan

### Trace Sections

The trace should be grouped into four blocks.

#### 1. Input Object Summary

For each object:

- object path
- recognized sections and their sizes
- presence of key externally visible symbols when practical

Example shape:

```text
[link] object build/output/foo.obj
[link]   section .text size=...
[link]   section .bss size=...
```

#### 2. Merged Section Layout

After section merge:

- section name
- virtual size
- raw size
- RVA
- raw file pointer
- whether the section is uninitialized data

This is the most important teaching output because it connects object sections to final PE image layout.

#### 3. External Symbol Resolution Summary

For notable resolved symbols:

- symbol name
- source object or section when available
- resolved RVA

For imported symbols:

- imported symbol name
- import thunk or IAT target summary

This block should make it obvious how multi-object references resolve.

#### 4. Relocation Application Summary

For each applied relocation in the supported subset:

- object or merged text context
- relocation kind
- target symbol or section
- applied target RVA
- resulting relative displacement when useful

This block is especially valuable for demonstrating section-symbol plus addend behavior such as the recently fixed `.bss` case.

### Non-Goals For Trace

- no full symbol table dump by default
- no raw relocation table dump by default
- no byte-by-byte output
- no attempt to mimic `dumpbin` or `objdump`

## Workstream C: Sample And Verification Organization

### Objective

Turn the current ad hoc validation samples into a small teaching set that matches the documented linker model.

### Sample Categories

The sample set should cover:

- single-file executable baseline
- multi-file function resolution
- initialized global data in `.data`
- uninitialized and tentative globals in `.bss`
- failure cases:
  - missing entry point
  - unresolved external symbol
  - duplicate external symbol

### Expected Near-Term Handling

The current sample files under `input/` can remain small and compiler-facing, but they should be treated as part of a named verification set rather than temporary scratch files.

The `.bss` samples introduced during debugging should become first-class regression inputs.

### Verification Expectations

At minimum, the phase should provide a documented manual verification flow for:

- build succeeds
- baseline executable returns expected exit code
- `.bss` integrity sample returns expected exit code
- multi-object sample links and runs
- `--link-trace` produces the documented high-level blocks

Automated regression coverage is desirable next, but not required to complete this phase.

## Recommended Implementation Order

### Step 1: Boundary Documentation

Write the detailed linker support document first.

Reason:

- it forces precise scope
- it defines what the trace should expose
- it makes later review easier

### Step 2: Trace Output

Implement `--link-trace` with the four documented blocks.

Reason:

- the desired output format is already defined
- implementation can stay disciplined and small

### Step 3: Sample Curation

Promote the `.bss` and multi-object samples into a documented teaching/verification set.

Reason:

- the new docs and trace now have concrete demonstrations

## Success Criteria

This phase is complete when:

- the current PE/COFF linker support boundary is documented clearly
- the README links users to that boundary explanation
- `--link-trace` exists and prints the planned high-level blocks
- the `.bss` integrity scenario is part of the documented verification story
- at least one multi-object link scenario is documented and reproducible

## Risks And Tradeoffs

### Risk: Trace Output Becomes Too Verbose

Mitigation:

- keep the default trace compact
- prefer summaries over dumps
- only print concepts that map directly to teaching goals

### Risk: Documentation Drifts From Implementation

Mitigation:

- derive the docs from the current code, not aspirational behavior
- keep examples small and executable
- use the trace output as the concrete behavior reference

### Risk: Temporary Samples Stay Temporary Forever

Mitigation:

- explicitly reclassify key samples as regression/teaching inputs
- document expected results

## Follow-On Phase

After this phase, the most natural next step is deeper PE/COFF support:

- broader relocation coverage
- richer import handling
- clearer duplicate/unresolved symbol diagnostics

At that point the linker will be easier to extend because its current model, observability, and validation set will already be in place.
