#include "Smt2Emitter.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

static std::string make_safe_name(const std::string &raw, int64_t id,
                                  std::unordered_set<std::string> &used) {
  std::string base;
  if (raw.empty()) {
    base = "input_" + std::to_string(id);
  } else {
    base = raw;
    for (auto &c : base) {
      bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '~' || c == '!' || c == '@' ||
                c == '$' || c == '%' || c == '^' || c == '&' || c == '*' ||
                c == '_' || c == '-' || c == '+' || c == '=' || c == '<' ||
                c == '>' || c == '.' || c == '?' || c == '/';
      if (!ok)
        c = '_';
    }
  }
  std::string name = base;
  for (int suffix = 0; !used.insert(name).second; suffix++) {
    name = base + "_" + std::to_string(suffix);
  }
  return name;
}

static const char *smt2_op(NodeKind k) {
  switch (k) {
  case NodeKind::Not:
    return "bvnot";
  case NodeKind::And:
    return "bvand";
  case NodeKind::Or:
    return "bvor";
  case NodeKind::Xor:
    return "bvxor";
  case NodeKind::Concat:
    return "concat";
  case NodeKind::Add:
    return "bvadd";
  case NodeKind::Sub:
    return "bvsub";
  case NodeKind::Mul:
    return "bvmul";
  case NodeKind::Sll:
    return "bvshl";
  case NodeKind::Srl:
    return "bvlshr";
  case NodeKind::Sra:
    return "bvashr";
  case NodeKind::Ult:
    return "bvult";
  case NodeKind::Ulte:
    return "bvule";
  case NodeKind::Ugt:
    return "bvugt";
  case NodeKind::Ugte:
    return "bvuge";
  case NodeKind::Slt:
    return "bvslt";
  case NodeKind::Slte:
    return "bvsle";
  case NodeKind::Sgt:
    return "bvsgt";
  case NodeKind::Sgte:
    return "bvsge";
  case NodeKind::Udiv:
    return "bvudiv";
  case NodeKind::Sdiv:
    return "bvsdiv";
  case NodeKind::Urem:
    return "bvurem";
  case NodeKind::Srem:
    return "bvsrem";
  case NodeKind::Smod:
    return "bvsmod";
  case NodeKind::Neg:
    return "bvneg";
  default:
    return nullptr;
  }
}

std::string emit_expr(const Module &m, int64_t id,
                      const std::unordered_map<int64_t, std::string> &names);

static std::string
emit_nested_binary(const Module &m, int64_t id,
                   const std::unordered_map<int64_t, std::string> &names,
                   const char *op) {
  const Node &n = m.nodes[id];
  std::ostringstream ss;
  ss << "(" << op;
  for (auto op_id : n.operands) {
    ss << " " << emit_expr(m, op_id, names);
  }
  ss << ")";
  return ss.str();
}

static bool is_smt_bool_kind(NodeKind k) {
  return k == NodeKind::Ult || k == NodeKind::Ulte || k == NodeKind::Ugt ||
         k == NodeKind::Ugte || k == NodeKind::Slt || k == NodeKind::Slte ||
         k == NodeKind::Sgt || k == NodeKind::Sgte || k == NodeKind::Eq ||
         k == NodeKind::Neq || k == NodeKind::Iff || k == NodeKind::Implies;
}

static std::string
emit_redxor(const Module &m, int64_t id,
            const std::unordered_map<int64_t, std::string> &names) {
  const Node &n = m.nodes[id];
  const Node &x = m.nodes[n.operands[0]];
  int64_t w = x.width;
  if (w == 1)
    return emit_expr(m, n.operands[0], names);
  std::string bits;
  for (int64_t i = w - 1; i >= 0; i--) {
    if (i > 0)
      bits += "(bvxor ";
    bits += "((_ extract " + std::to_string(i) + " " + std::to_string(i) +
            ") " + emit_expr(m, n.operands[0], names) + ")";
    if (i > 0 && i < w - 1)
      bits += " ";
  }
  for (int64_t i = 0; i < w - 1; i++)
    bits += ")";
  bits = "((_ extract 0 0) " + bits + ")";
  return bits;
}

