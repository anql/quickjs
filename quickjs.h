/*
 * QuickJS Javascript Engine
 * 快速 JavaScript 引擎
 *
 * Copyright (c) 2017-2021 Fabrice Bellard
 * Copyright (c) 2017-2021 Charlie Gordon
 *
 * 这是一个轻量级、可嵌入的 JavaScript 引擎，支持 ES2020 规范。
 * 由 Fabrice Bellard（FFmpeg 作者）开发，以小巧快速著称。
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
#ifndef QUICKJS_H
#define QUICKJS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 编译器优化宏定义
 * 用于 GCC/Clang 的分支预测优化和内联提示
 * ============================================================================ */
#if defined(__GNUC__) || defined(__clang__)
#define js_likely(x)          __builtin_expect(!!(x), 1)    // 提示编译器该条件很可能为真
#define js_unlikely(x)        __builtin_expect(!!(x), 0)    // 提示编译器该条件很可能为假
#define js_force_inline       inline __attribute__((always_inline))  // 强制内联
#define __js_printf_like(f, a)   __attribute__((format(printf, f, a)))  //  printf 格式检查
#else
#define js_likely(x)     (x)
#define js_unlikely(x)   (x)
#define js_force_inline  inline
#define __js_printf_like(a, b)
#endif

#define JS_BOOL int  // QuickJS 使用的布尔类型

/* ============================================================================
 * 前向声明
 * 这些是 QuickJS 的核心数据结构，具体实现隐藏在 .c 文件中
 * ============================================================================ */
typedef struct JSRuntime JSRuntime;    // 运行时实例，代表整个 JS 引擎实例
typedef struct JSContext JSContext;    // 上下文实例，代表一个 JS 执行环境（类似浏览器中的 window）
typedef struct JSClass JSClass;        // 类定义，用于定义 JS 对象类型
typedef uint32_t JSClassID;            // 类 ID 类型
typedef uint32_t JSAtom;               // 原子类型，用于高效存储和比较字符串/标识符

/* ============================================================================
 * 指针宽度相关宏
 * 根据平台是 64 位还是 32 位来决定是否使用 64 位指针支持
 * ============================================================================ */
#if INTPTR_MAX >= INT64_MAX
#define JS_PTR64
#define JS_PTR64_DEF(a) a
#else
#define JS_PTR64_DEF(a)
#endif

#ifndef JS_PTR64
#define JS_NAN_BOXING  // 如果不是 64 位平台，使用 NaN-boxing 技术来压缩 JSValue
#endif

/* ============================================================================
 * 大整数位宽配置
 * 根据平台是否支持 128 位整数来决定使用 64 位还是 32 位
 * ============================================================================ */
#if defined(__SIZEOF_INT128__) && (INTPTR_MAX >= INT64_MAX)
#define JS_LIMB_BITS 64
#else
#define JS_LIMB_BITS 32
#endif

#define JS_SHORT_BIG_INT_BITS JS_LIMB_BITS  // 短大整数的位宽
    
/* ============================================================================
 * JSValue 标签系统
 * QuickJS 使用标签来区分不同类型的 JS 值
 * 所有带引用计数的标签都是负数
 * ============================================================================ */
enum {
    /* 所有带引用计数的标签都是负数 */
    JS_TAG_FIRST       = -9, /* 第一个负标签 */
    JS_TAG_BIG_INT     = -9, /* 大整数（BigInt） */
    JS_TAG_SYMBOL      = -8, /* 符号（Symbol） */
    JS_TAG_STRING      = -7, /* 字符串 */
    JS_TAG_STRING_ROPE = -6, /* 绳索字符串（用于高效字符串拼接） */
    JS_TAG_MODULE      = -3, /* 模块（内部使用） */
    JS_TAG_FUNCTION_BYTECODE = -2, /* 函数字节码（内部使用） */
    JS_TAG_OBJECT      = -1, /* 对象 */

    JS_TAG_INT         = 0,  /* 整数 */
    JS_TAG_BOOL        = 1,  /* 布尔值 */
    JS_TAG_NULL        = 2,  /* null */
    JS_TAG_UNDEFINED   = 3,  /* undefined */
    JS_TAG_UNINITIALIZED = 4, /* 未初始化（内部使用） */
    JS_TAG_CATCH_OFFSET = 5,  /* catch 偏移（内部使用） */
    JS_TAG_EXCEPTION   = 6,  /* 异常 */
    JS_TAG_SHORT_BIG_INT = 7, /* 短大整数（小范围的 BigInt） */
    JS_TAG_FLOAT64     = 8,  /* 双精度浮点数 */
    /* 如果启用了 JS_NAN_BOXING，任何更大的标签都是 FLOAT64 */
};

/* ============================================================================
 * 引用计数头
 * 所有需要引用计数的值都在前面加上这个头
 * ============================================================================ */
typedef struct JSRefCountHeader {
    int ref_count;  // 引用计数
} JSRefCountHeader;

#define JS_FLOAT64_NAN NAN  // NaN 值定义

/* ============================================================================
 * JSValue 实现方式 1：调试模式（CONFIG_CHECK_JSVALUE）
 * 这种模式用于检测引用计数错误，不能用于实际运行
 * ============================================================================ */
#ifdef CONFIG_CHECK_JSVALUE
/* JSValue 一致性检查：这种模式不能运行代码，但有助于检测简单的引用计数错误。
   如果能修改静态 C 分析器来处理特定注解就更好了（Clang 有类似注解但仅用于 Objective-C） */
typedef struct __JSValue *JSValue;
typedef const struct __JSValue *JSValueConst;

#define JS_VALUE_GET_TAG(v) (int)((uintptr_t)(v) & 0xf)  // 获取值的标签
#define JS_VALUE_GET_NORM_TAG(v) JS_VALUE_GET_TAG(v)     // 获取标准化标签（NaN-boxing 下返回 JS_TAG_FLOAT64）
#define JS_VALUE_GET_INT(v) (int)((intptr_t)(v) >> 4)    // 获取整数值
#define JS_VALUE_GET_BOOL(v) JS_VALUE_GET_INT(v)         // 获取布尔值
#define JS_VALUE_GET_FLOAT64(v) (double)JS_VALUE_GET_INT(v)  // 获取浮点值
#define JS_VALUE_GET_SHORT_BIG_INT(v) JS_VALUE_GET_INT(v)  // 获取短大整数
#define JS_VALUE_GET_PTR(v) (void *)((intptr_t)(v) & ~0xf)  // 获取指针值

#define JS_MKVAL(tag, val) (JSValue)(intptr_t)(((val) << 4) | (tag))  // 创建带标签的值
#define JS_MKPTR(tag, p) (JSValue)((intptr_t)(p) | (tag))  // 创建带标签的指针

#define JS_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == JS_TAG_FLOAT64)  // 判断是否为浮点标签

#define JS_NAN JS_MKVAL(JS_TAG_FLOAT64, 1)  // NaN 值

static inline JSValue __JS_NewFloat64(JSContext *ctx, double d)
{
    return JS_MKVAL(JS_TAG_FLOAT64, (int)d);
}

static inline JS_BOOL JS_VALUE_IS_NAN(JSValue v)
{
    return 0;
}

static inline JSValue __JS_NewShortBigInt(JSContext *ctx, int32_t d)
{
    return JS_MKVAL(JS_TAG_SHORT_BIG_INT, d);
}

/* ============================================================================
 * JSValue 实现方式 2：NaN-boxing 模式
 * 这是一种在 64 位系统中压缩 JSValue 的技术，利用 NaN 的未使用位来存储其他类型
 * ============================================================================ */
#elif defined(JS_NAN_BOXING)

typedef uint64_t JSValue;  // JSValue 被压缩成一个 64 位整数

#define JSValueConst JSValue

#define JS_VALUE_GET_TAG(v) (int)((v) >> 32)  // 高 32 位是标签
#define JS_VALUE_GET_INT(v) (int)(v)  // 低 32 位是整数值
#define JS_VALUE_GET_BOOL(v) (int)(v)
#define JS_VALUE_GET_SHORT_BIG_INT(v) (int)(v)
#define JS_VALUE_GET_PTR(v) (void *)(intptr_t)(v)  // 低 32 位是指针

#define JS_MKVAL(tag, val) (((uint64_t)(tag) << 32) | (uint32_t)(val))  // 创建值：标签放高位，值放低位
#define JS_MKPTR(tag, ptr) (((uint64_t)(tag) << 32) | (uintptr_t)(ptr))  // 创建指针值

#define JS_FLOAT64_TAG_ADDEND (0x7ff80000 - JS_TAG_FIRST + 1) /* 静默 NaN 编码 */

