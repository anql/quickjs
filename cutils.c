/*
 * C 工具函数实现
 * 
 * 本文件实现 cutils.h 中声明的所有工具函数：
 * - 字符串安全操作（pstrcpy/pstrcat/strstart/has_suffix）
 * - 动态缓冲区管理（DynBuf 系列函数）
 * - Unicode/UTF-8 编解码（unicode_to_utf8/unicode_from_utf8）
 * - 可重入快速排序（rqsort）
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "cutils.h"

/* 安全字符串复制：带边界检查，确保目标缓冲区不溢出
 * 参数：buf - 目标缓冲区；buf_size - 缓冲区总大小；str - 源字符串
 * 行为：最多复制 buf_size-1 个字符，并自动添加 '\0' 终止符
 */
void pstrcpy(char *buf, int buf_size, const char *str)
{
    int c;
    char *q = buf;

    if (buf_size <= 0)
        return;

    for(;;) {
        c = *str++;
        if (c == 0 || q >= buf + buf_size - 1)  /* 遇到结束符或缓冲区满 */
            break;
        *q++ = c;
    }
    *q = '\0';  /* 确保字符串正确终止 */
}

/* 安全字符串拼接：在 buf 末尾追加 s，并确保不越界
 * 参数：buf - 目标缓冲区；buf_size - 缓冲区总大小；s - 要追加的字符串
 * 返回：buf 指针
 */
char *pstrcat(char *buf, int buf_size, const char *s)
{
    int len;
    len = strlen(buf);  /* 找到当前字符串末尾 */
    if (len < buf_size)
        pstrcpy(buf + len, buf_size - len, s);  /* 从末尾开始追加 */
    return buf;
}

/* 检查 str 是否以 val 开头
 * 参数：str - 被检查字符串；val - 期望前缀；ptr - 可选输出，指向 val 之后的位置
 * 返回：1-是前缀；0-不是前缀
 */
int strstart(const char *str, const char *val, const char **ptr)
{
    const char *p, *q;
    p = str;
    q = val;
    while (*q != '\0') {
        if (*p != *q)  /* 字符不匹配 */
            return 0;
        p++;
        q++;
    }
    if (ptr)
        *ptr = p;  /* 返回前缀之后的位置 */
    return 1;
}

/* 检查 str 是否以 suffix 结尾
 * 参数：str - 被检查字符串；suffix - 期望后缀
 * 返回：1-是后缀；0-不是后缀
 */
int has_suffix(const char *str, const char *suffix)
{
    size_t len = strlen(str);
    size_t slen = strlen(suffix);
    return (len >= slen && !memcmp(str + len - slen, suffix, slen));
}

/* ==================== 动态缓冲区（DynBuf）实现 ==================== */

/**
 * dbuf_default_realloc - 默认 DynBuf 内存重分配函数
 * 
 * 使用系统 realloc 进行内存分配，作为 DynBuf 的默认分配器。
 * 
 * @param opaque - 用户数据（未使用，保留用于未来扩展）
 * @param ptr - 原指针（NULL 时等同于 malloc）
 * @param size - 新大小（字节）
 * @return 新分配的指针；失败返回 NULL
 * 
 * 注意：
 * - size=0 时行为取决于 realloc 实现（可能返回 NULL 或可 free 的指针）
 * - 失败时原指针保持不变，需要调用者处理错误
 */
static void *dbuf_default_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

/* 初始化 DynBuf（带自定义重分配函数）
 * 参数：s - DynBuf 结构；opaque - 用户数据；realloc_func - 自定义重分配函数
 */
void dbuf_init2(DynBuf *s, void *opaque, DynBufReallocFunc *realloc_func)
{
    memset(s, 0, sizeof(*s));  /* 清零所有字段 */
    if (!realloc_func)
        realloc_func = dbuf_default_realloc;  /* 使用默认 realloc */
    s->opaque = opaque;
    s->realloc_func = realloc_func;
}

/* 初始化 DynBuf（使用默认重分配） */
void dbuf_init(DynBuf *s)
{
    dbuf_init2(s, NULL, NULL);
}

