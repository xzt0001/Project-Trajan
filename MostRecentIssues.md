Here are the most up to date info about my development progress focusing on low level debugging. My medium blog posts are hard to write, cause I have to dilute over 100 pages debugging journal into a 5mins read blog post. I will post regular updates here about the most up to date problems that I'm dealing with. 

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