static inline double JS_VALUE_GET_FLOAT64(JSValue v)
{
    union {
        JSValue v;
        double d;
    } u;
    u.v = v;
    u.v += (uint64_t)JS_FLOAT64_TAG_ADDEND << 32;  // 恢复浮点数的编码
    return u.d;
}

#define JS_NAN (0x7ff8000000000000 - ((uint64_t)JS_FLOAT64_TAG_ADDEND << 32))

static inline JSValue __JS_NewFloat64(JSContext *ctx, double d)
{
    union {
        double d;
        uint64_t u64;
    } u;
    JSValue v;
    u.d = d;
    /* 规范化 NaN */
    if (js_unlikely((u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000))
        v = JS_NAN;
    else
        v = u.u64 - ((uint64_t)JS_FLOAT64_TAG_ADDEND << 32);
    return v;
}

#define JS_TAG_IS_FLOAT64(tag) ((unsigned)((tag) - JS_TAG_FIRST) >= (JS_TAG_FLOAT64 - JS_TAG_FIRST))

/* 获取标准化标签，NaN-boxing 模式下会将所有浮点标签统一为 JS_TAG_FLOAT64 */
static inline int JS_VALUE_GET_NORM_TAG(JSValue v)
{
    uint32_t tag;
    tag = JS_VALUE_GET_TAG(v);
    if (JS_TAG_IS_FLOAT64(tag))
        return JS_TAG_FLOAT64;
    else
        return tag;
}

static inline JS_BOOL JS_VALUE_IS_NAN(JSValue v)
{
    uint32_t tag;
    tag = JS_VALUE_GET_TAG(v);
    return tag == (JS_NAN >> 32);
}

static inline JSValue __JS_NewShortBigInt(JSContext *ctx, int32_t d)
{
    return JS_MKVAL(JS_TAG_SHORT_BIG_INT, d);
}

/* ============================================================================
 * JSValue 实现方式 3：标准模式（联合体）
 * 使用联合体存储不同类型的值，这是最直观的实现方式
 * ============================================================================ */
#else /* !JS_NAN_BOXING */

typedef union JSValueUnion {
    int32_t int32;
    double float64;
    void *ptr;
#if JS_SHORT_BIG_INT_BITS == 32
    int32_t short_big_int;
#else
    int64_t short_big_int;
#endif
} JSValueUnion;

typedef struct JSValue {
    JSValueUnion u;  // 值联合体
    int64_t tag;     // 类型标签
} JSValue;

#define JSValueConst JSValue

#define JS_VALUE_GET_TAG(v) ((int32_t)(v).tag)  // 获取标签
#define JS_VALUE_GET_NORM_TAG(v) JS_VALUE_GET_TAG(v)
#define JS_VALUE_GET_INT(v) ((v).u.int32)  // 获取整数
#define JS_VALUE_GET_BOOL(v) ((v).u.int32)
#define JS_VALUE_GET_FLOAT64(v) ((v).u.float64)  // 获取浮点数
#define JS_VALUE_GET_SHORT_BIG_INT(v) ((v).u.short_big_int)
#define JS_VALUE_GET_PTR(v) ((v).u.ptr)  // 获取指针

#define JS_MKVAL(tag, val) (JSValue){ (JSValueUnion){ .int32 = val }, tag }  // 创建整数值
#define JS_MKPTR(tag, p) (JSValue){ (JSValueUnion){ .ptr = p }, tag }  // 创建指针值

#define JS_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == JS_TAG_FLOAT64)

#define JS_NAN (JSValue){ .u.float64 = JS_FLOAT64_NAN, JS_TAG_FLOAT64 }

static inline JSValue __JS_NewFloat64(JSContext *ctx, double d)
{
    JSValue v;
    v.tag = JS_TAG_FLOAT64;
    v.u.float64 = d;
    return v;
}

static inline JS_BOOL JS_VALUE_IS_NAN(JSValue v)
{
    union {
        double d;
        uint64_t u64;
    } u;
    if (v.tag != JS_TAG_FLOAT64)
        return 0;
    u.d = v.u.float64;
    return (u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000;
}

static inline JSValue __JS_NewShortBigInt(JSContext *ctx, int64_t d)
{
    JSValue v;
    v.tag = JS_TAG_SHORT_BIG_INT;
    v.u.short_big_int = d;
    return v;
}

#endif /* !JS_NAN_BOXING */

/* ============================================================================
 * JSValue 类型检查宏
 * ============================================================================ */
#define JS_VALUE_IS_BOTH_INT(v1, v2) ((JS_VALUE_GET_TAG(v1) | JS_VALUE_GET_TAG(v2)) == 0)  // 两个值都是整数
#define JS_VALUE_IS_BOTH_FLOAT(v1, v2) (JS_TAG_IS_FLOAT64(JS_VALUE_GET_TAG(v1)) && JS_TAG_IS_FLOAT64(JS_VALUE_GET_TAG(v2)))  // 两个值都是浮点数

#define JS_VALUE_HAS_REF_COUNT(v) ((unsigned)JS_VALUE_GET_TAG(v) >= (unsigned)JS_TAG_FIRST)  // 判断值是否有引用计数

/* ============================================================================
 * 特殊值定义
 * 这些是 JavaScript 中的基本常量值
 * ============================================================================ */
#define JS_NULL      JS_MKVAL(JS_TAG_NULL, 0)        // null
#define JS_UNDEFINED JS_MKVAL(JS_TAG_UNDEFINED, 0)   // undefined
#define JS_FALSE     JS_MKVAL(JS_TAG_BOOL, 0)        // false
#define JS_TRUE      JS_MKVAL(JS_TAG_BOOL, 1)        // true
#define JS_EXCEPTION JS_MKVAL(JS_TAG_EXCEPTION, 0)   // 异常
#define JS_UNINITIALIZED JS_MKVAL(JS_TAG_UNINITIALIZED, 0)  // 未初始化

/* ============================================================================
 * 对象属性标志
 * 用于定义对象属性的特性（ configurable, writable, enumerable 等）
 * ============================================================================ */
#define JS_PROP_CONFIGURABLE  (1 << 0)  // 可配置：属性可以被删除或修改特性
#define JS_PROP_WRITABLE      (1 << 1)  // 可写：属性的值可以被修改
#define JS_PROP_ENUMERABLE    (1 << 2)  // 可枚举：属性会在 for-in 循环中出现
#define JS_PROP_C_W_E         (JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE | JS_PROP_ENUMERABLE)  // 三者的组合
#define JS_PROP_LENGTH        (1 << 3)  // 长度属性（数组内部使用）
#define JS_PROP_TMASK         (3 << 4)  // 属性类型掩码
#define JS_PROP_NORMAL         (0 << 4)  // 普通属性
#define JS_PROP_GETSET         (1 << 4)  // getter/setter 属性
#define JS_PROP_VARREF         (2 << 4)  // 变量引用（内部使用）
#define JS_PROP_AUTOINIT       (3 << 4)  // 自动初始化（内部使用）

/* ============================================================================
 * JS_DefineProperty 标志
 * 用于更精细地控制属性定义行为
 * ============================================================================ */
#define JS_PROP_HAS_SHIFT        8
#define JS_PROP_HAS_CONFIGURABLE (1 << 8)  // 是否指定了 configurable
#define JS_PROP_HAS_WRITABLE     (1 << 9)  // 是否指定了 writable
#define JS_PROP_HAS_ENUMERABLE   (1 << 10) // 是否指定了 enumerable
#define JS_PROP_HAS_GET          (1 << 11) // 是否指定了 getter
#define JS_PROP_HAS_SET          (1 << 12) // 是否指定了 setter
#define JS_PROP_HAS_VALUE        (1 << 13) // 是否指定了 value

/* 如果返回 false 则抛出异常（用于 JS_DefineProperty/JS_SetProperty） */
#define JS_PROP_THROW            (1 << 14)
/* 仅在严格模式下返回 false 时抛出异常（用于 JS_SetProperty） */
#define JS_PROP_THROW_STRICT     (1 << 15)

#define JS_PROP_NO_EXOTIC        (1 << 16) /* 内部使用，禁止特殊对象行为 */

#ifndef JS_DEFAULT_STACK_SIZE
#define JS_DEFAULT_STACK_SIZE (1024 * 1024)  // 默认栈大小：1MB
#endif

/* ============================================================================
 * JS_Eval() 标志
 * 控制代码编译和执行的行为
 * ============================================================================ */
#define JS_EVAL_TYPE_GLOBAL   (0 << 0) /* 全局代码（默认） */
#define JS_EVAL_TYPE_MODULE   (1 << 0) /* 模块代码 */
#define JS_EVAL_TYPE_DIRECT   (2 << 0) /* 直接调用（内部使用） */
#define JS_EVAL_TYPE_INDIRECT (3 << 0) /* 间接调用（内部使用） */
#define JS_EVAL_TYPE_MASK     (3 << 0) /* 类型掩码 */

#define JS_EVAL_FLAG_STRICT   (1 << 3) /* 强制使用严格模式 */
/* 只编译不执行。结果是一个带有 JS_TAG_FUNCTION_BYTECODE 或 JS_TAG_MODULE 标签的对象。
   可以用 JS_EvalFunction() 执行 */
#define JS_EVAL_FLAG_COMPILE_ONLY (1 << 5)
/* 不在 Error() 回溯中包含此 eval 之前的栈帧 */
#define JS_EVAL_FLAG_BACKTRACE_BARRIER (1 << 6)
/* 允许在普通脚本中使用顶层 await。JS_Eval() 返回一个 promise。
   仅允许与 JS_EVAL_TYPE_GLOBAL 一起使用 */
#define JS_EVAL_FLAG_ASYNC (1 << 7)

/* ============================================================================
 * C 函数回调类型定义
 * 用于将 C 函数暴露给 JavaScript
 * ============================================================================ */
typedef JSValue JSCFunction(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
typedef JSValue JSCFunctionMagic(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic);
typedef JSValue JSCFunctionData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValue *func_data);

/* ============================================================================
 * 内存管理相关结构
 * ============================================================================ */
typedef struct JSMallocState {
    size_t malloc_count;   // 分配次数
    size_t malloc_size;    // 当前分配的总大小
    size_t malloc_limit;   // 分配限制
    void *opaque;          // 用户自定义数据
} JSMallocState;

typedef struct JSMallocFunctions {
    void *(*js_malloc)(JSMallocState *s, size_t size);     // 分配内存
    void (*js_free)(JSMallocState *s, void *ptr);          // 释放内存
    void *(*js_realloc)(JSMallocState *s, void *ptr, size_t size);  // 重新分配
    size_t (*js_malloc_usable_size)(const void *ptr);      // 获取可用大小
} JSMallocFunctions;

typedef struct JSGCObjectHeader JSGCObjectHeader;  // GC 对象头（前向声明）

/* ============================================================================
 * 运行时管理 API
 * JSRuntime 是整个引擎的核心实例，管理内存、GC 等
 * ============================================================================ */
JSRuntime *JS_NewRuntime(void);  // 创建新的运行时
/* info 的生命周期必须超过 rt */
void JS_SetRuntimeInfo(JSRuntime *rt, const char *info);  // 设置运行时信息（用于调试）
void JS_SetMemoryLimit(JSRuntime *rt, size_t limit);  // 设置内存限制
void JS_SetGCThreshold(JSRuntime *rt, size_t gc_threshold);  // 设置 GC 阈值
/* 使用 0 禁用最大栈大小检查 */
void JS_SetMaxStackSize(JSRuntime *rt, size_t stack_size);  // 设置最大栈大小
/* 切换线程时调用此函数以更新栈顶值，用于检查栈溢出 */
void JS_UpdateStackTop(JSRuntime *rt);
JSRuntime *JS_NewRuntime2(const JSMallocFunctions *mf, void *opaque);  // 使用自定义内存分配器创建运行时
void JS_FreeRuntime(JSRuntime *rt);  // 释放运行时
void *JS_GetRuntimeOpaque(JSRuntime *rt);  // 获取运行时用户数据
void JS_SetRuntimeOpaque(JSRuntime *rt, void *opaque);  // 设置运行时用户数据
typedef void JS_MarkFunc(JSRuntime *rt, JSGCObjectHeader *gp);  // GC 标记函数
void JS_MarkValue(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);  // 标记值（GC 使用）
void JS_RunGC(JSRuntime *rt);  // 手动运行 GC
JS_BOOL JS_IsLiveObject(JSRuntime *rt, JSValueConst obj);  // 检查对象是否仍然存活

/* ============================================================================
 * 上下文管理 API
 * JSContext 代表一个 JS 执行环境，类似浏览器中的 window 或 worker
 * ============================================================================ */
JSContext *JS_NewContext(JSRuntime *rt);  // 创建新上下文
void JS_FreeContext(JSContext *s);  // 释放上下文
JSContext *JS_DupContext(JSContext *ctx);  // 增加上下文引用计数
void *JS_GetContextOpaque(JSContext *ctx);  // 获取上下文用户数据
void JS_SetContextOpaque(JSContext *ctx, void *opaque);  // 设置上下文用户数据
JSRuntime *JS_GetRuntime(JSContext *ctx);  // 获取上下文所属的运行时
void JS_SetClassProto(JSContext *ctx, JSClassID class_id, JSValue obj);  // 设置类的原型
JSValue JS_GetClassProto(JSContext *ctx, JSClassID class_id);  // 获取类的原型

/* ============================================================================
 * 内置对象控制 API
 * 用于选择性加载内置对象以节省内存
 * ============================================================================ */
/* 以下函数用于选择加载哪些内置对象以节省内存 */
JSContext *JS_NewContextRaw(JSRuntime *rt);  // 创建不包含任何内置对象的原始上下文
int JS_AddIntrinsicBaseObjects(JSContext *ctx);  // 添加基础内置对象（Object, Function, Array 等）
int JS_AddIntrinsicDate(JSContext *ctx);  // 添加 Date
int JS_AddIntrinsicEval(JSContext *ctx);  // 添加 eval
int JS_AddIntrinsicStringNormalize(JSContext *ctx);  // 添加字符串规范化函数
void JS_AddIntrinsicRegExpCompiler(JSContext *ctx);  // 添加正则表达式编译器
int JS_AddIntrinsicRegExp(JSContext *ctx);  // 添加 RegExp
int JS_AddIntrinsicJSON(JSContext *ctx);  // 添加 JSON
int JS_AddIntrinsicProxy(JSContext *ctx);  // 添加 Proxy
int JS_AddIntrinsicMapSet(JSContext *ctx);  // 添加 Map 和 Set
int JS_AddIntrinsicTypedArrays(JSContext *ctx);  // 添加类型化数组
int JS_AddIntrinsicPromise(JSContext *ctx);  // 添加 Promise
int JS_AddIntrinsicWeakRef(JSContext *ctx);  // 添加 WeakRef

JSValue js_string_codePointRange(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv);

/* ============================================================================
 * 运行时内存管理 API
 * 直接在运行时级别进行内存操作
 * ============================================================================ */
void *js_malloc_rt(JSRuntime *rt, size_t size);  // 运行时分配
void js_free_rt(JSRuntime *rt, void *ptr);  // 运行时释放
void *js_realloc_rt(JSRuntime *rt, void *ptr, size_t size);  // 运行时重新分配
size_t js_malloc_usable_size_rt(JSRuntime *rt, const void *ptr);  // 获取运行时分配的实际大小
void *js_mallocz_rt(JSRuntime *rt, size_t size);  // 运行时分配并清零

/* ============================================================================
 * 上下文内存管理 API
 * 在上下文级别进行内存操作（更常用）
 * ============================================================================ */
void *js_malloc(JSContext *ctx, size_t size);  // 上下文分配
void js_free(JSContext *ctx, void *ptr);  // 上下文释放
void *js_realloc(JSContext *ctx, void *ptr, size_t size);  // 上下文重新分配
size_t js_malloc_usable_size(JSContext *ctx, const void *ptr);  // 获取上下文分配的实际大小
void *js_realloc2(JSContext *ctx, void *ptr, size_t size, size_t *pslack);  // 重新分配并返回空闲空间
void *js_mallocz(JSContext *ctx, size_t size);  // 上下文分配并清零
char *js_strdup(JSContext *ctx, const char *str);  // 字符串复制
char *js_strndup(JSContext *ctx, const char *s, size_t n);  // 字符串复制（指定长度）

/* ============================================================================
 * 内存使用统计结构
 * 用于分析和调试内存使用情况
 * ============================================================================ */
typedef struct JSMemoryUsage {
    int64_t malloc_size, malloc_limit, memory_used_size;  // 分配大小、限制、已使用大小
    int64_t malloc_count;  // 分配次数
    int64_t memory_used_count;  // 已使用对象计数
    int64_t atom_count, atom_size;  // 原子计数和大小
    int64_t str_count, str_size;  // 字符串计数和大小
    int64_t obj_count, obj_size;  // 对象计数和大小
    int64_t prop_count, prop_size;  // 属性计数和大小
    int64_t shape_count, shape_size;  // 形状计数和大小（用于优化对象布局）
    int64_t js_func_count, js_func_size, js_func_code_size;  // JS 函数相关统计
    int64_t js_func_pc2line_count, js_func_pc2line_size;  // PC 到行号映射统计
    int64_t c_func_count, array_count;  // C 函数计数、数组计数
    int64_t fast_array_count, fast_array_elements;  // 快速数组计数和元素数
    int64_t binary_object_count, binary_object_size;  // 二进制对象计数和大小
} JSMemoryUsage;

void JS_ComputeMemoryUsage(JSRuntime *rt, JSMemoryUsage *s);  // 计算内存使用情况
void JS_DumpMemoryUsage(FILE *fp, const JSMemoryUsage *s, JSRuntime *rt);  // 输出内存使用报告

/* ============================================================================
 * Atom（原子）支持
 * Atom 是内部化的字符串，用于高效的属性名和标识符存储
 * ============================================================================ */
#define JS_ATOM_NULL 0  // 空原子

JSAtom JS_NewAtomLen(JSContext *ctx, const char *str, size_t len);  // 从字符串创建原子（指定长度）
JSAtom JS_NewAtom(JSContext *ctx, const char *str);  // 从字符串创建原子
JSAtom JS_NewAtomUInt32(JSContext *ctx, uint32_t n);  // 从数字创建原子（用于数组索引）
JSAtom JS_DupAtom(JSContext *ctx, JSAtom v);  // 增加原子引用计数
void JS_FreeAtom(JSContext *ctx, JSAtom v);  // 释放原子
void JS_FreeAtomRT(JSRuntime *rt, JSAtom v);  // 在运行时级别释放原子
JSValue JS_AtomToValue(JSContext *ctx, JSAtom atom);  // 原子转 JSValue
JSValue JS_AtomToString(JSContext *ctx, JSAtom atom);  // 原子转字符串
const char *JS_AtomToCStringLen(JSContext *ctx, size_t *plen, JSAtom atom);  // 原子转 C 字符串
static inline const char *JS_AtomToCString(JSContext *ctx, JSAtom atom)
{
    return JS_AtomToCStringLen(ctx, NULL, atom);
}
JSAtom JS_ValueToAtom(JSContext *ctx, JSValueConst val);  // JSValue 转原子

/* ============================================================================
 * 对象类支持
 * 用于定义自定义对象类型和行为
 * ============================================================================ */

typedef struct JSPropertyEnum {
    JS_BOOL is_enumerable;  // 是否可枚举
    JSAtom atom;  // 属性名（原子）
} JSPropertyEnum;

typedef struct JSPropertyDescriptor {
    int flags;  // 属性标志
    JSValue value;  // 属性值
    JSValue getter;  // getter 函数
    JSValue setter;  // setter 函数
} JSPropertyDescriptor;

/* ============================================================================
 * 对象特殊方法
 * 用于实现 Proxy 等特殊对象的行为
 * ============================================================================ */
typedef struct JSClassExoticMethods {
    /* 如果异常返回 -1（仅可能发生在 Proxy 对象），
       如果属性不存在返回 FALSE，如果存在返回 TRUE。
       如果返回 1，且 desc != NULL，则填充属性描述符 'desc'。 */
    int (*get_own_property)(JSContext *ctx, JSPropertyDescriptor *desc,
                             JSValueConst obj, JSAtom prop);
    /* '*ptab' 应持有 '*plen' 个属性键。返回 0 表示成功，-1 表示异常。
       'is_enumerable' 字段被忽略。 */
    int (*get_own_property_names)(JSContext *ctx, JSPropertyEnum **ptab,
                                  uint32_t *plen,
                                  JSValueConst obj);
    /* 返回 < 0 表示异常，或 TRUE/FALSE */
    int (*delete_property)(JSContext *ctx, JSValueConst obj, JSAtom prop);
    /* 返回 < 0 表示异常或 TRUE/FALSE */
    int (*define_own_property)(JSContext *ctx, JSValueConst this_obj,
                               JSAtom prop, JSValueConst val,
                               JSValueConst getter, JSValueConst setter,
                               int flags);
    /* 以下方法可以用前面的方法模拟，所以通常不需要 */
    /* 返回 < 0 表示异常或 TRUE/FALSE */
    int (*has_property)(JSContext *ctx, JSValueConst obj, JSAtom atom);
    JSValue (*get_property)(JSContext *ctx, JSValueConst obj, JSAtom atom,
                            JSValueConst receiver);
    /* 返回 < 0 表示异常或 TRUE/FALSE */
    int (*set_property)(JSContext *ctx, JSValueConst obj, JSAtom atom,
                        JSValueConst value, JSValueConst receiver, int flags);

    /* 要获得一致的对象行为，当 get_prototype != NULL 时，
       get_property、set_property 和 set_prototype 必须 != NULL，
       且对象必须用 JS_NULL 原型创建。 */
    JSValue (*get_prototype)(JSContext *ctx, JSValueConst obj);
    /* 返回 < 0 表示异常或 TRUE/FALSE */
    int (*set_prototype)(JSContext *ctx, JSValueConst obj, JSValueConst proto_val);
    /* 返回 < 0 表示异常或 TRUE/FALSE */
    int (*is_extensible)(JSContext *ctx, JSValueConst obj);
    /* 返回 < 0 表示异常或 TRUE/FALSE */
    int (*prevent_extensions)(JSContext *ctx, JSValueConst obj);
} JSClassExoticMethods;

/* ============================================================================
 * 类回调类型
 * ============================================================================ */
typedef void JSClassFinalizer(JSRuntime *rt, JSValue val);  // 对象销毁时的清理函数
typedef void JSClassGCMark(JSRuntime *rt, JSValueConst val,
                           JS_MarkFunc *mark_func);  // GC 标记函数
#define JS_CALL_FLAG_CONSTRUCTOR (1 << 0)  // 作为构造函数调用
typedef JSValue JSClassCall(JSContext *ctx, JSValueConst func_obj,
                            JSValueConst this_val, int argc, JSValueConst *argv,
                            int flags);  // 对象调用函数

/* ============================================================================
 * 类定义结构
 * 用于注册自定义类
 * ============================================================================ */
typedef struct JSClassDef {
    const char *class_name;  // 类名
    JSClassFinalizer *finalizer;  // 析构函数
    JSClassGCMark *gc_mark;  // GC 标记函数
    /* 如果 call != NULL，对象是函数。如果 (flags & JS_CALL_FLAG_CONSTRUCTOR) != 0，
       函数作为构造函数调用。此时 'this_val' 是 new.target。
       仅当对象构造函数位被设置时才会发生构造函数调用（见 JS_SetConstructorBit()）。 */
    JSClassCall *call;
    /* XXX: 是否要消除这个间接层？它只是为了节省内存，因为只有少数类需要这些方法 */
    JSClassExoticMethods *exotic;  // 特殊方法
} JSClassDef;

#define JS_INVALID_CLASS_ID 0  // 无效的类 ID
JSClassID JS_NewClassID(JSClassID *pclass_id);  // 分配新的类 ID
/* 如果 `v` 是对象则返回类 ID，否则返回 JS_INVALID_CLASS_ID。 */
JSClassID JS_GetClassID(JSValue v);
int JS_NewClass(JSRuntime *rt, JSClassID class_id, const JSClassDef *class_def);  // 注册新类
int JS_IsRegisteredClass(JSRuntime *rt, JSClassID class_id);  // 检查类是否已注册

/* ============================================================================
 * 值处理 API
 * 创建、转换和操作 JSValue 的函数
 * ============================================================================ */

static js_force_inline JSValue JS_NewBool(JSContext *ctx, JS_BOOL val)
{
    return JS_MKVAL(JS_TAG_BOOL, (val != 0));
}

static js_force_inline JSValue JS_NewInt32(JSContext *ctx, int32_t val)
{
    return JS_MKVAL(JS_TAG_INT, val);
}

static js_force_inline JSValue JS_NewCatchOffset(JSContext *ctx, int32_t val)
{
    return JS_MKVAL(JS_TAG_CATCH_OFFSET, val);
}

static js_force_inline JSValue JS_NewInt64(JSContext *ctx, int64_t val)
{
    JSValue v;
    if (val == (int32_t)val) {
        v = JS_NewInt32(ctx, val);  // 如果能用 32 位表示，用整数
    } else {
        v = __JS_NewFloat64(ctx, val);  // 否则用浮点数
    }
    return v;
}

static js_force_inline JSValue JS_NewUint32(JSContext *ctx, uint32_t val)
{
    JSValue v;
    if (val <= 0x7fffffff) {
        v = JS_NewInt32(ctx, val);  // 正数范围内用整数
    } else {
        v = __JS_NewFloat64(ctx, val);  // 超出范围用浮点数
    }
    return v;
}

JSValue JS_NewBigInt64(JSContext *ctx, int64_t v);  // 创建 64 位大整数
JSValue JS_NewBigUint64(JSContext *ctx, uint64_t v);  // 创建 64 位无符号大整数

static js_force_inline JSValue JS_NewFloat64(JSContext *ctx, double d)
{
    int32_t val;
    union {
        double d;
        uint64_t u;
    } u, t;
    if (d >= INT32_MIN && d <= INT32_MAX) {
        u.d = d;
        val = (int32_t)d;
        t.d = val;
        /* -0 不能用整数表示，所以比较位表示 */
        if (u.u == t.u)
            return JS_MKVAL(JS_TAG_INT, val);  // 如果是整数值，用整数存储
    }
    return __JS_NewFloat64(ctx, d);
}

/* ============================================================================
 * 类型检查函数
 * 判断 JSValue 的类型
 * ============================================================================ */
static inline JS_BOOL JS_IsNumber(JSValueConst v)
{
    int tag = JS_VALUE_GET_TAG(v);
    return tag == JS_TAG_INT || JS_TAG_IS_FLOAT64(tag);  // 整数或浮点数
}

static inline JS_BOOL JS_IsBigInt(JSContext *ctx, JSValueConst v)
{
    int tag = JS_VALUE_GET_TAG(v);
    return tag == JS_TAG_BIG_INT || tag == JS_TAG_SHORT_BIG_INT;  // 大整数
}

static inline JS_BOOL JS_IsBool(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_BOOL;
}

static inline JS_BOOL JS_IsNull(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_NULL;
}

static inline JS_BOOL JS_IsUndefined(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_UNDEFINED;
}

static inline JS_BOOL JS_IsException(JSValueConst v)
{
    return js_unlikely(JS_VALUE_GET_TAG(v) == JS_TAG_EXCEPTION);
}

static inline JS_BOOL JS_IsUninitialized(JSValueConst v)
{
    return js_unlikely(JS_VALUE_GET_TAG(v) == JS_TAG_UNINITIALIZED);
}

static inline JS_BOOL JS_IsString(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_STRING ||
        JS_VALUE_GET_TAG(v) == JS_TAG_STRING_ROPE;
}

static inline JS_BOOL JS_IsSymbol(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_SYMBOL;
}

static inline JS_BOOL JS_IsObject(JSValueConst v)
{
    return JS_VALUE_GET_TAG(v) == JS_TAG_OBJECT;
}

/* ============================================================================
 * 异常处理 API
 * ============================================================================ */
JSValue JS_Throw(JSContext *ctx, JSValue obj);  // 抛出异常
void JS_SetUncatchableException(JSContext *ctx, JS_BOOL flag);  // 设置不可捕获的异常
JSValue JS_GetException(JSContext *ctx);  // 获取当前异常
JS_BOOL JS_HasException(JSContext *ctx);  // 检查是否有异常
JS_BOOL JS_IsError(JSContext *ctx, JSValueConst val);  // 检查是否是 Error 对象
JSValue JS_NewError(JSContext *ctx);  // 创建新的 Error 对象
JSValue __js_printf_like(2, 3) JS_ThrowSyntaxError(JSContext *ctx, const char *fmt, ...);  // 抛出语法错误
JSValue __js_printf_like(2, 3) JS_ThrowTypeError(JSContext *ctx, const char *fmt, ...);  // 抛出类型错误
JSValue __js_printf_like(2, 3) JS_ThrowReferenceError(JSContext *ctx, const char *fmt, ...);  // 抛出引用错误
JSValue __js_printf_like(2, 3) JS_ThrowRangeError(JSContext *ctx, const char *fmt, ...);  // 抛出范围错误
JSValue __js_printf_like(2, 3) JS_ThrowInternalError(JSContext *ctx, const char *fmt, ...);  // 抛出内部错误
JSValue JS_ThrowOutOfMemory(JSContext *ctx);  // 抛出内存不足错误

/* ============================================================================
 * 引用计数管理 API
 * QuickJS 使用手动引用计数来管理内存
 * ============================================================================ */
void __JS_FreeValue(JSContext *ctx, JSValue v);  // 实际释放值的函数
static inline void JS_FreeValue(JSContext *ctx, JSValue v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {  // 只有带引用计数的值才需要释放
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        if (--p->ref_count <= 0) {  // 引用计数减一，如果为零则释放
            __JS_FreeValue(ctx, v);
        }
    }
}
void __JS_FreeValueRT(JSRuntime *rt, JSValue v);  // 运行时级别释放
static inline void JS_FreeValueRT(JSRuntime *rt, JSValue v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        if (--p->ref_count <= 0) {
            __JS_FreeValueRT(rt, v);
        }
    }
}

static inline JSValue JS_DupValue(JSContext *ctx, JSValueConst v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        p->ref_count++;  // 引用计数加一
    }
    return (JSValue)v;
}

