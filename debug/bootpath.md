Full Execution Entry Path: Step-by-Step
Phase 0: QEMU Load
[Step 0] QEMU loads build/kernel8.img (raw binary derived from build/kernel.elf via objcopy -O binary) at physical address 0x80000 on the virt machine (-M virt). The linker script boot/linker.ld declares ENTRY(_start) and . = 0x80000. The first instruction fetched by the CPU is at 0x80000, which is the _start label.

Phase 1: _start in boot/start.S (assembly, pre-C)
[Step 1] → boot/start.S:29 → _start: → Entry symbol. First instruction executed.
[Step 2] → boot/start.S:31 → mov x20, #0x09000000 → Saves PL011 UART base in callee-saved register x20.
[Step 3] → boot/start.S:39-43 → mov x1, x20 / mov w2, #0 / str w2, [x1, #0x30] → Disables UART (writes 0 to UARTCR at offset 0x30).
[Step 4] → boot/start.S:52-56 → mov w2, #0x70 / str w2, [x1, #0x2C] / mov w2, #0x301 / str w2, [x1, #0x30] → Configures UART: 8N1+FIFO in UARTLCR_H, enables UART+TX+RX in UARTCR.
[Step 5] → boot/start.S:58-64 → Redundant UART re-configuration (same registers, same values).
[Step 6] → boot/start.S:67-70 → uart_init_delay: loop → Busy-wait 0x10000 iterations for UART stabilization.
[Step 7] → boot/start.S:74-75 → mov x0, #0x40800000 / mov sp, x0 → Sets stack pointer to 1GB+8MB mark.
[Step 8] → boot/start.S:79-81 → mov w2, #'S' / str w2, [x1] → Prints 'S' to UART confirming stack setup.
[Step 9] → boot/start.S:94-127 → Inline hex print loop → Prints stack pointer value (e.g., S0x40800000) as hex via pure assembly. No C calls.
[Step 10] → boot/start.S:130-133 → Prints 'X' to UART.
[Step 11] → boot/start.S:136-138 → Prints 'B' to UART (BSS init start marker).
[Step 12] → boot/start.S:140-148 → ldr x0, =_bss_start / ldr x1, =_bss_end / bss_loop_new: → Zeroes BSS section 16 bytes at a time via stp xzr, xzr.
[Step 13] → boot/start.S:151-154 → Prints 'b' to UART (BSS init complete).

Phase 2: Modular boot components (assembly, calling into C where noted)
[Step 14] → boot/start.S:162 → bl security_feature_init → Branches to boot/security_enhanced.S:21.
[Step 15] → boot/security_enhanced.S:21-122 → security_feature_init: → Prints <SEC>, checks PAC support via mrs x21, id_aa64isar1_el1, checks MTE via mrs x21, id_aa64pfr1_el1, prints feature indicators, returns via ret.
[Step 16] → boot/start.S:165 → bl enhanced_debug_setup → Branches to boot/debug_helpers.S:25.
[Step 17] → boot/debug_helpers.S:25-73 → enhanced_debug_setup: → Prints 'M', 's', '1', 'x'. Calls b test_return (direct branch, with x30 manually set to return_label_debug).
[Step 18] → memory/pmm.c:108 → test_return() (naked C function, pure inline asm) → Prints 'A' to UART, then returns via ret (to return_label_debug in debug_helpers.S:62). This is the first C function reached, though it is __attribute__((naked)) and consists entirely of inline asm.
[Step 19] → boot/debug_helpers.S:62-73 → Prints 'y', restores registers, returns to start.S.
[Step 20] → boot/start.S:210 → bl init_pmm → Branches to memory/pmm.c:255. This is the first true C function call (with compiler-generated prologue/epilogue).
[Step 21] → memory/pmm.c:255 → init_pmm() → Initializes physical memory manager (bitmap allocator for 0x40000000-0x48000000 region). Returns.
[Step 22] → boot/start.S:213 → bl debug_pmm_status → Branches to boot/debug_helpers.S:81.
[Step 23] → boot/debug_helpers.S:81-112 → debug_pmm_status: → Prints '2', 'c', 'V'. Returns.
[Step 24] → boot/start.S:239 → bl boot_state_verify → Branches to boot/boot_verify.S:24.
[Step 25] → boot/boot_verify.S:24-126 → boot_state_verify: → Prints SP value via bl uart_puthex (C call), verifies stack range (0x40000000-0x50000000), forces 16-byte alignment via bic x3, x3, #0xF. Returns.
[Step 26] → boot/start.S:291 → bl vector_table_setup → Branches to boot/vector_setup.S:21.
[Step 27] → boot/vector_setup.S:21-127 → vector_table_setup: → Gets physical address of vector_table via adrp/add, writes it to vbar_el1 via msr vbar_el1, x0 / isb, verifies, prints hex digits. Returns.
[Step 28] → boot/start.S:334 → bl init_vmm → Branches to memory/vmm.c:995. This begins the VMM/MMU initialization sequence.
[Step 29] → memory/vmm.c:995-1000 → init_vmm() → Calls init_vmm_impl() at memory/vmm.c:1258 to set up 4-level page tables (L0→L1→L2→L3) for both TTBR0 and TTBR1.
[Step 30] → memory/vmm.c:1004 → map_vector_table() → Maps vector table for post-MMU access.
[Step 31] → memory/vmm.c:1009 → map_uart() → Maps UART at virtual address for post-MMU use.
[Step 32] → memory/vmm.c:1014 → map_mmu_transition_code() → Identity-maps the enable_mmu code region so it can execute during the transition.
[Step 33] → memory/vmm.c:1019 → audit_memory_mappings() → Audits for overlapping mappings.
[Step 34] → memory/vmm.c:1024 → verify_code_is_executable() → Verifies PXN bits are clear on code pages.
[Step 35] → memory/vmm.c:1029 → enable_mmu(l0_table) → Branches to memory/vmm.c:521.
[Step 36] → memory/vmm.c:521-574 → enable_mmu() → Verifies critical mappings, performs cache maintenance, checks/sets VBAR_EL1, maps UART, issues dsb ish, then calls enable_mmu_enhanced(page_table_base) at line 574.

