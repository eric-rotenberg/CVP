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


class gshare_index_t {
private:
	// Global branch history register.
	uint64_t bhr;		// current state of global branch history register
	uint64_t bhr_msb;	// used to set the msb of the bhr

	// Parameters for index generation.
	uint64_t pc_mask;
	uint64_t bhr_shamt;

	// User can query what its predictor size should be.
	uint64_t size;

public:
	gshare_index_t(uint64_t pc_length, uint64_t bhr_length) {
	   // Global branch history register.
	   bhr = 0;
	   bhr_msb = (1 << (bhr_length - 1));

	   // Parameters for index generation.
	   pc_mask = ((1 << pc_length) - 1);
	   if (pc_length > bhr_length) {
	      bhr_shamt = (pc_length - bhr_length);
	      size = (1 << pc_length);
	   }
	   else {
	      bhr_shamt = 0;
	      size = (1 << bhr_length);
	   }
	}

	~gshare_index_t() {
	}

	uint64_t table_size() {
	   return(size);
	}

	// Function to generate gshare index.
	inline uint64_t index(uint64_t pc) {
	   return( ((pc >> 2) & pc_mask) ^ (bhr << bhr_shamt) );
	}

	// Function to update bhr.
	inline void update_bhr(bool taken) {
	   bhr = ((bhr >> 1) | (taken ? bhr_msb : 0));
	}
};

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
	// Gshare predictor for conditional branches.
	uint64_t *cb;
	gshare_index_t cb_index;

	// Gshare predictor for indirect branches.
	uint64_t *ib;
	gshare_index_t ib_index;

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
