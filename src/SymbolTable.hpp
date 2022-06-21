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
enum class BaseTypes;
class BaseAST;
typedef unique_ptr<BaseAST> PBase;
int GenID();

enum class SymbolTypes
{
  Var,
  GlobalVar,
  Func,
  FuncParamVar,
  Str,
  Const,
  Array,
  GlobalArray
};

struct Symbol
{
  SymbolTypes _type;
  std::variant<int, string, BaseTypes, vector<int>> _data;
};

class SymbolTable
{
  map<string, Symbol> _table;
  int _tableId;

public:
  SymbolTable() { _tableId = GenID(); }
  void insert(const string &id, Symbol w)
  {
    _table[id] = w;
  }
  string rename(string id) const
  {
    id += "_" + std::to_string(_tableId);
    return id;
  }
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
  bool _ban;

public:
  void banPush() { _ban = true; }
  void push()
  {
    if (_ban)
      _ban = false;
    else
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
  bool isGlobal() const { return _stack.size() == 1; }

  void insert(string id, Symbol w)
  {
    _stack.back().insert(id, w);
  }

  optional<string> rename(string id)
  {
    for (auto i = _stack.rbegin(); i != _stack.rend(); ++i)
    {
      auto res = i->query(id);
      if (res)
        return i->rename(id);
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
void RegisterLibFunc();