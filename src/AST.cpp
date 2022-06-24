#include "AST.hpp"
#include <fmt/core.h>
#include <iostream>
#include <algorithm>
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
  GetTableStack().push();
  string pre;
  for (auto &p : _params)
  {
    if (p->_type == BaseTypes::Integer)
      GetTableStack().insert(dynamic_cast<FuncDefParamAST &>(*p)._ident,
                             Symbol{SymbolTypes::FuncParamVar, BaseTypes::Integer});
    else
    {
      assert(p->_type == BaseTypes::Array);
      GetTableStack().insert(dynamic_cast<FuncDefParamAST &>(*p)._ident,
                             Symbol{SymbolTypes::FuncParamArrayVar, p->_info->getShapeArray()});
    }
  }
  res += DumpList(_params);
  res += format(")");
  if (_type != BaseTypes::Void)
    res += format(": {} ", _type);
  res += format("{{\n%entry:\n");

  GetTableStack().push();
  for (auto &p : _params)
  {
    auto ident = p->_ident;
    auto r = GetTableStack().query(ident);
    if (r->_type == SymbolTypes::FuncParamVar)
    {
      auto name = *GetTableStack().rename(ident);
      GetTableStack().insert(ident, Symbol{SymbolTypes::Var, BaseTypes::Integer});
      auto localName = *GetTableStack().rename(ident);

      res += format("\t@{} = alloc i32\n", localName);
      res += format("\tstore @{},@{}\n", name, localName);
    }
    else if (r->_type == SymbolTypes::FuncParamArrayVar)
    {
      auto name = *GetTableStack().rename(ident);
      GetTableStack().insert(ident, Symbol{SymbolTypes::ArrayPtr, r->_data});
      auto localName = *GetTableStack().rename(ident);

      res += format("\t@{} = alloc {}\n", localName, ArrayRefAST::get_shape(get<vector<int>>(r->_data)));
      res += format("\tstore @{},@{}\n", name, localName);
    }
  }
  GetTableStack().banPush();

  string blk = format("{}", *_block);
  res += blk;
  if (!_block->hasRetStmt())
  {
    if (_type != BaseTypes::Void)
      res += "\tret 0\n";
    else
      res += "\tret\n";
  }
  res += format("}}\n");
  GetTableStack().pop();
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

auto genSize = [](const vector<int> &v)
{
  return std::accumulate(
      v.begin(), v.end(), 1, [](int x, int y)
      { return x * y; });
};

