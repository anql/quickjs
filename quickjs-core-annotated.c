/*
 * QuickJS 核心代码注释 - 关键函数和数据结构详解
 * 
 * 本文件选取 quickjs.c 中的核心代码段添加详细中文注释
 * 帮助理解 JavaScript 引擎的内部工作原理
 * 
 * @note 这不是完整的 quickjs.c，而是关键部分的注释版本
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include "quickjs.h"

/* ==========================================================================
 * 第一部分：核心数据结构
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * JSRuntime - 运行时实例
 * 
 * 这是 QuickJS 的最高层级结构，每个进程通常只创建一个运行时
 * 管理所有全局资源：内存、GC、原子表、类注册等
 * -------------------------------------------------------------------------- */
struct JSRuntime {
    /* 内存管理 */
    JSMallocFunctions mf;           /* 内存分配函数表 */
    JSMallocState malloc_state;     /* 内存分配状态 */
    size_t malloc_limit;            /* 内存分配限制 */
    size_t gc_threshold;            /* GC 触发阈值 */
    
    /* 垃圾回收 */
    JSGCObjectHeader *first_gc_obj;  /* GC 对象链表头 */
    JSGCObjectHeader *last_gc_obj;   /* GC 对象链表尾 */
    int gc_pending;                  /* GC 待处理标志 */
    
    /* 原子表 - 字符串驻留 */
    JSAtom **atom_array;            /* 原子指针数组 */
    uint32_t atom_count;            /* 原子数量 */
    uint32_t atom_size;             /* 原子数组大小 */
    uint32_t atom_free_index;       /* 空闲原子索引（用于复用）*/
    
    /* 类注册表 */
    JSClass **class_array;          /* 类定义数组 */
    uint32_t class_count;           /* 已注册类数量 */
    
    /* 上下文链表 */
    JSContext *ctx_list;            /* 所有 Context 的链表 */
    
    /* 微任务队列 - Promise 回调 */
    JSValue *job_queue;             /* 微任务队列数组 */
    int job_count;                  /* 队列中的任务数 */
    int job_size;                   /* 队列容量 */
    
    /* 中断处理 - 用于超时控制 */
    JSInterruptHandler *interrupt_handler;  /* 中断处理函数 */
    void *interrupt_opaque;         /* 处理函数参数 */
    
    /* 运行时信息（调试用）*/
    const char *rt_info;
    
    /* 其他内部状态 */
    // ... 更多字段
};

/* --------------------------------------------------------------------------
 * JSContext - 执行上下文
 * 
 * 每个 Context 对应一个独立的 JavaScript 环境
 * 包含全局对象、作用域、异常状态等
 * -------------------------------------------------------------------------- */
struct JSContext {
    JSRuntime *rt;                  /* 关联的运行时 */
    JSContext *next;                /* 下一个 Context（链表）*/
    
    /* 全局对象 */
    JSValue global_obj;             /* 全局对象 */
    JSValue global_var_obj;         /* 全局变量对象（内部）*/
    
    /* 异常状态 */
    JSValue exception;              /* 当前异常值 */
    JS_BOOL exception_pending;      /* 是否有待处理异常 */
    
    /* 内存分配统计 */
    JSMallocState malloc_state;
    
    /* 字符串表 */
    JSString **str_hash;            /* 字符串哈希表 */
    uint32_t str_count;             /* 字符串数量 */
    
    /* 对象管理 */
    JSObject **obj_hash;            /* 对象哈希表 */
    uint32_t obj_count;             /* 对象数量 */
    
    /* 作用域链 */
    JSVarRef *var_refs;             /* 变量引用链表 */
    
    /* 执行栈 */
    JSValue *stack;                 /* 值栈 */
    JSValue *stack_top;             /* 栈顶指针 */
    size_t stack_size;              /* 栈大小 */
    
    /* 模块加载 */
    JSModuleDef **module_table;     /* 模块表 */
    uint32_t module_count;          /* 模块数量 */
    
    /* Promise 相关 */
    JSValue promise_ctor;           /* Promise 构造函数 */
    
    /* 用户不透明指针 */
    void *opaque;
};

/* --------------------------------------------------------------------------
 * JSObject - 对象内部表示
 * 
 * 所有 JavaScript 对象（包括数组、函数、Date 等）的内部结构
 * -------------------------------------------------------------------------- */
