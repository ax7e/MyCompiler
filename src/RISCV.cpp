#include <string>
#include <cassert>
#include <typeinfo>
#include <iostream>
#include <fstream>
#include <map>
#include "RISCV.h"
#include "IRInfo.h"
#include "koopa.h"

using namespace std;

int register_num = 0;
map<koopa_raw_value_t, int> regMap;
IRInfo kirinfo;

void koopa_ir_from_str(string irstr, ostream &outfile, IRInfo &kirinfo)
{
    const char *str = irstr.c_str();

    koopa_program_t program;
    koopa_error_code_t ret = koopa_parse_from_string(str, &program);
    assert(ret == KOOPA_EC_SUCCESS); // 确保解析时没有出错
    koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
    koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
    koopa_delete_program(program);
    kirinfo.register_num = 0;
    Visit(raw, outfile);
    koopa_delete_raw_program_builder(builder);
}

void Visit(const koopa_raw_program_t &program, std::ostream &outfile)
{
    Visit(program.values, outfile);
    Visit(program.funcs, outfile);
}

void Visit(const koopa_raw_slice_t &slice, std::ostream &outfile)
{
    for (size_t i = 0; i < slice.len; ++i)
    {
        auto ptr = slice.buffer[i];
        switch (slice.kind)
        {
        case KOOPA_RSIK_FUNCTION:
            Visit_func(reinterpret_cast<koopa_raw_function_t>(ptr), outfile);
            break;
        case KOOPA_RSIK_BASIC_BLOCK:
            Visit_bblcok(reinterpret_cast<koopa_raw_basic_block_t>(ptr), outfile);
            break;
        case KOOPA_RSIK_VALUE:
            Visit_val(reinterpret_cast<koopa_raw_value_t>(ptr), outfile);
            break;
        case KOOPA_RSIK_TYPE:
            break;
        default:
            assert(false);
        }
        kirinfo.register_num = 0;
    }
}

void Visit_func(const koopa_raw_function_t &func, std::ostream &outfile)
{
    kirinfo.reset_func_kir();

    if (func->bbs.len == 0)
    {
        return;
    }
    auto name = string(func->name).substr(1);
    outfile << format("\t.text\n\t.globl {}\n{}:\n", name, name);
    func_params_reg(func->params, outfile);
    func_prologue(func->bbs, outfile);
    Visit(func->bbs, outfile);
    outfile << "\n";
}

void func_params_reg(const koopa_raw_slice_t &params, std::ostream &outfile)
{
    for (size_t i = 0; i < params.len; ++i)
    {
        auto ptr = params.buffer[i];
        auto value = reinterpret_cast<koopa_raw_value_t>(ptr);
        kirinfo.regMap[value] = i;
    }
}

void func_prologue(const koopa_raw_slice_t &bbsslice, std::ostream &outfile)
{
    int stack_size = 0;
    int stack_s = 0; //局部变量分配的栈空间
    int stack_r = 0; //为  ra 分配的栈空间
    int stack_a = 0; //传参预留的栈空间
    // 1. 计算局部变量的栈空间S
    for (size_t i = 0; i < bbsslice.len; ++i)
    { //访问每一个基本块
        auto ptr = bbsslice.buffer[i];
        auto bblock = reinterpret_cast<koopa_raw_basic_block_t>(ptr);
        auto instr_slice = bblock->insts;
        for (size_t j = 0; j < instr_slice.len; j++)
        { //访问基本块中的指令
            const auto &value = reinterpret_cast<koopa_raw_value_t>(instr_slice.buffer[j]);
            if (value->ty->tag != KOOPA_RTT_UNIT)
            { //如果是指令的类型不是 unit
                // 1. 分配其在stack中的具体位置, 并记录, 数组为其基址地址， 数组形参为指针，也是4字节一个地址
                kirinfo.stackMap[value] = stack_s;
                if (value->kind.tag == KOOPA_RVT_ALLOC)
                { // 2. 如果是当前指令为 alloc 类型

                    // TODO 数组形参alloc可能需要修改
                    assert(value->ty->tag == KOOPA_RTT_POINTER);
                    auto kind = value->ty->data.pointer.base; //获取当前 指针的base
                    int arraysize = 1;                        //初始化数组长度
                    while (kind->tag == KOOPA_RTT_ARRAY)
                    { //初始化数值时此部分跳过
                        //如果当前 kind 指向的为数组
                        int cursize = kind->data.array.len; //获取当前维度的长度
                        arraysize *= cursize;
                        kind = kind->data.array.base; //获取当前数组的base
                    }
                    stack_s += 4 * arraysize;
                    // cout << value->name << " arraysize: "<< arraysize <<endl;
                }
                else
                { // 2. 非alloc类型在栈中分配 4字节
                    // 为每个指令分配 4 字节
                    stack_s += 4;
                }
            }

            if (value->kind.tag == KOOPA_RVT_CALL)
            { //访问到call指令
                // 2. 计算 ra 的栈空间 R
                stack_r = 4;

                // 3. 计算传参预留的栈空间 A
                if ((int(value->kind.data.call.args.len) - 8) > stack_a)
                { // max (len - 8, 0)
                    // cout << int(value->kind.data.call.args.len) - 8 <<endl;
                    stack_a = int(value->kind.data.call.args.len) - 8;
                }
            }
        }
    }
    stack_a *= 4;
    stack_size = stack_s + stack_r + stack_a;

    // 2. 计算
    if ((stack_size % 16) != 0)
    { //按16字节补齐
        stack_size = (stack_size / 16 + 1) * 16;
    }
    kirinfo.stack_value_base = stack_a;
    kirinfo.stack_size = stack_size;

    if (stack_size != 0)
    {
        if (stack_size > 2047)
        {
            outfile << "  li\tt0, " + to_string(-stack_size) + "\n";
            outfile << "  add\tsp, sp, t0\n";
        }
        else
        {
            outfile << "  addi\tsp, sp, " << to_string(-stack_size) << endl;
        }
    }

    if (stack_r != 0)
    { //保存 ra 寄存器，如果函数体中含有 call 指令
        if (stack_size > 2047)
        {
            outfile << "  li\tt0, " + to_string(stack_size - 4) + "\n";
            outfile << "  add\tt0, sp, t0\n";
            outfile << "  sw\tra, 0(t0)\n";
        }
        else
        {
            outfile << "  sw\tra, " + to_string(stack_size - 4) + "(sp)\n";
        }
        kirinfo.has_call = true; //后面函数中包含了call语句
    }

    cout << "************stack_size = " << stack_size << endl;
    return;
}

