# CustomOS Verified Execution Path

## Classification Key

| Tag              | Meaning |
|------------------|---------|
| **[PROVEN]**     | Direct branch/call/label/function visible in source code. |
| **[LIKELY]**     | Inferred from nearby code but not fully proven (e.g. depends on mapping logic working correctly). |
| **[UNVERIFIED]** | Depends on linker behaviour, runtime state, symbol address, macro expansion, config flag, or build output. |
| **[CONTRADICTION]** | Appears inconsistent with the code or ARM64 architecture. |

---

## Phase 0 — QEMU Load

### [Step 0] QEMU loads `build/kernel8.img`

**[UNVERIFIED]**

| Fact | Source | Status |
|------|--------|--------|
| Image is a flat binary | `Makefile:125` — `$(OBJCOPY) -O binary build/kernel.elf build/kernel8.img` | Proven |
| Entry symbol is `_start` | `boot/linker.ld:2` — `ENTRY(_start)` | Proven |
| Linker VMA starts at `0x80000` | `boot/linker.ld:15` — `. = 0x80000` | Proven |
| QEMU scripts use `-M virt -kernel build/kernel8.img` | `scripts/run_nographic.sh`, `debug.sh`, etc. | Proven |
| **Actual CPU load address is `0x80000`** | — | **Unverified** |

**Missing evidence:**

QEMU `virt` for AArch64 places RAM at `0x40000000`. With `-kernel` and a
raw binary (`objcopy -O binary` strips ELF headers), QEMU loads the image
using the Linux kernel boot protocol. The actual load address is typically
`0x40080000` (RAM base + 0x80000 offset), **not** `0x80000`.

If the true load address differs from the linker VMA:

- PC-relative instructions (`bl`, `adr`, `b`) still work.
- **Absolute** address loads (`ldr x0, =_bss_start`) point to wrong
  physical locations — BSS zeroing, literal-pool symbol references, etc.
  would break silently.

**To resolve:** run under GDB and inspect PC at first instruction, or
examine `qemu-system-aarch64 -d in_asm` trace output.

---

## Phase 1 — `_start` in `boot/start.S` (assembly, pre-C)

### [Step 1] `_start` — entry point

**[PROVEN]**

- `boot/start.S:1` — `.section .text.boot`
- `boot/start.S:2` — `.global _start`
- `boot/start.S:29` — `_start:` label
- `boot/linker.ld:21-23` — `.text.boot` is the first output section at
  `. = 0x80000`, containing `*(.text.boot)`.
- `Makefile:25` — `boot/start.o` is the first object in `$(OBJS)`.

First instruction executed: `mov x20, #0x09000000` (line 31).

---

### [Steps 2-6] UART hardware init

**[PROVEN]**

| Step | File:Line | Instruction | Purpose |
|------|-----------|-------------|---------|
| 2 | `boot/start.S:31` | `mov x20, #0x09000000` | Save PL011 UART base in callee-saved x20 |
| 3 | `boot/start.S:39-43` | `mov w2, #0` / `str w2, [x1, #0x30]` | Disable UART (UARTCR = 0) |
| 4 | `boot/start.S:52-56` | `str w2, [x1, #0x2C]` / `str w2, [x1, #0x30]` | 8N1+FIFO, enable TX/RX |
| 5 | `boot/start.S:58-64` | Same registers, same values | **Redundant** re-configuration (harmless) |
| 6 | `boot/start.S:67-70` | `uart_init_delay:` loop (0x10000 iters) | Busy-wait for UART stabilisation |

All sequential, no branches, correct PL011 offsets for QEMU `virt`.

---

### [Step 7] Stack pointer setup — `SP = 0x40800000`

**[PROVEN]** instruction exists. **[UNVERIFIED]** address validity.

```
boot/start.S:74  mov x0, #0x40800000
boot/start.S:75  mov sp, x0
```

- `0x40800000` = RAM base (`0x40000000`) + 8 MB.
- Stack grows downward, so it occupies addresses *below* `0x40800000`.
- On QEMU `virt` with default 128 MB RAM (`0x40000000`–`0x48000000`),
  this is within range.

**Risk:** If the kernel binary is loaded at `0x40080000` and exceeds ~7.5 MB,
stack and code could overlap. For typical kernel sizes (< 1 MB) this is safe.

---

### [Steps 8-11] Early UART diagnostics

**[PROVEN]**

| Step | File:Line | Output | Purpose |
|------|-----------|--------|---------|
| 8 | `boot/start.S:79-81` | `'S'` | Stack setup confirmation |
| 9 | `boot/start.S:94-127` | `0xNNNNNNNN` | Inline hex print of SP value (pure asm, no C) |
| 10 | `boot/start.S:130-133` | `'X'` | Execution continuation marker |
| 11 | `boot/start.S:136-138` | `'B'` | BSS init start marker |

