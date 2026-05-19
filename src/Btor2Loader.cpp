#include "Btor2Loader.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

/**
 * @brief 十进制字符串到二进制字符串转换函数，在BTOR2解析过程中将十进制常量转换为位向量二进制表示
 *
 * 该函数是BTOR2解析器处理constd指令（十进制常量）的核心辅助函数。
 * BTOR2格式支持以十进制和十六进制表示常量，此函数负责将十进制字符串转换为固定宽度的二进制字符串。
 * 转换过程采用除2取余法模拟二进制除法运算。对于负数，采用二进制补码表示法：
 * 1. 先将绝对值转换为二进制
 * 2. 按位取反
 * 3. 加1得到补码
 * 最终结果会被填充或截断至指定的width宽度。
 *
 * @param dec_str 十进制数字字符串，可能包含负号前缀
 * @param width 目标位向量宽度
 * @return 转换后的二进制字符串表示
 */
static std::string dec_to_bin_str(const std::string &dec_str, int64_t width) {
  std::string bits;
  std::string val = dec_str;

  bool negative = false;
  if (!val.empty() && val[0] == '-') {
    negative = true;
    val = val.substr(1);
  }

  while (!val.empty() && val != "0") {
    int carry = 0;
    std::string next;
    for (char c : val) {
      int cur = carry * 10 + (c - '0');
      next.push_back(static_cast<char>('0' + cur / 2));
      carry = cur % 2;
    }
    bits.push_back(static_cast<char>('0' + carry));
    val = next;
    val.erase(0, val.find_first_not_of('0'));
    if (val.empty())
      val = "0";
  }

  if (bits.empty())
    bits = "0";

  std::reverse(bits.begin(), bits.end());

  if (negative) {
    std::string twos(width, '0');
    for (int64_t i = 0; i < static_cast<int64_t>(bits.size()); i++) {
      twos[width - static_cast<int64_t>(bits.size()) + i] = bits[i];
    }
    for (auto &b : twos)
      b = (b == '0') ? '1' : '0';
    int64_t carry = 1;
    for (int64_t i = width - 1; i >= 0; i--) {
      int sum = (twos[i] - '0') + carry;
      twos[i] = static_cast<char>('0' + sum % 2);
      carry = sum / 2;
    }
    return twos;
  }

  if (static_cast<int64_t>(bits.size()) < width) {
    bits = std::string(width - static_cast<int64_t>(bits.size()), '0') + bits;
  }
  return bits;
}

/**
 * @brief 十六进制字符串到二进制字符串转换函数，在BTOR2解析过程中将十六进制常量转换为位向量二进制表示
 *
 * 该函数是BTOR2解析器处理consth指令（十六进制常量）的核心辅助函数。
 * 十六进制数字到二进制的转换通过查表实现：每个十六进制字符（0-9, a-f, A-F）
 * 对应4位二进制表示（0000-1111）。函数遍历十六进制字符串的每个字符，
 * 查表获取对应的4位二进制片段，最后根据width参数进行填充或截断。
 *
 * @param hex_str 十六进制数字字符串，支持大小写
 * @param width 目标位向量宽度
 * @return 转换后的二进制字符串表示
 * @throws std::runtime_error 遇到无效的十六进制字符时抛出
 */
static std::string hex_to_bin_str(const std::string &hex_str, int64_t width) {
  static const std::string hex_to_bin[16] = {
      "0000", "0001", "0010", "0011", "0100", "0101", "0110", "0111",
      "1000", "1001", "1010", "1011", "1100", "1101", "1110", "1111",
  };

  std::string bits;
  for (char c : hex_str) {
    if (c >= '0' && c <= '9')
      bits += hex_to_bin[c - '0'];
    else if (c >= 'a' && c <= 'f')
      bits += hex_to_bin[c - 'a' + 10];
    else if (c >= 'A' && c <= 'F')
      bits += hex_to_bin[c - 'A' + 10];
    else
      throw std::runtime_error("Invalid hex character: " + std::string(1, c));
  }

  if (static_cast<int64_t>(bits.size()) > width) {
    bits = bits.substr(bits.size() - width);
  } else if (static_cast<int64_t>(bits.size()) < width) {
    bits = std::string(width - static_cast<int64_t>(bits.size()), '0') + bits;
  }
  return bits;
}

/**
 * @brief 位向量填充或截断函数，在常量解析过程中确保位字符串符合指定宽度要求
 *
 * 该函数是BTOR2常量解析的辅助函数，负责将位字符串调整为指定的宽度。
 * 当位字符串长度大于width时，从低位截断多余部分；当长度小于width时，
 * 在高位填充0以达到指定宽度。这确保了后续处理中所有位向量具有统一的长度表示。
 *
 * @param bits 原始位字符串，通常来自常量解析
 * @param width 目标位向量宽度
 * @return 调整后的位字符串，长度恰好等于width
 */
