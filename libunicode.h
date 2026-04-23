/*
 * Unicode utilities - Unicode 字符处理工具库
 * 
 * 本文件提供 Unicode 字符范围管理、字符分类、大小写转换、标识符判断等核心功能
 * 是正则表达式引擎和 JavaScript 解析器的基础支撑库
 *
 * Copyright (c) 2017-2018 Fabrice Bellard
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
#ifndef LIBUNICODE_H
#define LIBUNICODE_H

#include <stdint.h>

/* define it to include all the unicode tables (40KB larger) */
/* 定义此宏以包含完整的 Unicode 表（会增加约 40KB 内存占用）*/
#define CONFIG_ALL_UNICODE

/* 大小写转换结果的最大长度（用于处理一个字符映射到多个字符的情况，如德语 ß → SS）*/
#define LRE_CC_RES_LEN_MAX 3

/* ==========================================================================
 * 字符范围 (CharRange) 数据结构
 * ========================================================================== */
/* 字符范围用于高效表示和操作 Unicode 字符集合
 * 内部使用排序的端点数组表示不相交的区间
 * 例如：[a-z] 表示为 [97, 123) 即 points[0]=97, points[1]=123
 * 多个区间：[a-z0-9] 表示为 [48, 58, 97, 123) */

typedef struct {
    int len;        /* 端点数量（以点计），始终为偶数（每个区间需要起点和终点）*/
    int size;       /* 当前分配的容量 */
    uint32_t *points; /* 端点数组，按递增顺序排序 */
    void *mem_opaque; /* 内存管理器的不透明上下文 */
    void *(*realloc_func)(void *opaque, void *ptr, size_t size); /* 重分配函数指针 */
} CharRange;

/* 字符范围集合操作类型枚举 */
typedef enum {
    CR_OP_UNION,    /* 并集：A ∪ B，合并两个字符范围 */
    CR_OP_INTER,    /* 交集：A ∩ B，取两个字符范围的公共部分 */
    CR_OP_XOR,      /* 异或：(A ∪ B) - (A ∩ B)，取对称差集 */
    CR_OP_SUB,      /* 差集：A - B，从 A 中移除 B 包含的字符 */
} CharRangeOpEnum;

/* CharRange 初始化：设置内存管理器和回调函数
 * @param cr 字符范围结构体指针
 * @param mem_opaque 内存管理器上下文（通常传入 JSRuntime）
 * @param realloc_func 内存重分配函数（通常传入 js_realloc_rt）*/
void cr_init(CharRange *cr, void *mem_opaque, void *(*realloc_func)(void *opaque, void *ptr, size_t size));

/* 释放字符范围占用的内存 */
void cr_free(CharRange *cr);

/* 为字符范围重新分配内存
 * @param cr 字符范围结构体指针
 * @param size 新的容量（端点数量）
 * @return 成功返回 0，失败返回 -1 */
int cr_realloc(CharRange *cr, int size);

/* 复制字符范围：将 cr1 的内容复制到 cr
 * @param cr 目标字符范围
 * @param cr1 源字符范围
 * @return 成功返回 0，失败返回 -1 */
int cr_copy(CharRange *cr, const CharRange *cr1);

/* 向字符范围添加单个端点（内联函数）
 * @param cr 字符范围结构体指针
 * @param v 端点值（Unicode 码点）
 * @return 成功返回 0，内存不足返回 -1 */
static inline int cr_add_point(CharRange *cr, uint32_t v)
{
    if (cr->len >= cr->size) {
        if (cr_realloc(cr, cr->len + 1))
            return -1;
    }
    cr->points[cr->len++] = v;
    return 0;
}

/* 向字符范围添加一个区间 [c1, c2)（内联函数）
 * @param cr 字符范围结构体指针
 * @param c1 区间起点（包含）
 * @param c2 区间终点（不包含）
 * @return 成功返回 0，内存不足返回 -1 */
static inline int cr_add_interval(CharRange *cr, uint32_t c1, uint32_t c2)
{
    if ((cr->len + 2) > cr->size) {
        if (cr_realloc(cr, cr->len + 2))
            return -1;
    }
    cr->points[cr->len++] = c1;
    cr->points[cr->len++] = c2;
    return 0;
}

/* 对两个字符范围执行集合运算
 * @param cr 输出结果字符范围
 * @param a_pt 第一个操作数的端点数组
 * @param a_len 第一个操作数的端点数量
 * @param b_pt 第二个操作数的端点数组
 * @param b_len 第二个操作数的端点数量
 * @param op 操作类型（CR_OP_UNION/INTER/XOR/SUB）
 * @return 成功返回 0，失败返回 -1 */
