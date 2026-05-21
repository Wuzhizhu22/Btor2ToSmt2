#include "Smt2Emitter.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

Smt2Emitter::Smt2Emitter(Smt2EmitterOptions options) : d_options(options) {}

/* ========================================================================
 *  Utility helpers
 * ======================================================================== */

/**
 * @brief
 * 名称安全化函数，在项目整体流程中负责将原始节点名称转换为合法且唯一的SMT2标识符
 *
 * 该函数属于Smt2Emitter文件中的基础辅助函数，位于SMT2文本生成流程的最前端。
 * 由于BTOR2输入节点携带的原始名称可能为空、包含非法字符，或与其他名称冲突，
 * 该函数负责在正式输出declare-const语句和中间节点定义之前完成名称规范化。
 *
 * 具体来说，该函数承担三项职责：
 * 1. 当原始名称为空时，基于节点编号生成默认名称 "input_<id>"
 * 2. 将不符合SMT2标识符约束的字符统一替换为下划线 "_"
 * 3. 结合used集合检查名称是否已被占用，必要时自动附加数字后缀以保证唯一性
 *
 * 该函数只处理“输出命名”问题，不负责节点语义分析、位宽推导、表达式构造
 * 或顶层约束组织。这些职责分别由emit_rhs、emit_node_definition和emit_asserts完成。
 *
 * @param raw 原始名称字符串，可能来自BTOR2输入节点的symbol字段
 * @param id 节点唯一编号，当原始名称为空时用于生成默认名称
 * @param used 当前输出过程中已经分配过的名称集合，用于保证返回名称唯一
 * @return 一个符合SMT2标识符规范且在当前输出上下文中唯一的名称
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

  if (!base.empty() && base[0] >= '0' && base[0] <= '9') {
    base = "v_" + base;
  }

  std::string name = base;
  for (int suffix = 0; !used.insert(name).second; suffix++) {
    name = base + "_" + std::to_string(suffix);
  }
  return name;
}

/**
 * @brief
 * 操作符映射函数，在项目整体流程中负责将内部NodeKind映射为对应的SMT2操作符名称
 *
 * 该函数服务于节点右侧表达式的输出阶段。项目内部使用NodeKind表示BTOR2中间表示中的
 * 运算类型，而SMT-LIB
 * v2要求使用标准字符串操作符，例如"bvand"、"bvadd"、"bvshl"等。
 * 本函数通过switch建立二者之间的映射关系，覆盖常见位向量逻辑运算、算术运算和移位运算。
 *
 * 对于不能直接通过单个SMT2操作符表达的节点，例如Eq、Neq、Ite、Slice、扩展、归约运算、
 * 旋转运算等，不在此函数中返回操作符，而由emit_rhs中的专门分支处理。
 *
 * @param k 内部节点操作类型
 * @return
 * 对应的SMT2操作符字符串；若当前类型不适合直接映射为单个操作符，则返回nullptr
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

/**
 * @brief 布尔语义判断函数，在项目整体流程中负责识别哪些节点在SMT2中产生Bool结果
 *
 * 该函数主要服务于emit_rhs中对条件表达式的处理，特别是Ite节点的条件位置。
 * 由于项目内部使用1位位向量和SMT2原生Bool并存的表示方式，输出阶段需要区分：
 * 某个子表达式是已经可以直接出现在SMT2条件位置的Bool，还是仍需通过“= #b1”桥接。
 *
 * 当前被视为SMT2 Bool结果的节点包括：
 * - 无符号比较：Ult、Ulte、Ugt、Ugte
 * - 有符号比较：Slt、Slte、Sgt、Sgte
 * - 相等性比较：Eq、Neq
 * - 逻辑联结：Iff、Implies
 *
 * 该函数只负责“结果类型类别判断”，不负责节点位宽检查，也不直接生成SMT2文本。
 *
 * @param k 内部节点操作类型
 * @return 若该类型在SMT2中产生Bool结果，则返回true；否则返回false
 */
static bool is_smt_bool_kind(NodeKind k) {
  return k == NodeKind::Ult || k == NodeKind::Ulte || k == NodeKind::Ugt ||
         k == NodeKind::Ugte || k == NodeKind::Slt || k == NodeKind::Slte ||
         k == NodeKind::Sgt || k == NodeKind::Sgte || k == NodeKind::Eq ||
         k == NodeKind::Neq || k == NodeKind::Iff || k == NodeKind::Implies;
}

/* ========================================================================
 *  DAG output helpers
 * ======================================================================== */

