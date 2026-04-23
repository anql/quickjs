/*
 * QuickJS opcode definitions
 * QuickJS 字节码操作码定义
 * 
 * 本文件定义了 QuickJS 虚拟机使用的所有字节码操作码。
 * 每个操作码由 DEF 宏定义，包含：
 * - 操作码名称
 * - 操作码长度（字节数）
 * - 弹出栈元素数量（n_pop）
 * - 压入栈元素数量（n_push）
 * - 操作数格式（f）
 * 
 * 字节码生成经过多个阶段：
 * - Phase 1: 生成包含临时操作码的初始字节码
 * - Phase 2: 移除作用域相关的临时操作码
 * - Phase 3: 移除标签等临时操作码，生成最终字节码
 *
 * Copyright (c) 2017-2018 Fabrice Bellard
 * Copyright (c) 2017-2018 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/* ==================== 操作数格式定义 ==================== */
/* FMT 宏用于定义操作数（operand）的格式类型 */

#ifdef FMT
/** none - 无操作数 */
FMT(none)
/** none_int - 无操作数（整数上下文） */
FMT(none_int)
/** none_loc - 无操作数（局部变量上下文） */
FMT(none_loc)
/** none_arg - 无操作数（参数上下文） */
FMT(none_arg)
/** none_var_ref - 无操作数（变量引用上下文） */
FMT(none_var_ref)
/** u8 - 8 位无符号整数 */
FMT(u8)
/** i8 - 8 位有符号整数 */
FMT(i8)
/** loc8 - 8 位局部变量索引 */
FMT(loc8)
/** const8 - 8 位常量索引 */
FMT(const8)
/** label8 - 8 位跳转标签 */
FMT(label8)
/** u16 - 16 位无符号整数 */
FMT(u16)
/** i16 - 16 位有符号整数 */
FMT(i16)
/** label16 - 16 位跳转标签 */
FMT(label16)
/** npop - 弹出参数数量 */
FMT(npop)
/** npopx - 弹出参数数量（可变） */
FMT(npopx)
/** npop_u16 - 16 位弹出参数数量 */
FMT(npop_u16)
/** loc - 局部变量索引 */
FMT(loc)
/** arg - 参数索引 */
FMT(arg)
/** var_ref - 变量引用 */
FMT(var_ref)
/** u32 - 32 位无符号整数 */
FMT(u32)
/** i32 - 32 位有符号整数 */
FMT(i32)
/** const - 常量池索引 */
FMT(const)
/** label - 跳转标签 */
FMT(label)
/** atom - 原子索引 */
FMT(atom)
/** atom_u8 - 原子索引 + 8 位整数 */
FMT(atom_u8)
/** atom_u16 - 原子索引 + 16 位整数 */
FMT(atom_u16)
/** atom_label_u8 - 原子索引 + 标签 + 8 位整数 */
FMT(atom_label_u8)
/** atom_label_u16 - 原子索引 + 标签 + 16 位整数 */
FMT(atom_label_u16)
/** label_u16 - 标签 + 16 位整数 */
FMT(label_u16)
#undef FMT
#endif /* FMT */

/* ==================== 操作码定义 ==================== */

#ifdef DEF

#ifndef def
#define def(id, size, n_pop, n_push, f) DEF(id, size, n_pop, n_push, f)
#endif

/**
 * invalid - 无效操作码
 * 字节数：1，弹出：0，压入：0
 * 永远不会被发射到最终字节码中，用于错误检测
 */
DEF(invalid, 1, 0, 0, none) /* never emitted */

/* ==================== 压栈操作 ==================== */

/**
 * push_i32 - 压入 32 位整数
 * 字节数：5，弹出：0，压入：1
 * 操作数：i32（32 位有符号整数）
 */
DEF(       push_i32, 5, 0, 1, i32)

/**
 * push_const - 压入常量
 * 字节数：5，弹出：0，压入：1
 * 操作数：const（常量池索引）
 */
DEF(     push_const, 5, 0, 1, const)

/**
 * fclosure - 压入闭包函数
 * 字节数：5，弹出：0，压入：1
 * 操作数：const（常量池索引）
 * 必须紧跟在 push_const 之后
 */
DEF(       fclosure, 5, 0, 1, const) /* must follow push_const */

/**
 * push_atom_value - 压入原子值
 * 字节数：5，弹出：0，压入：1
 * 操作数：atom（原子索引）
 */
DEF(push_atom_value, 5, 0, 1, atom)

/**
 * private_symbol - 压入私有 Symbol
 * 字节数：5，弹出：0，压入：1
 * 操作数：atom（原子索引）
 */
DEF( private_symbol, 5, 0, 1, atom)

/**
 * undefined - 压入 undefined
 * 字节数：1，弹出：0，压入：1
 */
DEF(      undefined, 1, 0, 1, none)

/**
 * null - 压入 null
 * 字节数：1，弹出：0，压入：1
 */
DEF(           null, 1, 0, 1, none)

/**
 * push_this - 压入 this
 * 字节数：1，弹出：0，压入：1
 * 仅在函数开始时使用
 */
DEF(      push_this, 1, 0, 1, none) /* only used at the start of a function */

/**
 * push_false - 压入 false
 * 字节数：1，弹出：0，压入：1
 */
DEF(     push_false, 1, 0, 1, none)

/**
 * push_true - 压入 true
 * 字节数：1，弹出：0，压入：1
 */
DEF(      push_true, 1, 0, 1, none)

/**
 * object - 创建空对象
 * 字节数：1，弹出：0，压入：1
 */
DEF(         object, 1, 0, 1, none)

/**
 * special_object - 创建特殊对象
 * 字节数：2，弹出：0，压入：1
 * 操作数：u8（对象类型）
 * 仅在函数开始时使用（如创建 arguments 对象）
 */
DEF( special_object, 2, 0, 1, u8) /* only used at the start of a function */

/**
 * rest - 创建剩余参数数组
 * 字节数：3，弹出：0，压入：1
 * 操作数：u16（起始参数索引）
 * 仅在函数开始时使用
 */
DEF(           rest, 3, 0, 1, u16) /* only used at the start of a function */

/* ==================== 栈操作 ==================== */

/**
 * drop - 丢弃栈顶元素
 * 字节数：1，弹出：1，压入：0
 * 栈：a -> (空)
 */
DEF(           drop, 1, 1, 0, none) /* a -> */

/**
 * nip - 移除次栈顶元素
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> b
 */
DEF(            nip, 1, 2, 1, none) /* a b -> b */

/**
 * nip1 - 移除第三个栈元素
 * 字节数：1，弹出：3，压入：2
 * 栈：a b c -> b c
 */
DEF(           nip1, 1, 3, 2, none) /* a b c -> b c */

/**
 * dup - 复制栈顶元素
 * 字节数：1，弹出：1，压入：2
 * 栈：a -> a a
 */
DEF(            dup, 1, 1, 2, none) /* a -> a a */

