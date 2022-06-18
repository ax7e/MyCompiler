#include "SymbolTable.hpp"

TableStack &GetTableStack()
{
  static TableStack t;
  return t;
}

int SymbolTable::_tableClock;