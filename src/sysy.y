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

%union {
  std::string *str_val;
  int int_val;
  BaseAST *ast_val; 
  ExprAST *exp_ast_val; 
  NumberAST *number_ast_val; 
  BlockAST *blk_ast_val; 
  std::vector<PBase> *vec_val;
  ArrayRefAST *array_ref_ast_val; 
  ArrayInitListAST *array_init_list_val;
  ArrayDefAST *array_def_ast_val;
  LValExprAST *lval_ast_val;
}

%token INT RETURN  AND_CONST OR_CONST CONST IF ELSE WHILE BREAK CONTINUE VOID
%token <str_val> IDENT REL_OP EQ_OP
%token <int_val> INT_CONST

%type <str_val> UnaryOp 
%type <lval_ast_val> LVal
%type <ast_val> FuncDef Stmt
%type <number_ast_val> Number  
%type <ast_val> Decl ConstDecl ConstDef
%type <blk_ast_val> Block
%type <exp_ast_val> MulExp AddExp RelExp EqExp LAndExp LOrExp Exp PrimaryExp UnaryExp ConstExp ConstInitVal InitVal
%type <vec_val> BlockItemList ConstDefList VarDefList CompUnitList FuncDefParamList FuncCallParamList 
%type <ast_val> VarDecl VarDef BlockItem FuncDefParam
%type <ast_val> OpenStmt ClosedStmt SimpleStmt 
%type <array_init_list_val> ArrayInitList
%type <array_ref_ast_val> ArrayRef ArrayParam
%type <vec_val> ArrayInitListInner


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
  | Decl {
    $$ = new vector<PBase>(); 
    $$->push_back(PBase($1));
  }
  | CompUnitList Decl {
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
  | ArrayParam {
    $$ = $1;
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
    $$ = new AssignAST($1, $3);
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
    $$ = new NumberExprAST($1); 
  }
  | LVal {
    $$ = $1;
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
    $$ = new NumberAST($1); 
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



ConstInitVal
  : ConstExp {
    $$ = $1;
  }
  ;

LVal 
  : IDENT {
    $$ = new LValVarExprAST($1); 
  }
  | ArrayRef {
     $$ = new LValArrayRefExprAST($1); 
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
    auto ast = new DefAST(DeclTypes::Variable, *unique_ptr<string>($1), unique_ptr<ExprAST>($3));
    $$ = ast;
  }
  | ArrayRef '=' ArrayInitList {
    $$ = new ArrayDefAST(DeclTypes::Variable, $1, $3);
  }
  | ArrayRef {
    $$ = new ArrayDefAST(DeclTypes::Variable, $1);
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



ArrayRef  
  : IDENT '[' ConstExp ']' {
    $$ = new ArrayRefAST($1);
    $$->_data.emplace_back($3);
  }
  | ArrayRef '[' ConstExp ']' {
    $$ = $1;
    $$->_data.emplace_back($3);
  }
  ;

ArrayParam
  : INT IDENT '[' ']' {
    $$ = new ArrayRefAST($2);
    $$->_data.emplace_back(new NumberExprAST(new NumberAST(-1)));
  }
  | ArrayParam '[' ConstExp ']' {
    $$ = $1;
    $$->_data.emplace_back($3);
  }

ArrayInitList 
  : '{'  '}' {
    $$ = new ArrayInitListAST(); 
  }
  | '{' ArrayInitListInner '}' {
    $$ = new ArrayInitListAST($2); 
  }
  ;

ArrayInitListInner
  : ConstExp  {
    $$ = new vector<unique_ptr<BaseAST>>();
    $$->emplace_back($1);
  }
  | ArrayInitList  {
    $$ = new vector<unique_ptr<BaseAST>>();
    $$->emplace_back($1);
  }
  | ArrayInitListInner ',' ConstExp {
    $$ = $1;
    $$->emplace_back($3);
  } 
  | ArrayInitListInner ',' ArrayInitList {
    $$ = $1;
    $$->emplace_back($3);
  }
  ;

ConstDef 
  : ArrayRef '=' ArrayInitList {
    $$ = new ArrayDefAST(DeclTypes::Const, $1, $3);
  }
  | ArrayRef {
    $$ = new ArrayDefAST(DeclTypes::Const, $1);
  }
  | IDENT '=' ConstInitVal {
    $$ = new DefAST(DeclTypes::Const, *unique_ptr<string>($1), unique_ptr<ExprAST>($3));
  }
  
%%

// 定义错误处理函数, 其中第二个参数是错误信息
// parser 如果发生错误 (例如输入的程序出现了语法错误), 就会调用这个函数
void yyerror(PBase &ast, const char *s) {
  cerr << "error: " << s << endl;
}