/* 预分配空间：确保缓冲区至少有 len 字节可用
 * 扩容策略：按 1.5 倍增长，避免频繁 realloc
 * 返回：0-成功；-1-失败（溢出或分配错误）
 */
int dbuf_claim(DynBuf *s, size_t len)
{
    size_t new_size, size;
    uint8_t *new_buf;
    new_size = s->size + len;
    if (new_size < len)  /* 溢出检查 */
        return -1;
    if (new_size > s->allocated_size) {  /* 需要扩容 */
        if (s->error)  /* 已有错误，拒绝分配 */
            return -1;
        /* 按 1.5 倍增长 */
        size = s->allocated_size + (s->allocated_size / 2);
        if (size < s->allocated_size)  /* 溢出检查 */
            return -1;
        if (size > new_size)
            new_size = size;  /* 使用较大的值 */
        new_buf = s->realloc_func(s->opaque, s->buf, new_size);
        if (!new_buf) {  /* 分配失败 */
            s->error = TRUE;
            return -1;
        }
        s->buf = new_buf;
        s->allocated_size = new_size;
    }
    return 0;
}

/* 追加数据：将 len 字节数据添加到缓冲区末尾
 * 自动扩容，使用 memcpy_no_ub 避免未定义行为
 * 返回：0-成功；-1-失败
 */
int dbuf_put(DynBuf *s, const uint8_t *data, size_t len)
{
    if (unlikely((s->allocated_size - s->size) < len)) {
        if (dbuf_claim(s, len))
            return -1;
    }
    memcpy_no_ub(s->buf + s->size, data, len);
    s->size += len;
    return 0;
}

/* 自我复制：将缓冲区内部 offset 处的 len 字节复制到末尾
 * 用于重复模式、数据复制等场景
 * 返回：0-成功；-1-失败
 */
int dbuf_put_self(DynBuf *s, size_t offset, size_t len)
{
    if (unlikely((s->allocated_size - s->size) < len)) {
        if (dbuf_claim(s, len))
            return -1;
    }
    memcpy(s->buf + s->size, s->buf + offset, len);
    s->size += len;
    return 0;
}

/* 以下函数是慢速路径：当空间不足时调用 */

/* 追加单个字节 */
int __dbuf_putc(DynBuf *s, uint8_t c)
{
    return dbuf_put(s, &c, 1);
}

/* 追加 16 位无符号整数 */
int __dbuf_put_u16(DynBuf *s, uint16_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 2);
}

/* 追加 32 位无符号整数 */
int __dbuf_put_u32(DynBuf *s, uint32_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 4);
}

/* 追加 64 位无符号整数 */
int __dbuf_put_u64(DynBuf *s, uint64_t val)
{
    return dbuf_put(s, (uint8_t *)&val, 8);
}

/* 追加字符串 */
int dbuf_putstr(DynBuf *s, const char *str)
{
    return dbuf_put(s, (const uint8_t *)str, strlen(str));
}

/* 格式化输出：支持 printf 风格格式化字符串
 * 优化：小字符串（<128 字节）使用栈缓冲区，避免 malloc
 * 返回：0-成功；-1-失败
 */
int __attribute__((format(printf, 2, 3))) dbuf_printf(DynBuf *s,
                                                      const char *fmt, ...)
{
    va_list ap;
    char buf[128];
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0)
        return -1;
    if (len < sizeof(buf)) {
        /* 快速路径：直接复制栈缓冲区 */
        return dbuf_put(s, (uint8_t *)buf, len);
    } else {
        /* 慢速路径：需要扩容 */
        if (dbuf_claim(s, len + 1))
            return -1;
        va_start(ap, fmt);
        vsnprintf((char *)(s->buf + s->size), s->allocated_size - s->size,
                  fmt, ap);
        va_end(ap);
        s->size += len;
    }
    return 0;
}

/* 释放 DynBuf 缓冲区
 * 调用 realloc(ptr, 0) 释放内存，并清零结构体
 * 支持重复调用（通过检查 s->buf 是否为 NULL）
 */
