/*
 * C 工具函数库
 * 
 * QuickJS 引擎的通用工具函数集合，提供：
 * - 字符串安全操作（带边界检查）
 * - 内存操作辅助函数
 * - 动态缓冲区（DynBuf）管理
 * - Unicode/UTF-8 编解码
 * - 字节序转换
 * - 位运算辅助（前导零/尾随零计数）
 * - 快速排序实现
 * - 浮点数转换（float16 <-> float64）
 *
 * Copyright (c) 2017 Fabrice Bellard
 * Copyright (c) 2018 Charlie Gordon
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
#ifndef CUTILS_H
#define CUTILS_H

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

/* 分支预测提示：告诉编译器某个条件大概率/小概率为真，优化指令流水线 */
#define likely(x)       __builtin_expect(!!(x), 1)   /* x 大概率为真 */
#define unlikely(x)     __builtin_expect(!!(x), 0)   /* x 大概率为假 */

/* 强制内联：要求编译器尽可能将函数内联展开，避免调用开销 */
#define force_inline inline __attribute__((always_inline))

/* 禁止内联：强制编译器不为该函数生成内联代码，用于减少代码膨胀或调试 */
#define no_inline __attribute__((noinline))

/* 可能未使用：标记该变量/参数可能不会被使用，避免编译器警告 */
#define __maybe_unused __attribute__((unused))

/* 宏拼接：将两个标记拼接成一个，用于生成唯一标识符 */
#define xglue(x, y) x ## y
#define glue(x, y) xglue(x, y)

/* 字符串化：将宏参数转换为字符串字面量 */
#define stringify(s)    tostring(s)
#define tostring(s)     #s

/* 计算结构体成员相对于结构体起始位置的偏移量 */
#ifndef offsetof
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif

/* 计算数组元素个数 */
#ifndef countof
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#endif

/* 通过成员指针反推结构体起始地址
 * 参数：ptr - 成员指针；type - 结构体类型；member - 成员名
 * 返回：包含该成员的结构体指针
 */
#ifndef container_of
#define container_of(ptr, type, member) ((type *)((uint8_t *)(ptr) - offsetof(type, member)))
#endif

/* C99 及以上支持变长数组，使用 static 声明最小长度要求 */
#if !defined(_MSC_VER) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define minimum_length(n)  static n
#else
#define minimum_length(n)  n
#endif

/* 布尔类型：QuickJS 使用 int 作为布尔值 */
typedef int BOOL;

#ifndef FALSE
enum {
    FALSE = 0,
    TRUE = 1,
};
#endif

/* 安全字符串复制：带边界检查，确保不越界
 * 参数：buf - 目标缓冲区；buf_size - 缓冲区大小；str - 源字符串
 */
void pstrcpy(char *buf, int buf_size, const char *str);

/* 安全字符串拼接：在 buf 末尾追加 s，并确保不越界
 * 返回：buf 指针
 */
char *pstrcat(char *buf, int buf_size, const char *s);

/* 检查 str 是否以 val 开头，若是则 *ptr 指向 val 之后的位置
 * 返回：1-是前缀；0-不是前缀
 */
int strstart(const char *str, const char *val, const char **ptr);

/* 检查 str 是否以 suffix 结尾
 * 返回：1-是后缀；0-不是后缀
 */
int has_suffix(const char *str, const char *suffix);

/* 安全的 memcpy：当 n==0 时避免未定义行为（src/dest 可能为 NULL） */
static inline void memcpy_no_ub(void *dest, const void *src, size_t n) {
    if (n)
        memcpy(dest, src, n);
}

