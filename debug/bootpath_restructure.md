The current bootpath is overengineered. I realized this is not because it is required but was accumulated during the debugging sessions where "something didn't work" happened too many times so I kept adding prints, extra barriers, re-writes as "fixes" for that stuck.

Here is the proposed version, revised after checking the 16-step draft against the verified current path (debug/bootpath.md) and the ARM64 Linux boot sequence (arch/arm64/kernel/head.S, arch/arm64/mm/proc.S). The step list is the target; the decisions below are what the steps silently depended on. Phased migration, file disposition and the debug runbook live in debug/bootpath_v2_plan.md.

==========================================================================
Decisions the step list depends on (write these down before coding)
==========================================================================

D1. Link model: linked high, loaded low, PIC until the jump.
    VMA = 0xFFFF800040080000, LMA = 0x40080000 via AT() per section.
    QEMU -M virt -kernel loads a raw image at RAM base + 0x80000 = 0x40080000
    (verified from uart.log PC values; re-verify under GDB before building).
    Everything before step 10 uses adrp/adr only. `ldr xN, =symbol` is forbidden
    pre-MMU except the three deliberate high-VA loads in steps 10, 10.5, 11.
    [Linux: image linked at KIMAGE_VADDR; adr_l everywhere until __primary_switched]
    Consequence: CFLAGS += -mgeneral-regs-only -fno-jump-tables (kernel-wide).
    FP/SIMD traps with CPACR_EL1.FPEN=0; jump tables hold absolute high VAs.

D2. TTBR1 is a linear map of RAM, with the image as the part that carries
    section permissions. VA = PA | HIGH_VIRT_BASE for all of 0x40000000–0x48000000.
    The 2 MB chunk(s) containing the image use 4 KB L3 pages (.text RX, .rodata
    RO, .data/.bss/stack/pool RW+XN); every other chunk is one 2 MB L2 block
    descriptor (Normal WBWA, RW, PXN|UXN). UART at 0xFFFF800009000000 as
    Device-nGnRE. Cost: one L1 entry, one L2 table with 64 entries, 1–2 L3 tables.
    PMM/VMM touch physical pages only via phys_to_virt(pa) = pa | HIGH_VIRT_BASE
    (today alloc_page() memsets through the PA and every page-table walk
    dereferences the PA; both die the moment the identity map is retired).
    [Linux: linear map / PAGE_OFFSET, __va/__pa; mark_linear_text_alias_ro]
    Requires block-descriptor support (bits[1:0]=0b01 at L2) in the table builder.

D3. The TTBR0 identity map exists only for steps 3–10 and is retired by
    repointing TTBR0 at a zeroed L0 page (reserved_pg_dir, inside the image), not
    by flipping EPD0. TCR is then written exactly once for the kernel's life, and
    the same TTBR0 repoint is what user tasks will do per switch later.
    [Linux: reserved_pg_dir, cpu_set_reserved_ttbr0]
    (EPD0=1 is an acceptable alternative; it costs a second TCR write now and a
    third when user space arrives.)

D4. Register values are compile-time constants in include/sysreg.h, derived from
    the ARM ARM, never copied from mmu_policy.c
    (which today has TG1=0b00 [reserved], IPS=36-bit under a "40-bit" comment,
    and TBI0/TBI1=1).
      INIT_MAIR_EL1       = 0x00000000_0444FF00
                            idx0 Device-nGnRnE, idx1 Normal WBWA, idx2 Normal NC,
                            idx3 Device-nGnRE (existing PTE_ATTRINDX users unchanged)
      INIT_TCR_EL1        = 0x00000002_B5103510
                            T0SZ=T1SZ=16 (48-bit VA), TG0=0b00 and TG1=0b10 (both 4 KB
                            — the encodings differ), SH0=SH1=inner, IRGN/ORGN=WBWA,
                            EPD0=0, EPD1=0, IPS=0b010 (40-bit; or read
                            ID_AA64MMFR0_EL1.PARange), TBI0=TBI1=0, AS=0
      INIT_SCTLR_EL1_MMU_ON = RES1 | M | C | I | SA | SA0 | DZE | UCT | nTWI | nTWE | UCI
                            = 0x34D5D81D  (RES1 = bits 29,28,23,22,20,11 = 0x30D00800)
                            RES1 is architecturally required; DZE/UCT/UCI/nTWx stop the
                            first user task trapping on dc zva, ctr_el0, wfe/wfi; SPAN
                            (bit 23, in RES1) keeps exception entry from setting PAN.
                            Verify the arithmetic when writing sysreg.h.
      INIT_SCTLR_EL1_MMU_ON_NOCACHE = same without C and I = 0x34D5C819 (bring-up rung)
    [Linux: INIT_SCTLR_EL1_MMU_ON, tcr_compute_pa_size, MAIR_EL1_SET]

