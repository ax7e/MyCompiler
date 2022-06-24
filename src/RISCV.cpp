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
    assert(ret == KOOPA_EC_SUCCESS);
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
    int stack_s = 0;
    int stack_r = 0;
    int stack_a = 0;

    for (size_t i = 0; i < bbsslice.len; ++i)
    {
        auto ptr = bbsslice.buffer[i];
        auto bblock = reinterpret_cast<koopa_raw_basic_block_t>(ptr);
        auto instr_slice = bblock->insts;
        for (size_t j = 0; j < instr_slice.len; j++)
        {
            const auto &value = reinterpret_cast<koopa_raw_value_t>(instr_slice.buffer[j]);
            if (value->ty->tag != KOOPA_RTT_UNIT)
            {

                kirinfo.stackMap[value] = stack_s;
                if (value->kind.tag == KOOPA_RVT_ALLOC)
                {

                    assert(value->ty->tag == KOOPA_RTT_POINTER);
                    auto kind = value->ty->data.pointer.base;
                    int arraysize = 1;
                    while (kind->tag == KOOPA_RTT_ARRAY)
                    {

                        int cursize = kind->data.array.len;
                        arraysize *= cursize;
                        kind = kind->data.array.base;
                    }
                    stack_s += 4 * arraysize;
                }
                else
                {

                    stack_s += 4;
                }
            }

            if (value->kind.tag == KOOPA_RVT_CALL)
            {

                stack_r = 4;

                if ((int(value->kind.data.call.args.len) - 8) > stack_a)
                {

                    stack_a = int(value->kind.data.call.args.len) - 8;
                }
            }
        }
    }
    stack_a *= 4;
    stack_size = stack_s + stack_r + stack_a;

    if ((stack_size % 16) != 0)
    {
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
    {
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
        kirinfo.has_call = true;
    }

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

        outfile << "  li\tt0, ";
        Visit_val(load.src, outfile);
        outfile << "\n";
        break;
    case KOOPA_RVT_ALLOC:

        loadstack = kirinfo.find(outfile, load.src);
        outfile << "  lw\tt0, " + loadstack << endl;
        break;
    case KOOPA_RVT_GLOBAL_ALLOC:

        global_name = load.src->name;

        global_name = global_name.substr(1);

        outfile << "  la\tt0, " + global_name << endl;

        outfile << "  lw\tt0, 0(t0)\n";
        break;
    case KOOPA_RVT_GET_ELEM_PTR:

        loadstack = kirinfo.find(outfile, load.src);
        outfile << "  lw\tt0, " + loadstack << endl;

        outfile << "  lw\tt0, 0(t0)\n";
        break;
    case KOOPA_RVT_GET_PTR:

        loadstack = kirinfo.find(outfile, load.src);
        outfile << "  lw\tt0, " + loadstack << endl;

        outfile << "  lw\tt0, 0(t0)\n";
        break;
    default:

        break;
    }

    string instr_stack = kirinfo.find(outfile, value);
    outfile << "  sw\tt0, " + instr_stack << endl;
    return;
}

