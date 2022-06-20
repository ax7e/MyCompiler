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
void yyerror(PBase &ast, const char *s);

using namespace std;

%}

// 定义 parser 函数和错误处理函数的附加参数
// 我们需要返回一个字符串作为 AST, 所以我们把附加参数定义成字符串的智能指针
// 解析完成后, 我们要手动修改这个参数, 把它设置成解析得到的字符串
%parse-param { PBase &ast }

// yylval 的定义, 我们把它定义成了一个联合体 (union)
// 因为 token 的值有的是字符串指针, 有的是整数
// 之前我们在 lexer 中用到的 str_val 和 int_val 就是在这里被定义的
// 至于为什么要用字符串指针而不直接用 string 或者 unique_ptr<string>?
// 请自行 STFW 在 union 里写一个带析构函数的类会出现什么情况
%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val; 
  ExprAST *exp_ast_val; 
  NumberAST *number_ast_val; 
  std::vector<PBase> *vec_val;
}

// lexer 返回的所有 token 种类的声明
// 注意 IDENT 和 INT_CONST 会返回 token 的值, 分别对应 str_val 和 int_val
%token INT RETURN  AND_CONST OR_CONST CONST IF ELSE WHILE BREAK CONTINUE VOID
%token <str_val> IDENT REL_OP EQ_OP
%token <int_val> INT_CONST

// 非终结符的类型定义
%type <str_val> UnaryOp LVal
%type <ast_val> FuncDef Block Stmt 
%type <number_ast_val> Number  
%type <ast_val> Decl ConstDecl ConstDef ConstInitVal 
%type <exp_ast_val> MulExp AddExp RelExp EqExp LAndExp LOrExp Exp PrimaryExp UnaryExp ConstExp
%type <vec_val> BlockItemList ConstDefList VarDefList CompUnitList FuncDefParamList FuncCallParamList 
%type <ast_val> VarDecl VarDef InitVal BlockItem FuncDefParam
%type <ast_val> OpenStmt ClosedStmt SimpleStmt 


%%

CompUnit 
  : CompUnitList {
    auto t = new CompUnitAST();
    t->_list = move(*unique_ptr<vector<PBase>>($1));
    ast = PBase(t);
  }
  ;

CompUnitList
  : FuncDef {
    $$ = new vector<PBase>(); 
    $$->push_back(PBase($1));
  }
  | CompUnitList FuncDef {
    $$ = $1;
    $$->push_back(PBase($2));
  }
  ;

// Function definition
FuncDef
  : INT IDENT '(' FuncDefParamList ')' Block {
    $$ = new FuncDefAST(BaseTypes::Integer, $2, $4, $6); 
  }
  | VOID IDENT '(' FuncDefParamList ')' Block {
    $$ = new FuncDefAST(BaseTypes::Void, $2, $4, $6); 
  }
  | INT IDENT '('')' Block  {
    $$ = new FuncDefAST(BaseTypes::Integer, $2, nullptr, $5); 
  }
  | VOID IDENT '(' ')' Block {
    $$ = new FuncDefAST(BaseTypes::Void, $2, nullptr, $5); 
  }
  ;

FuncDefParamList 
  : FuncDefParamList ',' FuncDefParam {
    $$ = $1;
    $$->push_back(PBase($3));
  }
  | FuncDefParam {
    auto v = new vector<PBase>();
    v->push_back(PBase($1));
    $$ = v;
  }
  ;

FuncDefParam
  : INT IDENT {
    auto t = new FuncDefParamAST();
    t->_ident = *unique_ptr<string>($2);
    t->_type = BaseTypes::Integer;
    $$ = t;
  }
  | VOID IDENT {
    auto t = new FuncDefParamAST();
    t->_ident = *unique_ptr<string>($2);
    t->_type = BaseTypes::Void;
    $$ = t;
  }
  ;

Block
  : '{' BlockItemList '}' {
    auto ast = new BlockAST();
    ast->_list = move(*unique_ptr<vector<PBase>>($2));
    $$ = ast; 
  }
  ;

Stmt 
  : OpenStmt {
    $$ = $1;
  }
  | ClosedStmt {
    $$ = $1;
  }
  ;

SimpleStmt
  : RETURN Exp ';' {
    $$ = new RetStmtAST($2); 
  }
  | RETURN ';' {
    $$ = new RetStmtAST(); 
  }
  | LVal '=' Exp ';' {
    auto ast = new AssignAST();
    ast->_id = unique_ptr<string>($1);
    ast->_r = PBase($3);
    $$ = ast;
  }
  | Block {
    $$ = $1;
  }
  | ';' {
    auto ast = new NullStmtAST();  
    $$ = ast;
  }
  | Exp ';' {
    auto ast = new ExpStmtAST();  
    ast->_exp = PBase($1);
    $$ = ast;

  }
  | BREAK {
    $$ = new BreakStmt();
  }
  | CONTINUE {
    $$ = new ContinueStmt();
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
    $$ = concat("||", $1, $3); 
  }
  ;

LAndExp 
  : EqExp {
    $$ = $1;
  } 
  | LAndExp AND_CONST EqExp {
    $$ = concat("&&", $1, $3); 
  }
  ;

EqExp 
  : RelExp {
    $$ = $1;
  } 
  | EqExp EQ_OP RelExp {
    $$ = concat(*unique_ptr<string>($2), $1, $3); 
  }
  ;

RelExp 
  : AddExp {
    $$ = $1;
  } 
  | RelExp REL_OP AddExp {
    $$ = concat(*unique_ptr<string>($2), $1, $3); 
  }
  ;


