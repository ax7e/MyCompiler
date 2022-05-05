#pragma once

#include <fmt/format.h>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

using std::endl;
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
      name = "i32";
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
  string dump() const override { return fmt::format("{}", *_funcDef); }
};

class FuncDefAST : public BaseAST {
public:
  unique_ptr<BaseAST> _funcType;
  std::string _ident;
  unique_ptr<BaseAST> _block;
  string dump() const override {
    return fmt::format("fun @{}: {} {{\n{}}}", _ident, *_funcType, *_block);
  }
};

class StmtAST;

class BlockAST : public BaseAST {
public:
  unique_ptr<BaseAST> _stmt;
  string dump() const override { return fmt::format("@entry:\t{}", *_stmt); }
};

class NumberAST;
class StmtAST : public BaseAST {
public:
  unique_ptr<BaseAST> _number;
  string dump() const override { return fmt::format("ret {}\n", *_number); }
};

class NumberAST : public BaseAST {
public:
  int value;
  string dump() const override { return fmt::format("{}", value); }
};

class FuncTypeAST : public BaseAST {
public:
  BaseTypes _type;
  string dump() const override { return fmt::format("{}", _type); }
};