typedef struct JSObject {
    JSGCObjectHeader header;        /* GC 对象头 - 必须放在开头 */
    
    JSClass *class;                 /* 类定义 */
    
    /* 属性存储 */
    JSProperty *prop;               /* 属性数组 */
    uint32_t prop_count;            /* 属性数量 */
    uint32_t prop_size;             /* 属性数组容量 */
    
    /* 原型链 */
    JSValue proto;                  /* 原型对象 */
    
    /* 对象标志 */
    uint32_t extensible:1;          /* 是否可扩展 */
    uint32_t is_fast_array:1;       /* 是否是快速数组 */
    uint32_t is_constructor:1;      /* 是否可作构造函数 */
    // ... 其他标志位
    
    /* 联合体 - 根据对象类型存储不同数据 */
    union {
        struct {
            /* 普通对象数据 */
            void *data;
        } object_data;
        
        struct {
            /* 数组专用 */
            JSValue *elements;      /* 元素数组 */
            uint32_t length;        /* 数组长度 */
            uint32_t size;          /* 元素数组容量 */
        } array;
        
        struct {
            /* 函数专用 */
            uint8_t *bytecode;      /* 字节码 */
            uint32_t bytecode_len;  /* 字节码长度 */
            JSAtom *arg_names;      /* 参数名表 */
            uint32_t arg_count;     /* 参数数量 */
            JSVarDef *var_defs;     /* 变量定义表 */
            uint32_t var_count;     /* 变量数量 */
            // ... 更多函数据
        } func;
        
        struct {
            /* C 函数绑定 */
            JSCFunction *cfunc;     /* C 函数指针 */
            int magic;              /* magic 值 */
        } cfunc;
        
        /* 其他对象类型的数据... */
    } u;
} JSObject;

/* --------------------------------------------------------------------------
 * JSString - 字符串内部表示
 * 
 * 使用 CESU-8 编码（类似 UTF-8）
 * -------------------------------------------------------------------------- */
typedef struct JSString {
    JSGCObjectHeader header;        /* GC 对象头 */
    
    uint32_t hash;                  /* 哈希值（用于快速查找）*/
    uint32_t len;                   /* 字符串长度（字符数）*/
    uint8_t flags;                  /* 标志位 */
    uint8_t is_well_known:1;        /* 是否是知名符号 */
    uint8_t is_bigint:1;            /* 是否是 BigInt 字符串表示 */
    // ... 其他标志
    
    uint8_t data[0];                /* 柔性数组 - 字符串数据 */
} JSString;

/* ==========================================================================
 * 第二部分：核心函数实现
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * JS_NewRuntime - 创建运行时
 * 
 * 这是使用 QuickJS 的第一步，创建全局运行时实例
 * -------------------------------------------------------------------------- */
JSRuntime *JS_NewRuntime(void)
{
    return JS_NewRuntime2(NULL, NULL);  /* 使用默认内存分配器 */
}

JSRuntime *JS_NewRuntime2(const JSMallocFunctions *mf, void *opaque)
{
    JSRuntime *rt;
    
    /* 1. 分配运行时结构 */
    rt = js_malloc_rt(NULL, sizeof(JSRuntime));
    if (!rt)
        return NULL;
    
    /* 2. 初始化内存分配器 */
    if (!mf) {
        /* 使用系统默认分配器 */
        mf = &js_default_malloc_funcs;
    }
    rt->mf = *mf;
    rt->malloc_state.opaque = opaque;
    
    /* 3. 初始化原子表 */
    rt->atom_size = 16;  /* 初始容量 */
    rt->atom_array = js_malloc_rt(rt, rt->atom_size * sizeof(JSAtom *));
    
    /* 4. 初始化 GC 链表 */
    rt->first_gc_obj = NULL;
    rt->last_gc_obj = NULL;
    
    /* 5. 初始化 Context 链表 */
    rt->ctx_list = NULL;
    
    /* 6. 初始化微任务队列 */
    rt->job_queue = NULL;
    rt->job_count = 0;
    rt->job_size = 0;
    
    /* 7. 设置默认限制 */
    rt->malloc_limit = SIZE_MAX;  /* 无限制 */
    rt->gc_threshold = 256 * 1024; /* 256KB 触发 GC */
    
    return rt;
}

/* --------------------------------------------------------------------------
 * JS_NewContext - 创建执行上下文
 * 
 * 每个 Context 是独立的 JS 环境，可以执行代码
 * -------------------------------------------------------------------------- */
JSContext *JS_NewContext(JSRuntime *rt)
{
    JSContext *ctx;
    
    /* 1. 分配 Context 结构 */
    ctx = js_mallocz_rt(rt, sizeof(JSContext));
    if (!ctx)
        return NULL;
    
    /* 2. 关联运行时 */
    ctx->rt = rt;
    
    /* 3. 加入运行时的 Context 链表 */
    ctx->next = rt->ctx_list;
    rt->ctx_list = ctx;
    
    /* 4. 初始化全局对象 */
    ctx->global_obj = JS_UNDEFINED;
    ctx->global_var_obj = JS_UNDEFINED;
    
    /* 5. 初始化异常状态 */
    ctx->exception = JS_UNDEFINED;
    ctx->exception_pending = JS_FALSE;
    
    /* 6. 初始化字符串哈希表 */
    ctx->str_hash = js_mallocz_rt(rt, JS_STR_HASH_SIZE * sizeof(JSString *));
    
    /* 7. 初始化执行栈 */
    ctx->stack_size = JS_DEFAULT_STACK_SIZE;
    ctx->stack = js_malloc_rt(rt, ctx->stack_size);
    
    /* 8. 添加内置对象（可选）*/
    /* JS_AddIntrinsicBaseObjects(ctx); */
    
    return ctx;
}

/* --------------------------------------------------------------------------
 * JS_FreeRuntime - 释放运行时
 * 
 * 清理所有资源，必须在所有 Context 释放后调用
 * -------------------------------------------------------------------------- */
