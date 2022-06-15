%code requires {
  #include <memory>
  #include <string>
}

%define parse.trace

%{

#include <iostream>
#include <memory>
#include <string>
#include "AST.hpp"

int yylex();
void yyerror(std::unique_ptr<BaseAST> &ast, const char *s);

using namespace std;

%}

// 定义 parser 函数和错误处理函数的附加参数
// 我们需要返回一个字符串作为 AST, 所以我们把附加参数定义成字符串的智能指针
// 解析完成后, 我们要手动修改这个参数, 把它设置成解析得到的字符串
%parse-param { std::unique_ptr<BaseAST> &ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val; 
}

// lexer 返回的所有 token 种类的声明
// 注意 IDENT 和 INT_CONST 会返回 token 的值, 分别对应 str_val 和 int_val
%token INT RETURN  AND_CONST OR_CONST
%token <str_val> IDENT REL_OP EQ_OP
%token <int_val> INT_CONST

// 非终结符的类型定义
%type <str_val> UnaryOp 
%type <ast_val> FuncDef FuncType Block Stmt Number Exp PrimaryExp UnaryExp MulExp AddExp RelExp EqExp LAndExp LOrExp 

%%

// 开始符, CompUnit ::= FuncDef, 大括号后声明了解析完成后 parser 要做的事情
// 之前我们定义了 FuncDef 会返回一个 str_val, 也就是字符串指针
// 而 parser 一旦解析完 CompUnit, 就说明所有的 token 都被解析了, 即解析结束了
// 此时我们应该把 FuncDef 返回的结果收集起来, 作为 AST 传给调用 parser 的函数
// $1 指代规则里第一个符号的返回值, 也就是 FuncDef 的返回值
CompUnit
  : FuncDef {
    auto compUnit = new CompUnitAST(); 
    compUnit->_funcDef = unique_ptr<BaseAST>($1); 
    ast = unique_ptr<BaseAST>(compUnit); 
  }
  ;

// Function definition
FuncDef
  : FuncType IDENT '(' ')' Block {
    auto ast = new FuncDefAST(); 
    ast->_funcType = unique_ptr<BaseAST>($1); 
    ast->_ident = *unique_ptr<string>($2); 
    ast->_block = unique_ptr<BaseAST>($5);
    $$ = ast;
  }
  ;

FuncType
  : INT {
    auto ast = new FuncTypeAST();
    ast->_type = BaseTypes::Integer; 
    $$ = ast;
  }
  ;

Block
  : '{' Stmt '}' {
    auto ast = new BlockAST();
    ast->_stmt = unique_ptr<BaseAST>($2);
    $$ = ast; 
  }
  ;

Stmt
  : RETURN Exp ';' {
    auto ast = new StmtAST(); 
    ast->_expr = unique_ptr<BaseAST>($2);
    $$ = ast;
  }
  ;

Exp 
  : LOrExp {
    $$ = $1;
  }
  ;

LOrExp 
  : LAndExp {
    $$ = $1; 
  }
  | LOrExp OR_CONST LAndExp {
    $$ = concat("||", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  ;

LAndExp 
  : EqExp {
    $$ = $1;
  } 
  | LAndExp AND_CONST EqExp {
    $$ = concat("&&", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  ;

EqExp 
  : RelExp {
    $$ = $1;
  } 
  | EqExp EQ_OP RelExp {
    $$ = concat(*unique_ptr<string>($2), unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  ;

RelExp 
  : AddExp {
    $$ = $1;
  } 
  | RelExp REL_OP AddExp {
    $$ = concat(*unique_ptr<string>($2), unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  ;


AddExp 
  : MulExp {
    $$ = $1;
  } 
  | AddExp '+' MulExp {
    $$ = concat("+", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  | AddExp '-' MulExp {
    $$ = concat("-", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  ;

MulExp 
  : UnaryExp {
    $$ = $1;
  } 
  | MulExp '*' UnaryExp {
    $$ = concat("*", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  | MulExp '/' UnaryExp {
    $$ = concat("/", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  | MulExp '%' UnaryExp {
    $$ = concat("%", unique_ptr<BaseAST>($1), unique_ptr<BaseAST>($3)); 
  }
  ;

UnaryOp 
  : '+' {
    $$ = new string("+");
  }
  | '-' {
    $$ = new string("-");
  }
  | '!' {
    $$ = new string("!");
  }
  ;

PrimaryExp 
  : '(' Exp ')' {
    $$ = $2;
  }
  | Number {
    auto ast = new ExprAST(); 
    ast->_type = ExpTypes::Const;
    ast->_l = unique_ptr<BaseAST>($1); 
    $$ = ast; 
  }

UnaryExp
  : PrimaryExp {
    $$ = $1;
  }
  | UnaryOp UnaryExp {
    auto ast = new ExprAST(); 
    ast->_op = *unique_ptr<string>($1); 
    ast->_type = ExpTypes::Unary;
    ast->_l = unique_ptr<BaseAST>($2); 
    $$ = ast; 
  }

Number
  : INT_CONST {
    auto ast = new NumberAST(); 
    ast->value = $1; 
    $$ = ast;
  }
  ;

%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(unique_ptr<BaseAST> &ast, const char *s) {
  cerr << "error: " << s << endl;
}
