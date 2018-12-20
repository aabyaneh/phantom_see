#include "sase.h"

// -----------------------------------------------------------------
// ---------------- Solver Aided Symbolic Execution ----------------
// -----------------------------------------------------------------

char           var_buffer[100]; // a buffer for automatic variable name generation
Btor*          btor;
BoolectorSort  bv_sort;
BoolectorNode* zero_bv;
BoolectorNode* one_bv;
uint64_t       sase_symbolic = 0;
uint64_t       mrif = 0;        // most recent conditional
uint64_t       b = 0;           // counting total number of backtracking
uint64_t       SASE = 8;        // Solver Aided Symbolic Execution
uint64_t       CONCRETE_T = 0;
uint64_t       SYMBOLIC_T = 1;

// symbolic registers
BoolectorNode**   sase_regs;         // array of pointers to SMT expressions
uint64_t*         sase_regs_typ;     // CONCRETE_T or SYMBOLIC_T

// engine trace
uint64_t        sase_trace_size = 10000000;
uint64_t        sase_tc = 0;            // trace counter
uint64_t*       sase_pcs;
BoolectorNode** sase_false_branchs;
uint64_t*       sase_read_trace_ptrs;   // pointers to read trace
uint64_t*       sase_program_brks;      // keep track of program_break
uint64_t*       sase_store_trace_ptrs;  // pointers to store trace
uint64_t*       sase_rds;

// store trace
// extern uint64_t  tc;
// extern uint64_t* tcs;
// extern uint64_t* vaddrs;
// extern uint64_t* values;
uint8_t*        is_symbolics;
BoolectorNode** symbolic_values;

// read trace
uint64_t*       concrete_reads;
BoolectorNode** constrained_reads;
uint64_t        read_tc         = 0;
uint64_t        read_tc_current = 0;
uint64_t        read_buffer     = 0;

// ********************** engine functions ************************

void init_sase() {
  btor        = boolector_new();
  bv_sort     = boolector_bitvec_sort(btor, 64);
  zero_bv     = boolector_unsigned_int(btor, 0, bv_sort);
  one_bv      = boolector_unsigned_int(btor, 1, bv_sort);
  boolector_set_opt(btor, BTOR_OPT_INCREMENTAL, 1);

  sase_regs     = malloc(sizeof(BoolectorNode*) * NUMBEROFREGISTERS);
  sase_regs_typ = malloc(sizeof(uint64_t)       * NUMBEROFREGISTERS);
  for (size_t i = 0; i < NUMBEROFREGISTERS; i++) {
    sase_regs_typ[i] = CONCRETE_T;
  }
  sase_regs[REG_ZR] = zero_bv;
  sase_regs[REG_FP] = zero_bv;

  sase_pcs              = malloc(sizeof(uint64_t)       * sase_trace_size);
  sase_false_branchs    = malloc(sizeof(BoolectorNode*) * sase_trace_size);
  sase_read_trace_ptrs  = malloc(sizeof(uint64_t)       * sase_trace_size);
  sase_program_brks     = malloc(sizeof(uint64_t)       * sase_trace_size);
  sase_store_trace_ptrs = malloc(sizeof(uint64_t)       * sase_trace_size);
  sase_rds              = malloc(sizeof(uint64_t)       * sase_trace_size);

  tcs             = malloc(sizeof(uint64_t)       * sase_trace_size);
  vaddrs          = malloc(sizeof(uint64_t)       * sase_trace_size);
  values          = malloc(sizeof(uint64_t)       * sase_trace_size);
  is_symbolics    = malloc(sizeof(uint8_t)        * sase_trace_size);
  symbolic_values = malloc(sizeof(BoolectorNode*) * sase_trace_size);

  concrete_reads        = malloc(sizeof(uint64_t)       * sase_trace_size);
  constrained_reads     = malloc(sizeof(BoolectorNode*) * sase_trace_size);
}

void store_registers_fp_sp_rd() {
  tc++;
  *(tcs             + tc) = *(registers + REG_FP);
  *(is_symbolics    + tc) = 0;
  *(values          + tc) = *(registers + REG_SP);
  *(symbolic_values + tc) = 0;
  *(vaddrs + tc)          = rd;
}

void restore_registers_fp_sp_rd(uint64_t tr_cnt, uint64_t rd_reg) {
  *(registers + REG_FP) = *(tcs    + tr_cnt);
  *(registers + REG_SP) = *(values + tr_cnt);
  *(registers + rd_reg) = 0;

  sase_regs[REG_FP] = boolector_unsigned_int(btor, *(registers + REG_FP), bv_sort);
  sase_regs[REG_SP] = boolector_unsigned_int(btor, *(registers + REG_SP), bv_sort);
  sase_regs[rd_reg] = zero_bv;

  sase_regs_typ[REG_FP] = CONCRETE_T;
  sase_regs_typ[REG_SP] = CONCRETE_T;
  sase_regs_typ[rd_reg] = CONCRETE_T;
}

