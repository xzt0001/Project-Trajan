Here are the most up to date info about my development progress focusing on low level debugging. 

October 20th, 2025

To address stucking at TLOW marker in trampoline.S, I initially thought about doing something like this "
mmu_trampoline_low:
    // Save registers
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!

    // Debug: TLOW marker (pre-MMU, physical UART OK)
    mov x19, #0x09000000
    mov w20, #'T'; str w20, [x19]
    mov w20, #'L'; str w20, [x19]
    mov w20, #'O'; str w20, [x19]
    mov w20, #'W'; str w20, [x19]
    mov w20, #'\r'; str w20, [x19]
    mov w20, #'\n'; str w20, [x19]

    // Bootstrap policy (allow both TTBR walks)
    bl  mmu_policy_set_epd_bootstrap_dual

    // Enable MMU (includes ISB inside your helper)
    bl  mmu_enable_translation

    mov x19, #0x09000000
    mov w20, #'M'; str w20, [x19]
    mov w20, #'+'; str w20, [x19]
    mov w20, #'\r'; str w20, [x19]
    mov w20, #'\n'; str w20, [x19]

    // Build HIGH_VIRT_BASE = 0xFFFF800000000000
    movz x21, #0,      lsl #0
    movk x21, #0x8000, lsl #32
    movk x21, #0xFFFF, lsl #48

    // Compute high alias of trampoline_high and branch
    adrp x20, mmu_trampoline_high
    add  x20, x20, :lo12:mmu_trampoline_high
    orr  x20, x20, x21
    br   x20

mmu_trampoline_high:
    // Debug: THIGH (OK if UART mapped or you keep a PA window)
    mov x19, #0x09000000
    mov w20, #'T'; str w20, [x19]
    mov w20, #'H'; str w20, [x19]

    bl  mmu_trampoline_continuation_point   // VBAR→TTBR1, EPD0=1, etc.

    // Restore and return
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    ret
    "
**This approach will likely fail.

Cause this snippet attempts to call C functions from mmu_policy.C from the identity-mapped trampoline code. However, these functions live in the general .text section, which is NOT identity-mapped in TTBR0.

**What happens
1. Trampoline executes at 0x4008f000 (identity-mapped)
2. bl mmu_policy_set_epd_bootstrap_dual branches to ~0x40085xxx
3. CPU attempts instruction fetch from 0x40085xxx
4. TTBR0 page table lookup fails (no mapping exists)
5. Hang

**Root Cause: The TTBR0 identity mapping setup in memory_core.c only maps:
- Assembly block (inline asm)
- .text.tramp section (trampoline)
- Vector table
- Stack
- UART device memory

But does NOT map:
- General .text section (where C functions compile to)
- .rodata, .data, .bss (where C functions access data)

**Correct Solution next would likely involve inline the necessary register operations directly in trampoline.S assembly - keep the trampoline self-contained with no external function calls.

October 8th, 2025

Context (post-VBAR work): VBAR set to physical/identity early, then switched to TTBR1 virtual in the trampoline continuation. Also dual-mapped the vector table (TTBR0 low + TTBR1 high).
Fixes done:
	•	Replaced hard-coded vector check (0x01000000) with mrs vbar_el1 (probe follows VBAR).
	•	Added map_vector_table_dual() and early VBAR=0x41000000 write; planned late VBAR write to TTBR1 high.
	•	Corrected trampoline size (address vs value bug) and clamped/align-up; removed 1 GB accidental mapping.
	•	Fixed register lifetime across inline asm (re-seeded TTBR base regs; breadcrumb proved progress past VEC).

Current issue: Reaching trampoline.S and print TLOW, then stall. 

Probable root cause: branching to the high-VA alias before MMU is enabled. With M=0, 0xFFFF8000… isn’t fetchable → no THIGH/continuation.

Latest kernel log: https://docs.google.com/document/d/14vs0qtIwmZPpFn26GG6p2ZGD71Ivu3BI98CK_V4OLVw/edit?usp=sharing 

September 26th, 2025

Successfully get rid of the massive hex dump. Now the problem appears to be an address mismatch of vector table.

I initially tried to create a dual mapping which created two copies of the vector table at the wrong address:
TTBR0 mapping: 0x41000000 → vector table
TTBR1 mapping: 0xFFFF800041000000 → vector table

