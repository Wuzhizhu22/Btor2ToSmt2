#include "Btor2Loader.h"

#include <iostream>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <input.btor2>" << std::endl;
    return 1;
  }

  try {
    Btor2Loader loader;
    loader.set_verbose(true);
    Module m = loader.load_from_file(argv[1]);

    std::cout << "=== BTOR2 Loaded Successfully ===" << std::endl;
    std::cout << "Total nodes:    " << m.nodes.size() << std::endl;
    std::cout << "Inputs:         " << m.inputs.size() << std::endl;
    std::cout << "Constraints:    " << m.constraints.size() << std::endl;
    std::cout << "Bads:           " << m.bads.size() << std::endl;

    std::cout << "\n=== Nodes ===" << std::endl;
    for (const auto &node : m.nodes) {
      std::cout << "Node " << node.id << ": " << kind_to_string(node.kind)
                << " [width=" << node.width << "]";

      if (!node.name.empty())
        std::cout << " name=" << node.name;

      if (!node.operands.empty()) {
        std::cout << " ops=[";
        for (size_t i = 0; i < node.operands.size(); i++) {
          if (i > 0)
            std::cout << ", ";
          std::cout << node.operands[i];
        }
        std::cout << "]";
      }

      if (node.kind == NodeKind::Const)
        std::cout << " value=" << node.const_bits.bits;

      if (node.kind == NodeKind::Slice)
        std::cout << " hi=" << node.param0 << " lo=" << node.param1;

      if (node.kind == NodeKind::Uext || node.kind == NodeKind::Sext)
        std::cout << " ext=" << node.param0;

      std::cout << " (btor2_id=" << node.src_btor2_id << ")" << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}