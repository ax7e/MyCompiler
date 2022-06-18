#pragma once

#include <fmt/format.h>
#include <variant>
#include <iostream>
#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>
#include <cassert>
#include <map>
#include <optional>
#include <vector>

using fmt::format;
using fmt::formatter;
using std::endl;
using std::map;
using std::monostate;
using std::optional;
using std::string;
using std::unique_ptr;
using std::variant;
using std::vector;
class BaseAST;
typedef unique_ptr<BaseAST> PBase;
typedef variant<monostate, int> Symbol;

class SymbolTable
{
  map<string, Symbol> _table;

public:
  void insert(const string &id, Symbol w) { _table[id] = w; }
  optional<Symbol> query(string id)
  {
    if (_table.count(id))
    {
      return _table[id];
    }
    else
    {
      return std::nullopt;
    }
  }
};

class TableStack
{
  vector<SymbolTable> _stack;

public:
  void push()
  {
    _stack.push_back(SymbolTable());
  }

  optional<Symbol> query(string id)
  {
    for (auto i = _stack.rbegin(); i != _stack.rend(); ++i)
    {
      auto res = i->query(id);
      if (res)
        return res;
    }
    return std::nullopt;
  }
  SymbolTable &back() { return _stack.back(); }

  void pop()
  {
    _stack.pop_back();
  }
};

TableStack &GetTableStack();