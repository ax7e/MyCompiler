#include "SymbolTable.hpp"
#include "AST.hpp"

TableStack &GetTableStack()
{
  static TableStack t;
  return t;
}

void RegisterLibFunc()
{
  auto reg = [](string s, BaseTypes t)
  {
    GetTableStack().insert(s, Symbol{SymbolTypes::Func, t});
  };
  reg("getint", BaseTypes::Integer);
  reg("getch", BaseTypes::Integer);
  reg("getarray", BaseTypes::Integer);
  reg("putint", BaseTypes::Void);
  reg("putch", BaseTypes::Void);
  reg("putarray", BaseTypes::Void);
  reg("starttime", BaseTypes::Void);
  reg("stoptime", BaseTypes::Void);
}

int GenID()
{
  static int clk = 0;
  return ++clk;
}
