#ifndef BTOR2TOSMT2_IR_H_INCLUDED
#define BTOR2TOSMT2_IR_H_INCLUDED

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class NodeKind {
  Input,
  Const,
  Not,
  And,
  Or,
  Xor,
  Eq,
  Neq,
  Ite,
  Concat,
  Slice,
  Uext,
  Sext,
  Add,
  Sub,
  Mul,
  Sll,
  Srl,
  Sra,
  Ult,
  Ulte,
  Ugt,
  Ugte,
  Slt,
  Slte,
  Sgt,
  Sgte,
  Constraint,
  Bad,

  Udiv,
  Sdiv,
  Urem,
  Srem,
  Smod,
  Inc,
  Dec,
  Neg,
  RedAnd,
  RedOr,
  RedXor,
  Iff,
  Implies,
  Nand,
  Nor,
  Xnor,
  Rol,
  Ror,
};

struct ConstBits {
  std::string bits;
  int64_t width;
};

struct Node {
  int64_t id = -1;
  NodeKind kind;
  int64_t width = 0;
  std::vector<int64_t> operands;
  std::string name;
  ConstBits const_bits;

  int64_t param0 = 0;
  int64_t param1 = 0;

  int64_t src_btor2_id = -1;
};

struct Module {
  std::vector<Node> nodes;

  std::vector<int64_t> inputs;
  std::vector<int64_t> constraints;
  std::vector<int64_t> bads;

  std::unordered_map<int64_t, int64_t> btor2_to_ir;
};

std::string kind_to_string(NodeKind k);
bool is_bool_width(int64_t w);
bool is_bool_node(const Node &n);

#endif