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

BaseAST *concat(const string &op, unique_ptr<BaseAST> l, unique_ptr<BaseAST> r)
{
  auto ast = new ExprAST();
  ast->_l = move(l);
  ast->_r = move(r);
  ast->_op = op;
  ast->_type = ExpTypes::Binary;
  return ast;
}

BaseTypes parse_type(const string &t)
{
  return BaseTypes::Integer;
}

const map<string, string> ExprAST::table_binary = {
    {"+", "add"}, {"-", "sub"}, {"*", "mul"}, {"/", "div"}, {"%", "mod"}, {">", "gt"}, {"<", "lt"}, {"<=", "le"}, {">=", "ge"}, {"==", "eq"}, {"!=", "ne"}};
const map<string, string> ExprAST::table_unary = {
    {"+", "add"}, {"-", "sub"}, {"!", "eq"}};