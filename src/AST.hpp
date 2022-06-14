#pragma once

#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>
#include <cassert>

using fmt::format;
using fmt::formatter;
using std::endl;
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
    return format("%entry:\n\t{}", *_stmt);
  }
};

class NumberAST;

class ExprAST : public BaseAST
{
public:
  ExpTypes _type;
  string _op;
  mutable int _id = -1;
  unique_ptr<BaseAST> _l, _r;
  string dump() const override
  {
    assert(_id != -1);
    return format("%{}", _id);
  }
  string dump_inst() const
  {
    assert(_type == ExpTypes::Unary || _type == ExpTypes::Const);
    string calc_l, calc_r, calc;
    if (_type == ExpTypes::Unary)
    {
      assert(typeid(*_l) == typeid(ExprAST));
      ExprAST &l = dynamic_cast<ExprAST &>(*_l);
      if (_op == "+")
      {
        _id = l._id;
      }
      else
      {
        _id = GetSlotAllocator().getSlot();
        calc_l = l.dump_inst();
        if (_op == "!")
        {
          calc = format("%{} = eq {}, 0\n", _id, _l->dump());
        }
        if (_op == "-")
        {
          calc = format("%{} = sub 0, {}\n", _id, _l->dump());
        }
      }
    }
    else if (_type == ExpTypes::Const)
    {
      _id = GetSlotAllocator().getSlot();
      calc = format("%{} = {}\n", _id, _l->dump());
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
    return format("{}\nret %{}\n", expr().dump_inst(), _expr->dump());
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

BaseAST *concat(const char *op, unique_ptr<BaseAST> l, unique_ptr<BaseAST> r);