static inline JSValue JS_DupValueRT(JSRuntime *rt, JSValueConst v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        p->ref_count++;
    }
    return (JSValue)v;
}

/* ============================================================================
 * 相等性比较 API
 * ============================================================================ */
JS_BOOL JS_StrictEq(JSContext *ctx, JSValueConst op1, JSValueConst op2);  // 严格相等 (===)
JS_BOOL JS_SameValue(JSContext *ctx, JSValueConst op1, JSValueConst op2);  // SameValue 算法（用于 Object.is）
JS_BOOL JS_SameValueZero(JSContext *ctx, JSValueConst op1, JSValueConst op2);  // SameValueZero（用于 Map/Set 键比较）

/* ============================================================================
 * 类型转换 API
 * 将 JSValue 转换为 C 类型
 * ============================================================================ */
int JS_ToBool(JSContext *ctx, JSValueConst val); /* 返回 -1 表示 JS_EXCEPTION */
int JS_ToInt32(JSContext *ctx, int32_t *pres, JSValueConst val);
static inline int JS_ToUint32(JSContext *ctx, uint32_t *pres, JSValueConst val)
{
    return JS_ToInt32(ctx, (int32_t*)pres, val);
}
int JS_ToInt64(JSContext *ctx, int64_t *pres, JSValueConst val);
int JS_ToIndex(JSContext *ctx, uint64_t *plen, JSValueConst val);  // 转换为数组索引
int JS_ToFloat64(JSContext *ctx, double *pres, JSValueConst val);
/* 如果 'val' 是数字则抛出异常 */
int JS_ToBigInt64(JSContext *ctx, int64_t *pres, JSValueConst val);
/* 与 JS_ToInt64() 相同但允许 BigInt */
int JS_ToInt64Ext(JSContext *ctx, int64_t *pres, JSValueConst val);