/**
 * @brief SMT-LIB
 * sort输出函数，在项目整体流程中负责将内部位宽信息转换为SMT2中的BitVec sort文本
 *
 * 该函数属于DAG式SMT2输出的基础工具函数，用于支撑declare-const和define-fun中的sort打印。
 * 当前项目面向位向量为主的BTOR2样例，因此这里统一输出(_ BitVec <width>)形式。
 *
 * 该函数只负责sort文本输出，不负责节点命名、表达式构造或约束组织。
 *
 * @param out 输出流，直接写入SMT-LIB sort文本
 * @param width 位向量宽度
 */
static void emit_sort(std::ostream &out, int64_t width) {
  out << "(_ BitVec " << width << ")";
}

/**
 * @brief
 * 节点命名表构建函数，在项目整体流程中负责为所有需要在SMT2中被引用的节点分配稳定名称
 *
 * 该函数是DAG风格输出的关键准备步骤。与树形递归展开不同，当前Emitter采用
 * “输入声明 + 中间节点define-fun +
 * 顶层assert引用”的输出模式，因此需要预先为可引用节点
 * 分配确定且唯一的SMT2名称。
 *
 * 命名策略如下：
 * 1. Input节点：优先使用原始名称，并通过make_safe_name完成合法化与去重
 * 2. 普通中间节点：统一分配为 "n<id>" 形式，便于调试和稳定引用
 * 3. Const节点：不单独命名，后续在RHS中直接以内联常量输出
 * 4. Constraint和Bad节点：不单独命名，其唯一操作数会在顶层assert中被直接引用
 *
 * 该函数只建立“节点ID ->
 * SMT2名称”的映射，不负责输出文本，也不判断节点拓扑顺序。
 *
 * @param m 已加载并解析完成的BTOR2模块对象
 * @return 一个包含所有可命名节点的名称映射表
 */
static NameMap build_node_names(const Module &m) {
  NameMap names;
  std::unordered_set<std::string> used;

  for (int64_t ir_id : m.inputs) {
    const Node &n = m.nodes[ir_id];
    names[ir_id] = make_safe_name(n.name, n.id, used);
  }

  for (const Node &n : m.nodes) {
    if (n.kind != NodeKind::Input && n.kind != NodeKind::Const &&
        n.kind != NodeKind::Constraint && n.kind != NodeKind::Bad) {
      names[n.id] = "n" + std::to_string(n.id);
    }
  }

  return names;
}

/**
 * @brief
 * 节点引用输出函数，在项目整体流程中负责将某个节点按“可引用形式”写入SMT2文本
 *
 * 该函数服务于DAG风格输出中的“引用而非展开”策略。对于某个子节点，Emitter不再递归打印其整棵子树，
 * 而是根据节点种类决定如何在当前上下文中引用它：
 * 1. Input节点：输出其已分配的输入名称
 * 2. Const节点：直接以内联常量形式输出
 * 3. 普通中间节点：输出其稳定名字，如n42
 *
 * 该函数是emit_rhs的基础支撑，用来避免共享子表达式被重复展开。
 * 它只负责“当前节点怎样被引用”，不负责当前节点自身的定义式输出。
 *
 * @param out 输出流，直接写入节点引用文本
 * @param m 当前模块对象
 * @param node_id 被引用节点的内部编号
 * @param names 节点名称映射表
 */
static void emit_node_ref(std::ostream &out, const Module &m, int64_t node_id,
                          const NameMap &names) {
  const Node &n = m.nodes[node_id];

  switch (n.kind) {
  case NodeKind::Input: {
    auto it = names.find(node_id);
    if (it != names.end())
      out << it->second;
    else
      out << "input_" << node_id;
    break;
  }
  case NodeKind::Const:
    out << "#b" << n.const_bits.bits;
    break;
  default: {
    auto it = names.find(node_id);
    if (it != names.end())
      out << it->second;
    else
      out << "n" << node_id;
    break;
  }
  }
}

