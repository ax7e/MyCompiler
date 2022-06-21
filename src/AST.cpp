#include "AST.hpp"
#include <fmt/core.h>
#include <iostream>

using std::cout;
using std::unique_ptr;

SlotAllocator &GetSlotAllocator()
{
  static SlotAllocator alloc;
  return alloc;
}

ExprAST *concat(const string &op, ExprAST *l, ExprAST *r)
{
  auto ast = new BinaryExprAST();
  ast->_l = unique_ptr<ExprAST>(l);
  ast->_r = unique_ptr<ExprAST>(r);
  ast->_op = op;
  return ast;
}

BaseTypes parse_type(const string &t)
{
  if (t == "int")
    return BaseTypes::Integer;
  else
    return BaseTypes::Void;
}

const map<string, string> ExprAST::table_binary = {
    {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"}, {">", "gt"}, {"<", "lt"}, {"<=", "le"}, {">=", "ge"}, {"==", "eq"}, {"!=", "ne"}};
const map<string, string> ExprAST::table_unary = {
    {"+", "add"}, {"-", "sub"}, {"!", "eq"}};

string BlockAST::dump() const
{

  GetTableStack().push();
  string res;
  for (const auto &p : _list)
  {
    res += p->dump();
    // Ignore all the statements after return
    if (typeid(*p) == typeid(RetStmtAST))
    {
      break;
    }
  }
  GetTableStack().pop();
  return res;
}

bool BlockAST::hasRetStmt() const
{
  for (const auto &p : _list)
  {
    if (typeid(*p) == typeid(BlockAST))
    {
      if (dynamic_cast<BlockAST &>(*p).hasRetStmt())
        return true;
    }
    if (typeid(*p) == typeid(RetStmtAST))
    {
      return true;
    }
  }
  return false;
}

BaseAST *WrapBlock(BaseAST *ast)
{
  if (!ast)
    return ast;
  if (typeid(*ast) == typeid(BlockAST))
    return ast;
  auto blk = new BlockAST();
  blk->_list.push_back(PBase(ast));
  return blk;
}

string FuncDefAST::dump() const
{
  assert(typeid(*_block) == typeid(BlockAST));
  GetTableStack().insert(_ident, Symbol{SymbolTypes::Func, _type});
  GetSlotAllocator().clear();
  string res = format("fun @{}(", _ident);
  for (auto &p : _params)
  {
    GetTableStack().insert(dynamic_cast<FuncDefParamAST &>(*p)._ident, Symbol{SymbolTypes::FuncParamVar, BaseTypes::Integer});
  }
  res += DumpList(_params);
  res += format(")");
  if (_type != BaseTypes::Void)
    res += format(": {} ", _type);
  res += format("{{\n%entry:\n");
  string blk = format("{}", *_block);
  res += blk;
  if (!_block->hasRetStmt())
    res += "\tret\n";
  res += format("}}\n");
  return res;
}

BlockAST &FuncDefAST::block() const
{
  return dynamic_cast<BlockAST &>(*_block);
}

const string LibFuncDecl =
    "decl @getint(): i32\n"
    "decl @getch(): i32\n"
    "decl @getarray(*i32): i32\n"
    "decl @putint(i32)\n"
    "decl @putch(i32)\n"
    "decl @putarray(i32, *i32)\n"
    "decl @starttime()\n"
    "decl @stoptime()\n\n";