But CPU still jumps to 0x01000000 where nothing exists.

Next step would likely involve repointing VBAR to where I mapped.

Latest kernel log: https://docs.google.com/document/d/1RIj3WxnEKWB1h_YYPwYR_e2hH1mMqQPRrA4Nok5xhRc/edit?usp=sharing 

September 20th 2025

I implemented a fix which lets the CPU flip the MMU on while already executing in TTBR1 (via the dual-mapped trampoline and EPD staging). I believe this removes the old “fetch from a disabled TTBR0” crash, so the boot now advances into memory-management code that never ran before. That code prints a line for thousands of pages (PMM scans, mapping dumps, etc.), which is why the UART is now flooded with hex addresses.

Latest kernel log: https://docs.google.com/document/d/1tQynkcX1iV-GyAi0jAjUIVZ2a1Xaqrk6yiAQTPsdmeA/edit?usp=sharing

September 16th 2025

Identified a fundamential mismatch: executing with PC in TTBR0 half(low VA) while policy disabbles TTBR0(EPD0 = 1) and keep TTBR1 enabled for the kernel.

Evidence:
Critical PC before MMU enable: CPC:B68C
Source: Assembly code in memory_core.c: 472-519 showing the lower 16 bits of the PC, which based on assembly block location, full address should be 0x4008B68C, and that's also TTBR0's address space.

Latest kernel log: https://docs.google.com/document/d/12J2vBiTDT0yyNiPk4ZhkNPDOOEs0AsMACsI1mfxeC1Q/edit?usp=sharing 

September 8th 2025

TLBI centralization across memory folder, all refering to mmu_comprehensive_tlbi_sequence.

Latest kernel log: https://docs.google.com/document/d/1AjBIK2MI9r64eAikBE8lx5qIRo5jlnKyDbn_3F-DktM/edit?tab=t.0

September 4th, 2025

Fixing MMU policy violations. Breaking up assembly block from mmu_core.c into pieces.

Latest kernel log: https://docs.google.com/document/d/1T9nySCFBoppp0Ei_IczjAONa6gPZYs4AF7WuI8IexnI/edit?usp=sharing

August 22nd, 2025

Several potential root causes that I just RULED OUT:

1. EL2 Banking Issues:
Confirmed at EL1, not EL2
SCTLR_EL1 is the correct register to write

2. No SCTLR_EL12 confusion:
Multiple Write Conflicts
Single writer (x28) confirmed
No retry loop interference
Clean execution path

3. Pre-Existing MMU State:
M bit = 0 before write (confirmed)
Clean MMU disabled baseline

4. Post-Write Verification Issues:
Hang occurs before readback
Problem is at the write instruction itself

Latest kernel log: https://docs.google.com/document/d/1_gwPRT80cPFGeaXvLu79bVNlKqz7B6o1xCJNlQP1Z04/edit?usp=sharing

August 15th 2025 (Continue from July 27th)

Started with DAIF register invevstigation. The kernel log should show DAIF1111, which means all interrupts disabled, but I only got 0000.

Next I tried multiple cpu configurations in run_nographic.sh, a53, a72, a76 and max mode. The result is all of them showed identical hang patterm.

Then I tried acceleration mode testing using -accel tcg, thread=single - smp 1, virtual machine with -m virt, and raspherry Pi 3B emulation, all ended with same hang pattern.

Then I used docker environment testing multiple QEMU version, turns out they were hanging at early boot, that includes QEMU 4.2.1 and QEMU 6.2.0. Which confirms my original assumption that my current QEMU environment(9.2.2) has regression bug is false.

But after I look closer, I identified a DAIF measurement bug:
Root Cause
The code is reading DAIF flags from wrong bit positions:
Current (WRONG): Extracting bits [3:0] from DAIF register
Correct: DAIF flags are in bits [9:6] per ARM Architecture Reference Manual

After fixing the following locations:
Lines 803-805: Early DAIF verification
Lines 881-907: First detailed DAIF readout
Lines 973-1005: Post-interrupt-disable DAIF readout
Lines 1008-1052: DAIF comparison section

The latest kernel log below shows "DAIF1111", meaning interrupts are properly diabled. This also meant the elimination of "interrupts enabbled during MMU" theory I had before.