static std::string
emit_rotate(const Module &m, int64_t id,
            const std::unordered_map<int64_t, std::string> &names, bool left) {
  const Node &n = m.nodes[id];
  int64_t w = m.nodes[n.operands[0]].width;
  std::string x = emit_expr(m, n.operands[0], names);
  std::string y = emit_expr(m, n.operands[1], names);

  std::ostringstream ss;
  ss << "(bvor (";
  if (left)
    ss << "bvshl " << x << " " << y;
  else
    ss << "bvlshr " << x << " " << y;
  ss << ") (";
  if (left)
    ss << "bvlshr " << x << " (bvsub (_ bv" << w << " " << w << ") " << y;
  else
    ss << "bvshl " << x << " (bvsub (_ bv" << w << " " << w << ") " << y;
  ss << ")))";
  return ss.str();
}

std::string emit_expr(const Module &m, int64_t id,
                      const std::unordered_map<int64_t, std::string> &names) {
  const Node &n = m.nodes[id];
  std::ostringstream ss;

  switch (n.kind) {
  case NodeKind::Input: {
    auto it = names.find(id);
    if (it == names.end())
      throw std::runtime_error("Input node " + std::to_string(id) +
                               " has no SMT2 name");
    return it->second;
  }
  case NodeKind::Const:
    return "#b" + n.const_bits.bits;
  case NodeKind::Not:
  case NodeKind::And:
  case NodeKind::Or:
  case NodeKind::Xor:
  case NodeKind::Add:
  case NodeKind::Sub:
  case NodeKind::Mul:
  case NodeKind::Sll:
  case NodeKind::Srl:
  case NodeKind::Sra:
  case NodeKind::Udiv:
  case NodeKind::Sdiv:
  case NodeKind::Urem:
  case NodeKind::Srem:
  case NodeKind::Smod:
  case NodeKind::Neg:
  case NodeKind::Concat:
    return emit_nested_binary(m, id, names, smt2_op(n.kind));
  case NodeKind::Ite: {
    const Node &cond_node = m.nodes[n.operands[0]];
    std::string cond = emit_expr(m, n.operands[0], names);
    std::string then = emit_expr(m, n.operands[1], names);
    std::string els = emit_expr(m, n.operands[2], names);
    if (cond_node.width == 1 && !is_smt_bool_kind(cond_node.kind))
      cond = "(= " + cond + " #b1)";
    return "(ite " + cond + " " + then + " " + els + ")";
  }
  case NodeKind::Ult:
  case NodeKind::Ulte:
  case NodeKind::Ugt:
  case NodeKind::Ugte:
  case NodeKind::Slt:
  case NodeKind::Slte:
  case NodeKind::Sgt:
  case NodeKind::Sgte: {
    std::string expr = emit_nested_binary(m, id, names, smt2_op(n.kind));
    return "(ite " + expr + " #b1 #b0)";
  }
  case NodeKind::Eq: {
    std::string a = emit_expr(m, n.operands[0], names);
    std::string b = emit_expr(m, n.operands[1], names);
    return "(ite (= " + a + " " + b + ") #b1 #b0)";
  }
  case NodeKind::Neq: {
    std::string a = emit_expr(m, n.operands[0], names);
    std::string b = emit_expr(m, n.operands[1], names);
    return "(ite (= " + a + " " + b + ") #b0 #b1)";
  }
  case NodeKind::Iff:
    return "(ite (bvcomp " + emit_expr(m, n.operands[0], names) + " " +
           emit_expr(m, n.operands[1], names) + ") #b1 #b0)";
  case NodeKind::Implies:
    return "(ite (bvor (bvnot " + emit_expr(m, n.operands[0], names) + ") " +
           emit_expr(m, n.operands[1], names) + ") #b1 #b0)";
  case NodeKind::Slice: {
    ss << "((_ extract " << n.param0 << " " << n.param1 << ") "
       << emit_expr(m, n.operands[0], names) << ")";
    return ss.str();
  }
  case NodeKind::Uext: {
    ss << "((_ zero_extend " << n.param0 << ") "
       << emit_expr(m, n.operands[0], names) << ")";
    return ss.str();
  }
  case NodeKind::Sext: {
    ss << "((_ sign_extend " << n.param0 << ") "
       << emit_expr(m, n.operands[0], names) << ")";
    return ss.str();
  }
  case NodeKind::Xnor:
    return "(bvnot (bvxor " + emit_expr(m, n.operands[0], names) + " " +
           emit_expr(m, n.operands[1], names) + "))";
  case NodeKind::Nand:
    return "(bvnot (bvand " + emit_expr(m, n.operands[0], names) + " " +
           emit_expr(m, n.operands[1], names) + "))";
  case NodeKind::Nor:
    return "(bvnot (bvor " + emit_expr(m, n.operands[0], names) + " " +
           emit_expr(m, n.operands[1], names) + "))";
  case NodeKind::RedAnd:
    return "(bvredand " + emit_expr(m, n.operands[0], names) + ")";
  case NodeKind::RedOr:
    return "(bvredor " + emit_expr(m, n.operands[0], names) + ")";
  case NodeKind::RedXor:
    return emit_redxor(m, id, names);
  case NodeKind::Inc: {
    const Node &op = m.nodes[n.operands[0]];
    return "(bvadd " + emit_expr(m, n.operands[0], names) + " (_ bv1 " +
           std::to_string(op.width) + "))";
  }
  case NodeKind::Dec: {
    const Node &op = m.nodes[n.operands[0]];
    return "(bvsub " + emit_expr(m, n.operands[0], names) + " (_ bv1 " +
           std::to_string(op.width) + "))";
  }
  case NodeKind::Rol:
    return emit_rotate(m, id, names, true);
  case NodeKind::Ror:
    return emit_rotate(m, id, names, false);
  default:
    throw std::runtime_error("Unsupported NodeKind in emit_expr: " +
                             kind_to_string(n.kind));
  }
}

