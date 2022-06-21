#pragma once

#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>
#include <cassert>
#include <map>
#include <optional>
#include <type_traits>
#include <vector>
#include <variant>
#include "SymbolTable.hpp"

using fmt::format;
using fmt::formatter;
using std::endl;
using std::logic_error;
using std::make_pair;
using std::map;
using std::optional;
using std::pair;
using std::string;
using std::to_string;
using std::unique_ptr;
using std::vector;
class BaseAST;
typedef unique_ptr<BaseAST> PBase;

enum class BaseTypes
{
  Integer,
  FloatNumber,
  Void
};
enum class DeclTypes
{
  Const,
  Variable
};

class SlotAllocator
{
  int _slots;

public:
  void clear()
  {
    _slots = 0;
  }
  int getSlot()
  {
    return _slots++;
  }
};

template <typename Derived, typename Base>
unique_ptr<Derived> derived_cast(unique_ptr<Base> p)
{
  Derived *tmp = dynamic_cast<Derived *>(p.get());
  std::unique_ptr<Derived> derivedPointer;
  if (tmp != nullptr)
  {
    p.release();
    derivedPointer.reset(tmp);
  }
  return derivedPointer;
}
SlotAllocator &GetSlotAllocator();
BaseAST *WrapBlock(BaseAST *ast);
template <typename T>
string DumpList(const vector<T> &params)
{
  string res;
  bool head = true;
  for (const auto &p : params)
  {
    if (head)
      head = false;
    else
      res += ",";
    res += p->dump();
  }
  return res;
}
extern const string LibFuncDecl;

class BaseAST
{
public:
  virtual ~BaseAST() = default;
  virtual string dump() const = 0;
};

#ifdef YYDEBUG
#define Debugprint(x) fmt::print(x)
#else
#define Debugprint(x)
#endif

namespace fmt
{
  template <typename AST>
  struct formatter<AST,
                   std::enable_if_t<std::is_base_of<BaseAST, AST>::value, char>>
      : formatter<std::string>
  {
    template <typename FormatCtx>
    auto format(const AST &a, FormatCtx &ctx)
    {
      return formatter<std::string>::format(a.dump(), ctx);
    }
  };

  template <>
  struct formatter<BaseTypes> : formatter<std::string>
  {
    template <typename FormatCtx>
    auto format(const BaseTypes &a, FormatCtx &ctx)
    {
      string name;
      switch (a)
      {
      case BaseTypes::Integer:
        name = "i32";
        break;
      default:
        throw format_error("invalid type");
      }
      return formatter<std::string>::format(name, ctx);
    }
  };
}

class CompUnitAST : public BaseAST
{
public:
  vector<unique_ptr<BaseAST>> _list;
  CompUnitAST() {}
  string dump() const override
  {
    GetTableStack().push();
    RegisterLibFunc();
    string res = LibFuncDecl;
    for (auto &p : _list)
    {
      res += p->dump();
    }
    GetTableStack().pop();
    return res;
  }
};

class BlockAST;
class FuncDefParamAST : public BaseAST
{
public:
  BaseTypes _type;
  string _ident;
  string dump() const override
  {
    return format("@{}:{}", *GetTableStack().rename(_ident), _type);
  }
};

class FuncDefAST : public BaseAST
{
public:
  BaseTypes _type;
  std::string _ident;
  unique_ptr<BlockAST> _block;
  vector<unique_ptr<FuncDefParamAST>> _params;
  FuncDefAST(BaseTypes type, string *ident, vector<PBase> *params, BlockAST *blk) : _type(type),
                                                                                    _ident(*unique_ptr<string>(ident)),
                                                                                    _block(blk)
  {
    auto t = unique_ptr<vector<PBase>>(params);
    if (!t)
      return;
    for (auto &p : *t)
    {
      _params.push_back(derived_cast<FuncDefParamAST>(move(p)));
    }
  }
  BlockAST &block() const;
  string dump() const override;
};