vector<int> FormatInitTable(const ArrayRefAST &t, const ArrayInitListAST &p)
{
  vector<int> shape = t.getShapeArray();

  auto sz = genSize(shape);
  auto data = vector<int>(sz, 0);
  /*
  int ptr = 0;
  for (const auto &x : p._list)
  {
    auto &t = dynamic_cast<ExprAST &>(*x);
    data[ptr++] = t.eval();
  }
  */
  std::function<void(vector<int> shape, const vector<PBase> &v, int idx)> f;
  f = [&](vector<int> shape, const vector<PBase> &v, int idx)
  {
    int k = 0;
    for (const auto &p : v)
    {
      if (typeid(*p) == typeid(ArrayInitListAST))
      {
        int t = idx;
        auto ptr = shape.rbegin();
        while (ptr != shape.rend())
        {
          if (t % *ptr != 0)
            break;
          t /= *ptr++;
        }
        if (ptr == shape.rend())
          ptr--;
        assert(ptr != shape.rbegin());
        vector<int> s2(shape.rbegin(), ptr);
        f(s2, dynamic_cast<ArrayInitListAST &>(*p)._list, idx);
        k += genSize(s2);
      }
      else
      {
        data[idx + k] = dynamic_cast<ExprAST &>(*p).eval();
        ++k;
      }
      int sz = genSize(shape);
      for (int i = k; i < sz; ++i)
        data[idx + i] = 0;
    }
  };
  f(shape, p._list, 0);
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
  auto r = GetTableStack().query(_ref->_ident);
  string pre, ref;
  if (r->_type == SymbolTypes::FuncParamArrayVar)
  {

    auto name = *GetTableStack().rename(_ref->_ident);
    GetTableStack().insert(_ref->_ident, Symbol{SymbolTypes::ArrayPtr, r->_data});
    auto localName = *GetTableStack().rename(_ref->_ident);

    pre += format("\t@{} = alloc {}\n", localName, ArrayRefAST::get_shape(get<vector<int>>(r->_data)));
    pre += format("\tstore @{},@{}\n", name, localName);
    ref = format("@p{}", GetSlotAllocator().getSlot());
    pre += format("\t{} =  load @{}\n", ref, localName);
  }
  else
  {
    auto r = _ref->dump_ref();
    pre += r.first;
    ref = r.second;
  }
  vector<int> shape = get<vector<int>>(GetTableStack().query(_ref->_ident)->_data);
  int k = 0, _ptr;
  for (const auto &pos : _ref->_data)
  {
    _ptr = GetSlotAllocator().getSlot();
    auto prei = pos->dump_inst();
    string inst = shape[k++] == 0 ? "getptr" : "getelemptr";
    pre += format("{}\t@p{} = {} {}, {}\n", prei, _ptr, inst, ref, pos->dump());
    ref = format("@p{}", _ptr);
  }

  return make_pair(pre, format("@p{}", _ptr));
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

    if (_init)
    {
      res += format("\tstore zeroinit, @{}\n", name);
      auto data = FormatInitTable(*_arrayType, *_init.value());
      auto dumpAssign = [&](vector<int> pos, int x)
      {
        auto ref = new ArrayRefAST(new string(_arrayType->_ident));
        for (auto &p : pos)
          ref->_data.emplace_back(new NumberExprAST(new NumberAST(p)));
        auto stmt = new AssignAST(new LValArrayRefExprAST(ref), new NumberExprAST(new NumberAST(x)));
        return stmt->dump();
      };
      int cnt = 0;
      auto shape = _arrayType->getShapeArray();
      int n = genSize(shape);
      auto iToPos = [&](int i)
      {
        vector<int> r;
        for (auto p = shape.rbegin(); p < shape.rend(); ++p)
        {
          r.push_back(i % *p);
          i /= *p;
        }
        reverse(r.begin(), r.end());
        return r;
      };
      for (int i = 0; i < n; ++i)
      {
        auto val = data[cnt++];
        if (val)
          res += dumpAssign(iToPos(i), val);
      }
    }
    else
    {
      res += format("\tstore zeroinit, @{}\n", name);
    }
  }

  return res;
}

string ArrayRefAST::get_shape(vector<int> shape)
{
  reverse(shape.begin(), shape.end());
  string str = "i32";
  for (auto x : shape)
  {
    if (x)
    {
      str = format("[{},{}]", str, x);
    }
    else
    {
      str = format("*{}", str);
    }
  }
  return str;
}

string FuncDefParamAST::dump() const
{
  if (_type != BaseTypes::Array)
    return format("@{}:{}", *GetTableStack().rename(_ident), _type);
  else
    return format("@{}:{}", *GetTableStack().rename(_ident), _info->dump_shape());
}

pair<string, string> LValVarExprAST::dump_ref() const
{
  auto r = GetTableStack().query(_ident);
  assert(r.has_value());
  if (r->_type == SymbolTypes::Var)
    return make_pair("", format("@{}", *GetTableStack().rename(_ident)));
  else if (r->_type == SymbolTypes::GlobalVar)
    return make_pair("", format("@{}", *GetTableStack().rename(_ident)));
  else if (r->_type == SymbolTypes::Array)
    return make_pair("", format("@{}", *GetTableStack().rename(_ident)));
  else if (r->_type == SymbolTypes::GlobalArray)
    return make_pair("", format("@{}", *GetTableStack().rename(_ident)));
  else if (r->_type == SymbolTypes::ArrayPtr)
    return make_pair("", format("@{}", *GetTableStack().rename(_ident)));
  // For function parameter, we will only manimanipulate its local copy

  else
  {
    throw logic_error("try to get ref on wrong variable");
  }
}

string LValArrayRefExprAST::dump_inst() const
{
  _id = GetSlotAllocator().getSlot();
  auto res = dump_ref();
  auto shape = get<vector<int>>(GetTableStack().query(_ref->_ident)->_data);
  if (_ref->_data.size() < shape.size())
    return format("{}\t%{} = getelemptr {}, {}\n", res.first, _id, res.second, 0);
  else
    return format("{}\t%{} = load {}\n", res.first, _id, res.second);
}