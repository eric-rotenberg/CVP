// CVP1 Trace Reader
// Author: Arthur Perais (arthur.perais@gmail.com)

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org>

This project relies on https://www.cs.unc.edu/Research/compgeom/gzstream/ (LGPL) to stream from compressed traces.
For ease of use, gzstream.h and gzstream.C are provided with minor modifications (include paths) with the reader.
*/

// Compilation : Don't forget to add gzstream.C in the source list and to link with zlib (-lz).
//
// Usage : CVPTraceReader reader("./my_trace.tar.gz")
//         while(reader.readInstr())
//           reader.mInstr.printInstr();
//            ...
// Note that this is an exemple file (that was used for CVP).
// Given the trace format below, you can write your own trace reader that
// populates the instruction format in the way you like.
//
// Trace Format :
// Inst PC 				- 8 bytes
// Inst Type 				- 1 byte
// If load/storeInst
//   Effective Address 			- 8 bytes
//   Access Size (one reg)		- 1 byte
// If branch
//   Taken 				- 1 byte
//   If Taken
//     Target				- 8 bytes
// Num Input Regs 			- 1 byte
// Input Reg Names 			- 1 byte each
// Num Output Regs 			- 1 byte
// Output Reg Names 			- 1 byte each
// Output Reg Values
//   If INT (0 to 31) or FLAG (64) 	- 8 bytes each
//   If SIMD (32 to 63)			- 16 bytes each

#ifndef __CVP_LIB_CVP_TRACE_READER_HH__
#define __CVP_LIB_CVP_TRACE_READER_HH__

#include <fstream>
#include <iostream>
#include <vector>
#include <cassert>
#include <bitset>
#include <algorithm>
#include <unordered_map>

#include "./gzstream.h"

#if 0
enum InstClass : uint8_t
{
  aluInstClass = 0,
  loadInstClass = 1,
  storeInstClass = 2,
  condBranchInstClass = 3,
  uncondDirectBranchInstClass = 4,
  uncondIndirectBranchInstClass = 5,
  fpInstClass = 6,
  slowAluInstClass = 7,
  undefInstClass = 8
};
#endif

#define NoMode 0
#define ImmOffset 1
#define RegOffset 2
#define BaseUpdate 3
#define StackPointer 31

struct addr_mode_infer_t
{
  int64_t actual_offset = 0;
  uint64_t reg_offset = 0;
  uint64_t count = 1;
  uint8_t mode = NoMode;
  // Only valid for stores
  bool is_pair = false;

  addr_mode_infer_t(int64_t offset, uint64_t reg, uint8_t md)
  : actual_offset(offset)
  , reg_offset(reg)
  , count(1)
  , mode(md)
  , is_pair(false)
  {
  }

  addr_mode_infer_t()
  : actual_offset(0)
  , reg_offset(0)
  , count(1)
  , mode(NoMode)
  , is_pair(false)
  {
  }
};

// This is a cache to help us refine our addressing mode/ pair vs single detection
extern std::unordered_map<uint64_t, addr_mode_infer_t> ldst_offsets;

// This structure is used by CVP1's simulator.
// Adapt for your own needs.
struct db_operand_t
{
  bool valid;
  bool is_int;
  uint64_t log_reg;
  uint64_t value;

  void print() const
  {
    std::cout << " (int: " << is_int << ", idx: " << log_reg << " val: " << std::hex << value << std::dec << ") ";
  }
};

// This structure is used by CVP1's simulator.
// Adapt for your own needs.
struct db_t
{
  uint8_t insn;
  uint64_t pc;
  uint64_t next_pc;

  db_operand_t A;
  db_operand_t B;
  db_operand_t C;
  db_operand_t D;

  bool is_load;
  bool is_store;
  bool is_base_update;
  bool is_guaranteed_base_update;
  bool ignore_hi_lane;
  bool is_kernel;

  uint64_t addr;
  uint64_t size;