/**
 * dup1 - 复制次栈顶元素
 * 字节数：1，弹出：2，压入：3
 * 栈：a b -> a a b
 */
DEF(           dup1, 1, 2, 3, none) /* a b -> a a b */

/**
 * dup2 - 复制栈顶两个元素
 * 字节数：1，弹出：2，压入：4
 * 栈：a b -> a b a b
 */
DEF(           dup2, 1, 2, 4, none) /* a b -> a b a b */

/**
 * dup3 - 复制栈顶三个元素
 * 字节数：1，弹出：3，压入：6
 * 栈：a b c -> a b c a b c
 */
DEF(           dup3, 1, 3, 6, none) /* a b c -> a b c a b c */

/**
 * insert2 - 插入栈顶元素到位置 2
 * 字节数：1，弹出：2，压入：3
 * 栈：obj a -> a obj a（也称为 dup_x1）
 */
DEF(        insert2, 1, 2, 3, none) /* obj a -> a obj a (dup_x1) */

/**
 * insert3 - 插入栈顶元素到位置 3
 * 字节数：1，弹出：3，压入：4
 * 栈：obj prop a -> a obj prop a（也称为 dup_x2）
 */
DEF(        insert3, 1, 3, 4, none) /* obj prop a -> a obj prop a (dup_x2) */

/**
 * insert4 - 插入栈顶元素到位置 4
 * 字节数：1，弹出：4，压入：5
 * 栈：this obj prop a -> a this obj prop a
 */
DEF(        insert4, 1, 4, 5, none) /* this obj prop a -> a this obj prop a */

/**
 * perm3 - 置换三个元素
 * 字节数：1，弹出：3，压入：3
 * 栈：obj a b -> a obj b
 */
DEF(          perm3, 1, 3, 3, none) /* obj a b -> a obj b */

/**
 * perm4 - 置换四个元素
 * 字节数：1，弹出：4，压入：4
 * 栈：obj prop a b -> a obj prop b
 */
DEF(          perm4, 1, 4, 4, none) /* obj prop a b -> a obj prop b */

/**
 * perm5 - 置换五个元素
 * 字节数：1，弹出：5，压入：5
 * 栈：this obj prop a b -> a this obj prop b
 */
DEF(          perm5, 1, 5, 5, none) /* this obj prop a b -> a this obj prop b */

/**
 * swap - 交换栈顶两个元素
 * 字节数：1，弹出：2，压入：2
 * 栈：a b -> b a
 */
DEF(           swap, 1, 2, 2, none) /* a b -> b a */

/**
 * swap2 - 交换栈顶四个元素为两组
 * 字节数：1，弹出：4，压入：4
 * 栈：a b c d -> c d a b
 */
DEF(          swap2, 1, 4, 4, none) /* a b c d -> c d a b */

/**
 * rot3l - 左旋转三个元素
 * 字节数：1，弹出：3，压入：3
 * 栈：x a b -> a b x
 */
DEF(          rot3l, 1, 3, 3, none) /* x a b -> a b x */

/**
 * rot3r - 右旋转三个元素
 * 字节数：1，弹出：3，压入：3
 * 栈：a b x -> x a b
 */
DEF(          rot3r, 1, 3, 3, none) /* a b x -> x a b */

/**
 * rot4l - 左旋转四个元素
 * 字节数：1，弹出：4，压入：4
 * 栈：x a b c -> a b c x
 */
DEF(          rot4l, 1, 4, 4, none) /* x a b c -> a b c x */

/**
 * rot5l - 左旋转五个元素
 * 字节数：1，弹出：5，压入：5
 * 栈：x a b c d -> a b c d x
 */
DEF(          rot5l, 1, 5, 5, none) /* x a b c d -> a b c d x */

/* ==================== 函数调用操作 ==================== */

/**
 * call_constructor - 调用构造函数（new）
 * 字节数：3，弹出：2，压入：1
 * 操作数：npop（参数数量）
 * 栈：func new.target args -> ret
 * 参数不计入 n_pop
 */
DEF(call_constructor, 3, 2, 1, npop) /* func new.target args -> ret. arguments are not counted in n_pop */

/**
 * call - 函数调用
 * 字节数：3，弹出：1，压入：1
 * 操作数：npop（参数数量）
 * 参数不计入 n_pop
 */
DEF(           call, 3, 1, 1, npop) /* arguments are not counted in n_pop */

/**
 * tail_call - 尾调用优化
 * 字节数：3，弹出：1，压入：0
 * 操作数：npop（参数数量）
 * 参数不计入 n_pop
 */
DEF(      tail_call, 3, 1, 0, npop) /* arguments are not counted in n_pop */

/**
 * call_method - 方法调用
 * 字节数：3，弹出：2，压入：1
 * 操作数：npop（参数数量）
 * 栈：this func args -> ret
 * 参数不计入 n_pop
 */
DEF(    call_method, 3, 2, 1, npop) /* arguments are not counted in n_pop */

/**
 * tail_call_method - 尾调用方法优化
 * 字节数：3，弹出：2，压入：0
 * 操作数：npop（参数数量）
 */
DEF(tail_call_method, 3, 2, 0, npop) /* arguments are not counted in n_pop */

/**
 * array_from - 从参数创建数组
 * 字节数：3，弹出：0，压入：1
 * 操作数：npop（参数数量）
 */
DEF(     array_from, 3, 0, 1, npop) /* arguments are not counted in n_pop */

/**
 * apply - Function.prototype.apply
 * 字节数：3，弹出：3，压入：1
 * 操作数：u16（参数数量）
 * 栈：this func argsArray -> ret
 */
DEF(          apply, 3, 3, 1, u16)

/**
 * return - 函数返回
 * 字节数：1，弹出：1，压入：0
 * 栈：value -> (返回)
 */
DEF(         return, 1, 1, 0, none)

/**
 * return_undef - 返回 undefined
 * 字节数：1，弹出：0，压入：0
 */
DEF(   return_undef, 1, 0, 0, none)

/**
 * check_ctor_return - 检查构造函数返回值
 * 字节数：1，弹出：1，压入：2
 */
DEF(check_ctor_return, 1, 1, 2, none)

/**
 * check_ctor - 检查构造函数调用
 * 字节数：1，弹出：0，压入：0
 */
DEF(     check_ctor, 1, 0, 0, none)

/**
 * init_ctor - 初始化构造函数
 * 字节数：1，弹出：0，压入：1
 */
DEF(      init_ctor, 1, 0, 1, none)

/**
 * check_brand - 检查类品牌（private 字段访问）
 * 字节数：1，弹出：2，压入：2
 * 栈：this_obj func -> this_obj func
 */
DEF(    check_brand, 1, 2, 2, none) /* this_obj func -> this_obj func */

/**
 * add_brand - 添加类品牌
 * 字节数：1，弹出：2，压入：0
 * 栈：this_obj home_obj ->
 */
DEF(      add_brand, 1, 2, 0, none) /* this_obj home_obj -> */

/**
 * return_async - 异步函数返回
 * 字节数：1，弹出：1，压入：0
 */
