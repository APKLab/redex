/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "IRInstruction.h"

#include "DexClass.h"
#include "DexUtil.h"

#include <boost/range/any_range.hpp>
#include <cstring>
#include <iterator>

IRInstruction::IRInstruction(IROpcode op) : m_opcode(op) {
  auto count = opcode_impl::min_srcs_size(op);
  if (count <= MAX_NUM_INLINE_SRCS) {
    m_num_inline_srcs = count;
  } else {
    m_num_inline_srcs = MAX_NUM_INLINE_SRCS + 1;
    m_srcs = new std::vector<reg_t>(count);
  }
}

IRInstruction::IRInstruction(const IRInstruction& other)
    : m_opcode(other.m_opcode),
      m_num_inline_srcs(other.m_num_inline_srcs),
      m_dest(other.m_dest),
      m_literal(other.m_literal) {
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    for (auto i = 0; i < m_num_inline_srcs; ++i) {
      m_inline_srcs[i] = other.m_inline_srcs[i];
    }
  } else {
    m_srcs = new std::vector<reg_t>(*other.m_srcs);
  }
}

IRInstruction::~IRInstruction() {
  if (m_num_inline_srcs > MAX_NUM_INLINE_SRCS) {
    delete m_srcs;
  }
}

// Structural equality of opcodes except branches offsets are ignored
// because they are unknown until we sync back to DexInstructions.
bool IRInstruction::operator==(const IRInstruction& that) const {
  bool simple_fields_match =
      m_opcode == that.m_opcode &&
      m_num_inline_srcs == that.m_num_inline_srcs && m_dest == that.m_dest &&
      m_literal == that.m_literal; // just test one member of the union
  if (!simple_fields_match) {
    return false;
  }
  // Check the source registers union
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    for (auto i = 0; i < m_num_inline_srcs; ++i) {
      if (m_inline_srcs[i] != that.m_inline_srcs[i]) {
        return false;
      }
    }
    return true;
  } else {
    return *m_srcs == *that.m_srcs;
  }
}

reg_t IRInstruction::src(size_t i) const {
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    always_assert(i < m_num_inline_srcs);
    return m_inline_srcs[i];
  }
  return m_srcs->at(i);
}

IRInstruction::reg_range IRInstruction::srcs() const {
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    const reg_t* begin = std::begin(m_inline_srcs);
    const reg_t* end = begin + m_num_inline_srcs;
    return reg_range(begin, end);
  }
  const reg_t* begin = m_srcs->data();
  const reg_t* end = begin + m_srcs->size();
  return reg_range(begin, end);
}

std::vector<reg_t> IRInstruction::srcs_vec() const {
  std::vector<reg_t> result;
  result.reserve(srcs_size());
  for (reg_t src : srcs()) {
    result.push_back(src);
  }
  return result;
}

IRInstruction* IRInstruction::set_src(size_t i, reg_t reg) {
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    always_assert(i < m_num_inline_srcs);
    m_inline_srcs[i] = reg;
  } else {
    m_srcs->at(i) = reg;
  }
  return this;
}

size_t IRInstruction::srcs_size() const {
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    return m_num_inline_srcs;
  }
  return m_srcs->size();
}

IRInstruction* IRInstruction::set_srcs_size(uint16_t count) {
  if (m_num_inline_srcs <= MAX_NUM_INLINE_SRCS) {
    if (count <= MAX_NUM_INLINE_SRCS) {
      // staying in the inline state
      m_num_inline_srcs = count;
    } else {
      // inline regs -> vector
      auto srcs = new std::vector<reg_t>();
      srcs->reserve(count);
      for (auto i = 0; i < m_num_inline_srcs; ++i) {
        srcs->push_back(m_inline_srcs[i]);
      }
      srcs->resize(count);
      m_num_inline_srcs = MAX_NUM_INLINE_SRCS + 1;
      m_srcs = srcs;
    }
  } else {
    if (count <= MAX_NUM_INLINE_SRCS) {
      // vector -> inline regs
      auto old_srcs_ptr = m_srcs;
      m_num_inline_srcs = count;
      always_assert(count <= old_srcs_ptr->size());
      std::memcpy(m_inline_srcs, old_srcs_ptr->data(), count * sizeof(reg_t));
      delete old_srcs_ptr;
    } else {
      // staying in the vector state
      m_srcs->resize(count);
    }
  }
  return this;
}

