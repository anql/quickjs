/*
 * QuickJS Javascript Engine - 带注释版本
 * 
 * 这是 QuickJS 引擎的核心头文件，定义了所有公共 API 和数据结构
 * 
 * @author Fabrice Bellard, Charlie Gordon
 * @note 本文件添加了详细中文注释以便理解
 */
#ifndef QUICKJS_H
#define QUICKJS_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 编译器优化宏
 * ========================================================================== */
#if defined(__GNUC__) || defined(__clang__)
/**
 * @brief 分支预测优化 - 提示编译器该条件很可能为真
 * @note 使用 GCC/Clang 的 __builtin_expect 实现
 */
#define js_likely(x)          __builtin_expect(!!(x), 1)

/**
 * @brief 分支预测优化 - 提示编译器该条件很可能为假
 * @note 用于异常处理等罕见情况
 */
#define js_unlikely(x)        __builtin_expect(!!(x), 0)

/**
 * @brief 强制内联函数
 * @note 用于性能关键的短函数
 */
#define js_force_inline       inline __attribute__((always_inline))

/**
 * @brief 标记 printf 风格函数，用于编译器格式字符串检查
 */
#define __js_printf_like(f, a)   __attribute__((format(printf, f, a)))
#else
#define js_likely(x)     (x)
#define js_unlikely(x)   (x)
#define js_force_inline  inline
#define __js_printf_like(a, b)
#endif

/* ==========================================================================
 * 基本类型定义
 * ========================================================================== */
#define JS_BOOL int

/* 前向声明 - 主要数据结构 */
typedef struct JSRuntime JSRuntime;   /**< 运行时实例，管理全局资源 */
typedef struct JSContext JSContext;   /**< 执行上下文，对应一个 JS 环境 */
typedef struct JSClass JSClass;       /**< 类定义，用于自定义对象类型 */
typedef uint32_t JSClassID;           /**< 类 ID 类型 */
typedef uint32_t JSAtom;              /**< 原子类型，用于字符串内部表示 */

/* ==========================================================================
 * 指针宽度检测
 * ========================================================================== */
/**
 * @brief 检测是否为 64 位指针架构
 * @note 64 位系统使用不同的值表示策略
 */
#if INTPTR_MAX >= INT64_MAX
#define JS_PTR64
#define JS_PTR64_DEF(a) a
#else
#define JS_PTR64_DEF(a)
#endif

/**
 * @brief 非 64 位系统启用 NaN-boxing 优化
 * @note NaN-boxing 允许在 64 位空间中存储任意 JS 值
 */
#ifndef JS_PTR64
#define JS_NAN_BOXING
#endif

/* ==========================================================================
 * 大整数位宽配置
 * ========================================================================== */
#if defined(__SIZEOF_INT128__) && (INTPTR_MAX >= INT64_MAX)
#define JS_LIMB_BITS 64   /**< 支持 128 位整数时使用 64 位 limb */
#else
#define JS_LIMB_BITS 32   /**< 否则使用 32 位 limb */
#endif

#define JS_SHORT_BIG_INT_BITS JS_LIMB_BITS
    
/* ==========================================================================
 * JS 值标签系统
 * 
 * QuickJS 使用标签值（tagged value）系统来表示所有 JavaScript 值
 * 标签用于区分不同的数据类型
 * ========================================================================== */
enum {
    /* 所有带引用计数的标签都是负数 */
    JS_TAG_FIRST       = -9, /**< 第一个负标签 */
    JS_TAG_BIG_INT     = -9, /**< 大整数 (BigInt) */
    JS_TAG_SYMBOL      = -8, /**< 符号类型 (Symbol) */
    JS_TAG_STRING      = -7, /**< 字符串类型 */
    JS_TAG_STRING_ROPE = -6, /**< 绳索字符串（用于高效拼接）*/
    JS_TAG_MODULE      = -3, /**< 模块类型（内部使用）*/
    JS_TAG_FUNCTION_BYTECODE = -2, /**< 字节码函数（内部使用）*/
    JS_TAG_OBJECT      = -1, /**< 对象类型 */

    /* 非负标签 - 直接存储值，无引用计数 */
    JS_TAG_INT         = 0,  /**< 32 位整数 */
    JS_TAG_BOOL        = 1,  /**< 布尔值 */
    JS_TAG_NULL        = 2,  /**< null */
    JS_TAG_UNDEFINED   = 3,  /**< undefined */
    JS_TAG_UNINITIALIZED = 4,/**< 未初始化变量 */
    JS_TAG_CATCH_OFFSET = 5, /**< catch 块偏移（内部使用）*/
    JS_TAG_EXCEPTION   = 6,  /**< 异常标记 */
    JS_TAG_SHORT_BIG_INT = 7,/**< 短大整数（优化小 BigInt）*/
    JS_TAG_FLOAT64     = 8,  /**< 64 位浮点数 */
    /* 任何更大的标签在 NaN-boxing 模式下都是 FLOAT64 */
};