uint8_t check_next_1_instrs() {
  uint64_t op;

  pc = pc + INSTRUCTIONSIZE;
  fetch();
  pc = pc - INSTRUCTIONSIZE;
  op = get_opcode(ir);
  if (op == OP_BRANCH)
    return 1;
  else
    return 0;
}

uint8_t match_sub(uint64_t prev_instr_rd) {
  uint64_t rs1_;
  uint64_t rs2_;
  uint64_t rd_;
  uint64_t funct3_;
  uint64_t funct7_;

  funct7_ = get_funct7(ir);
  funct3_ = get_funct3(ir);
  rs1_    = get_rs1(ir);
  rs2_    = get_rs2(ir);
  rd_     = get_rd(ir);

  if (funct3_ == F3_ADD) {
    if (funct7_ == F7_SUB)
      if (rs1_ == prev_instr_rd)
        if (rs2_ == rd)
          if (rd_ == rs2_)
            return 1;
  }

  return 0;
}

uint8_t match_addi() {
  uint64_t rs1_;
  uint64_t rd_;
  uint64_t funct3_;
  uint64_t imm_;

  rs1_    = get_rs1(ir);
  rd_     = get_rd(ir);
  funct3_ = get_funct3(ir);
  imm_    = get_immediate_i_format(ir);

  if (funct3_ == F3_ADDI) {
    if (imm_ == 1)
      if (rs1_ == REG_ZR)
        if (rd_ != rd)
          return 1;
  }

  return 0;
}

uint8_t check_next_3_instrs() {
  uint64_t rd_;
  uint64_t opcode_;
  uint64_t saved_pc;

  saved_pc = pc;

  pc = saved_pc + INSTRUCTIONSIZE;
  fetch();
  opcode_ = get_opcode(ir);
  if (opcode_ == OP_IMM) {
    if (match_addi()) {
      rd_ = get_rd(ir);
      pc = saved_pc + 2 * INSTRUCTIONSIZE;
      fetch();
      opcode_ = get_opcode(ir);
      if (opcode_ == OP_OP) {
        if (match_sub(rd_)) {
          rd = get_rd(ir);
          pc = saved_pc + 3 * INSTRUCTIONSIZE;
          fetch();
          opcode_ = get_opcode(ir);
          pc = saved_pc;
          if (opcode_ == OP_BRANCH)
            return 2;
          else
            return 2;
        }
      }
    }
  }

  pc = saved_pc;
  return 1;
}

// ********************** engine instructions ************************

void sase_lui() {
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_unsigned_int(btor, imm << 12, bv_sort);
    sase_regs_typ[rd] = CONCRETE_T;
  }
}

void sase_addi() {
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_add(btor, sase_regs[rs1], boolector_unsigned_int(btor, imm, bv_sort));

    sase_regs_typ[rd] = sase_regs_typ[rs1];
  }
}

void sase_add() {
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_add(btor, sase_regs[rs1], sase_regs[rs2]);

    if (sase_regs_typ[rs1] == CONCRETE_T && sase_regs_typ[rs2] == CONCRETE_T)
      sase_regs_typ[rd] = CONCRETE_T;
    else
      sase_regs_typ[rd] = SYMBOLIC_T;
  }
}

void sase_sub() {
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_sub(btor, sase_regs[rs1], sase_regs[rs2]);

    if (sase_regs_typ[rs1] == CONCRETE_T && sase_regs_typ[rs2] == CONCRETE_T)
      sase_regs_typ[rd] = CONCRETE_T;
    else
      sase_regs_typ[rd] = SYMBOLIC_T;
  }
}

void sase_mul() {
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_mul(btor, sase_regs[rs1], sase_regs[rs2]);

    if (sase_regs_typ[rs1] == CONCRETE_T && sase_regs_typ[rs2] == CONCRETE_T)
      sase_regs_typ[rd] = CONCRETE_T;
    else
      sase_regs_typ[rd] = SYMBOLIC_T;
  }
}

void sase_divu() {
  // check if divisor is zero?
  boolector_push(btor, 1);
  boolector_assert(btor, boolector_eq(btor, sase_regs[rs2], zero_bv));
  if (boolector_sat(btor) == BOOLECTOR_SAT) {
    printf("%s\n", "SE division by zero!");
    printf("pc: %llx\n", pc - entry_point);
    boolector_print_model (btor, "smt2", stdout);
    // exit(EXITCODE_SYMBOLICEXECUTIONERROR);
  }
  boolector_pop(btor, 1);

  // divu semantics
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_udiv(btor, sase_regs[rs1], sase_regs[rs2]);

    if (sase_regs_typ[rs1] == CONCRETE_T && sase_regs_typ[rs2] == CONCRETE_T)
      sase_regs_typ[rd] = CONCRETE_T;
    else
      sase_regs_typ[rd] = SYMBOLIC_T;
  }
}