void dbuf_free(DynBuf *s)
{
    /* 防止重复释放导致崩溃 */
    if (s->buf) {
        s->realloc_func(s->opaque, s->buf, 0);
    }
    memset(s, 0, sizeof(*s));
}

/* ==================== Unicode/UTF-8 编解码 ==================== */

/* Unicode 码点转 UTF-8 编码
 * 参数：buf - 输出缓冲区；c - Unicode 码点（最多 31 位）
 * 返回：输出的字节数（1-6 字节）
 * 
 * UTF-8 编码规则：
 * U+0000..U+007F:   0xxxxxxx
 * U+0080..U+007FF:  110xxxxx 10xxxxxx
 * U+0800..U+FFFF:   1110xxxx 10xxxxxx 10xxxxxx
 * U+10000..U+1FFFFF: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 * ...
 */
int unicode_to_utf8(uint8_t *buf, unsigned int c)
{
    uint8_t *q = buf;

    if (c < 0x80) {
        *q++ = c;  /* ASCII：单字节 */
    } else {
        if (c < 0x800) {
            *q++ = (c >> 6) | 0xc0;  /* 2 字节序列 */
        } else {
            if (c < 0x10000) {
                *q++ = (c >> 12) | 0xe0;  /* 3 字节序列 */
            } else {
                if (c < 0x00200000) {
                    *q++ = (c >> 18) | 0xf0;  /* 4 字节序列 */
                } else {
                    if (c < 0x04000000) {
                        *q++ = (c >> 24) | 0xf8;  /* 5 字节序列 */
                    } else if (c < 0x80000000) {
                        *q++ = (c >> 30) | 0xfc;  /* 6 字节序列首字节 */
                        *q++ = ((c >> 24) & 0x3f) | 0x80;
                    } else {
                        return 0;  /* 超出编码范围 */
                    }
                    *q++ = ((c >> 18) & 0x3f) | 0x80;
                }
                *q++ = ((c >> 12) & 0x3f) | 0x80;
            }
            *q++ = ((c >> 6) & 0x3f) | 0x80;
        }
        *q++ = (c & 0x3f) | 0x80;  /* 续字节：高 2 位为 10 */
    }
    return q - buf;  /* 返回编码字节数 */
}

/* UTF-8 首字节掩码：用于提取有效位 */
static const unsigned int utf8_min_code[5] = {
    0x80, 0x800, 0x10000, 0x00200000, 0x04000000,
};

/* UTF-8 首字节有效位掩码：n 字节序列的首字节保留 (8-n-1) 位 */
static const unsigned char utf8_first_code_mask[5] = {
    0x1f, 0xf, 0x7, 0x3, 0x1,
};

/* UTF-8 解码：将 UTF-8 序列转换为 Unicode 码点
 * 参数：p - 输入指针；max_len - 最大读取长度；pp - 输出指针（指向下一字符）
 * 返回：Unicode 码点；-1-解码错误（无效序列/截断/过长）
 * 
 * 验证规则：
 * 1. 续字节必须是 10xxxxxx 格式
 * 2. 必须使用最短编码（拒绝过度编码）
 * 3. 序列长度不能超过 max_len-1
 */
int unicode_from_utf8(const uint8_t *p, int max_len, const uint8_t **pp)
{
    int l, c, b, i;

    c = *p++;
    if (c < 0x80) {
        /* ASCII：单字节，直接返回 */
        *pp = p;
        return c;
    }
    /* 根据首字节确定序列长度 */
    switch(c) {
    case 0xc0 ... 0xdf:  /* 110xxxxx：2 字节序列 */
        l = 1;
        break;
    case 0xe0 ... 0xef:  /* 1110xxxx：3 字节序列 */
        l = 2;
        break;
    case 0xf0 ... 0xf7:  /* 11110xxx：4 字节序列 */
        l = 3;
        break;
    case 0xf8 ... 0xfb:  /* 111110xx：5 字节序列 */
        l = 4;
        break;
    case 0xfc ... 0xfd:  /* 1111110x：6 字节序列 */
        l = 5;
        break;
    default:
        return -1;  /* 无效首字节（10xxxxxx 或 0xff） */
    }
    /* 检查剩余长度是否足够 */
    if (l > (max_len - 1))
        return -1;
    /* 提取首字节有效位 */
    c &= utf8_first_code_mask[l - 1];
    /* 读取续字节 */
    for(i = 0; i < l; i++) {
        b = *p++;
        if (b < 0x80 || b >= 0xc0)  /* 必须是 10xxxxxx */
            return -1;
        c = (c << 6) | (b & 0x3f);
    }
    /* 验证最短编码：拒绝过度编码 */
    if (c < utf8_min_code[l - 1])
        return -1;
    *pp = p;
    return c;
}