int cr_op(CharRange *cr, const uint32_t *a_pt, int a_len,
          const uint32_t *b_pt, int b_len, int op);

/* 对当前字符范围与另一个范围执行集合运算（一元版本）
 * @param cr 输入输出字符范围（结果覆盖原值）
 * @param b_pt 第二个操作数的端点数组
 * @param b_len 第二个操作数的端点数量
 * @param op 操作类型
 * @return 成功返回 0，失败返回 -1 */
int cr_op1(CharRange *cr, const uint32_t *b_pt, int b_len, int op);

/* 将区间 [c1, c2] 并入了字符范围（闭区间，两端包含）
 * @param cr 字符范围结构体指针
 * @param c1 区间起点
 * @param c2 区间终点（包含）
 * @return 成功返回 0，失败返回 -1 */
static inline int cr_union_interval(CharRange *cr, uint32_t c1, uint32_t c2)
{
    uint32_t b_pt[2];
    b_pt[0] = c1;
    b_pt[1] = c2 + 1;  /* 转换为半开区间 [c1, c2+1) */
    return cr_op1(cr, b_pt, 2, CR_OP_UNION);
}

/* 对字符范围取反：得到当前范围之外的所有字符
 * @param cr 输入输出字符范围
 * @return 成功返回 0，失败返回 -1 */
int cr_invert(CharRange *cr);

/* 对字符范围进行正则规范化（用于正则表达式的不区分大小写匹配）
 * 将字符范围扩展为包含所有大小写等价的字符
 * @param cr 输入输出字符范围
 * @param is_unicode 是否启用 Unicode 模式
 * @return 成功返回 0，失败返回 -1 */
int cr_regexp_canonicalize(CharRange *cr, int is_unicode);

/* ==========================================================================
 * Unicode 规范化形式
 * ========================================================================== */
/* Unicode 规范化用于将等价的字符序列转换为统一的标准形式
 * 例如："é" 可以表示为单个字符 U+00E9，也可以是 "e" + U+0301（组合重音）
 * 
 * NFC/NFD: 兼容分解 vs 标准分解
 * NFKC/NFKD: 兼容分解（额外处理兼容字符，如将 "ﬁ" 分解为 "fi"）*/
typedef enum {
    UNICODE_NFC,    /* 标准合成形式：先分解再合成，最常用的标准形式 */
    UNICODE_NFD,    /* 标准分解形式：完全分解为基字符 + 组合标记 */
    UNICODE_NFKC,   /* 兼容合成形式：先兼容分解再合成，用于搜索和比较 */
    UNICODE_NFKD,   /* 兼容分解形式：完全兼容分解，用于标准化处理 */
} UnicodeNormalizationEnum;

/* Unicode 规范化函数：将字符串转换为指定的规范化形式
 * @param pdst 输出缓冲区指针的地址（函数会分配内存）
 * @param src 源字符串（Unicode 码点数组）
 * @param src_len 源字符串长度
 * @param n_type 规范化类型（NFC/NFD/NFKC/NFKD）
 * @param opaque 内存管理器上下文
 * @param realloc_func 内存重分配函数
 * @return 成功返回 0，失败返回 -1 */
int unicode_normalize(uint32_t **pdst, const uint32_t *src, int src_len,
                      UnicodeNormalizationEnum n_type,
                      void *opaque, void *(*realloc_func)(void *opaque, void *ptr, size_t size));

/* ==========================================================================
 * Unicode 字符范围查询函数
 * ========================================================================== */
/* 根据 Unicode 脚本名称构建字符范围（如 "Latin"、"Han"、"Arabic"）
 * @param cr 输出字符范围
 * @param script_name 脚本名称（如 "Latin"、"Han"）
 * @param is_ext 是否支持扩展脚本名称（如 "Latin_Extended"）
 * @return 成功返回 0，失败返回 -1 */
int unicode_script(CharRange *cr, const char *script_name, int is_ext);

/* 根据 Unicode 通用分类名称构建字符范围（如 "Lu"=大写字母、"Nd"=十进制数字）
 * @param cr 输出字符范围
 * @param gc_name 通用分类名称（2 字母代码，如 "Lu"、"Ll"、"Nd"、"Zs"）
 * @return 成功返回 0，失败返回 -1 */
int unicode_general_category(CharRange *cr, const char *gc_name);

/* 根据 Unicode 属性名称构建字符范围（如 "Alphabetic"、"White_Space"）
 * @param cr 输出字符范围
 * @param prop_name 属性名称（如 "Alphabetic"、"ID_Start"、"White_Space"）
 * @return 成功返回 0，失败返回 -1 */
