/*
** EPITECH PROJECT, 2022
** compiler [Container maxxing/compiler-dev (frosty_curran)]
** File description:
** Generator
*/

#pragma once
#include "fmt/core.h"
#include "fmt/format.h"
#include "koopa.h"
#include <string>

using std::string;

class Generator {
private:
  decltype(fmt::memory_buffer()) _result;

public:
  Generator() : _result(fmt::memory_buffer()) {}
  void Visit(const koopa_raw_program_t &program);
  void Visit(const koopa_raw_slice_t &slice);
  void Visit(const koopa_raw_function_t &func);
  void Visit(const koopa_raw_basic_block_t &bb);
  void Visit(const koopa_raw_value_t &value);
  void Visit(const koopa_raw_return_t &ret);
  int Visit(const koopa_raw_integer_t &integer);
  template <typename... T>
  auto dump(string fmt, T &&...args) -> decltype(std::back_inserter(_result)) {
    return fmt::format_to(std::back_inserter(_result), fmt + "\n", args...);
  }

  template <typename... T>
  auto dumpt(string fmt, T &&...args) -> decltype(std::back_inserter(_result)) {
    return fmt::format_to(std::back_inserter(_result), "\t" + fmt + "\n",
                          args...);
  }
  string getAssembly() { return fmt::to_string(_result); }
};