void Visit_bblcok(const koopa_raw_basic_block_t &bblock, std::ostream &outfile)
{
    string bblock_name = bblock->name;
    if (bblock_name != "%entry")
        outfile << bblock_name.substr(1) << ":" << endl;
    Visit(bblock->params, outfile);
    Visit(bblock->insts, outfile);
    return;
}

void Visit_val(const koopa_raw_value_t &value, std::ostream &outfile)
{
    const auto &kind = value->kind;
    switch (kind.tag)
    {
    case KOOPA_RVT_RETURN:
        outfile << "\t #ret \n";
        Visit_ret(kind.data.ret, outfile);
        break;
    case KOOPA_RVT_INTEGER:
        Visit_int(kind.data.integer, outfile);
        break;
    case KOOPA_RVT_BINARY:
        Visit_binary(value, outfile);
        break;
    case KOOPA_RVT_ALLOC:
        Visit_alloc(value, outfile);
        break;
    case KOOPA_RVT_LOAD:
        outfile << "\t #load \n";
        Visit_load(value, outfile);
        break;
    case KOOPA_RVT_STORE:
        outfile << "\t #store \n";
        Visit_store(kind.data.store, outfile);
        break;
    case KOOPA_RVT_BRANCH:
        outfile << "\t #branch \n";
        Visit_branch(kind.data.branch, outfile);
        break;
    case KOOPA_RVT_JUMP:
        outfile << "\t #jump \n";
        Visit_jump(kind.data.jump, outfile);
        break;
    case KOOPA_RVT_CALL:
        outfile << "\t #call \n";
        Visit_call(value, outfile);
        break;
    case KOOPA_RVT_GLOBAL_ALLOC:
        Visit_global_alloc(value, outfile);
        break;
    case KOOPA_RVT_AGGREGATE:
        visit_aggregate(value, outfile);
        break;
    case KOOPA_RVT_GET_ELEM_PTR:
        outfile << "\t #getelemptr \n";
        visit_getelemptr(value, outfile);
        break;
    case KOOPA_RVT_GET_PTR:
        outfile << "\t #getptr \n";
        visit_getptr(value, outfile);
        break;
    default:
        // 其他类型暂时遇不到
        assert(false);
    }
    return;
}
void Visit_ret(const koopa_raw_return_t &ret, std::ostream &outfile)
{
    if (ret.value != NULL)
    {
        koopa_raw_value_t retval = ret.value;
        const auto &kind = retval->kind;
        string retreg;
        string retstack;
        if (kind.tag == KOOPA_RVT_INTEGER)
        {
            outfile << "  li\ta0, ";
            Visit_val(ret.value, outfile);
            outfile << "\n";
        }
        else
        {
            retstack = kirinfo.find(outfile, ret.value);
            outfile << "  lw\ta0, " + retstack + "\n";
        }
    }
    if (kirinfo.has_call)
    {
        if (kirinfo.stack_size > 2047)
        {
            outfile << "  li\tt0, " + to_string(kirinfo.stack_size - 4) + "\n";
            outfile << "  add\tt0, sp, t0\n";
            outfile << "  lw\tra, 0(t0)\n";
        }
        else
        {
            outfile << "  lw\tra, " + to_string(kirinfo.stack_size - 4) + "(sp)\n";
        }
    }
    if (kirinfo.stack_size != 0)
    {
        if (kirinfo.stack_size > 2047)
        {
            outfile << "  li\tt0, " + to_string(kirinfo.stack_size) + "\n";
            outfile << "  add\tsp, sp, t0\n";
        }
        else
        {
            outfile << "  addi\tsp, sp, " << to_string(kirinfo.stack_size) << endl;
        }
    }

    outfile << "  ret" << endl;
    return;
}

