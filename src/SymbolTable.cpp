#include "SymbolTable.hpp"

TableStack &GetTableStack()
{
  static TableStack t;
  return t;
}

int GenID()
{
  static int clk = 0;
  return ++clk;
}

HelperTable &GetHelperTable()
{
  static HelperTable t;
  return t;
}