  void printInst() const
  {
    static constexpr const char * cInfo[] = {"aluOp", "loadOp", "stOp", "condBrOp", "uncondDirBrOp", "uncondIndBrOp", "fpOp", "slowAluOp" };

    std::cout << "[PC: 0x" << std::hex << pc <<std::dec << " type: "  << cInfo[insn] << " ld: " << is_load << " st: " << is_store << " bu: " << is_base_update << "ign_hi_vec:" << ignore_hi_lane;
      if(insn == InstClass::loadInstClass || insn == InstClass::storeInstClass)
      {
        assert(is_load || is_store || is_base_update);
        std::cout << " ea: 0x" << std::hex << addr << std::dec << " size: " << size;
      }
      if(insn == InstClass::condBranchInstClass || insn == InstClass::uncondDirectBranchInstClass || insn == InstClass::uncondIndirectBranchInstClass)
        std::cout << " ( tkn:" << (next_pc != pc + 4) << " tar: 0x" << std::hex << next_pc << ") " << std::dec;

      if(A.valid)
      {
        std::cout << " 1st input: ";
        A.print();
      }

      if(B.valid)
      {
        std::cout << "2nd input: ";
        B.print();
      }

      if(C.valid)
      {
        std::cout << "3rd input: ";
        C.print();
      }

      if(D.valid)
      {
        std::cout << " output: ";
        D.print();
      }

      std::cout << " ]" << std::endl;
      std::fflush(stdout);
  }
};

// INT registers are registers 0 to 31. SIMD/FP registers are registers 32 to 63. Flag register is register 64
enum Offset
{
  vecOffset = 32,
  ccOffset = 64
};

// Trace reader class.
// Format assumes that instructions have at most three inputs and at most one input.
// If the trace contains an instruction that has more than three inputs, they are ignored.
// If the trace contains an instruction that has more than one outputs, one instruction object will be created for each output,
// For instance, for a multiply, two objects with same PC will be created through two subsequent calls to get_inst(). Each will have the same
// inputs, but one will have the low part of the product as output, and one will have the high part of the product as output.
// Other instructions subject to this are (list not exhaustive): load pair and vector inststructions.
struct CVPTraceReader
{
  struct Instr
  {
    uint64_t mPc;
    uint8_t mType; // Type is InstClass
    bool mTaken;
    bool mBaseUpdate;

    uint64_t mTarget;
    uint64_t mEffAddr;
    uint8_t mMemSize; // In bytes
    uint8_t mNumInRegs;
    std::vector<uint8_t> mInRegs;
    uint8_t mNumOutRegs;
    std::vector<uint8_t> mOutRegs;
    std::vector<uint64_t> mOutRegsValues;

    // Used to ignore hi-lane in vector load during cracking.
    // Ultimately prevents writing in the wrong VRF hi/lo lane
    // Non-load may still write garbage in hi lane but that will not prevent consistent
    // load/store data as stores will only pick up the correct lane(s)
    std::bitset<32> mIgnoreVec;
    
    Instr()
    {
      reset();
    }

    void reset()
    {
      mPc = mTarget = 0xdeadbeef;
      mEffAddr = 0xdeadbeef;
      mMemSize = 0;
      mType = undefInstClass;
      mTaken = false;
      mBaseUpdate = false;
      mNumInRegs = mNumOutRegs = 0;
      mInRegs.clear();
      mOutRegs.clear();
      mOutRegsValues.clear();
      mIgnoreVec.reset();
    }

    void printInstr()
    {
      static constexpr const char * cInfo[] = {"aluOp", "loadOp", "stOp", "condBrOp", "uncondDirBrOp", "uncondIndBrOp", "fpOp", "slowAluOp" };

      std::cout << "[PC: 0x" << std::hex << mPc << std::dec  << " type: "  << cInfo[mType];
      if(mType == InstClass::loadInstClass || mType == InstClass::storeInstClass)
        std::cout << " ea: 0x" << std::hex << mEffAddr << std::dec << " size: " << (uint64_t) mMemSize;

      if(mType == InstClass::condBranchInstClass || mType == InstClass::uncondDirectBranchInstClass || mType == InstClass::uncondIndirectBranchInstClass)
        std::cout << " ( tkn:" << mTaken << " tar: 0x" << std::hex << mTarget << ") ";

      std::cout << std::dec << " InRegs : { ";
      for(unsigned elt : mInRegs)
      {
        std::cout << elt << " ";
      }
      std::cout << " } OutRegs : { ";
      for(unsigned i = 0, j = 0; i < mOutRegs.size(); i++)
      {
        if(mOutRegs[i] >= Offset::vecOffset && mOutRegs[i] != Offset::ccOffset)
        {
          assert(j+1 < mOutRegsValues.size());
          std::cout << std::dec << (unsigned) mOutRegs[i] << std::hex << " (hi:" << mOutRegsValues[j+1] << " lo:" << mOutRegsValues[j] << ") ";
          j += 2;
        }
        else
        {
          assert(j < mOutRegsValues.size());
          std::cout << std::dec << (unsigned) mOutRegs[i] << std::hex << " (" << mOutRegsValues[j++] << ") ";
        }
      }
      std::cout << ") ]" << std::endl;
      std::fflush(stdout);
    }
  };

