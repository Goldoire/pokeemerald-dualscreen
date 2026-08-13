# Pokeemerald-Multiplatform ROM Hack Importer

The ROM Hack Importer is a Python-based utility located at `tools/romhack_importer/`.
Its purpose is to extract compatible modifications from a standard `pret/pokeemerald` decompilation project and convert them into a runtime mod package for `pokeemerald-multiplatform`.

## Features
The importer correctly detects and exports:
- **Map Patching**: Detects changes to map tiles and object events.
- **Script Compilation**: Extracts modified `scripts.inc` and compiles supported macros.
- **Starters**: Detects changes to `starter_choose.c`.
- **Species**: Parses `species_info.h` to extract changed base stats.
- **Trainers**: Parses `trainers.h` and `trainer_parties.h` to extract changed trainer parties.
- **Encounters**: Parses `wild_encounters.json` to extract changed wild encounters.
- **Moves**: Parses `battle_moves.h` to extract modified move parameters.
- **Items**: Parses `items.h` to extract modified item parameters (excluding custom function pointers).
- **Text**: Parses `species_names.h` to extract modified species names.

## Limitations & Unsupported Features
The importer is designed for runtime parity. The following modifications are **not supported** by the ModManager and will be ignored/reported during import:
- Arbitrary C code/ASM modifications
- New Maps, Resized Maps, and Map connections
- New Species IDs, New Move IDs, New Item IDs, New Trainer IDs
- Trainer modifications beyond Party Level and Species
- Arbitrary text changes outside of Species Names and script Dialogue
- Unsupported script macros
- Binary IPS/UPS patch importing

## Usage

Run the importer by providing the path to a completely vanilla pokeemerald tree, the path to the modified hack, and the output directory for the mod package.

```bash
python3 tools/romhack_importer/main.py --vanilla <path/to/vanilla> --source <path/to/hack> --output mods/<mod_name>
```

### Options
- `--dry-run`: Runs the importer and outputs a report to stdout, but does not write any files to disk.
- `--force`: If the output directory already exists, overwrite it.

## Port Report

Every time the importer runs, it generates two files in the output directory:
- `PORT_REPORT.md`: A human-readable markdown summary of what was imported and what was unsupported.
- `port_report.json`: A machine-readable copy of the report.