At this point, it is still hanging at LOOP marker, which currently on line 1118 in memory_core.c. 

Next steps would likely involving using real hardware like Raspherry Pi 5.

Latest kernel log: https://docs.google.com/document/d/18_WSzjycW-qaWhEfDN25P8jBBw-6B7YBRBf6NW0O0AI/edit?usp=sharing

July 29th 2025

Fixed early BSS dependency violation in start.S. Check out line 34-75.

Benefits of This Fix:
1. Eliminates BSS Dependency Violation: No C functions called before BSS initialization
2. Maintains Identical Functionality: Still prints "S0x40800000" format
3. Zero Global Dependencies: Uses only registers and immediate values

Kernel log remains the same after the fix.

Latest kernel log: https://docs.google.com/document/d/1-g-1TRH4TXnBPG7qMkAmbQU5yldJXDIKJYEyyG_YLyw/edit?usp=sharing

July 27th 2025

I added a timeout mechanism that should work like this:
"
Iteration 1: msr sctlr_el1, x28 → interrupt fires → MMU bit = 0
Iteration 2: msr sctlr_el1, x28 → interrupt fires → MMU bit = 0  
Iteration 3: msr sctlr_el1, x28 → interrupt fires → MMU bit = 0
...
Iterations 1000+: Eventually MMU enables between interrupts → SUCCESS
Timeout Scenario:
1M iterations completed → timeout handler → try alternative approach
"
But the latest kernel log shows the system hangs immediately after LOOP marker after line 1106 in memory_core.c.

This confirms the hang is almost certain a hard CPU halt, not an infinite loop, because:
1. No timeout counter decrements (would see progress markers)
2. No retry attempts (would see . progress dots)
3. Complete instruction pipeline freeze

Next steps would likely involve:
1: Change the cpu model
2: Try the latest QEMU

Latest kernel log(Check out the last line): https://docs.google.com/document/d/10g2wIC9Cj6006UDV5GZkEnE-ULnX7lZQNwPRckBmGAg/edit?usp=sharing

July 23rd 2025

At this point, there is a high likelihood that the hang is due to QEMU DAIF emulation bug.

The diagnostic approach is like this(check out line 954-1073 in memory_core.c):
1. Hypothesis Formation - "Interrupts cause MMU hang"
2. Test Design - "Disable interrupts before MMU enable"
3. Implementation - Perfect assembly code with verification
4. Result Analysis - Detailed bit-level state inspection
5. Conclusion - "QEMU DAIF emulation is broken"

Latest kernel log, check out the last line specifically: https://docs.google.com/document/d/1kc7L14QY8x5WJ7C-4yPWLIV22OvLXRxhYVEEZJLzybc/edit?usp=sharing

July 21st 2025

After adding more debug code in memory_core.c, the most likely root causes for the hang are:
1. Hardware/QEMU issue
2. Interrupt interference
3. Memory coherency
4. QEMU MMU emulation bug

Latest kernel log: https://docs.google.com/document/d/1bxsN9yYKinFYpxTyc8XGWqoaPOV4PuG_CuRb1vpQQ2Q/edit?usp=sharing

July 9th 2025

It appears the problem now is the mmu enable instruction itself.

Possible Root Causes:
QEMU MMU emulation bug - The msr sctlr_el1 instruction with M bit hangs
Page table walker failure - Hardware can't access page tables
Physical memory alignment issue - Page tables not properly aligned
Cache coherency deadlock - MMU enable triggers cache coherency issue

At this point I'm considering a temporary MMU bypass, this would allow continued development while investigating the root cause.

Latest kernel log: https://docs.google.com/document/d/1qwpcVBwSc3F_KNT4loUSGIxl3UQ8Twg6rhktmqaXZ7w/edit?usp=sharing

June 24th 2025

Current Status, still hanging at MMU enablement on line 3214.

Summary of major fixes since last update:
- Addressed the identity mapping gap problem for the assembly block
- Conducted a cache flush for page table coherency issue

Problem that might cause the hang: see lines 2828-2832 in vmm.c, this brute force loop is overkill and might accidentally cleaned/invalidated data cache.