void JS_FreeRuntime(JSRuntime *rt)
{
    /* 1. 释放所有 Context */
    while (rt->ctx_list) {
        JS_FreeContext(rt->ctx_list);
    }
    
    /* 2. 运行 GC 清理残留对象 */
    JS_RunGC(rt);
    
    /* 3. 释放原子表 */
    for (uint32_t i = 0; i < rt->atom_count; i++) {
        if (rt->atom_array[i]) {
            JS_FreeAtomRT(rt, rt->atom_array[i]);
        }
    }
    js_free_rt(rt, rt->atom_array);
    
    /* 4. 释放类注册表 */
    for (uint32_t i = 0; i < rt->class_count; i++) {
        js_free_rt(rt, rt->class_array[i]);
    }
    js_free_rt(rt, rt->class_array);
    
    /* 5. 释放运行时结构本身 */
    js_free_rt(rt, rt);
}

/* --------------------------------------------------------------------------
 * JS_FreeContext - 释放上下文
 * -------------------------------------------------------------------------- */
void JS_FreeContext(JSContext *ctx)
{
    JSRuntime *rt = ctx->rt;
    
    /* 1. 从运行时链表中移除 */
    JSContext **pctx = &rt->ctx_list;
    while (*pctx != ctx) {
        pctx = &(*pctx)->next;
    }
    *pctx = ctx->next;
    
    /* 2. 释放全局对象 */
    JS_FreeValue(ctx, ctx->global_obj);
    JS_FreeValue(ctx, ctx->global_var_obj);
    
    /* 3. 释放所有对象 */
    /* 遍历对象哈希表，释放每个对象 */
    
    /* 4. 释放所有字符串 */
    /* 遍历字符串哈希表，释放每个字符串 */
    
    /* 5. 释放执行栈 */
    js_free_rt(rt, ctx->stack);
    
    /* 6. 释放 Context 结构 */
    js_free_rt(rt, ctx);
}

/* --------------------------------------------------------------------------
 * JS_Eval - 执行 JavaScript 代码
 * 
 * 这是最核心的函数之一，将源码编译并执行
 * -------------------------------------------------------------------------- */
JSValue JS_Eval(JSContext *ctx, const char *input, size_t input_len,
                const char *filename, int eval_flags)
{
    JSValue ret;
    JSFunctionDef *fd = NULL;
    
    /* 1. 词法分析和语法分析 */
    /* 将源码解析为函数定义（包含字节码）*/
    fd = js_parse_script(ctx, input, input_len, filename, eval_flags);
    if (!fd) {
        return JS_EXCEPTION;  /* 解析错误，异常已设置 */
    }
    
    /* 2. 如果是仅编译模式，返回函数对象 */
    if (eval_flags & JS_EVAL_FLAG_COMPILE_ONLY) {
        ret = JS_MKPTR(JS_TAG_FUNCTION_BYTECODE, fd);
        return ret;
    }
    
    /* 3. 执行字节码 */
    ret = JS_EvalFunction(ctx, fd);
    
    /* 4. 如果是 async 模式，返回 Promise */
    if ((eval_flags & JS_EVAL_FLAG_ASYNC) && !JS_IsException(ret)) {
        ret = js_async_function_call(ctx, ret);
    }
    
    return ret;
}

/* --------------------------------------------------------------------------
 * JS_EvalFunction - 执行函数字节码
 * 
 * 将编译后的函数执行，返回结果
 * -------------------------------------------------------------------------- */
JSValue JS_EvalFunction(JSContext *ctx, JSFunctionDef *fd)
{
    JSValue ret;
    JSObject *func_obj;
    
    /* 1. 创建函数对象 */
    func_obj = js_create_function_object(ctx, fd);
    if (!func_obj) {
        return JS_EXCEPTION;
    }
    
    /* 2. 调用函数（无参数）*/
    ret = JS_Call(ctx, JS_MKPTR(JS_TAG_OBJECT, func_obj),
                  ctx->global_obj, 0, NULL);
    
    return ret;
}

/* --------------------------------------------------------------------------
 * JS_Call - 调用函数
 * 
 * 支持各种调用类型：普通调用、构造调用、方法调用等
 * -------------------------------------------------------------------------- */
JSValue JS_Call(JSContext *ctx, JSValueConst func_obj,
                JSValueConst this_obj, int argc, JSValueConst *argv)
{
    JSObject *p;
    
    if (!JS_IsObject(func_obj)) {
        return JS_ThrowTypeError(ctx, "not a function");
    }
    
    p = JS_VALUE_GET_OBJ(func_obj);
    
    /* 1. C 函数 - 直接调用 */
    if (p->class->call) {
        return p->class->call(ctx, func_obj, this_obj, argc, argv, 0);
    }
    
    /* 2. 字节码函数 - 进入解释器 */
    if (p->class->class_id == JS_CLASS_BYTECODE_FUNCTION) {
        return js_interpreter(ctx, p->u.func.func_def, this_obj, argc, argv);
    }
    
    /* 3. Bound Function - 绑定 this 后调用 */
    if (p->class->class_id == JS_CLASS_BOUND_FUNCTION) {
        return js_call_bound_function(ctx, p, this_obj, argc, argv);
    }
    
    return JS_ThrowTypeError(ctx, "not callable");
}