class RetStmtAST;

class BlockAST : public BaseAST
{

public:
  vector<PBase> _list;
  string dump() const override;
  bool hasRetStmt() const;
};

class NumberAST;

class NumberAST : public BaseAST
{
public:
  int value;
  string dump() const override { return format("{}", value); }
};

class ExprAST : public BaseAST
{
protected:
  static const map<string, string> table_binary;
  static const map<string, string> table_unary;

public:
  mutable int _id = -1;
  virtual string dump() const override
  {
    assert(_id != -1);
    return format("%{}", _id);
  }
  virtual string dump_inst() const = 0;
  virtual int eval() const
  {
    throw logic_error("const expr is illegal");
    return 0;
  }
};

struct ConstExprAST : public ExprAST
{
  unique_ptr<NumberAST> _num;
  string dump() const override { return format("{}", *_num); }
  string dump_inst() const { return ""; }
  int eval() const override
  {
    return _num->value;
  }
};

struct LValExprAST : public ExprAST
{
  virtual std::pair<string, string> dump_ref() const = 0;
};

struct LValVarExprAST : public LValExprAST
{
  string _ident;
  LValVarExprAST(string *ident) : _ident(*unique_ptr<string>(ident)) {}
  string dump() const override
  {
    auto r = GetTableStack().query(_ident);
    assert(r.has_value());
    if (r->_type == SymbolTypes::Const)
    {
      return format("{}", std::get<int>(r->_data));
    }
    else if (r->_type == SymbolTypes::GlobalVar)
    {
      assert(_id != -1);
      return format("%{}", _id);
    }
    else
    {
      assert(_id != -1);
      return format("%{}", _id);
    }
  }
  pair<string, string> dump_ref() const override
  {
    auto r = GetTableStack().query(_ident);
    assert(r.has_value());
    if (r->_type == SymbolTypes::Var)
      return make_pair("", format("%{}", *GetTableStack().rename(_ident)));
    else if (r->_type == SymbolTypes::GlobalVar)
      return make_pair("", format("@{}", *GetTableStack().rename(_ident)));
    // For function parameter, we will only manimanipulate its local copy
    else if (r->_type == SymbolTypes::FuncParamVar)
    {
      auto name = *GetTableStack().rename(_ident);
      GetTableStack().insert(_ident, Symbol{SymbolTypes::Var, BaseTypes::Integer});
      auto localName = *GetTableStack().rename(_ident);

      auto pre = format("\t%{} = alloc i32\n", localName);
      pre += format("\tstore @{},%{}\n", name, localName);
      return make_pair(pre, "%" + localName);
    }
    else
    {
      throw logic_error("try to get ref on wrong variable");
    }
  }
  string dump_inst() const override
  {
    try
    {
      auto p = dump_ref();
      _id = GetSlotAllocator().getSlot();
      return format("{}\t%{} = load {}\n", p.first, _id, p.second);
    }
    catch (std::logic_error &e)
    {
      return "";
    }
  }
  int eval() const override
  {
    auto res = GetTableStack().query(_ident);
    assert(res.has_value());
    assert(res->_type == SymbolTypes::Const);
    return get<int>(res->_data);
  }
};

class ArrayRefAST;
struct LValArrayRefExprAST : public LValExprAST
{
  unique_ptr<ArrayRefAST> _ref;
  LValArrayRefExprAST(ArrayRefAST *ref) : _ref(ref) {}
  string dump() const override
  {
    return "";
  }
  string dump_inst() const override
  {
    return "";
  }
  pair<string, string> dump_ref() const override
  {
    return make_pair(string(), string());
  }
};

struct UnaryExprAST : public ExprAST
{
  string _op;
  unique_ptr<ExprAST> _child;
  string dump_inst() const override
  {
    _id = GetSlotAllocator().getSlot();
    auto inst = _child->dump_inst();
    return format("{}\t%{} = {} 0, {}\n", inst, _id, table_unary.at(_op), _child->dump());
  }
  int eval() const override
  {
    int result = _child->eval();
    if (_op == "-")
      result = -result;
    else if (_op == "!")
      result = !result;
    return result;
  }
};