  gz::igzstream * dpressed_input;

  // Buffer to hold trace instruction information
  Instr mInstr;

  // There are bookkeeping variables for trace instructions that necessitate the creation of several objects.
  // Specifically, load pair yields two objects with different register outputs, but SIMD instructions also yield
  // multiple objects but they have the same register output, although one has the low 64-bit and one has the high 64-bit.
  // Thus, for load pair, first piece will have mCrackRegIdx 0 (first output of trace instruction) and second will have mCrackRegIdx 1
  // (second output of trace instruction). However, in both cases, since outputs are 64-bit, mCrackValIdx will remain 0.
  // Conversely, both pieces of a SIMD instruction will have mCrackRegIdx 0 (since SIMD instruction has a single 128-bit output), but first piece
  // will have mCrackValIdx 0 (low 64-bit) and second piece will have mCrackValIdx 1 (high 64-bit).
  uint8_t mCrackRegIdx;
  uint8_t mCrackValIdx;

  // How many pieces left to generate for the current trace instruction being processed.
  uint8_t mRemainingPieces;

  // Inverse of memory size multiplier for each piece. E.g., a 128-bit access will have size 16, and each piece will have size 8 (16 * 1/2).
  uint8_t mSizeFactor;

  // Number of instructions processed so far.
  uint64_t nInstr;

  // This simply tracks how many lanes one SIMD register have been processed.
  // In this case, since SIMD is 128 bits and pieces output 64 bits, if it is pair and we are creating an instruction object from a trace instruction, this means that
  // the output of the instruction object will contain the low order bits of the SIMD register.
  // If it is odd, it means that it will contain the high order bits of the SIMD register.
  uint8_t start_fp_reg;

  // To gauge the number of time we see "load 8B into hi lane of 16B vector reg"
  uint64_t stat_wrongly_ignored_vec_hi = 0;
  
  // Note that there is no check for trace existence, so modify to suit your needs.
  CVPTraceReader(const char * trace_name)
  {
    dpressed_input = new gz::igzstream();
    dpressed_input->open(trace_name, std::ios_base::in | std::ios_base::binary);

    mCrackRegIdx = mCrackValIdx = mRemainingPieces = mSizeFactor = nInstr = start_fp_reg =  0;
  }

  ~CVPTraceReader()
  {
    if(dpressed_input)
      delete dpressed_input;

    std::cout  << " Read " << nInstr << " instrs " << std::endl;
  }

  // This is the main API function
  // There is no specific reason to call the other functions from without this file.
  // Idiom is : while(instr = get_inst())
  //              ... process instr
  db_t  *get_inst()
  {
   // If we are creating several pieces from a single trace instructions and some are left to create,
   // mRemainingPieces will be != 0
   if(mRemainingPieces)
     return populateNewInstr();
   // If there is a single piece to create
   else if(readInstr())
     return populateNewInstr();
   // If the trace is done
   else
     return nullptr;

  }