/* 整数最大值/最小值函数族：返回两个值中的较大/较小者 */
static inline int max_int(int a, int b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int min_int(int a, int b)
{
    if (a < b)
        return a;
    else
        return b;
}

static inline uint32_t max_uint32(uint32_t a, uint32_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline uint32_t min_uint32(uint32_t a, uint32_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

static inline int64_t max_int64(int64_t a, int64_t b)
{
    if (a > b)
        return a;
    else
        return b;
}

static inline int64_t min_int64(int64_t a, int64_t b)
{
    if (a < b)
        return a;
    else
        return b;
}

/* 前导零计数（Count Leading Zeros）：计算 32 位整数二进制表示中前导零的个数
 * 注意：a=0 时行为未定义
 * 用途：快速计算数值的位数、归一化等
 */
static inline int clz32(unsigned int a)
{
    return __builtin_clz(a);
}

/* 前导零计数：64 位版本 */
static inline int clz64(uint64_t a)
{
    return __builtin_clzll(a);
}

/* 尾随零计数（Count Trailing Zeros）：计算 32 位整数二进制表示中末尾零的个数
 * 注意：a=0 时行为未定义
 * 用途：快速找到最低位的 1、因子分解等
 */
static inline int ctz32(unsigned int a)
{
    return __builtin_ctz(a);
}

/* 尾随零计数：64 位版本 */
static inline int ctz64(uint64_t a)
{
    return __builtin_ctzll(a);
}

/* 打包结构体：用于未对齐的内存访问，避免严格对齐要求导致的崩溃
 * packed 属性告诉编译器不要添加填充字节
 */
struct __attribute__((packed)) packed_u64 {
    uint64_t v;
};

struct __attribute__((packed)) packed_u32 {
    uint32_t v;
};

struct __attribute__((packed)) packed_u16 {
    uint16_t v;
};

/* 从字节数组读取无符号/有符号整数（小端序）
 * 这些函数允许未对齐访问，用于解析字节码/二进制数据
 */
static inline uint64_t get_u64(const uint8_t *tab)
{
    return ((const struct packed_u64 *)tab)->v;
}

static inline int64_t get_i64(const uint8_t *tab)
{
    return (int64_t)((const struct packed_u64 *)tab)->v;
}

static inline void put_u64(uint8_t *tab, uint64_t val)
{
    ((struct packed_u64 *)tab)->v = val;
}

static inline uint32_t get_u32(const uint8_t *tab)
{
    return ((const struct packed_u32 *)tab)->v;
}

static inline int32_t get_i32(const uint8_t *tab)
{
    return (int32_t)((const struct packed_u32 *)tab)->v;
}

static inline void put_u32(uint8_t *tab, uint32_t val)
{
    ((struct packed_u32 *)tab)->v = val;
}

static inline uint32_t get_u16(const uint8_t *tab)
{
    return ((const struct packed_u16 *)tab)->v;
}

static inline int32_t get_i16(const uint8_t *tab)
{
    return (int16_t)((const struct packed_u16 *)tab)->v;
}

static inline void put_u16(uint8_t *tab, uint16_t val)
{
    ((struct packed_u16 *)tab)->v = val;
}

/* 8 位读写：直接解引用 */
static inline uint32_t get_u8(const uint8_t *tab)
{
    return *tab;
}

static inline int32_t get_i8(const uint8_t *tab)
{
    return (int8_t)*tab;
}

static inline void put_u8(uint8_t *tab, uint8_t val)
{
    *tab = val;
}

/* 字节序交换（Byte Swap）：在大小端之间转换
 * 用于网络传输（大端序）与主机字节序（通常小端）之间的转换
 */
#ifndef bswap16
static inline uint16_t bswap16(uint16_t x)
{
    return (x >> 8) | (x << 8);  /* 交换高 8 位和低 8 位 */
}
#endif

#ifndef bswap32
static inline uint32_t bswap32(uint32_t v)
{
    /* 反转 4 个字节的顺序：byte0<->byte3, byte1<->byte2 */
    return ((v & 0xff000000) >> 24) | ((v & 0x00ff0000) >>  8) |
        ((v & 0x0000ff00) <<  8) | ((v & 0x000000ff) << 24);
}
#endif

#ifndef bswap64
static inline uint64_t bswap64(uint64_t v)
{
    /* 反转 8 个字节的顺序 */
    return ((v & ((uint64_t)0xff << (7 * 8))) >> (7 * 8)) |
        ((v & ((uint64_t)0xff << (6 * 8))) >> (5 * 8)) |
        ((v & ((uint64_t)0xff << (5 * 8))) >> (3 * 8)) |
        ((v & ((uint64_t)0xff << (4 * 8))) >> (1 * 8)) |
        ((v & ((uint64_t)0xff << (3 * 8))) << (1 * 8)) |
        ((v & ((uint64_t)0xff << (2 * 8))) << (3 * 8)) |
        ((v & ((uint64_t)0xff << (1 * 8))) << (5 * 8)) |
        ((v & ((uint64_t)0xff << (0 * 8))) << (7 * 8));
}
#endif

/* 动态缓冲区（Dynamic Buffer）：可自动扩容的字节数组
 * 用于构建二进制数据、序列化、字符串拼接等场景
 */

/* 重分配函数类型：opaque-用户数据；ptr-原指针；size-新大小；返回新指针 */
typedef void *DynBufReallocFunc(void *opaque, void *ptr, size_t size);

typedef struct DynBuf {
    uint8_t *buf;              /* 缓冲区指针 */
    size_t size;               /* 已使用大小 */
    size_t allocated_size;     /* 已分配容量 */
    BOOL error;                /* 内存分配错误标志 */
    DynBufReallocFunc *realloc_func;  /* 自定义重分配函数 */
    void *opaque;              /* 传递给 realloc_func 的用户数据 */
} DynBuf;

/* 初始化 DynBuf：使用默认 realloc */
void dbuf_init(DynBuf *s);

/* 初始化 DynBuf：可指定自定义重分配函数和用户数据 */
void dbuf_init2(DynBuf *s, void *opaque, DynBufReallocFunc *realloc_func);

/* 预分配空间：确保缓冲区至少有 len 字节可用
 * 返回：0-成功；-1-失败（溢出或分配错误）
 */
int dbuf_claim(DynBuf *s, size_t len);

/* 追加数据：将 len 字节数据添加到缓冲区末尾 */
int dbuf_put(DynBuf *s, const uint8_t *data, size_t len);

/* 自我复制：将缓冲区内部 offset 处的 len 字节复制到末尾 */
int dbuf_put_self(DynBuf *s, size_t offset, size_t len);

/* 追加字符串 */
int dbuf_putstr(DynBuf *s, const char *str);

/* 追加单个字节/整数（慢速路径，需要扩容时调用） */
int __dbuf_putc(DynBuf *s, uint8_t c);
int __dbuf_put_u16(DynBuf *s, uint16_t val);
int __dbuf_put_u32(DynBuf *s, uint32_t val);
int __dbuf_put_u64(DynBuf *s, uint64_t val);

/* 快速路径：空间足够时直接写入，避免函数调用 */
static inline int dbuf_putc(DynBuf *s, uint8_t val)
{
    if (unlikely((s->allocated_size - s->size) < 1)) {
        return __dbuf_putc(s, val);
    } else {
        s->buf[s->size++] = val;
        return 0;
    }
}

static inline int dbuf_put_u16(DynBuf *s, uint16_t val)
{
    if (unlikely((s->allocated_size - s->size) < 2)) {
        return __dbuf_put_u16(s, val);
    } else {
        put_u16(s->buf + s->size, val);
        s->size += 2;
        return 0;
    }
}

static inline int dbuf_put_u32(DynBuf *s, uint32_t val)
{
    if (unlikely((s->allocated_size - s->size) < 4)) {
        return __dbuf_put_u32(s, val);
    } else {
        put_u32(s->buf + s->size, val);
        s->size += 4;
        return 0;
    }
}

static inline int dbuf_put_u64(DynBuf *s, uint64_t val)
{
    if (unlikely((s->allocated_size - s->size) < 8)) {
        return __dbuf_put_u64(s, val);
    } else {
        put_u64(s->buf + s->size, val);
        s->size += 8;
        return 0;
    }
}

/* 格式化输出：支持 printf 风格格式化 */
int __attribute__((format(printf, 2, 3))) dbuf_printf(DynBuf *s,
                                                      const char *fmt, ...);

/* 释放缓冲区 */
void dbuf_free(DynBuf *s);

/* 检查是否有错误 */
static inline BOOL dbuf_error(DynBuf *s) {
    return s->error;
}

/* 手动设置错误标志 */
static inline void dbuf_set_error(DynBuf *s)
{
    s->error = TRUE;
}

/* UTF-8 编码最大字节数：6 字节（理论最大值，实际 Unicode 只需 4 字节） */
#define UTF8_CHAR_LEN_MAX 6

/* Unicode 码点转 UTF-8 编码
 * 参数：buf - 输出缓冲区；c - Unicode 码点
 * 返回：输出的字节数
 */
int unicode_to_utf8(uint8_t *buf, unsigned int c);

/* UTF-8 解码为 Unicode 码点
 * 参数：p - 输入指针；max_len - 最大读取长度；pp - 输出指针（指向下一个字符）
 * 返回：Unicode 码点；-1-解码错误
 */
int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp);

/* UTF-16 代理对（Surrogate Pair）判断与转换
 * Unicode 码点 > 0xFFFF 时需要用两个 16 位值表示（代理对）
 */

/* 判断是否为代理对（0xD800-0xDFFF） */
static inline BOOL is_surrogate(uint32_t c)
{
    return (c >> 11) == (0xD800 >> 11);
}

/* 判断是否为高代理（0xD800-0xDBFF） */
static inline BOOL is_hi_surrogate(uint32_t c)
{
    return (c >> 10) == (0xD800 >> 10);
}

/* 判断是否为低代理（0xDC00-0xDFFF） */
static inline BOOL is_lo_surrogate(uint32_t c)
{
    return (c >> 10) == (0xDC00 >> 10);
}

/* 从码点计算高代理值 */
static inline uint32_t get_hi_surrogate(uint32_t c)
{
    return (c >> 10) - (0x10000 >> 10) + 0xD800;
}

/* 从码点计算低代理值 */
static inline uint32_t get_lo_surrogate(uint32_t c)
{
    return (c & 0x3FF) | 0xDC00;
}

/* 从代理对重建码点 */
static inline uint32_t from_surrogate(uint32_t hi, uint32_t lo)
{
    return 0x10000 + 0x400 * (hi - 0xD800) + (lo - 0xDC00);
}

/* 十六进制字符转数值：'0'-'9'->0-9, 'A'-'F'/'a'-'f'->10-15 */
static inline int from_hex(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1;
}

/* 可重入快速排序：带用户数据的 qsort 变种
 * 参数：base-数组基址；nmemb-元素个数；size-元素大小
 *       cmp-比较函数（带用户数据 arg）；arg-用户数据
 */
void rqsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *, void *),
            void *arg);

