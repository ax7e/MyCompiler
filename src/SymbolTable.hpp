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

class SymbolTable
{
  map<string, int> _table;

public:
  void insert(const string &id, int w) { _table[id] = w; }
  optional<int> query(string id)
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

  optional<int> query(string id)
  {
    for (auto i = _stack.rbegin(); i != _stack.rend(); ++i)
    {
      auto res = i->query(id);
      if (res)
        return res;
    }
    return std::nullopt;
  }

  void pop()
  {
    _stack.pop_back();
  }
};

TableStack &GetTableStack();