DEF(   return_async, 1, 1, 0, none)

/**
 * throw - 抛出异常
 * 字节数：1，弹出：1，压入：0
 * 栈：error -> (抛出)
 */
DEF(          throw, 1, 1, 0, none)

/**
 * throw_error - 抛出错误
 * 字节数：6，弹出：0，压入：0
 * 操作数：atom_u8（错误类型 + 原子索引）
 */
DEF(    throw_error, 6, 0, 0, atom_u8)

/**
 * eval - eval 调用
 * 字节数：5，弹出：1，压入：1
 * 操作数：npop_u16（参数数量）
 * 栈：func args... -> ret_val
 */
DEF(           eval, 5, 1, 1, npop_u16) /* func args... -> ret_val */

/**
 * apply_eval - 应用 eval
 * 字节数：3，弹出：2，压入：1
 * 操作数：u16（参数数量）
 * 栈：func array -> ret_eval
 */
DEF(     apply_eval, 3, 2, 1, u16) /* func array -> ret_eval */

/**
 * regexp - 创建正则表达式对象
 * 字节数：1，弹出：2，压入：1
 * 栈：pattern bytecode -> RegExp 对象
 */
DEF(         regexp, 1, 2, 1, none) /* create a RegExp object from the pattern and a
                                       bytecode string */

/**
 * get_super - 获取 super 属性
 * 字节数：1，弹出：1，压入：1
 * 栈：this -> super
 */
DEF(      get_super, 1, 1, 1, none)

/**
 * import - 动态模块导入
 * 字节数：1，弹出：2，压入：1
 * 栈：module specifier -> import promise
 */
DEF(         import, 1, 2, 1, none) /* dynamic module import */

/* ==================== 变量访问操作 ==================== */

/**
 * get_var_undef - 获取变量（不存在则返回 undefined）
 * 字节数：3，弹出：0，压入：1
 * 操作数：var_ref（变量引用）
 */
DEF(  get_var_undef, 3, 0, 1, var_ref) /* push undefined if the variable does not exist */

/**
 * get_var - 获取变量（不存在则抛出异常）
 * 字节数：3，弹出：0，压入：1
 * 操作数：var_ref（变量引用）
 */
DEF(        get_var, 3, 0, 1, var_ref) /* throw an exception if the variable does not exist */

/**
 * put_var - 设置变量
 * 字节数：3，弹出：1，压入：0
 * 操作数：var_ref（变量引用）
 * 必须紧跟在 get_var 之后
 */
DEF(        put_var, 3, 1, 0, var_ref) /* must come after get_var */

/**
 * put_var_init - 初始化全局词法变量
 * 字节数：3，弹出：1，压入：0
 * 操作数：var_ref（变量引用）
 * 必须紧跟在 put_var 之后
 */
DEF(   put_var_init, 3, 1, 0, var_ref) /* must come after put_var. Used to initialize a global lexical variable */

/**
 * get_ref_value - 获取引用值
 * 字节数：1，弹出：2，压入：3
 * 栈：base prop -> base prop value
 */
DEF(  get_ref_value, 1, 2, 3, none)

/**
 * put_ref_value - 设置引用值
 * 字节数：1，弹出：3，压入：0
 * 栈：base prop value ->
 */
DEF(  put_ref_value, 1, 3, 0, none)

/* ==================== 属性访问操作 ==================== */

/**
 * get_field - 获取对象字段
 * 字节数：5，弹出：1，压入：1
 * 操作数：atom（属性名原子索引）
 * 栈：obj -> obj[prop]
 */
DEF(      get_field, 5, 1, 1, atom)

/**
 * get_field2 - 获取对象字段（保留对象）
 * 字节数：5，弹出：1，压入：2
 * 操作数：atom（属性名原子索引）
 * 栈：obj -> obj obj[prop]
 */
DEF(     get_field2, 5, 1, 2, atom)

/**
 * put_field - 设置对象字段
 * 字节数：5，弹出：2，压入：0
 * 操作数：atom（属性名原子索引）
 * 栈：obj value ->
 */
DEF(      put_field, 5, 2, 0, atom)

/**
 * get_private_field - 获取私有字段
 * 字节数：1，弹出：2，压入：1
 * 栈：obj prop -> value
 */
DEF( get_private_field, 1, 2, 1, none) /* obj prop -> value */

/**
 * put_private_field - 设置私有字段
 * 字节数：1，弹出：3，压入：0
 * 栈：obj value prop ->
 */
DEF( put_private_field, 1, 3, 0, none) /* obj value prop -> */

/**
 * define_private_field - 定义私有字段
 * 字节数：1，弹出：3，压入：1
 * 栈：obj prop value -> obj
 */
DEF(define_private_field, 1, 3, 1, none) /* obj prop value -> obj */

/**
 * get_array_el - 获取数组元素
 * 字节数：1，弹出：2，压入：1
 * 栈：obj index -> obj[index]
 */
DEF(   get_array_el, 1, 2, 1, none)

/**
 * get_array_el2 - 获取数组元素（保留对象）
 * 字节数：1，弹出：2，压入：2
 * 栈：obj prop -> obj value
 */
DEF(  get_array_el2, 1, 2, 2, none) /* obj prop -> obj value */

/**
 * get_array_el3 - 获取数组元素（保留对象和属性）
 * 字节数：1，弹出：2，压入：3
 * 栈：obj prop -> obj prop value
 */
DEF(  get_array_el3, 1, 2, 3, none) /* obj prop -> obj prop1 value */

/**
 * put_array_el - 设置数组元素
 * 字节数：1，弹出：3，压入：0
 * 栈：obj index value ->
 */
DEF(   put_array_el, 1, 3, 0, none)

/**
 * get_super_value - 获取 super 值
 * 字节数：1，弹出：3，压入：1
 * 栈：this obj prop -> value
 */
DEF(get_super_value, 1, 3, 1, none) /* this obj prop -> value */

/**
 * put_super_value - 设置 super 值
 * 字节数：1，弹出：4，压入：0
 * 栈：this obj prop value ->
 */
DEF(put_super_value, 1, 4, 0, none) /* this obj prop value -> */

/**
 * define_field - 定义字段
 * 字节数：5，弹出：2，压入：1
 * 操作数：atom（属性名原子索引）
 * 栈：obj value -> obj
 */
DEF(   define_field, 5, 2, 1, atom)

/**
 * set_name - 设置名称
 * 字节数：5，弹出：1，压入：1
 * 操作数：atom（名称原子索引）
 * 栈：func -> func
 */
DEF(       set_name, 5, 1, 1, atom)

/**
 * set_name_computed - 设置计算名称
 * 字节数：1，弹出：2，压入：2
 * 栈：func name -> func name
 */
DEF(set_name_computed, 1, 2, 2, none)

/**
 * set_proto - 设置原型
 * 字节数：1，弹出：2，压入：1
 * 栈：obj proto -> obj
 */
DEF(      set_proto, 1, 2, 1, none)