Phase 3: MMU Enablement
[Step 37] → memory/memory_core.c:271 → enable_mmu_enhanced() → The master MMU enable function. Since TEST_POLICY_APPROACH is #defined to 0, takes the #else path (original approach, line 309+).
[Step 38] → memory/memory_core.c:359 → mmu_configure_tcr_bootstrap_dual() → Configures TCR_EL1 with EPD0=0 (TTBR0 enabled), EPD1=0 (TTBR1 enabled) for dual-table bootstrap.
[Step 39] → memory/memory_core.c:366 → mmu_configure_mair() → Writes MAIR_EL1 with memory attribute indices.
[Step 40] → memory/memory_core.c:373 → mmu_set_ttbr_bases() → Writes TTBR0_EL1 and TTBR1_EL1 with the respective L0 page table physical addresses.
[Step 41] → memory/memory_core.c:448 → map_range(page_table_base, ...) → Identity-maps the assembly block around mmu_enable_point.
[Step 42] → memory/memory_core.c:501 → map_range_dual_trampoline(...) → Dual-maps mmu_trampoline_low (memory/trampoline.S) at both TTBR0 (low physical) and TTBR1 (high virtual) addresses.
[Step 43] → memory/memory_core.c:529 → map_vector_table_dual(...) → Dual-maps vector table.
[Step 44] → memory/memory_core.c:535-538 → mmu_barrier_sequence_pre_enable() / mmu_comprehensive_tlbi_sequence() → Pre-enable barriers and full TLB invalidation.
[Step 45] → memory/memory_core.c:541-595 → Inline asm block assembly_start: → Loads registers x19-x24 with page table bases, TCR, MAIR, continuation addresses. Prints diagnostic markers.
[Step 46] → memory/memory_core.c:602-611 → Policy layer re-applies MAIR, TCR (bootstrap dual), TTBR bases from C.
[Step 47] → memory/memory_core.c:626-672 → Second asm block → Flushes cache lines for TTBR0 and TTBR1 page tables via dc cvac, issues dsb sy.
[Step 48] → memory/memory_core.c:687 → mmu_comprehensive_tlbi_sequence() → Full TLB invalidation before MMU enable.
[Step 49] → memory/memory_core.c:692-967 → Third asm block → Multi-stage barrier sequence (dsb sy/isb/ic iallu), verifies VBAR_EL1, dumps TTBR0/TTBR1 values, performs extensive pre-enable diagnostics. Does NOT write SCTLR_EL1 here (all msr sctlr_el1, x23 are commented out as DEAD CODE).
[Step 50] → memory/memory_core.c:2154 → asm volatile("b mmu_trampoline_low") → Unconditional branch to the trampoline. Execution transfers to memory/trampoline.S:33. Does not return.

Phase 4: Trampoline — The Actual MMU Enable
[Step 51] → memory/trampoline.S:33 → mmu_trampoline_low: → Saves registers. Prints "TLOW" to UART.
[Step 52] → memory/trampoline.S:61-62 → dsb sy / isb → Pre-MMU barrier sequence.
[Step 53] → memory/trampoline.S:71 → mrs x0, sctlr_el1 → Reads current SCTLR_EL1.
[Step 54] → memory/trampoline.S:79 → orr x0, x0, #0x1 → Sets M bit (bit 0, MMU enable).
[Step 55] → memory/trampoline.S:87 → msr sctlr_el1, x0 → THE CRITICAL INSTRUCTION: MMU is now enabled. The CPU begins translating addresses via page tables.
[Step 56] → memory/trampoline.S:96-97 → dsb sy / isb → Post-MMU mandatory barriers per ARM ARM.
[Step 57] → memory/trampoline.S:110-112 → mrs x0, sctlr_el1 / and x0, x0, #0x1 / cbnz x0, 1f → Verifies MMU enable succeeded (M bit = 1). Branches to label 1: (line 131).
[Step 58] → memory/trampoline.S:137-144 → Prints "M+" to UART confirming MMU verification passed.
[Step 59] → memory/trampoline.S:160-161 → movz x21, #0x8000, lsl #32 / movk x21, #0xFFFF, lsl #48 → Constructs HIGH_VIRT_BASE = 0xFFFF800000000000.
[Step 60] → memory/trampoline.S:164-165 → adr x20, mmu_trampoline_high / orr x20, x20, x21 → Computes high virtual address of mmu_trampoline_high.
[Step 61] → memory/trampoline.S:181 → br x20 → Atomic PC transition: jumps from TTBR0 (identity-mapped physical) to TTBR1 (high virtual) address space.
[Step 62] → memory/trampoline.S:188 → mmu_trampoline_high: → Now executing from high virtual address 0xFFFF8000XXXXXXXX. Constructs high UART virtual address 0xFFFF800009000000. Prints "HI".
[Step 63] → memory/trampoline.S:210 → bl mmu_trampoline_continuation_point → Branches to memory/vmm.c:1039 → mmu_continuation_point() in high virtual space. This function finalizes the MMU transition (switches TCR to kernel-only mode, updates VBAR_EL1 to virtual address, etc.).
