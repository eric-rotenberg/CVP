/*

Copyright (c) 2019, North Carolina State University
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. The names “North Carolina State University”, “NCSU” and any trade-name, personal name,
trademark, trade device, service mark, symbol, image, icon, or any abbreviation, contraction or
simulation thereof owned by North Carolina State University must not be used to endorse or promote products derived from this software without prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

// Author: Eric Rotenberg (ericro@ncsu.edu)


#include <map>
#include "spdlog/spdlog.h"
#include "spdlog/fmt/ostr.h"
#include "cvp.h"
#include "stride_prefetcher.h"
using namespace std;

#ifndef _RISCV_UARCHSIM_H
#define _RISCV_UARCHSIM_H

#define RFSIZE 65	// integer: r0-r31.  fp/simd: r32-r63. flags: r64.
#define RFFLAGS 64	// flags register is r64 (65th register)

struct window_t {
   uint64_t retire_cycle;
   uint64_t seq_no;
   uint64_t addr;
   uint64_t value;
   uint64_t latency;
};

struct store_queue_t {
   uint64_t exec_cycle;	// store's execution cycle
   uint64_t ret_cycle;	// store's commit cycle
};

// Class for a microarchitectural simulator.

class uarchsim_t {
   private:
      // Add your class member variables here to facilitate your limit study.

      // register timestamps
      uint64_t RF[RFSIZE];

      // store queue byte timestamps
      map<uint64_t, store_queue_t> SQ;

      // memory block timestamps
      cache_t L1;
      cache_t L2;
      cache_t L3;

      // fetch timestamp
      uint64_t fetch_cycle;
      uint64_t previous_fetch_cycle = 0;
   
      // Modeling resources: (1) finite fetch bundle, (2) finite window, and (3) finite execution lanes.
      uint64_t num_fetched;
      uint64_t num_fetched_branch;
      fifo_t<window_t> window;
      resource_schedule *alu_lanes;
      resource_schedule *ldst_lanes;

      // Branch predictor.
      bp_t BP;

      // Instruction cache.
      cache_t IC;

      //Prefetcher
      StridePrefetcher prefetcher;
      // Instruction and cycle counts for IPC.
      uint64_t num_inst;
      uint64_t cycle;

      // CVP measurements
      uint64_t num_eligible;
      uint64_t num_correct;
      uint64_t num_incorrect;

      // stats
      uint64_t num_load;
      uint64_t num_load_sqmiss;

      uint64_t stat_pfs_issued_to_mem = 0;

      // Helper for oracle hit/miss information
      uint64_t get_load_exec_cycle(db_t *inst) const;

   public:
      uarchsim_t();
      ~uarchsim_t();

      //void set_funcsim(processor_t *funcsim);
      void step(db_t *inst);
      void output();
      PredictionRequest get_prediction_req_for_track(uint64_t cycle, uint64_t seq_no, uint8_t piece, db_t *inst);
};

#endif
