# CVP Infrastructure

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