/* --------------------------------------------------------------------------
 * js_interpreter - 字节码解释器主循环
 * 
 * 这是 QuickJS 的"心脏"，负责执行字节码
 * 使用基于栈的执行模型
 * -------------------------------------------------------------------------- */
JSValue js_interpreter(JSContext *ctx, JSFunctionDef *fd,
                       JSValueConst this_obj, int argc, JSValueConst *argv)
{
    JSValue *sp;              /* 栈指针 */
    uint8_t *pc;              /* 程序计数器 */
    JSValue ret;
    
    /* 1. 初始化执行环境 */
    sp = ctx->stack_top;      /* 设置栈指针 */
    pc = fd->bytecode;        /* 设置程序计数器 */
    
    /* 2. 压入参数 */
    for (int i = 0; i < argc; i++) {
        *sp++ = argv[i];
    }
    
    /* 3. 主循环 - 取指 - 执行 */
    for (;;) {
        uint8_t opcode = *pc++;  /* 取指令 */
        
#ifdef CONFIG_STACK_CHECK
        /* 检查栈溢出 */
        if (js_unlikely((uint8_t *)sp > ctx->stack + ctx->stack_size)) {
            ret = JS_ThrowStackOverflow(ctx);
            goto exception;
        }
#endif
        
        /* 检查中断（用于超时控制）*/
        if (js_unlikely(ctx->rt->interrupt_handler)) {
            if (ctx->rt->interrupt_handler(ctx->rt, ctx->rt->interrupt_opaque)) {
                ret = JS_ThrowInterruptError(ctx);
                goto exception;
            }
        }
        
        /* 执行指令 */
        switch (opcode) {
            /* --------------------------------------------------------------
             * 栈操作
             * -------------------------------------------------------------- */
            case OP_push_i8:  /* 压入 8 位整数 */
                *sp++ = JS_NewInt32(ctx, *(int8_t *)pc);
                pc++;
                break;
                
            case OP_push_i32:  /* 压入 32 位整数 */
                *sp++ = JS_NewInt32(ctx, *(int32_t *)pc);
                pc += 4;
                break;
                
            case OP_push_const:  /* 压入常量 */
                {
                    uint32_t idx = *(uint32_t *)pc;
                    *sp++ = fd->cpool[idx];  /* 从常量池获取 */
                    pc += 4;
                }
                break;
            
            case OP_dup:  /* 复制栈顶 */
                *sp = sp[-1];
                sp++;
                break;
                
            case OP_pop:  /* 弹出栈顶 */
                JS_FreeValue(ctx, *--sp);
                break;
                
            /* --------------------------------------------------------------
             * 算术运算
             * -------------------------------------------------------------- */
            case OP_add:  /* 加法 */
                {
                    JSValue val1 = sp[-2];
                    JSValue val2 = sp[-1];
                    
                    /* 快速路径：两个整数相加 */
                    if (JS_VALUE_IS_BOTH_INT(val1, val2)) {
                        int32_t a = JS_VALUE_GET_INT(val1);
                        int32_t b = JS_VALUE_GET_INT(val2);
                        /* 检查溢出 */
                        if (js_likely((int32_t)(a + b) == a + b)) {
                            sp[-2] = JS_NewInt32(ctx, a + b);
                            sp--;
                            break;
                        }
                    }
                    
                    /* 慢速路径：一般加法（包括字符串拼接）*/
                    sp[-2] = js_binary_math_op(ctx, JS_OP_ADD, val1, val2);
                    JS_FreeValue(ctx, val2);
                    sp--;
                }
                break;
                
            case OP_sub:  /* 减法 */
            case OP_mul:  /* 乘法 */
            case OP_div:  /* 除法 */
            case OP_mod:  /* 取模 */
                /* 类似加法实现 */
                break;
                
            /* --------------------------------------------------------------
             * 类型转换
             * -------------------------------------------------------------- */
            case OP_to_bool:  /* 转布尔值 */
                {
                    JSValue val = sp[-1];
                    int res = js_to_bool(ctx, val);
                    JS_FreeValue(ctx, val);
                    sp[-1] = JS_NewBool(ctx, res);
                }
                break;
                
            case OP_to_number:  /* 转数字 */
                {
                    JSValue val = sp[-1];
                    JSValue num = js_to_number(ctx, val);
                    JS_FreeValue(ctx, val);
                    sp[-1] = num;
                }
                break;
                
            /* --------------------------------------------------------------
             * 对象操作
             * -------------------------------------------------------------- */
            case OP_get_field:  /* 获取对象属性 */
                {
                    JSValue obj = sp[-1];
                    uint32_t atom_idx = *(uint32_t *)pc;
                    JSAtom atom = fd->cpool[atom_idx].u.atom;
                    
                    sp[-1] = JS_GetProperty(ctx, obj, atom);
                    JS_FreeValue(ctx, obj);
                    pc += 4;
                }
                break;
                
            case OP_put_field:  /* 设置对象属性 */
                {
                    JSValue value = sp[-1];
                    JSValue obj = sp[-2];
                    uint32_t atom_idx = *(uint32_t *)pc;
                    JSAtom atom = fd->cpool[atom_idx].u.atom;
                    
                    JS_SetProperty(ctx, obj, atom, value);
                    sp -= 2;
                    pc += 4;
                }
                break;
                
            case OP_invoke:  /* 方法调用 */
                {
                    uint32_t atom_idx = *(uint32_t *)pc;
                    int argc = *pc++;
                    JSAtom atom = fd->cpool[atom_idx].u.atom;
                    
                    /* 获取方法 */
                    JSValue method = JS_GetProperty(ctx, sp[-argc-1], atom);
                    
                    /* 调用方法 */
                    JSValue result = JS_Call(ctx, method, sp[-argc-1],
                                            argc, sp - argc);
                    
                    JS_FreeValue(ctx, method);
                    for (int i = 0; i <= argc; i++) {
                        JS_FreeValue(ctx, sp[-argc-1+i]);
                    }
                    sp -= argc + 1;
                    *sp++ = result;
                    pc += 4;
                }
                break;
                
            /* --------------------------------------------------------------
             * 控制流
             * -------------------------------------------------------------- */
            case OP_jump:  /* 无条件跳转 */
                {
                    int32_t offset = *(int32_t *)pc;
                    pc += offset;
                }
                break;
                
            case OP_if_false:  /* 条件跳转 */
                {
                    int32_t offset = *(int32_t *)pc;
                    JSValue val = sp[-1];
                    
                    if (!js_to_bool(ctx, val)) {
                        pc += offset;  /* 条件为假，跳转 */
                    } else {
                        pc += 4;  /* 条件为真，继续 */
                    }
                    JS_FreeValue(ctx, val);
                }
                break;
                
            case OP_loop:  /* 循环 */
                {
                    int32_t offset = *(int32_t *)pc;
                    /* 用于 for-of、for-in 等循环 */
                    pc += offset;
                }
                break;
                
            /* --------------------------------------------------------------
             * 函数调用
             * -------------------------------------------------------------- */
            case OP_call:  /* 函数调用 */
                {
                    int argc = *pc++;
                    JSValue func = sp[-argc-1];
                    
                    JSValue result = JS_Call(ctx, func, JS_UNDEFINED,
                                            argc, sp - argc);
                    
                    /* 清理栈 */
                    for (int i = 0; i <= argc; i++) {
                        JS_FreeValue(ctx, sp[-argc-1+i]);
                    }
                    sp -= argc + 1;
                    *sp++ = result;
                }
                break;
                
            case OP_construct:  /* 构造函数调用 */
                {
                    int argc = *pc++;
                    JSValue ctor = sp[-argc-1];
                    
                    JSValue result = JS_CallConstructor(ctx, ctor,
                                                       argc, sp - argc);
                    
                    /* 清理栈 */
                    for (int i = 0; i <= argc; i++) {
                        JS_FreeValue(ctx, sp[-argc-1+i]);
                    }
                    sp -= argc + 1;
                    *sp++ = result;
                }
                break;
                
            /* --------------------------------------------------------------
             * 返回
             * -------------------------------------------------------------- */
            case OP_return:  /* 返回 */
                ret = sp[-1];
                sp--;
                goto done;
                
            case OP_return_undef:  /* 返回 undefined */
                ret = JS_UNDEFINED;
                goto done;
        }
    }
    
exception:
    /* 异常处理：展开栈帧，传播异常 */
    while (sp > ctx->stack_top) {
        JS_FreeValue(ctx, *--sp);
    }
    
done:
    ctx->stack_top = sp;
    return ret;
}

