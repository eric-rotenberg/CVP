# CVP Infrastructure

## CVP2v2 Changelog

**Preamble:** There have been many changes to the infrastructure to provide correct store data to contestants. However, the traces were initially generated with as little information as possible to fit Qualcomm's requirements for publishing the traces, and therefore some information has to be inferred in a best effort fashion. Specifically, store data is not guaranteed to be correct, even if it is correct the vast majority of the time. Similar for load addressing mode and store single vs. pair.

- Infer store pair vs. store single to provide the correct store data to the updatePredictor() API. Notably, the trace does not contain addressing mode information and it is not obvious whether store A, B, C is the instruction stp A, B, [C] (or other addressing modes using a single register) or str A, [B + C] (or other similar addressing modes using two registers). **Best effort**.
- Infer exception level as the stack pointer is not the same based on the exception level, and some traces switch between EL0 and EL1 quite often. **Best effort**.
- Infer base register update for loads :
  - Better reflects true performance as base register update would see the latency of the load while it is an ALU operation. Therefore, tight loops with base register update operation could run much slower than they should have. The base register update piece is of type **aluOp**.
  - Better reflects memory footprint as base register update would access and modify the cache state while base register update does not involve the memory hierarchy. For instance, a load pair with base update from address X would be cracked into three pieces and load 8 bytes in each, from X, X + 8 and X + 16, which is wrong as the instruction does not load from X + 16 .. X + 23.
  - **Best effort** : 
    - ldp A, B, [A] may be miscategorized as load B, [A] with base register update if the output value of A matches the offset between the input value of A and the effective address (which would be an immediate offset that is not embedded in the trace) by coincidence.
	- Another limitation is that for post-indexed with base register update addressing mode, the load piece(s) of the load instruction do not depend on the base register update piece of the instruction. However, in the current infrastructure, this false dependency is present (although it is unlikely to have significant impact as loads with base register update within loops will still be fully pipelined). This may be improved in a subsequent version by attempting to infer post- vs. pre- indexing via the base register value and the effective address.
- Provide Store Data, access size and store single/pair information to the updatePredictor() API
- Provide access size and store single/pair information to the speculativeUpdate() API
- [mypredictor.cc](./mypredictor.cc) provides an example implementation of a memory tracker that builds an image of memory from store data information passed to updatePredictor() and counts mismatches. Also note the consideration of ARM's DCZVA (Data Cache zero-out cache line by VA). Mismatches are due to  :
  - Uninitialized memory (the trace does not provide the state of memory at the start of the trace)
  - Virtual aliasing (memory does not provide physical address or any other information that can help with virtual dealiasing). This is especially visible in server-class traces that heavily involve the system.
  - Specific ARM instructions/idioms that modify memory without providing the trace with enough information to determine what memory was touched.
  - Instruction miscategorization (store pair vs. store single) and exception level change miscategorization (the wrong SP value may be stored to memory)
- Assume vector instructions always operate on both lanes. This is pessimistic from the point of view of the CVP2 infrastructure as it will generate more instructions as there is one piece for the lo lane and one piece for the hi lane of 128-bit vector operations. This was done to avoid missing some output data as the heuristic to determine hi/lo vs lo only was that the output of the hi part was 0, which is reasonable but occasionally incorrect. The consequence is an increased instruction count vs. CVP2v1 (e.g., compute_fp_9.gz). This may be improved in a subsequent version by tracking actual vector operand size via memory access size in load/store and propagating.

**Conclusions:** Due to the improved address calculation flow (load + base register update case), the speedup brought by VP is likely to decrease as the base register is usually predictable and had a _minimum_ execution latency of 3 cycles in CVPv1, while it has a _maximum_ execution latency of 1 in CVP2v2. However, the base register update is now categorized as **aluOp**, so there is no hidden "low cost" operation behind loadOp in CVP2v2.


## CVP1v1 Changelog

- New CVP Tracks (All, LoadsOnly, LoadsOnly + HitMiss Info)
- TAGE/ITTAGE branch/target predictors (vs. perfect BP in CVP1)
- Stride prefetcher at L1D (vs. no prefetcher in CVP1)
- More realistic fetch bandwidth : no fetching past taken branch (vs. fetching past taken in CVP1)
- Finite number of execution units (vs. infinite in CVP1)
- Added 128KB 8-way assoc L1I (vs. idealized L1I in CVP1)
- Increased L1D to 64KB (8-way) (vs. 32KB 4-way in CVP1)
- Includes CVP1 winner as baseline value predictor (vs. under-performing FCM-based predictor in CVP1)
- Added spdlog/fmt libraries for printing

## Examples & Tracks

See Simulator options:

`./cvp`

Running the simulator on `trace.gz`:

`./cvp trace.gz`

Running with value prediction (`-v`) and predict all instructions (first track, `-t 0`):