void sase_remu() {
  // check if divisor is zero?
  boolector_push(btor, 1);
  boolector_assert(btor, boolector_eq(btor, sase_regs[rs2], zero_bv));
  if (boolector_sat (btor) == BOOLECTOR_SAT) {
    printf("%s\n", "SE division by zero!");
    printf("pc: %llx\n", pc - entry_point);
    boolector_print_model (btor, "smt2", stdout);
    // exit(EXITCODE_SYMBOLICEXECUTIONERROR);
  }
  boolector_pop(btor, 1);

  // remu semantics
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_urem(btor, sase_regs[rs1], sase_regs[rs2]);

    if (sase_regs_typ[rs1] == CONCRETE_T && sase_regs_typ[rs2] == CONCRETE_T)
      sase_regs_typ[rd] = CONCRETE_T;
    else
      sase_regs_typ[rd] = SYMBOLIC_T;
  }
}

void sase_sltu() {
  uint8_t  is_branch;
  uint64_t op;
  uint64_t saved_pc;

  ic_sltu = ic_sltu + 1;

  if (rd != REG_ZR) {

    // concrete semantics
    if (sase_regs_typ[rs1] == CONCRETE_T && sase_regs_typ[rs2] == CONCRETE_T) {
      if (*(registers + rs1) < *(registers + rs2)) {
        *(registers + rd) = 1;
        sase_regs[rd]     = one_bv;
      } else {
        *(registers + rd) = 0;
        sase_regs[rd]     = zero_bv;
      }

      sase_regs_typ[rd] = CONCRETE_T;
      pc = pc + INSTRUCTIONSIZE;
      return;
    }

    is_branch = check_next_1_instrs();
    if (is_branch == 0) {
      is_branch = check_next_3_instrs();

      if (is_branch == 2) {
        sase_false_branchs[sase_tc]    = boolector_ult(btor, sase_regs[rs1], sase_regs[rs2]);
        sase_pcs[sase_tc]              = pc  + 3 * INSTRUCTIONSIZE;

        boolector_push(btor, 1);
        boolector_assert(btor, boolector_ugte(btor, sase_regs[rs1], sase_regs[rs2]));

        // skip execution of next two instructions
        pc = pc + 3 * INSTRUCTIONSIZE;
        ic_addi = ic_addi + 1;
        ic_sub  = ic_sub  + 1;
      }
    }

    if (is_branch == 1) {
      sase_false_branchs[sase_tc]    = boolector_ugte(btor, sase_regs[rs1], sase_regs[rs2]);
      sase_pcs[sase_tc]              = pc  + INSTRUCTIONSIZE;

      boolector_push(btor, 1);
      boolector_assert(btor, boolector_ult(btor, sase_regs[rs1], sase_regs[rs2]));
      pc = pc + INSTRUCTIONSIZE;
    }

    // symbolic semantics
    sase_program_brks[sase_tc]     = get_program_break(current_context);
    sase_read_trace_ptrs[sase_tc]  = read_tc_current;
    sase_store_trace_ptrs[sase_tc] = mrif;
    mrif = tc;
    store_registers_fp_sp_rd(); // after mrif =
    sase_tc++;

    if (boolector_sat(btor) == BOOLECTOR_SAT) {
      sase_regs[rd]     = one_bv;
      sase_regs_typ[rd] = CONCRETE_T;
      *(registers + rd) = 1;
    } else {
      // printf("%s\n", "unreachable branch true!");
      // b++;
      sase_backtrack_sltu(1);
    }

  }
}

void sase_backtrack_sltu(int is_true_branch_unreachable) {
  if (sase_tc == 0) {
    printf("pc: %llx, read_tc: %llu\n", pc - entry_point, read_tc);
    pc = 0;
    return;
  }

  sase_tc--;
  pc              = sase_pcs[sase_tc];
  read_tc_current = sase_read_trace_ptrs[sase_tc];
  set_program_break(current_context, sase_program_brks[sase_tc]);
  backtrack_branch_stores(); // before mrif =
  mrif = sase_store_trace_ptrs[sase_tc];

  boolector_pop(btor, 1);
  boolector_assert(btor, sase_false_branchs[sase_tc]);
  if (boolector_sat(btor) != BOOLECTOR_SAT) {
    if (is_true_branch_unreachable) {
      printf("%s\n", "unreachable branch both true and false!");
      // exit(EXITCODE_SYMBOLICEXECUTIONERROR);
    } else {
      // printf("%s %llu\n", "unreachable branch false!", pc);
      sase_backtrack_sltu(0);
    }
  } else {
    sase_regs[rd]     = zero_bv;
    sase_regs_typ[rd] = CONCRETE_T;
    *(registers + rd) = 0;
  }
}