/**
 * set_home_object - 设置家对象（super 绑定）
 * 字节数：1，弹出：2，压入：2
 * 栈：method home_obj -> method home_obj
 */
DEF(set_home_object, 1, 2, 2, none)

/**
 * define_array_el - 定义数组元素
 * 字节数：1，弹出：3，压入：2
 * 栈：obj index value -> obj value
 */
DEF(define_array_el, 1, 3, 2, none)

/**
 * append - 追加元素到数组
 * 字节数：1，弹出：3，压入：2
 * 栈：obj index value -> obj newLength
 */
DEF(         append, 1, 3, 2, none) /* append enumerated object, update length */

/**
 * copy_data_properties - 复制数据属性
 * 字节数：2，弹出：3，压入：3
 * 操作数：u8（标志）
 * 栈：target source excluded -> target source excluded
 */
DEF(copy_data_properties, 2, 3, 3, u8)

/**
 * define_method - 定义方法
 * 字节数：6，弹出：2，压入：1
 * 操作数：atom_u8（方法名 + 标志）
 * 栈：obj func -> obj
 */
DEF(  define_method, 6, 2, 1, atom_u8)

/**
 * define_method_computed - 定义计算属性方法
 * 字节数：2，弹出：3，压入：1
 * 操作数：u8（标志）
 * 必须紧跟在 define_method 之后
 */
DEF(define_method_computed, 2, 3, 1, u8) /* must come after define_method */

/**
 * define_class - 定义类
 * 字节数：6，弹出：2，压入：2
 * 操作数：atom_u8（类名 + 标志）
 * 栈：parent ctor -> ctor proto
 */
DEF(   define_class, 6, 2, 2, atom_u8) /* parent ctor -> ctor proto */

/**
 * define_class_computed - 定义计算属性名类
 * 字节数：6，弹出：3，压入：3
 * 操作数：atom_u8（类名 + 标志）
 * 栈：field_name parent ctor -> field_name ctor proto
 */
DEF(   define_class_computed, 6, 3, 3, atom_u8) /* field_name parent ctor -> field_name ctor proto (class with computed name) */

/* ==================== 局部变量操作 ==================== */

/**
 * get_loc - 获取局部变量
 * 字节数：3，弹出：0，压入：1
 * 操作数：loc（局部变量索引）
 */
DEF(        get_loc, 3, 0, 1, loc)

/**
 * put_loc - 设置局部变量
 * 字节数：3，弹出：1，压入：0
 * 操作数：loc（局部变量索引）
 * 必须紧跟在 get_loc 之后
 */
DEF(        put_loc, 3, 1, 0, loc) /* must come after get_loc */

/**
 * set_loc - 设置局部变量并保留值
 * 字节数：3，弹出：1，压入：1
 * 操作数：loc（局部变量索引）
 * 必须紧跟在 put_loc 之后
 */
DEF(        set_loc, 3, 1, 1, loc) /* must come after put_loc */

/**
 * get_arg - 获取参数
 * 字节数：3，弹出：0，压入：1
 * 操作数：arg（参数索引）
 */
DEF(        get_arg, 3, 0, 1, arg)

/**
 * put_arg - 设置参数
 * 字节数：3，弹出：1，压入：0
 * 操作数：arg（参数索引）
 * 必须紧跟在 get_arg 之后
 */
DEF(        put_arg, 3, 1, 0, arg) /* must come after get_arg */

/**
 * set_arg - 设置参数并保留值
 * 字节数：3，弹出：1，压入：1
 * 操作数：arg（参数索引）
 * 必须紧跟在 put_arg 之后
 */
DEF(        set_arg, 3, 1, 1, arg) /* must come after put_arg */

/**
 * get_var_ref - 获取变量引用
 * 字节数：3，弹出：0，压入：1
 * 操作数：var_ref（变量引用）
 */
DEF(    get_var_ref, 3, 0, 1, var_ref)

/**
 * put_var_ref - 设置变量引用
 * 字节数：3，弹出：1，压入：0
 * 操作数：var_ref（变量引用）
 * 必须紧跟在 get_var_ref 之后
 */
DEF(    put_var_ref, 3, 1, 0, var_ref) /* must come after get_var_ref */

/**
 * set_var_ref - 设置变量引用并保留值
 * 字节数：3，弹出：1，压入：1
 * 操作数：var_ref（变量引用）
 * 必须紧跟在 put_var_ref 之后
 */
DEF(    set_var_ref, 3, 1, 1, var_ref) /* must come after put_var_ref */

/**
 * set_loc_uninitialized - 设置局部变量为未初始化
 * 字节数：3，弹出：0，压入：0
 * 操作数：loc（局部变量索引）
 */
DEF(set_loc_uninitialized, 3, 0, 0, loc)

/**
 * get_loc_check - 获取局部变量（检查 TDZ）
 * 字节数：3，弹出：0，压入：1
 * 操作数：loc（局部变量索引）
 */
DEF(  get_loc_check, 3, 0, 1, loc)

/**
 * put_loc_check - 设置局部变量（检查 TDZ）
 * 字节数：3，弹出：1，压入：0
 * 操作数：loc（局部变量索引）
 * 必须紧跟在 get_loc_check 之后
 */
DEF(  put_loc_check, 3, 1, 0, loc) /* must come after get_loc_check */

/**
 * set_loc_check - 设置局部变量并保留值（检查 TDZ）
 * 字节数：3，弹出：1，压入：1
 * 操作数：loc（局部变量索引）
 * 必须紧跟在 put_loc_check 之后
 */
DEF(  set_loc_check, 3, 1, 1, loc) /* must come after put_loc_check */

/**
 * put_loc_check_init - 设置局部变量（初始化检查）
 * 字节数：3，弹出：1，压入：0
 * 操作数：loc（局部变量索引）
 */
DEF(  put_loc_check_init, 3, 1, 0, loc)

/**
 * get_loc_checkthis - 获取局部变量（检查 this）
 * 字节数：3，弹出：0，压入：1
 * 操作数：loc（局部变量索引）
 */
DEF(get_loc_checkthis, 3, 0, 1, loc)

/**
 * get_var_ref_check - 获取变量引用（检查 TDZ）
 * 字节数：3，弹出：0，压入：1
 * 操作数：var_ref（变量引用）
 */
DEF(get_var_ref_check, 3, 0, 1, var_ref)

/**
 * put_var_ref_check - 设置变量引用（检查 TDZ）
 * 字节数：3，弹出：1，压入：0
 * 操作数：var_ref（变量引用）
 * 必须紧跟在 get_var_ref_check 之后
 */
DEF(put_var_ref_check, 3, 1, 0, var_ref) /* must come after get_var_ref_check */

/**
 * put_var_ref_check_init - 设置变量引用（初始化检查）
 * 字节数：3，弹出：1，压入：0
 * 操作数：var_ref（变量引用）
 */
DEF(put_var_ref_check_init, 3, 1, 0, var_ref)

/**
 * close_loc - 关闭局部变量（用于闭包）
 * 字节数：3，弹出：0，压入：0
 * 操作数：loc（局部变量索引）
 */