/* ============================================================================
 * 字符串 API
 * ============================================================================ */
JSValue JS_NewStringLen(JSContext *ctx, const char *str1, size_t len1);  // 创建字符串（指定长度）
static inline JSValue JS_NewString(JSContext *ctx, const char *str)
{
    return JS_NewStringLen(ctx, str, strlen(str));
}
JSValue JS_NewAtomString(JSContext *ctx, const char *str);  // 创建原子字符串
JSValue JS_ToString(JSContext *ctx, JSValueConst val);  // 转换为字符串
JSValue JS_ToPropertyKey(JSContext *ctx, JSValueConst val);  // 转换为属性键（字符串或 Symbol）
const char *JS_ToCStringLen2(JSContext *ctx, size_t *plen, JSValueConst val1, JS_BOOL cesu8);  // 转换为 C 字符串
static inline const char *JS_ToCStringLen(JSContext *ctx, size_t *plen, JSValueConst val1)
{
    return JS_ToCStringLen2(ctx, plen, val1, 0);
}
static inline const char *JS_ToCString(JSContext *ctx, JSValueConst val1)
{
    return JS_ToCStringLen2(ctx, NULL, val1, 0);
}
void JS_FreeCString(JSContext *ctx, const char *ptr);  // 释放 C 字符串