AddExp 
  : MulExp {
    $$ = $1;
  } 
  | AddExp '+' MulExp {
    $$ = concat("+", $1, $3); 
  }
  | AddExp '-' MulExp {
    $$ = concat("-", $1, $3); 
  }
  ;

MulExp 
  : UnaryExp {
    $$ = $1;
  } 
  | MulExp '*' UnaryExp {
    $$ = concat("*", ($1), ($3)); 
  }
  | MulExp '/' UnaryExp {
    $$ = concat("/", ($1), ($3)); 
  }
  | MulExp '%' UnaryExp {
    $$ = concat("%", ($1), ($3)); 
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
    auto ast = new ConstExprAST(); 
    ast->_num = unique_ptr<NumberAST>($1);
    $$ = ast; 
  }
  | LVal {
    auto ast = new LValExprAST(); 
    ast->_ident = *unique_ptr<string>($1);
    $$ = ast; 
  }
  ;

UnaryExp
  : PrimaryExp {
    $$ = $1;
  }
  | UnaryOp UnaryExp {
    auto ast = new UnaryExprAST(); 
    ast->_op = *unique_ptr<string>($1); 
    ast->_child = unique_ptr<ExprAST>($2); 
    $$ = ast; 
  }
  | IDENT '(' ')' {
    $$ = new FuncCallExprAST($1); 
  }
  | IDENT '(' FuncCallParamList ')' {
    $$ = new FuncCallExprAST($1, $3); 
  }
  ;

FuncCallParamList :
  Exp {
    auto v = new vector<PBase>();
    v->push_back(PBase($1));
    $$ = v;
  }
  | FuncCallParamList ',' Exp {
    $$ = $1;
    $$->push_back(PBase($3));
  }

Number
  : INT_CONST {
    auto ast = new NumberAST(); 
    ast->value = $1; 
    $$ = ast;
  }
  ;

Decl
  : ConstDecl {
    $$ = $1;
  }
  | VarDecl {
    $$ = $1;
  }
  ;

ConstDecl
  : CONST INT ConstDefList ';' {
    auto ast = new DeclAST(
      DeclTypes::Const, 
      BaseTypes::Integer, 
      unique_ptr<vector<PBase>>($3));
    $$ = ast;
  }
  ;

ConstDef
  : IDENT '=' ConstInitVal {
    auto ast = new DefAST(
      DeclTypes::Const,
      *unique_ptr<string>($1), // ident
      PBase($3)); // init
    $$ = ast;
  }
  ;

ConstInitVal
  : ConstExp {
    $$ = $1;
  }
  ;

LVal 
  : IDENT {
    $$ = $1;
  }
  ;

ConstExp 
  : Exp {
    $$ = $1;
  }
  ;

VarDecl
  : INT VarDefList ';' {
    auto ast = new DeclAST(
      DeclTypes::Variable, 
      BaseTypes::Integer, 
      unique_ptr<vector<PBase>>($2));
    $$ = ast;
  }
  ;

VarDef
  : IDENT {
    auto ast = new DefAST(DeclTypes::Variable, *unique_ptr<string>($1));
    $$ = ast;
  }
  | IDENT '=' InitVal {
    auto ast = new DefAST(DeclTypes::Variable, *unique_ptr<string>($1), PBase($3));
    $$ = ast;
  }
  ;

InitVal 
  : Exp {
    $$ = $1;
  }
  ;

ConstDefList
  : ConstDef {
    vector<PBase > *v = new vector<PBase >;
    v->push_back(PBase($1));
    $$ = v;
  }
  | ConstDefList ',' ConstDef {
    vector<PBase > *v = ($1);
    v->push_back(PBase($3));
    $$ = v;
  }
  ;

VarDefList
  : VarDef {
    vector<PBase > *v = new vector<PBase >;
    v->push_back(PBase($1));
    $$ = v;
  }
  | VarDefList ',' VarDef {
    vector<PBase > *v = ($1);
    v->push_back(PBase($3));
    $$ = v;
  }
  ;

BlockItem 
  : Decl {
    $$ = $1;
  }
  | Stmt {
    $$ = $1;
  }
  ;
  
BlockItemList
  : {
    vector<PBase> *v = new vector<PBase>(); 
    $$ = v;
  }
  | BlockItemList BlockItem {
    auto v = $1;
    v->push_back(PBase($2));
    $$ = v;
  }
  ;

ClosedStmt 
  : SimpleStmt {
      $$ = $1;
  }
  | IF '(' Exp ')' ClosedStmt ELSE ClosedStmt {
    auto ast = new IFStmtAST($3,$5,$7); 
    $$ = ast;
  }
  | WHILE '(' Exp ')' ClosedStmt {
    $$ = new WhileStmtAST($3, $5);
  }
  ;

OpenStmt 
  : IF '(' Exp ')' Stmt {
    auto ast = new IFStmtAST($3,$5,nullptr); 
    assert(typeid(*ast->_if) == typeid(BlockAST));
    $$ = ast;
  } 
  | IF '(' Exp ')' ClosedStmt ELSE OpenStmt {
    auto ast = new IFStmtAST($3,$5,$7);
    assert(typeid(*ast->_if) == typeid(BlockAST));
    $$ = ast;
  }
  | WHILE '(' Exp ')' OpenStmt {
    $$ = new WhileStmtAST($3, $5);
  }
  ;
%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(PBase &ast, const char *s) {
  cerr << "error: " << s << endl;
}

