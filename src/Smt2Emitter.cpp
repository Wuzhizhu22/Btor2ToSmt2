#include "Smt2Emitter.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

/**
 * @brief 名称安全性处理工具函数，在SMT2格式化输出过程中，将原始名称转换为符合SMT2语法规范的标识符
 *
 * 该函数是SMT2输出流程的前置处理步骤。由于SMT2对标识符有严格的字符集要求
 * （只能包含字母、数字及特定符号），此函数负责：
 * 1. 对空名称自动生成 "input_<id>" 格式的替代名称
 * 2. 将所有非允许字符替换为下划线 "_"
 * 3. 通过used集合确保生成的名称不与已使用名称冲突，必要时添加数字后缀
 *
 * @param raw 原始名称字符串，可能包含任意字符
 * @param id 节点的唯一标识符，用于生成默认名称
 * @param used 已使用的名称集合，用于保证名称唯一性
 * @return 符合SMT2规范的字符串标识符
 */
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

/**
 * @brief 节点操作符到SMT2操作符字符串的映射函数，在表达式输出过程中将内部节点操作符转换为SMT2格式的操作符名称
 *
 * 该函数是SMT2表达式生成的核心辅助函数。项目使用NodeKind枚举表示BTOR2中间表示中的操作类型，
 * 而SMT2格式要求使用特定的字符串操作符名称（如"bvand"、"bvadd"等）。
 * 此函数通过switch语句建立两者之间的映射关系，覆盖位向量常见的逻辑运算、算术运算和移位操作。
 * 对于不支持的操作符类型，返回nullptr，由调用者处理异常情况。
 *
 * @param k 内部节点操作符类型（NodeKind枚举值）
 * @return 对应的SMT2操作符字符串指针，若不支持则返回nullptr
 */
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

/**
 * @brief 二元操作嵌套格式化输出函数，在SMT2表达式生成过程中将二元操作以嵌套括号形式输出
 *
 * 该函数是emit_expr的辅助函数，负责处理具有两个或多个操作数的二元操作节点。
 * SMT2格式要求操作符采用前缀表示法，格式为：(op operand1 operand2 ...)
 * 此函数遍历节点的所有操作数，递归调用emit_expr生成每个操作数的SMT2表示，
 * 最终组合成完整的嵌套表达式字符串返回。
 *
 * @param m 模块引用，包含所有节点的完整图结构
 * @param id 当前要处理的节点标识符
 * @param names 节点ID到SMT2名称的映射表
 * @param op 要使用的SMT2操作符字符串
 * @return 格式化后的SMT2二元表达式字符串
 */
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

/**
 * @brief SMT2布尔类型判断函数，在条件表达式处理过程中判断某操作符类型在SMT2中是否表示布尔值
 *
 * 该函数用于识别哪些NodeKind在SMT2格式化输出时需要作为布尔类型处理。
 * 由于SMT2的布尔类型和位向量类型在某些操作（如ite条件表达式）中的处理方式不同，
 * 此函数预先定义了一组在SMT2中产生布尔结果的操作符类型，包括：
 * 无符号比较（Ult, Ulte, Ugt, Ugte）、有符号比较（Slt, Slte, Sgt, Sgte）、
 * 相等性比较（Eq, Neq）以及逻辑联结词（Iff, Implies）。
 *
 * @param k 节点操作符类型（NodeKind枚举值）
 * @return 如果该操作符在SMT2中表示布尔类型则返回true，否则返回false
 */
static bool is_smt_bool_kind(NodeKind k) {
  return k == NodeKind::Ult || k == NodeKind::Ulte || k == NodeKind::Ugt ||
         k == NodeKind::Ugte || k == NodeKind::Slt || k == NodeKind::Slte ||
         k == NodeKind::Sgt || k == NodeKind::Sgte || k == NodeKind::Eq ||
         k == NodeKind::Neq || k == NodeKind::Iff || k == NodeKind::Implies;
}

/**
 * @brief 归约异或运算发射函数，在SMT2表达式生成过程中将RedXor节点转换为SMT2格式的归约异或表达式
 *
 * 该函数是emit_expr处理RedXor（归约异或）节点的专用辅助函数。
 * 归约异或操作将一个多位位向量所有位进行连续异或运算，最终产生一位结果。
 * SMT2没有直接的归约异或操作符，因此此函数通过以下方式实现：
 * 1. 对于宽度为1的向量，直接返回操作数本身
 * 2. 对于多位向量，使用extract逐位提取后通过嵌套bvxor操作两两异或
 * 3. 最终通过extract(0,0)获取最低位作为归约结果
 *
 * @param m 模块引用，包含所有节点的完整图结构
 * @param id RedXor节点的标识符
 * @param names 节点ID到SMT2名称的映射表
 * @return 归约异或运算的SMT2表达式字符串
 */
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

