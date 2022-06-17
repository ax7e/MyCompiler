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
#include <vector>
#include "SymbolTable.hpp"

using fmt::format;
using fmt::formatter;
using std::endl;
using std::map;
using std::optional;
using std::string;
using std::unique_ptr;
using std::vector;
class BaseAST;
typedef unique_ptr<BaseAST> PBase;

enum class BaseTypes
{
  Integer,
  FloatNumber
};
enum class DeclTypes
{
  Const,
  Variable
};
enum class ExpTypes
{
  Unary,
  Binary,
  Const,
  LVal
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

SlotAllocator &GetSlotAllocator();

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
  unique_ptr<BaseAST> _funcDef;
  string dump() const override { return format("{}", *_funcDef); }
};

class FuncDefAST : public BaseAST
{
public:
  unique_ptr<BaseAST> _funcType;
  std::string _ident;
  unique_ptr<BaseAST> _block;
  string dump() const override
  {
    return format("fun @{}(): {} {{\n{}}}", _ident, *_funcType, *_block);
  }
};

class StmtAST;

class BlockAST : public BaseAST
{
public:
  vector<PBase> _list;
  string dump() const override
  {
    GetSlotAllocator().clear();
    return ("");
    // return format("%entry:\n{}", *_stmt);
  }
};

class NumberAST;

class ExprAST : public BaseAST
{
  static const map<string, string> table_binary;
  static const map<string, string> table_unary;

public:
  ExpTypes _type;
  string _op;
  mutable int _id = -1;
  unique_ptr<BaseAST> _l, _r;
  optional<string> _ident;
  string dump() const override
  {
    if (_type == ExpTypes::Const)
    {
      return format("{}", *_l);
    }
    assert(_id != -1);
    return format("%{}", _id);
  }
  string dump_inst() const
  {
    string calc_l, calc_r, calc;
    if (_type == ExpTypes::Unary)
    {
      assert(typeid(*_l) == typeid(ExprAST));
      ExprAST &l = dynamic_cast<ExprAST &>(*_l);
      calc_l = l.dump_inst();
      _id = GetSlotAllocator().getSlot();
      calc = format("\t%{} = {} 0, {}\n", _id, table_unary.at(_op), _l->dump());
    }
    else if (_type == ExpTypes::Const)
    {
      calc = "";
    }
    else if (_type == ExpTypes::Binary)
    {
      assert(typeid(*_l) == typeid(ExprAST));
      assert(typeid(*_r) == typeid(ExprAST));
      ExprAST &l = dynamic_cast<ExprAST &>(*_l);
      ExprAST &r = dynamic_cast<ExprAST &>(*_r);
      calc_l = l.dump_inst();
      calc_r = r.dump_inst();
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
    }
    return format("{}{}{}", calc_l, calc_r, calc);
  }
  int eval() const
  {
    if (_type == ExpTypes::LVal)
    {
      auto res = GetSymbolStack().query(_ident);
      assert(res.has_value());
      return res.value();
    }
    if (_type == ExpTypes::Binary)
    {
      int result;
      ExprAST &l = dynamic_cast<ExprAST &>(*_l);
      ExprAST &r = dynamic_cast<ExprAST &>(*_r);
      int wl = l.eval();
      int wr = r.eval();
      if (_op == "+")
      {
        result = wl + wr;
      }
      else if (_op == "-")
      {
        result = wl - wr;
      }
      else if (_op == "*")
      {
        result = wl * wr;
      }
      else if (_op == "/")
      {
        result = wl / wr;
      }

      else if (_op == "%")
      {
        result = wl % wr;
      }

      else if (_op == ">")
      {
        result = wl > wr;
      }

      else if (_op == "<")
      {
        result = wl < wr;
      }

      else if (_op == ">=")
      {
        result = wl >= wr;
      }

      else if (_op == "<=")
      {
        result = wl <= wr;
      }
      else if (_op == "==")
      {
        result = wl == wr;
      }
      else if (_op == "!=")
      {
        result = wl != wr;
      }
      else if (_op == "&&")
      {
        result = wl && wr;
      }
      else if (_op == "||")
      {
        result = wl || wr;
      }
      else
      {
        throw logic_error(format("unknown op {}", _op));
      }
      return result;
    }
    else if (_type == ExpTypes::Unary)
    {
      int result = l.eval();
      ExprAST &l = dynamic_cast<ExprAST &>(*_l);
      if (_op == "-")
      {
        result = -result;
      }
      else if (_op == "!")
      {
        result = !result;
      }
      return result;
    }
    else if (_type == ExpTypes::Const)
    {
      NumberAST &l = dynamic_cast<NumberAST &>(*l);
      return l->value;
    }
  }
};

class StmtAST : public BaseAST
{
public:
  unique_ptr<BaseAST> _expr;
  const ExprAST &expr() const { return dynamic_cast<const ExprAST &>(*_expr); }
  string dump() const override
  {
    assert(typeid(*_expr) == typeid(ExprAST));
    auto inst = expr().dump_inst();
    auto id = expr().dump();
    return format("{}\tret {}\n", inst, id);
  }
};

class NumberAST : public BaseAST
{
public:
  int value;
  string dump() const override { return format("{}", value); }
};

class FuncTypeAST : public BaseAST
{
public:
  BaseTypes _type;
  string dump() const override { return format("{}", _type); }
};

BaseAST *concat(const string &op, unique_ptr<BaseAST> l, unique_ptr<BaseAST> r);
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
  string dump() const override { return ""; }
};

class DefAST : public BaseAST
{
public:
  string _ident;
  optional<PBase> _init;
  DefAST(const string &i) : _ident(i) {}
  DefAST(const string &i, PBase p) : _ident(i), _init(move(p)) {}
  string dump() const override { return ""; }
};