int unicode_prop(CharRange *cr, const char *prop_name);

/* Unicode 序列属性回调函数类型定义
 * 用于 unicode_sequence_prop 函数，处理字符序列属性查询结果
 * @param opaque 用户上下文
 * @param buf 字符序列缓冲区
 * @param len 序列长度 */
typedef void UnicodeSequencePropCB(void *opaque, const uint32_t *buf, int len);

/* 查询 Unicode 序列属性（用于处理多字符组合的属性，如 Emoji 序列）
 * @param prop_name 属性名称
 * @param cb 回调函数（对每个匹配序列调用）
 * @param opaque 用户上下文传递给回调
 * @param cr 输出字符范围（单字符属性）
 * @return 成功返回 0，失败返回 -1 */
int unicode_sequence_prop(const char *prop_name, UnicodeSequencePropCB *cb, void *opaque,
                          CharRange *cr);

/* ==========================================================================
 * 大小写转换函数
 * ========================================================================== */
/* 大小写转换函数：将字符转换为大写、小写或标题大写
 * @param res 输出缓冲区（最多 LRE_CC_RES_LEN_MAX 个码点）
 * @param c 输入字符（Unicode 码点）
 * @param conv_type 转换类型（0=小写，1=大写，2=标题大写）
 * @return 返回输出字符数量 */
int lre_case_conv(uint32_t *res, uint32_t c, int conv_type);

/* 字符规范化（用于不区分大小写的比较）
 * 将字符映射到其规范化形式（通常是小写，但有特殊规则）
 * @param c 输入字符
 * @param is_unicode 是否启用 Unicode 模式
 * @return 规范化后的字符 */
int lre_canonicalize(uint32_t c, int is_unicode);

/* ==========================================================================
 * 代码点类型分类（ASCII 字符集）
 * ========================================================================== */
/* 字符类型位标志（用于 ASCII 字符的快速分类，查表法）
 * 每个标志对应一个位，可以组合使用 */
enum {
    UNICODE_C_SPACE  = (1 << 0),  /* 空白字符：空格、制表符、换行等 */
    UNICODE_C_DIGIT  = (1 << 1),  /* 数字：0-9 */
    UNICODE_C_UPPER  = (1 << 2),  /* 大写字母：A-Z */
    UNICODE_C_LOWER  = (1 << 3),  /* 小写字母：a-z */
    UNICODE_C_UNDER  = (1 << 4),  /* 下划线：_ */
    UNICODE_C_DOLLAR = (1 << 5),  /* 美元符号：$（JavaScript 标识符允许）*/
    UNICODE_C_XDIGIT = (1 << 6),  /* 十六进制数字：0-9, A-F, a-f */
};

/* ASCII 字符类型查找表（256 个条目，每个条目是位标志的组合）
 * 例如：'A' 的类型是 (UNICODE_C_UPPER | UNICODE_C_XDIGIT)
 *       'a' 的类型是 (UNICODE_C_LOWER | UNICODE_C_XDIGIT)
 *       '5' 的类型是 (UNICODE_C_DIGIT | UNICODE_C_XDIGIT) */
extern uint8_t const lre_ctype_bits[256];

/* ==========================================================================
 * 字符类型判断函数
 * ========================================================================== */
/* 判断字符是否有大小写（即是否是大写或小写字母）
 * @param c Unicode 码点
 * @return 有大小写返回非零，否则返回 0 */
int lre_is_cased(uint32_t c);

/* 判断字符是否可忽略大小写（如某些组合标记、零宽字符）
 * 这些字符在大小写转换时保持不变，但可能影响大小写规则
 * @param c Unicode 码点
 * @return 可忽略返回非零，否则返回 0 */
int lre_is_case_ignorable(uint32_t c);

/* 判断字符是否可以作为 Unicode 标识符的首字符
 * 包括字母、下划线、美元符号以及 Unicode 字母
 * @param c Unicode 码点
 * @return 可以作为标识符首字符返回非零，否则返回 0 */
int lre_is_id_start(uint32_t c);

/* 判断字符是否可以作为 Unicode 标识符的后续字符
 * 在 is_id_start 的基础上还包括数字、连接符等
 * @param c Unicode 码点
 * @return 可以作为标识符后续字符返回非零，否则返回 0 */
int lre_is_id_continue(uint32_t c);

/* 判断 ASCII 字符是否为空白字符（内联函数）
 * @param c ASCII 字符（0-255）
 * @return 是空白字符返回非零，否则返回 0 */
