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


#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include "cvp.h"
#include "cvp_trace_reader.h"
#include "fifo.h"
#include "cache.h"
#include "bp.h"
#include "resource_schedule.h"
#include "uarchsim.h"
#include "parameters.h"

//uarchsim_t::uarchsim_t():window(WINDOW_SIZE),
uarchsim_t::uarchsim_t():BP(20,16,20,16,64),window(WINDOW_SIZE),
			 L3(L3_SIZE, L3_ASSOC, L3_BLOCKSIZE, L3_LATENCY, (cache_t *)NULL),
			 L2(L2_SIZE, L2_ASSOC, L2_BLOCKSIZE, L2_LATENCY, &L3),
			 L1(L1_SIZE, L1_ASSOC, L1_BLOCKSIZE, L1_LATENCY, &L2),
                         IC(IC_SIZE, IC_ASSOC, IC_BLOCKSIZE, 0, &L2) {
   assert(WINDOW_SIZE);
   //assert(FETCH_WIDTH);

   //setup logger
   // Set this to "spdlog::level::debug" for verbose debug prints
   spdlog::set_level(spdlog::level::info);
   spdlog::set_pattern("[%l]  %v");

   ldst_lanes = ((NUM_LDST_LANES > 0) ? (new resource_schedule(NUM_LDST_LANES)) : ((resource_schedule *)NULL));
   alu_lanes = ((NUM_ALU_LANES > 0) ? (new resource_schedule(NUM_ALU_LANES)) : ((resource_schedule *)NULL));

   for (int i = 0; i < RFSIZE; i++)
      RF[i] = 0;

   num_fetched = 0;
   num_fetched_branch = 0;
   fetch_cycle = 0;

   num_inst = 0;
   cycle = 0;
 
   // CVP measurements
   num_eligible = 0;
   num_correct = 0;
   num_incorrect = 0;

   // stats
   num_load = 0;
   num_load_sqmiss = 0;
}

uarchsim_t::~uarchsim_t() {
}

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) > (b)) ? (b) : (a))

PredictionRequest uarchsim_t::get_prediction_req_for_track(uint64_t cycle, uint64_t seq_no, uint8_t piece, db_t *inst)
{
   PredictionRequest req;
   req.seq_no = seq_no;
   req.pc = inst->pc;
   req.piece = piece;
   req.cache_hit = HitMissInfo::Invalid;


   switch(VPTracks(VP_TRACK)){
   case VPTracks::ALL:
         req.is_candidate = true;
         break;
   case VPTracks::LoadsOnly:
         req.is_candidate = inst->is_load;
         break;
   case VPTracks::LoadsOnlyHitMiss:
   {
         req.is_candidate = inst->is_load;
     
         if(req.is_candidate)
         {
            req.cache_hit = HitMissInfo::Miss;
            uint64_t exec_cycle = get_load_exec_cycle(inst);
            if(L1.is_hit(exec_cycle, inst->addr))
            {
               req.cache_hit = HitMissInfo::L1DHit;
            }
            else if(L2.is_hit(exec_cycle, inst->addr))
            {
               req.cache_hit = HitMissInfo::L2Hit;
            }
            else if(L3.is_hit(exec_cycle, inst->addr))
            {
               req.cache_hit = HitMissInfo::L3Hit;
            }
         }
         break;
   }
   default:
         assert(false && "Invalid Track\n");
         break;
   }
   return req;
}

