#include "AST.h"
#include <iostream>
#include <memory>

using std::cout;
using uptr = std::unique_ptr;

enum struct BaseTypes { integer, float; };
enum struct Instructions { return; };

void CompUnitAST::dump() { _funcDef->dump(); }

void FuncDefAST::dump() {
  printf("fun")
}

class BlockAST : public BaseAST {
public:
  uptr<StmtAST> _stmt;
  void dump() const override;
};

class ExprAST : public BaseAST {
public:
  tuple<Instructions, int, int> > _info;
  void dump() const override;
};

class StmtAST : public BaseAST {
public:
  vector<uptr<ExprAST>> _stmts;
  void dump() const override;
};

class NumberAST : public BaseAST {
public:
  int value;
  void dump() const override;
}

class FuncTypeAST : public BaseAST {
public:
  BaseTypes _type;
  void dump() const override;
}