/**
 * @brief 引用计数头结构
 * @note 所有带引用计数的对象都以此开头
 */
typedef struct JSRefCountHeader {
    int ref_count;  /**< 引用计数，为 0 时释放对象 */
} JSRefCountHeader;

/* ==========================================================================
 * JS 值表示 - 三种实现策略
 * 
 * 1. CONFIG_CHECK_JSVALUE: 调试模式，带类型检查
 * 2. JS_NAN_BOXING: 32 位系统优化，使用 NaN-boxing
 * 3. 默认：64 位系统，使用联合体
 * ========================================================================== */

#ifdef CONFIG_CHECK_JSVALUE
/* 
 * 调试模式：JSValue 是指针类型
 * 用于检测引用计数错误，不能用于实际运行
 */
typedef struct __JSValue *JSValue;
typedef const struct __JSValue *JSValueConst;

#define JS_VALUE_GET_TAG(v) (int)((uintptr_t)(v) & 0xf)
#define JS_VALUE_GET_INT(v) (int)((intptr_t)(v) >> 4)
#define JS_VALUE_GET_BOOL(v) JS_VALUE_GET_INT(v)
#define JS_VALUE_GET_FLOAT64(v) (double)JS_VALUE_GET_INT(v)
#define JS_VALUE_GET_SHORT_BIG_INT(v) JS_VALUE_GET_INT(v)
#define JS_VALUE_GET_PTR(v) (void *)((intptr_t)(v) & ~0xf)

#define JS_MKVAL(tag, val) (JSValue)(intptr_t)(((val) << 4) | (tag))
#define JS_MKPTR(tag, p) (JSValue)((intptr_t)(p) | (tag))

#define JS_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == JS_TAG_FLOAT64)
#define JS_NAN JS_MKVAL(JS_TAG_FLOAT64, 1)

#elif defined(JS_NAN_BOXING)
/*
 * NaN-boxing 模式：32 位系统优化
 * 利用 IEEE 754 NaN 的未使用位存储其他类型
 * 
 * 64 位布局：
 * [32 位标签] [32 位值]
 * 对于浮点数：编码为特殊的 NaN 值
 */
typedef uint64_t JSValue;
#define JSValueConst JSValue

#define JS_VALUE_GET_TAG(v) (int)((v) >> 32)        /**< 获取高 32 位标签 */
#define JS_VALUE_GET_INT(v) (int)(v)                /**< 获取低 32 位整数 */
#define JS_VALUE_GET_BOOL(v) (int)(v)
#define JS_VALUE_GET_SHORT_BIG_INT(v) (int)(v)
#define JS_VALUE_GET_PTR(v) (void *)(intptr_t)(v)

/** 构造标签 + 整数值 */
#define JS_MKVAL(tag, val) (((uint64_t)(tag) << 32) | (uint32_t)(val))
/** 构造标签 + 指针值 */
#define JS_MKPTR(tag, ptr) (((uint64_t)(tag) << 32) | (uintptr_t)(ptr))

/** NaN 编码偏移量 - 用于区分 NaN 和其他浮点数 */
#define JS_FLOAT64_TAG_ADDEND (0x7ff80000 - JS_TAG_FIRST + 1)

/** 获取浮点数值 - 需要特殊解码 */
static inline double JS_VALUE_GET_FLOAT64(JSValue v)
{
    union {
        JSValue v;
        double d;
    } u;
    u.v = v;
    u.v += (uint64_t)JS_FLOAT64_TAG_ADDEND << 32;
    return u.d;
}

/** NaN 的 NaN-boxing 表示 */
#define JS_NAN (0x7ff8000000000000 - ((uint64_t)JS_FLOAT64_TAG_ADDEND << 32))

/** 创建浮点数 */
static inline JSValue __JS_NewFloat64(JSContext *ctx, double d)
{
    union {
        double d;
        uint64_t u64;
    } u;
    JSValue v;
    u.d = d;
    /* 规范化 NaN - 确保所有 NaN 使用相同表示 */
    if (js_unlikely((u.u64 & 0x7fffffffffffffff) > 0x7ff0000000000000))
        v = JS_NAN;
    else
        v = u.u64 - ((uint64_t)JS_FLOAT64_TAG_ADDEND << 32);
    return v;
}

/** 检查标签是否为浮点数类型 */
#define JS_TAG_IS_FLOAT64(tag) ((unsigned)((tag) - JS_TAG_FIRST) >= (JS_TAG_FLOAT64 - JS_TAG_FIRST))

