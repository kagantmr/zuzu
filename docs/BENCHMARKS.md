# zuzu benchmarks
 
All numbers measured on **Raspberry Pi 4 (BCM2711, Cortex-A72)** using the
PMU cycle counter (PMCCNTR), single-core. QEMU numbers are **not** used for
performance since QEMU does not model the PMU, cache, or TLB, and its cycle
counter is unreliable. QEMU is used only for correctness (boot, completion,
no panic).
 
Method: min-of-N over 100,000 iterations (+500 warm-up). **min** is the
best-case cost (warm cache, no IRQ/preemption mid-measurement); avg and max
include scheduler/IRQ jitter. Report min for "what the operation costs."
 
## Headline numbers (current)
 
| Metric | Cycles (min) | Notes |
| --- | --- | --- |
| `getpid` round trip | 519 | SVC entry/exit floor (cost of entering the kernel at all) |
| IPC RTT (`msg_call`, register) | 2006 | cross-thread, same address space, 0-length |
| IPC RTT (`lcall`, 32-byte payload) | 2148 | long-message path via lmsg buffer |
| context switch | ~107 | measured via bench build; instrumentation `#ifdef`'d out of production |
 
Derived:
- Long-message overhead: 2148 − 2006 = **142 cycles for a 32-byte payload**
  (the cost of the lmsg buffer copy + length handling; ~4.4 cy/byte + fixed).
- IPC decomposition: 2× kernel entry (1038) + 2× switch (214) + IPC logic (~754).
- 
## Optimization arc (IPC RTT, register, Pi4)
 
| Stage | IPC RTT | Δ | Mechanism |
| --- | --- | --- | --- |
| Baseline (eager VFP, instrumented) | 2440 | — | full D32 VFP save/restore every switch |
| + lazy VFP | 2291 | −149 | FPU disabled on switch; first FP use traps and restores. Integer/IPC path pays zero VFP cost. |
| + direct-switch + O(1) priority bitmap | 2281 | −10 | `call`→waiting-receiver hands off directly, skipping the run-queue insert + `schedule()` scan. `clz` on a priority bitmap makes `sched_pick_next` and the direct-switch guard O(1). |
| + remove instrumentation pollution | **2006** | **−275** | PMCCNTR reads + 4×`isb` per switch were inflating every measured switch. Removed from production path. |
 
Total: **2440 → 2006, ~18% reduction**, each step measured on silicon.
 
Context switch: **203 -> 107 cycles (−47%)**, primarily lazy VFP (eliminated
~75 cy/switch of VFP save/restore on the integer path).
 