#pragma once

#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>
#include <cassert>
#include <map>

using fmt::format;
using fmt::formatter;
using std::endl;
using std::map;
using std::string;
using std::unique_ptr;

enum class BaseTypes
{
  Integer,
  FloatNumber
};
enum class ExpTypes
{
  Unary,
  Binary,
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
  unique_ptr<BaseAST> _stmt;
  string dump() const override
  {
    GetSlotAllocator().clear();
    return format("%entry:\n{}", *_stmt);
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