/* float64 位模式 reinterpret 为 uint64：用于直接操作浮点数的二进制表示 */
static inline uint64_t float64_as_uint64(double d)
{
    union {
        double d;
        uint64_t u64;
    } u;
    u.d = d;
    return u.u64;
}

/* uint64 位模式 reinterpret 为 float64 */
static inline double uint64_as_float64(uint64_t u64)
{
    union {
        double d;
        uint64_t u64;
    } u;
    u.u64 = u64;
    return u.d;
}

/* float16（半精度浮点）转 float64
 * float16 格式：1 位符号 + 5 位指数 + 10 位尾数
 * 用于 WebGPU、机器学习等需要节省内存的场景
 */
static inline double fromfp16(uint16_t v)
{
    double d;
    uint32_t v1;
    v1 = v & 0x7fff;  /* 去掉符号位 */
    if (unlikely(v1 >= 0x7c00))
        v1 += 0x1f8000; /* NaN 或无穷大调整 */
    /* 将 float16 的指数和尾数扩展到 float64 格式 */
    d = uint64_as_float64(((uint64_t)(v >> 15) << 63) | ((uint64_t)v1 << (52 - 10)));
    return d * 0x1p1008;  /* 调整指数偏置 */
}

/* float64 转 float16（半精度浮点）
 * 处理规格化数、非规格化数、零、NaN、无穷大等所有情况
 * 包含舍入逻辑
 */