struct BinaryExprAST : public ExprAST
{
  string _op;
  unique_ptr<ExprAST> _l, _r;
  string dump_inst() const override
  {
    string calc_l, calc_r, calc;
    calc_l = _l->dump_inst();
    calc_r = _r->dump_inst();
    _id = GetSlotAllocator().getSlot();
    if (_op == "||")
    {
      //  jump %entry
      //%entry:
      //  %t0 = alloc i32
      //  %t1 = ne l->dump(), 0
      //  store %t1, %t0
      //  br %t1, %then, %else
      //%then:
      //%else:
      //  %t3 = ne r->dump(), 0
      //  store %t3, %t0
      //%end:
      //  id = load %t0
      auto t0 = format("%{}", GetSlotAllocator().getSlot());
      auto t1 = format("%{}", GetSlotAllocator().getSlot());
      auto t2 = format("%{}", GetSlotAllocator().getSlot());
      auto tagEntry = format("%shortcut_entry_{}", GenID());
      auto tagThen = format("%shortcut_then_{}", GenID());
      auto tagElse = format("%shortcut_else_{}", GenID());
      auto tagEnd = format("%shortcut_end_{}", GenID());
      calc += format("\tjump {}\n", tagEntry);
      calc += format("{}:\n", tagEntry);
      calc += format("\t{} = alloc i32\n", t0);
      calc += format("{}", calc_l);
      calc += format("\t{} = ne {}, 0\n", t1, _l->dump());
      calc += format("\tbr {}, {}, {}\n", t1, tagThen, tagElse);
      calc += format("{}:\n", tagThen);
      calc += format("store {}, {}", t1, t0);
      calc += format("\tjump {}\n", tagEnd);
      calc += format("{}:\n", tagElse);
      calc += format("{}", calc_r);
      calc += format("\t{} = ne {}, 0\n", t2, _r->dump());
      calc += format("\tstore {}, {}\n", t2, t0);
      calc += format("\tjump {}\n", tagEnd);
      calc += format("{}:\n", tagEnd);
      calc += format("\t%{} = load {}", _id, t0);
      return calc;
    }
    else if (_op == "&&")
    {
      //  jump %entry
      //%entry:
      //  %t0 = alloc i32
      //  %t1 = ne l->dump(), 0
      //  store %t1, %t0
      //  br %t1, %then, %else
      //%then:
      //%else:
      //  %t3 = ne r->dump(), 0
      //  store %t3, %t0
      //%end:
      //  id = load %t0
      auto t0 = format("%{}", GetSlotAllocator().getSlot());
      auto t1 = format("%{}", GetSlotAllocator().getSlot());
      auto t2 = format("%{}", GetSlotAllocator().getSlot());
      auto tagEntry = format("%shortcut_entry_{}", GenID());
      auto tagThen = format("%shortcut_then_{}", GenID());
      auto tagElse = format("%shortcut_else_{}", GenID());
      auto tagEnd = format("%shortcut_end_{}", GenID());
      calc += format("\tjump {}\n", tagEntry);
      calc += format("{}:\n", tagEntry);
      calc += format("\t{} = alloc i32\n", t0);
      calc += format("{}", calc_l);
      calc += format("\t{} = ne {}, 0\n", t1, _l->dump());
      calc += format("\tbr {}, {}, {}\n", t1, tagThen, tagElse);
      calc += format("{}:\n", tagThen);
      calc += format("{}", calc_r);
      calc += format("\t{} = ne {}, 0\n", t2, _r->dump());
      calc += format("\tstore {}, {}\n", t2, t0);
      calc += format("\tjump {}\n", tagEnd);
      calc += format("{}:\n", tagElse);
      calc += format("store {}, {}", t1, t0);
      calc += format("\tjump {}\n", tagEnd);
      calc += format("{}:\n", tagEnd);
      calc += format("\t%{} = load {}", _id, t0);
      return calc;
    }
    else
    {
      calc = format("\t%{} = {} {}, {}\n", _id, table_binary.at(_op), _l->dump(), _r->dump());
    }

    return format("{}{}{}", calc_l, calc_r, calc);
  }
  /*
  string dump_inst() const override
  {
    string calc_l, calc_r, calc;
    calc_l = _l->dump_inst();
    calc_r = _r->dump_inst();
    _id = GetSlotAllocator().getSlot();
    if (_op == "||")
    {
      auto tid = GetSlotAllocator().getSlot();
      calc = format("\t%{} = or {}, {}\n", tid, _l->dump(), _r->dump());
      calc += format("\t%{} = ne %{}, 0\n", _id, tid);
    }
    else if (_op == "&&")
    {
      auto tid1 = GetSlotAllocator().getSlot();
      auto tid2 = GetSlotAllocator().getSlot();
      calc += format("\t%{} = ne {}, 0\n", tid1, _l->dump());
      calc += format("\t%{} = ne {}, 0\n", tid2, _r->dump());
      calc += format("\t%{} = and %{}, %{}\n", _id, tid1, tid2);
    }
    else
    {
      calc = format("\t%{} = {} {}, {}\n", _id, table_binary.at(_op), _l->dump(), _r->dump());
    }

    return format("{}{}{}", calc_l, calc_r, calc);
  }
  */
  int eval() const override
  {
    int result = 0;
    ExprAST &l = dynamic_cast<ExprAST &>(*_l);
    ExprAST &r = dynamic_cast<ExprAST &>(*_r);
    int wl = l.eval();
    int wr = r.eval();
    if (_op == "+")
      result = wl + wr;
    else if (_op == "-")
      result = wl - wr;
    else if (_op == "*")
      result = wl * wr;
    else if (_op == "/")
      result = wl / wr;
    else if (_op == "%")
      result = wl % wr;
    else if (_op == ">")
      result = wl > wr;
    else if (_op == "<")
      result = wl < wr;
    else if (_op == ">=")
      result = wl >= wr;
    else if (_op == "<=")
      result = wl <= wr;
    else if (_op == "==")
      result = wl == wr;
    else if (_op == "!=")
      result = wl != wr;
    else if (_op == "&&")
      result = wl && wr;
    else if (_op == "||")
      result = wl || wr;
    else
      throw logic_error(format("unknown op {}", _op));
    return result;
  }
};

