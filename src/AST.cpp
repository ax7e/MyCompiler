#include "AST.hpp"
#include <fmt/core.h>
#include <iostream>
#include <functional>

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

vector<int> FormatInitTable(const ArrayRefAST &t, const ArrayInitListAST &p)
{
  vector<int> shape = t.getShapeArray();
  int x = 1;
  for (auto u : shape)
    x *= u;
  auto data = vector<int>(x, 0);
  int ptr = 0;
  for (const auto &x : p._list)
  {
    auto &t = dynamic_cast<ExprAST &>(*x);
    data[ptr++] = t.eval();
  }
  return data;
}

string FormatInitListToString(const ArrayRefAST &t, const ArrayInitListAST &p)
{
  vector<int> data = FormatInitTable(t, p);
  vector<int> shape = t.getShapeArray();
  vector<int> offset = shape;
  offset[shape.size() - 1] = 1;
  for (int i = offset.size() - 2; i >= 0; --i)
    offset[i] = offset[i + 1] * shape[i + 1];

  std::function<string(int, int)> dump;
  string out;

  dump = [&](size_t k, int idx)
  {
    if (k == shape.size())
      return format("{}", data[idx]);
    string res;
    res += "{";
    for (int i = 0; i < shape[k]; ++i)
    {
      if (i > 0)
        res += ",";
      res += dump(k + 1, idx + i * offset[k]);
    }
    res += "}";
    return res;
  };
  return dump(0, 0);
}

pair<string, string> LValArrayRefExprAST::dump_ref() const
{
  int pos = _ref->getShapeArray()[0];
  _ptr = GetSlotAllocator().getSlot();
  auto pre = format("\t%{} = getelemptr {}, {}\n", _ptr, _ref->dump(), pos);
  return make_pair(pre, format("%{}", _ptr));
}

ArrayDefAST::ArrayDefAST(DeclTypes type, ArrayRefAST *arrayType, ArrayInitListAST *init)
    : _type(type), _arrayType(arrayType)
{
  if (init)
  {
    _init = unique_ptr<ArrayInitListAST>(init);
  }
}
string ArrayDefAST::dump() const
{
  string res;
  int isGlobal = GetTableStack().isGlobal();
  if (isGlobal)
  {
    GetTableStack().insert(_arrayType->_ident, Symbol{SymbolTypes::GlobalArray, _arrayType->getShapeArray()});
    string name = *GetTableStack().rename(_arrayType->_ident);
    res = format("global @{} = alloc {}, {}\n",
                 name, _arrayType->dump_shape(), _init ? FormatInitListToString(*_arrayType, **_init) : "zeroinit");
  }
  else
  {
    GetTableStack().insert(_arrayType->_ident, Symbol{SymbolTypes::Array, _arrayType->getShapeArray()});
    string name = *GetTableStack().rename(_arrayType->_ident);
    res = format("\t@{} = alloc {}\n", name, _arrayType->dump_shape());

    // TODO:multidimension
    if (_init)
    {
      res += format("\tstore zeroinit, @{}\n", name);
      int n = _arrayType->getShapeArray()[0];
      auto data = FormatInitTable(*_arrayType, *_init.value());
      assert((int)data.size() == n);
      auto dumpAssign = [&](vector<int> pos, int x)
      {
        auto ref = new ArrayRefAST(new string(_arrayType->_ident));
        for (auto &p : pos)
          ref->_data.emplace_back(new ConstExprAST(new NumberAST(p)));
        auto stmt = new AssignAST(new LValArrayRefExprAST(ref), new ConstExprAST(new NumberAST(x)));
        return stmt->dump();
      };
      int cnt = 0;
      for (int i = 0; i < n; ++i)
      {
        auto val = data[cnt++];
        if (val)
          res += dumpAssign(vector<int>{i}, val);
      }
    }
    else
    {
      res += format("\tstore zeroinit, @{}\n", name);
    }
  }

  return res;
}
