#include "IR.h"

/**
 * @brief
 * 节点操作符类型到字符串的转换函数，在项目调试和日志输出过程中提供可读的操作符名称
 *
 * 该函数是IR模块的辅助函数，负责将NodeKind枚举值转换为人类可读的字符串表示。
 * 在verbose模式和错误消息中用于标识节点类型，便于调试和理解模块结构。
 * 函数覆盖了BTOR2 IR支持的所有节点操作符类型。
 *
 * @param k NodeKind枚举值
 * @return 对应的字符串名称，如"Add"、"And"、"Input"等
 */
std::string kind_to_string(NodeKind k) {
  switch (k) {
  case NodeKind::Input:
    return "Input";
  case NodeKind::Const:
    return "Const";
  case NodeKind::Not:
    return "Not";
  case NodeKind::And:
    return "And";
  case NodeKind::Or:
    return "Or";
  case NodeKind::Xor:
    return "Xor";
  case NodeKind::Eq:
    return "Eq";
  case NodeKind::Neq:
    return "Neq";
  case NodeKind::Ite:
    return "Ite";
  case NodeKind::Concat:
    return "Concat";
  case NodeKind::Slice:
    return "Slice";
  case NodeKind::Uext:
    return "Uext";
  case NodeKind::Sext:
    return "Sext";
  case NodeKind::Add:
    return "Add";
  case NodeKind::Sub:
    return "Sub";
  case NodeKind::Mul:
    return "Mul";
  case NodeKind::Sll:
    return "Sll";
  case NodeKind::Srl:
    return "Srl";
  case NodeKind::Sra:
    return "Sra";
  case NodeKind::Ult:
    return "Ult";
  case NodeKind::Ulte:
    return "Ulte";
  case NodeKind::Ugt:
    return "Ugt";
  case NodeKind::Ugte:
    return "Ugte";
  case NodeKind::Slt:
    return "Slt";
  case NodeKind::Slte:
    return "Slte";
  case NodeKind::Sgt:
    return "Sgt";
  case NodeKind::Sgte:
    return "Sgte";
  case NodeKind::Constraint:
    return "Constraint";
  case NodeKind::Bad:
    return "Bad";
  case NodeKind::Udiv:
    return "Udiv";
  case NodeKind::Sdiv:
    return "Sdiv";
  case NodeKind::Urem:
    return "Urem";
  case NodeKind::Srem:
    return "Srem";
  case NodeKind::Smod:
    return "Smod";
  case NodeKind::Inc:
    return "Inc";
  case NodeKind::Dec:
    return "Dec";
  case NodeKind::Neg:
    return "Neg";
  case NodeKind::RedAnd:
    return "RedAnd";
  case NodeKind::RedOr:
    return "RedOr";
  case NodeKind::RedXor:
    return "RedXor";
  case NodeKind::Iff:
    return "Iff";
  case NodeKind::Implies:
    return "Implies";
  case NodeKind::Nand:
    return "Nand";
  case NodeKind::Nor:
    return "Nor";
  case NodeKind::Xnor:
    return "Xnor";
  case NodeKind::Rol:
    return "Rol";
  case NodeKind::Ror:
    return "Ror";
  }
  return "Unknown";
}

/**
 * @brief 布尔宽度判断函数，在类型检查过程中判断给定宽度是否为布尔类型（单比特）
 *
 * 该函数是IR模块的辅助函数，用于快速判断一个位向量宽度是否表示布尔值。
 * 在BTOR2/IR中，布尔类型实际上是一比特位向量（width=1）。
 * 此函数封装了这一约定，便于在各处进行布尔类型判断。
 *
 * @param w 位向量宽度
 * @return 若宽度为1则返回true，表示布尔类型；否则返回false
 */
bool is_bool_width(int64_t w) { return w == 1; }

/**
 * @brief 布尔节点判断函数，在类型检查过程中判断给定节点是否为布尔类型
 *
 * 该函数是IR模块的辅助函数，用于判断一个节点是否为布尔类型。
 * 节点是否为布尔类型取决于其width属性是否为1。
 * 此函数提供了一种统一的方式来检查节点类型，无需直接访问width属性。
 *
 * @param n 要检查的节点引用
 * @return 若节点宽度为1则返回true，表示布尔节点；否则返回false
 */
bool is_bool_node(const Node &n) { return n.width == 1; }