struct FuncCallExprAST : public ExprAST
{
  vector<unique_ptr<ExprAST>> _params;
  string _ident;
  FuncCallExprAST(string *ident, vector<unique_ptr<BaseAST>> *params = nullptr)
      : _ident(*unique_ptr<string>(ident))
  {
    if (params)
      for (auto &p : *params)
      {
        _params.push_back(derived_cast<ExprAST>(move(p)));
      }
  }
  string dump() const override
  {
    auto type = get<BaseTypes>(GetTableStack().query(_ident)->_data);
    if (type == BaseTypes::Integer)
    {
      assert(_id != -1);
      return format("%{}", _id);
    }
    return "";
  }
  string dump_inst() const override
  {
    string res;
    for (auto &p : _params)
      res += p->dump_inst();
    auto type = get<BaseTypes>(GetTableStack().query(_ident)->_data);
    if (type == BaseTypes::Integer)
    {
      _id = GetSlotAllocator().getSlot();
      res += format("\t%{} = ", _id);
    }
    res += format("\tcall @{}(", _ident);
    res += DumpList(_params);
    res += ")\n";
    return res;
  }
};

class RetStmtAST : public BaseAST
{
public:
  unique_ptr<ExprAST> _expr;
  RetStmtAST(ExprAST *expr = nullptr) : _expr(expr) {}
  string dump() const override
  {
    if (!_expr)
      return "\tret\n";
    auto inst = _expr->dump_inst();
    return format("{}\tret {}\n", inst, _expr->dump());
  }
};

