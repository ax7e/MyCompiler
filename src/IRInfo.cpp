#include "IRInfo.h"
#include <memory>
#include <iostream>
#include <string>
#include <vector>

void SemanticError(int line_no, const std::string &&error_msg)
{
    std::cerr << "语义错误 at line " << line_no << " : " << error_msg << std::endl;
    exit(255);
}

IRInfo::IRInfo()
{
    this->has_call = false;
    this->register_num = 0;
    this->stack_size = 0;
    this->stack_value_base = 0;
}

void IRInfo::reset_func_kir()
{
    this->has_call = false;
    this->register_num = 0;
    this->stack_size = 0;
    this->stack_value_base = 0;
}

std::string IRInfo::find(std::ostream &outfile, koopa_raw_value_t value)
{
    int temp;
    if (this->stack_value_base != 0)
    {
        temp = this->stackMap[value] + this->stack_value_base;
    }
    else
    {
        temp = this->stackMap[value];
    }

    if (temp > 2047)
    {
        string treg = "t" + std::to_string(this->register_num++);
        outfile << "  li\t" + treg + ", " + std::to_string(temp) + "\n";
        outfile << "  add\t" + treg + ", sp, " + treg + "\n";
        return "0(" + treg + ")";
    }
    else
    {
        return std::to_string(temp) + "(sp)";
    }
}

int IRInfo::find_value_in_stack_int(koopa_raw_value_t value)
{
    if (this->stack_value_base != 0)
    { //如果有实参占用栈帧

        return this->stackMap[value] + this->stack_value_base;
    }
    else
    { // 如果没有实参占用栈帧
        return this->stackMap[value];
    }
}