DEF(      close_loc, 3, 0, 0, loc)

/* ==================== 控制流操作 ==================== */

/**
 * if_false - 条件跳转（假则跳）
 * 字节数：5，弹出：1，压入：0
 * 操作数：label（跳转目标）
 * 栈：cond -> (空)
 */
DEF(       if_false, 5, 1, 0, label)

/**
 * if_true - 条件跳转（真则跳）
 * 字节数：5，弹出：1，压入：0
 * 操作数：label（跳转目标）
 * 必须紧跟在 if_false 之后
 */
DEF(        if_true, 5, 1, 0, label) /* must come after if_false */

/**
 * goto - 无条件跳转
 * 字节数：5，弹出：0，压入：0
 * 操作数：label（跳转目标）
 * 必须紧跟在 if_true 之后
 */
DEF(           goto, 5, 0, 0, label) /* must come after if_true */

/**
 * catch - 异常捕获点
 * 字节数：5，弹出：0，压入：1
 * 操作数：label（异常处理程序）
 * 栈：-> error
 */
DEF(          catch, 5, 0, 1, label)

/**
 * gosub - 调用子程序（用于 finally 块）
 * 字节数：5，弹出：0，压入：0
 * 操作数：label（子程序入口）
 */
DEF(          gosub, 5, 0, 0, label) /* used to execute the finally block */

/**
 * ret - 从子程序返回
 * 字节数：1，弹出：1，压入：0
 * 用于从 finally 块返回
 */
DEF(            ret, 1, 1, 0, none) /* used to return from the finally block */

/**
 * nip_catch - 移除捕获的异常
 * 字节数：1，弹出：2，压入：1
 * 栈：catch ... a -> a
 */
DEF(      nip_catch, 1, 2, 1, none) /* catch ... a -> a */

/* ==================== 类型转换操作 ==================== */

/**
 * to_object - 转换为对象
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> Object(value)
 */
DEF(      to_object, 1, 1, 1, none)

/**
 * to_propkey - 转换为属性键
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> property key
 */
DEF(     to_propkey, 1, 1, 1, none)

/* ==================== with 语句操作 ==================== */

/**
 * with_get_var - with 作用域获取变量
 * 字节数：10，弹出：1，压入：0
 * 操作数：atom_label_u8（属性名 + 跳转标签 + 标志）
 */
DEF(   with_get_var, 10, 1, 0, atom_label_u8)     /* must be in the same order as scope_xxx */

/**
 * with_put_var - with 作用域设置变量
 * 字节数：10，弹出：2，压入：1
 * 操作数：atom_label_u8（属性名 + 跳转标签 + 标志）
 */
DEF(   with_put_var, 10, 2, 1, atom_label_u8)     /* must be in the same order as scope_xxx */

/**
 * with_delete_var - with 作用域删除变量
 * 字节数：10，弹出：1，压入：0
 * 操作数：atom_label_u8（属性名 + 跳转标签 + 标志）
 */
DEF(with_delete_var, 10, 1, 0, atom_label_u8)     /* must be in the same order as scope_xxx */

/**
 * with_make_ref - with 作用域创建引用
 * 字节数：10，弹出：1，压入：0
 * 操作数：atom_label_u8（属性名 + 跳转标签 + 标志）
 */
DEF(  with_make_ref, 10, 1, 0, atom_label_u8)     /* must be in the same order as scope_xxx */

/**
 * with_get_ref - with 作用域获取引用
 * 字节数：10，弹出：1，压入：0
 * 操作数：atom_label_u8（属性名 + 跳转标签 + 标志）
 */
DEF(   with_get_ref, 10, 1, 0, atom_label_u8)     /* must be in the same order as scope_xxx */

/* ==================== 引用创建操作 ==================== */

/**
 * make_loc_ref - 创建局部变量引用
 * 字节数：7，弹出：0，压入：2
 * 操作数：atom_u16（名称 + 局部变量索引）
 */
DEF(   make_loc_ref, 7, 0, 2, atom_u16)

/**
 * make_arg_ref - 创建参数引用
 * 字节数：7，弹出：0，压入：2
 * 操作数：atom_u16（名称 + 参数索引）
 */
DEF(   make_arg_ref, 7, 0, 2, atom_u16)

/**
 * make_var_ref_ref - 创建变量引用引用
 * 字节数：7，弹出：0，压入：2
 * 操作数：atom_u16（名称 + 变量引用）
 */
DEF(make_var_ref_ref, 7, 0, 2, atom_u16)

/**
 * make_var_ref - 创建变量引用
 * 字节数：5，弹出：0，压入：2
 * 操作数：atom（名称原子索引）
 */
DEF(   make_var_ref, 5, 0, 2, atom)

/* ==================== 迭代器操作 ==================== */

/**
 * for_in_start - for-in 循环初始化
 * 字节数：1，弹出：1，压入：1
 * 栈：obj -> iter
 */
DEF(   for_in_start, 1, 1, 1, none)

/**
 * for_of_start - for-of 循环初始化
 * 字节数：1，弹出：1，压入：3
 * 栈：iterable -> iter nextMethod done
 */
DEF(   for_of_start, 1, 1, 3, none)

/**
 * for_await_of_start - for-await-of 循环初始化
 * 字节数：1，弹出：1，压入：3
 */
DEF(for_await_of_start, 1, 1, 3, none)

/**
 * for_in_next - for-in 循环下一步
 * 字节数：1，弹出：1，压入：3
 * 栈：iter -> iter key done
 */
DEF(    for_in_next, 1, 1, 3, none)

/**
 * for_of_next - for-of 循环下一步
 * 字节数：2，弹出：3，压入：5
 * 操作数：u8（标志）
 */
DEF(    for_of_next, 2, 3, 5, u8)

/**
 * for_await_of_next - for-await-of 循环下一步
 * 字节数：1，弹出：3，压入：4
 * 栈：iter next catch_offset -> iter next catch_offset obj
 */
DEF(for_await_of_next, 1, 3, 4, none) /* iter next catch_offset -> iter next catch_offset obj */

/**
 * iterator_check_object - 检查迭代器是否为对象
 * 字节数：1，弹出：1，压入：1
 */
DEF(iterator_check_object, 1, 1, 1, none)

/**
 * iterator_get_value_done - 获取迭代器的值和完成标志
 * 字节数：1，弹出：2，压入：3
 * 栈：catch_offset obj -> catch_offset value done
 */
DEF(iterator_get_value_done, 1, 2, 3, none) /* catch_offset obj -> catch_offset value done */

/**
 * iterator_close - 关闭迭代器
 * 字节数：1，弹出：3，压入：0
 */
DEF( iterator_close, 1, 3, 0, none)

/**
 * iterator_next - 迭代器下一步
 * 字节数：1，弹出：4，压入：4
 */
DEF(  iterator_next, 1, 4, 4, none)

/**
 * iterator_call - 调用迭代器方法
 * 字节数：2，弹出：4，压入：5
 * 操作数：u8（方法索引）
 */