/**
 * @brief
 * 节点右侧表达式输出函数，在项目整体流程中负责为单个中间节点生成define-fun右侧的SMT2表达式
 *
 * 该函数是DAG式Emitter的核心内部函数。它与旧版递归emit_expr的最大区别在于：
 * 当前函数只负责生成“当前节点自身”的右侧表达式，而其子节点统一通过emit_node_ref引用，
 * 不再递归展开整棵表达式树。
 *
 * 这样做的好处是：
 * 1. 每个中间节点只定义一次，避免共享子式重复展开
 * 2. 输出体积更可控
 * 3. 大用例下不再需要构造超长的单棵表达式字符串
 *
 * 本函数覆盖当前项目已支持的主要节点类型，包括：
 * - 通用位向量运算：Not、And、Or、Xor、Add、Sub、Mul、Shift、Div/Rem等
 * - 比较与逻辑联结：Eq、Neq、Iff、Implies及各类有符号/无符号比较
 * - 条件表达式：Ite
 * - 位操作：Slice、Uext、Sext、Concat
 * - 派生操作：Xnor、Nand、Nor、RedAnd、RedOr、RedXor、Inc、Dec、Rol、Ror
 *
 * @param out 输出流，直接写入当前节点RHS表达式
 * @param m 当前模块对象，包含全部节点图结构
 * @param n 当前要生成定义的节点
 * @param names 节点名称映射表
 * @throws std::runtime_error 若遇到当前版本尚未支持的NodeKind
 */
void Smt2Emitter::emit_rhs(std::ostream &out, const Module &m, const Node &n,
                           const NameMap &names) {
  switch (n.kind) {
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
  case NodeKind::Concat: {
    const char *op = smt2_op(n.kind);
    out << "(" << op;
    for (int64_t op_id : n.operands) {
      out << " ";
      emit_node_ref(out, m, op_id, names);
    }
    out << ")";
    break;
  }

  case NodeKind::Ite: {
    const Node &cond_node = m.nodes[n.operands[0]];
    if (cond_node.width == 1) {
      out << "(ite (= ";
      emit_node_ref(out, m, n.operands[0], names);
      out << " #b1) ";
    } else {
      out << "(ite ";
      emit_node_ref(out, m, n.operands[0], names);
      out << " ";
    }
    emit_node_ref(out, m, n.operands[1], names);
    out << " ";
    emit_node_ref(out, m, n.operands[2], names);
    out << ")";
    break;
  }

  case NodeKind::Ult:
  case NodeKind::Ulte:
  case NodeKind::Ugt:
  case NodeKind::Ugte:
  case NodeKind::Slt:
  case NodeKind::Slte:
  case NodeKind::Sgt:
  case NodeKind::Sgte: {
    const char *op = smt2_op(n.kind);
    out << "(ite (" << op;
    for (int64_t op_id : n.operands) {
      out << " ";
      emit_node_ref(out, m, op_id, names);
    }
    out << ") #b1 #b0)";
    break;
  }

  case NodeKind::Eq: {
    out << "(ite (= ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ") #b1 #b0)";
    break;
  }

  case NodeKind::Neq: {
    out << "(ite (= ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ") #b0 #b1)";
    break;
  }

  case NodeKind::Iff: {
    out << "(ite (= (bvcomp ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ") #b1) #b1 #b0)";
    break;
  }

  case NodeKind::Implies: {
    out << "(ite (= (bvor (bvnot ";
    emit_node_ref(out, m, n.operands[0], names);
    out << ") ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ") #b1) #b1 #b0)";
    break;
  }

  case NodeKind::Slice: {
    out << "((_ extract " << n.param0 << " " << n.param1 << ") ";
    emit_node_ref(out, m, n.operands[0], names);
    out << ")";
    break;
  }

  case NodeKind::Uext: {
    out << "((_ zero_extend " << n.param0 << ") ";
    emit_node_ref(out, m, n.operands[0], names);
    out << ")";
    break;
  }

  case NodeKind::Sext: {
    out << "((_ sign_extend " << n.param0 << ") ";
    emit_node_ref(out, m, n.operands[0], names);
    out << ")";
    break;
  }

  case NodeKind::Xnor: {
    out << "(bvnot (bvxor ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << "))";
    break;
  }

  case NodeKind::Nand: {
    out << "(bvnot (bvand ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << "))";
    break;
  }

  case NodeKind::Nor: {
    out << "(bvnot (bvor ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << "))";
    break;
  }

  case NodeKind::RedAnd: {
    if (d_options.strict_smtlib) {
      const Node &op = m.nodes[n.operands[0]];
      out << "(ite (= ";
      emit_node_ref(out, m, n.operands[0], names);
      out << " (bvnot (_ bv0 " << op.width << "))) #b1 #b0)";
    } else {
      out << "(bvredand ";
      emit_node_ref(out, m, n.operands[0], names);
      out << ")";
    }
    break;
  }

  case NodeKind::RedOr: {
    if (d_options.strict_smtlib) {
      const Node &op = m.nodes[n.operands[0]];
      out << "(ite (distinct ";
      emit_node_ref(out, m, n.operands[0], names);
      out << " (_ bv0 " << op.width << ")) #b1 #b0)";
    } else {
      out << "(bvredor ";
      emit_node_ref(out, m, n.operands[0], names);
      out << ")";
    }
    break;
  }

  case NodeKind::RedXor: {
    const Node &x = m.nodes[n.operands[0]];
    int64_t w = x.width;
    if (w == 1) {
      emit_node_ref(out, m, n.operands[0], names);
      return;
    }

    out << "((_ extract 0 0) ";
    for (int64_t i = w - 1; i >= 0; i--) {
      if (i > 0)
        out << "(bvxor ";
      out << "((_ extract " << i << " " << i << ") ";
      emit_node_ref(out, m, n.operands[0], names);
      out << ")";
      if (i > 0 && i < w - 1)
        out << " ";
    }
    for (int64_t i = 0; i < w - 1; i++)
      out << ")";
    out << ")";
    break;
  }

  case NodeKind::Inc: {
    const Node &op = m.nodes[n.operands[0]];
    out << "(bvadd ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " (_ bv1 " << op.width << "))";
    break;
  }

  case NodeKind::Dec: {
    const Node &op = m.nodes[n.operands[0]];
    out << "(bvsub ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " (_ bv1 " << op.width << "))";
    break;
  }

  case NodeKind::Rol: {
    const Node &op_node = m.nodes[n.operands[0]];
    int64_t w = op_node.width;
    out << "(bvor (bvshl ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ") (bvlshr ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " (bvsub (_ bv" << w << " " << w << ") ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ")))";
    break;
  }

  case NodeKind::Ror: {
    const Node &op_node = m.nodes[n.operands[0]];
    int64_t w = op_node.width;
    out << "(bvor (bvlshr ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ") (bvshl ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " (bvsub (_ bv" << w << " " << w << ") ";
    emit_node_ref(out, m, n.operands[1], names);
    out << ")))";
    break;
  }

  default:
    throw std::runtime_error("Unsupported NodeKind in emit_rhs: " +
                             kind_to_string(n.kind));
  }
}

