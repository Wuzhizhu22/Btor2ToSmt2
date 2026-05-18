#ifndef BTOR2TOSMT2_BTOR2LOADER_H_INCLUDED
#define BTOR2TOSMT2_BTOR2LOADER_H_INCLUDED

#include "IR.h"

#include <string>

class Btor2Loader {
public:
  Module load_from_file(const std::string &path);
  void set_verbose(bool v) { d_verbose = v; }

private:
  bool d_verbose = false;
};

#endif