uint16_t IRInstruction::size() const {
  auto op = m_opcode;
  if (opcode::is_an_internal(op)) {
    return 0;
  }
  static int args[] = {
      0, /* FMT_f00x   */
      1, /* FMT_f10x   */
      1, /* FMT_f12x   */
      1, /* FMT_f12x_2 */
      1, /* FMT_f11n   */
      1, /* FMT_f11x_d */
      1, /* FMT_f11x_s */
      1, /* FMT_f10t   */
      2, /* FMT_f20t   */
      2, /* FMT_f20bc  */
      2, /* FMT_f22x   */
      2, /* FMT_f21t   */
      2, /* FMT_f21s   */
      2, /* FMT_f21h   */
      2, /* FMT_f21c_d */
      2, /* FMT_f21c_s */
      2, /* FMT_f23x_d */
      2, /* FMT_f23x_s */
      2, /* FMT_f22b   */
      2, /* FMT_f22t   */
      2, /* FMT_f22s   */
      2, /* FMT_f22c_d */
      2, /* FMT_f22c_s */
      2, /* FMT_f22cs  */
      3, /* FMT_f30t   */
      3, /* FMT_f32x   */
      3, /* FMT_f31i   */
      3, /* FMT_f31t   */
      3, /* FMT_f31c   */
      3, /* FMT_f35c   */
      3, /* FMT_f35ms  */
      3, /* FMT_f35mi  */
      3, /* FMT_f3rc   */
      3, /* FMT_f3rms  */
      3, /* FMT_f3rmi  */
      5, /* FMT_f51l   */
      4, /* FMT_f41c_d */
      4, /* FMT_f41c_s */
      5, /* FMT_f52c_d */
      5, /* FMT_f52c_s */
      5, /* FMT_f5rc */
      5, /* FMT_f57c */
      0, /* FMT_fopcode   */
  };
  return args[dex_opcode::format(opcode::to_dex_opcode(op))];
}

// The instruction format doesn't tell us the width of registers for invoke
// so we inspect the signature of the method we're calling
bool IRInstruction::invoke_src_is_wide(size_t i) const {
  always_assert(has_method());

  // virtual methods have `this` as the 0th register argument, but the
  // arg list does NOT include `this`
  if (!is_invoke_static(m_opcode)) {
    if (i == 0) {
      // reference to `this`. References are never wide
      return false;
    }
    --i;
  }

  const std::deque<DexType*>& args =
      m_method->get_proto()->get_args()->get_type_list();
  return type::is_wide_type(args[i]);
}

bool IRInstruction::src_is_wide(size_t i) const {
  always_assert(i < srcs_size());

  if (opcode::is_an_invoke(m_opcode)) {
    return invoke_src_is_wide(i);
  }

  switch (opcode()) {
  case OPCODE_MOVE_WIDE:
  case OPCODE_RETURN_WIDE:
    return i == 0;

  case OPCODE_CMPL_DOUBLE:
  case OPCODE_CMPG_DOUBLE:
  case OPCODE_CMP_LONG:
    return i == 0 || i == 1;

  case OPCODE_APUT_WIDE:
  case OPCODE_IPUT_WIDE:
  case OPCODE_SPUT_WIDE:
    return i == 0;

  case OPCODE_NEG_LONG:
  case OPCODE_NOT_LONG:
  case OPCODE_NEG_DOUBLE:
  case OPCODE_LONG_TO_INT:
  case OPCODE_LONG_TO_FLOAT:
  case OPCODE_LONG_TO_DOUBLE:
  case OPCODE_DOUBLE_TO_INT:
  case OPCODE_DOUBLE_TO_LONG:
  case OPCODE_DOUBLE_TO_FLOAT:
    return i == 0;

  case OPCODE_ADD_LONG:
  case OPCODE_SUB_LONG:
  case OPCODE_MUL_LONG:
  case OPCODE_DIV_LONG:
  case OPCODE_REM_LONG:
  case OPCODE_AND_LONG:
  case OPCODE_OR_LONG:
  case OPCODE_XOR_LONG:
  case OPCODE_ADD_DOUBLE:
  case OPCODE_SUB_DOUBLE:
  case OPCODE_MUL_DOUBLE:
  case OPCODE_DIV_DOUBLE:
  case OPCODE_REM_DOUBLE:
    return i == 0 || i == 1;

  case OPCODE_SHL_LONG:
  case OPCODE_SHR_LONG:
  case OPCODE_USHR_LONG:
    return i == 0;

  default:
    return false;
  }
}

