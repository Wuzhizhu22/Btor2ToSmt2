#ifndef BTOR2TOSMT2_SMT2EMITTER_H_INCLUDED
#define BTOR2TOSMT2_SMT2EMITTER_H_INCLUDED

#include "IR.h"

#include <iosfwd>
#include <string>

class Smt2Emitter {
public:
  void emit_to_file(const Module &m, const std::string &path);
  void emit_to_stream(const Module &m, std::ostream &out);
  std::string emit_to_string(const Module &m);
};

#endif