/** 获取标准化标签 - 将所有浮点标签统一为 JS_TAG_FLOAT64 */
static inline int JS_VALUE_GET_NORM_TAG(JSValue v)
{
    uint32_t tag;
    tag = JS_VALUE_GET_TAG(v);
    if (JS_TAG_IS_FLOAT64(tag))
        return JS_TAG_FLOAT64;
    else
        return tag;
}

/** 检查是否为 NaN */
static inline JS_BOOL JS_VALUE_IS_NAN(JSValue v)
{
    uint32_t tag;
    tag = JS_VALUE_GET_TAG(v);
    return tag == (JS_NAN >> 32);
}

#else /* !JS_NAN_BOXING - 64 位默认模式 */
/*
 * 默认模式：使用联合体
 * 64 位系统有足够空间直接存储标签和值
 */
typedef union JSValueUnion {
    int32_t int32;          /**< 32 位整数 */
    double float64;         /**< 64 位浮点数 */
    void *ptr;              /**< 指针（对象、字符串等）*/
#if JS_SHORT_BIG_INT_BITS == 32
    int32_t short_big_int;  /**< 短大整数 */
#else
    int64_t short_big_int;
#endif
} JSValueUnion;

/**
 * @brief JSValue - JavaScript 值的内部表示
 * 
 * 使用结构体而非联合体，因为：
 * 1. 64 位系统有足够空间
 * 2. 代码更清晰，易于调试
 * 3. 编译器优化足够好
 */
typedef struct JSValue {
    JSValueUnion u;     /**< 值联合体 */
    int64_t tag;        /**< 类型标签 */
} JSValue;

#define JSValueConst JSValue

#define JS_VALUE_GET_TAG(v) ((int32_t)(v).tag)      /**< 获取标签 */
#define JS_VALUE_GET_NORM_TAG(v) JS_VALUE_GET_TAG(v)
#define JS_VALUE_GET_INT(v) ((v).u.int32)           /**< 获取整数值 */
#define JS_VALUE_GET_BOOL(v) ((v).u.int32)          /**< 获取布尔值 */
#define JS_VALUE_GET_FLOAT64(v) ((v).u.float64)     /**< 获取浮点数值 */
#define JS_VALUE_GET_SHORT_BIG_INT(v) ((v).u.short_big_int)
#define JS_VALUE_GET_PTR(v) ((v).u.ptr)             /**< 获取指针值 */

/** 构造整数值 */
#define JS_MKVAL(tag, val) (JSValue){ (JSValueUnion){ .int32 = val }, tag }
/** 构造指针值 */
#define JS_MKPTR(tag, p) (JSValue){ (JSValueUnion){ .ptr = p }, tag }

#define JS_TAG_IS_FLOAT64(tag) ((unsigned)(tag) == JS_TAG_FLOAT64)
#define JS_NAN (JSValue){ .u.float64 = JS_FLOAT64_NAN, JS_TAG_FLOAT64 }

/** 创建浮点数 */
static inline JSValue __JS_NewFloat64(JSContext *ctx, double d)
{
    JSValue v;
    v.tag = JS_TAG_FLOAT64;
    v.u.float64 = d;
    return v;
}

/** 检查是否为 NaN */
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

#endif /* !JS_NAN_BOXING */

/* ==========================================================================
 * 常用值检查宏
 * ========================================================================== */
/** 检查两个值是否都是整数 - 用于快速路径优化 */
#define JS_VALUE_IS_BOTH_INT(v1, v2) ((JS_VALUE_GET_TAG(v1) | JS_VALUE_GET_TAG(v2)) == 0)
/** 检查两个值是否都是浮点数 */
#define JS_VALUE_IS_BOTH_FLOAT(v1, v2) (JS_TAG_IS_FLOAT64(JS_VALUE_GET_TAG(v1)) && JS_TAG_IS_FLOAT64(JS_VALUE_GET_TAG(v2)))

/** 检查值是否有引用计数 - 负标签表示需要 GC 管理 */
#define JS_VALUE_HAS_REF_COUNT(v) ((unsigned)JS_VALUE_GET_TAG(v) >= (unsigned)JS_TAG_FIRST)

/* ==========================================================================
 * 特殊值常量
 * ========================================================================== */
#define JS_NULL      JS_MKVAL(JS_TAG_NULL, 0)       /**< null */
#define JS_UNDEFINED JS_MKVAL(JS_TAG_UNDEFINED, 0)  /**< undefined */
#define JS_FALSE     JS_MKVAL(JS_TAG_BOOL, 0)       /**< false */
#define JS_TRUE      JS_MKVAL(JS_TAG_BOOL, 1)       /**< true */
#define JS_EXCEPTION JS_MKVAL(JS_TAG_EXCEPTION, 0)  /**< 异常标记 */
#define JS_UNINITIALIZED JS_MKVAL(JS_TAG_UNINITIALIZED, 0) /**< 未初始化 */