void Visit_int(const koopa_raw_integer_t &integer, std::ostream &outfile)
{
    int32_t intnum = integer.value;
    outfile << intnum;
    return;
}

void Visit_alloc(const koopa_raw_value_t &value, std::ostream &outfile)
{
    // Everything is on stack
    return;
}

void Visit_load(const koopa_raw_value_t &value, std::ostream &outfile)
{

    kirinfo.register_num++;
    const auto &load = value->kind.data.load;
    string loadstack, global_name;
    switch (load.src->kind.tag)
    {
    case KOOPA_RVT_INTEGER:
        //如果是integer型
        outfile << "  li\tt0, ";
        Visit_val(load.src, outfile);
        outfile << "\n";
        break;
    case KOOPA_RVT_ALLOC:
        //如果是alloc型
        loadstack = kirinfo.find(outfile, load.src); // load指令右值对应的stack位置
        outfile << "  lw\tt0, " + loadstack << endl;
        break;
    case KOOPA_RVT_GLOBAL_ALLOC:
        //如果是global alloc型
        // 1. 先获取全局变量的名称
        global_name = load.src->name;
        // 去掉@符号
        global_name = global_name.substr(1);
        // 2. 将其地址存入t0中
        outfile << "  la\tt0, " + global_name << endl;
        // 3. 利用基址加变址的方式将对应内存存进来。
        outfile << "  lw\tt0, 0(t0)\n";
        break;
    case KOOPA_RVT_GET_ELEM_PTR:
        //如果是 getelemptr 型
        // 1. 将其指针的地址中的内容取出
        loadstack = kirinfo.find(outfile, load.src); // load指令右值对应的stack位置
        outfile << "  lw\tt0, " + loadstack << endl;
        //再将其load进来
        outfile << "  lw\tt0, 0(t0)\n";
        break;
    case KOOPA_RVT_GET_PTR:
        //如果是 getelemptr 型
        // 1. 将其指针的地址中的内容取出
        loadstack = kirinfo.find(outfile, load.src); // load指令右值对应的stack位置
        outfile << "  lw\tt0, " + loadstack << endl;
        //再将其load进来
        outfile << "  lw\tt0, 0(t0)\n";
        break;
    default:
        cout << "load.src->kind.tag = " << load.src->kind.tag << endl;
        std::cerr << "程序错误：load 指令的目标类型不符合预期" << std::endl;

        break;
    }

    // 2.再存入当前 load 指令对应stack的位置
    string instr_stack = kirinfo.find(outfile, value);
    outfile << "  sw\tt0, " + instr_stack << endl;
    return;
}

//访问 store 指令
void Visit_store(const koopa_raw_store_t &store, std::ostream &outfile)
{ // store 指令先load进来, 再存入对应为该指令对应的位置。

    // 1.先load进来
    // 当前指令用t0
    string store_value_reg = "t" + to_string(kirinfo.register_num++);
    // 1.1如果是函数形参则不需要load进来
    if (store.value->kind.tag == KOOPA_RVT_FUNC_ARG_REF)
    {
        // cout << store.value->name << "被函数所使用" << endl;
        string dest_stack = kirinfo.find(outfile, store.dest);
        if (kirinfo.regMap[store.value] >= 8)
        { //当形参没有在寄存器中
            //当前参数存放在上一个函数的栈帧区域
            string loadstack = to_string((kirinfo.regMap[store.value] - 8) * 4 + kirinfo.stack_size) + "(sp)";
            outfile << "  lw\t" + store_value_reg + ", " + loadstack << endl;

            outfile << "  sw\t" + store_value_reg + ", " + dest_stack << endl;
            return;
        }
        string temp = "a" + to_string(kirinfo.regMap[store.value]);
        outfile << "  sw\t" + temp + ", " + dest_stack << endl;
        return;
    }
    else if (store.value->kind.tag == KOOPA_RVT_INTEGER)
    { //如果是数值型
        outfile << "  li\t" + store_value_reg + ", ";
        Visit_val(store.value, outfile);
        outfile << "\n";
    }
    else
    {
        //如果不是数值型
        string loadstack = kirinfo.find(outfile, store.value); // load指令右值对应的stack位置
        outfile << "  lw\t" + store_value_reg + ", " + loadstack << endl;
    }

    // 2.再存入dest对应stack的位置
    string global_name, dest_stack;
    string dest_reg = "t" + to_string(kirinfo.register_num++);
    switch (store.dest->kind.tag)
    {
    case KOOPA_RVT_GLOBAL_ALLOC:
        //如果存入的位置是全局变量
        // 1. 再申请一个变量计算存入的内存位置
        // 先获取全局变量名称
        global_name = store.dest->name;
        // 去掉@符号
        global_name = global_name.substr(1);
        outfile << "  la\t" + dest_reg + ", " + global_name << endl;
        // 2. sw语句存入
        outfile << "  sw\t" + store_value_reg + ", 0(" + dest_reg + ")\n";
        break;
    case KOOPA_RVT_ALLOC:
        //如果存入的位置是局部变量
        dest_stack = kirinfo.find(outfile, store.dest);
        outfile << "  sw\t" + store_value_reg + ", " + dest_stack << endl;
        break;
    case KOOPA_RVT_GET_ELEM_PTR:
        //如果是 getelemptr 型
        //将指针对应stack中存放的内容取出 ： 指针指向的地址
        ++kirinfo.register_num;
        dest_stack = kirinfo.find(outfile, store.dest);
        outfile << "  lw\t" + dest_reg + ", " + dest_stack << endl;
        //将内容从存入 地址对应的内存位置
        outfile << "  sw\t" + store_value_reg + ", 0(" + dest_reg + ")" << endl;
        break;
    case KOOPA_RVT_GET_PTR:
        //如果是getptr型
        ++kirinfo.register_num;
        dest_stack = kirinfo.find(outfile, store.dest);
        outfile << "  lw\t" + dest_reg + ", " + dest_stack << endl;
        //将内容从存入 地址对应的内存位置
        outfile << "  sw\t" + store_value_reg + ", 0(" + dest_reg + ")" << endl;
        break;
    default:
        cout << "store.dest->kind.tag: " << store.dest->kind.tag << endl;

        std::cerr << "程序错误：store dest 类型不符合预期" << std::endl;
        break;
    }

    return;
}