/* ==================== 可重入快速排序（rqsort） ==================== */
/* 
 * 注意：以下简单版本被禁用（#if 0），使用下方的优化版本
 * 简单版本使用全局变量存储比较函数参数，不可重入
 * 仅在 Emscripten 或 Android 等特殊平台使用（这些平台 qsort 性能问题）
 */
#if 0

#if defined(EMSCRIPTEN) || defined(__ANDROID__)

/**
 * rqsort_arg - 全局比较函数参数（仅用于简单版本）
 * 注意：此方法不可重入，已被优化版本废弃
 */
static void *rqsort_arg;

/**
 * rqsort_cmp - 全局比较函数指针（仅用于简单版本）
 * 注意：此方法不可重入，已被优化版本废弃
 */
static int (*rqsort_cmp)(const void *, const void *, void *);

/**
 * rqsort_cmp2 - 比较函数适配器
 * 
 * 将标准 qsort 的比较函数签名（两参数）转换为带用户数据的版本（三参数）。
 * 由于 rqsort_arg 是全局变量，此函数不可重入。
 * 
 * @param p1 - 第一个比较元素
 * @param p2 - 第二个比较元素
 * @return 比较结果：负数-p1<p2；0-p1==p2；正数-p1>p2
 */
static int rqsort_cmp2(const void *p1, const void *p2)
{
    return rqsort_cmp(p1, p2, rqsort_arg);
}

/* 简单版本：包装标准 qsort */
void rqsort(void *base, size_t nmemb, size_t size,
            int (*cmp)(const void *, const void *, void *),
            void *arg)
{
    rqsort_arg = arg;
    rqsort_cmp = cmp;
    qsort(base, nmemb, size, rqsort_cmp2);
}

#endif

#else

/* 函数指针类型定义 */
typedef void (*exchange_f)(void *a, void *b, size_t size);  /* 交换函数 */
typedef int (*cmp_f)(const void *, const void *, void *opaque);  /* 比较函数 */

/* 交换函数族：根据数据类型和对齐情况选择最优实现
 * 优化策略：按类型大小批量交换，减少循环次数
 */

/**
 * exchange_bytes - 逐字节交换（通用但最慢）
 * 
 * 当数据未对齐时使用此函数，逐字节复制交换。
 * 
 * @param a - 第一个内存区域
 * @param b - 第二个内存区域
 * @param size - 交换字节数
 */
static void exchange_bytes(void *a, void *b, size_t size) {
    uint8_t *ap = (uint8_t *)a;
    uint8_t *bp = (uint8_t *)b;

    while (size-- != 0) {
        uint8_t t = *ap;
        *ap++ = *bp;
        *bp++ = t;
    }
}

/**
 * exchange_one_byte - 单字节交换（优化版本）
 * 
 * 专门用于交换单个字节的特化版本，避免循环开销。
 * 
 * @param a - 第一个字节地址
 * @param b - 第二个字节地址
 * @param size - 固定为 1（未使用）
 */
static void exchange_one_byte(void *a, void *b, size_t size) {
    uint8_t *ap = (uint8_t *)a;
    uint8_t *bp = (uint8_t *)b;
    uint8_t t = *ap;
    *ap = *bp;
    *bp = t;
}