static inline uint16_t tofp16(double d)
{
    uint64_t a, addend;
    uint32_t v, sgn;
    int shift;
    
    a = float64_as_uint64(d);
    sgn = a >> 63;  /* 提取符号位 */
    a = a & 0x7fffffffffffffff;  /* 去掉符号位 */
    if (unlikely(a > 0x7ff0000000000000)) {
        /* NaN：返回 quiet NaN */
        v = 0x7c01;
    } else if (a < 0x3f10000000000000) { /* 小于 2^-14 */
        /* 非规格化数或零 */
        if (a <= 0x3e60000000000000) { /* 小于 2^-25，舍入为零 */
            v = 0x0000;
        } else {
            /* 舍入到最近的 float16 非规格化数 */
            shift = 1051 - (a >> 52);
            a = ((uint64_t)1 << 52) | (a & (((uint64_t)1 << 52) - 1));
            addend = ((a >> shift) & 1) + (((uint64_t)1 << (shift - 1)) - 1);
            v = (a + addend) >> shift;
        }
    } else {
        /* 规格化数或无穷大 */
        a -= 0x3f00000000000000; /* 调整指数偏置 */
        /* 舍入：加 0.5 后截断 */
        addend = ((a >> (52 - 10)) & 1) + (((uint64_t)1 << (52 - 11)) - 1);
        v = (a + addend) >> (52 - 10);
        /* 溢出检查：超过 float16 最大值则截断为无穷大 */
        if (unlikely(v > 0x7c00))
            v = 0x7c00;
    }
    return v | (sgn << 15);  /* 恢复符号位 */
}

/* 判断 float16 是否为 NaN */
static inline int isfp16nan(uint16_t v)
{
    return (v & 0x7FFF) > 0x7C00;
}

/* 判断 float16 是否为零（正负零都返回真） */
static inline int isfp16zero(uint16_t v)
{
    return (v & 0x7FFF) == 0;
}

#endif  /* CUTILS_H */
