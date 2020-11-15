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
// Modified by A. Seznec (andre.seznec@inria.fr) to include TAGE-SC-L predictor and the ITTAGE indirect branch predictor

#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include "cvp.h"
#include "bp.h"
#include "parameters.h"

#include "parameters.h"

bp_t::bp_t(uint64_t cb_pc_length, uint64_t cb_bhr_length,
	   uint64_t ib_pc_length, uint64_t ib_bhr_length,
	   uint64_t ras_size)
   /* A. Seznec: introduction of  TAGE-SC-L and ITTAGE*/
   : TAGESCL(new PREDICTOR())
   , ITTAGE(new IPREDICTOR())
   , ras(ras_size) {

   // Initialize measurements.
   meas_branch_n = 0;
   meas_branch_m = 0;
   meas_jumpdir_n = 0;
   meas_jumpind_n = 0;
   meas_jumpind_m = 0;
   meas_jumpret_n = 0;
   meas_jumpret_m = 0;
   meas_notctrl_n = 0;
   meas_notctrl_m = 0;
}

bp_t::~bp_t() {
}

// Returns true if instruction is a mispredicted branch.
// Also updates all branch predictor structures as applicable.
bool bp_t::predict(InstClass insn, uint64_t pc, uint64_t next_pc) {
   bool taken;
   bool pred_taken;
   uint64_t pred_target;
   bool misp;

   if (insn == InstClass::condBranchInstClass) {
      // CONDITIONAL BRANCH

      // Determine the actual taken/not-taken outcome.
      taken = (next_pc != (pc + 4));

      // Make prediction.
      pred_taken= TAGESCL->GetPrediction (pc);
      
      // Determine if mispredicted or not.
      misp = (pred_taken != taken);
      
      /* A. Seznec: uodate TAGE-SC-L*/
      TAGESCL-> UpdatePredictor (pc , 1,  taken, pred_taken, next_pc);
      // Update measurements.
      meas_branch_n++;
      meas_branch_m += misp;
   }
   else if (insn == InstClass::uncondDirectBranchInstClass) {
      // CALL OR JUMP DIRECT

      // Target of JAL or J (rd=x0) will be available in either the fetch stage (BTB hit)
      // or the pre-decode/decode stage (BTB miss), so these are not predicted.
      // Never mispredicted.
      misp = false;
      
      /* A. Seznec: update branch  histories for TAGE-SC-L and ITTAGE */
      TAGESCL->TrackOtherInst(pc , 0,  true,next_pc);
      ITTAGE->TrackOtherInst(pc , next_pc);
#if 0
      // If the destination register is the link register (x1) or alternate link register (x5),
      // then the instruction is a call instruction by software convention.
      // Push the RAS in this case.
      if (is_link_reg(insn.rd()))
         ras.push(pc + 4);
#endif

      // Update measurements.
      meas_jumpdir_n++;
   }
   else if (insn == InstClass::uncondIndirectBranchInstClass) {
#if 0
      // RISCV ISA spec, Table 2.1, explains rules for inferring a return instruction.
      if (is_link_reg(insn.rs1()) && (!is_link_reg(insn.rd()) || (insn.rs1() != insn.rd()))) {
         // RETURN
 
         // Make prediction.
	 pred_target = ras.pop();

         // Determine if mispredicted or not.
         misp = (pred_target != next_pc);
 
         // Update measurements.
         meas_jumpret_n++;
         if (misp) meas_jumpret_m++;
      }
      else {
         // NOT RETURN
#endif
      if (PERFECT_INDIRECT_PRED) {
	      misp = false;
         // Update measurements.
         meas_jumpind_n++;
      }
      else {
         // Make prediction.
         pred_target= ITTAGE->GetPrediction (pc);

         // Determine if mispredicted or not.
         misp = (pred_target != next_pc);
      
         /* A. Seznec: update ITTAGE*/
         ITTAGE-> UpdatePredictor (pc , next_pc);
      
         // Update measurements.
         meas_jumpind_n++;
         meas_jumpind_m += misp;
      }

      /* A. Seznec: update history for TAGE-SC-L */
      TAGESCL->TrackOtherInst(pc , 2,  true,next_pc);
#if 0
      }

      // RISCV ISA spec, Table 2.1, explains rules for inferring a call instruction.
      if (is_link_reg(insn.rd()))
         ras.push(pc + 4);
#endif
   }
   else {
      // not a control-transfer instruction
      misp = (next_pc != pc + 4);

      // Update measurements.
      meas_notctrl_n++;
      meas_notctrl_m+=misp;
   }

   return(misp);
}

inline bool bp_t::is_link_reg(uint64_t x) {
   return((x == 1) || (x == 5));
}

#define BP_OUTPUT(str, n, m, i) \
	printf("%s%10ld %10ld %5.2lf%% %5.2lf\n", (str), (n), (m), 100.0*((double)(m)/(double)(n)), 1000.0*((double)(m)/(double)(i)))

void bp_t::output() {
   uint64_t num_inst = (meas_branch_n + meas_jumpdir_n + meas_jumpind_n + meas_jumpret_n + meas_notctrl_n);
   uint64_t num_misp = (meas_branch_m + meas_jumpind_m + meas_jumpret_m + meas_notctrl_m);
   printf("BRANCH PREDICTION MEASUREMENTS---------------------\n");
   printf("Type                      n          m     mr  mpki\n");
   BP_OUTPUT("All              ", num_inst, num_misp, num_inst);
   BP_OUTPUT("Branch           ", meas_branch_n, meas_branch_m, num_inst);
   BP_OUTPUT("Jump: Direct     ", meas_jumpdir_n, (uint64_t)0, num_inst);
   BP_OUTPUT("Jump: Indirect   ", meas_jumpind_n, meas_jumpind_m, num_inst);
   BP_OUTPUT("Jump: Return     ", meas_jumpret_n, meas_jumpret_m, num_inst);
   BP_OUTPUT("Not control      ", meas_notctrl_n, meas_notctrl_m, num_inst);
}

