#pragma once

#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

using std::string;
using std::unique_ptr;

enum class BaseTypes { Integer, FloatNumber };

class BaseAST {
public:
  virtual ~BaseAST() = default;
  virtual string dump() const = 0;
};

template <typename AST>
struct fmt::formatter<
    AST, std::enable_if_t<std::is_base_of<BaseAST, AST>::value, char>>
    : fmt::formatter<std::string> {
  template <typename FormatCtx> auto format(const AST &a, FormatCtx &ctx) {
    return fmt::formatter<std::string>::format(a.dump(), ctx);
  }
};

template <> struct fmt::formatter<BaseTypes> : fmt::formatter<std::string> {
  template <typename FormatCtx>
  auto format(const BaseTypes &a, FormatCtx &ctx) {
    string name;
    switch (a) {
    case BaseTypes::Integer:
      name = "Int";
      break;
    case BaseTypes::FloatNumber:
      name = "Float";
      break;
    default:
      throw format_error("invalid type");
    }
    return fmt::formatter<std::string>::format(name, ctx);
  }
};

class CompUnitAST : public BaseAST {
public:
  unique_ptr<BaseAST> _funcDef;
  string dump() const override {
    auto out = fmt::memory_buffer();
    format_to(std::back_inserter(out), "CompUnitAST {{ {} }}", *_funcDef);
    return string(out.data());
  }
};

class FuncDefAST : public BaseAST {
public:
  unique_ptr<BaseAST> _funcType;
  std::string _ident;
  unique_ptr<BaseAST> _block;
  string dump() const override {
    return fmt::format("FuncDefAST {{ {}, {}, {} }}", *_funcType, _ident,
                       *_block);
  }
};

class StmtAST;

class BlockAST : public BaseAST {
public:
  unique_ptr<BaseAST> _stmt;
  string dump() const override {
    return fmt::format("BlockAST {{ {} }}", *_stmt);
  }
};

class NumberAST;
class StmtAST : public BaseAST {
public:
  unique_ptr<BaseAST> _number;
  string dump() const override {
    return fmt::format("StmtAST {{ {} }}", *_number);
  }
};

class NumberAST : public BaseAST {
public:
  int value;
  string dump() const override { return fmt::format("Number {{ {} }}", value); }
};

class FuncTypeAST : public BaseAST {
public:
  BaseTypes _type;
  string dump() const override {
    return fmt::format("FuncTypeAST {{ {} }}", _type);
  }
};