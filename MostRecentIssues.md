Here are the most up to date info about my development progress focusing on low level debugging. My medium blog posts are hard to write, cause I have to dilute over 100 pages debugging journal into a 5mins read blog post. I will post regular updates here about the most up to date problems that I'm dealing with. 

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