/* ==========================================================================
 * 对象属性标志
 * 
 * 属性描述符标志位：
 * - 位 0-1: configurable, writable
 * - 位 2: enumerable
 * - 位 3: length (内部使用)
 * - 位 4-5: 属性类型 (NORMAL, GETSET, VARREF, AUTOINIT)
 * ========================================================================== */
#define JS_PROP_CONFIGURABLE  (1 << 0)  /**< 属性可配置（可删除、可修改）*/
#define JS_PROP_WRITABLE      (1 << 1)  /**< 属性可写 */
#define JS_PROP_ENUMERABLE    (1 << 2)  /**< 属性可枚举（for-in 遍历）*/
#define JS_PROP_C_W_E         (JS_PROP_CONFIGURABLE | JS_PROP_WRITABLE | JS_PROP_ENUMERABLE)
#define JS_PROP_LENGTH        (1 << 3)  /**< 用于数组 length 属性（内部）*/
#define JS_PROP_TMASK         (3 << 4)  /**< 属性类型掩码 */

#define JS_PROP_NORMAL         (0 << 4) /**< 普通数据属性 */
#define JS_PROP_GETSET         (1 << 4) /**< 访问器属性（getter/setter）*/
#define JS_PROP_VARREF         (2 << 4) /**< 变量引用（内部使用）*/
#define JS_PROP_AUTOINIT       (3 << 4) /**< 自动初始化（内部使用）*/

/* JS_DefineProperty 的标志 */
#define JS_PROP_HAS_SHIFT        8
#define JS_PROP_HAS_CONFIGURABLE (1 << 8)  /**< 指定 configurable */
#define JS_PROP_HAS_WRITABLE     (1 << 9)  /**< 指定 writable */
#define JS_PROP_HAS_ENUMERABLE   (1 << 10) /**< 指定 enumerable */
#define JS_PROP_HAS_GET          (1 << 11) /**< 指定 getter */
#define JS_PROP_HAS_SET          (1 << 12) /**< 指定 setter */
#define JS_PROP_HAS_VALUE        (1 << 13) /**< 指定 value */

/** 如果返回 false 则抛出异常 */
#define JS_PROP_THROW            (1 << 14)
/** 仅在严格模式下抛出异常 */
#define JS_PROP_THROW_STRICT     (1 << 15)

#define JS_PROP_NO_EXOTIC        (1 << 16) /**< 不使用特殊对象行为（内部）*/

/* ==========================================================================
 * 栈大小配置
 * ========================================================================== */
#ifndef JS_DEFAULT_STACK_SIZE
#define JS_DEFAULT_STACK_SIZE (1024 * 1024)  /**< 默认栈大小：1MB */
#endif

/* ==========================================================================
 * JS_Eval() 编译标志
 * ========================================================================== */
#define JS_EVAL_TYPE_GLOBAL   (0 << 0)  /**< 全局代码（默认）*/
#define JS_EVAL_TYPE_MODULE   (1 << 0)  /**< 模块代码 */
#define JS_EVAL_TYPE_DIRECT   (2 << 0)  /**< 直接调用（内部）*/
#define JS_EVAL_TYPE_INDIRECT (3 << 0)  /**< 间接调用（内部）*/
#define JS_EVAL_TYPE_MASK     (3 << 0)

#define JS_EVAL_FLAG_STRICT   (1 << 3)  /**< 强制严格模式 */
/** 仅编译不执行 - 返回字节码对象 */
#define JS_EVAL_FLAG_COMPILE_ONLY (1 << 5)
/** 不在 Error 堆栈中包含此 eval 之前的帧 */
#define JS_EVAL_FLAG_BACKTRACE_BARRIER (1 << 6)
/** 允许顶层 await - 仅与 JS_EVAL_TYPE_GLOBAL 一起使用 */
#define JS_EVAL_FLAG_ASYNC (1 << 7)

/* ==========================================================================
 * C 函数绑定类型
 * ========================================================================== */