/* ============================================================================
 * 对象创建 API
 * ============================================================================ */
JSValue JS_NewObjectProtoClass(JSContext *ctx, JSValueConst proto, JSClassID class_id);  // 创建带原型和类的对象
JSValue JS_NewObjectClass(JSContext *ctx, int class_id);  // 创建类对象
JSValue JS_NewObjectProto(JSContext *ctx, JSValueConst proto);  // 创建带指定原型的对象
JSValue JS_NewObject(JSContext *ctx);  // 创建普通对象 {}

JS_BOOL JS_IsFunction(JSContext* ctx, JSValueConst val);  // 检查是否是函数
JS_BOOL JS_IsConstructor(JSContext* ctx, JSValueConst val);  // 检查是否是构造函数
JS_BOOL JS_SetConstructorBit(JSContext *ctx, JSValueConst func_obj, JS_BOOL val);  // 设置构造函数标志

JSValue JS_NewArray(JSContext *ctx);  // 创建数组 []
int JS_IsArray(JSContext *ctx, JSValueConst val);  // 检查是否是数组

JSValue JS_NewDate(JSContext *ctx, double epoch_ms);  // 创建 Date 对象

/* ============================================================================
 * 属性访问 API
 * ============================================================================ */
JSValue JS_GetPropertyInternal(JSContext *ctx, JSValueConst obj,
                               JSAtom prop, JSValueConst receiver,
                               JS_BOOL throw_ref_error);  // 内部属性获取