---

### [Step 12] BSS zeroing

**[UNVERIFIED]**

```
boot/start.S:140  ldr x0, =_bss_start    // absolute address from literal pool
boot/start.S:141  ldr x1, =_bss_end      // absolute address from literal pool
boot/start.S:146  stp xzr, xzr, [x0], #16
```

- `boot/linker.ld:90-93` — `_bss_start` / `_bss_end` defined in BSS section.
- `ldr x0, =symbol` loads an **absolute** linker-assigned address from a
  literal pool.

**Missing evidence:** If the actual load address differs from the linker
VMA (`0x80000`), these absolute addresses point to the wrong physical
locations. BSS zeroing would write to unmapped or wrong memory.

---

### [Step 13] BSS complete marker

**[PROVEN]**

```
boot/start.S:151-154  Prints 'b' to UART
```

---

## Phase 2 — Modular Boot Components

### [Step 14-15] `security_feature_init`

**[PROVEN]**

| Detail | Source |
|--------|--------|
| Call | `boot/start.S:162` — `bl security_feature_init` (PC-relative) |
| Target | `boot/security_enhanced.S:21` — `security_feature_init:` |
| Section | `.text` (line 1) |
| Behaviour | Prints `<SEC>`, checks PAC via `mrs id_aa64isar1_el1`, checks MTE via `mrs id_aa64pfr1_el1`, returns via `ret` |

---

### [Step 16-19] `enhanced_debug_setup` → `test_return`

**[PROVEN]** (branch chain). **[LIKELY]** (correct execution).

| Step | Source | Detail |
|------|--------|--------|
| 16 | `boot/start.S:165` | `bl enhanced_debug_setup` (PC-relative) |
| 17 | `boot/debug_helpers.S:25-60` | Prints `M`, `s`, `1`, `x`; sets `x30` manually via `adr x30, return_label_debug`; does `b test_return` |
| 18 | `memory/pmm.c:108` | `test_return()` — `__attribute__((naked))`, body is pure inline asm. Prints `'A'`, returns via `ret` |
| 19 | `boot/debug_helpers.S:62-73` | Prints `'y'`, restores registers, returns |

`test_return` is declared in C but is `naked` — no compiler-generated
prologue. **Not a true C function.** The compiler only provides the symbol.

---

### [Step 20-21] `init_pmm` — FIRST TRUE C FUNCTION

**[PROVEN]**

```
boot/start.S:210    bl init_pmm          // PC-relative branch-and-link
memory/pmm.c:255    void init_pmm(void)  // real C function with prologue
```

- Calls `init_pmm_impl()` at `memory/pmm.c:263`.
- Initialises bitmap allocator for `0x40000000`–`0x48000000` region.
- This is provably the **first genuine C function call** from the boot path.

---

### [Step 22-23] `debug_pmm_status`

**[PROVEN]**

- `boot/start.S:213` — `bl debug_pmm_status`
- `boot/debug_helpers.S:81-112` — Prints `'2'`, `'c'`, `'V'`. Returns.

---

### [Step 24-25] `boot_state_verify`

**[PROVEN]**

- `boot/start.S:239` — `bl boot_state_verify`
- `boot/boot_verify.S:24-126` — Prints SP via `bl uart_puthex` (C call,
  safe after BSS init), verifies stack range `0x40000000`–`0x50000000`,
  forces 16-byte alignment via `bic x3, x3, #0xF`. Returns.

---

### [Step 26-27] `vector_table_setup` — VBAR_EL1

**[CONTRADICTION]**

- `boot/start.S:291` — `bl vector_table_setup` — proven (PC-relative).
- `boot/vector_setup.S:42-43`:

```
adrp x0, vector_table           // page address (PC-relative to VMA)
add  x0, x0, :lo12:vector_table // exact VMA address
```

**The problem:**

| Property | Value | Source |
|----------|-------|--------|
| `vector_table` **VMA** | `0x1000000` | `boot/linker.ld:50-52` — `. = 0x1000000` |
| `vector_table` **LMA** | `_vector_table_load_start` ≈ `0x89000` | `boot/linker.ld:47,51` — `AT(_vector_table_load_start)` |
| What `adrp`/`add` computes | The **VMA** (`0x1000000`) | `adrp` uses linker-assigned PC-relative offset based on VMAs |