/** 标准 C 函数签名 */
typedef JSValue JSCFunction(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

/** 带 magic 值的 C 函数 - 用于共享处理函数 */
typedef JSValue JSCFunctionMagic(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic);

/** 带函数数据的 C 函数 - 用于闭包 */
typedef JSValue JSCFunctionData(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv, int magic, JSValue *func_data);

/* ==========================================================================
 * 内存管理结构
 * ========================================================================== */

/** 内存分配状态 */
typedef struct JSMallocState {
    size_t malloc_count;   /**< 分配次数 */
    size_t malloc_size;    /**< 当前分配总量 */
    size_t malloc_limit;   /**< 分配限制 */
    void *opaque;          /**< 用户不透明指针 */
} JSMallocState;

/** 内存分配函数表 - 用于自定义内存管理 */
typedef struct JSMallocFunctions {
    void *(*js_malloc)(JSMallocState *s, size_t size);
    void (*js_free)(JSMallocState *s, void *ptr);
    void *(*js_realloc)(JSMallocState *s, void *ptr, size_t size);
    size_t (*js_malloc_usable_size)(const void *ptr);
} JSMallocFunctions;

typedef struct JSGCObjectHeader JSGCObjectHeader;  /**< GC 对象头（前向声明）*/

/* ==========================================================================
 * 运行时 API - 核心函数声明
 * ========================================================================== */

/**
 * @brief 创建新的运行时实例
 * @return JSRuntime 指针，失败返回 NULL
 * @note 每个进程通常只创建一个运行时
 */
JSRuntime *JS_NewRuntime(void);

/**
 * @brief 设置运行时信息（用于调试）
 * @param rt 运行时实例
 * @param info 信息字符串，生命周期必须超过 rt
 */
void JS_SetRuntimeInfo(JSRuntime *rt, const char *info);

/**
 * @brief 设置内存限制
 * @param limit 最大内存字节数
 */
void JS_SetMemoryLimit(JSRuntime *rt, size_t limit);

/**
 * @brief 设置 GC 阈值
 * @param gc_threshold 触发 GC 的内存阈值
 */
void JS_SetGCThreshold(JSRuntime *rt, size_t gc_threshold);

/**
 * @brief 设置最大栈大小
 * @param stack_size 栈大小字节数，0 表示禁用检查
 */
void JS_SetMaxStackSize(JSRuntime *rt, size_t stack_size);

/**
 * @brief 更新栈顶指针 - 切换线程时调用
 * @note 用于检测栈溢出
 */
void JS_UpdateStackTop(JSRuntime *rt);

/**
 * @brief 使用自定义内存分配器创建运行时
 * @param mf 内存分配函数表
 * @param opaque 用户不透明指针，传递给分配器
 */
JSRuntime *JS_NewRuntime2(const JSMallocFunctions *mf, void *opaque);

/**
 * @brief 释放运行时
 * @note 必须先释放所有关联的 Context
 */
void JS_FreeRuntime(JSRuntime *rt);

/**
 * @brief 获取运行时不透明指针
 */
void *JS_GetRuntimeOpaque(JSRuntime *rt);

/**
 * @brief 设置运行时不透明指针
 */
void JS_SetRuntimeOpaque(JSRuntime *rt, void *opaque);

/** GC 标记函数类型 */
typedef void JS_MarkFunc(JSRuntime *rt, JSGCObjectHeader *gp);

/**
 * @brief 标记 JS 值 - GC 过程中使用
 * @param val 要标记的值
 * @param mark_func 标记函数
 */
void JS_MarkValue(JSRuntime *rt, JSValueConst val, JS_MarkFunc *mark_func);

/**
 * @brief 手动运行 GC
 */
void JS_RunGC(JSRuntime *rt);

/**
 * @brief 检查对象是否存活
 * @return JS_TRUE 如果对象仍然可达
 */
JS_BOOL JS_IsLiveObject(JSRuntime *rt, JSValueConst obj);

/* ==========================================================================
 * 上下文 API
 * ========================================================================== */

/**
 * @brief 创建新的执行上下文
 * @param rt 运行时实例
 * @return JSContext 指针
 * @note 每个 Context 对应一个独立的 JS 环境
 */
JSContext *JS_NewContext(JSRuntime *rt);

/**
 * @brief 释放上下文
 */
void JS_FreeContext(JSContext *s);

/**
 * @brief 增加上下文引用计数
 * @note 用于多个所有者共享 Context
 */
JSContext *JS_DupContext(JSContext *ctx);

/** 获取/设置上下文不透明指针 */
void *JS_GetContextOpaque(JSContext *ctx);
void JS_SetContextOpaque(JSContext *ctx, void *opaque);

/** 获取上下文关联的运行时 */
JSRuntime *JS_GetRuntime(JSContext *ctx);

/** 设置/获取类的原型对象 */
void JS_SetClassProto(JSContext *ctx, JSClassID class_id, JSValue obj);
JSValue JS_GetClassProto(JSContext *ctx, JSClassID class_id);

/* ==========================================================================
 * 内置对象懒加载
 * 
 * 为节省内存，内置对象按需加载
 * ========================================================================== */

/** 创建裸上下文 - 不包含任何内置对象 */
JSContext *JS_NewContextRaw(JSRuntime *rt);

/** 添加基础内置对象（Object, Function, Array 等）*/
int JS_AddIntrinsicBaseObjects(JSContext *ctx);
int JS_AddIntrinsicDate(JSContext *ctx);          /**< Date */
int JS_AddIntrinsicEval(JSContext *ctx);          /**< eval */
int JS_AddIntrinsicStringNormalize(JSContext *ctx); /**< 字符串规范化 */
void JS_AddIntrinsicRegExpCompiler(JSContext *ctx); /**< 正则编译器 */
int JS_AddIntrinsicRegExp(JSContext *ctx);        /**< RegExp */
int JS_AddIntrinsicJSON(JSContext *ctx);          /**< JSON */
int JS_AddIntrinsicProxy(JSContext *ctx);         /**< Proxy */
int JS_AddIntrinsicMapSet(JSContext *ctx);        /**< Map, Set */
int JS_AddIntrinsicTypedArrays(JSContext *ctx);   /**< TypedArray */
int JS_AddIntrinsicPromise(JSContext *ctx);       /**< Promise */
int JS_AddIntrinsicWeakRef(JSContext *ctx);       /**< WeakRef, FinalizationRegistry */

/* ==========================================================================
 * 内存分配 API
 * ========================================================================== */

/* 运行时级别的分配 */
void *js_malloc_rt(JSRuntime *rt, size_t size);
void js_free_rt(JSRuntime *rt, void *ptr);
void *js_realloc_rt(JSRuntime *rt, void *ptr, size_t size);
size_t js_malloc_usable_size_rt(JSRuntime *rt, const void *ptr);
void *js_mallocz_rt(JSRuntime *rt, size_t size);  /**< 分配并清零 */

/* 上下文级别的分配 */
void *js_malloc(JSContext *ctx, size_t size);
void js_free(JSContext *ctx, void *ptr);
void *js_realloc(JSContext *ctx, void *ptr, size_t size);
size_t js_malloc_usable_size(JSContext *ctx, const void *ptr);
void *js_realloc2(JSContext *ctx, void *ptr, size_t size, size_t *pslack);
void *js_mallocz(JSContext *ctx, size_t size);    /**< 分配并清零 */
char *js_strdup(JSContext *ctx, const char *str); /**< 字符串复制 */
char *js_strndup(JSContext *ctx, const char *s, size_t n); /**< 限制长度复制 */

/* ==========================================================================
 * 内存使用统计
 * ========================================================================== */

typedef struct JSMemoryUsage {
    int64_t malloc_size, malloc_limit, memory_used_size;
    int64_t malloc_count;
    int64_t memory_used_count;
    int64_t atom_count, atom_size;      /**< 原子统计 */
    int64_t str_count, str_size;        /**< 字符串统计 */
    int64_t obj_count, obj_size;        /**< 对象统计 */
    int64_t prop_count, prop_size;      /**< 属性统计 */
    int64_t shape_count, shape_size;    /**< 形状统计 */
    int64_t js_func_count, js_func_size, js_func_code_size; /**< 函数统计 */
    int64_t js_func_pc2line_count, js_func_pc2line_size;
    int64_t c_func_count, array_count;
    int64_t fast_array_count, fast_array_elements;
    int64_t binary_object_count, binary_object_size;
} JSMemoryUsage;

/** 计算内存使用情况 */
void JS_ComputeMemoryUsage(JSRuntime *rt, JSMemoryUsage *s);

/** 打印内存使用报告 */
void JS_DumpMemoryUsage(FILE *fp, const JSMemoryUsage *s, JSRuntime *rt);

/* ==========================================================================
 * 原子（Atom）API
 * 
 * Atom 是字符串的内部表示，用于属性名、变量名等
 * 特点：唯一化、快速比较、引用计数
 * ========================================================================== */

#define JS_ATOM_NULL 0  /**< 空原子 */

/** 从字符串创建原子 */
JSAtom JS_NewAtomLen(JSContext *ctx, const char *str, size_t len);
JSAtom JS_NewAtom(JSContext *ctx, const char *str);
JSAtom JS_NewAtomUInt32(JSContext *ctx, uint32_t n);  /**< 从数字创建原子 */

/** 增加原子引用计数 */
JSAtom JS_DupAtom(JSContext *ctx, JSAtom v);

/** 释放原子 */
void JS_FreeAtom(JSContext *ctx, JSAtom v);
void JS_FreeAtomRT(JSRuntime *rt, JSAtom v);

/** 原子与值的转换 */
JSValue JS_AtomToValue(JSContext *ctx, JSAtom atom);
JSValue JS_AtomToString(JSContext *ctx, JSAtom atom);
const char *JS_AtomToCStringLen(JSContext *ctx, size_t *plen, JSAtom atom);
static inline const char *JS_AtomToCString(JSContext *ctx, JSAtom atom)
{
    return JS_AtomToCStringLen(ctx, NULL, atom);
}

/** 值转原子 */
JSAtom JS_ValueToAtom(JSContext *ctx, JSValueConst val);

/* ==========================================================================
 * 对象类系统
 * ========================================================================== */

/** 属性枚举项 */
typedef struct JSPropertyEnum {
    JS_BOOL is_enumerable;
    JSAtom atom;
} JSPropertyEnum;

/** 属性描述符 */
typedef struct JSPropertyDescriptor {
    int flags;
    JSValue value;
    JSValue getter;
    JSValue setter;
} JSPropertyDescriptor;

/**
 * @brief 特殊对象方法表
 * @note 用于 Proxy、TypedArray 等特殊对象
 */
typedef struct JSClassExoticMethods {
    /**
     * @brief 获取自身属性
     * @return -1 异常，FALSE 不存在，TRUE 存在
     */
    int (*get_own_property)(JSContext *ctx, JSPropertyDescriptor *desc,
                             JSValueConst obj, JSAtom prop);
    
    /**
     * @brief 获取所有自身属性名
     * @param ptab 输出属性表
     * @param plen 属性数量
     */
    int (*get_own_property_names)(JSContext *ctx, JSPropertyEnum **ptab,
                                  uint32_t *plen,
                                  JSValueConst obj);
    
    int (*delete_property)(JSContext *ctx, JSValueConst obj, JSAtom prop);
    int (*define_own_property)(JSContext *ctx, JSValueConst this_obj,
                               JSAtom prop, JSValueConst val,
                               JSValueConst getter, JSValueConst setter,
                               int flags);
    int (*has_property)(JSContext *ctx, JSValueConst obj, JSAtom atom);
    JSValue (*get_property)(JSContext *ctx, JSValueConst obj, JSAtom atom,
                            JSValueConst receiver);
    int (*set_property)(JSContext *ctx, JSValueConst obj, JSAtom atom,
                        JSValueConst value, JSValueConst receiver, int flags);
    JSValue (*get_prototype)(JSContext *ctx, JSValueConst obj);
    int (*set_prototype)(JSContext *ctx, JSValueConst obj, JSValueConst proto_val);
    int (*is_extensible)(JSContext *ctx, JSValueConst obj);
    int (*prevent_extensions)(JSContext *ctx, JSValueConst obj);
} JSClassExoticMethods;

/** 类终结器 - 对象销毁时调用 */
typedef void JSClassFinalizer(JSRuntime *rt, JSValue val);

/** GC 标记函数 */
typedef void JSClassGCMark(JSRuntime *rt, JSValueConst val,
                           JS_MarkFunc *mark_func);

#define JS_CALL_FLAG_CONSTRUCTOR (1 << 0)  /**< 构造函数调用标志 */

/** 类调用函数 - 对象作为函数调用时执行 */
typedef JSValue JSClassCall(JSContext *ctx, JSValueConst func_obj,
                            JSValueConst this_val, int argc, JSValueConst *argv,
                            int flags);

/**
 * @brief 类定义结构
 * @note 用于定义自定义对象类型
 */
typedef struct JSClassDef {
    const char *class_name;           /**< 类名（用于调试）*/
    JSClassFinalizer *finalizer;      /**< 终结器 */
    JSClassGCMark *gc_mark;           /**< GC 标记函数 */
    JSClassCall *call;                /**< 调用函数（如果是函数对象）*/
    JSClassExoticMethods *exotic;     /**< 特殊方法（可选）*/
} JSClassDef;

#define JS_INVALID_CLASS_ID 0  /**< 无效类 ID */

/** 分配新的类 ID */
JSClassID JS_NewClassID(JSClassID *pclass_id);

/** 获取对象的类 ID */
JSClassID JS_GetClassID(JSValue v);

/** 注册新类 */
int JS_NewClass(JSRuntime *rt, JSClassID class_id, const JSClassDef *class_def);

/** 检查类是否已注册 */
int JS_IsRegisteredClass(JSRuntime *rt, JSClassID class_id);

/* ==========================================================================
 * 值操作 API - 内联函数
 * ========================================================================== */

/** 创建布尔值 */
static js_force_inline JSValue JS_NewBool(JSContext *ctx, JS_BOOL val)
{
    return JS_MKVAL(JS_TAG_BOOL, (val != 0));
}

/** 创建 32 位整数 */
static js_force_inline JSValue JS_NewInt32(JSContext *ctx, int32_t val)
{
    return JS_MKVAL(JS_TAG_INT, val);
}

/** 创建 catch 偏移（内部使用）*/
static js_force_inline JSValue JS_NewCatchOffset(JSContext *ctx, int32_t val)
{
    return JS_MKVAL(JS_TAG_CATCH_OFFSET, val);
}

/** 创建 64 位整数 - 自动选择 Int32 或 Float64 表示 */
static js_force_inline JSValue JS_NewInt64(JSContext *ctx, int64_t val)
{
    JSValue v;
    if (val == (int32_t)val) {
        v = JS_NewInt32(ctx, val);
    } else {
        v = __JS_NewFloat64(ctx, val);
    }
    return v;
}

/** 创建无符号 32 位整数 */
static js_force_inline JSValue JS_NewUint32(JSContext *ctx, uint32_t val)
{
    JSValue v;
    if (val <= 0x7fffffff) {
        v = JS_NewInt32(ctx, val);
    } else {
        v = __JS_NewFloat64(ctx, val);
    }
    return v;
}

/** 创建 BigInt64 */
JSValue JS_NewBigInt64(JSContext *ctx, int64_t v);

/** 创建 BigUint64 */
JSValue JS_NewBigUint64(JSContext *ctx, uint64_t v);

/**
 * @brief 创建浮点数 - 智能选择 Int32 或 Float64 表示
 * 
 * 优化：如果浮点数可以精确表示为整数，则使用 Int32 存储
 * 例如：1.0 存储为整数而非浮点数
 */
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
        /* -0 不能表示为整数，所以比较位表示 */
        if (u.u == t.u)
            return JS_MKVAL(JS_TAG_INT, val);
    }
    return __JS_NewFloat64(ctx, d);
}