Latest kernel log: https://docs.google.com/document/d/1Kfx7aKpJ1KYf1kqv4Sp5kyj2wPTBvAJTLmVy7pv_Wl8/edit?usp=sharing

June 21th 2025

Current status: still debugging issues during MMU enablement, which currently on line 3081.

Summary of major fixes since last update: 
Canonical address and high virtual fixes, 
UART attribute clean up, 
UART MMIO mappings, 
L0 page table mapping.

Potential major root causes to investigate:
1. Identity mapping gap Problem
2. Page Table Coherency issues involving two TTBR tables.
3. Incorrect SCTLR_EL1 manipulation (line 3073-3075 in vmm.c)

Latest kernel log: https://docs.google.com/document/d/1eFv0iJ2kfNLMoQsiNg1mksoyKuIfnK7iKR_ab1NhAe8/edit?usp=sharing 

June 18th 2025

Current status: I'm debugging a critical issue during MMU enablement. The system halt as the  "msr sctlr_el1, x23\n" execute on line 2966 in vmm.c. I have ruled out a series potential errors, see debugging code from line 2734 up to 2964. At this point, I believe the probable root causes are most likely architectural violation that ARMv8 hardware caches and rejects, rather than a sutle mapping error.

Potential root causes to investigate:
1. TCR_EL1 configuration error
2. TTBR1/TTBR0_EL1 point to the same page table
3. Page Table physical address issue(likely not an issue, but needs to investigate)
4. MAIR_EL1 attribute conflicts

Latest kernel log, check out the last line: https://docs.google.com/document/d/1VL6uaqKs2RUYrOm3Hw5ph9sTu_uVRY9kTUl0B0ipDZw/edit?usp=sharing

June 15th 2025

Major problems solved:

1: Debugging Infrastructure Crisis
Problem: uart_puts_early() and all string output functions were hanging the system
Root Cause: String manipulation functions fundamentally broken during early boot
Solution:
Systematic conversion to direct UART character output (*uart = 'X')
Eliminated all string function dependencies in critical MMU code
Result: System progressed from F:ENAB to detailed step-by-step output

2: TLB Invalidation "Rug Pull" Crisis
Problem: Aggressive TLB/cache invalidation sequence causing system hang
Original "Rug Pull" Code:
asm volatile(
        "dsb ish\n"             // Data synchronization barrier
        "tlbi vmalle1is\n"      // Invalidate all TLB entries for EL1
        "tlbi alle1is\n"        // Invalidate all TLB entries including EL0
        "dsb ish\n"             // Wait for TLB invalidation
        "ic iallu\n"            // Invalidate instruction cache
        "dsb ish\n"             // Wait for instruction cache invalidation
        "isb\n"                 // Instruction synchronization barrier
        ::: "memory"
    );
I then implemented a more conservative approach, see line 2998-3021 in vmm.c
Result: System progressed from hanging in TLB invalidation to completing verification phase

3: Page Table Architecture
Achievements:
L0 Page Table: Successfully allocated at 0x40000000 with proper alignment
Multi-level Page Tables: L0→L1→L2→L3 hierarchy working correctly
Auto-creation: Missing page table levels automatically created on-demand
Cache Maintenance: Proper cache line flushing for all page table updates
Memory Mapping Verification: All critical mappings verified and auto-fixed

4: 7-Stage Conservative MMU Enable Sequence
Problem: No visibility into exactly where MMU enable was failing
Solution: Implemented staged approach with individual debug markers (MMU:1234567)
Result: Pinpointed exact hang location to specific instruction level

5: MMU Hardware Enable SUCCESS
Problem: Uncertain if MMU enable instruction (msr sctlr_el1, x23) actually worked
Breakthrough: System reaches MMU:1234 proving:
Stage 1-3: All pre-MMU barriers successful
Stage 4: msr sctlr_el1, x23 MMU ENABLE INSTRUCTION WORKS.
Impact: Proved MMU hardware enablement is successful, system transitions to virtual addressing mode