//访问 jump 指令
void Visit_jump(const koopa_raw_jump_t &jump, std::ostream &outfile)
{
    string jump_target = jump.target->name;
    cout << "jump_target:   " << jump_target << endl;

    // 将 @ 符号去除
    outfile << "  j\t" + jump_target.substr(1) + "\n\n";
    return;
}

//访问 branch 指令
void Visit_branch(const koopa_raw_branch_t &branch, std::ostream &outfile)
{
    // 1.将cond的结果 load 进来
    // 当前指令用t0
    kirinfo.register_num++;
    if (branch.cond->kind.tag == KOOPA_RVT_INTEGER)
    { //如果是数值型
        outfile << "  li\tt0, ";
        Visit_val(branch.cond, outfile);
        outfile << "\n";
    }
    else
    {
        //如果不是数值型
        string loadstack = kirinfo.find(outfile, branch.cond); // load指令右值对应的stack位置
        outfile << "  lw\tt0, " + loadstack << endl;
    }

    string true_name = branch.true_bb->name;
    string false_name = branch.false_bb->name;

    // 将 @ 符号去除
    outfile << "  bnez\tt0, " + true_name.substr(1) + "\n";
    outfile << "  j\t" + false_name.substr(1) + "\n\n";

    return;
}

//访问 call 指令
void Visit_call(const koopa_raw_value_t &value, std::ostream &outfile)
{
    // 0. 遍历到call语句
    kirinfo.has_call = true;

    const auto &call = value->kind.data.call;
    // 1. 如果有实参，则将各个实参存入a0-7寄存器 和 sp的栈帧中
    cout << call.args.len << endl;

    for (int i = 0; i < (int)call.args.len; i++)
    {
        koopa_raw_value_t arg_value = reinterpret_cast<koopa_raw_value_t>(call.args.buffer[i]);
        kirinfo.register_num = 0;
        if (i < 8)
        { // 用lw 到 a0 - a7
            if (arg_value->kind.tag == KOOPA_RVT_INTEGER)
            {                                                 //如果是数值型
                int num = arg_value->kind.data.integer.value; //获取数值
                outfile << "  li\ta" + to_string(i) + ", " + to_string(num) + "\n";
            }
            else
            {
                string arg_stack = kirinfo.find(outfile, arg_value); //把栈帧中的内存存入
                outfile << "  lw\ta" + to_string(i) + ", " + arg_stack + "\n";
            }
        }
        else
        {
            string arg_reg = "t" + to_string(kirinfo.register_num++);
            if (arg_value->kind.tag == KOOPA_RVT_INTEGER)
            {                                                 //如果是数值型
                int num = arg_value->kind.data.integer.value; //获取数值
                outfile << "  li\t" + arg_reg + ", " + to_string(num) + "\n";
                outfile << "  sw\t" + arg_reg + ", " + to_string((i - 8) * 4) + "(sp)\n";
            }
            else
            {
                string arg_stack = kirinfo.find(outfile, arg_value); //把栈帧中的内存存入
                outfile << "  lw\t" + arg_reg + ", " + arg_stack + "\n";
                outfile << "  sw\t" + arg_reg + ", " + to_string((i - 8) * 4) + "(sp)\n";
            }
        }
    }
    cout << call.callee->name << endl;
    string funcname = call.callee->name;
    // 将 @ 符号去除
    funcname = funcname.substr(1);
    // 2. call函数调用
    outfile << "  call\t" + funcname + "\n";

    // 3.如果当前call具有返回值，需要将当前call得到的结果保存在栈帧中
    if (value->ty->tag != KOOPA_RTT_UNIT)
    {
        string call_stack = kirinfo.find(outfile, value);
        outfile << "  sw\ta0, " + call_stack + "\n";
    }
}