/**
 * @brief 节点定义输出函数，在项目整体流程中负责为单个中间节点生成完整的SMT2
 * define-fun定义
 *
 * 该函数位于DAG风格输出的中间层，作用是把一个普通中间节点写成如下形式：
 *
 *   (define-fun n42 () (_ BitVec 8) <rhs>)
 *
 * 其中：
 * - 节点名称来自预先构建的NameMap
 * - sort由emit_sort输出
 * - 右侧表达式由emit_rhs生成
 *
 * 对于不需要独立定义的节点（例如const、constraint、bad或未分配名称的节点），
 * 本函数会直接返回，不输出任何内容。
 *
 * @param out 输出流，直接写入单条define-fun定义
 * @param m 当前模块对象
 * @param n 当前要输出定义的节点
 * @param names 节点名称映射表
 */
void Smt2Emitter::emit_node_definition(std::ostream &out, const Module &m,
                                       const Node &n, const NameMap &names) {
  auto it = names.find(n.id);
  if (it == names.end())
    return;

  out << "(define-fun " << it->second << " () ";
  emit_sort(out, n.width);
  out << " ";
  emit_rhs(out, m, n, names);
  out << ")\n";
}

/**
 * @brief
 * 顶层断言输出函数，在项目整体流程中负责将constraint和bad节点转换为最终SMT2
 * assert语句
 *
 * 该函数属于模块级输出收尾阶段。当前IR中的Constraint和Bad节点本身只是顶层包装节点，
 * 它们真正需要被求解器断言的是其唯一操作数所表示的布尔/1位位向量条件。
 *
 * 当前输出策略统一为：
 *   (assert (= <operand-ref> #b1))
 *
 * 这样可以保持与项目当前“比较结果和谓词结果均以BV1承载”的内部表示一致，
 * 避免在顶层断言时混入新的Bool/BV桥接策略。
 *
 * 该函数只负责输出顶层assert，不参与中间节点定义生成。
 *
 * @param out 输出流，直接写入assert语句
 * @param m 当前模块对象
 * @param names 节点名称映射表
 */
void Smt2Emitter::emit_asserts(std::ostream &out, const Module &m,
                               const NameMap &names) {
  for (int64_t ir_id : m.constraints) {
    const Node &n = m.nodes[ir_id];
    if (n.operands.empty())
      continue;
    out << "(assert (= ";
    emit_node_ref(out, m, n.operands[0], names);
    out << " #b1))\n";
  }

  if (m.bads.empty())
    return;

  if (m.bads.size() == 1) {
    const Node &n = m.nodes[m.bads[0]];
    if (!n.operands.empty()) {
      out << "(assert (= ";
      emit_node_ref(out, m, n.operands[0], names);
      out << " #b1))\n";
    }
  } else {
    out << "(assert (or";
    for (int64_t ir_id : m.bads) {
      const Node &n = m.nodes[ir_id];
      if (n.operands.empty())
        continue;
      out << " (= ";
      emit_node_ref(out, m, n.operands[0], names);
      out << " #b1)";
    }
    out << "))\n";
  }
}