/**
 * @brief 循环移位运算发射函数，在SMT2表达式生成过程中将Rol/Ror节点转换为循环移位表达式
 *
 * 该函数是emit_expr处理Rol（循环左移）和Ror（循环右移）节点的专用辅助函数。
 * 循环移位将位向量的位向指定方向循环移动，移出的位从另一端移入。
 * SMT2没有直接的循环移位操作符，此函数通过逻辑移位和位或操作组合实现：
 * 以左旋为例：先将向量左移y位，再将向量右移(w-y)位，最后将两部分结果进行位或。
 * 右旋则方向相反。这种实现利用了循环移位的数学性质。
 *
 * @param m 模块引用，包含所有节点的完整图结构
 * @param id Rol或Ror节点的标识符
 * @param names 节点ID到SMT2名称的映射表
 * @param left 若为true表示左旋，为false表示右旋
 * @return 循环移位运算的SMT2表达式字符串
 */
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

/**
 * @brief SMT2表达式发射核心函数，在项目转换流程中负责将BTOR2中间表示的单个节点转换为SMT2格式的表达式字符串
 *
 * 该函数是整个Btor2ToSmt2转换引擎的核心函数，位于将BTOR2格式转换为SMT2格式的关键路径上。
 * 它接收一个模块和一个节点ID，递归地将该节点及其所有子节点转换为符合SMT2语法规范的字符串表示。
 * 函数通过switch语句分派到不同的处理分支：
 * - 基础类型（Input、Const）：直接返回对应的SMT2标识符或常量
 * - 二元操作（And、Or、Add等）：调用emit_nested_binary处理
 * - 条件表达式（Ite）：处理时需区分布尔和位向量条件
 * - 比较操作（Ult、Slt等）：需要包装为ite表达式转换为位向量
 * - 位操作（Slice、Uext、Sext）：使用SMT2的extract和extend操作符
 * - 归约操作（RedAnd、RedOr、RedXor）：调用专用辅助函数处理
 * - 特殊操作（Inc、Dec、Rol、Ror等）：分别调用相应辅助函数
 *
 * @param m 模块引用，包含所有节点的完整图结构
 * @param id 要转换的节点在模块中的唯一标识符
 * @param names 节点ID到已处理的SMT2名称的映射表，用于解析Input节点的命名
 * @return 对应节点的SMT2表达式字符串
 */
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

/**
 * @brief 模块到SMT2字符串转换函数，在项目整体转换流程中作为Smt2Emitter类的核心输出接口
 *
 * 该函数是Smt2Emitter类的主要公有方法，负责将完整的BTOR2模块转换为SMT2格式的字符串表示。
 * 这是项目转换流程的最终输出阶段，生成的字符串可直接用于SMT求解器。
 * 转换过程包括以下步骤：
 * 1. 名称管理：为所有输入节点生成符合SMT2规范的标识符名称
 * 2. 设置SMT2元信息：输出(set-logic QF_BV)和(set-info :status unknown)
 * 3. 声明输入变量：为每个模块输入生成declare-const语句，指定位向量宽度
 * 4. 声明约束：将BTOR2的constraint节点转换为SMT2的assert语句
 * 5. 声明Bad状态：将BTOR2的bad节点转换为assert语句
 * 6. 输出求解指令：输出(check-sat)表示请求SMT求解器求解
 *
 * @param m 已加载并解析完成的BTOR2模块对象
 * @return 完整的SMT2格式字符串，可直接写入文件或发送给SMT求解器
 */
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

/**
 * @brief SMT2文件输出函数，在项目整体流程中作为最终输出接口将转换结果写入指定文件
 *
 * 该函数是Smt2Emitter类的公有方法，提供将SMT2格式输出到文件的便捷方式。
 * 它封装了文件操作流程：先打开指定路径的文件，将emit_to_string生成的SMT2字符串写入，
 * 然后验证写入是否成功。若文件无法打开或写入失败，则抛出runtime_error异常。
 * 此函数是用户直接调用的出口函数，完成从BTOR2到SMT2转换的最后一环。
 *
 * @param m 已加载并解析完成的BTOR2模块对象
 * @param path 要写入的SMT2文件路径
 * @throws std::runtime_error 若文件无法打开或写入失败
 */
void Smt2Emitter::emit_to_file(const Module &m, const std::string &path) {
  std::ofstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open output file: " + path);
  file << emit_to_string(m);
  if (!file.good())
    throw std::runtime_error("Failed to write to output file: " + path);
}