  // Creates a new object and populate it with trace information.
  // Subsequent calls to populateNewInstr() will take care of creating multiple pieces for a trace instruction
  // that has several outputs or 128-bit output.
  // Number of calls is decided by mRemainingPieces from get_inst().
  db_t *populateNewInstr()
  {
     db_t * inst = new db_t();

     if(mInstr.mType == InstClass::loadInstClass && mCrackRegIdx == 0 && mInstr.mBaseUpdate)
     {
        // The base update part of loads is ALU and should see 1-cycle latency.
        inst->insn = InstClass::aluInstClass;
        inst->addr = mInstr.mEffAddr;
        // Keep element size for validation purpose
        inst->size = std::max(1, mInstr.mMemSize / mSizeFactor);
        inst->is_base_update = true;
        // SP cannot be loaded through regular load in ARMv8
        inst->is_guaranteed_base_update = (mInstr.mNumOutRegs == 3) || (mInstr.mOutRegs[0] == StackPointer);
     }
     else if(mInstr.mType == InstClass::storeInstClass && mInstr.mBaseUpdate)
     {
        inst->insn = mInstr.mType;
        inst->size = std::max(1, mInstr.mMemSize / mSizeFactor);
        inst->addr = mInstr.mEffAddr + ((mSizeFactor - mRemainingPieces) * inst->size);
        inst->is_base_update = true;
        inst->is_guaranteed_base_update =  false;
     }
     else
     {
        inst->insn = mInstr.mType;
        inst->size = std::max(1, mInstr.mMemSize / mSizeFactor);
        inst->addr = mInstr.mEffAddr + ((mSizeFactor - mRemainingPieces) * inst->size);
        inst->is_base_update = false;
        inst->is_guaranteed_base_update = false;
     }

     // Note
     // Store pair size will be amended (x2) when we determine it is a store pair in uarchsim.cc
     
     inst->pc = mInstr.mPc;
     inst->next_pc = mInstr.mTarget;
     inst->ignore_hi_lane = false;
     // We'll approximate kernel code this way
     inst->is_kernel = inst->pc & 0x8000000000000000;

     if(mInstr.mNumInRegs >= 1)
     {
       inst->A.valid = true;
       inst->A.is_int = mInstr.mInRegs.at(0) < Offset::vecOffset || mInstr.mInRegs[0] == Offset::ccOffset;
       inst->A.log_reg = mInstr.mInRegs[0];
       inst->A.value = 0xdeadbeef;
     }
     else
       inst->A.valid = false;

     if(mInstr.mNumInRegs >= 2)
     {
       inst->B.valid = true;
       inst->B.is_int = mInstr.mInRegs.at(1) < Offset::vecOffset || mInstr.mInRegs[1] == Offset::ccOffset;
       inst->B.log_reg = mInstr.mInRegs[1];
       inst->B.value = 0xdeadbeef;
     }
     else
       inst->B.valid = false;

     if(mInstr.mNumInRegs >= 3)
     {
       inst->C.valid = true;
       inst->C.is_int = mInstr.mInRegs.at(2) < Offset::vecOffset || mInstr.mInRegs[2] == Offset::ccOffset;
       inst->C.log_reg = mInstr.mInRegs[2];
       inst->C.value = 0xdeadbeef;
     }
     else
       inst->C.valid = false;

     // We'll ignore that some ARM instructions have more than 3 inputs
     // assert(mInstr.mNumInRegs <= 3);

     if(mInstr.mNumOutRegs >= 1)
     {
       inst->D.valid = true;
       // Flag register is considered to be INT
       inst->D.is_int = mInstr.mOutRegs.at(mCrackRegIdx) < Offset::vecOffset || mInstr.mOutRegs.at(mCrackRegIdx) == Offset::ccOffset;
       inst->D.log_reg = mInstr.mOutRegs[mCrackRegIdx];
       inst->D.value = mInstr.mOutRegsValues.at(mCrackValIdx);
       // if SIMD register, we processed one more 64-bit lane.
       if(!inst->D.is_int)
         start_fp_reg++;
       else
         start_fp_reg = 0;
     }
     else
     {
       inst->D.valid = false;
       start_fp_reg = 0;
     }

     inst->is_load = mInstr.mType == InstClass::loadInstClass && !inst->is_base_update;
     // Stores have only one piece, so mark it correctly as storeInstClass even if the store is base update
     inst->is_store = mInstr.mType == InstClass::storeInstClass;
     inst->size = std::max(1, mInstr.mMemSize / mSizeFactor);

     assert(inst->size || !(inst->is_load || inst->is_store));
     assert(mRemainingPieces != 0);

     // At this point, if mRemainingPieces is 0, the next statements will have no effect.
     mRemainingPieces--;

     // If there are more output registers to be processed and they are SIMD
     if(mInstr.mNumOutRegs > mCrackRegIdx && mInstr.mOutRegs.at(mCrackRegIdx) >= Offset::vecOffset && mInstr.mOutRegs[mCrackRegIdx] != Offset::ccOffset)
     {
      // Next output value is in the next 64-bit lane
      mCrackValIdx++;
      inst->ignore_hi_lane = mInstr.mIgnoreVec.test(mCrackValIdx);
      
      // If we processed the high-order bits of the current SIMD register, the next output is a different register
      if(start_fp_reg % 2 == 0 || mInstr.mIgnoreVec.test(mCrackValIdx))
        mCrackRegIdx++;

      // Ignoring the hi lane of vector if we are only doing one lane op
      mCrackValIdx += mInstr.mIgnoreVec.test(mCrackValIdx);
     }
     // If there are more output INT registers, go to next value and next register name
     else
     {
       mCrackValIdx++;
       mCrackRegIdx++;
     }

     return inst;
  }