DEF(  iterator_call, 2, 4, 5, u8)

/* ==================== 生成器操作 ==================== */

/**
 * initial_yield - 初始 yield
 * 字节数：1，弹出：0，压入：0
 */
DEF(  initial_yield, 1, 0, 0, none)

/**
 * yield - yield 表达式
 * 字节数：1，弹出：1，压入：2
 * 栈：value -> value resumeValue
 */
DEF(          yield, 1, 1, 2, none)

/**
 * yield_star - yield* 表达式
 * 字节数：1，弹出：1，压入：2
 */
DEF(     yield_star, 1, 1, 2, none)

/**
 * async_yield_star - 异步 yield* 表达式
 * 字节数：1，弹出：1，压入：2
 */
DEF(async_yield_star, 1, 1, 2, none)

/**
 * await - await 表达式
 * 字节数：1，弹出：1，压入：1
 * 栈：promise -> value
 */
DEF(          await, 1, 1, 1, none)

/* ==================== 算术/逻辑运算操作 ==================== */

/**
 * neg - 一元负号
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> -value
 */
DEF(            neg, 1, 1, 1, none)

/**
 * plus - 一元正号
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> +value
 */
DEF(           plus, 1, 1, 1, none)

/**
 * dec - 前置递减
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> value-1
 */
DEF(            dec, 1, 1, 1, none)

/**
 * inc - 前置递增
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> value+1
 */
DEF(            inc, 1, 1, 1, none)

/**
 * post_dec - 后置递减
 * 字节数：1，弹出：1，压入：2
 * 栈：value -> value value-1
 */
DEF(       post_dec, 1, 1, 2, none)

/**
 * post_inc - 后置递增
 * 字节数：1，弹出：1，压入：2
 * 栈：value -> value value+1
 */
DEF(       post_inc, 1, 1, 2, none)

/**
 * dec_loc - 递减局部变量
 * 字节数：2，弹出：0，压入：0
 * 操作数：loc8（8 位局部变量索引）
 */
DEF(        dec_loc, 2, 0, 0, loc8)

/**
 * inc_loc - 递增局部变量
 * 字节数：2，弹出：0，压入：0
 * 操作数：loc8（8 位局部变量索引）
 */
DEF(        inc_loc, 2, 0, 0, loc8)

/**
 * add_loc - 加到局部变量
 * 字节数：2，弹出：1，压入：0
 * 操作数：loc8（8 位局部变量索引）
 * 栈：value -> (loc += value)
 */
DEF(        add_loc, 2, 1, 0, loc8)

/**
 * not - 按位取反
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> ~value
 */
DEF(            not, 1, 1, 1, none)

/**
 * lnot - 逻辑非
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> !value
 */
DEF(           lnot, 1, 1, 1, none)

/**
 * typeof - typeof 操作符
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> typeof value
 */
DEF(         typeof, 1, 1, 1, none)

/**
 * delete - delete 操作符
 * 字节数：1，弹出：2，压入：1
 * 栈：obj prop -> deleted
 */
DEF(         delete, 1, 2, 1, none)

/**
 * delete_var - 删除变量
 * 字节数：5，弹出：0，压入：1
 * 操作数：atom（变量名原子索引）
 */
DEF(     delete_var, 5, 0, 1, atom)

/**
 * mul - 乘法
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a * b
 */
DEF(            mul, 1, 2, 1, none)

/**
 * div - 除法
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a / b
 */
DEF(            div, 1, 2, 1, none)

/**
 * mod - 取模
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a % b
 */
DEF(            mod, 1, 2, 1, none)

/**
 * add - 加法
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a + b
 */
DEF(            add, 1, 2, 1, none)

/**
 * sub - 减法
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a - b
 */
DEF(            sub, 1, 2, 1, none)

/**
 * pow - 幂运算
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a ** b
 */
DEF(            pow, 1, 2, 1, none)

/**
 * shl - 左移
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a << b
 */
DEF(            shl, 1, 2, 1, none)

/**
 * sar - 算术右移
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a >> b
 */
DEF(            sar, 1, 2, 1, none)

/**
 * shr - 逻辑右移
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a >>> b
 */
DEF(            shr, 1, 2, 1, none)

/**
 * lt - 小于
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a < b
 */
DEF(             lt, 1, 2, 1, none)

/**
 * lte - 小于等于
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a <= b
 */
DEF(            lte, 1, 2, 1, none)

/**
 * gt - 大于
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a > b
 */
DEF(             gt, 1, 2, 1, none)

/**
 * gte - 大于等于
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a >= b
 */
DEF(            gte, 1, 2, 1, none)

/**
 * instanceof - instanceof 操作符
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a instanceof b
 */
DEF(     instanceof, 1, 2, 1, none)

/**
 * in - in 操作符
 * 字节数：1，弹出：2，压入：1
 * 栈：prop obj -> prop in obj
 */
DEF(             in, 1, 2, 1, none)

/**
 * eq - 相等（==）
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a == b
 */
DEF(             eq, 1, 2, 1, none)

/**
 * neq - 不相等（!=）
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a != b
 */
DEF(            neq, 1, 2, 1, none)

/**
 * strict_eq - 严格相等（===）
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a === b
 */
DEF(      strict_eq, 1, 2, 1, none)

/**
 * strict_neq - 严格不相等（!==）
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a !== b
 */
DEF(     strict_neq, 1, 2, 1, none)

/**
 * and - 按位与
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a & b
 */
DEF(            and, 1, 2, 1, none)

/**
 * xor - 按位异或
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a ^ b
 */
DEF(            xor, 1, 2, 1, none)

/**
 * or - 按位或
 * 字节数：1，弹出：2，压入：1
 * 栈：a b -> a | b
 */
DEF(             or, 1, 2, 1, none)

/**
 * is_undefined_or_null - 检查是否为 undefined 或 null
 * 字节数：1，弹出：1，压入：1
 * 栈：value -> value === undefined || value === null
 */
DEF(is_undefined_or_null, 1, 1, 1, none)

/**
 * private_in - 检查私有字段存在性
 * 字节数：1，弹出：2，压入：1
 * 栈：key obj -> key in obj
 */
DEF(     private_in, 1, 2, 1, none)

/**
 * push_bigint_i32 - 压入 32 位 BigInt
 * 字节数：5，弹出：0，压入：1
 * 操作数：i32（32 位整数）
 */
DEF(push_bigint_i32, 5, 0, 1, i32)

/**
 * nop - 空操作
 * 字节数：1，弹出：0，压入：0
 * 必须是非短操作码和非临时操作码的最后一个
 */
DEF(            nop, 1, 0, 0, none) /* must be the last non short and non temporary opcode */

/* ==================== 临时操作码（不会出现在最终字节码中） ==================== */

/**
 * enter_scope - 进入作用域（Phase 1 发射，Phase 2 移除）
 * 字节数：3，弹出：0，压入：0
 * 操作数：u16（作用域标志）
 */