class NullStmtAST : public BaseAST
{
public:
  string dump() const override
  {
    return "";
  }
};

class ExpStmtAST : public BaseAST
{
public:
  PBase _exp;
  string dump() const override
  {
    return dynamic_cast<ExprAST &>(*_exp).dump_inst();
  }
};

class TypeAST : public BaseAST
{
public:
  BaseTypes _type;
  string dump() const override { return format("{}", _type); }
};

ExprAST *concat(const string &op, ExprAST *l, ExprAST *r);
BaseTypes parse_type(const string &t);

class DeclAST : public BaseAST
{
public:
  DeclTypes _type;
  BaseTypes _bType;
  unique_ptr<vector<PBase>> _vars;
  DeclAST(DeclTypes type, BaseTypes bType, unique_ptr<vector<PBase>> vars)
      : _type(type), _bType(bType), _vars(move(vars))
  {
  }
  string dump() const override
  {
    string res;
    for (const auto &p : *_vars)
    {
      res += p->dump();
    }
    return res;
  }
};

class DefAST : public BaseAST
{
public:
  DeclTypes _type;
  string _ident;
  optional<unique_ptr<ExprAST>> _init;
  BaseTypes _bType;
  DefAST(DeclTypes type, const string &i) : _type(type), _ident(i) {}
  DefAST(DeclTypes type, const string &i, unique_ptr<ExprAST> p) : _type(type), _ident(i), _init(move(p)) {}
  string dump() const override
  {
    string calc;
    if (_type == DeclTypes::Const)
    {
      GetTableStack().insert(_ident, Symbol{SymbolTypes::Const, (*_init)->eval()});
    }
    else
    {
      auto type = GetTableStack().isGlobal() ? SymbolTypes::GlobalVar : SymbolTypes::Var;

      GetTableStack().insert(_ident, Symbol{type, BaseTypes::Integer});

      auto r = *GetTableStack().rename(_ident);
      if (type == SymbolTypes::GlobalVar)
      {
        auto init = _init.has_value() ? format("{}", (*_init)->eval()) : "zeroinit";
        calc += format("global @{} = alloc i32, {}\n", r, init);
      }
      else
      {
        calc += format("\t%{} = alloc i32\n", r);
        if (_init.has_value())
        {
          auto &p = dynamic_cast<ExprAST &>(*_init.value());
          calc += p.dump_inst();
          calc += format("\tstore {}, %{}\n", p.dump(), r);
        }
      }
    }
    return calc;
  }
};

class AssignAST : public BaseAST
{
public:
  unique_ptr<LValExprAST> _l;
  unique_ptr<ExprAST> _r;
  AssignAST(LValExprAST *l, ExprAST *r) : _l(l), _r(r) {}
  string dump() const override
  {
    string code;
    code += _r->dump_inst();
    auto ref = _l->dump_ref();
    code += format("\t{}store {}, {}\n", ref.first, _r->dump(), ref.second);
    return code;
  }
};

class IFStmtAST : public BaseAST
{

public:
  PBase _expr, _if, _else;
  IFStmtAST(BaseAST *expr, BaseAST *if_st, BaseAST *else_st) : _expr(expr), _if(WrapBlock(if_st)), _else(WrapBlock(else_st))
  {
  }
  const ExprAST &expr() const { return dynamic_cast<const ExprAST &>(*_expr); }
  string dump() const override
  {
    int labelIf = GenID(),
        labelEnd = GenID(),
        labelElse = GenID();
    if (_if)
      assert(typeid(*_if) == typeid(BlockAST));
    if (_else)
      assert(typeid(*_else) == typeid(BlockAST));

    string res;
    res += expr().dump_inst();
    res += format("\tbr {}, %then_{}, %else_{}\n", expr().dump(), labelIf, labelElse);
    res += format("%then_{}:\n{}", labelIf, _if->dump());
    if (!(_if && dynamic_cast<BlockAST &>(*_if).hasRetStmt()))
      res += format("\tjump %end_{}\n", labelEnd);
    res += format("%else_{}:\n{}", labelElse, _else ? _else->dump() : "", labelEnd);
    if (!(_else && dynamic_cast<BlockAST &>(*_else).hasRetStmt()))
      res += format("\tjump %end_{}\n", labelEnd);
    res += format("%end_{}:\n", labelEnd);
    return res;
  }
};