void Visit_store(const koopa_raw_store_t &store, std::ostream &outfile)
{

    string store_value_reg = "t" + to_string(kirinfo.register_num++);

    if (store.value->kind.tag == KOOPA_RVT_FUNC_ARG_REF)
    {

        string dest_stack = kirinfo.find(outfile, store.dest);
        if (kirinfo.regMap[store.value] >= 8)
        {

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
    {
        outfile << "  li\t" + store_value_reg + ", ";
        Visit_val(store.value, outfile);
        outfile << "\n";
    }
    else
    {

        string loadstack = kirinfo.find(outfile, store.value);
        outfile << "  lw\t" + store_value_reg + ", " + loadstack << endl;
    }

    string global_name, dest_stack;
    string dest_reg = "t" + to_string(kirinfo.register_num++);
    switch (store.dest->kind.tag)
    {
    case KOOPA_RVT_GLOBAL_ALLOC:

        global_name = store.dest->name;

        global_name = global_name.substr(1);
        outfile << "  la\t" + dest_reg + ", " + global_name << endl;

        outfile << "  sw\t" + store_value_reg + ", 0(" + dest_reg + ")\n";
        break;
    case KOOPA_RVT_ALLOC:

        dest_stack = kirinfo.find(outfile, store.dest);
        outfile << "  sw\t" + store_value_reg + ", " + dest_stack << endl;
        break;
    case KOOPA_RVT_GET_ELEM_PTR:

        ++kirinfo.register_num;
        dest_stack = kirinfo.find(outfile, store.dest);
        outfile << "  lw\t" + dest_reg + ", " + dest_stack << endl;

        outfile << "  sw\t" + store_value_reg + ", 0(" + dest_reg + ")" << endl;
        break;
    case KOOPA_RVT_GET_PTR:

        ++kirinfo.register_num;
        dest_stack = kirinfo.find(outfile, store.dest);
        outfile << "  lw\t" + dest_reg + ", " + dest_stack << endl;

        outfile << "  sw\t" + store_value_reg + ", 0(" + dest_reg + ")" << endl;
        break;
    default:

        break;
    }

    return;
}

void Visit_jump(const koopa_raw_jump_t &jump, std::ostream &outfile)
{
    string jump_target = jump.target->name;

    outfile << "  j\t" + jump_target.substr(1) + "\n\n";
    return;
}

void Visit_branch(const koopa_raw_branch_t &branch, std::ostream &outfile)
{

    kirinfo.register_num++;
    if (branch.cond->kind.tag == KOOPA_RVT_INTEGER)
    {
        outfile << "  li\tt0, ";
        Visit_val(branch.cond, outfile);
        outfile << "\n";
    }
    else
    {

        string loadstack = kirinfo.find(outfile, branch.cond);
        outfile << "  lw\tt0, " + loadstack << endl;
    }

    string true_name = branch.true_bb->name;
    string false_name = branch.false_bb->name;

    outfile << "  bnez\tt0, " + true_name.substr(1) + "\n";
    outfile << "  j\t" + false_name.substr(1) + "\n\n";

    return;
}

void Visit_call(const koopa_raw_value_t &value, std::ostream &outfile)
{

    kirinfo.has_call = true;

    const auto &call = value->kind.data.call;

    for (int i = 0; i < (int)call.args.len; i++)
    {
        koopa_raw_value_t arg_value = reinterpret_cast<koopa_raw_value_t>(call.args.buffer[i]);
        kirinfo.register_num = 0;
        if (i < 8)
        {
            if (arg_value->kind.tag == KOOPA_RVT_INTEGER)
            {
                int num = arg_value->kind.data.integer.value;
                outfile << "  li\ta" + to_string(i) + ", " + to_string(num) + "\n";
            }
            else
            {
                string arg_stack = kirinfo.find(outfile, arg_value);
                outfile << "  lw\ta" + to_string(i) + ", " + arg_stack + "\n";
            }
        }
        else
        {
            string arg_reg = "t" + to_string(kirinfo.register_num++);
            if (arg_value->kind.tag == KOOPA_RVT_INTEGER)
            {
                int num = arg_value->kind.data.integer.value;
                outfile << "  li\t" + arg_reg + ", " + to_string(num) + "\n";
                outfile << "  sw\t" + arg_reg + ", " + to_string((i - 8) * 4) + "(sp)\n";
            }
            else
            {
                string arg_stack = kirinfo.find(outfile, arg_value);
                outfile << "  lw\t" + arg_reg + ", " + arg_stack + "\n";
                outfile << "  sw\t" + arg_reg + ", " + to_string((i - 8) * 4) + "(sp)\n";
            }
        }
    }
    string funcname = call.callee->name;

    funcname = funcname.substr(1);

    outfile << "  call\t" + funcname + "\n";

    if (value->ty->tag != KOOPA_RTT_UNIT)
    {
        string call_stack = kirinfo.find(outfile, value);
        outfile << "  sw\ta0, " + call_stack + "\n";
    }
}

void Visit_global_alloc(const koopa_raw_value_t &value, std::ostream &outfile)
{

    outfile << "  .data\n  .globl ";

    string global_name = value->name;

    global_name = global_name.substr(1);
    outfile << global_name << "\n"
            << global_name << ":\n";
    const auto &init = value->kind.data.global_alloc.init;

    if (init->kind.tag == KOOPA_RVT_ZERO_INIT)
    {
        if (value->ty->tag == KOOPA_RTT_POINTER)
        {
            int arraysize = 1;
            auto kind = value->ty->data.pointer.base;
            while (kind->tag == KOOPA_RTT_ARRAY)
            {

                int cursize = kind->data.array.len;
                arraysize *= cursize;
                kind = kind->data.array.base;
            }
            arraysize *= 4;

            outfile << "  .zero " + to_string(arraysize) + "\n";
        }
        else
        {
            outfile << "  .zero 4\n\n";
        }
    }
    else if (init->kind.tag == KOOPA_RVT_INTEGER)
    {
        int num = init->kind.data.integer.value;
        outfile << "  .word " + to_string(num) + "\n";
    }
    else if (init->kind.tag == KOOPA_RVT_AGGREGATE)
    {

        Visit_val(init, outfile);
    }
    else
    {
    }
    outfile << endl;
    return;
}

void visit_aggregate(const koopa_raw_value_t &aggregate, std::ostream &outfile)
{
    auto elems = aggregate->kind.data.aggregate.elems;
    for (size_t i = 0; i < elems.len; ++i)
    {
        auto ptr = elems.buffer[i];

        if (elems.kind == KOOPA_RSIK_VALUE)
        {
            auto elem = reinterpret_cast<koopa_raw_value_t>(ptr);
            if (elem->kind.tag == KOOPA_RVT_AGGREGATE)
            {
                visit_aggregate(elem, outfile);
            }
            else if (elem->kind.tag == KOOPA_RVT_INTEGER)
            {
                outfile << "  .word ";
                Visit_int(elem->kind.data.integer, outfile);
                outfile << endl;
            }
            else if (elem->kind.tag == KOOPA_RVT_ZERO_INIT)
            {
                auto kind = elem->ty->data.pointer.base;
                int arraysize = 1;
                while (kind->tag == KOOPA_RTT_ARRAY)
                {

                    int cursize = kind->data.array.len;
                    arraysize *= cursize;
                    kind = kind->data.array.base;
                }
                arraysize *= 4;
                outfile << "  .zero " + to_string(arraysize) << endl;
            }
            else
            {
            }
        }
        else
        {
        }
    }
}

void visit_getelemptr(const koopa_raw_value_t &getelemptr, std::ostream &outfile)
{
    auto src = getelemptr->kind.data.get_elem_ptr.src;
    auto index = getelemptr->kind.data.get_elem_ptr.index;

    auto kind = getelemptr->ty->data.pointer.base;
    int arraysize = 1;
    while (kind->tag == KOOPA_RTT_ARRAY)
    {

        int cursize = kind->data.array.len;
        arraysize *= cursize;
        kind = kind->data.array.base;
    }

    string src_reg = "t" + to_string(kirinfo.register_num++);

    if (src->kind.tag == KOOPA_RVT_GLOBAL_ALLOC)
    {

        string global_name = src->name;

        global_name = global_name.substr(1);
        outfile << "  la\t" + src_reg + ", " + global_name << endl;
    }
    else if (src->kind.tag == KOOPA_RVT_ALLOC)
    {
        int srcstack = kirinfo.find_value_in_stack_int(src);
        if (srcstack < 2047)
        {
            outfile << "  addi\t" + src_reg + ", sp, " + to_string(srcstack) << endl;
        }
        else
        {
            outfile << "  li\t" + src_reg + ", " + to_string(srcstack) << endl;
            outfile << "  add\t" + src_reg + ", sp, " + src_reg << endl;
        }
    }
    else if (src->kind.tag == KOOPA_RVT_GET_ELEM_PTR)
    {
        string srcstack = kirinfo.find(outfile, src);
        outfile << "  lw\t" + src_reg + ", " + srcstack << endl;
    }
    else if (src->kind.tag == KOOPA_RVT_GET_PTR)
    {
        string srcstack = kirinfo.find(outfile, src);
        outfile << "  lw\t" + src_reg + ", " + srcstack << endl;
    }
    else
    {
    }

    string indexreg = "t" + to_string(kirinfo.register_num++);
    if (index->kind.tag == KOOPA_RVT_INTEGER)
    {
        if (index->kind.data.integer.value == 0 && arraysize == 1)
        {
            string geteleptr_stack = kirinfo.find(outfile, getelemptr);
            outfile << "  sw\t" + src_reg + ", " + geteleptr_stack + "\n";
            return;
        }

        outfile << "  li\t" + indexreg + ", ";
        Visit_int(index->kind.data.integer, outfile);
    }
    else
    {
        string index_stack = kirinfo.find(outfile, index);
        outfile << "  lw\t" + indexreg + ", " + index_stack;
    }

    string size_reg = "t" + to_string(kirinfo.register_num++);
    arraysize *= 4;
    outfile << "\n  li\t" + size_reg + ", " + to_string(arraysize) + "\n";
    outfile << "  mul\t" + indexreg + ", " + indexreg + ", " + size_reg + "\n";

    outfile << "  add\t" + src_reg + ", " + src_reg + ", " + indexreg + "\n";

    string geteleptr_stack = kirinfo.find(outfile, getelemptr);
    outfile << "  sw\t" + src_reg + ", " + geteleptr_stack + "\n";
    return;
}

void visit_getptr(const koopa_raw_value_t &getptr, std::ostream &outfile)
{

    auto src = getptr->kind.data.get_ptr.src;
    auto index = getptr->kind.data.get_ptr.index;

    auto kind = src->ty->data.pointer.base;
    int arraysize = 1;
    while (kind->tag == KOOPA_RTT_ARRAY)
    {

        int cursize = kind->data.array.len;
        arraysize *= cursize;
        kind = kind->data.array.base;
    }

    string src_reg = "t" + to_string(kirinfo.register_num++);

    if (src->kind.tag == KOOPA_RVT_LOAD)
    {
        string srcstack = kirinfo.find(outfile, src);
        outfile << "  lw\t" + src_reg + ", " + srcstack << endl;
    }
    else
    {
    }

    string indexreg = "t" + to_string(kirinfo.register_num++);
    if (index->kind.tag == KOOPA_RVT_INTEGER)
    {
        if (index->kind.data.integer.value == 0 && arraysize == 1)
        {
            string getptr_stack = kirinfo.find(outfile, getptr);
            outfile << "  sw\t" + src_reg + ", " + getptr_stack + "\n";
            return;
        }

        outfile << "  li\t" + indexreg + ", ";
        Visit_int(index->kind.data.integer, outfile);
    }
    else
    {
        string index_stack = kirinfo.find(outfile, index);
        outfile << "  lw\t" + indexreg + ", " + index_stack;
    }

    string size_reg = "t" + to_string(kirinfo.register_num++);
    arraysize *= 4;
    outfile << "\n  li\t" + size_reg + ", " + to_string(arraysize) + "\n";
    outfile << "  mul\t" + indexreg + ", " + indexreg + ", " + size_reg + "\n";

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

        assert(false);
    }
    kirinfo.regMap.erase(kirinfo.regMap.begin(), kirinfo.regMap.end());
    return;
}

void Visit_bin_cond(const koopa_raw_value_t &value, ostream &outfile)
{
    const auto &binary = value->kind.data.binary;

    string leftreg, rightreg, eqregister;
    kirinfo.regMap[value] = kirinfo.register_num++;
    eqregister = get_reg_(value);

    handle_left_right_reg(value, outfile, leftreg, rightreg, eqregister);

    switch (binary.op)
    {
    case KOOPA_RBO_EQ:

        outfile << "  xor\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  seqz\t" + eqregister + ", " + eqregister + '\n';
        break;
    case KOOPA_RBO_NOT_EQ:

        outfile << "  xor\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  snez\t" + eqregister + ", " + eqregister + '\n';
        break;
    case KOOPA_RBO_OR:

        outfile << "  or\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  snez\t" + eqregister + ", " + eqregister + '\n';
        break;
    case KOOPA_RBO_GT:

        outfile << "  slt\t" + eqregister + ", " + rightreg + ", " + leftreg + '\n';
        break;
    case KOOPA_RBO_LT:

        outfile << "  slt\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        break;
    case KOOPA_RBO_GE:

        outfile << "  slt\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        outfile << "  xori\t" + eqregister + ", " + eqregister + ", 1\n";
        break;
    case KOOPA_RBO_LE:

        outfile << "  slt\t" + eqregister + ", " + rightreg + ", " + leftreg + '\n';
        outfile << "  xori\t" + eqregister + ", " + eqregister + ", 1\n";
        break;
    case KOOPA_RBO_AND:

        outfile << "  snez\t" + leftreg + ", " + leftreg + '\n';
        outfile << "  snez\t" + rightreg + ", " + rightreg + '\n';
        outfile << "  and\t" + eqregister + ", " + leftreg + ", " + rightreg + '\n';
        break;
    default:
        assert(false);
    }

    string instr_stack = kirinfo.find(outfile, value);
    outfile << "  sw\t" + eqregister + ", " + instr_stack << endl;

    return;
}

void Visit_bin_double_reg(const koopa_raw_value_t &value, ostream &outfile)
{

    const auto &binary = value->kind.data.binary;
    string bin_op;
    switch (binary.op)
    {
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
    default:
        assert(false);
    }
    string leftreg, rightreg;
    kirinfo.regMap[value] = kirinfo.register_num++;
    string eqregister = get_reg_(value);

    handle_left_right_reg(value, outfile, leftreg, rightreg, eqregister);

    outfile << "  " + bin_op + '\t' + eqregister + ", " + leftreg + ", " + rightreg + "\n";

    string instr_stack = kirinfo.find(outfile, value);
    outfile << "  sw\t" + eqregister + ", " + instr_stack << endl;
}

void handle_left_right_reg(const koopa_raw_value_t &value, ostream &outfile, string &leftreg, string &rightreg, string &eqregister)
{
    const auto &binary = value->kind.data.binary;
    if (binary.lhs->kind.tag == KOOPA_RVT_INTEGER)
    {
        if (binary.lhs->kind.data.integer.value == 0)
        {
            leftreg = "x0";
        }
        else
        {
            outfile << "  li\t" << eqregister << ", ";
            Visit_val(binary.lhs, outfile);
            outfile << endl;
            leftreg = eqregister;
        }
    }
    else
    {
        assert(binary.lhs->kind.tag != KOOPA_RVT_INTEGER);

        string lhs_stack = kirinfo.find(outfile, binary.lhs);
        outfile << "  lw\t" + eqregister + ", " + lhs_stack << endl;

        leftreg = eqregister;
    }
    if (binary.rhs->kind.tag == KOOPA_RVT_INTEGER)
    {
        if (binary.rhs->kind.data.integer.value == 0)
        {
            rightreg = "x0";
        }
        else
        {
            kirinfo.regMap[value] = kirinfo.register_num++;
            eqregister = get_reg_(value);
            outfile << "  li\t" << eqregister << ", ";
            Visit_val(binary.rhs, outfile);
            outfile << endl;
            rightreg = eqregister;
        }
    }
    else
    {
        assert(binary.rhs->kind.tag != KOOPA_RVT_INTEGER);
        kirinfo.regMap[value] = kirinfo.register_num++;
        eqregister = get_reg_(value);

        string rhs_stack = kirinfo.find(outfile, binary.rhs);
        outfile << "  lw\t" + eqregister + ", " + rhs_stack << endl;

        rightreg = eqregister;
    }
}

string get_reg_(const koopa_raw_value_t &value)
{

    if (kirinfo.regMap[value] > 6)
    {
        return "a" + to_string(kirinfo.regMap[value] - 7);
    }
    else
    {
        return "t" + to_string(kirinfo.regMap[value]);
    }
}
