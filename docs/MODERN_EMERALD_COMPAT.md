# Modern Emerald Compatibility Matrix

This document tracks the compatibility between `pokeemerald-multiplatform` and the Modern Emerald ROM hack.

## Core Systems

| Feature | Status | Notes |
|---------|--------|-------|
| Asset Pipeline | ✅ Supported | `tx_rac_menu.c` and associated UI assets compile successfully within the platform extensions. |
| Map Patches | ✅ Supported | `PORT_REPORT.md` confirmed 27 out of 27 map headers processed. Object events injected properly. |
| Overrides | ✅ Supported | Species, Trainers, and basic encounters are mapped. String text loading fixed to support EOS termination. |
| Scripting | ⚠️ Partial | Scripts successfully translated; however, advanced Modern Emerald opcodes may require runtime engine hooks. |
| Configuration UI | ✅ Supported | Challenge & Randomizer UI successfully injected as a host-side runtime extension into the Birch intro. Memory leak in arrow task IDs patched. |
| Early Game Flow | ✅ Supported | Moving truck, Littleroot Town, and Starter Selection all functional and tested. |

## Bug Fixes and Patches
- **`tx_rac_menu.c` Task Leak**: Fixed a memory leak where `sOptions->arrowTaskId` remained active after closing the configuration UI.
- **`mod_text.c` String Engine Crash**: Fixed a critical `SIGSEGV` during starter selection. `mod_text.c` injected ASCII strings terminated with `0x00`. `pokeemerald` text engine interprets `0x00` as a space and loops until `0xFF` (`EOS`). The loader now safely appends `0xFF` to injected text overrides.

## Mod Integration Architecture

- **`--mod <mod_id>` Exclusivity:** Allows launching a specific mod exclusively. 
- **Decoupled Configuration:** Modern Emerald's custom variables have been segregated from the core `SaveBlock1` into a host-side structure (`gModernEmeraldConfig`).
- **Hooking Strategy:** The configuration flow is injected into `Task_NewGameBirchSpeech_ChooseGender` without mutating vanilla callbacks.

## Future Requirements

- Dynamic `SaveModData` persistence hook to save `gModernEmeraldConfig` alongside the vanilla save payload.
- Advanced opcode compatibility implementations.