static js_force_inline JSValue JS_GetProperty(JSContext *ctx, JSValueConst this_obj,
                                              JSAtom prop)
{
    return JS_GetPropertyInternal(ctx, this_obj, prop, this_obj, 0);
}
JSValue JS_GetPropertyStr(JSContext *ctx, JSValueConst this_obj,
                          const char *prop);  // 通过字符串获取属性
JSValue JS_GetPropertyUint32(JSContext *ctx, JSValueConst this_obj,
                             uint32_t idx);  // 通过索引获取属性（用于数组）

int JS_SetPropertyInternal(JSContext *ctx, JSValueConst obj,
                           JSAtom prop, JSValue val, JSValueConst this_obj,
                           int flags);  // 内部属性设置
static inline int JS_SetProperty(JSContext *ctx, JSValueConst this_obj,
                                 JSAtom prop, JSValue val)
{
    return JS_SetPropertyInternal(ctx, this_obj, prop, val, this_obj, JS_PROP_THROW);
}
int JS_SetPropertyUint32(JSContext *ctx, JSValueConst this_obj,
                         uint32_t idx, JSValue val);  // 设置索引属性
int JS_SetPropertyInt64(JSContext *ctx, JSValueConst this_obj,
                        int64_t idx, JSValue val);  // 设置 64 位索引属性
int JS_SetPropertyStr(JSContext *ctx, JSValueConst this_obj,
                      const char *prop, JSValue val);  // 通过字符串设置属性
int JS_HasProperty(JSContext *ctx, JSValueConst this_obj, JSAtom prop);  // 检查属性是否存在
int JS_IsExtensible(JSContext *ctx, JSValueConst obj);  // 检查对象是否可扩展
int JS_PreventExtensions(JSContext *ctx, JSValueConst obj);  // 禁止对象扩展
int JS_DeleteProperty(JSContext *ctx, JSValueConst obj, JSAtom prop, int flags);  // 删除属性
int JS_SetPrototype(JSContext *ctx, JSValueConst obj, JSValueConst proto_val);  // 设置原型
JSValue JS_GetPrototype(JSContext *ctx, JSValueConst val);  // 获取原型

/* ============================================================================
 * 属性枚举标志
 * ============================================================================ */
#define JS_GPN_STRING_MASK  (1 << 0)  // 包含字符串属性
#define JS_GPN_SYMBOL_MASK  (1 << 1)  // 包含 Symbol 属性
#define JS_GPN_PRIVATE_MASK (1 << 2)  // 包含私有属性
/* 仅包含可枚举属性 */
#define JS_GPN_ENUM_ONLY    (1 << 4)
/* 设置 JSPropertyEnum.is_enumerable 字段 */
#define JS_GPN_SET_ENUM     (1 << 5)

int JS_GetOwnPropertyNames(JSContext *ctx, JSPropertyEnum **ptab,
                           uint32_t *plen, JSValueConst obj, int flags);  // 获取属性名列表
void JS_FreePropertyEnum(JSContext *ctx, JSPropertyEnum *tab,
                         uint32_t len);  // 释放属性名列表
int JS_GetOwnProperty(JSContext *ctx, JSPropertyDescriptor *desc,
                      JSValueConst obj, JSAtom prop);  // 获取自有属性描述符

/* ============================================================================
 * 函数调用 API
 * ============================================================================ */
JSValue JS_Call(JSContext *ctx, JSValueConst func_obj, JSValueConst this_obj,
                int argc, JSValueConst *argv);  // 调用函数
JSValue JS_Invoke(JSContext *ctx, JSValueConst this_val, JSAtom atom,
                  int argc, JSValueConst *argv);  // 调用对象的方法
JSValue JS_CallConstructor(JSContext *ctx, JSValueConst func_obj,
                           int argc, JSValueConst *argv);  // 作为构造函数调用
JSValue JS_CallConstructor2(JSContext *ctx, JSValueConst func_obj,
                            JSValueConst new_target,
                            int argc, JSValueConst *argv);  // 带 new_target 的构造函数调用
JS_BOOL JS_DetectModule(const char *input, size_t input_len);  // 检测是否是模块
/* 'input' 必须是零终止的，即 input[input_len] = '\0'。 */
JSValue JS_Eval(JSContext *ctx, const char *input, size_t input_len,
                const char *filename, int eval_flags);  // 执行代码
/* 与 JS_Eval() 相同但有显式的 'this_obj' 参数 */
JSValue JS_EvalThis(JSContext *ctx, JSValueConst this_obj,
                    const char *input, size_t input_len,
                    const char *filename, int eval_flags);
JSValue JS_GetGlobalObject(JSContext *ctx);  // 获取全局对象
int JS_IsInstanceOf(JSContext *ctx, JSValueConst val, JSValueConst obj);  // 检查 instanceof
int JS_DefineProperty(JSContext *ctx, JSValueConst this_obj,
                      JSAtom prop, JSValueConst val,
                      JSValueConst getter, JSValueConst setter, int flags);  // 定义属性
int JS_DefinePropertyValue(JSContext *ctx, JSValueConst this_obj,
                           JSAtom prop, JSValue val, int flags);  // 定义属性值
int JS_DefinePropertyValueUint32(JSContext *ctx, JSValueConst this_obj,
                                 uint32_t idx, JSValue val, int flags);  // 定义索引属性
int JS_DefinePropertyValueStr(JSContext *ctx, JSValueConst this_obj,
                              const char *prop, JSValue val, int flags);  // 定义字符串属性
int JS_DefinePropertyGetSet(JSContext *ctx, JSValueConst this_obj,
                            JSAtom prop, JSValue getter, JSValue setter,
                            int flags);  // 定义 getter/setter
void JS_SetOpaque(JSValue obj, void *opaque);  // 设置对象的不透明指针（用于关联 C 数据）
void *JS_GetOpaque(JSValueConst obj, JSClassID class_id);  // 获取不透明指针
void *JS_GetOpaque2(JSContext *ctx, JSValueConst obj, JSClassID class_id);  // 带检查的获取
void *JS_GetAnyOpaque(JSValueConst obj, JSClassID *class_id);  // 获取任意类的不透明指针

/* ============================================================================
 * JSON API
 * ============================================================================ */
/* 'buf' 必须是零终止的，即 buf[buf_len] = '\0'。 */
JSValue JS_ParseJSON(JSContext *ctx, const char *buf, size_t buf_len,
                     const char *filename);  // 解析 JSON
#define JS_PARSE_JSON_EXT (1 << 0) /* 允许扩展 JSON */
JSValue JS_ParseJSON2(JSContext *ctx, const char *buf, size_t buf_len,
                      const char *filename, int flags);
JSValue JS_JSONStringify(JSContext *ctx, JSValueConst obj,
                         JSValueConst replacer, JSValueConst space0);  // JSON.stringify

/* ============================================================================
 * ArrayBuffer 和类型化数组 API
 * ============================================================================ */
typedef void JSFreeArrayBufferDataFunc(JSRuntime *rt, void *opaque, void *ptr);
JSValue JS_NewArrayBuffer(JSContext *ctx, uint8_t *buf, size_t len,
                          JSFreeArrayBufferDataFunc *free_func, void *opaque,
                          JS_BOOL is_shared);  // 创建 ArrayBuffer（使用外部内存）
JSValue JS_NewArrayBufferCopy(JSContext *ctx, const uint8_t *buf, size_t len);  // 创建 ArrayBuffer（复制数据）
void JS_DetachArrayBuffer(JSContext *ctx, JSValueConst obj);  // 分离 ArrayBuffer
uint8_t *JS_GetArrayBuffer(JSContext *ctx, size_t *psize, JSValueConst obj);  // 获取 ArrayBuffer 数据

/* 类型化数组枚举 */
typedef enum JSTypedArrayEnum {
    JS_TYPED_ARRAY_UINT8C = 0,
    JS_TYPED_ARRAY_INT8,
    JS_TYPED_ARRAY_UINT8,
    JS_TYPED_ARRAY_INT16,
    JS_TYPED_ARRAY_UINT16,
    JS_TYPED_ARRAY_INT32,
    JS_TYPED_ARRAY_UINT32,
    JS_TYPED_ARRAY_BIG_INT64,
    JS_TYPED_ARRAY_BIG_UINT64,
    JS_TYPED_ARRAY_FLOAT16,
    JS_TYPED_ARRAY_FLOAT32,
    JS_TYPED_ARRAY_FLOAT64,
} JSTypedArrayEnum;