//访问 global alloc 指令
void Visit_global_alloc(const koopa_raw_value_t &value, std::ostream &outfile)
{
    // 1. 数据段
    outfile << "  .data\n  .globl ";
    // 2. 获取全局变量定义的名称
    string global_name = value->name;
    // 将 @ 符号去除
    global_name = global_name.substr(1);
    outfile << global_name << "\n"
            << global_name << ":\n";
    const auto &init = value->kind.data.global_alloc.init;
    // cout << init->kind.tag << endl;
    if (init->kind.tag == KOOPA_RVT_ZERO_INIT)
    { //如果初始值为zeroinit
        if (value->ty->tag == KOOPA_RTT_POINTER)
        {                      //如果用zeroinit初始化全0数组
            int arraysize = 1; //初始化数组长度
            auto kind = value->ty->data.pointer.base;
            while (kind->tag == KOOPA_RTT_ARRAY)
            { //初始化数值时此部分跳过
                //如果当前 kind 指向的为数组
                int cursize = kind->data.array.len; //获取当前维度的长度
                arraysize *= cursize;
                kind = kind->data.array.base; //获取当前数组的base
            }
            arraysize *= 4;
            // int sizeofarray = 4 * (value->ty->data.pointer.base->data.array.len);
            outfile << "  .zero " + to_string(arraysize) + "\n";
        }
        else
        {
            cout << ".zero" << value->ty->tag << endl;
            outfile << "  .zero 4\n\n";
        }
    }
    else if (init->kind.tag == KOOPA_RVT_INTEGER)
    { //是数值初始
        int num = init->kind.data.integer.value;
        outfile << "  .word " + to_string(num) + "\n";
    }
    else if (init->kind.tag == KOOPA_RVT_AGGREGATE)
    { // 用 aggregate初始化数组
        // 暂时处理一维数组情况
        // 访问aggregate
        Visit_val(init, outfile);
    }
    else
    {
        std::cerr << "程序错误：全局变量初始化不符合预期" << std::endl;
    }
    outfile << endl;
    return;
}

// 访问 aggregate 数组初始化语句
void visit_aggregate(const koopa_raw_value_t &aggregate, std::ostream &outfile)
{
    auto elems = aggregate->kind.data.aggregate.elems;
    for (size_t i = 0; i < elems.len; ++i)
    {
        auto ptr = elems.buffer[i];
        // 根据 elems 的 kind 决定将 ptr 视作何种元素，同一 elems 中存放着所有 elem 为同一类型
        if (elems.kind == KOOPA_RSIK_VALUE)
        { //如果当前的 elem 为 value 类型
            auto elem = reinterpret_cast<koopa_raw_value_t>(ptr);
            if (elem->kind.tag == KOOPA_RVT_AGGREGATE)
            { //如果是 aggregate 型则循环嵌套访问
                visit_aggregate(elem, outfile);
            }
            else if (elem->kind.tag == KOOPA_RVT_INTEGER)
            { //如果是 integer 型 则用 .word + int
                outfile << "  .word ";
                Visit_int(elem->kind.data.integer, outfile); //访问int型
                outfile << endl;
            }
            else if (elem->kind.tag == KOOPA_RVT_ZERO_INIT)
            { //如果是zeroinit类型
                auto kind = elem->ty->data.pointer.base;
                int arraysize = 1; //初始化数组长度
                while (kind->tag == KOOPA_RTT_ARRAY)
                { //初始化数值时此部分跳过
                    //如果当前 kind 指向的为数组
                    int cursize = kind->data.array.len; //获取当前维度的长度
                    arraysize *= cursize;
                    kind = kind->data.array.base; //获取当前数组的base
                }
                arraysize *= 4;
                outfile << "  .zero " + to_string(arraysize) << endl;
            }
            else
            {
                std::cerr << "程序错误： aggregate 的elem 类型不符合预期" << std::endl;
            }
        }
        else
        {
            cout << "elem.kind = " << elems.kind << endl;
            std::cerr << "程序错误： aggregate 的elem 类型不符合预期" << std::endl;
        }
    }
}