/* ==========================================================================
 * 类型检查函数
 * ========================================================================== */

static inline JS_BOOL JS_IsNumber(JSValueConst v)
{
    int tag = JS_VALUE_GET_TAG(v);
    return tag == JS_TAG_INT || JS_TAG_IS_FLOAT64(tag);
}

static inline JS_BOOL JS_IsBigInt(JSContext *ctx, JSValueConst v)
{
    int tag = JS_VALUE_GET_TAG(v);
    return tag == JS_TAG_BIG_INT || tag == JS_TAG_SHORT_BIG_INT;
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

/** 检查是否为异常 - 使用分支预测优化 */
static inline JS_BOOL JS_IsException(JSValueConst v)
{
    return js_unlikely(JS_VALUE_GET_TAG(v) == JS_TAG_EXCEPTION);
}

/** 检查是否未初始化 */
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

/* ==========================================================================
 * 异常处理 API
 * ========================================================================== */

/** 抛出异常 */
JSValue JS_Throw(JSContext *ctx, JSValue obj);

/** 设置异常为不可捕获（用于内部错误）*/
void JS_SetUncatchableException(JSContext *ctx, JS_BOOL flag);

/** 获取当前异常 */
JSValue JS_GetException(JSContext *ctx);

/** 检查是否有待处理异常 */
JS_BOOL JS_HasException(JSContext *ctx);

/** 检查是否为 Error 对象 */
JS_BOOL JS_IsError(JSContext *ctx, JSValueConst val);

/** 创建 Error 对象 */
JSValue JS_NewError(JSContext *ctx);

/** 创建各种类型的错误 */
JSValue __js_printf_like(2, 3) JS_ThrowSyntaxError(JSContext *ctx, const char *fmt, ...);
JSValue __js_printf_like(2, 3) JS_ThrowTypeError(JSContext *ctx, const char *fmt, ...);
JSValue __js_printf_like(2, 3) JS_ThrowReferenceError(JSContext *ctx, const char *fmt, ...);
JSValue __js_printf_like(2, 3) JS_ThrowRangeError(JSContext *ctx, const char *fmt, ...);
JSValue __js_printf_like(2, 3) JS_ThrowInternalError(JSContext *ctx, const char *fmt, ...);
JSValue JS_ThrowOutOfMemory(JSContext *ctx);

/* ==========================================================================
 * 引用计数管理
 * ========================================================================== */

/** 内部释放函数 */
void __JS_FreeValue(JSContext *ctx, JSValue v);

/**
 * @brief 释放值
 * 
 * 引用计数减 1，如果为 0 则真正释放
 * 使用内联函数优化常见路径
 */
static inline void JS_FreeValue(JSContext *ctx, JSValue v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        if (--p->ref_count <= 0) {
            __JS_FreeValue(ctx, v);
        }
    }
}

void __JS_FreeValueRT(JSRuntime *rt, JSValue v);

static inline void JS_FreeValueRT(JSRuntime *rt, JSValue v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        if (--p->ref_count <= 0) {
            __JS_FreeValueRT(rt, v);
        }
    }
}

/**
 * @brief 增加值的引用计数
 * @note 返回值以便链式调用：JS_DupValue(ctx, v)
 */
static inline JSValue JS_DupValue(JSContext *ctx, JSValueConst v)
{
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        p->ref_count++;
    }
    return (JSValue)v;
}

/* [文件继续... 由于篇幅限制，这里只注释了关键部分] */

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_H */