  // Read bytes from the trace and populate a buffer object.
  // Returns true if something was read from the trace, false if we the trace is over.
  bool readInstr()
  {
    // Trace Format :
    // Inst PC 				- 8 bytes
    // Inst Type			- 1 byte
    // If load/storeInst
    //   Effective Address 		- 8 bytes
    //   Access Size (one reg)		- 1 byte
    // If branch
    //   Taken 				- 1 byte
    //   If Taken
    //     Target			- 8 bytes
    // Num Input Regs 			- 1 byte
    // Input Reg Names 			- 1 byte each
    // Num Output Regs 			- 1 byte
    // Output Reg Names			- 1 byte each
    // Output Reg Values
    //   If INT (0 to 31) or FLAG (64) 	- 8 bytes each
    //   If SIMD (32 to 63)		- 16 bytes each
    mInstr.reset();
    start_fp_reg = 0;
    dpressed_input->read((char*) &mInstr.mPc, sizeof(mInstr.mPc));

    if(dpressed_input->eof())
      return false;

    mRemainingPieces = 1;
    mSizeFactor = 1;
    mCrackRegIdx = 0;
    mCrackValIdx = 0;

    mInstr.mTarget = mInstr.mPc + 4;
    dpressed_input->read((char*) &mInstr.mType, sizeof(mInstr.mType));

    assert(mInstr.mType != undefInstClass);

    if(mInstr.mType == InstClass::loadInstClass || mInstr.mType == InstClass::storeInstClass)
    {
      dpressed_input->read((char*) &mInstr.mEffAddr, sizeof(mInstr.mEffAddr));
      dpressed_input->read((char*) &mInstr.mMemSize, sizeof(mInstr.mMemSize));
    }
    if(mInstr.mType == InstClass::condBranchInstClass || mInstr.mType == InstClass::uncondDirectBranchInstClass || mInstr.mType == InstClass::uncondIndirectBranchInstClass)
    {
      dpressed_input->read((char*) &mInstr.mTaken, sizeof(mInstr.mTaken));
      if(mInstr.mTaken)
        dpressed_input->read((char*) &mInstr.mTarget, sizeof(mInstr.mTarget));
    }

    dpressed_input->read((char*) &mInstr.mNumInRegs, sizeof(mInstr.mNumInRegs));

    for(auto i = 0; i != mInstr.mNumInRegs; i++)
    {
      uint8_t inReg;
      dpressed_input->read((char*) &inReg, sizeof(inReg));
      mInstr.mInRegs.push_back(inReg);
    }

    dpressed_input->read((char*) &mInstr.mNumOutRegs, sizeof(mInstr.mNumOutRegs));

    mRemainingPieces = std::max(mRemainingPieces, mInstr.mNumOutRegs);

    const bool is_store = mInstr.mType == InstClass::storeInstClass;
    const bool is_load = mInstr.mType == InstClass::loadInstClass;
    
    // This will misclassify ldp x, y, [x] or ldp x, y, [y] but the count is marginal
    const bool load_is_not_bu = (ldst_offsets.count(mInstr.mPc) != 0) && (ldst_offsets[mInstr.mPc].mode == ImmOffset);
    const bool is_potential_base_update = is_store || (is_load && (mInstr.mNumOutRegs > 1) && !load_is_not_bu) || (is_load && mInstr.mNumOutRegs == 3);

    for(auto i = 0; i != mInstr.mNumOutRegs; i++)
    {
      uint8_t outReg;
      dpressed_input->read((char*) &outReg, sizeof(outReg));
      mInstr.mOutRegs.push_back(outReg);

      mInstr.mBaseUpdate |= is_potential_base_update &&
        (std::find(mInstr.mInRegs.begin(),  mInstr.mInRegs.end(), outReg) !=  mInstr.mInRegs.end());
    }

    bool vec_output = false;
    for(auto i = 0; i != mInstr.mNumOutRegs; i++)
    {
      uint64_t val;
      dpressed_input->read((char*) &val, sizeof(val));
      mInstr.mOutRegsValues.push_back(val);

      // Don't emit pieces for vector loads that only look at lo part of registers
      if(mInstr.mOutRegs[i] >= Offset::vecOffset && mInstr.mOutRegs[i] != Offset::ccOffset)
      {
        vec_output = true;
        dpressed_input->read((char*) &val, sizeof(val));
        mInstr.mOutRegsValues.push_back(val);

        const bool is_load = mInstr.mType == InstClass::loadInstClass;
        const bool multi_lane = (is_load && mInstr.mMemSize > 8) || !is_load;
        
        // If load size says we are only doing a single vec lane load, ignore second piece
        if(multi_lane)
        {
          mRemainingPieces++;
        }
        else 
        {
          mInstr.mIgnoreVec.set(mInstr.mOutRegsValues.size() - 1);
          stat_wrongly_ignored_vec_hi += val != 0;
        }
      }
    }



    // Rearrange output regs for vector loads such that base register is first if 
    // base update
    if(vec_output && mInstr.mNumOutRegs > 1 && mInstr.mOutRegs.back() < Offset::vecOffset)
    {
      auto tmp_reg = mInstr.mOutRegs.back();
      auto tmp_value = mInstr.mOutRegsValues.back();

      for(int i = mInstr.mOutRegs.size() - 1; i >= 0; i--)
      {
        mInstr.mOutRegs[i+1] = mInstr.mOutRegs[i];
      }
      mInstr.mOutRegs[0] = tmp_reg;

      for(int i = mInstr.mOutRegsValues.size() - 1; i >= 0; i--)
      {
         mInstr.mOutRegsValues[i+1] = mInstr.mOutRegsValues[i];
      }
      mInstr.mOutRegsValues[0] = tmp_value;
    }
    
    // Memsize has to be adjusted as it is giving only the access size for one register.
    if(mInstr.mType == InstClass::loadInstClass)
    {
      // Disregard base update output as it is not accessing memory
      mInstr.mMemSize = mInstr.mMemSize * std::max(1lu, (long unsigned) mInstr.mNumOutRegs - mInstr.mBaseUpdate);
    }

    mSizeFactor = std::max(1, mRemainingPieces - mInstr.mBaseUpdate);

    // Trace INT instructions with 0 outputs are generally CMP, so we mark them as producing the flag register
    // The trace does not have the value of the flag register, though
    if(mInstr.mType == aluInstClass && mInstr.mOutRegs.size() == 0)
    {
      mInstr.mOutRegs.push_back(Offset::ccOffset);
      mInstr.mOutRegsValues.push_back(0xdeadbeef);
      mInstr.mNumOutRegs++;
    }
    else if(mInstr.mType == condBranchInstClass && mInstr.mInRegs.size() == 0)
    {
      mInstr.mInRegs.push_back(Offset::ccOffset);
      mInstr.mNumInRegs++;
    }
    nInstr++;

    if(nInstr % 100000 == 0)
      std::cout << nInstr << " instrs " << std::endl;

    return true;
  }
};

#endif