static inline int lre_is_space_byte(uint8_t c) {
    return lre_ctype_bits[c] & UNICODE_C_SPACE;
}

/* 判断 ASCII 字符是否可以作为标识符首字符（内联函数）
 * 包括：大写字母、小写字母、下划线、美元符号
 * @param c ASCII 字符（0-255）
 * @return 可以作为标识符首字符返回非零，否则返回 0 */
static inline int lre_is_id_start_byte(uint8_t c) {
    return lre_ctype_bits[c] & (UNICODE_C_UPPER | UNICODE_C_LOWER |
                                UNICODE_C_UNDER | UNICODE_C_DOLLAR);
}

/* 判断 ASCII 字符是否可以作为标识符后续字符（内联函数）
 * 在 is_id_start_byte 的基础上还包括数字
 * @param c ASCII 字符（0-255）
 * @return 可以作为标识符后续字符返回非零，否则返回 0 */
static inline int lre_is_id_continue_byte(uint8_t c) {
    return lre_ctype_bits[c] & (UNICODE_C_UPPER | UNICODE_C_LOWER |
                                UNICODE_C_UNDER | UNICODE_C_DOLLAR |
                                UNICODE_C_DIGIT);
}

/* 判断 ASCII 字符是否可以作为单词字符（内联函数）
 * 用于正则表达式 \w 匹配：字母、数字、下划线
 * @param c ASCII 字符（0-255）
 * @return 是单词字符返回非零，否则返回 0 */
static inline int lre_is_word_byte(uint8_t c) {
    return lre_ctype_bits[c] & (UNICODE_C_UPPER | UNICODE_C_LOWER |
                                UNICODE_C_UNDER | UNICODE_C_DIGIT);
}

/* 判断非 ASCII 字符是否为空白字符
 * 检查 Unicode 中定义的空白字符（如 U+00A0 不换行空格、U+2000 等）
 * @param c Unicode 码点（>= 256）
 * @return 是空白字符返回非零，否则返回 0 */
int lre_is_space_non_ascii(uint32_t c);

/* 判断任意字符是否为空白字符（内联函数）
 * ASCII 字符查表，非 ASCII 字符调用专门函数
 * @param c Unicode 码点
 * @return 是空白字符返回非零，否则返回 0 */
static inline int lre_is_space(uint32_t c) {
    if (c < 256)
        return lre_is_space_byte(c);
    else
        return lre_is_space_non_ascii(c);
}

/* 判断字符是否可以作为 JavaScript 标识符的首字符（内联函数）
 * JavaScript 标识符规则：
 * - ASCII 范围（<128）：字母、下划线、美元符号
 * - 非 ASCII 范围：Unicode 字母（CONFIG_ALL_UNICODE 启用时）
 * @param c Unicode 码点
 * @return 可以作为标识符首字符返回非零，否则返回 0 */
static inline int lre_js_is_ident_first(uint32_t c) {
    if (c < 128) {
        return lre_is_id_start_byte(c);
    } else {
#ifdef CONFIG_ALL_UNICODE
        /* 启用 Unicode 支持时，检查完整的 Unicode 标识符规则 */
        return lre_is_id_start(c);
#else
        /* 禁用 Unicode 支持时，简单排除空白字符 */
        return !lre_is_space_non_ascii(c);
#endif
    }
}

/* 判断字符是否可以作为 JavaScript 标识符的后续字符（内联函数）
 * JavaScript 标识符规则：
 * - ASCII 范围（<128）：字母、数字、下划线、美元符号
 * - 非 ASCII 范围：Unicode 字母、数字、连接符
 * - 特殊：U+200C (ZWNJ) 和 U+200D (ZWJ) 零宽连接符也允许
 * @param c Unicode 码点
 * @return 可以作为标识符后续字符返回非零，否则返回 0 */
static inline int lre_js_is_ident_next(uint32_t c) {
    if (c < 128) {
        return lre_is_id_continue_byte(c);
    } else {
        /* ZWNJ (Zero Width Non-Joiner) 和 ZWJ (Zero Width Joiner) 在标识符中允许使用 */
        if (c >= 0x200C && c <= 0x200D)
            return TRUE;
#ifdef CONFIG_ALL_UNICODE
        /* 启用 Unicode 支持时，检查完整的 Unicode 标识符规则 */
        return lre_is_id_continue(c);
#else
        /* 禁用 Unicode 支持时，简单排除空白字符 */
        return !lre_is_space_non_ascii(c);
#endif
    }
}

#endif /* LIBUNICODE_H */