`./cvp -v -t 0 trace.gz`

Running with value prediction (`-v`) and predict only loads (second track, `-t 1`):

`./cvp -v -t 1 trace.gz`

Running with value prediction (`-v`) and predict only loads but with hit/miss information (third track, `-t 2`):

`./cvp -v -t 2 trace.gz`

Baseline (no arguments) is equivalent to (prefetcher (`-P`), 512-window (`-w 512`), 8 LDST (`-M 8`), 16 ALU (`-A 16`), 16-wide fetch (`16`) with 16 branches max per cycle (`16`), stop fetch at cond taken (`1`), stop fetch at indirect (`1`), model ICache (`1`)):

`./cvp -P -w 512 -M 8 -A 16 -F 16,16,1,1,1`

## Notes

Run `make clean && make` to ensure your changes are taken into account.

## Value Predictor Interface

See [cvp.h](./cvp.h) header.

## Getting Traces

135 30M Traces @ [TAMU ](http://hpca23.cse.tamu.edu/CVP-1/public_traces/)

2013 100M Traces @ [TAMU ](http://hpca23.cse.tamu.edu/CVP-1/secret_traces/)


## Sample Output

```
VP_ENABLE = 1
VP_PERFECT = 0
VP_TRACK = LoadsOnly
WINDOW_SIZE = 512
FETCH_WIDTH = 16
FETCH_NUM_BRANCH = 16
FETCH_STOP_AT_INDIRECT = 1
FETCH_STOP_AT_TAKEN = 1
FETCH_MODEL_ICACHE = 1
PERFECT_BRANCH_PRED = 0
PERFECT_INDIRECT_PRED = 0
PIPELINE_FILL_LATENCY = 5
NUM_LDST_LANES = 8
NUM_ALU_LANES = 16
MEMORY HIERARCHY CONFIGURATION---------------------
STRIDE Prefetcher = 1
PERFECT_CACHE = 0
WRITE_ALLOCATE = 1
Within-pipeline factors:
	AGEN latency = 1 cycle
	Store Queue (SQ): SQ size = window size, oracle memory disambiguation, store-load forwarding = 1 cycle after store's or load's agen.
	* Note: A store searches the L1$ at commit. The store is released
	* from the SQ and window, whether it hits or misses. Store misses
	* are buffered until the block is allocated and the store is
	* performed in the L1$. While buffered, conflicting loads get
	* the store's data as they would from the SQ.
I$: 64 KB, 4-way set-assoc., 64B block size
L1$: 64 KB, 8-way set-assoc., 64B block size, 3-cycle search latency
L2$: 1 MB, 8-way set-assoc., 64B block size, 12-cycle search latency
L3$: 8 MB, 16-way set-assoc., 128B block size, 60-cycle search latency
Main Memory: 150-cycle fixed search time
STORE QUEUE MEASUREMENTS---------------------------
Number of loads: 8278928
Number of loads that miss in SQ: 7456870 (90.07%)
Number of PFs issued to the memory system 639484
MEMORY HIERARCHY MEASUREMENTS----------------------
I$:
	accesses   = 31414867
	misses     = 683314
	miss ratio = 2.18%
	pf accesses   = 0
	pf misses     = 0
	pf miss ratio = -nan%
L1$:
	accesses   = 11380235
	misses     = 352104
	miss ratio = 3.09%
	pf accesses   = 639484
	pf misses     = 10547
	pf miss ratio = 1.65%
L2$:
	accesses   = 1035418
	misses     = 217593
	miss ratio = 21.01%
	pf accesses   = 10547
	pf misses     = 5096
	pf miss ratio = 48.32%
L3$:
	accesses   = 217593
	misses     = 28693
	miss ratio = 13.19%
	pf accesses   = 5096
	pf misses     = 1576
	pf miss ratio = 30.93%
BRANCH PREDICTION MEASUREMENTS---------------------
Type                      n          m     mr  mpki
All                31414867      28863  0.09%  0.92
Branch              4202035      22366  0.53%  0.71
Jump: Direct         664629          0  0.00%  0.00
Jump: Indirect       706742       6497  0.92%  0.21
Jump: Return              0          0  -nan%  0.00
Not control        25841461          0  0.00%  0.00
ILP LIMIT STUDY------------------------------------
instructions = 31414867
cycles       = 21670415
IPC          = 1.450
Prefetcher------------------------------------------
Num Trainings :8278928
Num Prefetches generated :640803
Num Prefetches issued :1352783
Num Prefetches filtered by PF queue :49713
Num untimely prefetches dropped from PF queue :1319
Num prefetches not issued LDST contention :713299
Num prefetches not issued stride 0 :2719377
CVP STUDY------------------------------------------
prediction-eligible instructions = 19841790
correct predictions              = 885532 (4.46%)
incorrect predictions            = 21692 (0.11%)
 Read 30002325 instrs 
```
