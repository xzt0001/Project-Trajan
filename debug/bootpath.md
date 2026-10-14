Full Execution Entry Path: Step-by-Step
(Corrected version, verified line-by-line against the source on 2026-09-03. Step numbering kept compatible with bootpath_verified.md; new findings inserted as Steps 49a/49b/64. Config-dependent behavior is collected in "Build-configuration notes" at the end.)

Phase 0: QEMU Load
[Step 0] QEMU is launched via scripts/run_*.sh as qemu-system-aarch64 -M virt -kernel build/kernel8.img (raw binary derived from build/kernel.elf via objcopy -O binary). The image has no ARM64 Linux Image header, so QEMU loads it at RAM base + 0x80000 = physical 0x40080000 (virt RAM starts at 0x40000000), NOT at 0x80000. The linker script boot/linker.ld declares ENTRY(_start) and . = 0x80000, so the kernel is LINKED at 0x80000 but RUNS at 0x40080000 — a constant +0x40000000 slide. Consequence: PC-relative addressing (adr/adrp, C '&symbol' under the small code model) yields true runtime addresses; absolute addressing (ldr xN, =symbol, literal pools) yields linked 0x8xxxx/0x100xxxx addresses that fall in the virt flash region, where stores are silently discarded. After QEMU's built-in boot stub, the first kernel instruction executed is _start at 0x40080000.

Phase 1: _start in boot/start.S (assembly, pre-C)
[Step 1] → boot/start.S:29 → _start: → Entry symbol. First kernel instruction executed.
[Step 2] → boot/start.S:31 → mov x20, #0x09000000 → Saves PL011 UART base in callee-saved register x20.
[Step 3] → boot/start.S:39-43 → mov x1, x20 / mov w2, #0 / str w2, [x1, #0x30] → Disables UART (writes 0 to UARTCR at offset 0x30).
[Step 4] → boot/start.S:52-56 → mov w2, #0x70 / str w2, [x1, #0x2C] / mov w2, #0x301 / str w2, [x1, #0x30] → Configures UART: 8N1+FIFO in UARTLCR_H, enables UART+TX+RX in UARTCR.
[Step 5] → boot/start.S:58-64 → Redundant UART re-configuration (same registers, same values).
[Step 6] → boot/start.S:67-70 → uart_init_delay: loop → Busy-wait 0x10000 iterations for UART stabilization.
[Step 7] → boot/start.S:74-75 → mov x0, #0x40800000 / mov sp, x0 → Sets stack pointer to 1GB+8MB mark.
[Step 8] → boot/start.S:79-81 → mov w2, #'S' / str w2, [x1] → Prints 'S' to UART confirming stack setup.
[Step 9] → boot/start.S:94-127 → Inline hex print loop → Prints stack pointer value (e.g., S0x40800000) as hex via pure assembly. No C calls.
[Step 10] → boot/start.S:130-133 → Prints 'X' to UART.
[Step 11] → boot/start.S:136-138 → Prints 'B' to UART (BSS init start marker).
[Step 12] → boot/start.S:140-148 → ldr x0, =_bss_start / ldr x1, =_bss_end / bss_loop_new: → Runs the 16-bytes-at-a-time stp xzr, xzr zeroing loop — but over the LINKED addresses (0x1007000-0x100783C per build/kernel.elf), which land in the flash region, not over the real loaded BSS at 0x41007000-0x4100783C (see Step 0). The loop is effectively a no-op; the actual BSS is zero only because QEMU zero-fills cold RAM.
[Step 13] → boot/start.S:151-154 → Prints 'b' to UART (BSS init complete).