//访问 getelemptr 指令
void visit_getelemptr(const koopa_raw_value_t &getelemptr, std::ostream &outfile)
{
    auto src = getelemptr->kind.data.get_elem_ptr.src;
    auto index = getelemptr->kind.data.get_elem_ptr.index;

    auto kind = getelemptr->ty->data.pointer.base;
    int arraysize = 1; //初始化数组长度
    while (kind->tag == KOOPA_RTT_ARRAY)
    { //初始化数值时此部分跳过
        //如果当前 kind 指向的为数组
        int cursize = kind->data.array.len; //获取当前维度的长度
        arraysize *= cursize;
        kind = kind->data.array.base; //获取当前数组的base
    }
    // cout << arraysize << endl;

    string src_reg = "t" + to_string(kirinfo.register_num++); //本行的src地址所用的寄存器
    // 1. 计算 src 的地址
    if (src->kind.tag == KOOPA_RVT_GLOBAL_ALLOC)
    { //如果当前src为全局变量分配
        // 先获取全局变量名称
        string global_name = src->name;
        // 去掉@符号
        global_name = global_name.substr(1);
        outfile << "  la\t" + src_reg + ", " + global_name << endl;
    }
    else if (src->kind.tag == KOOPA_RVT_ALLOC) //如果当前指向的是局部变量
    {
        int srcstack = kirinfo.find_value_in_stack_int(src); // src 对应的stack位置
        if (srcstack < 2047)
        { //如果srcstack 的size 小于 12 位 最大为+2047
            outfile << "  addi\t" + src_reg + ", sp, " + to_string(srcstack) << endl;
        }
        else
        {
            outfile << "  li\t" + src_reg + ", " + to_string(srcstack) << endl;
            outfile << "  add\t" + src_reg + ", sp, " + src_reg << endl;
        }
    }
    else if (src->kind.tag == KOOPA_RVT_GET_ELEM_PTR)
    { //如果当前指向的是指针 getelemptr 将里面的内容load进来
        string srcstack = kirinfo.find(outfile, src);
        outfile << "  lw\t" + src_reg + ", " + srcstack << endl;
    }
    else if (src->kind.tag == KOOPA_RVT_GET_PTR)
    { //如果当前指向的是指针 getptr 将里面的内容load进来
        string srcstack = kirinfo.find(outfile, src);
        outfile << "  lw\t" + src_reg + ", " + srcstack << endl;
        // cout << "src->kind.tag:" << src->kind.tag << endl;
    }
    else
    {
        cout << "src->kind.tag = " << src->kind.tag << endl;
        std::cerr << "程序错误： getelemptr 的 src 类型不符合预期" << std::endl;
    }

    // 2. 获得 index 的大小
    string indexreg = "t" + to_string(kirinfo.register_num++);
    if (index->kind.tag == KOOPA_RVT_INTEGER)
    { // index 要么为数值型
        if (index->kind.data.integer.value == 0 && arraysize == 1)
        { // index 为 0 且为最低一维时 不用计算index，直接return
            string geteleptr_stack = kirinfo.find(outfile, getelemptr);
            outfile << "  sw\t" + src_reg + ", " + geteleptr_stack + "\n";
            return;
        }

        outfile << "  li\t" + indexreg + ", ";
        Visit_int(index->kind.data.integer, outfile); //访问int型
    }
    else
    { // 要么为 %x koopa ir变量
        string index_stack = kirinfo.find(outfile, index);
        outfile << "  lw\t" + indexreg + ", " + index_stack;
        // cout << "index.kind = " << index->kind.tag << endl;
        // std::cerr << "程序错误： getelemptr 的 index 类型不符合预期" << std::endl;
    }

    string size_reg = "t" + to_string(kirinfo.register_num++);
    arraysize *= 4;
    outfile << "\n  li\t" + size_reg + ", " + to_string(arraysize) + "\n"; //当前指针的大小
    outfile << "  mul\t" + indexreg + ", " + indexreg + ", " + size_reg + "\n";

    // 3. 计算 getelemptr 的结果
    outfile << "  add\t" + src_reg + ", " + src_reg + ", " + indexreg + "\n";

    string geteleptr_stack = kirinfo.find(outfile, getelemptr);
    outfile << "  sw\t" + src_reg + ", " + geteleptr_stack + "\n";
    return;
}