`adrp`/`add` resolves to VMA `0x1000000`, but the vector table **data**
physically resides at LMA ~`0x89000` (immediately after `.text` in the
binary). VBAR_EL1 is set to `~0x1000000`, which is **wrong** — any
exception before MMU enable jumps to garbage.

**Corroborating evidence:** `kernel/init/main.c:230` later calls
`write_vbar_el1(0x89000)`, treating `0x89000` as the correct physical
address. This contradicts what `vector_table_setup` wrote.

---

### [Step 28] `bl init_vmm` — VMM/MMU sequence (NO RETURN)

**[PROVEN]** the call exists. **[CONTRADICTION]** regarding control flow.

```
boot/start.S:334    bl init_vmm
```

- `memory/vmm.c:995` — `init_vmm()` exists.
- `memory/vmm.c:1029` — calls `enable_mmu(l0_table)`.
- `memory/vmm.c:1031-1032` — comment: *"We should never reach here since
  we branch to mmu_continuation_point in enable_mmu"*.
- `enable_mmu()` → `enable_mmu_enhanced()` → `asm volatile("b mmu_trampoline_low")`
  — **never returns**.

**Consequence:** Everything after `start.S:334` is **unreachable**:

```
start.S:337  bl debug_vmm_status      ← DEAD CODE
start.S:364  bl final_verification    ← DEAD CODE
start.S:375  bl kernel_main           ← DEAD CODE
```

Execution continues inside `init_vmm` → `enable_mmu` →
`enable_mmu_enhanced` → trampoline, **not** back to `start.S`.

---

## Phase 3 — MMU Enablement (inside `enable_mmu_enhanced`)

### [Step 29-34] `init_vmm` internals

**[PROVEN]** — all calls visible in `memory/vmm.c:995-1029`.

| Step | File:Line | Function | Purpose |
|------|-----------|----------|---------|
| 29 | `vmm.c:1000` | `init_vmm_impl()` | Sets up 4-level page tables for TTBR0 and TTBR1 |
| 30 | `vmm.c:1004` | `map_vector_table()` | Maps vector table for post-MMU |
| 31 | `vmm.c:1009` | `map_uart()` | Maps UART at virtual address `0xFFFF800009000000` |
| 32 | `vmm.c:1014` | `map_mmu_transition_code()` | Identity-maps `enable_mmu` code region |
| 33 | `vmm.c:1019` | `audit_memory_mappings()` | Audits for overlapping mappings |
| 34 | `vmm.c:1024` | `verify_code_is_executable()` | Verifies PXN bits clear on code pages |

---

### [Step 35-36] `enable_mmu` → `enable_mmu_enhanced`

**[PROVEN]**

```
vmm.c:1029            enable_mmu(l0_table)
vmm.c:574             enable_mmu_enhanced(page_table_base)
memory_core.c:271     void enable_mmu_enhanced(...)
memory_core.c:280     #define TEST_POLICY_APPROACH 0  → takes #else path
```

---

### [Step 37-40] Policy-layer register configuration

**[PROVEN]** calls exist. **[UNVERIFIED]** actual register values.

| Step | File:Line | Call | Policy Implementation |
|------|-----------|------|----------------------|
| 37 | `memory_core.c:359` | `mmu_configure_tcr_bootstrap_dual(48)` | `mmu_policy.c:128` — sets EPD0=0, EPD1=0 |
| 38 | `memory_core.c:366` | `mmu_configure_mair()` | `mmu_policy.c:48` — writes MAIR_EL1 |
| 39 | `memory_core.c:373` | `mmu_set_ttbr_bases(ttbr0, ttbr1)` | `mmu_policy.c:366` — writes TTBR0/TTBR1_EL1 |

TTBR0/TTBR1 L0 table physical addresses are allocated by `alloc_page()` at
runtime from the `0x40000000`–`0x48000000` region. Exact addresses unknown
without execution.

---

### [Step 41-44] Identity mapping and dual trampoline mapping

**[LIKELY]**

| Step | File:Line | What | TTBR |
|------|-----------|------|------|
| 41 | `memory_core.c:448` | Identity-map assembly block around `mmu_enable_point` | TTBR0 |
| 42 | `memory_core.c:501` | `map_range_dual_trampoline(...)` — dual-map `.text.tramp` | TTBR0 + TTBR1 |
| 43 | `memory_core.c:529` | `map_vector_table_dual(...)` — dual-map vector table | TTBR0 + TTBR1 |
| 44 | `memory_core.c:535-538` | Pre-enable barriers + full TLB invalidation | — |

**Trampoline dual-map detail** (`memory_core.c:256-262`):

- TTBR0: `map_range(l0_ttbr0, tramp_phys, tramp_phys + size, tramp_phys, PTE_KERN_TEXT)`
  → identity map
