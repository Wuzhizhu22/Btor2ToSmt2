#include "Btor2Loader.h"
#include "Smt2Emitter.h"

#include <iostream>

/**
 * @brief 程序主入口函数，协调BTOR2到SMT2的完整转换流程
 *
 * 该函数是Btor2ToSmt2工具的入口点，负责命令行参数解析并协调整个转换流程。
 * 程序遵循标准的"加载-转换-输出"模式：
 * 1. 命令行解析：处理-v/--verbose、-o输出文件、-h/--help以及输入文件路径
 * 2. BTOR2加载：调用Btor2Loader::load_from_file将文件解析为Module结构
 * 3. 转换执行：调用Smt2Emitter::emit_to_string或emit_to_file生成SMT2格式输出
 * 4. 调试输出：在verbose模式下输出模块的统计信息和所有节点的详细信息
 *
 * 支持的命令行选项：
 * - -o <file>: 指定SMT2输出文件路径
 * - -v, --verbose: 启用详细输出
 * - -h, --help: 显示帮助信息
 *
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 程序退出码，0表示成功，1表示错误
 */
int main(int argc, char **argv) {
  bool verbose = false;
  std::string input_file;
  std::string output_file;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-o") {
      if (i + 1 < argc) {
        output_file = argv[++i];
      } else {
        std::cerr << "Error: -o requires a filename argument" << std::endl;
        return 1;
      }
    } else if (arg == "-h" || arg == "--help") {
      std::cerr << "Usage: " << argv[0] << " [options] <input.btor2>"
                << std::endl;
      std::cerr << "Options:" << std::endl;
      std::cerr << "  -o <file>        Output SMT2 file path" << std::endl;
      std::cerr << "  -v, --verbose    Enable verbose output" << std::endl;
      std::cerr << "  -h, --help       Show this help message" << std::endl;
      return 0;
    } else if (input_file.empty()) {
      input_file = arg;
    }
  }

  if (input_file.empty()) {
    std::cerr << "Usage: " << argv[0] << " [options] <input.btor2>"
              << std::endl;
    std::cerr << "Use -h or --help for more information." << std::endl;
    return 1;
  }

  try {
    Btor2Loader loader;
    if (verbose)
      loader.set_verbose(true);
    Module m = loader.load_from_file(input_file);

    std::cerr << "=== BTOR2 Loaded Successfully ===" << std::endl;
    std::cerr << "Total nodes:    " << m.nodes.size() << std::endl;
    std::cerr << "Inputs:         " << m.inputs.size() << std::endl;
    std::cerr << "Constraints:    " << m.constraints.size() << std::endl;
    std::cerr << "Bads:           " << m.bads.size() << std::endl;

    if (!output_file.empty()) {
      Smt2Emitter emitter;
      emitter.emit_to_file(m, output_file);
      std::cerr << "SMT2 emitted to: " << output_file << std::endl;
    }

    if (verbose) {
      std::cerr << "\n=== Nodes ===" << std::endl;
      for (const auto &node : m.nodes) {
        std::cerr << "Node " << node.id << ": " << kind_to_string(node.kind)
                  << " [width=" << node.width << "]";

        if (!node.name.empty())
          std::cerr << " name=" << node.name;

        if (!node.operands.empty()) {
          std::cerr << " ops=[";
          for (size_t i = 0; i < node.operands.size(); i++) {
            if (i > 0)
              std::cerr << ", ";
            std::cerr << node.operands[i];
          }
          std::cerr << "]";
        }

        if (node.kind == NodeKind::Const)
          std::cerr << " value=" << node.const_bits.bits;

        if (node.kind == NodeKind::Slice)
          std::cerr << " hi=" << node.param0 << " lo=" << node.param1;

        if (node.kind == NodeKind::Uext || node.kind == NodeKind::Sext)
          std::cerr << " ext=" << node.param0;

        std::cerr << " (btor2_id=" << node.src_btor2_id << ")" << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}