static std::string pad_or_trunc(const std::string &bits, int64_t width) {
  if (static_cast<int64_t>(bits.size()) > width)
    return bits.substr(bits.size() - width);
  if (static_cast<int64_t>(bits.size()) < width)
    return std::string(width - static_cast<int64_t>(bits.size()), '0') + bits;
  return bits;
}

/**
 * @brief BTOR2文件加载与解析函数，在项目整体流程中作为数据导入的核心入口
 *
 * 该函数是Btor2Loader类的核心公有方法，负责将BTOR2格式的文件加载并解析为内部的Module结构。
 * 这是项目数据流的第一阶段——从外部文件格式到内部中间表示（IR）的转换。
 * 解析过程包括：
 * 1. 文件I/O：打开并逐行读取BTOR2文件
 * 2. 词法解析：将每行分割为标记（token）流
 * 3. 命令处理：识别sort、input、const、constd、consth、zero、one、ones、bad、constraint等命令
 * 4. 节点构建：为每条命令创建对应的IR节点，建立BTOR2 ID到IR ID的映射
 * 5. 操作数解析：处理负ID表示的隐式取反操作
 *
 * 函数内部定义了多个lambda辅助函数：
 * - add_node: 向模块添加节点并分配IR ID
 * - resolve_operand: 解析操作数ID，处理负ID的取反情况
 * - get_width: 从sort_map获取位向量宽度
 * - map_op: 将BTOR2操作符字符串映射到NodeKind枚举
 *
 * @param path 要加载的BTOR2文件路径
 * @return 解析后生成的Module对象，包含所有节点、输入、约束和bad状态
 * @throws std::runtime_error 文件无法打开或遇到无效格式时抛出
 */