std::string Smt2Emitter::emit_to_string(const Module &m) {
  std::ostringstream out;
  std::unordered_map<int64_t, std::string> names;
  std::unordered_set<std::string> name_set;

  names.reserve(m.inputs.size());
  for (int64_t ir_id : m.inputs) {
    const Node &n = m.nodes[ir_id];
    std::string smt2_name = make_safe_name(n.name, n.id, name_set);
    names[ir_id] = smt2_name;
  }

  out << "(set-logic QF_BV)\n";
  out << "(set-info :status unknown)\n";

  for (int64_t ir_id : m.inputs) {
    const Node &n = m.nodes[ir_id];
    auto it = names.find(ir_id);
    if (it == names.end())
      throw std::runtime_error("Input node " + std::to_string(ir_id) +
                               " missing SMT2 name");
    out << "(declare-const " << it->second << " (_ BitVec " << n.width
        << "))\n";
  }

  for (int64_t ir_id : m.constraints) {
    const Node &n = m.nodes[ir_id];
    if (n.operands.empty())
      continue;
    out << "(assert (= " << emit_expr(m, n.operands[0], names) << " #b1))\n";
  }

  for (int64_t ir_id : m.bads) {
    const Node &n = m.nodes[ir_id];
    if (n.operands.empty())
      continue;
    out << "(assert (= " << emit_expr(m, n.operands[0], names) << " #b1))\n";
  }

  out << "(check-sat)\n";
  return out.str();
}

void Smt2Emitter::emit_to_file(const Module &m, const std::string &path) {
  std::ofstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open output file: " + path);
  file << emit_to_string(m);
  if (!file.good())
    throw std::runtime_error("Failed to write to output file: " + path);
}