void IRInstruction::normalize_registers() {
  if (opcode::is_an_invoke(opcode())) {
    auto& args = get_method()->get_proto()->get_args()->get_type_list();
    size_t old_srcs_idx{0};
    size_t srcs_idx{0};
    if (m_opcode != OPCODE_INVOKE_STATIC) {
      ++srcs_idx;
      ++old_srcs_idx;
    }
    for (size_t args_idx = 0; args_idx < args.size(); ++args_idx) {
      always_assert_log(
          old_srcs_idx < srcs_size(),
          "Invalid arg indices in %s args_idx %d old_srcs_idx %d\n",
          SHOW(this),
          args_idx,
          old_srcs_idx);
      set_src(srcs_idx++, src(old_srcs_idx));
      old_srcs_idx += type::is_wide_type(args.at(args_idx)) ? 2 : 1;
    }
    always_assert(old_srcs_idx == srcs_size());
    set_srcs_size(srcs_idx);
  }
}

void IRInstruction::denormalize_registers() {
  if (opcode::is_an_invoke(m_opcode)) {
    auto& args = get_method()->get_proto()->get_args()->get_type_list();
    bool has_wide = false;
    for (const auto& arg : args) {
      if (type::is_wide_type(arg)) {
        has_wide = true;
        break;
      }
    }
    if (!has_wide) {
      return;
    }

    std::vector<reg_t> srcs;
    size_t args_idx{0};
    size_t srcs_idx{0};
    if (m_opcode != OPCODE_INVOKE_STATIC) {
      srcs.push_back(src(srcs_idx++));
    }
    for (; args_idx < args.size(); ++args_idx, ++srcs_idx) {
      srcs.push_back(src(srcs_idx));
      if (type::is_wide_type(args.at(args_idx))) {
        srcs.push_back(src(srcs_idx) + 1);
      }
    }

    // update m_inline_srcs or m_srcs
    if (m_num_inline_srcs > MAX_NUM_INLINE_SRCS) {
      delete m_srcs;
    }
    if (srcs.size() <= MAX_NUM_INLINE_SRCS) {
      m_num_inline_srcs = srcs.size();
      for (size_t i = 0; i < srcs.size(); ++i) {
        m_inline_srcs[i] = srcs.at(i);
      }
    } else {
      m_num_inline_srcs = MAX_NUM_INLINE_SRCS + 1;
      m_srcs = new std::vector<reg_t>(std::move(srcs));
    }
  }
}

bit_width_t required_bit_width(uint16_t v) {
  bit_width_t result{1};
  while (v >>= 1) {
    ++result;
  }
  return result;
}

bool needs_range_conversion(const IRInstruction* insn) {
  auto op = insn->opcode();
  if (!opcode::has_range_form(op)) {
    return false;
  }
  if (insn->srcs_size() > dex_opcode::NON_RANGE_MAX) {
    return true;
  }
  always_assert(!opcode::is_an_internal(op));
  auto dex_op = opcode::to_dex_opcode(op);
  for (size_t i = 0; i < insn->srcs_size(); ++i) {
    if (required_bit_width(insn->src(i)) >
        dex_opcode::src_bit_width(dex_op, i)) {
      return true;
    }
  }
  return false;
}

uint64_t IRInstruction::hash() const {
  uint64_t result = opcode();

  for (size_t i = 0; i < srcs_size(); i++) {
    result ^= src(i);
  }

  if (has_dest()) {
    result ^= dest();
  }

  switch (opcode::ref(opcode())) {
  case opcode::Ref::Data: {
    size_t size = get_data()->data_size();
    const auto& data = get_data()->data();
    for (size_t i = 0; i < size; i++) {
      result ^= data[i];
    }
    break;
  }
  case opcode::Ref::CallSite:
  case opcode::Ref::MethodHandle:
  case opcode::Ref::Field:
  case opcode::Ref::Method:
  case opcode::Ref::Literal:
  case opcode::Ref::String:
  case opcode::Ref::Type: {
    result ^= m_literal;
    break;
  }
  case opcode::Ref::None:
    break;
  }

  return result;
}

void IRInstruction::gather_types(std::vector<DexType*>& ltype) const {
  switch (opcode::ref(opcode())) {
  case opcode::Ref::None:
  case opcode::Ref::String:
  case opcode::Ref::Literal:
  case opcode::Ref::Data:
  case opcode::Ref::CallSite:
  case opcode::Ref::MethodHandle:
    break;

  case opcode::Ref::Type:
    ltype.push_back(m_type);
    break;

  case opcode::Ref::Field:
    m_field->gather_types_shallow(ltype);
    break;

  case opcode::Ref::Method:
    m_method->gather_types_shallow(ltype);
    break;
  }
}