Phase 2: Modular boot components (assembly, calling into C where noted)
[Step 14] → boot/start.S:162 → bl security_feature_init → Branches to boot/security_enhanced.S:21.
[Step 15] → boot/security_enhanced.S:21-122 → security_feature_init: → Prints <SEC>, checks PAC support via mrs x21, id_aa64isar1_el1, checks MTE via mrs x21, id_aa64pfr1_el1, prints feature indicators ('P'/'N', 'M'/'n', '+'), returns via ret.
[Step 16] → boot/start.S:165 → bl enhanced_debug_setup → Branches to boot/debug_helpers.S:25.
[Step 17] → boot/debug_helpers.S:25-73 → enhanced_debug_setup: → Prints 'M', 's', '1', 'x'. Calls b test_return (direct branch, with x30 manually set to return_label_debug).
[Step 18] → memory/pmm.c:108 → test_return() (naked C function, pure inline asm) → Prints 'A'; then, because DEBUG_BOOT_VERBOSE enables DEBUG_TEST_PATTERNS_ENABLED (see notes), calls uart_hex64_early() six times (three 64-bit test patterns, twice each) and prints 'B'; returns via ret (to return_label_debug in debug_helpers.S:62). test_return is the first C-file function reached, but it is __attribute__((naked)) inline asm — the first ordinary compiled C function to execute is uart_hex64_early() (kernel/drivers/uart/uart_core.c:333), called from inside this step.
[Step 19] → boot/debug_helpers.S:62-73 → Prints 'y', restores registers, returns to start.S.
[Step 20] → boot/start.S:210 → bl init_pmm → Branches to memory/pmm.c:255. First C function invoked directly from start.S with a compiler-generated prologue/epilogue (note: uart_hex64_early already ran inside Step 18, so this is not the first compiled C code to execute).
[Step 21] → memory/pmm.c:255 → init_pmm() → Initializes physical memory manager (bitmap allocator for 0x40000000-0x48000000 region). Returns.
[Step 22] → boot/start.S:213 → bl debug_pmm_status → Branches to boot/debug_helpers.S:81.
[Step 23] → boot/debug_helpers.S:81-112 → debug_pmm_status: → Prints '2', 'P', 'c', 'V'. Returns. (The 'P' sits inside #ifdef DEBUG_MARKERS_EXTRA_ENABLED, but the guard is inert — see notes on .S preprocessing. Confirmed by uart.log: "2PcV".)
[Step 24] → boot/start.S:239 → bl boot_state_verify → Branches to boot/boot_verify.S:24.
[Step 25] → boot/boot_verify.S:24-126 → boot_state_verify: → Prints 'S' + SP value via bl uart_puthex (C call), verifies stack range (0x40000000-0x50000000, prints 'R'), forces 16-byte alignment via bic x3, x3, #0xF (markers 'F', misalignment digit, 'N', SP again, alignment digit, '='/'!'), and always prints the trailing 'C','D' markers (inert #ifdef, same as Step 23). Returns.
[Step 26] → boot/start.S:291 → bl vector_table_setup → Branches to boot/vector_setup.S:21.
[Step 27] → boot/vector_setup.S:21-127 → vector_table_setup: → Gets the runtime address of vector_table via adrp/add (PC-relative), verifies non-NULL and 2KB alignment, writes it to vbar_el1 via msr vbar_el1, x0 / isb, reads back and verifies, prints hex digits. Returns. CAVEAT: .vector_table is linked at VMA 0x1000000 with LMA ~0x93000 (AT() in linker.ld), and nothing in this path copies it — copy_vector_table_to_ram_if_needed() (kernel/init/arch/vector_ops.c) is never called. adrp therefore yields 0x41000000 (slid VMA) while the vector code bytes actually sit at ~0x40093000, so VBAR_EL1 points at memory containing no vectors. (Known issue; see bootpath_v2_plan.md F4.)
[Step 28] → boot/start.S:334 → bl init_vmm → Branches to memory/vmm.c:995. This begins the VMM/MMU initialization sequence.
[Step 29] → memory/vmm.c:995-1000 → init_vmm() → Calls init_vmm_impl() at memory/vmm.c:1258 to set up 4-level page tables (L0→L1→L2→L3) for both TTBR0 and TTBR1 (separate L0 tables allocated in init_page_tables(), memory/memory_core.c:72-79).
[Step 30] → memory/vmm.c:1004 → map_vector_table() → Maps vector table for post-MMU access.
[Step 31] → memory/vmm.c:1009 → map_uart() → Maps UART at virtual address for post-MMU use.
[Step 32] → memory/vmm.c:1014 → map_mmu_transition_code() → Maps the transition-code region (span covering enable_mmu_enhanced, mmu_continuation_point, and the current PC, ±64KB) twice: identity (PH1A) and at its HIGH_VIRT_BASE alias (PH1B), plus the current stack ±8KB (also low and high). The PH1B high mapping is what later makes the Step 63 call target reachable.
[Step 33] → memory/vmm.c:1019 → audit_memory_mappings() → Audits for overlapping mappings.
[Step 34] → memory/vmm.c:1024 → verify_code_is_executable() → Verifies PXN bits are clear on code pages.
[Step 35] → memory/vmm.c:1029 → enable_mmu(l0_table) → Branches to memory/vmm.c:521.
[Step 36] → memory/vmm.c:521-574 → enable_mmu() → Verifies critical mappings, performs cache maintenance, checks/sets VBAR_EL1, maps UART, issues dsb ish, then calls enable_mmu_enhanced(page_table_base) at line 574.

Phase 3: MMU Enablement
[Step 37] → memory/memory_core.c:271 → enable_mmu_enhanced() → The master MMU enable function. Since TEST_POLICY_APPROACH is #defined to 0 (line 280), takes the #else path (original approach, line 309+, prints "ORIG:DEBUG").
[Step 38] → memory/memory_core.c:359 → mmu_configure_tcr_bootstrap_dual() → Configures TCR_EL1 with EPD0=0 (TTBR0 enabled), EPD1=0 (TTBR1 enabled) for dual-table bootstrap.
[Step 39] → memory/memory_core.c:366 → mmu_configure_mair() → Writes MAIR_EL1 with memory attribute indices.
[Step 40] → memory/memory_core.c:373 → mmu_set_ttbr_bases() → Writes TTBR0_EL1 and TTBR1_EL1 with the respective L0 page table physical addresses.
[Step 41] → memory/memory_core.c:448 → map_range(page_table_base, ...) → Identity-maps 16KB around the assembly_start label (covers the inline asm blocks including mmu_enable_point).
[Step 42] → memory/memory_core.c:501 → map_range_dual_trampoline(...) → Dual-maps mmu_trampoline_low (memory/trampoline.S) at both TTBR0 (low physical) and TTBR1 (high virtual) addresses.
[Step 43] → memory/memory_core.c:529 → map_vector_table_dual(...) → Dual-maps vector table.
[Step 44] → memory/memory_core.c:535-538 → mmu_barrier_sequence_pre_enable() / mmu_comprehensive_tlbi_sequence() → Pre-enable barriers and full TLB invalidation.
[Step 45] → memory/memory_core.c:541-595 → First inline asm block, assembly_start: → Loads x19←TTBR0 L0 base, x18←TTBR1 L0 base, x20←TCR, x21←MAIR, x22←continuation_phys, x24←continuation_virt. Prints "PHYS:<EL>" and "POL1:" diagnostic markers.
[Step 46] → memory/memory_core.c:602-611 → Policy layer re-applies MAIR, TCR (bootstrap dual), TTBR bases from C.
[Step 47] → memory/memory_core.c:626-672 → Second asm block → Flushes cache lines for TTBR0 and TTBR1 page tables via dc cvac, issues dsb sy.
[Step 48] → memory/memory_core.c:687 → mmu_comprehensive_tlbi_sequence() → Full TLB invalidation before MMU enable.
[Step 49] → memory/memory_core.c:692-1227 → Third asm block → Multi-stage barrier sequence (dsb sy/isb/ic iallu), mmu_enable_point label (now only a label), firmware SCTLR dump, verifies VBAR_EL1, reads L0[0], dumps TTBR0/TTBR1 values, extensive pre-enable diagnostics. Does NOT write SCTLR_EL1 — every msr sctlr_el1 in this file is commented out as DEAD CODE; the only reachable SCTLR_EL1 write in the whole boot path is memory/trampoline.S:87 (Step 55).
[Step 49a] → memory/memory_core.c:1245-1613 → Fourth asm block ("minimal MMU enable test" — diagnostics only, no enable) → Disables all interrupts (msr daifset, #15), dumps DAIF bits ("DAIF:"..."FULL"/"PART"), checks whether the MMU is already on (cbnz x0, already_enabled — a label in the unreachable code below Step 50), then a comprehensive register dump ("SING:", "DIAG:": CurrentEL, SCTLR M-bit readback, EL1/EL2/VHE/HCR probing), ends with "ASM:"/"POL2:" markers. No SCTLR write here either.
[Step 49b] → memory/memory_core.c:1615-2153 → C diagnostics before the jump → "MMU:START" marker (historical name; nothing is enabled here), TCR checkpoint 2 with EPD0 dump, verifies the trampoline and stack identity PTEs in TTBR0, walks the full L0→L1→L2→L3 translation for the UART in TTBR0 ("UWALK:") and for the high-virtual UART 0xFFFF800009000000 in TTBR1 ("UL0[256]/UL1[0]/UL2[72]/UL3[0]" with PA check), prints "JMP:TRAMP".
[Step 50] → memory/memory_core.c:2154 → asm volatile("b mmu_trampoline_low") → Unconditional branch to the trampoline. Execution transfers to memory/trampoline.S:33. Does not return. Everything after this line — including the fifth asm block at 2161-2486 containing the commented-out SCTLR fallback ladder — is unreachable.

Phase 4: Trampoline — The Actual MMU Enable
[Step 51] → memory/trampoline.S:33 → mmu_trampoline_low: → Saves x29/x30, x19/x20, x21/x22. Prints "TLOW\r\n" to the physical UART.
[Step 52] → memory/trampoline.S:61-62 → dsb sy / isb → Pre-MMU barrier sequence. Prints "E1".
[Step 53] → memory/trampoline.S:71 → mrs x0, sctlr_el1 → Reads current SCTLR_EL1. Prints "E2".
[Step 54] → memory/trampoline.S:79 → orr x0, x0, #0x1 → Sets M bit (bit 0, MMU enable). Prints "E3".
[Step 55] → memory/trampoline.S:87 → msr sctlr_el1, x0 → THE CRITICAL INSTRUCTION: MMU is now enabled — the only SCTLR_EL1 write reachable in the entire boot path. Prints "E4".
[Step 56] → memory/trampoline.S:96-97 → dsb sy / isb → Post-MMU mandatory barriers per ARM ARM. Prints "E5\r\n".
[Step 57] → memory/trampoline.S:110-112 → mrs x0, sctlr_el1 / and x0, x0, #0x1 / cbnz x0, 1f → Verifies MMU enable succeeded (M bit = 1). Branches to label 1: (line 131); on failure prints "MFAIL" and parks in b . (line 129).
[Step 58] → memory/trampoline.S:137-144 → Prints "M+" to UART confirming MMU verification passed.
[Step 59] → memory/trampoline.S:160-161 → movz x21, #0x8000, lsl #32 / movk x21, #0xFFFF, lsl #48 → Constructs HIGH_VIRT_BASE = 0xFFFF800000000000.
[Step 60] → memory/trampoline.S:164-165 → adr x20, mmu_trampoline_high / orr x20, x20, x21 → Computes high virtual address of mmu_trampoline_high.
[Step 61] → memory/trampoline.S:181 → br x20 → (prints "BR\r\n" first, lines 169-178) Atomic PC transition: jumps from TTBR0 (identity-mapped physical) to TTBR1 (high virtual) address space.
[Step 62] → memory/trampoline.S:188 → mmu_trampoline_high: → Now executing from high virtual address 0xFFFF80004009XXXX. Constructs high UART virtual address 0xFFFF800009000000. Prints "HI".
[Step 63] → memory/trampoline.S:210 → bl mmu_trampoline_continuation_point → Branches to mmu_trampoline_continuation_point() at memory/memory_core.c:2505 (reachable in high VA thanks to Step 32's PH1B mapping). Running in TTBR1 space, it: prints "CONT:HIGH" via the high UART; updates VBAR_EL1 to its high alias (HIGH_VIRT_BASE | vbar_low, lines 2516-2530, prints "VBAR:HIGH"); switches TCR from bootstrap-dual to kernel-only mode (EPD0=1, EPD1=0) via mmu_configure_tcr_kernel_only() (line 2541, prints "CONT:TCR+"); self-tests the exception vectors with brk #0 (line 2551, expects "TEST:OK"); prints "CONT:OK" and returns to the trampoline.
    NOTE: the similarly named mmu_continuation_point() at memory/vmm.c:1038-1039 is a DIFFERENT function that never executes in this build — it is only referenced by the compiled-out TEST_POLICY_APPROACH path and by address diagnostics. Earlier revisions of this document incorrectly named it as the Step 63 target.
[Step 64] → memory/trampoline.S:212-216 → End of the documented path: the trampoline restores x21/x22, x19/x20, x29/x30 and executes ret. But the x30 saved at Step 51 is stale — it was clobbered as a scratch register by the enable_mmu_enhanced asm blocks/calls before Step 50, and at best holds a low TTBR0-space return site inside enable_mmu_enhanced, which is no longer translatable after Step 63 set EPD0=1. Control therefore never returns to init_vmm()/start.S: debug_vmm_status (start.S:337), final_verification (start.S:364), and bl kernel_main (start.S:375) are unreachable in the successful-MMU path. kernel_main is never entered by this flow.

Build-configuration notes (affect the markers and claims above)
- DEBUG_BOOT_VERBOSE is hard-defined in include/debug_config.h:18 (and passed again as -DDEBUG_BOOT_VERBOSE by the Makefile), so DEBUG_TEST_PATTERNS_ENABLED and friends are always on for C files (this is why Step 18 prints the six test patterns and 'B').
- .S files are assembled directly with aarch64-elf-as (no C preprocessor pass), so #include/#ifdef/#endif lines in boot/*.S are treated as comments: all "conditionally guarded" markers there are unconditionally assembled ('P' in Step 23, 'C','D' in Step 25).
- TEST_POLICY_APPROACH is #defined to 0 (memory/memory_core.c:280): enable_mmu_enhanced takes the "ORIG:DEBUG" path, and the policy-based enable (including mmu_enable_translation()'s SCTLR write at memory/mmu_policy.c:467) is never invoked.
- In the checked-in uart.log only uart_*_early() output and raw 0x09000000 stores appear; the non-early uart_puts()/uart_putc() strings ("[VMM] ...", "[PMM] ...", init_pmm's 'A'/'B'/'P') are absent. That log predates the current source. In the current code g_uart_base is an initialized .data global (kernel/drivers/uart/uart_globals.c:6, = 0x09000000) accessed PC-relatively, so these functions should reach the UART pre-MMU too (modulo the offset-scaling quirk in uart_core.c's uart_read_reg/uart_write_reg, where g_uart_base + 0x18 addresses byte offset 0x60); treat missing strings in the old log as a stale-build artifact, not documented behavior.
- Runtime caveat: the checked-in uart.log predates the current source and ends before "TLOW" is ever printed; Steps 51-64 are verified against the source code, not against a captured successful run.