void visit_getptr(const koopa_raw_value_t &getptr, std::ostream &outfile)
{

    // TODO
    auto src = getptr->kind.data.get_ptr.src;
    auto index = getptr->kind.data.get_ptr.index;
    // cout << "src->ty->data.pointer.base->tag:" << src->ty->data.pointer.base->tag << endl;

    auto kind = src->ty->data.pointer.base; // src为指针，获取指针的基地址，以此来求 src指向内容的size
    int arraysize = 1;                      //初始化数组长度
    while (kind->tag == KOOPA_RTT_ARRAY)
    { //初始化数值时此部分跳过
        //如果当前 kind 指向的为数组
        int cursize = kind->data.array.len; //获取当前维度的长度
        arraysize *= cursize;
        kind = kind->data.array.base; //获取当前数组的base
    }
    // cout << "arraysize:" << arraysize << endl;

    string src_reg = "t" + to_string(kirinfo.register_num++); //本行的 src 地址所用的寄存器

    // 1. 计算 src 的地址 TODO 这一部分的 src 应该只是从 load 获得的
    if (src->kind.tag == KOOPA_RVT_LOAD)
    {
        string srcstack = kirinfo.find(outfile, src);
        outfile << "  lw\t" + src_reg + ", " + srcstack << endl;
    }
    else
    {
        cout << "src->kind.tag = " << src->kind.tag << endl;
        std::cerr << "程序错误： getptr 的 src 类型不符合预期" << std::endl;
    }

    // 2. 获得 index 的大小
    string indexreg = "t" + to_string(kirinfo.register_num++);
    if (index->kind.tag == KOOPA_RVT_INTEGER)
    { // index 要么为数值型
        if (index->kind.data.integer.value == 0 && arraysize == 1)
        { // index 为 0 且为最低一维时 不用计算index，直接return
            string getptr_stack = kirinfo.find(outfile, getptr);
            outfile << "  sw\t" + src_reg + ", " + getptr_stack + "\n";
            return;
        }

        outfile << "  li\t" + indexreg + ", ";
        Visit_int(index->kind.data.integer, outfile); //访问int型
    }
    else
    { // 要么为 %x koopa ir变量
        string index_stack = kirinfo.find(outfile, index);
        outfile << "  lw\t" + indexreg + ", " + index_stack;
        // cout << "index.kind = " << index->kind.tag << endl;
        // std::cerr << "程序错误： getelemptr 的 index 类型不符合预期" << std::endl;
    }

    string size_reg = "t" + to_string(kirinfo.register_num++);
    arraysize *= 4;
    outfile << "\n  li\t" + size_reg + ", " + to_string(arraysize) + "\n"; //当前指针的大小
    outfile << "  mul\t" + indexreg + ", " + indexreg + ", " + size_reg + "\n";

    // 3. 计算 getelemptr 的结果
    outfile << "  add\t" + src_reg + ", " + src_reg + ", " + indexreg + "\n";

    string getptr_stack = kirinfo.find(outfile, getptr);
    outfile << "  sw\t" + src_reg + ", " + getptr_stack + "\n";
    return;
}

void Visit_binary(const koopa_raw_value_t &value, ostream &outfile)
{
    kirinfo.register_num = 0;
    const auto &binary = value->kind.data.binary;
    switch (binary.op)
    {
    case KOOPA_RBO_NOT_EQ:
        outfile << "  # or\n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_EQ:
        outfile << "  # eq\n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_OR:
        outfile << "\t #ne \n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_GT:
        outfile << "\t #gt \n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_LT:
        outfile << "\t #lt \n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_GE:
        outfile << "\t #ge \n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_LE:
        outfile << "\t #le \n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_AND:
        outfile << "\t #and \n";
        Visit_bin_cond(value, outfile);
        break;
    case KOOPA_RBO_ADD:
        outfile << "\t # add\n";
        Visit_bin_double_reg(value, outfile);
        break;
    case KOOPA_RBO_SUB:
        outfile << "\t # sub\n";
        Visit_bin_double_reg(value, outfile);
        break;
    case KOOPA_RBO_MUL:
        outfile << "\t # mul\n";
        Visit_bin_double_reg(value, outfile);
        break;
    case KOOPA_RBO_DIV:
        outfile << "\t # div\n";
        Visit_bin_double_reg(value, outfile);
        break;
    case KOOPA_RBO_MOD:
        outfile << "\t # mod\n";
        Visit_bin_double_reg(value, outfile);
        break;
    default:
        // 其他类型暂时遇不到
        assert(false);
    }
    kirinfo.regMap.erase(kirinfo.regMap.begin(), kirinfo.regMap.end()); //一条运算指令处理完之后清空当前regMap
    return;
}

void Visit_bin_cond(const koopa_raw_value_t &value, ostream &outfile)
{
    const auto &binary = value->kind.data.binary;

    string leftreg, rightreg, eqregister;           // 左右节点值
    kirinfo.regMap[value] = kirinfo.register_num++; // 为当前指令分配一个寄存器
    eqregister = get_reg_(value);                   //当前指令的寄存器号

    handle_left_right_reg(value, outfile, leftreg, rightreg, eqregister);

    switch (binary.op) // 根据指令类型判断当前指令的运算符
    {
    case KOOPA_RBO_EQ:
        //使用xor和seqz指令完成 等值 判断
        outfile << "  xor\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  seqz\t" + eqregister + ", " + eqregister + '\n';
        break;
    case KOOPA_RBO_NOT_EQ:
        //使用xor和snez指令完成 不等 判断
        outfile << "  xor\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  snez\t" + eqregister + ", " + eqregister + '\n';
        break;
    case KOOPA_RBO_OR:
        //使用or和snez指令完成 或 判断
        outfile << "  or\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  snez\t" + eqregister + ", " + eqregister + '\n';
        break;
    case KOOPA_RBO_GT:
        //大于使用 slt 指令,交换两操作数位置,一条语句直接结束
        outfile << "  slt\t" + eqregister + ", " + rightreg + ", " + leftreg + '\n';
        break;
    case KOOPA_RBO_LT:
        //小于使用 slt 指令,一条语句直接结束
        outfile << "  slt\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        break;
    case KOOPA_RBO_GE:
        //>= 大于等于 先判断反命题: slt 判断 左 < 右, 再用异或 '1' 得到原命题
        outfile << "  slt\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  xori\t" + eqregister + ", " + eqregister + ", 1\n";
        break;
    case KOOPA_RBO_LE:
        //<= 小于等于,交换量操作数后, 与上面操作一致
        outfile << "  slt\t" + eqregister + ", " + rightreg + ", " + leftreg + '\n';
        outfile << "  xori\t" + eqregister + ", " + eqregister + ", 1\n";
        break;
    case KOOPA_RBO_AND:
        // and使用三条指令
        outfile << "  snez\t" + leftreg + ", " + leftreg + '\n';
        outfile << "  snez\t" + rightreg + ", " + rightreg + '\n';
        outfile << "  and\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        break;
    default: // 其他类型暂时遇不到
        assert(false);
    }
    // 2.将计算的结果存入
    string instr_stack = kirinfo.find(outfile, value);
    outfile << "  sw\t" + eqregister + ", " + instr_stack << endl;

    return;
}

