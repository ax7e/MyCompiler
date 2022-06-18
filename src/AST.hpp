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
    GetSlotAllocator().clear();
    string res = format("fun @{}(): {} {{\n%entry:\n{}}}", _ident, *_funcType, *_block);
    return res;
  }
};

class RetStmtAST;

class BlockAST : public BaseAST
{
public:
  vector<PBase> _list;
  string dump() const override
  {

    GetTableStack().push();
    string res;
    for (const auto &p : _list)
    {
      res += p->dump();
    }
    GetTableStack().pop();
    return res;
  }
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
    string calc;
    if (_type == ExpTypes::Const)
    {
      calc = format("{}", *_l);
    }
    else if (_type == ExpTypes::LVal)
    {
      assert(_ident.has_value());
      auto r = GetTableStack().query(*_ident);
      assert(r.has_value());
      if (r->index() == 0)
      {
        calc = format("%{}", _id);
      }
      else
      {
        calc = format("{}", std::get<int>(*r));
      };
    }
    else
    {
      assert(_id != -1);
      calc = format("%{}", _id);
    }
    return calc;
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
    else if (_type == ExpTypes::LVal)
    {
      assert(_ident.has_value());
      auto r = GetTableStack().query(*_ident);
      assert(r.has_value());
      if (r->index() == 0)
      {
        _id = GetSlotAllocator().getSlot();
        calc += format("\t%{} = load %{}\n", _id, *GetTableStack().rename(_ident.value()));
      }
    }
    return format("{}{}{}", calc_l, calc_r, calc);
  }
  int eval() const
  {
    if (_type == ExpTypes::LVal)
    {
      assert(_ident.has_value());
      auto res = GetTableStack().query(*_ident);
      assert(res.has_value());
      assert(res->index() == 1);
      return get<int>(*res);
    }
    else if (_type == ExpTypes::Binary)
    {
      int result = 0;
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
      ExprAST &l = dynamic_cast<ExprAST &>(*_l);
      int result = l.eval();
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
      NumberAST &l = dynamic_cast<NumberAST &>(*_l);
      return l.value;
    }
    else
    {
      throw logic_error("invalid expr type");
      return 0;
    }
  }
};

class RetStmtAST : public BaseAST
{
public:
  unique_ptr<BaseAST> _expr;
  const ExprAST &expr() const { return dynamic_cast<const ExprAST &>(*_expr); }
  string dump() const override
  {
    if (!_expr)
      return "\tret\n";
    assert(typeid(*_expr) == typeid(ExprAST));
    auto inst = expr().dump_inst();
    auto id = expr().dump();
    return format("{}\tret {}\n", inst, id);
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
  optional<PBase> _init;
  BaseTypes _bType;
  DefAST(DeclTypes type, const string &i) : _type(type), _ident(i) {}
  DefAST(DeclTypes type, const string &i, PBase p) : _type(type), _ident(i), _init(move(p)) {}
  string dump() const override
  {
    string calc;
    if (_type == DeclTypes::Const)
    {
      assert(_init.has_value());
      auto &init = dynamic_cast<ExprAST &>(**_init);
      GetTableStack().back().insert(_ident, init.eval());
    }
    else
    {
      GetTableStack().back().insert(_ident, Symbol());
      auto r = *GetTableStack().rename(_ident);
      calc += format("\t%{} = alloc i32\n", r);
      if (_init.has_value())
      {
        auto &p = dynamic_cast<ExprAST &>(*_init.value());
        calc += p.dump_inst();
        calc += format("\tstore {}, %{}\n", p.dump(), r);
      }
    }
    return calc;
  }
};

class AssignAST : public BaseAST
{
public:
  unique_ptr<string> _id;
  PBase _r;
  string dump() const override
  {
    auto &r = dynamic_cast<ExprAST &>(*_r);
    string code;
    code += r.dump_inst();
    code += format("\tstore {}, %{}\n", r.dump(), *GetTableStack().rename(*_id));
    return code;
  }
};