class WhileStmtAST : public BaseAST
{
public:
  PBase _expr, _body;
  WhileStmtAST(BaseAST *expr, BaseAST *body) : _expr(expr), _body(WrapBlock(body)) {}
  const ExprAST &expr() const { return dynamic_cast<const ExprAST &>(*_expr); }
  string dump() const override
  {
    assert(typeid(*_body) == typeid(BlockAST));

    string tagBody = format("while_body_{}", GenID());
    string tagEntry = format("while_entry_{}", GenID());
    string tagEnd = format("while_end_{}", GenID());
    string res;
    res += format("\tjump %{}\n", tagEntry);
    res += format("%{}:\n", tagEntry);
    res += expr().dump_inst();
    res += format("\tbr {}, %{}, %{}\n", expr().dump(), tagBody, tagEnd);
    GetTableStack().push();
    GetTableStack().insert("while_entry", Symbol{SymbolTypes::Str, tagEntry});
    GetTableStack().insert("while_end", Symbol{SymbolTypes::Str, tagEnd});
    GetTableStack().insert("while_body", Symbol{SymbolTypes::Str, tagBody});
    GetTableStack().banPush();
    res += format("%{}:\n{}", tagBody, _body->dump());
    if (!(_body && dynamic_cast<BlockAST &>(*_body).hasRetStmt()))
    {
      res += format("\tjump %{}\n", tagEntry);
    }
    res += format("%{}:\n", tagEnd);
    return res;
  }
};

class BreakStmt : public BaseAST
{
  string dump() const override
  {
    string inst;
    inst += format("\tjump %{}\n", std::get<string>(GetTableStack().query("while_end")->_data));
    inst += format("%while_body_{}:\n", GenID());
    return inst;
  }
};

class ContinueStmt : public BaseAST
{
  string dump() const override
  {
    string inst;
    inst += format("\tjump %{}\n", std::get<string>(GetTableStack().query("while_entry")->_data));
    inst += format("%while_body_{}:\n", GenID());
    return inst;
  }
};

class ArrayInitListAST;
class ArrayRefAST;

struct ArrayDefAST : public BaseAST
{
  DeclTypes _type;
  unique_ptr<ArrayRefAST> _arrayType;
  optional<unique_ptr<ArrayInitListAST>> _init;
  ArrayDefAST(DeclTypes type, ArrayRefAST *arrayType, ArrayInitListAST *init = nullptr)
      : _type(type), _arrayType(arrayType)
  {
    if (init)
    {
      _init = unique_ptr<ArrayInitListAST>(init);
    }
  }
  string dump() const override
  {
    return "";
  }
};

struct ArrayInitListAST : public BaseAST
{
  vector<unique_ptr<BaseAST>> _list;
  ArrayInitListAST(vector<unique_ptr<BaseAST>> *list = nullptr)
  {
    if (list)
    {
      _list = move(*unique_ptr<vector<unique_ptr<BaseAST>>>(list));
    }
  }
  string dump() const override
  {
    return "";
  }
};

struct ArrayRefAST : public BaseAST
{
  vector<unique_ptr<ExprAST>> _shape;
  string _ident;

  ArrayRefAST(string *ident) : _ident(*unique_ptr<string>(ident)) {}

  string dump() const override
  {
    return "";
  }
};