==========================================================================
Boot sequence
==========================================================================

_start:                                  (linked high, executing low, PIC only)
    0.   LMA = 0x40080000 (= QEMU load address, gate: first PC under GDB);
         VMA = 0xFFFF800040080000 via AT(). _start is byte 0 of the image.
    0.5  Assert CurrentEL == EL1, hang with a marker otherwise (QEMU virt default;
         -M virt,virtualization=on would enter at EL2). msr daifset, #0xf to make
         the reset-state assumption explicit.                 [head.S: init_kernel_el]
    0.6  Stash the DTB pointer (x0) in a callee-saved register.
    1.   SP = boot_stack_top (adrp/add → physical).
    2.   Zero BSS with adrp/add bounds (correct regardless of link base; today's
         `ldr =_bss_start` zeroes flash-space addresses). Then store the DTB global.
    2.5  BRINGUP: VBAR = physical vector_table (adrp) so early_trap.S can dump
         ESR/ELR/FAR over the physical UART.
    3.   bl build_boot_page_tables (C, one call; dc ivac the pool first; pool is
         pre-zeroed by step 2)
         TTBR0 identity map covers: the whole image (not just "code through
         step 10" — vectors and early_trap must stay reachable), boot stack,
         pool, and the UART page [BRINGUP].
    4.   TTBR1 map per D2: linear map of RAM with image permissions, UART device
         page. Vector table is inside .text (see P1). Returns bases in x0/x1.
    4.5  Checks the MMU configuration depends on, before touching it:
         ID_AA64MMFR0_EL1.TGran4 supported; PARange >= chosen IPS.  [head.S: __enable_mmu]
    5.   msr mair_el1 = INIT_MAIR_EL1 ; msr tcr_el1 = INIT_TCR_EL1 (EPD0=0)   — once
    5.5  x0 = INIT_SCTLR_EL1_MMU_ON (complete constant, no mrs sctlr_el1 anywhere)
    5.6  msr ttbr0_el1 ; msr ttbr1_el1 ; isb                                    — once
    6.   dsb sy ; tlbi vmalle1 ; dsb nsh ; isb                           [proc.S: __cpu_setup]
    7.   msr sctlr_el1, x0        — the only SCTLR_EL1 write in the kernel
    8.   isb ; ic iallu ; dsb nsh ; isb                                 [head.S: set_sctlr_el1]
    --- MMU on, PC still identity-mapped. No stack, no UART (except one BRINGUP
        marker via the identity UART page) until 10.5 / 12 ---
    9.   (dropped — Linux has no post-enable verification; BRINGUP marker only)
    10.  ldr x1, =high_entry ; br x1     (literal holds the linked high VA; legal
         only because of D1 — with a low link base this branches low)
    --- executing via TTBR1 ---
high_entry:
    10.5 ldr x0, =boot_stack_top ; mov sp, x0                (high alias of the stack)
    11.  ldr x0, =vector_table ; msr vbar_el1, x0 ; isb      — the only VBAR write
    12.  g_uart_base = UART_VIRT                             (one store; see P5)
    12.5 Retire the identity map (D3): msr ttbr0_el1 = reserved_pg_dir ; isb ;
         tlbi vmalle1 ; dsb nsh ; isb.  From here no code may use a physical
         address as a pointer. Negative test (bring-up gate, then an optional
         self-test): a load from 0x40080000 must fault into the vector handler
         and the report must arrive over the virtual UART.
    13.  init_pmm: reserve [0x40080000, _end) (covers stack, pool, reserved_pg_dir
         by D5), the PMM bitmap, and the DTB blob; alloc_page/map_page use
         phys_to_virt(). Today's init_pmm_impl reserves nothing — its marking
         code sits inside a /* */ block and the allocator would reach the kernel
         image after 128 pages.
    14.  CPU feature report in C (replaces security_enhanced.S). Informational
         only; anything the MMU needed moved to 4.5.
    15.  First normal boot message, over the virtual UART.
    16.  bl kernel_main from the high-entry stub. There is no return path to
         _start (today's path ends in a `ret` through a clobbered x30).