/* --------------------------------------------------------------------------
 * JS_NewObject - 创建对象
 * 
 * 创建一个普通的 JavaScript 对象
 * -------------------------------------------------------------------------- */
JSValue JS_NewObject(JSContext *ctx)
{
    return JS_NewObjectProto(ctx, JS_NULL);  /* 原型为 null */
}

JSValue JS_NewObjectProto(JSContext *ctx, JSValue proto)
{
    JSObject *p;
    
    /* 1. 分配对象结构 */
    p = js_mallocz(ctx, sizeof(JSObject));
    if (!p)
        return JS_EXCEPTION;
    
    /* 2. 初始化 GC 头 */
    p->header.ref_count = 1;
    p->header.gc_mark = 0;
    
    /* 3. 设置类信息 */
    p->class = &js_object_class;
    
    /* 4. 初始化属性数组 */
    p->prop_size = 4;  /* 初始容量 */
    p->prop = js_malloc(ctx, p->prop_size * sizeof(JSProperty));
    p->prop_count = 0;
    
    /* 5. 设置原型 */
    p->proto = JS_DupValue(ctx, proto);
    
    /* 6. 设置标志 */
    p->extensible = 1;
    p->is_fast_array = 0;
    
    return JS_MKPTR(JS_TAG_OBJECT, p);
}