def(    enter_scope, 3, 0, 0, u16)  /* emitted in phase 1, removed in phase 2 */

/**
 * leave_scope - 离开作用域（Phase 1 发射，Phase 2 移除）
 * 字节数：3，弹出：0，压入：0
 * 操作数：u16（作用域标志）
 */
def(    leave_scope, 3, 0, 0, u16)  /* emitted in phase 1, removed in phase 2 */

/**
 * label - 标签（Phase 1 发射，Phase 3 移除）
 * 字节数：5，弹出：0，压入：0
 * 操作数：label（跳转目标）
 */
def(          label, 5, 0, 0, label) /* emitted in phase 1, removed in phase 3 */

/* 以下操作码必须与 with_xxx 和 get_var_undef/get_var/put_var 保持相同顺序 */

/**
 * scope_get_var_undef - 作用域获取变量（未定义则返回 undefined）
 * 字节数：7，弹出：0，压入：1
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(scope_get_var_undef, 7, 0, 1, atom_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_get_var - 作用域获取变量
 * 字节数：7，弹出：0，压入：1
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(  scope_get_var, 7, 0, 1, atom_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_put_var - 作用域设置变量
 * 字节数：7，弹出：1，压入：0
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(  scope_put_var, 7, 1, 0, atom_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_delete_var - 作用域删除变量
 * 字节数：7，弹出：0，压入：1
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(scope_delete_var, 7, 0, 1, atom_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_make_ref - 作用域创建引用
 * 字节数：11，弹出：0，压入：2
 * 操作数：atom_label_u16（变量名 + 标签 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def( scope_make_ref, 11, 0, 2, atom_label_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_get_ref - 作用域获取引用
 * 字节数：7，弹出：0，压入：2
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(  scope_get_ref, 7, 0, 2, atom_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_put_var_init - 作用域初始化变量
 * 字节数：7，弹出：0，压入：2
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(scope_put_var_init, 7, 0, 2, atom_u16) /* emitted in phase 1, removed in phase 2 */

/**
 * scope_get_var_checkthis - 作用域获取 this（用于派生类构造函数）
 * 字节数：7，弹出：0，压入：1
 * 操作数：atom_u16（变量名 + 作用域索引）
 * Phase 1 发射，Phase 2 移除
 */
def(scope_get_var_checkthis, 7, 0, 1, atom_u16) /* emitted in phase 1, removed in phase 2, only used to return 'this' in derived class constructors */

/**
 * scope_get_private_field - 作用域获取私有字段
 * 字节数：7，弹出：1，压入：1
 * 操作数：atom_u16（字段名 + 作用域索引）
 * 栈：obj -> value
 * Phase 1 发射，Phase 2 移除
 */
def(scope_get_private_field, 7, 1, 1, atom_u16) /* obj -> value, emitted in phase 1, removed in phase 2 */

/**
 * scope_get_private_field2 - 作用域获取私有字段（保留对象）
 * 字节数：7，弹出：1，压入：2
 * 操作数：atom_u16（字段名 + 作用域索引）
 * 栈：obj -> obj value
 * Phase 1 发射，Phase 2 移除
 */
def(scope_get_private_field2, 7, 1, 2, atom_u16) /* obj -> obj value, emitted in phase 1, removed in phase 2 */

/**
 * scope_put_private_field - 作用域设置私有字段
 * 字节数：7，弹出：2，压入：0
 * 操作数：atom_u16（字段名 + 作用域索引）
 * 栈：obj value ->
 * Phase 1 发射，Phase 2 移除
 */
def(scope_put_private_field, 7, 2, 0, atom_u16) /* obj value ->, emitted in phase 1, removed in phase 2 */

/**
 * scope_in_private_field - 作用域检查私有字段存在性
 * 字节数：7，弹出：1，压入：1
 * 操作数：atom_u16（字段名 + 作用域索引）
 * 栈：obj -> res
 * Phase 1 发射，Phase 2 移除
 */
def(scope_in_private_field, 7, 1, 1, atom_u16) /* obj -> res emitted in phase 1, removed in phase 2 */

/**
 * get_field_opt_chain - 获取字段（可选链）
 * 字节数：5，弹出：1，压入：1
 * 操作数：atom（属性名原子索引）
 * Phase 1 发射，Phase 2 移除
 */
def(get_field_opt_chain, 5, 1, 1, atom) /* emitted in phase 1, removed in phase 2 */

/**
 * get_array_el_opt_chain - 获取数组元素（可选链）
 * 字节数：1，弹出：2，压入：1
 * Phase 1 发射，Phase 2 移除
 */
def(get_array_el_opt_chain, 1, 2, 1, none) /* emitted in phase 1, removed in phase 2 */

/**
 * set_class_name - 设置类名
 * 字节数：5，弹出：1，压入：1
 * 操作数：u32（类名原子索引）
 * Phase 1 发射，Phase 2 移除
 */
def( set_class_name, 5, 1, 1, u32) /* emitted in phase 1, removed in phase 2 */

/**
 * line_num - 行号信息（调试用）
 * 字节数：5，弹出：0，压入：0
 * 操作数：u32（行号）
 * Phase 1 发射，Phase 3 移除
 */
def(       line_num, 5, 0, 0, u32) /* emitted in phase 1, removed in phase 3 */

/* ==================== 短操作码优化（SHORT_OPCODES） ==================== */
/* 这些是单字节优化版本，用于常用操作 */

#if SHORT_OPCODES

/** push_minus1 - 压入 -1（短版本） */
DEF(    push_minus1, 1, 0, 1, none_int)

/** push_0 - 压入 0（短版本） */
DEF(         push_0, 1, 0, 1, none_int)

/** push_1 - 压入 1（短版本） */
DEF(         push_1, 1, 0, 1, none_int)

/** push_2 - 压入 2（短版本） */
DEF(         push_2, 1, 0, 1, none_int)

/** push_3 - 压入 3（短版本） */
DEF(         push_3, 1, 0, 1, none_int)

/** push_4 - 压入 4（短版本） */
DEF(         push_4, 1, 0, 1, none_int)

/** push_5 - 压入 5（短版本） */
DEF(         push_5, 1, 0, 1, none_int)

/** push_6 - 压入 6（短版本） */
DEF(         push_6, 1, 0, 1, none_int)

/** push_7 - 压入 7（短版本） */
DEF(         push_7, 1, 0, 1, none_int)

/** push_i8 - 压入 8 位整数（短版本） */
DEF(        push_i8, 2, 0, 1, i8)

/** push_i16 - 压入 16 位整数（短版本） */
DEF(       push_i16, 3, 0, 1, i16)

/** push_const8 - 压入常量（8 位索引短版本） */
DEF(    push_const8, 2, 0, 1, const8)

/** fclosure8 - 压入闭包（8 位索引短版本） */
DEF(      fclosure8, 2, 0, 1, const8) /* must follow push_const8 */

/** push_empty_string - 压入空字符串 */
DEF(push_empty_string, 1, 0, 1, none)

