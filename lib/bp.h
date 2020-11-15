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

#include "tage_sc_l.h"
#include "ittage.h"

class ras_t {
private:
	uint64_t *ras;
	uint64_t size;
	uint64_t tos;

public:
	ras_t(uint64_t size) {
	   this->size = ((size > 0) ? size : 1);
	   ras = new uint64_t[this->size];
	   tos = 0;
	}

	~ras_t() {
	}

	inline void push(uint64_t x) {
           ras[tos] = x;
	   tos++;
	   if (tos == size)
	      tos = 0;
	}

	inline uint64_t pop() {
	   tos = ((tos > 0) ? (tos - 1) : (size - 1));
	   return(ras[tos]);
	}
};

class bp_t {
private:
    // Conditional branch predictor based on CBP-5 TAGE-SC-L
    PREDICTOR *TAGESCL;

	// Indirect target predictor based on ITTAGE
    IPREDICTOR *ITTAGE;

	// Return address stack for predicting return targets.
	ras_t ras;

	// Check for link register (x1) or alternate link register (x5)
	bool is_link_reg(uint64_t x);

	// Measurements.
	uint64_t meas_branch_n;		// # branches
	uint64_t meas_branch_m;		// # mispredicted branches
	uint64_t meas_jumpdir_n;	// # jumps, direct
	uint64_t meas_jumpind_n;	// # jumps, indirect
	uint64_t meas_jumpind_m;	// # mispredicted jumps, indirect
	uint64_t meas_jumpret_n;	// # jumps, return
	uint64_t meas_jumpret_m;	// # mispredicted jumps, return
	uint64_t meas_notctrl_n;	// # non-control transfer instructions
	uint64_t meas_notctrl_m;	// # non-control transfer instructions for which: next_pc != pc + 4

public:
	bp_t(uint64_t cb_pc_length, uint64_t cb_bhr_length,
	     uint64_t ib_pc_length, uint64_t ib_bhr_length,
	     uint64_t ras_size);
	~bp_t();

	// Returns true if instruction is a mispredicted branch.
	// Also updates all branch predictor structures as applicable.
	bool predict(InstClass insn, uint64_t pc, uint64_t next_pc);

	// Output all branch prediction measurements.
	void output();
};