uint64_t uarchsim_t::get_load_exec_cycle(db_t *inst) const
{
   uint64_t exec_cycle = fetch_cycle;

   // No need to re-access ICache because fetch_cycle has already been updated    
   exec_cycle = exec_cycle + PIPELINE_FILL_LATENCY;

   if (inst->A.valid) {
      assert(inst->A.log_reg < RFSIZE);
      exec_cycle = MAX(exec_cycle, RF[inst->A.log_reg]);
   }
   if (inst->B.valid) {
      assert(inst->B.log_reg < RFSIZE);
      exec_cycle = MAX(exec_cycle, RF[inst->B.log_reg]);
   }
   if (inst->C.valid) {
      assert(inst->C.log_reg < RFSIZE);
      exec_cycle = MAX(exec_cycle, RF[inst->C.log_reg]);
   }

   if (ldst_lanes) exec_cycle = ldst_lanes->try_schedule(exec_cycle);

   // AGEN takes 1 cycle.
   exec_cycle = (exec_cycle + 1);

   return exec_cycle;
}

void uarchsim_t::step(db_t *inst) 
{
   spdlog::debug("Stepping, FC: {}",fetch_cycle);

   // Preliminary step: determine which piece of the instruction this is.
   static uint8_t piece = 0;
   static uint64_t prev_pc = 0xdeadbeef;
   piece = ((inst->pc == prev_pc) ? (piece + 1) : 0);
   prev_pc = inst->pc;

 
   /////////////////////////////
   // Manage window: retire.
   /////////////////////////////
   while (!window.empty() && (fetch_cycle >= window.peekhead().retire_cycle)) {
      window_t w = window.pop();
      if (VP_ENABLE && !VP_PERFECT)
         updatePredictor(w.seq_no, w.addr, w.value, w.latency);
   }
 
   // CVP variables
   uint64_t seq_no = num_inst;
   bool predictable = (inst->D.valid && (inst->D.log_reg != RFFLAGS));
   PredictionResult pred;
   bool squash = false;
   uint64_t latency;
   // 
   // Schedule the instruction's execution cycle.
   //
   uint64_t i;
   uint64_t addr;
   uint64_t exec_cycle;

   if (FETCH_MODEL_ICACHE)
      fetch_cycle = IC.access(fetch_cycle, true, inst->pc);   // Note: I-cache hit latency is "0" (above), so fetch cycle doesn't increase on hits.

   // Predict at fetch time
   if (VP_ENABLE)
   {
      if (VP_PERFECT)
      {
         PredictionRequest req = get_prediction_req_for_track(fetch_cycle, seq_no, piece, inst);
         pred.predicted_value = inst->D.value;
         pred.speculate = predictable && req.is_candidate;
         predictable &= req.is_candidate;
      }
      else
      {
         PredictionRequest req = get_prediction_req_for_track(fetch_cycle, seq_no, piece, inst);
         pred = getPrediction(req);
         speculativeUpdate(seq_no, predictable, ((predictable && pred.speculate && req.is_candidate) ? ((pred.predicted_value == inst->D.value) ? 1 : 0) : 2),
                           inst->pc, inst->next_pc, (InstClass)inst->insn, piece,
                           (inst->A.valid ? inst->A.log_reg : 0xDEADBEEF),
                           (inst->B.valid ? inst->B.log_reg : 0xDEADBEEF),
                           (inst->C.valid ? inst->C.log_reg : 0xDEADBEEF),
                           (inst->D.valid ? inst->D.log_reg : 0xDEADBEEF));
         // Override any predictor attempting to predict an instruction that is not candidate.
         pred.speculate &= req.is_candidate;
         predictable &= req.is_candidate;
      }
   }
   else {
      pred.speculate = false;
   }
 
   exec_cycle = fetch_cycle + PIPELINE_FILL_LATENCY;

   if (inst->A.valid) {
      assert(inst->A.log_reg < RFSIZE);
      exec_cycle = MAX(exec_cycle, RF[inst->A.log_reg]);
   }
   if (inst->B.valid) {
      assert(inst->B.log_reg < RFSIZE);
      exec_cycle = MAX(exec_cycle, RF[inst->B.log_reg]);
   }
   if (inst->C.valid) {
      assert(inst->C.log_reg < RFSIZE);
      exec_cycle = MAX(exec_cycle, RF[inst->C.log_reg]);
   }

   //
   // Schedule an execution lane.
   //
   if (inst->is_load || inst->is_store) {
      if (ldst_lanes) exec_cycle = ldst_lanes->schedule(exec_cycle);
   }
   else {
      if (alu_lanes) exec_cycle = alu_lanes->schedule(exec_cycle);
   }

   if (inst->is_load) {
     
      latency = exec_cycle;	// record start of execution

      // AGEN takes 1 cycle.
      exec_cycle = (exec_cycle + 1);

      // Train the prefetcher when the load finds out its outcome in the L1D
      if (PREFETCHER_ENABLE)
      {
         // Generate prefetches ahead of time as in "Effective Hardware-Based Data Prefetching for High-Performance Processors"
         // Instruction PC will be 4B aligned.
         prefetcher.lookahead((inst->pc >> 2), fetch_cycle);

         // Train the prefetcher 
         const bool hit = L1.is_hit(exec_cycle, inst->addr);
         PrefetchTrainingInfo info{inst->pc >> 2, inst->addr, 0, hit};
         prefetcher.train(info);
      }

      // Search D$ using AGEN's cycle.
      uint64_t data_cache_cycle;
      if (PERFECT_CACHE)
         data_cache_cycle = exec_cycle + L1_LATENCY;
      else
         data_cache_cycle = L1.access(exec_cycle, true, inst->addr);

      // Search of SQ takes 1 cycle after AGEN cycle.
      exec_cycle = (exec_cycle + 1);

      bool inc_sqmiss = false;
      uint64_t temp_cycle = 0;
      for (i = 0, addr = inst->addr; i < inst->size; i++, addr++) {
         if ((SQ.find(addr) != SQ.end()) && (exec_cycle < SQ[addr].ret_cycle)) {
            // SQ hit: the byte's timestamp is the later of load's execution cycle and store's execution cycle
            temp_cycle = MAX(temp_cycle, MAX(exec_cycle, SQ[addr].exec_cycle));
         }
         else {
            // SQ miss: the byte's timestamp is its availability in L1 D$
            temp_cycle = MAX(temp_cycle, data_cache_cycle);
            inc_sqmiss = true;
         }
      }

      num_load++;					// stat
      num_load_sqmiss += (inc_sqmiss ? 1 : 0);		// stat

      assert(temp_cycle >= exec_cycle);
      exec_cycle = temp_cycle;

      latency = (exec_cycle - latency);	// end of execution minus start of execution
      assert(latency >= 2);	// 2 cycles if all bytes hit in SQ
   }
   else {
      // Determine the fixed execution latency based on ALU type.
      if (inst->insn == InstClass::fpInstClass)
         latency = 3;
      else if (inst->insn == InstClass::slowAluInstClass)
         latency = 4;
      else
         latency = 1;

      // Account for execution latency.
      exec_cycle += latency;
   }

   // Drain prefetches from PF Queue
   // The idea is that a prefetch can go only if there is a free LDST slot "this" cycle
   // Here, "this" means all the cycles between the previous fetch cycle and the current one since all fetched ld/st will have been
   // scheduled and prefetch can correctly "steal" ld/st slots.
   if(PREFETCHER_ENABLE)
   {
      uint64_t tmp_previous_fetch_cycle;
      Prefetch p;
      bool issued;
      while(prefetcher.issue(p, fetch_cycle))
      {
         tmp_previous_fetch_cycle = MAX(previous_fetch_cycle, p.cycle_generated);
         issued = false;
         while(tmp_previous_fetch_cycle <= fetch_cycle)
         {
            spdlog::debug("Issuing prefetch:{}", p);
            uint64_t cycle_pf_exec = tmp_previous_fetch_cycle;

            if(ldst_lanes) cycle_pf_exec = ldst_lanes->schedule(cycle_pf_exec, 0);

            if(cycle_pf_exec != MAX_CYCLE)
            {
               L1.access(cycle_pf_exec, true, p.address, true);
               ++stat_pfs_issued_to_mem;
               issued = true;
               break;
            }
            else
            {
               tmp_previous_fetch_cycle++;
               spdlog::debug("Could not find empty LDST slot for PF this cycle, increasing");
            }
         }
         
         if(!issued)
         {
            prefetcher.put_back(p);
            break;
         }
      }
   }

   // Update the instruction count and simulation cycle (max. completion cycle among all scheduled instructions).
   num_inst += 1;
   cycle = MAX(cycle, exec_cycle);

   // Update destination register timestamp.
   if (inst->D.valid) {
      assert(inst->D.log_reg < RFSIZE);
      if (inst->D.log_reg != RFFLAGS) 
      {
         squash = (pred.speculate && (pred.predicted_value != inst->D.value));         
         RF[inst->D.log_reg] = ((pred.speculate && (pred.predicted_value == inst->D.value)) ? fetch_cycle : exec_cycle);
      }
   }

   // Update SQ byte timestamps.
   if (inst->is_store) {
      uint64_t data_cache_cycle;
      if (!WRITE_ALLOCATE || PERFECT_CACHE)
         data_cache_cycle = exec_cycle;
      else
         data_cache_cycle = L1.access(exec_cycle, true, inst->addr);

      // uint64_t ret_cycle = MAX(exec_cycle, (window.empty() ? 0 : window.peektail().retire_cycle));
      uint64_t ret_cycle = MAX(data_cache_cycle, (window.empty() ? 0 : window.peektail().retire_cycle));
      for (i = 0, addr = inst->addr; i < inst->size; i++, addr++) {
         SQ[addr].exec_cycle = exec_cycle;
         SQ[addr].ret_cycle = ret_cycle;
      }
   }

   // CVP measurements
   num_eligible += (predictable ? 1 : 0);
   num_correct += ((predictable && pred.speculate && !squash) ? 1 : 0);
   num_incorrect += ((predictable && pred.speculate && squash) ? 1 : 0);

   /////////////////////////////
   // Manage window: dispatch.
   /////////////////////////////
   window.push({MAX(exec_cycle, (window.empty() ? 0 : window.peektail().retire_cycle)),
               seq_no,
               ((inst->is_load || inst->is_store) ? inst->addr : 0xDEADBEEF),
               ((inst->D.valid && (inst->D.log_reg != RFFLAGS)) ? inst->D.value : 0xDEADBEEF),
	       latency});

   /////////////////////////////
   // Manage fetch cycle.
   /////////////////////////////
   previous_fetch_cycle = fetch_cycle;

   if (squash) {			// control dependency on the retire cycle of the value-mispredicted instruction
      num_fetched = 0;			// new fetch bundle
      assert(!window.empty() && (fetch_cycle < window.peektail().retire_cycle));
      fetch_cycle = window.peektail().retire_cycle;
   }
   else if (window.full()) {
      if (fetch_cycle < window.peekhead().retire_cycle) {
         num_fetched = 0;		// new fetch bundle
         fetch_cycle = window.peekhead().retire_cycle;
      }
   }
   else {				// fetch bundle constraints
      bool stop = false;
      bool cond_branch = ((InstClass) inst->insn == InstClass::condBranchInstClass);
      bool uncond_direct = ((InstClass) inst->insn == InstClass::uncondDirectBranchInstClass);
      bool uncond_indirect = ((InstClass) inst->insn == InstClass::uncondIndirectBranchInstClass);

      // Finite fetch bundle.
      if (FETCH_WIDTH > 0) {
         num_fetched++;
         if (num_fetched == FETCH_WIDTH)
            stop = true;
      }

      // Finite branch throughput.
      if ((FETCH_NUM_BRANCH > 0) && (cond_branch || uncond_direct || uncond_indirect)) {
         num_fetched_branch++;
         if (num_fetched_branch == FETCH_NUM_BRANCH)
            stop = true;
      }

      // Indirect branch constraint.
      if (FETCH_STOP_AT_INDIRECT && uncond_indirect)
         stop = true;

      // Taken branch constraint.
      if (FETCH_STOP_AT_TAKEN && (uncond_direct || uncond_indirect || (cond_branch && (inst->next_pc != (inst->pc + 4)))))
         stop = true;

      if (stop) {
         // new fetch bundle
         num_fetched = 0;
	 num_fetched_branch = 0;
         fetch_cycle++;
      }
   }

   // Account for the effect of a mispredicted branch on the fetch cycle.
   if (!PERFECT_BRANCH_PRED && BP.predict((InstClass) inst->insn, inst->pc, inst->next_pc))
      fetch_cycle = MAX(fetch_cycle, exec_cycle);

   spdlog::debug("Updating base_cycle to {}", MIN(fetch_cycle, prefetcher.get_oldest_pf_cycle()));

   // Attempt to advance the base cycles of resource schedules.
   // Note : We may have some prefetches to issue still that are older than the fetch cycle.
   if (ldst_lanes) ldst_lanes->advance_base_cycle(MIN(fetch_cycle, prefetcher.get_oldest_pf_cycle()));
   if (alu_lanes) alu_lanes->advance_base_cycle(MIN(fetch_cycle, prefetcher.get_oldest_pf_cycle()));

   // DEBUG
   //printf("%d,%d\n", num_inst, cycle);
}