JSValue JS_NewTypedArray(JSContext *ctx, int argc, JSValueConst *argv,
                         JSTypedArrayEnum array_type);  // 创建类型化数组
JSValue JS_GetTypedArrayBuffer(JSContext *ctx, JSValueConst obj,
                               size_t *pbyte_offset,
                               size_t *pbyte_length,
                               size_t *pbytes_per_element);  // 获取底层 ArrayBuffer

/* SharedArrayBuffer 回调函数 */
typedef struct {
    void *(*sab_alloc)(void *opaque, size_t size);
    void (*sab_free)(void *opaque, void *ptr);
    void (*sab_dup)(void *opaque, void *ptr);
    void *sab_opaque;
} JSSharedArrayBufferFunctions;
void JS_SetSharedArrayBufferFunctions(JSRuntime *rt,
                                      const JSSharedArrayBufferFunctions *sf);

/* ============================================================================
 * Promise API
 * ============================================================================ */
typedef enum JSPromiseStateEnum {
    JS_PROMISE_PENDING,    // 等待中
    JS_PROMISE_FULFILLED,  // 已完成
    JS_PROMISE_REJECTED,   // 已拒绝
} JSPromiseStateEnum;

JSValue JS_NewPromiseCapability(JSContext *ctx, JSValue *resolving_funcs);  // 创建 Promise 能力对象
JSPromiseStateEnum JS_PromiseState(JSContext *ctx, JSValue promise);  // 获取 Promise 状态
JSValue JS_PromiseResult(JSContext *ctx, JSValue promise);  // 获取 Promise 结果

/* is_handled = TRUE 表示拒绝已被处理 */
typedef void JSHostPromiseRejectionTracker(JSContext *ctx, JSValueConst promise,
                                           JSValueConst reason,
                                           JS_BOOL is_handled, void *opaque);
void JS_SetHostPromiseRejectionTracker(JSRuntime *rt, JSHostPromiseRejectionTracker *cb, void *opaque);  // 设置 Promise 拒绝追踪器

/* ============================================================================
 * 中断和调试控制 API
 * ============================================================================ */
/* 如果 != 0 则需要中断 JS 代码执行 */
typedef int JSInterruptHandler(JSRuntime *rt, void *opaque);
void JS_SetInterruptHandler(JSRuntime *rt, JSInterruptHandler *cb, void *opaque);  // 设置中断处理器
/* 如果 can_block 为 TRUE，可以使用 Atomics.wait() */
void JS_SetCanBlock(JSRuntime *rt, JS_BOOL can_block);
/* 选择从编译代码中剥离哪些调试信息 */
#define JS_STRIP_SOURCE (1 << 0) /* 剥离源代码 */
#define JS_STRIP_DEBUG  (1 << 1) /* 剥离所有调试信息包括源代码 */
void JS_SetStripInfo(JSRuntime *rt, int flags);  // 设置剥离信息标志
int JS_GetStripInfo(JSRuntime *rt);  // 获取剥离信息标志

/* 设置 [IsHTMLDDA] 内部槽（用于实现 HTML 文档的特殊行为） */
void JS_SetIsHTMLDDA(JSContext *ctx, JSValueConst obj);

/* ============================================================================
 * 模块系统 API
 * ============================================================================ */
typedef struct JSModuleDef JSModuleDef;

/* 返回模块说明符（用 js_malloc() 分配）或 NULL 表示异常 */
typedef char *JSModuleNormalizeFunc(JSContext *ctx,
                                    const char *module_base_name,
                                    const char *module_name, void *opaque);
typedef JSModuleDef *JSModuleLoaderFunc(JSContext *ctx,
                                        const char *module_name, void *opaque);
typedef JSModuleDef *JSModuleLoaderFunc2(JSContext *ctx,
                                         const char *module_name, void *opaque,
                                         JSValueConst attributes);
/* 返回 -1 表示异常，0 表示成功 */
typedef int JSModuleCheckSupportedImportAttributes(JSContext *ctx, void *opaque,
                                                   JSValueConst attributes);
                                                   
/* module_normalize = NULL 允许并使用默认的模块文件名规范化器 */
void JS_SetModuleLoaderFunc(JSRuntime *rt,
                            JSModuleNormalizeFunc *module_normalize,
                            JSModuleLoaderFunc *module_loader, void *opaque);  // 设置模块加载器
/* 与 JS_SetModuleLoaderFunc 相同但支持属性。如果 module_check_attrs = NULL，则不进行属性检查。 */
void JS_SetModuleLoaderFunc2(JSRuntime *rt,
                             JSModuleNormalizeFunc *module_normalize,
                             JSModuleLoaderFunc2 *module_loader,
                             JSModuleCheckSupportedImportAttributes *module_check_attrs,
                             void *opaque);
/* 返回模块的 import.meta 对象 */
JSValue JS_GetImportMeta(JSContext *ctx, JSModuleDef *m);
JSAtom JS_GetModuleName(JSContext *ctx, JSModuleDef *m);  // 获取模块名
JSValue JS_GetModuleNamespace(JSContext *ctx, JSModuleDef *m);  // 获取模块命名空间

/* ============================================================================
 * Job（微任务）API
 * ============================================================================ */
typedef JSValue JSJobFunc(JSContext *ctx, int argc, JSValueConst *argv);
int JS_EnqueueJob(JSContext *ctx, JSJobFunc *job_func, int argc, JSValueConst *argv);  // 入队 Job

JS_BOOL JS_IsJobPending(JSRuntime *rt);  // 检查是否有待处理的 Job
int JS_ExecutePendingJob(JSRuntime *rt, JSContext **pctx);  // 执行待处理的 Job

/* ============================================================================
 * 对象序列化 API（用于预编译代码）
 * ============================================================================ */
#define JS_WRITE_OBJ_BYTECODE  (1 << 0) /* 允许函数/模块 */
#define JS_WRITE_OBJ_BSWAP     (1 << 1) /* 字节交换输出 */
#define JS_WRITE_OBJ_SAB       (1 << 2) /* 允许 SharedArrayBuffer */
#define JS_WRITE_OBJ_REFERENCE (1 << 3) /* 允许对象引用以编码任意对象图 */
uint8_t *JS_WriteObject(JSContext *ctx, size_t *psize, JSValueConst obj,
                        int flags);  // 序列化对象
uint8_t *JS_WriteObject2(JSContext *ctx, size_t *psize, JSValueConst obj,
                         int flags, uint8_t ***psab_tab, size_t *psab_tab_len);

#define JS_READ_OBJ_BYTECODE  (1 << 0) /* 允许函数/模块 */
#define JS_READ_OBJ_ROM_DATA  (1 << 1) /* 避免复制 'buf' 数据 */
#define JS_READ_OBJ_SAB       (1 << 2) /* 允许 SharedArrayBuffer */
#define JS_READ_OBJ_REFERENCE (1 << 3) /* 允许对象引用 */
JSValue JS_ReadObject(JSContext *ctx, const uint8_t *buf, size_t buf_len,
                      int flags);  // 反序列化对象
/* 实例化并执行字节码函数。仅在使用 JS_ReadObject() 读取脚本或模块时使用 */
JSValue JS_EvalFunction(JSContext *ctx, JSValue fun_obj);
/* 加载模块 'obj' 的依赖项。当 JS_ReadObject() 返回模块时有用。 */
int JS_ResolveModule(JSContext *ctx, JSValueConst obj);

/* 仅对 os.Worker() 导出 */
JSAtom JS_GetScriptOrModuleName(JSContext *ctx, int n_stack_levels);
/* 仅对 os.Worker() 导出 */
JSValue JS_LoadModule(JSContext *ctx, const char *basename,
                      const char *filename);

/* ============================================================================
 * C 函数定义
 * 用于将 C 函数注册为 JS 函数
 * ============================================================================ */
typedef enum JSCFunctionEnum {  /* XXX: 应该重命名以隔离命名空间 */
    JS_CFUNC_generic,
    JS_CFUNC_generic_magic,
    JS_CFUNC_constructor,
    JS_CFUNC_constructor_magic,
    JS_CFUNC_constructor_or_func,
    JS_CFUNC_constructor_or_func_magic,
    JS_CFUNC_f_f,
    JS_CFUNC_f_f_f,
    JS_CFUNC_getter,
    JS_CFUNC_setter,
    JS_CFUNC_getter_magic,
    JS_CFUNC_setter_magic,
    JS_CFUNC_iterator_next,
} JSCFunctionEnum;