/* --------------------------------------------------------------------------
 * JS_SetProperty - 设置对象属性
 * 
 * 这是最常用的 API 之一，用于给对象添加/修改属性
 * -------------------------------------------------------------------------- */
int JS_SetProperty(JSContext *ctx, JSValueConst this_obj,
                   JSAtom prop, JSValue val)
{
    return JS_SetPropertyInternal(ctx, this_obj, prop, val,
                                  JS_PROP_THROW, NULL);
}

int JS_SetPropertyInternal(JSContext *ctx, JSValueConst this_obj,
                           JSAtom prop, JSValue val, int flags,
                           JSValue *pgetter)
{
    JSObject *p;
    JSProperty *pr;
    int ret;
    
    if (!JS_IsObject(this_obj)) {
        JS_FreeValue(ctx, val);
        return -1;
    }
    
    p = JS_VALUE_GET_OBJ(this_obj);
    
    /* 1. 查找现有属性 */
    pr = JS_FindProperty(p, prop);
    
    if (pr) {
        /* 2. 属性已存在 - 修改值 */
        if (!(pr->flags & JS_PROP_WRITABLE)) {
            /* 只读属性 */
            JS_FreeValue(ctx, val);
            if (flags & JS_PROP_THROW) {
                return JS_ThrowTypeError(ctx, "cannot assign to read-only property");
            }
            return -1;
        }
        
        /* 释放旧值，设置新值 */
        JS_FreeValue(ctx, pr->value);
        pr->value = val;
        return 0;
    }
    
    /* 3. 属性不存在 - 添加新属性 */
    if (!p->extensible) {
        /* 对象不可扩展 */
        JS_FreeValue(ctx, val);
        if (flags & JS_PROP_THROW) {
            return JS_ThrowTypeError(ctx, "object is not extensible");
        }
        return -1;
    }
    
    /* 4. 扩展属性数组（如果需要）*/
    if (p->prop_count >= p->prop_size) {
        uint32_t new_size = p->prop_size * 2;
        JSProperty *new_prop = js_realloc(ctx, p->prop,
                                         new_size * sizeof(JSProperty));
        if (!new_prop) {
            JS_FreeValue(ctx, val);
            return -1;
        }
        p->prop = new_prop;
        p->prop_size = new_size;
    }
    
    /* 5. 添加新属性 */
    pr = &p->prop[p->prop_count++];
    pr->atom = JS_DupAtom(ctx, prop);
    pr->value = val;
    pr->flags = JS_PROP_WRITABLE | JS_PROP_ENUMERABLE | JS_PROP_CONFIGURABLE;
    
    return 0;
}

/* --------------------------------------------------------------------------
 * JS_GetProperty - 获取对象属性
 * 
 * 获取对象的属性值，支持原型链查找
 * -------------------------------------------------------------------------- */
JSValue JS_GetProperty(JSContext *ctx, JSValueConst this_obj, JSAtom prop)
{
    JSObject *p;
    JSProperty *pr;
    
    if (!JS_IsObject(this_obj)) {
        return JS_ThrowTypeError(ctx, "not an object");
    }
    
    p = JS_VALUE_GET_OBJ(this_obj);
    
    /* 1. 在对象自身查找 */
    pr = JS_FindProperty(p, prop);
    if (pr) {
        return JS_DupValue(ctx, pr->value);
    }
    
    /* 2. 在原型链查找 */
    if (!JS_IsNull(p->proto)) {
        return JS_GetProperty(ctx, p->proto, prop);
    }
    
    /* 3. 属性不存在 */
    return JS_UNDEFINED;
}

/* --------------------------------------------------------------------------
 * JS_RunGC - 运行垃圾回收
 * 
 * 标记 - 清除算法：
 * 1. 从根对象开始标记所有可达对象
 * 2. 扫描所有对象，释放未标记的对象
 * -------------------------------------------------------------------------- */
void JS_RunGC(JSRuntime *rt)
{
    JSGCObjectHeader *gp;
    
#ifdef DUMP_GC
    printf("GC start\n");
#endif
    
    /* 1. 清除阶段 - 重置所有对象的 GC 标记 */
    for (gp = rt->first_gc_obj; gp != NULL; gp = gp->next) {
        gp->gc_mark = 0;
    }
    
    /* 2. 标记阶段 - 从根对象开始标记 */
    /* 标记所有 Context */
    for (JSContext *ctx = rt->ctx_list; ctx; ctx = ctx->next) {
        js_mark_value(ctx, ctx->global_obj);
        js_mark_value(ctx, ctx->exception);
        /* 标记栈上的值 */
        for (JSValue *sp = ctx->stack; sp < ctx->stack_top; sp++) {
            js_mark_value(ctx, *sp);
        }
    }
    
    /* 标记原子表中的值 */
    for (uint32_t i = 0; i < rt->atom_count; i++) {
        if (rt->atom_array[i]) {
            js_mark_atom(rt, rt->atom_array[i]);
        }
    }
    
    /* 3. 清除阶段 - 释放未标记的对象 */
    gp = rt->first_gc_obj;
    while (gp != NULL) {
        JSGCObjectHeader *next = gp->next;
        
        if (!gp->gc_mark) {
            /* 未标记 - 对象不可达，释放 */
            js_free_gc_object(rt, gp);
        }
        
        gp = next;
    }
    
#ifdef DUMP_GC
    printf("GC end\n");
#endif
}