/**
 * exchange_int16s - 16 位整数批量交换
 * 
 * 按 uint16_t 为单位批量交换，适用于 2 字节对齐的数据。
 * 
 * @param a - 第一个内存区域
 * @param b - 第二个内存区域
 * @param size - 交换总字节数（必须是 2 的倍数）
 */
static void exchange_int16s(void *a, void *b, size_t size) {
    uint16_t *ap = (uint16_t *)a;
    uint16_t *bp = (uint16_t *)b;

    for (size /= sizeof(uint16_t); size-- != 0;) {
        uint16_t t = *ap;
        *ap++ = *bp;
        *bp++ = t;
    }
}

/**
 * exchange_one_int16 - 单 16 位整数交换（优化版本）
 * 
 * 专门用于交换单个 uint16_t 的特化版本。
 * 
 * @param a - 第一个 16 位整数地址
 * @param b - 第二个 16 位整数地址
 * @param size - 固定为 2（未使用）
 */
static void exchange_one_int16(void *a, void *b, size_t size) {
    uint16_t *ap = (uint16_t *)a;
    uint16_t *bp = (uint16_t *)b;
    uint16_t t = *ap;
    *ap = *bp;
    *bp = t;
}

/**
 * exchange_int32s - 32 位整数批量交换
 * 
 * 按 uint32_t 为单位批量交换，适用于 4 字节对齐的数据。
 * 
 * @param a - 第一个内存区域
 * @param b - 第二个内存区域
 * @param size - 交换总字节数（必须是 4 的倍数）
 */
static void exchange_int32s(void *a, void *b, size_t size) {
    uint32_t *ap = (uint32_t *)a;
    uint32_t *bp = (uint32_t *)b;

    for (size /= sizeof(uint32_t); size-- != 0;) {
        uint32_t t = *ap;
        *ap++ = *bp;
        *bp++ = t;
    }
}

/**
 * exchange_one_int32 - 单 32 位整数交换（优化版本）
 * 
 * 专门用于交换单个 uint32_t 的特化版本。
 * 
 * @param a - 第一个 32 位整数地址
 * @param b - 第二个 32 位整数地址
 * @param size - 固定为 4（未使用）
 */
static void exchange_one_int32(void *a, void *b, size_t size) {
    uint32_t *ap = (uint32_t *)a;
    uint32_t *bp = (uint32_t *)b;
    uint32_t t = *ap;
    *ap = *bp;
    *bp = t;
}

/**
 * exchange_int64s - 64 位整数批量交换
 * 
 * 按 uint64_t 为单位批量交换，适用于 8 字节对齐的数据。
 * 
 * @param a - 第一个内存区域
 * @param b - 第二个内存区域
 * @param size - 交换总字节数（必须是 8 的倍数）
 */
static void exchange_int64s(void *a, void *b, size_t size) {
    uint64_t *ap = (uint64_t *)a;
    uint64_t *bp = (uint64_t *)b;

    for (size /= sizeof(uint64_t); size-- != 0;) {
        uint64_t t = *ap;
        *ap++ = *bp;
        *bp++ = t;
    }
}

/**
 * exchange_one_int64 - 单 64 位整数交换（优化版本）
 * 
 * 专门用于交换单个 uint64_t 的特化版本。
 * 
 * @param a - 第一个 64 位整数地址
 * @param b - 第二个 64 位整数地址
 * @param size - 固定为 8（未使用）
 */
static void exchange_one_int64(void *a, void *b, size_t size) {
    uint64_t *ap = (uint64_t *)a;
    uint64_t *bp = (uint64_t *)b;
    uint64_t t = *ap;
    *ap = *bp;
    *bp = t;
}

/**
 * exchange_int128s - 128 位整数批量交换
 * 
 * 按两个 uint64_t 为单位批量交换 128 位数据，适用于 16 字节对齐。
 * 
 * @param a - 第一个内存区域
 * @param b - 第二个内存区域
 * @param size - 交换总字节数（必须是 16 的倍数）
 */