- TTBR1: `map_range(l0_ttbr1, HIGH_VIRT_BASE + tramp_phys, ... + size, tramp_phys, PTE_KERN_TEXT)`
  → high virtual map

**Missing evidence:** correctness depends on `map_range()` properly
handling TTBR1 high addresses (stripping canonical bits, correct L0
indexing for `0xFFFF8000XXXXXXXX`).

---

### [Step 45-49] Assembly blocks and diagnostics

**[PROVEN]**

| Step | File:Line | Content |
|------|-----------|---------|
| 45 | `memory_core.c:541-595` | First asm block: loads x19-x24 with config values |
| 46 | `memory_core.c:602-611` | Policy layer re-applies MAIR, TCR, TTBR from C |
| 47 | `memory_core.c:626-672` | Second asm block: cache flush via `dc cvac` + `dsb sy` |
| 48 | `memory_core.c:687` | `mmu_comprehensive_tlbi_sequence()` — full TLB invalidation |
| 49 | `memory_core.c:692-967` | Third asm block: barriers, VBAR/TTBR verification. **All `msr sctlr_el1, x23` commented out (DEAD CODE)** |

---

### [Step 50] `b mmu_trampoline_low` — NO RETURN

**[PROVEN]**

```
memory_core.c:2154    asm volatile("b mmu_trampoline_low")
```

- PC-relative unconditional branch to `memory/trampoline.S:33`.
- Not `bl` — no return address saved. Execution never returns to
  `enable_mmu_enhanced`.

---

## Phase 4 — Trampoline: The Actual MMU Enable

### [Step 51-52] Trampoline entry + pre-MMU barriers

**[PROVEN]**

```
trampoline.S:33   mmu_trampoline_low:   // saves registers, prints "TLOW"
trampoline.S:61   dsb sy
trampoline.S:62   isb
```

---

### [Step 53-55] THE MMU ENABLE

**[PROVEN]**

```
trampoline.S:71   mrs  x0, sctlr_el1      // read current SCTLR_EL1
trampoline.S:79   orr  x0, x0, #0x1       // set M bit (bit 0)
trampoline.S:87   msr  sctlr_el1, x0      // *** MMU IS NOW ENABLED ***
```

This is the **only non-commented `msr sctlr_el1` that sets the M bit** in
the entire codebase. All others in `memory_core.c` are commented out as
DEAD CODE.

---

### [Step 56-58] Post-MMU barriers and verification

**[PROVEN]**

```
trampoline.S:96-97     dsb sy / isb          // mandatory per ARM ARM
trampoline.S:110-112   mrs x0, sctlr_el1     // read back
                       and x0, x0, #0x1      // check M bit
                       cbnz x0, 1f           // branch if M=1 (success)
trampoline.S:129       b .                   // halt on failure (unreached if ok)
trampoline.S:137-144   Prints "M+"           // MMU verification passed
```

---

### [Step 59] `HIGH_VIRT_BASE` construction

**[PROVEN]**

```
trampoline.S:160  movz x21, #0x8000, lsl #32     // 0x0000_8000_0000_0000
trampoline.S:161  movk x21, #0xFFFF, lsl #48     // 0xFFFF_8000_0000_0000
```

Matches `HIGH_VIRT_BASE` = `0xFFFF800000000000` defined in
`include/memory_config.h:58` and `include/uart.h:44`.

---

### [Step 60-61] TTBR0 → TTBR1 PC transition

**[LIKELY]**

```
trampoline.S:164  adr x20, mmu_trampoline_high   // PC-relative → physical addr
trampoline.S:165  orr x20, x20, x21              // create high virtual address
trampoline.S:181  br  x20                         // jump to TTBR1 address space
```

- `adr` gives the physical (runtime) address; `orr` with
  `HIGH_VIRT_BASE` creates the TTBR1 equivalent.
- `orr` works because physical addresses on QEMU `virt` are < 4 GB —
  no bit collision with `0xFFFF800000000000`.
- `mmu_trampoline_high` is within `.text.tramp`, which was dual-mapped in
  TTBR1 at step 42.

**Unverified:** that the TTBR1 mapping is correct (depends on `map_range`
implementation handling canonical-extension addresses properly).

---

### [Step 62] UART access via high virtual address

**[LIKELY]**

```
trampoline.S:196  movz x19, #0x0900, lsl #16     // 0x09000000
trampoline.S:197  movk x19, #0x8000, lsl #32     // 0x0000800009000000
trampoline.S:198  movk x19, #0xFFFF, lsl #48     // 0xFFFF800009000000 = UART_VIRT
```