void sase_ld() {
  uint64_t mrv;
  uint64_t vaddr = *(registers + rs1) + imm;

  ic_ld = ic_ld + 1;

  if (is_valid_virtual_address(vaddr)) {
    if (is_virtual_address_mapped(pt, vaddr)) {
      if (rd != REG_ZR) {
        mrv = load_symbolic_memory(pt, vaddr);

        // if (mrv == 0) {
        //   printf("%s %llu %x\n", "uninitialize memory", vaddr, pc - entry_point);
        // }

        if (*(is_symbolics + mrv) == CONCRETE_T) {
          sase_regs_typ[rd] = CONCRETE_T;
          sase_regs[rd]     = boolector_unsigned_int(btor, *(values + mrv), bv_sort);
        } else {
          sase_regs_typ[rd] = SYMBOLIC_T;
          sase_regs[rd]     = *(symbolic_values + mrv);
        }

        *(registers + rd) = *(values + mrv);

        pc = pc + INSTRUCTIONSIZE;
      }
    } else
      throw_exception(EXCEPTION_PAGEFAULT, get_page_of_virtual_address(vaddr));
  } else
    throw_exception(EXCEPTION_INVALIDADDRESS, vaddr);
}

void sase_sd() {
  uint64_t vaddr = *(registers + rs1) + imm;

  ic_sd = ic_sd + 1;

  if (is_valid_virtual_address(vaddr)) {
    if (is_virtual_address_mapped(pt, vaddr)) {

      if (sase_regs_typ[rs2] == CONCRETE_T) {
        sase_store_memory(pt, vaddr, CONCRETE_T, *(registers + rs2), 0);
      } else {
        sase_store_memory(pt, vaddr, SYMBOLIC_T, *(registers + rs2), sase_regs[rs2]);
      }

      pc = pc + INSTRUCTIONSIZE;
    } else
      throw_exception(EXCEPTION_PAGEFAULT, get_page_of_virtual_address(vaddr));
  } else
    throw_exception(EXCEPTION_INVALIDADDRESS, vaddr);
}

void sase_jal_jalr() {
  if (rd != REG_ZR) {
    sase_regs[rd] = boolector_unsigned_int(btor, pc + INSTRUCTIONSIZE, bv_sort);

    sase_regs_typ[rd] = CONCRETE_T;
  }
}

void sase_store_memory(uint64_t* pt, uint64_t vaddr, uint8_t is_symbolic, uint64_t value, BoolectorNode* sym_value) {
  uint64_t mrv;

  mrv = load_symbolic_memory(pt, vaddr);

  if (mrv != 0)
    if (is_symbolic == *(is_symbolics + mrv))
      if (value == *(values + mrv))
        if (sym_value == *(symbolic_values + mrv))
          return;

  if (mrif < mrv && vaddr != read_buffer) {
    *(is_symbolics    + mrv) = is_symbolic;
    *(values          + mrv) = value;
    *(symbolic_values + mrv) = sym_value;
  } else {
    tc++;

    *(tcs             + tc) = mrv;
    *(is_symbolics    + tc) = is_symbolic;
    *(values          + tc) = value;
    *(symbolic_values + tc) = sym_value;
    *(vaddrs + tc) = vaddr;

    store_virtual_memory(pt, vaddr, tc);
  }
}

void backtrack_branch_stores() {
  uint64_t pre_tc;

  while (mrif < tc) {
    if (*(vaddrs + tc) < NUMBEROFREGISTERS) {
      restore_registers_fp_sp_rd(tc, *(vaddrs + tc));
    } else {
      store_virtual_memory(pt, *(vaddrs + tc), *(tcs + tc));
    }
    tc--;
  }
}

/* -----------------------------------------------------------------
------------------- Modified functions in Selfie.c:-----------------

void implement_read(uint64_t* context);
void implement_write(uint64_t* context);
void implement_open(uint64_t* context);
void implement_brk(uint64_t* context);
void decode_execute();
uint64_t down_load_string(uint64_t* table, uint64_t vaddr, uint64_t* s);
void map_and_store(uint64_t* context, uint64_t vaddr, uint64_t data);
void up_load_arguments(uint64_t* context, uint64_t argc, uint64_t* argv);
uint64_t monster(uint64_t* to_context);
uint64_t selfie_run(uint64_t machine);

------------------------------------------------------------------- */