The current bootpath is overengineered. I realized this is not because it is required but was accumulated during the debugging sessions where "something didn't work" happened too many times so I kept adding prints, extra barriers, re-writes as "fixes" for that stuck.

Here is a newly proposed version:

_start:
    0.  Ensure linker base matches QEMU load address
    1.  Set SP from linker-provided boot_stack_top (adrp/add)
    2.  Zero BSS
    3.  Build TTBR0 identity map (bump allocator in C)
         — must cover: code through Step 10, stack, page tables
    4.  Build TTBR1 kernel map (bump allocator in C)
         — must cover: .text, .rodata, .data, .bss, stack,
           vector table, UART device, page table pages
    5.  Configure MAIR, TCR once
    5.5 Compute full SCTLR_EL1 value (M|C|I|SA — not read-modify-write)
    5.6 Write TTBR0, TTBR1 once
    6.  tlbi vmalle1 ; dsb nsh ; isb
    7.  msr sctlr_el1, x0 (complete known value)
    8.  isb ; ic iallu ; dsb nsh ; isb
    --- MMU on, PC still identity-mapped ---
    9.  (verification / no-op if TTBR1 was pre-built)
    10. br to high virtual address (orr with HIGH_VIRT_BASE, or ldr =symbol)
    --- now running via TTBR1 ---
    10.5 Set SP to virtual stack address
    11. Set VBAR_EL1 to virtual vector table ; isb
    12. Switch UART base to virtual address
    13. Init PMM / memory subsystem
    14. Check CPU features
    15. Print first normal boot message
    16. Enter kernel_main