static void exchange_int128s(void *a, void *b, size_t size) {
    uint64_t *ap = (uint64_t *)a;
    uint64_t *bp = (uint64_t *)b;

    for (size /= sizeof(uint64_t) * 2; size-- != 0; ap += 2, bp += 2) {
        uint64_t t = ap[0];
        uint64_t u = ap[1];
        ap[0] = bp[0];
        ap[1] = bp[1];
        bp[0] = t;
        bp[1] = u;
    }
}

/**
 * exchange_one_int128 - 单 128 位整数交换（优化版本）
 * 
 * 专门用于交换单个 128 位数据（两个 uint64_t）的特化版本。
 * 
 * @param a - 第一个 128 位数据地址
 * @param b - 第二个 128 位数据地址
 * @param size - 固定为 16（未使用）
 */
static void exchange_one_int128(void *a, void *b, size_t size) {
    uint64_t *ap = (uint64_t *)a;
    uint64_t *bp = (uint64_t *)b;
    uint64_t t = ap[0];
    uint64_t u = ap[1];
    ap[0] = bp[0];
    ap[1] = bp[1];
    bp[0] = t;
    bp[1] = u;
}

/**
 * exchange_func - 根据对齐情况选择最优交换函数
 * 
 * 通过分析地址和大小的低 4 位，判断数据的对齐情况，选择最高效的交换策略。
 * 这是 rqsort 性能优化的关键：对齐数据可以使用更大粒度的交换（如 128 位），
 * 显著减少循环次数和内存访问次数。
 * 
 * @param base - 数组基址
 * @param size - 元素大小（字节）
 * @return 最优的交换函数指针
 * 
 * 对齐策略：
 * - 16 字节对齐：使用 128 位交换（一次交换 16 字节）
 * - 8 字节对齐：使用 64 位交换（一次交换 8 字节）
 * - 4 字节对齐：使用 32 位交换（一次交换 4 字节）
 * - 2 字节对齐：使用 16 位交换（一次交换 2 字节）
 * - 未对齐：使用字节交换（通用但最慢）
 */
static inline exchange_f exchange_func(const void *base, size_t size) {
    /* 使用低 4 位判断对齐情况 */
    switch (((uintptr_t)base | (uintptr_t)size) & 15) {
    case 0:  /* 16 字节对齐：使用 128 位交换 */
        if (size == sizeof(uint64_t) * 2)
            return exchange_one_int128;
        else
            return exchange_int128s;
    case 8:  /* 8 字节对齐：使用 64 位交换 */
        if (size == sizeof(uint64_t))
            return exchange_one_int64;
        else
            return exchange_int64s;
    case 4:  /* 4 字节对齐：使用 32 位交换 */
    case 12:
        if (size == sizeof(uint32_t))
            return exchange_one_int32;
        else
            return exchange_int32s;
    case 2:  /* 2 字节对齐：使用 16 位交换 */
    case 6:
    case 10:
    case 14:
        if (size == sizeof(uint16_t))
            return exchange_one_int16;
        else
            return exchange_int16s;
    default:  /* 未对齐：使用字节交换 */
        if (size == 1)
            return exchange_one_byte;
        else
            return exchange_bytes;
    }
}

/**
 * heapsortx - 堆排序（rqsort 的后备算法）
 * 
 * 当快速排序递归深度过大（>50 层）时切换到此算法，保证最坏情况仍为 O(n log n)。
 * 使用最大堆：堆顶始终是最大元素，依次与末尾交换并重新调整堆。
 * 
 * @param base - 数组基址
 * @param nmemb - 元素个数
 * @param size - 元素大小（字节）
 * @param cmp - 比较函数（带用户数据）
 * @param opaque - 用户数据（传递给比较函数）
 * 
 * 算法步骤：
 * 1. 构建最大堆：从最后一个非叶子节点开始，向下调整
 * 2. 排序：将堆顶（最大值）与末尾交换，缩小堆范围，重新调整
 * 3. 重复步骤 2 直到堆大小为 1
 * 
 * 时间复杂度：
 * - 最好/最坏/平均：均为 O(n log n)
 * - 空间复杂度：O(1)，原地排序
 */
