#include "SymbolTable.hpp"

TableStack &GetTableStack()
{
  static TableStack t;
  return t;
}