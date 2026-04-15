# AltirraSDL Upstream Sync History

One line per sync, newest first.

Format:  `YYYY-MM-DD  <OLD> → <NEW>  <summary>`

2026-04-15  4.50-test8 → 4.50-test9  166 trivial copies, 43 three-way merges, 4 added (printer PDF/SVG export). AltirraOS 3.49 kernel fixes (printer P:, SIO timeout), AMDC/Percom/SAP bug fixes, GCC portability macros (VD_COMPILER_CLANG_OR_GCC, VD_PLATFORM_WINDOWS, VD_NO_UNIQUE_ADDRESS) adopted from upstream. Kernel ROM rebuilt via MADS (kernel.rom 10K, kernelxl.rom 16K, nokernel.rom 16K) and re-embedded as C arrays under src/AltirraSDL/romdata/. Linux release build green; --test-mode ping/query_state OK.