/** get_loc8 - 获取 8 位局部变量 */
DEF(       get_loc8, 2, 0, 1, loc8)

/** put_loc8 - 设置 8 位局部变量 */
DEF(       put_loc8, 2, 1, 0, loc8)

/** set_loc8 - 设置 8 位局部变量并保留值 */
DEF(       set_loc8, 2, 1, 1, loc8)

/** get_loc0 - 获取局部变量 0（单字节优化） */
DEF(       get_loc0, 1, 0, 1, none_loc)

/** get_loc1 - 获取局部变量 1（单字节优化） */
DEF(       get_loc1, 1, 0, 1, none_loc)

/** get_loc2 - 获取局部变量 2（单字节优化） */
DEF(       get_loc2, 1, 0, 1, none_loc)

/** get_loc3 - 获取局部变量 3（单字节优化） */
DEF(       get_loc3, 1, 0, 1, none_loc)

/** put_loc0 - 设置局部变量 0（单字节优化） */
DEF(       put_loc0, 1, 1, 0, none_loc)

/** put_loc1 - 设置局部变量 1（单字节优化） */
DEF(       put_loc1, 1, 1, 0, none_loc)

/** put_loc2 - 设置局部变量 2（单字节优化） */
DEF(       put_loc2, 1, 1, 0, none_loc)

/** put_loc3 - 设置局部变量 3（单字节优化） */
DEF(       put_loc3, 1, 1, 0, none_loc)

/** set_loc0 - 设置局部变量 0 并保留值（单字节优化） */
DEF(       set_loc0, 1, 1, 1, none_loc)

/** set_loc1 - 设置局部变量 1 并保留值（单字节优化） */
DEF(       set_loc1, 1, 1, 1, none_loc)

/** set_loc2 - 设置局部变量 2 并保留值（单字节优化） */
DEF(       set_loc2, 1, 1, 1, none_loc)

/** set_loc3 - 设置局部变量 3 并保留值（单字节优化） */
DEF(       set_loc3, 1, 1, 1, none_loc)

/** get_arg0 - 获取参数 0（单字节优化） */
DEF(       get_arg0, 1, 0, 1, none_arg)

/** get_arg1 - 获取参数 1（单字节优化） */
DEF(       get_arg1, 1, 0, 1, none_arg)

/** get_arg2 - 获取参数 2（单字节优化） */
DEF(       get_arg2, 1, 0, 1, none_arg)

/** get_arg3 - 获取参数 3（单字节优化） */
DEF(       get_arg3, 1, 0, 1, none_arg)

/** put_arg0 - 设置参数 0（单字节优化） */
DEF(       put_arg0, 1, 1, 0, none_arg)

/** put_arg1 - 设置参数 1（单字节优化） */
DEF(       put_arg1, 1, 1, 0, none_arg)

/** put_arg2 - 设置参数 2（单字节优化） */
DEF(       put_arg2, 1, 1, 0, none_arg)

/** put_arg3 - 设置参数 3（单字节优化） */
DEF(       put_arg3, 1, 1, 0, none_arg)

/** set_arg0 - 设置参数 0 并保留值（单字节优化） */
DEF(       set_arg0, 1, 1, 1, none_arg)

/** set_arg1 - 设置参数 1 并保留值（单字节优化） */
DEF(       set_arg1, 1, 1, 1, none_arg)

/** set_arg2 - 设置参数 2 并保留值（单字节优化） */
DEF(       set_arg2, 1, 1, 1, none_arg)

/** set_arg3 - 设置参数 3 并保留值（单字节优化） */
DEF(       set_arg3, 1, 1, 1, none_arg)

/** get_var_ref0 - 获取变量引用 0（单字节优化） */
DEF(   get_var_ref0, 1, 0, 1, none_var_ref)

/** get_var_ref1 - 获取变量引用 1（单字节优化） */
DEF(   get_var_ref1, 1, 0, 1, none_var_ref)

/** get_var_ref2 - 获取变量引用 2（单字节优化） */
DEF(   get_var_ref2, 1, 0, 1, none_var_ref)

/** get_var_ref3 - 获取变量引用 3（单字节优化） */
DEF(   get_var_ref3, 1, 0, 1, none_var_ref)

/** put_var_ref0 - 设置变量引用 0（单字节优化） */
DEF(   put_var_ref0, 1, 1, 0, none_var_ref)

/** put_var_ref1 - 设置变量引用 1（单字节优化） */
DEF(   put_var_ref1, 1, 1, 0, none_var_ref)

/** put_var_ref2 - 设置变量引用 2（单字节优化） */
DEF(   put_var_ref2, 1, 1, 0, none_var_ref)

/** put_var_ref3 - 设置变量引用 3（单字节优化） */
DEF(   put_var_ref3, 1, 1, 0, none_var_ref)

/** set_var_ref0 - 设置变量引用 0 并保留值（单字节优化） */
DEF(   set_var_ref0, 1, 1, 1, none_var_ref)

/** set_var_ref1 - 设置变量引用 1 并保留值（单字节优化） */
DEF(   set_var_ref1, 1, 1, 1, none_var_ref)

/** set_var_ref2 - 设置变量引用 2 并保留值（单字节优化） */
DEF(   set_var_ref2, 1, 1, 1, none_var_ref)

/** set_var_ref3 - 设置变量引用 3 并保留值（单字节优化） */
DEF(   set_var_ref3, 1, 1, 1, none_var_ref)

/** get_length - 获取 length 属性（优化版本） */
DEF(      get_length, 1, 1, 1, none)

/** if_false8 - 8 位条件跳转（假则跳） */
DEF(      if_false8, 2, 1, 0, label8)

/** if_true8 - 8 位条件跳转（真则跳） */
DEF(       if_true8, 2, 1, 0, label8) /* must come after if_false8 */

/** goto8 - 8 位无条件跳转 */
DEF(          goto8, 2, 0, 0, label8) /* must come after if_true8 */

/** goto16 - 16 位无条件跳转 */
DEF(         goto16, 3, 0, 0, label16)

/** call0 - 调用 0 个参数（优化版本） */
DEF(          call0, 1, 1, 1, npopx)

/** call1 - 调用 1 个参数（优化版本） */
DEF(          call1, 1, 1, 1, npopx)

/** call2 - 调用 2 个参数（优化版本） */
DEF(          call2, 1, 1, 1, npopx)

/** call3 - 调用 3 个参数（优化版本） */
DEF(          call3, 1, 1, 1, npopx)

/** is_undefined - 检查是否为 undefined */
DEF(     is_undefined, 1, 1, 1, none)

/** is_null - 检查是否为 null */
DEF(        is_null, 1, 1, 1, none)

/** typeof_is_undefined - 检查 typeof 是否为 undefined */
DEF(typeof_is_undefined, 1, 1, 1, none)

/** typeof_is_function - 检查 typeof 是否为 function */
DEF( typeof_is_function, 1, 1, 1, none)

#endif

#undef DEF
#undef def
#endif  /* DEF */