static void heapsortx(void *base, size_t nmemb, size_t size, cmp_f cmp, void *opaque)
{
    uint8_t *basep = (uint8_t *)base;
    size_t i, n, c, r;
    exchange_f swap = exchange_func(base, size);

    if (nmemb > 1) {
        /* 构建最大堆 */
        i = (nmemb / 2) * size;
        n = nmemb * size;

        while (i > 0) {
            i -= size;
            for (r = i; (c = r * 2 + size) < n; r = c) {
                if (c < n - size && cmp(basep + c, basep + c + size, opaque) <= 0)
                    c += size;  /* 选择较大的子节点 */
                if (cmp(basep + r, basep + c, opaque) > 0)
                    break;
                swap(basep + r, basep + c, size);
            }
        }
        /* 排序：依次取出堆顶元素 */
        for (i = n - size; i > 0; i -= size) {
            swap(basep, basep + i, size);  /* 堆顶与末尾交换 */

            /* 重新调整堆 */
            for (r = 0; (c = r * 2 + size) < i; r = c) {
                if (c < i - size && cmp(basep + c, basep + c + size, opaque) <= 0)
                    c += size;
                if (cmp(basep + r, basep + c, opaque) > 0)
                    break;
                swap(basep + r, basep + c, size);
            }
        }
    }
}

/**
 * med3 - 三数取中（选择枢轴）
 * 
 * 从三个元素中选择中位数，用于快速排序的枢轴选择策略。
 * 相比随机选择，三数取中可以显著提高分区平衡性，避免最坏情况。
 * 
 * @param a - 第一个元素
 * @param b - 第二个元素
 * @param c - 第三个元素
 * @param cmp - 比较函数
 * @param opaque - 用户数据
 * @return 指向中位数元素的指针
 * 
 * 逻辑说明：
 * - 如果 a < b：中位数在 b 和 c 之间（取较小者），或 a 和 c 之间（取较大者）
 * - 如果 a >= b：中位数在 b 和 c 之间（取较大者），或 a 和 c 之间（取较小者）
 */
static inline void *med3(void *a, void *b, void *c, cmp_f cmp, void *opaque)
{
    return cmp(a, b, opaque) < 0 ?
        (cmp(b, c, opaque) < 0 ? b : (cmp(a, c, opaque) < 0 ? c : a )) :
        (cmp(b, c, opaque) > 0 ? b : (cmp(a, c, opaque) < 0 ? a : c ));
}

/* 可重入快速排序：优化的三路分区快速排序实现
 * 
 * 核心特性：
 * 1. 三路分区（Dutch National Flag）：将数组分为 <pivot、=pivot、>pivot 三部分
 *    优点：处理大量重复元素时效率极高
 * 2. 中位数枢轴：从 1/4、1/2、3/4 位置取三数中值，避免最坏情况
 * 3. 递归深度限制：超过 50 层切换为堆排序，保证 O(n log n) 最坏复杂度
 * 4. 小数组优化：元素数 <= 6 时使用插入排序
 * 5. 栈大小优化：先处理小区间，大区间入栈，减少栈空间使用
 * 6. 对齐优化：根据地址/大小对齐选择最优交换函数
 * 
 * 参数：base-数组基址；nmemb-元素个数；size-元素大小；cmp-比较函数；opaque-用户数据
 */