- `UART_VIRT` = `HIGH_VIRT_BASE + 0x09000000` = `0xFFFF800009000000`.
- `map_uart()` at `memory/pmm.c:766` selects `l0_table_ttbr1` for this
  address, so it should be mapped in TTBR1.
- Prints "HI" to confirm high-VA execution.

---

### [Step 63] `bl mmu_trampoline_continuation_point`

**[CONTRADICTION]**

```
trampoline.S:210              bl mmu_trampoline_continuation_point
memory/memory_core.c:2504-05  __attribute__((noinline))
                              void mmu_trampoline_continuation_point(void)
```

**The problem:**

| Property | Value |
|----------|-------|
| Function section | Default `.text` (no section attribute) |
| Mapped in TTBR0? | Yes — `map_kernel_sections()` maps `.text` in TTBR0 (`vmm.c:1465`) |
| Mapped in TTBR1? | **NO** — only trampoline (`.text.tramp`), vector table, and UART are dual-mapped |

The `bl` instruction encodes a **fixed PC-relative offset** computed from
link-time VMAs. When executed from the high virtual address of
`mmu_trampoline_high`, the branch target is:

```
target = current_high_PC + (continuation_VMA - trampoline_VMA)
       = 0xFFFF8000XXXXXXXX + small_offset
       = another 0xFFFF8000... address
```

This high virtual address for `mmu_trampoline_continuation_point` is
**not mapped** in TTBR1. At this point TCR still has EPD0=0 (TTBR0
enabled), but the address `0xFFFF8000...` falls in the TTBR1 range
(bit 55 = 1 for 48-bit VA). Since there is no TTBR1 mapping for this
function, **this should cause a translation fault**.

**Note:** The similarly-named `mmu_continuation_point()` (in `vmm.c:1038`)
is a **different, unreachable** function — it is in section
`.text.mmu_continuation` and is never called from the trampoline.

---

### `kernel_main` reachability

**[CONTRADICTION]**

| Path | Reachable? | Reason |
|------|------------|--------|
| `start.S:375` → `bl kernel_main` | **NO** | `init_vmm` (line 334) never returns |
| `mmu_continuation_point()` (vmm.c:1039) | **NO** | Never called — trampoline calls `mmu_trampoline_continuation_point` instead |
| `mmu_trampoline_continuation_point` → `kernel_main` | **NO** | Function does not call `kernel_main` (`memory_core.c:2505-2566`) |

**There is no proven code path from the trampoline to `kernel_main` in the
normal MMU-enable flow.** The boot sequence appears to terminate at or
shortly after `mmu_trampoline_continuation_point` (assuming the translation
fault at Step 63 does not occur).

---

## Summary of Critical Findings

| # | Concern | Verdict | Detail |
|---|---------|---------|--------|
| 1 | QEMU load address vs linker VMA | **UNVERIFIED** | QEMU `virt` loads at `0x40000000+`, not `0x80000`. Absolute address refs (`ldr x, =sym`) would be wrong. |
| 2 | `_start` placement | **PROVEN** | First in `.text.boot`, matches `ENTRY(_start)`. |
| 3 | SP = `0x40800000` validity | **UNVERIFIED** | Within QEMU RAM but could overlap kernel if binary is large. |
| 4 | First true C call | **PROVEN** | `bl init_pmm` at `start.S:210` → `pmm.c:255`. |
| 5 | VBAR_EL1 physical vs virtual | **CONTRADICTION** | `vector_table_setup` writes VMA `0x1000000` via `adrp`/`add`; data lives at LMA `~0x89000`. |
| 6 | TTBR0/TTBR1 L0 table addresses | **UNVERIFIED** | Allocated by `alloc_page()` at runtime; values unknown. |
| 7 | Trampoline identity-mapped (TTBR0) | **LIKELY** | `map_range_dual_trampoline` maps `phys → phys` in TTBR0. |
| 8 | Trampoline mapped in TTBR1 | **LIKELY** | `map_range_dual_trampoline` maps `HIGH_VIRT_BASE+phys → phys` in TTBR1. |
| 9 | UART VA after MMU | **LIKELY** | `map_uart()` maps `UART_VIRT` in TTBR1 via `l0_table_ttbr1`. |
| 10 | `mmu_trampoline_continuation_point` reachable | **CONTRADICTION** | In `.text`, not dual-mapped in TTBR1. `bl` from high VA targets unmapped address. |
| 11 | `mmu_continuation_point` reachable | **CONTRADICTION** | Different function; never called from trampoline. |
| 12 | `kernel_main` reachable | **CONTRADICTION** | No proven path from trampoline to `kernel_main`. |