6: Virtual Addressing Transition Challenge (Current Issue)
Problem: System hangs at Stage 5 (mov w27, #'5') - first instruction executed in virtual addressing mode
Root Cause: UART address context issue - x26 register contains physical address (0x09000000) but system now needs virtual address (0xFFFF000009000000)

Most recent kernel log: https://docs.google.com/document/d/13BAfn4JgYcijhX6pSabx4WHetFR293TtGkhGdvUXdec/edit?usp=sharing 

June 9th 2025

**TLB Invalidation Fix and Dual Mapping Success**

**Root Cause Identified:** The previous MMU hang was caused by inner shareable TLB invalidation operations (`tlbi vmalle1is`, `dsb ish`) attempting to coordinate with non-existent cores in single-core QEMU emulation. The system was waiting indefinitely for acknowledgments from imaginary cores.

**Fix Applied:** Replaced inner shareable operations with system-wide operations(line 1639-1647 in vmm.c):
- `dsb ish` → `dsb sy` (system-wide data synchronization) 
- `tlbi vmalle1is` → `tlbi vmalle1` (local core TLB invalidation)
- Removed `tlbi alle1is` (unnecessary aggressive invalidation)

**Progress Achieved:**
- **Dual address space mapping successful**: identity, virtual, stack all complete
- **Critical function verification passed**: F0:ID+VI+, F1:ID+VI+, F2:ID+VI+ (all functions mapped in both address spaces)
- **TLB invalidation working**: TLB: → TLB:OK → PH1:COMP 
- **Reached MMU enable call**: F:ENAB marker from `init_vmm_impl()` before calling `enable_mmu_enhanced()`

**Current Status:** System successfully completed all page table setup phases (PH1:COMP) and reached the final step in `init_vmm_impl()` where it outputs F:ENAB marker. However, the system hangs immediately when attempting to call `enable_mmu_enhanced()` function.

Here is the most recent kernel log: https://docs.google.com/document/d/14IAhEB3iEmnMqrUpoDBubB-cMhbpbuhfoaIggEbS7HQ/edit?usp=sharing

June 4th, 2025

The problem I'm facing now is my recent "fixes" broke the MMU that was working. In Chinese you can call it as "牙膏倒吸“. 

**Regression Analysis:** The working MMU system (which successfully enabled and reached post-MMU execution showing "FIXX:" output) was broken by oversimplification attempts. Key destructive changes include: (1) **removing the dual mapping strategy** - eliminated the critical high virtual mappings (0xFFFF...) while keeping only identity mapping, (2) **eliminating the fallback branch mechanism** in enable_mmu_enhanced() assembly - replaced complex virtual→physical fallback with single branch attempt, (3) **removing auto-fixing of executable permissions** and comprehensive verification functions that checked and corrected PTE flags. Notably, the dynamic address calculation logic was preserved (min/max function address calculation still intact in lines 1478-1489). The system now hangs during page table construction (E:TRAN phase) before even attempting MMU enable, whereas previously it successfully enabled MMU and executed post-MMU code (just to wrong location). This represents a significant regression from a working MMU with fixable branch target issues to complete pre-MMU failure. The missing dual mapping (virtual mapping should be added after line 1513 in map_mmu_transition_code()) is the primary suspect for the regression.

Here is the most recent kernel log: https://docs.google.com/document/d/1WdYOuM_NnSHNwliC6isXkV2X45yxn2BLoFTcehQ4thU/edit?usp=sharing 



June 2nd, 2025

Problem: Silent Hang After MMU Enable
Pattern: System reaches F:ENAB then complete silence - no virtual UART output, no continuation point markers, no fallback paths.

See most recent kernel log: https://docs.google.com/document/d/1BbZrTNEqS_fjCiEeWsVvu3WbqbJn4As8ym2DfSzaMAM/edit?usp=sharing

The primary suspect as for now is - Instruction Fetch failure After MMU Enablement
The Problem: After msr sctlr_el1, x0 enables MMU at line 1376 in vmm.c, the CPU immediately needs to fetch the next instruction using virtual addressing. But the current PC (Program Counter) is still a physical address.
Code Analysis:
	"msr sctlr_el1, x0\n"        // ← MMU enabled HERE
	// CPU now expects ALL addresses to be virtual, including PC
	"dsb sy\n"                   // ← This instruction needs virtual fetch
	"isb\n"                      // ← This instruction needs virtual fetch  
	"mov x0, #'M'\n"             // ← This instruction needs virtual fetch
The Issue: The instructions after MMU enable are physically addressed in the binary, but CPU now expects them to be virtually addressed.