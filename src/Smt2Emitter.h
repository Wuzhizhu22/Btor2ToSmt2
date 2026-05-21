#ifndef BTOR2TOSMT2_SMT2EMITTER_H_INCLUDED
#define BTOR2TOSMT2_SMT2EMITTER_H_INCLUDED

#include "IR.h"

#include <iosfwd>
#include <string>
#include <unordered_map>

struct Smt2EmitterOptions {
  bool strict_smtlib = true;
};

using NameMap = std::unordered_map<int64_t, std::string>;

class Smt2Emitter {
public:
  explicit Smt2Emitter(Smt2EmitterOptions options = {});
  void emit_to_file(const Module &m, const std::string &path);
  void emit_to_stream(const Module &m, std::ostream &out);
  std::string emit_to_string(const Module &m);

private:
  void emit_rhs(std::ostream &out, const Module &m, const Node &n,
                const NameMap &names);
  void emit_node_definition(std::ostream &out, const Module &m, const Node &n,
                            const NameMap &names);
  void emit_asserts(std::ostream &out, const Module &m, const NameMap &names);

  Smt2EmitterOptions d_options;
};

#endif