void Visit_bin_double_reg(const koopa_raw_value_t &value, ostream &outfile)
{
    // cout << " 二者均为寄存器的指令" << endl;
    const auto &binary = value->kind.data.binary;
    string bin_op; //当前指令的运算符
    switch (binary.op)
    { // 根据指令类型判断当前指令的运算符
    case KOOPA_RBO_ADD:
        bin_op = "add";
        break;
    case KOOPA_RBO_SUB:
        bin_op = "sub";
        break;
    case KOOPA_RBO_MUL:
        bin_op = "mul";
        break;
    case KOOPA_RBO_DIV:
        bin_op = "div";
        break;
    case KOOPA_RBO_MOD:
        bin_op = "rem";
        break;
    default: // 其他类型暂时遇不到
        assert(false);
    }
    string leftreg, rightreg;                       // 左右节点值
    kirinfo.regMap[value] = kirinfo.register_num++; // 为当前指令分配一个寄存器
    string eqregister = get_reg_(value);            // 定义当前指令的寄存器

    handle_left_right_reg(value, outfile, leftreg, rightreg, eqregister);

    outfile << "  " + bin_op + '\t' + eqregister + ", " + leftreg + ", " + rightreg + "\n";
    // 2.将计算的结果存入
    string instr_stack = kirinfo.find(outfile, value);
    outfile << "  sw\t" + eqregister + ", " + instr_stack << endl;
}

void handle_left_right_reg(const koopa_raw_value_t &value, ostream &outfile, string &leftreg, string &rightreg, string &eqregister)
{
    const auto &binary = value->kind.data.binary;
    if (binary.lhs->kind.tag == KOOPA_RVT_INTEGER)
    { //先处理左节点,左节点为int值
        if (binary.lhs->kind.data.integer.value == 0)
        { //左节点为0
            leftreg = "x0";
        }
        else
        { //左节点为非0 int, li到当前寄存器
            outfile << "  li\t" << eqregister << ", ";
            Visit_val(binary.lhs, outfile);
            outfile << endl;
            leftreg = eqregister;
        }
    }
    else
    {
        assert(binary.lhs->kind.tag != KOOPA_RVT_INTEGER); // assert左节点不是int值
        //用 lw  stack 中对应的值 到当前寄存器
        string lhs_stack = kirinfo.find(outfile, binary.lhs);
        outfile << "  lw\t" + eqregister + ", " + lhs_stack << endl;

        leftreg = eqregister; // 获取左边的寄存器
    }
    if (binary.rhs->kind.tag == KOOPA_RVT_INTEGER)
    { //处理右节点,右节点为int值且为0
        if (binary.rhs->kind.data.integer.value == 0)
        { //右节点为0
            rightreg = "x0";
        }
        else
        {                                                   //右节点为非0 int, li到当前寄存器
            kirinfo.regMap[value] = kirinfo.register_num++; // 为当前指令再分配一个寄存器
            eqregister = get_reg_(value);                   // 定义当前指令的寄存器
            outfile << "  li\t" << eqregister << ", ";
            Visit_val(binary.rhs, outfile);
            outfile << endl;
            rightreg = eqregister;
        }
    }
    else
    {
        assert(binary.rhs->kind.tag != KOOPA_RVT_INTEGER); // assert左右节点不是int值
        kirinfo.regMap[value] = kirinfo.register_num++;    // 为当前指令再分配一个寄存器
        eqregister = get_reg_(value);

        //用 lw  stack 中对应的值 到当前寄存器
        string rhs_stack = kirinfo.find(outfile, binary.rhs);
        outfile << "  lw\t" + eqregister + ", " + rhs_stack << endl;

        rightreg = eqregister; // 获取右边的寄存器
    }
}

string get_reg_(const koopa_raw_value_t &value)
{

    if (kirinfo.regMap[value] > 6)
    {                                                      //当t0~t6用完时
        return "a" + to_string(kirinfo.regMap[value] - 7); // 用a0~a7
    }
    else
    {
        return "t" + to_string(kirinfo.regMap[value]); // 获得当前指令的寄存器
    }
}