/* --------------------------------------------------------------------------
 * js_mark_value - 标记值（GC 辅助函数）
 * 
 * 递归标记值及其引用的所有对象
 * -------------------------------------------------------------------------- */
void js_mark_value(JSContext *ctx, JSValue val)
{
    if (!JS_VALUE_HAS_REF_COUNT(val))
        return;  /* 立即数无需标记 */
    
    JSObject *p = JS_VALUE_GET_OBJ(val);
    if (p->header.gc_mark)
        return;  /* 已标记 */
    
    /* 标记对象 */
    p->header.gc_mark = 1;
    
    /* 标记原型 */
    if (!JS_IsNull(p->proto)) {
        js_mark_value(ctx, p->proto);
    }
    
    /* 标记所有属性值 */
    for (uint32_t i = 0; i < p->prop_count; i++) {
        js_mark_value(ctx, p->prop[i].value);
    }
    
    /* 根据对象类型标记特定数据 */
    switch (p->class->class_id) {
        case JS_CLASS_ARRAY:
            /* 标记数组元素 */
            for (uint32_t i = 0; i < p->u.array.length; i++) {
                js_mark_value(ctx, p->u.array.elements[i]);
            }
            break;
            
        case JS_CLASS_BYTECODE_FUNCTION:
            /* 标记函数闭包变量 */
            /* ... */
            break;
            
        /* 其他类型... */
    }
}

/* --------------------------------------------------------------------------
 * JS_NewCFunction - 创建 C 函数绑定
 * 
 * 将 C 函数暴露给 JavaScript 调用
 * -------------------------------------------------------------------------- */
JSValue JS_NewCFunction(JSContext *ctx, JSCFunction *func,
                        const char *name, int length)
{
    return JS_NewCFunctionMagic(ctx, func, name, length, 0);
}

JSValue JS_NewCFunctionMagic(JSContext *ctx, JSCFunctionMagic *func,
                             const char *name, int length, int magic)
{
    JSObject *p;
    JSAtom name_atom;
    
    /* 1. 分配对象 */
    p = js_mallocz(ctx, sizeof(JSObject));
    if (!p)
        return JS_EXCEPTION;
    
    /* 2. 初始化 GC 头 */
    p->header.ref_count = 1;
    
    /* 3. 设置类为 C 函数 */
    p->class = &js_c_function_class;
    
    /* 4. 存储 C 函数指针 */
    p->u.cfunc.cfunc = (JSCFunction *)func;
    p->u.cfunc.magic = magic;
    
    /* 5. 设置函数名 */
    if (name) {
        name_atom = JS_NewAtom(ctx, name);
        JS_SetProperty(ctx, JS_MKPTR(JS_TAG_OBJECT, p), name_atom,
                      JS_NewString(ctx, name));
    }
    
    /* 6. 设置 length 属性（参数数量）*/
    JS_SetPropertyStr(ctx, JS_MKPTR(JS_TAG_OBJECT, p), "length",
                     JS_NewInt32(ctx, length));
    
    return JS_MKPTR(JS_TAG_OBJECT, p);
}

/* ==========================================================================
 * 第三部分：辅助工具和宏
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * 值操作宏
 * -------------------------------------------------------------------------- */

/**
 * @brief 获取值的标签
 * @note 这是最频繁使用的宏之一，必须高效
 */
#define JS_VALUE_GET_TAG(v) ((int32_t)(v).tag)

/**
 * @brief 获取值的指针部分
 * @note 用于访问对象、字符串等引用类型
 */
#define JS_VALUE_GET_PTR(v) ((v).u.ptr)

/**
 * @brief 从标签和指针构造值
 */
#define JS_MKPTR(tag, p) (JSValue){ (JSValueUnion){ .ptr = p }, tag }

/**
 * @brief 从标签和整数构造值
 */
#define JS_MKVAL(tag, val) (JSValue){ (JSValueUnion){ .int32 = val }, tag }

/* --------------------------------------------------------------------------
 * 对象访问宏
 * -------------------------------------------------------------------------- */

/**
 * @brief 检查值是否有引用计数
 * @note 负标签表示是引用类型
 */
#define JS_VALUE_HAS_REF_COUNT(v) ((unsigned)JS_VALUE_GET_TAG(v) >= (unsigned)JS_TAG_FIRST)

/**
 * @brief 从值获取对象指针
 * @note 使用前必须确保值是对象类型
 */
#define JS_VALUE_GET_OBJ(v) ((JSObject *)JS_VALUE_GET_PTR(v))

/**
 * @brief 从值获取字符串指针
 */
#define JS_VALUE_GET_STRING(v) ((JSString *)JS_VALUE_GET_PTR(v))

/* --------------------------------------------------------------------------
 * 内存分配宏
 * -------------------------------------------------------------------------- */

/**
 * @brief 分配内存并清零
 * @note 最常用的分配方式
 */