void rqsort(void *base, size_t nmemb, size_t size, cmp_f cmp, void *opaque)
{
    /* 显式栈：避免递归，每个栈帧保存待处理区间的基址、元素数、深度 */
    struct { uint8_t *base; size_t count; int depth; } stack[50], *sp = stack;
    uint8_t *ptr, *pi, *pj, *plt, *pgt, *top, *m;
    size_t m4, i, lt, gt, span, span2;
    int c, depth;
    exchange_f swap = exchange_func(base, size);
    exchange_f swap_block = exchange_func(base, size | 128);  /* 块交换优化 */

    if (nmemb < 2 || size <= 0)
        return;

    /* 初始化：将整个数组压入栈 */
    sp->base = (uint8_t *)base;
    sp->count = nmemb;
    sp->depth = 0;
    sp++;

    /* 主循环：处理栈中所有区间 */
    while (sp > stack) {
        sp--;  /* 弹出栈顶 */
        ptr = sp->base;
        nmemb = sp->count;
        depth = sp->depth;

        /* 处理当前区间 */
        while (nmemb > 6) {  /* 大区间：使用快速排序 */
            if (++depth > 50) {
                /* 深度限制：切换为堆排序，避免栈溢出和最坏情况 */
                heapsortx(ptr, nmemb, size, cmp, opaque);
                nmemb = 0;
                break;
            }
            /* 选择枢轴：从 1/4、1/2、3/4 位置取三数中值 */
            m4 = (nmemb >> 2) * size;
            m = med3(ptr + m4, ptr + 2 * m4, ptr + 3 * m4, cmp, opaque);
            swap(ptr, m, size);  /* 枢轴移到数组开头 */
            
            /* 三路分区初始化 */
            i = lt = 1;
            pi = plt = ptr + size;  /* plt 指向等于枢轴区域的末尾 */
            gt = nmemb;
            pj = pgt = top = ptr + nmemb * size;  /* pgt 指向等于枢轴区域的开头 */
            
            /* 分区主循环 */
            for (;;) {
                /* 从左向右扫描：找大于枢轴的元素 */
                while (pi < pj && (c = cmp(ptr, pi, opaque)) >= 0) {
                    if (c == 0) {  /* 等于枢轴：移到左侧 */
                        swap(plt, pi, size);
                        lt++;
                        plt += size;
                    }
                    i++;
                    pi += size;
                }
                /* 从右向左扫描：找小于枢轴的元素 */
                while (pi < (pj -= size) && (c = cmp(ptr, pj, opaque)) <= 0) {
                    if (c == 0) {  /* 等于枢轴：移到右侧 */
                        gt--;
                        pgt -= size;
                        swap(pgt, pj, size);
                    }
                }
                if (pi >= pj)
                    break;
                swap(pi, pj, size);  /* 交换 */
                i++;
                pi += size;
            }
            
            /* 此时数组分为 4 部分：
             * [0..lt)     : 等于枢轴
             * [lt..pi)    : 小于枢轴
             * [pi..gt)    : 大于枢轴
             * [gt..n)     : 等于枢轴
             */
            
            /* 将等于枢轴的元素移到中间 */
            /* 交换 [0..lt) 和 [i-lt..i) */
            span = plt - ptr;
            span2 = pi - plt;
            lt = i - lt;
            if (span > span2)
                span = span2;  /* 只交换较小的跨度 */
            swap_block(ptr, pi - span, span);
            
            /* 交换 [gt..top) 和 [i..top-(top-gt)) */
            span = top - pgt;
            span2 = pgt - pi;
            pgt = top - span2;
            gt = nmemb - (gt - i);
            if (span > span2)
                span = span2;
            swap_block(pi, top - span, span);

            /* 现在数组分为 3 部分：
             * [0..lt)     : 小于枢轴
             * [lt..gt)    : 等于枢轴
             * [gt..n)     : 大于枢轴
             */
            
            /* 栈优化：先处理小区间，大区间入栈，减少栈深度 */
            if (lt > nmemb - gt) {
                /* 左侧大：栈存左侧，处理右侧 */
                sp->base = ptr;
                sp->count = lt;
                sp->depth = depth;
                sp++;
                ptr = pgt;
                nmemb -= gt;
            } else {
                /* 右侧大：栈存右侧，处理左侧 */
                sp->base = pgt;
                sp->count = nmemb - gt;
                sp->depth = depth;
                sp++;
                nmemb = lt;
            }
        }
        
        /* 小区间：使用插入排序（元素数 <= 6） */
        for (pi = ptr + size, top = ptr + nmemb * size; pi < top; pi += size) {
            for (pj = pi; pj > ptr && cmp(pj - size, pj, opaque) > 0; pj -= size)
                swap(pj, pj - size, size);
        }
    }
}

#endif
