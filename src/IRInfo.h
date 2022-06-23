#pragma once

#include <iostream>
#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>
#include "koopa.h"
#include <map>
#include "fmt/core.h"
using fmt::format;
using fmt::formatter;
using namespace std;

class IRInfo
{
public:
    int register_num;
    int stack_size;
    int stack_value_base; // pointer to the args when there are more than 8 args
    bool has_call;        //用于判断 当前指令 前的部分是否有过call指令，用于epilogue中从战阵中恢复ra寄存器值。

    map<koopa_raw_value_t, int> regMap;   //当前指令对应的寄存器号
    map<koopa_raw_value_t, int> stackMap; //当前指令对应的栈帧偏移量，未加stack_value_base

    IRInfo();
    ~IRInfo() = default;

    void reset_func_kir();
    std::string find(std::ostream &outfile, koopa_raw_value_t value);
    int find_value_in_stack_int(koopa_raw_value_t value);
};