#define js_mallocz(ctx, size) js_malloc(ctx, size)  /* 实际会清零 */

/**
 * @brief 重新分配内存
 * @note 保持原有数据
 */
#define js_realloc(ctx, ptr, size) ctx->rt->mf.js_realloc(&ctx->rt->malloc_state, ptr, size)

/**
 * @brief 释放内存
 */
#define js_free(ctx, ptr) ctx->rt->mf.js_free(&ctx->rt->malloc_state, ptr)

/* ==========================================================================
 * 第四部分：常见模式和最佳实践
 * ========================================================================== */

/* --------------------------------------------------------------------------
 * 模式 1：创建和释放值
 * 
 * 每个 JS_NewXXX 都需要对应的 JS_FreeValue
 * -------------------------------------------------------------------------- */
void example_value_lifecycle(JSContext *ctx) {
    /* 创建值 */
    JSValue str = JS_NewString(ctx, "Hello");
    JSValue num = JS_NewInt32(ctx, 42);
    
    /* 使用值 */
    // ...
    
    /* 释放值 - 必须配对调用 */
    JS_FreeValue(ctx, str);
    JS_FreeValue(ctx, num);
}

/* --------------------------------------------------------------------------
 * 模式 2：错误处理
 * 
 * 检查异常并及时清理
 * -------------------------------------------------------------------------- */
JSValue example_error_handling(JSContext *ctx) {
    JSValue result = JS_Eval(ctx, code, len, "test.js", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result)) {
        /* 获取异常 */
        JSValue exc = JS_GetException(ctx);
        
        /* 处理异常（记录日志、转换格式等）*/
        // ...
        
        /* 释放异常值 */
        JS_FreeValue(ctx, exc);
        
        /* 返回异常 */
        return JS_EXCEPTION;
    }
    
    return result;
}

/* --------------------------------------------------------------------------
 * 模式 3：C 函数参数处理
 * 
 * 验证参数类型和数量
 * -------------------------------------------------------------------------- */
static JSValue js_my_function(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    /* 1. 检查参数数量 */
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "Expected 2 arguments");
    }
    
    /* 2. 转换参数类型 */
    int32_t a;
    double b;
    
    if (JS_ToInt32(ctx, &a, argv[0]))
        return JS_EXCEPTION;  /* 类型转换失败，异常已设置 */
    
    if (JS_ToFloat64(ctx, &b, argv[1]))
        return JS_EXCEPTION;
    
    /* 3. 执行业务逻辑 */
    double result = a + b;
    
    /* 4. 返回结果 */
    return JS_NewFloat64(ctx, result);
}

/* --------------------------------------------------------------------------
 * 模式 4：对象属性操作
 * 
 * 使用 Atom 提高性能
 * -------------------------------------------------------------------------- */
void example_property_ops(JSContext *ctx) {
    JSValue obj = JS_NewObject(ctx);
    
    /* 创建 Atom（可复用）*/
    JSAtom nameAtom = JS_NewAtom(ctx, "name");
    JSAtom ageAtom = JS_NewAtom(ctx, "age");
    
    /* 设置属性 */
    JS_SetProperty(ctx, obj, nameAtom, JS_NewString(ctx, "Alice"));
    JS_SetProperty(ctx, obj, ageAtom, JS_NewInt32(ctx, 30));
    
    /* 获取属性 */
    JSValue name = JS_GetProperty(ctx, obj, nameAtom);
    
    /* 使用属性 */
    // ...
    
    /* 清理 */
    JS_FreeValue(ctx, name);
    JS_FreeValue(ctx, obj);
    JS_FreeAtom(ctx, nameAtom);
    JS_FreeAtom(ctx, ageAtom);
}

/* --------------------------------------------------------------------------
 * 模式 5：数组操作
 * 
 * 使用 JS_DefinePropertyValueUint32 设置数组元素
 * -------------------------------------------------------------------------- */
JSValue create_array_example(JSContext *ctx) {
    JSValue arr = JS_NewArray(ctx);
    
    for (int i = 0; i < 10; i++) {
        JS_DefinePropertyValueUint32(ctx, arr, i,
                                     JS_NewInt32(ctx, i * 2),
                                     JS_PROP_C_W_E);
    }
    
    return arr;
}

/* ==========================================================================
 * 总结
 * ========================================================================== */

/*
 * QuickJS 核心架构总结：
 * 
 * 1. 值表示：使用标签值系统，区分立即数和引用类型
 * 
 * 2. 内存管理：引用计数 + 标记 - 清除 GC
 * 
 * 3. 执行模型：基于栈的字节码解释器
 * 
 * 4. 对象系统：原型链 + 属性描述符
 * 
 * 5. 扩展机制：C 函数绑定 + 自定义类
 * 
 * 关键设计原则：
 * - 简单优先：代码清晰易懂
 * - 轻量级：最小化依赖和复杂度
 * - 实用主义：在性能和简洁性之间平衡
 * 
 * 进一步阅读：
 * - ARCHITECTURE.md - 架构文档
 * - PRINCIPLES.md - 原理详解
 * - quickjs-annotated.h - 头文件注释
 */