/* C 函数类型联合体 */
typedef union JSCFunctionType {
    JSCFunction *generic;
    JSValue (*generic_magic)(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic);
    JSCFunction *constructor;
    JSValue (*constructor_magic)(JSContext *ctx, JSValueConst new_target, int argc, JSValueConst *argv, int magic);
    JSCFunction *constructor_or_func;
    double (*f_f)(double);
    double (*f_f_f)(double, double);
    JSValue (*getter)(JSContext *ctx, JSValueConst this_val);
    JSValue (*setter)(JSContext *ctx, JSValueConst this_val, JSValueConst val);
    JSValue (*getter_magic)(JSContext *ctx, JSValueConst this_val, int magic);
    JSValue (*setter_magic)(JSContext *ctx, JSValueConst this_val, JSValueConst val, int magic);
    JSValue (*iterator_next)(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv, int *pdone, int magic);
} JSCFunctionType;

JSValue JS_NewCFunction2(JSContext *ctx, JSCFunction *func,
                         const char *name,
                         int length, JSCFunctionEnum cproto, int magic);  // 创建 C 函数
JSValue JS_NewCFunctionData(JSContext *ctx, JSCFunctionData *func,
                            int length, int magic, int data_len,
                            JSValueConst *data);  // 创建带数据的 C 函数

static inline JSValue JS_NewCFunction(JSContext *ctx, JSCFunction *func, const char *name,
                                      int length)
{
    return JS_NewCFunction2(ctx, func, name, length, JS_CFUNC_generic, 0);
}

static inline JSValue JS_NewCFunctionMagic(JSContext *ctx, JSCFunctionMagic *func,
                                           const char *name,
                                           int length, JSCFunctionEnum cproto, int magic)
{
    /* 用于消除 -Wcast-function-type 警告 */
    JSCFunctionType ft = { .generic_magic = func };
    return JS_NewCFunction2(ctx, ft.generic, name, length, cproto, magic);
}
int JS_SetConstructor(JSContext *ctx, JSValueConst func_obj,
                      JSValueConst proto);  // 设置函数的 prototype 属性

/* ============================================================================
 * C 属性定义
 * 用于批量定义对象属性
 * ============================================================================ */
typedef struct JSCFunctionListEntry {
    const char *name;
    uint8_t prop_flags;
    uint8_t def_type;
    int16_t magic;
    union {
        struct {
            uint8_t length; /* XXX: 应该移到联合体外面 */
            uint8_t cproto; /* XXX: 应该移到联合体外面 */
            JSCFunctionType cfunc;
        } func;
        struct {
            JSCFunctionType get;
            JSCFunctionType set;
        } getset;
        struct {
            const char *name;
            int base;
        } alias;
        struct {
            const struct JSCFunctionListEntry *tab;
            int len;
        } prop_list;
        const char *str;
        int32_t i32;
        int64_t i64;
        double f64;
    } u;
} JSCFunctionListEntry;

/* 属性定义类型枚举 */
#define JS_DEF_CFUNC          0
#define JS_DEF_CGETSET        1
#define JS_DEF_CGETSET_MAGIC  2
#define JS_DEF_PROP_STRING    3
#define JS_DEF_PROP_INT32     4
#define JS_DEF_PROP_INT64     5
#define JS_DEF_PROP_DOUBLE    6
#define JS_DEF_PROP_UNDEFINED 7
#define JS_DEF_OBJECT         8
#define JS_DEF_ALIAS          9
#define JS_DEF_PROP_ATOM     10
#define JS_DEF_PROP_BOOL     11

/* 注意：C++ 不喜欢嵌套设计器 */
/* 以下宏用于方便定义属性列表 */
#define JS_CFUNC_DEF(name, length, func1) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, 0, .u = { .func = { length, JS_CFUNC_generic, { .generic = func1 } } } }
#define JS_CFUNC_MAGIC_DEF(name, length, func1, magic) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, magic, .u = { .func = { length, JS_CFUNC_generic_magic, { .generic_magic = func1 } } } }
#define JS_CFUNC_SPECIAL_DEF(name, length, cproto, func1) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, 0, .u = { .func = { length, JS_CFUNC_ ## cproto, { .cproto = func1 } } } }
#define JS_ITERATOR_NEXT_DEF(name, length, func1, magic) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_CFUNC, magic, .u = { .func = { length, JS_CFUNC_iterator_next, { .iterator_next = func1 } } } }
#define JS_CGETSET_DEF(name, fgetter, fsetter) { name, JS_PROP_CONFIGURABLE, JS_DEF_CGETSET, 0, .u = { .getset = { .get = { .getter = fgetter }, .set = { .setter = fsetter } } } }
#define JS_CGETSET_MAGIC_DEF(name, fgetter, fsetter, magic) { name, JS_PROP_CONFIGURABLE, JS_DEF_CGETSET_MAGIC, magic, .u = { .getset = { .get = { .getter_magic = fgetter }, .set = { .setter_magic = fsetter } } } }
#define JS_PROP_STRING_DEF(name, cstr, prop_flags) { name, prop_flags, JS_DEF_PROP_STRING, 0, .u = { .str = cstr } }
#define JS_PROP_INT32_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_INT32, 0, .u = { .i32 = val } }
#define JS_PROP_INT64_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_INT64, 0, .u = { .i64 = val } }
#define JS_PROP_DOUBLE_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_DOUBLE, 0, .u = { .f64 = val } }
#define JS_PROP_UNDEFINED_DEF(name, prop_flags) { name, prop_flags, JS_DEF_PROP_UNDEFINED, 0, .u = { .i32 = 0 } }
#define JS_PROP_ATOM_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_ATOM, 0, .u = { .i32 = val } }
#define JS_PROP_BOOL_DEF(name, val, prop_flags) { name, prop_flags, JS_DEF_PROP_BOOL, 0, .u = { .i32 = val } }
#define JS_OBJECT_DEF(name, tab, len, prop_flags) { name, prop_flags, JS_DEF_OBJECT, 0, .u = { .prop_list = { tab, len } } }
#define JS_ALIAS_DEF(name, from) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_ALIAS, 0, .u = { .alias = { from, -1 } } }
#define JS_ALIAS_BASE_DEF(name, from, base) { name, JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE, JS_DEF_ALIAS, 0, .u = { .alias = { from, base } } }

int JS_SetPropertyFunctionList(JSContext *ctx, JSValueConst obj,
                               const JSCFunctionListEntry *tab,
                               int len);  // 批量设置属性函数列表

/* ============================================================================
 * C 模块定义
 * 用于创建原生模块
 * ============================================================================ */
typedef int JSModuleInitFunc(JSContext *ctx, JSModuleDef *m);

JSModuleDef *JS_NewCModule(JSContext *ctx, const char *name_str,
                           JSModuleInitFunc *func);  // 创建 C 模块
/* 只能在模块实例化之前调用 */
int JS_AddModuleExport(JSContext *ctx, JSModuleDef *m, const char *name_str);  // 添加导出
int JS_AddModuleExportList(JSContext *ctx, JSModuleDef *m,
                           const JSCFunctionListEntry *tab, int len);  // 批量添加导出
/* 只能在模块实例化之后调用 */
int JS_SetModuleExport(JSContext *ctx, JSModuleDef *m, const char *export_name,
                       JSValue val);  // 设置导出值
int JS_SetModuleExportList(JSContext *ctx, JSModuleDef *m,
                           const JSCFunctionListEntry *tab, int len);  // 批量设置导出值
/* 将 JSValue 关联到 C 模块 */
int JS_SetModulePrivateValue(JSContext *ctx, JSModuleDef *m, JSValue val);
JSValue JS_GetModulePrivateValue(JSContext *ctx, JSModuleDef *m);
                        
/* ============================================================================
 * 调试值输出
 * 用于打印 JSValue 的内容
 * ============================================================================ */
typedef struct {
    JS_BOOL show_hidden : 8; /* 仅显示可枚举属性 */
    JS_BOOL raw_dump : 8; /* 避免自动初始化和任何 malloc() 调用（内部使用） */
    uint32_t max_depth; /* 递归到这个深度，0 = 无限制 */
    uint32_t max_string_length; /* 字符串打印不超过这个长度，0 = 无限制 */
    uint32_t max_item_count; /* 数组或对象打印不超过这个数量，0 = 无限制 */
} JSPrintValueOptions;

typedef void JSPrintValueWrite(void *opaque, const char *buf, size_t len);

void JS_PrintValueSetDefaultOptions(JSPrintValueOptions *options);  // 设置默认打印选项
void JS_PrintValueRT(JSRuntime *rt, JSPrintValueWrite *write_func, void *write_opaque,
                     JSValueConst val, const JSPrintValueOptions *options);  // 运行时级别打印
void JS_PrintValue(JSContext *ctx, JSPrintValueWrite *write_func, void *write_opaque,
                   JSValueConst val, const JSPrintValueOptions *options);  // 上下文级别打印

#undef js_unlikely
#undef js_force_inline

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_H */