Module Btor2Loader::load_from_file(const std::string &path) {
  std::ifstream file(path);
  if (!file.is_open())
    throw std::runtime_error("Cannot open file: " + path);

  Module m;
  std::unordered_map<int64_t, int64_t> sort_map;
  int64_t next_ir_id = 0;

  auto add_node = [&](Node n) -> int64_t {
    n.id = next_ir_id++;
    m.nodes.push_back(n);
    return n.id;
  };

  auto resolve_operand = [&](int64_t btor2_id) -> int64_t {
    if (btor2_id < 0) {
      int64_t abs_id = -btor2_id;
      auto it = m.btor2_to_ir.find(abs_id);
      if (it == m.btor2_to_ir.end())
        throw std::runtime_error("Unknown BTOR2 operand id: " +
                                 std::to_string(abs_id));
      int64_t inner_ir = it->second;
      Node not_node;
      not_node.kind = NodeKind::Not;
      not_node.width = m.nodes[inner_ir].width;
      not_node.operands.push_back(inner_ir);
      not_node.src_btor2_id = btor2_id;
      return add_node(not_node);
    }
    auto it = m.btor2_to_ir.find(btor2_id);
    if (it == m.btor2_to_ir.end())
      throw std::runtime_error("Unknown BTOR2 operand id: " +
                               std::to_string(btor2_id));
    return it->second;
  };

  auto get_width = [&](int64_t sid) -> int64_t {
    auto it = sort_map.find(sid);
    if (it == sort_map.end())
      throw std::runtime_error("Unknown sort id: " + std::to_string(sid));
    return it->second;
  };

  auto map_op = [](const std::string &op) -> NodeKind {
    if (op == "not")
      return NodeKind::Not;
    if (op == "and")
      return NodeKind::And;
    if (op == "or")
      return NodeKind::Or;
    if (op == "xor")
      return NodeKind::Xor;
    if (op == "xnor")
      return NodeKind::Xnor;
    if (op == "nand")
      return NodeKind::Nand;
    if (op == "nor")
      return NodeKind::Nor;
    if (op == "eq")
      return NodeKind::Eq;
    if (op == "neq")
      return NodeKind::Neq;
    if (op == "iff")
      return NodeKind::Iff;
    if (op == "implies")
      return NodeKind::Implies;
    if (op == "ite")
      return NodeKind::Ite;
    if (op == "concat")
      return NodeKind::Concat;
    if (op == "slice")
      return NodeKind::Slice;
    if (op == "uext")
      return NodeKind::Uext;
    if (op == "sext")
      return NodeKind::Sext;
    if (op == "add")
      return NodeKind::Add;
    if (op == "sub")
      return NodeKind::Sub;
    if (op == "mul")
      return NodeKind::Mul;
    if (op == "udiv")
      return NodeKind::Udiv;
    if (op == "sdiv")
      return NodeKind::Sdiv;
    if (op == "urem")
      return NodeKind::Urem;
    if (op == "srem")
      return NodeKind::Srem;
    if (op == "smod")
      return NodeKind::Smod;
    if (op == "neg")
      return NodeKind::Neg;
    if (op == "inc")
      return NodeKind::Inc;
    if (op == "dec")
      return NodeKind::Dec;
    if (op == "sll")
      return NodeKind::Sll;
    if (op == "srl")
      return NodeKind::Srl;
    if (op == "sra")
      return NodeKind::Sra;
    if (op == "rol")
      return NodeKind::Rol;
    if (op == "ror")
      return NodeKind::Ror;
    if (op == "redand")
      return NodeKind::RedAnd;
    if (op == "redor")
      return NodeKind::RedOr;
    if (op == "redxor")
      return NodeKind::RedXor;
    if (op == "ult")
      return NodeKind::Ult;
    if (op == "ulte")
      return NodeKind::Ulte;
    if (op == "ugt")
      return NodeKind::Ugt;
    if (op == "ugte")
      return NodeKind::Ugte;
    if (op == "slt")
      return NodeKind::Slt;
    if (op == "slte")
      return NodeKind::Slte;
    if (op == "sgt")
      return NodeKind::Sgt;
    if (op == "sgte")
      return NodeKind::Sgte;
    if (op == "uaddo" || op == "umulo" || op == "usubo" || op == "saddo" ||
        op == "ssubo" || op == "smulo" || op == "sdivo" || op == "nego")
      throw std::runtime_error("Overflow opcodes not supported: " + op);
    throw std::runtime_error("Unsupported BTOR2 opcode: " + op);
  };

  std::string line;

  while (std::getline(file, line)) {
    auto comment_pos = line.find(';');
    if (comment_pos != std::string::npos)
      line = line.substr(0, comment_pos);

    std::istringstream iss(line);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok)
      tokens.push_back(tok);

    if (tokens.empty())
      continue;

    if (tokens.size() < 2)
      throw std::runtime_error("Line too short: " + line);

    int64_t line_id = std::stoll(tokens[0]);
    const std::string &cmd = tokens[1];

    if (cmd == "sort") {
      if (tokens.size() < 4)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed sort");

      const std::string &sort_type = tokens[2];

      if (sort_type == "bitvec") {
        int64_t width = std::stoll(tokens[3]);
        sort_map[line_id] = width;
      } else if (sort_type == "array") {
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": array sort not supported");
      } else {
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": unknown sort type: " + sort_type);
      }
    } else if (cmd == "input") {
      if (tokens.size() < 3)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed input");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);

      std::string name;
      if (tokens.size() >= 4)
        name = tokens[3];

      Node n;
      n.kind = NodeKind::Input;
      n.width = width;
      n.name = name;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
      m.inputs.push_back(ir_id);
    } else if (cmd == "const") {
      if (tokens.size() < 4)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed const");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);
      std::string bits = pad_or_trunc(tokens[3], width);

      Node n;
      n.kind = NodeKind::Const;
      n.width = width;
      n.const_bits.bits = bits;
      n.const_bits.width = width;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    } else if (cmd == "constd") {
      if (tokens.size() < 4)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed constd");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);
      std::string bits = dec_to_bin_str(tokens[3], width);

      Node n;
      n.kind = NodeKind::Const;
      n.width = width;
      n.const_bits.bits = bits;
      n.const_bits.width = width;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    } else if (cmd == "consth") {
      if (tokens.size() < 4)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed consth");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);
      std::string bits = hex_to_bin_str(tokens[3], width);

      Node n;
      n.kind = NodeKind::Const;
      n.width = width;
      n.const_bits.bits = bits;
      n.const_bits.width = width;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    } else if (cmd == "zero") {
      if (tokens.size() < 3)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed zero");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);

      Node n;
      n.kind = NodeKind::Const;
      n.width = width;
      n.const_bits.bits = std::string(width, '0');
      n.const_bits.width = width;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    } else if (cmd == "one") {
      if (tokens.size() < 3)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed one");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);

      Node n;
      n.kind = NodeKind::Const;
      n.width = width;
      n.const_bits.bits = std::string(width - 1, '0') + "1";
      n.const_bits.width = width;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    } else if (cmd == "ones") {
      if (tokens.size() < 3)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed ones");

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);

      Node n;
      n.kind = NodeKind::Const;
      n.width = width;
      n.const_bits.bits = std::string(width, '1');
      n.const_bits.width = width;
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    } else if (cmd == "bad") {
      if (tokens.size() < 3)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed bad");

      int64_t operand_id = std::stoll(tokens[2]);
      int64_t operand_ir = resolve_operand(operand_id);

      Node n;
      n.kind = NodeKind::Bad;
      n.width = 1;
      n.operands.push_back(operand_ir);
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
      m.bads.push_back(ir_id);
    } else if (cmd == "constraint") {
      if (tokens.size() < 3)
        throw std::runtime_error("Line " + std::to_string(line_id) +
                                 ": malformed constraint");

      int64_t operand_id = std::stoll(tokens[2]);
      int64_t operand_ir = resolve_operand(operand_id);

      Node n;
      n.kind = NodeKind::Constraint;
      n.width = m.nodes[operand_ir].width;
      n.operands.push_back(operand_ir);
      n.src_btor2_id = line_id;

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
      m.constraints.push_back(ir_id);
    } else {
      NodeKind kind;
      try {
        kind = map_op(cmd);
      } catch (const std::runtime_error &e) {
        throw std::runtime_error("Line " + std::to_string(line_id) + ": " +
                                 e.what());
      }

      int64_t sort_id = std::stoll(tokens[2]);
      int64_t width = get_width(sort_id);

      Node n;
      n.kind = kind;
      n.width = width;
      n.src_btor2_id = line_id;

      if (kind == NodeKind::Not || kind == NodeKind::Neg ||
          kind == NodeKind::Inc || kind == NodeKind::Dec ||
          kind == NodeKind::RedAnd || kind == NodeKind::RedOr ||
          kind == NodeKind::RedXor) {
        if (tokens.size() < 4)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": malformed unary op");
        int64_t op_id = std::stoll(tokens[3]);
        n.operands.push_back(resolve_operand(op_id));
      } else if (kind == NodeKind::Slice) {
        if (tokens.size() < 6)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": malformed slice");
        int64_t op_id = std::stoll(tokens[3]);
        int64_t hi = std::stoll(tokens[4]);
        int64_t lo = std::stoll(tokens[5]);
        n.operands.push_back(resolve_operand(op_id));
        n.param0 = hi;
        n.param1 = lo;
        if (hi < lo)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": slice hi < lo");
      } else if (kind == NodeKind::Uext || kind == NodeKind::Sext) {
        if (tokens.size() < 5)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": malformed uext/sext");
        int64_t op_id = std::stoll(tokens[3]);
        int64_t ext_width = std::stoll(tokens[4]);
        n.operands.push_back(resolve_operand(op_id));
        n.param0 = ext_width;
      } else if (kind == NodeKind::Ite) {
        if (tokens.size() < 6)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": malformed ite");
        int64_t cond_id = std::stoll(tokens[3]);
        int64_t then_id = std::stoll(tokens[4]);
        int64_t else_id = std::stoll(tokens[5]);
        int64_t cond_ir = resolve_operand(cond_id);
        n.operands.push_back(cond_ir);
        n.operands.push_back(resolve_operand(then_id));
        n.operands.push_back(resolve_operand(else_id));
        if (m.nodes[cond_ir].width != 1)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": ite condition width != 1");
      } else if (kind == NodeKind::Eq || kind == NodeKind::Neq ||
                 kind == NodeKind::Iff || kind == NodeKind::Implies) {
        if (tokens.size() < 5)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": malformed eq/neq/iff/implies");
        n.operands.push_back(resolve_operand(std::stoll(tokens[3])));
        n.operands.push_back(resolve_operand(std::stoll(tokens[4])));
        if (width != 1)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": eq/neq result width != 1");
      } else {
        if (tokens.size() < 5)
          throw std::runtime_error("Line " + std::to_string(line_id) +
                                   ": malformed binary op");
        n.operands.push_back(resolve_operand(std::stoll(tokens[3])));
        n.operands.push_back(resolve_operand(std::stoll(tokens[4])));
      }

      int64_t ir_id = add_node(n);
      m.btor2_to_ir[line_id] = ir_id;
    }
  }

  if (d_verbose) {
    std::cerr << "BTOR2 Loader Summary:" << std::endl;
    std::cerr << "  Total nodes:    " << m.nodes.size() << std::endl;
    std::cerr << "  Inputs:         " << m.inputs.size() << std::endl;
    std::cerr << "  Constraints:    " << m.constraints.size() << std::endl;
    std::cerr << "  Bads:           " << m.bads.size() << std::endl;
  }

  return m;
}