/* ========================================================================
 *  Public interface
 * ======================================================================== */

/**
 * @brief
 * 模块流式输出函数，在项目整体流程中作为Smt2Emitter类的核心输出接口生成完整SMT2文本
 *
 * 该函数是当前DAG风格Emitter的主入口，负责将已经加载完成的模块对象按固定四段结构
 * 直接输出到目标流中：
 *
 * 1. Header：输出(set-logic QF_BV)和状态信息
 * 2. Input declarations：为输入节点生成declare-const
 * 3. Derived node definitions：为中间节点生成define-fun
 * 4. Top-level asserts：输出constraint和bad对应的assert语句
 *
 * 当前实现采用“节点命名 + 定义式输出 + 顶层引用”的策略，而不是递归树形展开，
 * 因此更适合共享子表达式较多的大型用例，也为后续进一步引入let-sharing或use-count控制
 * 预留了空间。
 *
 * @param m 已加载并解析完成的BTOR2模块对象
 * @param out 输出流，直接写入完整SMT-LIB v2文本
 * @throws std::runtime_error 若输入节点缺失名称映射
 */
void Smt2Emitter::emit_to_stream(const Module &m, std::ostream &out) {
  NameMap names = build_node_names(m);

  out << "(set-logic QF_BV)\n";
  out << "(set-info :status unknown)\n";

  for (int64_t ir_id : m.inputs) {
    const Node &n = m.nodes[ir_id];
    auto it = names.find(ir_id);
    if (it == names.end()) {
      throw std::runtime_error("Input node " + std::to_string(ir_id) +
                               " missing SMT2 name");
    }
    out << "(declare-const " << it->second << " (_ BitVec " << n.width
        << "))\n";
  }

  for (const Node &n : m.nodes) {
    switch (n.kind) {
    case NodeKind::Input:
    case NodeKind::Const:
    case NodeKind::Constraint:
    case NodeKind::Bad:
      continue;
    default:
      this->emit_node_definition(out, m, n, names);
    }
  }

  this->emit_asserts(out, m, names);
  out << "(check-sat)\n";
}

/**
 * @brief
 * SMT2字符串输出函数，在项目整体流程中作为便于调试和测试的辅助接口生成完整SMT2文本
 *
 * 该函数通过std::ostringstream调用emit_to_stream，将流式输出结果汇总为一个完整字符串返回。
 * 由于该接口会在内存中构造整份SMT2文本，因此更适合：
 * - 小规模用例调试
 * - 单元测试中的字符串比对
 * - 输出格式快速检查
 *
 * 对于大规模benchmark或正式文件导出，推荐优先调用emit_to_file或直接使用emit_to_stream，
 * 以避免不必要的完整字符串驻留内存。
 *
 * @param m 已加载并解析完成的BTOR2模块对象
 * @return 一份完整的SMT-LIB v2字符串
 */
std::string Smt2Emitter::emit_to_string(const Module &m) {
  std::ostringstream out;
  emit_to_stream(m, out);
  return out.str();
}

/**
 * @brief
 * SMT2文件输出函数，在项目整体流程中作为最终输出接口将转换结果写入指定文件
 *
 * 该函数是Smt2Emitter类的公有方法，提供将SMT2格式输出到文件的便捷方式。
 * 它封装了文件输出流程：先打开指定路径的文件，再调用emit_to_stream将内容直接写入，
 * 最后检查写入状态是否正常。若文件无法打开或写入失败，则抛出runtime_error异常。
 *
 * 在当前项目结构中，该函数是面向外部调用方的最终出口函数，用于完成从模块级IR
 * 到磁盘文件的最后一环转换。调用方无需关心名称管理、中间节点定义输出或assert组织细节。
 *
 * @param m 已加载并解析完成的BTOR2模块对象
 * @param path 目标SMT2输出文件路径
 * @throws std::runtime_error 若文件无法打开或写入失败
 */
void Smt2Emitter::emit_to_file(const Module &m, const std::string &path) {
  std::ofstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open output file: " + path);

  emit_to_stream(m, file);

  if (!file.good())
    throw std::runtime_error("Failed to write to output file: " + path);
}