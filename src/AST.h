#pragma once

using uptr = std::unique_ptr;

enum struct BaseTypes { Integer, Float; };
enum struct Instructions { Return; };

class BaseAST {
public:
  virtual ~BaseAST() = default;
  virtual void dump() const = 0;
};

class CompUnitAST : public BaseAST {
public:
  uptr<BaseAST> _funcDef;
  void dump() const override;
};

class FuncDefAST : public BaseAST {
public:
  uptr<BaseAST> _funcType;
  std::string _ident;
  uptr<BaseAST> _block;
  void dump() const override;
};

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