#define KILOBYTE	(1<<10)
#define MEGABYTE	(1<<20)
#define SCALED_SIZE(size)	((size/KILOBYTE >= KILOBYTE) ? (size/MEGABYTE) : (size/KILOBYTE))
#define SCALED_UNIT(size)	((size/KILOBYTE >= KILOBYTE) ? "MB" : "KB")

void uarchsim_t::output() {
   auto get_track_name = [] (uint64_t track){
      static std::string track_names [] = {
         "ALL",
         "LoadsOnly",
         "LoadsOnlyHitMiss",
      };
      //return track_names[static_cast<std::underlying_type<VPTracks>::type>(t)].c_str();
      return track_names[track].c_str();
   };
   printf("VP_ENABLE = %d\n", (VP_ENABLE ? 1 : 0));
   printf("VP_PERFECT = %s\n", (VP_ENABLE ? (VP_PERFECT ? "1" : "0") : "n/a"));
   printf("VP_TRACK = %s\n", (VP_ENABLE ? get_track_name(VP_TRACK) : "n/a"));
   printf("WINDOW_SIZE = %ld\n", WINDOW_SIZE);
   printf("FETCH_WIDTH = %ld\n", FETCH_WIDTH);
   printf("FETCH_NUM_BRANCH = %ld\n", FETCH_NUM_BRANCH);
   printf("FETCH_STOP_AT_INDIRECT = %s\n", (FETCH_STOP_AT_INDIRECT ? "1" : "0"));
   printf("FETCH_STOP_AT_TAKEN = %s\n", (FETCH_STOP_AT_TAKEN ? "1" : "0"));
   printf("FETCH_MODEL_ICACHE = %s\n", (FETCH_MODEL_ICACHE ? "1" : "0"));
   printf("PERFECT_BRANCH_PRED = %s\n", (PERFECT_BRANCH_PRED ? "1" : "0"));
   printf("PERFECT_INDIRECT_PRED = %s\n", (PERFECT_INDIRECT_PRED ? "1" : "0"));
   printf("PIPELINE_FILL_LATENCY = %ld\n", PIPELINE_FILL_LATENCY);
   printf("NUM_LDST_LANES = %ld%s", NUM_LDST_LANES, ((NUM_LDST_LANES > 0) ? "\n" : " (unbounded)\n"));
   printf("NUM_ALU_LANES = %ld%s", NUM_ALU_LANES, ((NUM_ALU_LANES > 0) ? "\n" : " (unbounded)\n"));
   //BP.output();
   printf("MEMORY HIERARCHY CONFIGURATION---------------------\n");
   printf("STRIDE Prefetcher = %s\n", PREFETCHER_ENABLE ? "1" : "0");
   printf("PERFECT_CACHE = %s\n", (PERFECT_CACHE ? "1" : "0"));
   printf("WRITE_ALLOCATE = %s\n", (WRITE_ALLOCATE ? "1" : "0"));
   printf("Within-pipeline factors:\n");
   printf("\tAGEN latency = 1 cycle\n");
   printf("\tStore Queue (SQ): SQ size = window size, oracle memory disambiguation, store-load forwarding = 1 cycle after store's or load's agen.\n");
   printf("\t* Note: A store searches the L1$ at commit. The store is released\n");
   printf("\t* from the SQ and window, whether it hits or misses. Store misses\n");
   printf("\t* are buffered until the block is allocated and the store is\n");
   printf("\t* performed in the L1$. While buffered, conflicting loads get\n");
   printf("\t* the store's data as they would from the SQ.\n");
   if (FETCH_MODEL_ICACHE) {
      printf("I$: %ld %s, %ld-way set-assoc., %ldB block size\n",
   	     SCALED_SIZE(IC_SIZE), SCALED_UNIT(IC_SIZE), IC_ASSOC, IC_BLOCKSIZE);
   }
   printf("L1$: %ld %s, %ld-way set-assoc., %ldB block size, %ld-cycle search latency\n",
   	  SCALED_SIZE(L1_SIZE), SCALED_UNIT(L1_SIZE), L1_ASSOC, L1_BLOCKSIZE, L1_LATENCY);
   printf("L2$: %ld %s, %ld-way set-assoc., %ldB block size, %ld-cycle search latency\n",
   	  SCALED_SIZE(L2_SIZE), SCALED_UNIT(L2_SIZE), L2_ASSOC, L2_BLOCKSIZE, L2_LATENCY);
   printf("L3$: %ld %s, %ld-way set-assoc., %ldB block size, %ld-cycle search latency\n",
   	  SCALED_SIZE(L3_SIZE), SCALED_UNIT(L3_SIZE), L3_ASSOC, L3_BLOCKSIZE, L3_LATENCY);
   printf("Main Memory: %ld-cycle fixed search time\n", MAIN_MEMORY_LATENCY);
   printf("STORE QUEUE MEASUREMENTS---------------------------\n");
   printf("Number of loads: %ld\n", num_load);
   printf("Number of loads that miss in SQ: %ld (%.2f%%)\n", num_load_sqmiss, 100.0*(double)num_load_sqmiss/(double)num_load);
   printf("Number of PFs issued to the memory system %ld\n", stat_pfs_issued_to_mem);
   printf("MEMORY HIERARCHY MEASUREMENTS----------------------\n");
   if (FETCH_MODEL_ICACHE) {
      printf("I$:\n"); IC.stats();
   }
   printf("L1$:\n"); L1.stats();
   printf("L2$:\n"); L2.stats();
   printf("L3$:\n"); L3.stats();
   BP.output();
   printf("ILP LIMIT STUDY------------------------------------\n");
   printf("instructions = %ld\n", num_inst);
   printf("cycles       = %ld\n", cycle);
   printf("IPC          = %.3f\n", ((double)num_inst/(double)cycle));
   printf("Prefetcher------------------------------------------\n");
   prefetcher.print_stats();
   printf("CVP STUDY------------------------------------------\n");
   printf("prediction-eligible instructions = %ld\n", num_eligible);
   printf("correct predictions              = %ld (%.2f%%)\n", num_correct, (100.0*(double)num_correct/(double)num_eligible));
   printf("incorrect predictions            = %ld (%.2f%%)\n", num_incorrect, (100.0*(double)num_incorrect/(double)num_eligible));
 
}
