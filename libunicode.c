/*
 * Unicode utilities
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "cutils.h"
#include "libunicode.h"
#include "libunicode-table.h"

/* 
 * 大小写转换的运行类型枚举
 * 用于压缩 Unicode 大小写转换表，通过不同的编码方式减少表的大小
 * 
 * RUN_TYPE_U: 转大写 (Upper)
 * RUN_TYPE_L: 转小写 (Lower)
 * RUN_TYPE_UF: 转大写或折叠 (Upper/Fold)
 * RUN_TYPE_LF: 转小写或折叠 (Lower/Fold)
 * RUN_TYPE_UL: 大小写交替 (Upper-Lower alternating，如 A/a, B/b)
 * RUN_TYPE_LSU: 特殊大小写转换 (Lower-Special-Upper)
 * RUN_TYPE_U2L_399_EXT2: 大写转小写，扩展表，返回 2 个字符（第二个是 0x399）
 * RUN_TYPE_UF_D20: 大写/折叠，差值 0x20
 * RUN_TYPE_UF_D1_EXT: 大写/折叠，扩展表，差值 1
 * RUN_TYPE_U_EXT: 转大写，使用扩展表
 * RUN_TYPE_LF_EXT: 转小写/折叠，使用扩展表
 * RUN_TYPE_UF_EXT2: 大写/折叠，扩展表，返回 2 个字符
 * RUN_TYPE_LF_EXT2: 小写/折叠，扩展表，返回 2 个字符
 * RUN_TYPE_UF_EXT3: 大写/折叠，扩展表，返回 3 个字符
 */
enum {
    RUN_TYPE_U,              /* 0: 转大写 */
    RUN_TYPE_L,              /* 1: 转小写 */
    RUN_TYPE_UF,             /* 2: 转大写或折叠 */
    RUN_TYPE_LF,             /* 3: 转小写或折叠 */
    RUN_TYPE_UL,             /* 4: 大小写交替 */
    RUN_TYPE_LSU,            /* 5: 特殊大小写转换 */
    RUN_TYPE_U2L_399_EXT2,   /* 6: 大写转小写 + 0x399 */
    RUN_TYPE_UF_D20,         /* 7: 差值 0x20 */
    RUN_TYPE_UF_D1_EXT,      /* 8: 扩展表差值 1 */
    RUN_TYPE_U_EXT,          /* 9: 扩展表转大写 */
    RUN_TYPE_LF_EXT,         /* 10: 扩展表转小写/折叠 */
    RUN_TYPE_UF_EXT2,        /* 11: 扩展表返回 2 字符 */
    RUN_TYPE_LF_EXT2,        /* 12: 扩展表返回 2 字符 (小写) */
    RUN_TYPE_UF_EXT3,        /* 13: 扩展表返回 3 字符 */
};

/*
 * 【单字符大小写转换 - 辅助函数】
 * 功能：对单个字符进行大小写转换，返回转换后的单个字符
 * 参数：
 *   - c: 输入的 Unicode 字符码点
 *   - conv_type: 转换类型 (0=转大写，1=转小写，2=大小写折叠)
 * 返回：转换后的字符码点
 * 
 * 说明：这是 lre_case_conv 的简化包装，只返回第一个结果字符
 *       用于处理单字符转换的简单场景
 */
static int lre_case_conv1(uint32_t c, int conv_type)
{
    uint32_t res[LRE_CC_RES_LEN_MAX];
    lre_case_conv(res, c, conv_type);
    return res[0];
}

/*
 * 【大小写转换表条目处理 - 核心函数】
 * 功能：根据压缩表中的条目信息，执行具体的大小写转换
 * 参数：
 *   - res: 输出数组，存储转换后的字符（可能多个）
 *   - c: 输入的 Unicode 字符码点
 *   - conv_type: 转换类型 (0=转大写，1=转小写，2=大小写折叠)
 *   - idx: 在 case_conv_table2 中的索引
 *   - v: 表条目的原始值 (32 位)
 * 返回：转换后字符的数量（1、2 或 3）
 * 
 * 数据结构说明：
 *   v 的 32 位布局：[17 位起始码点][7 位长度][4 位类型][4 位数据高 4 位]
 *   data = [4 位数据高 4 位][8 位 case_conv_table2 索引值]
 * 
 * 压缩原理：
 *   Unicode 大小写转换表使用运行长度编码 (RLE) 压缩
 *   连续具有相同转换规则的字符被编码为一个"运行"(run)
 *   每个运行类型对应不同的转换公式
 */
static int lre_case_conv_entry(uint32_t *res, uint32_t c, int conv_type, uint32_t idx, uint32_t v)
{
    uint32_t code, data, type, a, is_lower;
    is_lower = (conv_type != 0);  /* 判断是否为转小写/折叠模式 */
    type = (v >> (32 - 17 - 7 - 4)) & 0xf;  /* 提取 4 位运行类型 */
    data = ((v & 0xf) << 8) | case_conv_table2[idx];  /* 组合完整数据索引 */
    code = v >> (32 - 17);  /* 提取 17 位起始码点 */
    switch(type) {
    /* 
     * RUN_TYPE_U/L/UF/LF: 简单偏移转换
     * 转换公式：目标字符 = 输入字符 - 起始码点 + 偏移量
     * type & 1 判断是转大写 (0) 还是转小写 (1)
     */
    case RUN_TYPE_U:
    case RUN_TYPE_L:
    case RUN_TYPE_UF:
    case RUN_TYPE_LF:
        if (conv_type == (type & 1) ||
            (type >= RUN_TYPE_UF && conv_type == 2)) {
            c = c - code + (case_conv_table1[data] >> (32 - 17));
        }
        break;
    
    /*
     * RUN_TYPE_UL: 大小写交替模式
     * 用于 A/a, B/b, C/c 这种成对出现的字符
     * 通过奇偶位判断当前是大写还是小写，然后翻转
     */
    case RUN_TYPE_UL:
        a = c - code;
        if ((a & 1) != (1 - is_lower))  /* 检查奇偶性是否匹配 */
            break;
        c = (a ^ 1) + code;  /* 翻转最低位实现大小写切换 */
        break;
    
    /*
     * RUN_TYPE_LSU: 特殊三态转换
     * 用于某些特殊的希腊字母，有三种状态
     * 例如：σ (U+03C2) 在词尾和词中形式不同
     */
    case RUN_TYPE_LSU:
        a = c - code;
        if (a == 1) {
            c += 2 * is_lower - 1;  /* 状态 1 转换 */
        } else if (a == (1 - is_lower) * 2) {
            c += (2 * is_lower - 1) * 2;  /* 状态 2 转换 */
        }
        break;
    
    /*
     * RUN_TYPE_U2L_399_EXT2: 大写转小写 + 追加字符
     * 特殊场景：一个大写字母转小写时需要拆分成两个字符
     * 第二个字符固定为 0x399 (希腊字母 Ι)
     */
    case RUN_TYPE_U2L_399_EXT2:
        if (!is_lower) {
            res[0] = c - code + case_conv_ext[data >> 6];  /* 第一个字符从扩展表查 */
            res[1] = 0x399;  /* 固定追加 Ι */
            return 2;
        } else {
            c = c - code + case_conv_ext[data & 0x3f];
        }
        break;
    
    /*
     * RUN_TYPE_UF_D20: 固定差值 0x20 转换
     * ASCII 字母的大小写差值正好是 0x20 (32)
     * 用于某些具有固定差值的字符范围
     */
    case RUN_TYPE_UF_D20:
        if (conv_type == 1)  /* 转小写时不适用 */
            break;
        c = data + (conv_type == 2) * 0x20;  /* 折叠时加 0x20 */
        break;
    
    /*
     * RUN_TYPE_UF_D1_EXT: 扩展表差值 1 转换
     * 从扩展表查基础值，折叠时加 1
     */
    case RUN_TYPE_UF_D1_EXT:
        if (conv_type == 1)
            break;
        c = case_conv_ext[data] + (conv_type == 2);
        break;
    
    /*
     * RUN_TYPE_U_EXT / RUN_TYPE_LF_EXT: 扩展表直接查找
     * 转换结果直接存储在扩展表中，不适用简单公式
     */
    case RUN_TYPE_U_EXT:
    case RUN_TYPE_LF_EXT:
        if (is_lower != (type - RUN_TYPE_U_EXT))
            break;
        c = case_conv_ext[data];
        break;
    
    /*
     * RUN_TYPE_LF_EXT2: 扩展表双字符输出 (小写模式)
     * 一个小写字母对应两个字符的展开
     */
    case RUN_TYPE_LF_EXT2:
        if (!is_lower)
            break;
        res[0] = c - code + case_conv_ext[data >> 6];  /* 高 6 位索引 */
        res[1] = case_conv_ext[data & 0x3f];  /* 低 6 位索引 */
        return 2;
    
    /*
     * RUN_TYPE_UF_EXT2: 扩展表双字符输出 (大写/折叠模式)
     * 折叠时需要递归转换为小写
     */
    case RUN_TYPE_UF_EXT2:
        if (conv_type == 1)
            break;
        res[0] = c - code + case_conv_ext[data >> 6];
        res[1] = case_conv_ext[data & 0x3f];
        if (conv_type == 2) {
            /* 折叠模式：递归转换为小写 */
            res[0] = lre_case_conv1(res[0], 1);
            res[1] = lre_case_conv1(res[1], 1);
        }
        return 2;
    
    /*
     * RUN_TYPE_UF_EXT3: 扩展表三字符输出
     * 最复杂的情况，一个大写字母对应三个字符的展开
     * 例如：ß (U+00DF) 大写展开为 "SSS"
     */
    default:
    case RUN_TYPE_UF_EXT3:
        if (conv_type == 1)
            break;
        res[0] = case_conv_ext[data >> 8];  /* 高 8 位索引 */
        res[1] = case_conv_ext[(data >> 4) & 0xf];  /* 中 4 位索引 */
        res[2] = case_conv_ext[data & 0xf];  /* 低 4 位索引 */
        if (conv_type == 2) {
            /* 折叠模式：递归转换为小写 */
            res[0] = lre_case_conv1(res[0], 1);
            res[1] = lre_case_conv1(res[1], 1);
            res[2] = lre_case_conv1(res[2], 1);
        }
        return 3;
    }
    res[0] = c;
    return 1;  /* 默认返回单字符 */
}

/*
 * 【大小写转换主函数】
 * 功能：将 Unicode 字符进行大小写转换
 * 参数：
 *   - res: 输出数组，存储转换后的字符（LRE_CC_RES_LEN_MAX 长度，通常为 3）
 *   - c: 输入的 Unicode 字符码点
 *   - conv_type: 转换类型
 *       0 = to upper (转大写)
 *       1 = to lower (转小写)
 *       2 = case folding (大小写折叠，用于正则匹配，类似转小写但有特殊规则)
 * 返回：转换后字符的数量（1、2 或 3）
 * 
 * 算法说明：
 * 1. ASCII 字符 (c < 128): 直接计算，A-Z 转 a-z 或反之
 * 2. 非 ASCII 字符：使用二分查找在压缩表中定位
 *    - case_conv_table1 存储运行条目，每个条目包含：
 *      [17 位起始码点][7 位长度][8 位其他数据]
 *    - 找到匹配的条目后，调用 lre_case_conv_entry 处理具体转换
 * 
 * 性能优化：
 * - ASCII 快速路径：无需查表，直接位运算
 * - 二分查找：O(log N) 时间复杂度查找表条目
 */
int lre_case_conv(uint32_t *res, uint32_t c, int conv_type)
{
    /* 快速路径：ASCII 字符直接处理 */
    if (c < 128) {
        if (conv_type) {
            /* 转小写或折叠：A-Z -> a-z */
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        } else {
            /* 转大写：a-z -> A-Z */
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }
    } else {
        /* 非 ASCII 字符：二分查找压缩表 */
        uint32_t v, code, len;
        int idx, idx_min, idx_max;

        idx_min = 0;
        idx_max = countof(case_conv_table1) - 1;
        while (idx_min <= idx_max) {
            idx = (unsigned)(idx_max + idx_min) / 2;
            v = case_conv_table1[idx];
            code = v >> (32 - 17);  /* 提取起始码点 */
            len = (v >> (32 - 17 - 7)) & 0x7f;  /* 提取运行长度 */
            if (c < code) {
                idx_max = idx - 1;  /* 在左半部分查找 */
            } else if (c >= code + len) {
                idx_min = idx + 1;  /* 在右半部分查找 */
            } else {
                /* 找到匹配的条目，委托给入口处理函数 */
                return lre_case_conv_entry(res, c, conv_type, idx, v);
            }
        }
    }
    /* 未找到转换规则或无需转换，返回原字符 */
    res[0] = c;
    return 1;
}

/*
 * 【正则表达式大小写折叠入口】
 * 功能：为正则表达式引擎提供大小写折叠支持
 * 参数：
 *   - c: 输入字符
 *   - idx: 压缩表索引
 *   - v: 表条目值
 *   - is_unicode: 是否为 Unicode 模式（正则的 /u 标志）
 * 返回：折叠后的字符
 * 
 * 背景知识：
 * 正则表达式的不区分大小写匹配需要"折叠"大小写差异
 * Unicode 模式 vs 传统模式有不同规则：
 * - Unicode 模式：严格遵循 Unicode 大小写折叠标准
 * - 传统模式：ASCII 字母转大写，非 ASCII 单字符也转大写（历史原因）
 */
static int lre_case_folding_entry(uint32_t c, uint32_t idx, uint32_t v, BOOL is_unicode)
{
    uint32_t res[LRE_CC_RES_LEN_MAX];
    int len;

    if (is_unicode) {
        /* Unicode 模式：使用标准大小写折叠 (conv_type=2) */
        len = lre_case_conv_entry(res, c, 2, idx, v);
        if (len == 1) {
            c = res[0];
        } else {
            /* 
             * 处理少数多字符折叠的特殊情况
             * 这些是 Unicode 标准中的边缘案例
             * 例如：0xFB06 (ﬁ) 折叠为 0xFB05 (ﬅ)
             */
            if (c == 0xfb06) {
                c = 0xfb05;
            } else if (c == 0x01fd3) {
                c = 0x390;
            } else if (c == 0x01fe3) {
                c = 0x3b0;
            }
        }
    } else {
        /* 传统模式（非 Unicode）：兼容旧版正则行为 */
        if (likely(c < 128)) {
            /* ASCII 字母转大写 */
            if (c >= 'a' && c <= 'z')
                c = c - 'a' + 'A';
        } else {
            /* 
             * 历史行为：非 ASCII 单字符也转大写
             * 这是为了向后兼容早期 JavaScript 引擎
             */
            len = lre_case_conv_entry(res, c, FALSE, idx, v);
            if (len == 1 && res[0] >= 128)
                c = res[0];
        }
    }
    return c;
}

/*
 * 【JS 正则表达式专用大小写规范化】
 * 功能：为正则表达式的不区分大小写匹配提供字符规范化
 * 参数：
 *   - c: 输入字符
 *   - is_unicode: 是否为 Unicode 模式（正则的 /u 标志）
 * 返回：规范化后的字符（用于比较）
 * 
 * 使用场景：
 * 当正则表达式使用 /i 标志（不区分大小写）时：
 * - /a/i 应该匹配 'a' 和 'A'
 * - 实现方式：将两个字符都规范化到同一形式再比较
 * 
 * 与 lre_case_conv 的区别：
 * - lre_case_conv: 完整的大小写转换，可能返回多字符
 * - lre_canonicalize: 只返回单字符，用于比较目的
 * 
 * 算法：
 * 1. ASCII 快速路径
 *    - Unicode 模式：大写转小写 (A->a)
 *    - 传统模式：小写转大写 (a->A)，历史兼容性
 * 2. 非 ASCII：二分查找 + 折叠处理
 */
int lre_canonicalize(uint32_t c, BOOL is_unicode)
{
    /* 快速路径：ASCII 字符 */
    if (c < 128) {
        if (is_unicode) {
            /* Unicode 模式：统一转小写（现代标准） */
            if (c >= 'A' && c <= 'Z') {
                c = c - 'A' + 'a';
            }
        } else {
            /* 传统模式：统一转大写（历史兼容） */
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
        }
    } else {
        /* 非 ASCII：二分查找压缩表 */
        uint32_t v, code, len;
        int idx, idx_min, idx_max;

        idx_min = 0;
        idx_max = countof(case_conv_table1) - 1;
        while (idx_min <= idx_max) {
            idx = (unsigned)(idx_max + idx_min) / 2;
            v = case_conv_table1[idx];
            code = v >> (32 - 17);
            len = (v >> (32 - 17 - 7)) & 0x7f;
            if (c < code) {
                idx_max = idx - 1;
            } else if (c >= code + len) {
                idx_min = idx + 1;
            } else {
                /* 找到匹配条目，调用折叠入口处理 */
                return lre_case_folding_entry(c, idx, v, is_unicode);
            }
        }
    }
    return c;  /* 无转换规则，返回原字符 */
}

/*
 * 【读取 24 位小端整数】
 * 功能：从字节数组中读取一个 24 位（3 字节）的小端整数
 * 参数：ptr - 指向 3 字节数据的指针
 * 返回：32 位整数（高 8 位为 0）
 * 
 * 用途：Unicode 表使用 24 位编码节省空间
 *       每个索引条目占 3 字节：[21 位码点][3 位块内偏移高 3 位]
 */
static uint32_t get_le24(const uint8_t *ptr)
{
    return ptr[0] | (ptr[1] << 8) | (ptr[2] << 16);
}

/* 索引块长度：每个块包含 32 个字符的位图数据 */
#define UNICODE_INDEX_BLOCK_LEN 32

/*
 * 【获取字符在 Unicode 属性表中的索引位置】
 * 功能：在两级索引结构中定位字符所在的位图块
 * 参数：
 *   - pcode: 输出参数，返回块的起始码点
 *   - c: 要查找的字符
 *   - index_table: 一级索引表（24 位条目数组）
 *   - index_table_len: 索引表长度
 * 返回：
 *   - -1: 字符超出表的范围
 *   - 其他：在位图表中的字节偏移量
 * 
 * 数据结构：
 * 两级索引设计用于压缩大型 Unicode 属性表：
 * 1. 一级索引 (index_table): 24 位/条目
 *    - 低 21 位：块的起始码点
 *    - 高 3 位：块内偏移的高位
 * 2. 二级数据 (table): 压缩的位图数据
 *    - 每个块 32 字符，用压缩游程编码存储
 * 
 * 算法：二分查找一级索引，定位字符所属的块
 */
static int get_index_pos(uint32_t *pcode, uint32_t c,
                         const uint8_t *index_table, int index_table_len)
{
    uint32_t code, v;
    int idx_min, idx_max, idx;

    idx_min = 0;
    v = get_le24(index_table);
    code = v & ((1 << 21) - 1);  /* 提取 21 位起始码点 */
    if (c < code) {
        *pcode = 0;
        return 0;  /* 字符在表之前 */
    }
    idx_max = index_table_len - 1;
    code = get_le24(index_table + idx_max * 3);
    if (c >= code)
        return -1;  /* 字符超出表的范围 */
    
    /* 二分查找不变式：tab[idx_min] <= c < tab[idx_max] */
    while ((idx_max - idx_min) > 1) {
        idx = (idx_max + idx_min) / 2;
        v = get_le24(index_table + idx * 3);
        code = v & ((1 << 21) - 1);
        if (c < code) {
            idx_max = idx;
        } else {
            idx_min = idx;
        }
    }
    v = get_le24(index_table + idx_min * 3);
    *pcode = v & ((1 << 21) - 1);
    return (idx_min + 1) * UNICODE_INDEX_BLOCK_LEN + (v >> 21);
}

/*
 * 【检查字符是否在 Unicode 属性表中】
 * 功能：查询某个 Unicode 字符是否具有特定属性（如是否字母、是否数字等）
 * 参数：
 *   - c: 要查询的 Unicode 字符码点
 *   - table: 压缩的位图数据表
 *   - index_table: 一级索引表
 *   - index_table_len: 索引表长度
 * 返回：TRUE/FALSE，表示字符是否具有该属性
 * 
 * 压缩格式详解（游程编码 RLE）：
 * 属性表存储的是"具有某属性的字符范围"，使用压缩游程编码：
 * 
 * 字节值范围      编码格式                      长度计算
 * ─────────────────────────────────────────────────────
 * 0x00-0x3F   2 个打包长度 (3 位 +3 位)      (b>>3)+1 和 (b&7)+1
 * 0x40-0x5F   5 位 + 1 字节扩展             ((b-0x40)<<8 | p[0]) + 1
 * 0x60-0x7F   5 位 + 2 字节扩展             ((b-0x60)<<16 | p[0]<<8 | p[1]) + 1
 * 0x80-0xFF   7 位长度                     (b-0x80) + 1
 * 
 * 解码逻辑：
 * - 从起始码点开始，依次读取每个范围的长度
 * - 范围交替表示"假 - 真 - 假 - 真..."
 * - bit=0 表示当前范围外的字符不具有该属性
 * - bit=1 表示当前范围内的字符具有该属性
 * 
 * 示例：
 * 假设表存储"大写字母范围"，编码可能是：
 * [0x41-0x5A] 为真，其他为假
 * 压缩后：跳过 0x00-0x40(假) -> 0x41-0x5A(真) -> 0x5B-... (假)
 */
static BOOL lre_is_in_table(uint32_t c, const uint8_t *table,
                            const uint8_t *index_table, int index_table_len)
{
    uint32_t code, b, bit;
    int pos;
    const uint8_t *p;

    /* 第一步：通过两级索引定位到位图表中的位置 */
    pos = get_index_pos(&code, c, index_table, index_table_len);
    if (pos < 0)
        return FALSE;  /* 字符超出表的范围 */
    
    p = table + pos;  /* 指向压缩数据的起始位置 */
    bit = 0;  /* 初始状态为"假"（不在范围内） */
    
    /* 第二步：解码压缩游程编码，查找字符所在的范围 */
    for(;;) {
        b = *p++;  /* 读取控制字节 */
        if (b < 64) {
            /* 0x00-0x3F: 两个 3 位长度打包在一个字节中 */
            code += (b >> 3) + 1;  /* 第一个范围长度 */
            if (c < code)
                return bit;  /* 字符在第一个范围内 */
            bit ^= 1;  /* 切换真假状态 */
            code += (b & 7) + 1;  /* 第二个范围长度 */
        } else if (b >= 0x80) {
            /* 0x80-0xFF: 7 位长度，无扩展字节 */
            code += b - 0x80 + 1;
        } else if (b < 0x60) {
            /* 0x40-0x5F: 5 位 + 1 字节扩展 (13 位长度) */
            code += (((b - 0x40) << 8) | p[0]) + 1;
            p++;
        } else {
            /* 0x60-0x7F: 5 位 + 2 字节扩展 (21 位长度) */
            code += (((b - 0x60) << 16) | (p[0] << 8) | p[1]) + 1;
            p += 2;
        }
        /* 检查字符是否在当前范围内 */
        if (c < code)
            return bit;
        bit ^= 1;  /* 切换到下一个范围（真假交替） */
    }
}

/*
 * 【判断字符是否有大小写】
 * 功能：检查 Unicode 字符是否是"有大小写"的字母
 * 参数：c - Unicode 字符码点
 * 返回：TRUE=有大小写，FALSE=无大小写
 * 
 * 什么是"cased"字符：
 * - 有大写和小写形式的字母（如 A/a, B/b, Σ/σ）
 * - 不包括数字、标点、符号等
 * 
 * 判断逻辑：
 * 1. 先在大小写转换表中查找（快速路径）
 *    - 如果字符在转换表中，说明有大小写变化，返回 TRUE
 * 2. 如果不在转换表中，再查 Cased1 属性表
 *    - 某些字符有大小写属性但没有转换规则
 * 
 * 应用场景：
 * - 正则表达式的 \p{Cased} 属性
 * - 字符串的大小写敏感操作
 */
BOOL lre_is_cased(uint32_t c)
{
    uint32_t v, code, len;
    int idx, idx_min, idx_max;

    /* 第一步：在大小写转换表中查找 */
    idx_min = 0;
    idx_max = countof(case_conv_table1) - 1;
    while (idx_min <= idx_max) {
        idx = (unsigned)(idx_max + idx_min) / 2;
        v = case_conv_table1[idx];
        code = v >> (32 - 17);
        len = (v >> (32 - 17 - 7)) & 0x7f;
        if (c < code) {
            idx_max = idx - 1;
        } else if (c >= code + len) {
            idx_min = idx + 1;
        } else {
            /* 在转换表中找到，说明有大小写 */
            return TRUE;
        }
    }
    /* 第二步：查 Cased1 属性表（补充检查） */
    return lre_is_in_table(c, unicode_prop_Cased1_table,
                           unicode_prop_Cased1_index,
                           sizeof(unicode_prop_Cased1_index) / 3);
}

/*
 * 【判断字符是否可忽略大小写】
 * 功能：检查字符是否在大小写折叠时可以忽略
 * 参数：c - Unicode 字符码点
 * 返回：TRUE=可忽略，FALSE=不可忽略
 * 
 * 什么是"case ignorable"字符：
 * - 在大小写折叠时不影响匹配的字符
 * - 主要是某些变音符号、连接符等
 * - 例如：U+00AD (软连字符)、U+200C (零宽不连字)
 * 
 * 应用场景：
 * - 正则表达式的不区分大小写匹配
 * - 字符串比较时跳过这些字符
 */
BOOL lre_is_case_ignorable(uint32_t c)
{
    return lre_is_in_table(c, unicode_prop_Case_Ignorable_table,
                           unicode_prop_Case_Ignorable_index,
                           sizeof(unicode_prop_Case_Ignorable_index) / 3);
}

/* ============================================================
 * 字符范围 (CharRange) 操作工具
 * ============================================================
 * 
 * 数据结构说明：
 * CharRange 使用"端点数组"表示一组字符区间的并集
 * 
 * 表示方法：
 * - points 数组存储成对的端点：[start1, end1, start2, end2, ...]
 * - 每个区间是左闭右开：[start, end)
 * - 偶数索引 (0,2,4...) 是区间起点
 * - 奇数索引 (1,3,5...) 是区间终点
 * 
 * 示例：
 * 表示字符范围 [a-z] ∪ [A-Z]：
 *   points = ['a', 'z'+1, 'A', 'Z'+1]
 *   len = 4
 * 
 * 用途：
 * - 正则表达式字符类的内部表示
 * - Unicode 属性集合操作（并、交、差、补）
 */

/* 【调试函数】打印字符范围内容（仅调试用） */
static __maybe_unused void cr_dump(CharRange *cr)
{
    int i;
    for(i = 0; i < cr->len; i++)
        printf("%d: 0x%04x\n", i, cr->points[i]);
}

/* 【默认内存重分配函数】使用标准 realloc */
static void *cr_default_realloc(void *opaque, void *ptr, size_t size)
{
    return realloc(ptr, size);
}

/*
 * 【初始化字符范围】
 * 功能：初始化 CharRange 结构为空
 * 参数：
 *   - cr: 要初始化的 CharRange 结构
 *   - mem_opaque: 内存分配器的上下文（通常为 NULL）
 *   - realloc_func: 内存重分配函数，NULL 则使用默认 realloc
 */
void cr_init(CharRange *cr, void *mem_opaque, DynBufReallocFunc *realloc_func)
{
    cr->len = cr->size = 0;
    cr->points = NULL;
    cr->mem_opaque = mem_opaque;
    cr->realloc_func = realloc_func ? realloc_func : cr_default_realloc;
}

/*
 * 【释放字符范围】
 * 功能：释放 CharRange 占用的内存
 */
void cr_free(CharRange *cr)
{
    cr->realloc_func(cr->mem_opaque, cr->points, 0);
}

/*
 * 【字符范围扩容】
 * 功能：确保 CharRange 能容纳至少 size 个元素
 * 参数：
 *   - cr: CharRange 结构
 *   - size: 期望的容量
 * 返回：0=成功，-1=内存分配失败
 * 
 * 扩容策略：
 * - 新容量 = max(size, 原容量 × 1.5)
 * - 增长因子 1.5 平衡了内存利用率和扩容频率
 */
int cr_realloc(CharRange *cr, int size)
{
    int new_size;
    uint32_t *new_buf;

    if (size > cr->size) {
        new_size = max_int(size, cr->size * 3 / 2);
        new_buf = cr->realloc_func(cr->mem_opaque, cr->points,
                                   new_size * sizeof(cr->points[0]));
        if (!new_buf)
            return -1;
        cr->points = new_buf;
        cr->size = new_size;
    }
    return 0;
}

/*
 * 【复制字符范围】
 * 功能：将 cr1 的内容复制到 cr
 */
int cr_copy(CharRange *cr, const CharRange *cr1)
{
    if (cr_realloc(cr, cr1->len))
        return -1;
    memcpy(cr->points, cr1->points, sizeof(cr->points[0]) * cr1->len);
    cr->len = cr1->len;
    return 0;
}

/*
 * 【压缩字符范围】
 * 功能：合并连续的区间，移除空区间，优化存储
 * 
 * 优化场景：
 * 1. 空区间：[5, 5) 这样的区间不包含任何字符，移除
 * 2. 连续区间：[a, m) 和 [m, z) 可以合并为 [a, z)
 * 
 * 算法：
 * - 遍历所有区间对 (start, end)
 * - 跳过空区间 (start == end)
 * - 合并连续区间 (前一个 end == 后一个 start)
 * - 原地压缩，k 是写入位置，i/j 是读取位置
 * 
 * 示例：
 * 输入：[1,3), [3,5), [5,5), [7,9)
 * 输出：[1,5), [7,9)
 */
static void cr_compress(CharRange *cr)
{
    int i, j, k, len;
    uint32_t *pt;

    pt = cr->points;
    len = cr->len;
    i = 0;
    j = 0;
    k = 0;
    while ((i + 1) < len) {
        if (pt[i] == pt[i + 1]) {
            /* 空区间，跳过 */
            i += 2;
        } else {
            j = i;
            /* 查找可以合并的连续区间 */
            while ((j + 3) < len && pt[j + 1] == pt[j + 2])
                j += 2;
            /* 复制合并后的区间 */
            pt[k] = pt[i];
            pt[k + 1] = pt[j + 1];
            k += 2;
            i = j + 2;
        }
    }
    cr->len = k;
}

/*
 * 【字符范围集合运算】
 * 功能：对两个字符范围执行集合运算（并、交、差、异或）
 * 参数：
 *   - cr: 输出结果的 CharRange（需已初始化）
 *   - a_pt, a_len: 第一个操作数的端点数组和长度
 *   - b_pt, b_len: 第二个操作数的端点数组和长度
 *   - op: 运算类型
 *       CR_OP_UNION  - 并集 (A ∪ B)
 *       CR_OP_INTER  - 交集 (A ∩ B)
 *       CR_OP_XOR    - 异或 (A ⊕ B)
 *       CR_OP_SUB    - 差集 (A - B)
 * 返回：0=成功，-1=失败
 * 
 * 算法：扫描线算法 (Sweep Line)
 * 1. 将两个端点数组按升序合并遍历
 * 2. 维护"进入/退出"状态：
 *    - a_idx & 1: A 集合的内外状态（奇数=内，偶数=外）
 *    - b_idx & 1: B 集合的内外状态
 * 3. 当结果的内外状态变化时，添加端点
 * 
 * 状态转换真值表：
 * 运算    | A 外 B 外 | A 内 B 外 | A 外 B 内 | A 内 B 内
 * --------+---------+---------+---------+--------
 * UNION   |   外    |   内    |   内    |   内
 * INTER   |   外    |   外    |   外    |   内
 * XOR     |   外    |   内    |   内    |   外
 * SUB     |   外    |   内    |   外    |   外
 */
int cr_op(CharRange *cr, const uint32_t *a_pt, int a_len,
          const uint32_t *b_pt, int b_len, int op)
{
    int a_idx, b_idx, is_in;
    uint32_t v;

    a_idx = 0;
    b_idx = 0;
    for(;;) {
        /* 按升序从 a 或 b 获取下一个端点 */
        if (a_idx < a_len && b_idx < b_len) {
            if (a_pt[a_idx] < b_pt[b_idx]) {
                goto a_add;
            } else if (a_pt[a_idx] == b_pt[b_idx]) {
                /* 端点重合，同时消耗两个 */
                v = a_pt[a_idx];
                a_idx++;
                b_idx++;
            } else {
                goto b_add;
            }
        } else if (a_idx < a_len) {
        a_add:
            v = a_pt[a_idx++];
        } else if (b_idx < b_len) {
        b_add:
            v = b_pt[b_idx++];
        } else {
            break;  /* 两个数组都处理完 */
        }
        /* 根据运算类型计算结果的内/外状态 */
        switch(op) {
        case CR_OP_UNION:
            is_in = (a_idx & 1) | (b_idx & 1);  /* 任一在内则在内 */
            break;
        case CR_OP_INTER:
            is_in = (a_idx & 1) & (b_idx & 1);  /* 都在内才在内 */
            break;
        case CR_OP_XOR:
            is_in = (a_idx & 1) ^ (b_idx & 1);  /* 仅一个在内 */
            break;
        case CR_OP_SUB:
            is_in = (a_idx & 1) & ((b_idx & 1) ^ 1);  /* 在 A 但不在 B */
            break;
        default:
            abort();
        }
        /* 状态变化时添加端点（形成新区间的起点或终点） */
        if (is_in != (cr->len & 1)) {
            if (cr_add_point(cr, v))
                return -1;
        }
    }
    cr_compress(cr);  /* 压缩结果，合并连续区间 */
    return 0;
}

int cr_op1(CharRange *cr, const uint32_t *b_pt, int b_len, int op)
{
    CharRange a = *cr;
    int ret;
    cr->len = 0;
    cr->size = 0;
    cr->points = NULL;
    ret = cr_op(cr, a.points, a.len, b_pt, b_len, op);
    cr_free(&a);
    return ret;
}

int cr_invert(CharRange *cr)
{
    int len;
    len = cr->len;
    if (cr_realloc(cr, len + 2))
        return -1;
    memmove(cr->points + 1, cr->points, len * sizeof(cr->points[0]));
    cr->points[0] = 0;
    cr->points[len + 1] = UINT32_MAX;
    cr->len = len + 2;
    cr_compress(cr);
    return 0;
}

#define CASE_U (1 << 0)
#define CASE_L (1 << 1)
#define CASE_F (1 << 2)

/* use the case conversion table to generate range of characters.
   CASE_U: set char if modified by uppercasing,
   CASE_L: set char if modified by lowercasing,
   CASE_F: set char if modified by case folding,
 */
static int unicode_case1(CharRange *cr, int case_mask)
{
#define MR(x) (1 << RUN_TYPE_ ## x)
    const uint32_t tab_run_mask[3] = {
        MR(U) | MR(UF) | MR(UL) | MR(LSU) | MR(U2L_399_EXT2) | MR(UF_D20) |
        MR(UF_D1_EXT) | MR(U_EXT) | MR(UF_EXT2) | MR(UF_EXT3),

        MR(L) | MR(LF) | MR(UL) | MR(LSU) | MR(U2L_399_EXT2) | MR(LF_EXT) | MR(LF_EXT2),

        MR(UF) | MR(LF) | MR(UL) | MR(LSU) | MR(U2L_399_EXT2) | MR(LF_EXT) | MR(LF_EXT2) | MR(UF_D20) | MR(UF_D1_EXT) | MR(LF_EXT) | MR(UF_EXT2) | MR(UF_EXT3),
    };
#undef MR
    uint32_t mask, v, code, type, len, i, idx;

    if (case_mask == 0)
        return 0;
    mask = 0;
    for(i = 0; i < 3; i++) {
        if ((case_mask >> i) & 1)
            mask |= tab_run_mask[i];
    }
    for(idx = 0; idx < countof(case_conv_table1); idx++) {
        v = case_conv_table1[idx];
        type = (v >> (32 - 17 - 7 - 4)) & 0xf;
        code = v >> (32 - 17);
        len = (v >> (32 - 17 - 7)) & 0x7f;
        if ((mask >> type) & 1) {
            //            printf("%d: type=%d %04x %04x\n", idx, type, code, code + len - 1);
            switch(type) {
            case RUN_TYPE_UL:
                if ((case_mask & CASE_U) && (case_mask & (CASE_L | CASE_F)))
                    goto def_case;
                code += ((case_mask & CASE_U) != 0);
                for(i = 0; i < len; i += 2) {
                    if (cr_add_interval(cr, code + i, code + i + 1))
                        return -1;
                }
                break;
            case RUN_TYPE_LSU:
                if ((case_mask & CASE_U) && (case_mask & (CASE_L | CASE_F)))
                    goto def_case;
                if (!(case_mask & CASE_U)) {
                    if (cr_add_interval(cr, code, code + 1))
                        return -1;
                }
                if (cr_add_interval(cr, code + 1, code + 2))
                    return -1;
                if (case_mask & CASE_U) {
                    if (cr_add_interval(cr, code + 2, code + 3))
                        return -1;
                }
                break;
            default:
            def_case:
                if (cr_add_interval(cr, code, code + len))
                    return -1;
                break;
            }
        }
    }
    return 0;
}

static int point_cmp(const void *p1, const void *p2, void *arg)
{
    uint32_t v1 = *(uint32_t *)p1;
    uint32_t v2 = *(uint32_t *)p2;
    return (v1 > v2) - (v1 < v2);
}

static void cr_sort_and_remove_overlap(CharRange *cr)
{
    uint32_t start, end, start1, end1, i, j;

    /* the resulting ranges are not necessarily sorted and may overlap */
    rqsort(cr->points, cr->len / 2, sizeof(cr->points[0]) * 2, point_cmp, NULL);
    j = 0;
    for(i = 0; i < cr->len; ) {
        start = cr->points[i];
        end = cr->points[i + 1];
        i += 2;
        while (i < cr->len) {
            start1 = cr->points[i];
            end1 = cr->points[i + 1];
            if (start1 > end) {
                /* |------|
                 *           |-------| */
                break;
            } else if (end1 <= end) {
                /* |------|
                 *    |--| */
                i += 2;
            } else {
                /* |------|
                 *     |-------| */
                end = end1;
                i += 2;
            }
        }
        cr->points[j] = start;
        cr->points[j + 1] = end;
        j += 2;
    }
    cr->len = j;
}

/* canonicalize a character set using the JS regex case folding rules
   (see lre_canonicalize()) */
int cr_regexp_canonicalize(CharRange *cr, BOOL is_unicode)
{
    CharRange cr_inter, cr_mask, cr_result, cr_sub;
    uint32_t v, code, len, i, idx, start, end, c, d_start, d_end, d;

    cr_init(&cr_mask, cr->mem_opaque, cr->realloc_func);
    cr_init(&cr_inter, cr->mem_opaque, cr->realloc_func);
    cr_init(&cr_result, cr->mem_opaque, cr->realloc_func);
    cr_init(&cr_sub, cr->mem_opaque, cr->realloc_func);

    if (unicode_case1(&cr_mask, is_unicode ? CASE_F : CASE_U))
        goto fail;
    if (cr_op(&cr_inter, cr_mask.points, cr_mask.len, cr->points, cr->len, CR_OP_INTER))
        goto fail;

    if (cr_invert(&cr_mask))
        goto fail;
    if (cr_op(&cr_sub, cr_mask.points, cr_mask.len, cr->points, cr->len, CR_OP_INTER))
        goto fail;

    /* cr_inter = cr & cr_mask */
    /* cr_sub = cr & ~cr_mask */

    /* use the case conversion table to compute the result */
    d_start = -1;
    d_end = -1;
    idx = 0;
    v = case_conv_table1[idx];
    code = v >> (32 - 17);
    len = (v >> (32 - 17 - 7)) & 0x7f;
    for(i = 0; i < cr_inter.len; i += 2) {
        start = cr_inter.points[i];
        end = cr_inter.points[i + 1];

        for(c = start; c < end; c++) {
            for(;;) {
                if (c >= code && c < code + len)
                    break;
                idx++;
                assert(idx < countof(case_conv_table1));
                v = case_conv_table1[idx];
                code = v >> (32 - 17);
                len = (v >> (32 - 17 - 7)) & 0x7f;
            }
            d = lre_case_folding_entry(c, idx, v, is_unicode);
            /* try to merge with the current interval */
            if (d_start == -1) {
                d_start = d;
                d_end = d + 1;
            } else if (d_end == d) {
                d_end++;
            } else {
                cr_add_interval(&cr_result, d_start, d_end);
                d_start = d;
                d_end = d + 1;
            }
        }
    }
    if (d_start != -1) {
        if (cr_add_interval(&cr_result, d_start, d_end))
            goto fail;
    }

    /* the resulting ranges are not necessarily sorted and may overlap */
    cr_sort_and_remove_overlap(&cr_result);

    /* or with the character not affected by the case folding */
    cr->len = 0;
    if (cr_op(cr, cr_result.points, cr_result.len, cr_sub.points, cr_sub.len, CR_OP_UNION))
        goto fail;

    cr_free(&cr_inter);
    cr_free(&cr_mask);
    cr_free(&cr_result);
    cr_free(&cr_sub);
    return 0;
 fail:
    cr_free(&cr_inter);
    cr_free(&cr_mask);
    cr_free(&cr_result);
    cr_free(&cr_sub);
    return -1;
}

#ifdef CONFIG_ALL_UNICODE

BOOL lre_is_id_start(uint32_t c)
{
    return lre_is_in_table(c, unicode_prop_ID_Start_table,
                           unicode_prop_ID_Start_index,
                           sizeof(unicode_prop_ID_Start_index) / 3);
}

BOOL lre_is_id_continue(uint32_t c)
{
    return lre_is_id_start(c) ||
        lre_is_in_table(c, unicode_prop_ID_Continue1_table,
                        unicode_prop_ID_Continue1_index,
                        sizeof(unicode_prop_ID_Continue1_index) / 3);
}

#define UNICODE_DECOMP_LEN_MAX 18

typedef enum {
    DECOMP_TYPE_C1, /* 16 bit char */
    DECOMP_TYPE_L1, /* 16 bit char table */
    DECOMP_TYPE_L2,
    DECOMP_TYPE_L3,
    DECOMP_TYPE_L4,
    DECOMP_TYPE_L5, /* XXX: not used */
    DECOMP_TYPE_L6, /* XXX: could remove */
    DECOMP_TYPE_L7, /* XXX: could remove */
    DECOMP_TYPE_LL1, /* 18 bit char table */
    DECOMP_TYPE_LL2,
    DECOMP_TYPE_S1, /* 8 bit char table */
    DECOMP_TYPE_S2,
    DECOMP_TYPE_S3,
    DECOMP_TYPE_S4,
    DECOMP_TYPE_S5,
    DECOMP_TYPE_I1, /* increment 16 bit char value */
    DECOMP_TYPE_I2_0,
    DECOMP_TYPE_I2_1,
    DECOMP_TYPE_I3_1,
    DECOMP_TYPE_I3_2,
    DECOMP_TYPE_I4_1,
    DECOMP_TYPE_I4_2,
    DECOMP_TYPE_B1, /* 16 bit base + 8 bit offset */
    DECOMP_TYPE_B2,
    DECOMP_TYPE_B3,
    DECOMP_TYPE_B4,
    DECOMP_TYPE_B5,
    DECOMP_TYPE_B6,
    DECOMP_TYPE_B7,
    DECOMP_TYPE_B8,
    DECOMP_TYPE_B18,
    DECOMP_TYPE_LS2,
    DECOMP_TYPE_PAT3,
    DECOMP_TYPE_S2_UL,
    DECOMP_TYPE_LS2_UL,
} DecompTypeEnum;

static uint32_t unicode_get_short_code(uint32_t c)
{
    static const uint16_t unicode_short_table[2] = { 0x2044, 0x2215 };

    if (c < 0x80)
        return c;
    else if (c < 0x80 + 0x50)
        return c - 0x80 + 0x300;
    else
        return unicode_short_table[c - 0x80 - 0x50];
}

static uint32_t unicode_get_lower_simple(uint32_t c)
{
    if (c < 0x100 || (c >= 0x410 && c <= 0x42f))
        c += 0x20;
    else
        c++;
    return c;
}

static uint16_t unicode_get16(const uint8_t *p)
{
    return p[0] | (p[1] << 8);
}

static int unicode_decomp_entry(uint32_t *res, uint32_t c,
                                int idx, uint32_t code, uint32_t len,
                                uint32_t type)
{
    uint32_t c1;
    int l, i, p;
    const uint8_t *d;

    if (type == DECOMP_TYPE_C1) {
        res[0] = unicode_decomp_table2[idx];
        return 1;
    } else {
        d = unicode_decomp_data + unicode_decomp_table2[idx];
        switch(type) {
        case DECOMP_TYPE_L1:
        case DECOMP_TYPE_L2:
        case DECOMP_TYPE_L3:
        case DECOMP_TYPE_L4:
        case DECOMP_TYPE_L5:
        case DECOMP_TYPE_L6:
        case DECOMP_TYPE_L7:
            l = type - DECOMP_TYPE_L1 + 1;
            d += (c - code) * l * 2;
            for(i = 0; i < l; i++) {
                if ((res[i] = unicode_get16(d + 2 * i)) == 0)
                    return 0;
            }
            return l;
        case DECOMP_TYPE_LL1:
        case DECOMP_TYPE_LL2:
            {
                uint32_t k, p;
                l = type - DECOMP_TYPE_LL1 + 1;
                k = (c - code) * l;
                p = len * l * 2;
                for(i = 0; i < l; i++) {
                    c1 = unicode_get16(d + 2 * k) |
                        (((d[p + (k / 4)] >> ((k % 4) * 2)) & 3) << 16);
                    if (!c1)
                        return 0;
                    res[i] = c1;
                    k++;
                }
            }
            return l;
        case DECOMP_TYPE_S1:
        case DECOMP_TYPE_S2:
        case DECOMP_TYPE_S3:
        case DECOMP_TYPE_S4:
        case DECOMP_TYPE_S5:
            l = type - DECOMP_TYPE_S1 + 1;
            d += (c - code) * l;
            for(i = 0; i < l; i++) {
                if ((res[i] = unicode_get_short_code(d[i])) == 0)
                    return 0;
            }
            return l;
        case DECOMP_TYPE_I1:
            l = 1;
            p = 0;
            goto decomp_type_i;
        case DECOMP_TYPE_I2_0:
        case DECOMP_TYPE_I2_1:
        case DECOMP_TYPE_I3_1:
        case DECOMP_TYPE_I3_2:
        case DECOMP_TYPE_I4_1:
        case DECOMP_TYPE_I4_2:
            l = 2 + ((type - DECOMP_TYPE_I2_0) >> 1);
            p = ((type - DECOMP_TYPE_I2_0) & 1) + (l > 2);
        decomp_type_i:
            for(i = 0; i < l; i++) {
                c1 = unicode_get16(d + 2 * i);
                if (i == p)
                    c1 += c - code;
                res[i] = c1;
            }
            return l;
        case DECOMP_TYPE_B18:
            l = 18;
            goto decomp_type_b;
        case DECOMP_TYPE_B1:
        case DECOMP_TYPE_B2:
        case DECOMP_TYPE_B3:
        case DECOMP_TYPE_B4:
        case DECOMP_TYPE_B5:
        case DECOMP_TYPE_B6:
        case DECOMP_TYPE_B7:
        case DECOMP_TYPE_B8:
            l = type - DECOMP_TYPE_B1 + 1;
        decomp_type_b:
            {
                uint32_t c_min;
                c_min = unicode_get16(d);
                d += 2 + (c - code) * l;
                for(i = 0; i < l; i++) {
                    c1 = d[i];
                    if (c1 == 0xff)
                        c1 = 0x20;
                    else
                        c1 += c_min;
                    res[i] = c1;
                }
            }
            return l;
        case DECOMP_TYPE_LS2:
            d += (c - code) * 3;
            if (!(res[0] = unicode_get16(d)))
                return 0;
            res[1] = unicode_get_short_code(d[2]);
            return 2;
        case DECOMP_TYPE_PAT3:
            res[0] = unicode_get16(d);
            res[2] = unicode_get16(d + 2);
            d += 4 + (c - code) * 2;
            res[1] = unicode_get16(d);
            return 3;
        case DECOMP_TYPE_S2_UL:
        case DECOMP_TYPE_LS2_UL:
            c1 = c - code;
            if (type == DECOMP_TYPE_S2_UL) {
                d += c1 & ~1;
                c = unicode_get_short_code(*d);
                d++;
            } else {
                d += (c1 >> 1) * 3;
                c = unicode_get16(d);
                d += 2;
            }
            if (c1 & 1)
                c = unicode_get_lower_simple(c);
            res[0] = c;
            res[1] = unicode_get_short_code(*d);
            return 2;
        }
    }
    return 0;
}


/* return the length of the decomposition (length <=
   UNICODE_DECOMP_LEN_MAX) or 0 if no decomposition */
static int unicode_decomp_char(uint32_t *res, uint32_t c, BOOL is_compat1)
{
    uint32_t v, type, is_compat, code, len;
    int idx_min, idx_max, idx;

    idx_min = 0;
    idx_max = countof(unicode_decomp_table1) - 1;
    while (idx_min <= idx_max) {
        idx = (idx_max + idx_min) / 2;
        v = unicode_decomp_table1[idx];
        code = v >> (32 - 18);
        len = (v >> (32 - 18 - 7)) & 0x7f;
        //        printf("idx=%d code=%05x len=%d\n", idx, code, len);
        if (c < code) {
            idx_max = idx - 1;
        } else if (c >= code + len) {
            idx_min = idx + 1;
        } else {
            is_compat = v & 1;
            if (is_compat1 < is_compat)
                break;
            type = (v >> (32 - 18 - 7 - 6)) & 0x3f;
            return unicode_decomp_entry(res, c, idx, code, len, type);
        }
    }
    return 0;
}

/* return 0 if no pair found */
static int unicode_compose_pair(uint32_t c0, uint32_t c1)
{
    uint32_t code, len, type, v, idx1, d_idx, d_offset, ch;
    int idx_min, idx_max, idx, d;
    uint32_t pair[2];

    idx_min = 0;
    idx_max = countof(unicode_comp_table) - 1;
    while (idx_min <= idx_max) {
        idx = (idx_max + idx_min) / 2;
        idx1 = unicode_comp_table[idx];

        /* idx1 represent an entry of the decomposition table */
        d_idx = idx1 >> 6;
        d_offset = idx1 & 0x3f;
        v = unicode_decomp_table1[d_idx];
        code = v >> (32 - 18);
        len = (v >> (32 - 18 - 7)) & 0x7f;
        type = (v >> (32 - 18 - 7 - 6)) & 0x3f;
        ch = code + d_offset;
        unicode_decomp_entry(pair, ch, d_idx, code, len, type);
        d = c0 - pair[0];
        if (d == 0)
            d = c1 - pair[1];
        if (d < 0) {
            idx_max = idx - 1;
        } else if (d > 0) {
            idx_min = idx + 1;
        } else {
            return ch;
        }
    }
    return 0;
}

/* return the combining class of character c (between 0 and 255) */
static int unicode_get_cc(uint32_t c)
{
    uint32_t code, n, type, cc, c1, b;
    int pos;
    const uint8_t *p;

    pos = get_index_pos(&code, c,
                        unicode_cc_index, sizeof(unicode_cc_index) / 3);
    if (pos < 0)
        return 0;
    p = unicode_cc_table + pos;
    /* Compressed run length encoding:
       - 2 high order bits are combining class type
       -         0:0, 1:230, 2:extra byte linear progression, 3:extra byte
       - 00..2F: range length (add 1)
       - 30..37: 3-bit range-length + 1 extra byte
       - 38..3F: 3-bit range-length + 2 extra byte
     */
    for(;;) {
        b = *p++;
        type = b >> 6;
        n = b & 0x3f;
        if (n < 48) {
        } else if (n < 56) {
            n = (n - 48) << 8;
            n |= *p++;
            n += 48;
        } else {
            n = (n - 56) << 8;
            n |= *p++ << 8;
            n |= *p++;
            n += 48 + (1 << 11);
        }
        if (type <= 1)
            p++;
        c1 = code + n + 1;
        if (c < c1) {
            switch(type) {
            case 0:
                cc = p[-1];
                break;
            case 1:
                cc = p[-1] + c - code;
                break;
            case 2:
                cc = 0;
                break;
            default:
            case 3:
                cc = 230;
                break;
            }
            return cc;
        }
        code = c1;
    }
}

static void sort_cc(int *buf, int len)
{
    int i, j, k, cc, cc1, start, ch1;

    for(i = 0; i < len; i++) {
        cc = unicode_get_cc(buf[i]);
        if (cc != 0) {
            start = i;
            j = i + 1;
            while (j < len) {
                ch1 = buf[j];
                cc1 = unicode_get_cc(ch1);
                if (cc1 == 0)
                    break;
                k = j - 1;
                while (k >= start) {
                    if (unicode_get_cc(buf[k]) <= cc1)
                        break;
                    buf[k + 1] = buf[k];
                    k--;
                }
                buf[k + 1] = ch1;
                j++;
            }
#if 0
            printf("cc:");
            for(k = start; k < j; k++) {
                printf(" %3d", unicode_get_cc(buf[k]));
            }
            printf("\n");
#endif
            i = j;
        }
    }
}

static void to_nfd_rec(DynBuf *dbuf,
                       const int *src, int src_len, int is_compat)
{
    uint32_t c, v;
    int i, l;
    uint32_t res[UNICODE_DECOMP_LEN_MAX];

    for(i = 0; i < src_len; i++) {
        c = src[i];
        if (c >= 0xac00 && c < 0xd7a4) {
            /* Hangul decomposition */
            c -= 0xac00;
            dbuf_put_u32(dbuf, 0x1100 + c / 588);
            dbuf_put_u32(dbuf, 0x1161 + (c % 588) / 28);
            v = c % 28;
            if (v != 0)
                dbuf_put_u32(dbuf, 0x11a7 + v);
        } else {
            l = unicode_decomp_char(res, c, is_compat);
            if (l) {
                to_nfd_rec(dbuf, (int *)res, l, is_compat);
            } else {
                dbuf_put_u32(dbuf, c);
            }
        }
    }
}

/* return 0 if not found */
static int compose_pair(uint32_t c0, uint32_t c1)
{
    /* Hangul composition */
    if (c0 >= 0x1100 && c0 < 0x1100 + 19 &&
        c1 >= 0x1161 && c1 < 0x1161 + 21) {
        return 0xac00 + (c0 - 0x1100) * 588 + (c1 - 0x1161) * 28;
    } else if (c0 >= 0xac00 && c0 < 0xac00 + 11172 &&
               (c0 - 0xac00) % 28 == 0 &&
               c1 >= 0x11a7 && c1 < 0x11a7 + 28) {
        return c0 + c1 - 0x11a7;
    } else {
        return unicode_compose_pair(c0, c1);
    }
}

int unicode_normalize(uint32_t **pdst, const uint32_t *src, int src_len,
                      UnicodeNormalizationEnum n_type,
                      void *opaque, DynBufReallocFunc *realloc_func)
{
    int *buf, buf_len, i, p, starter_pos, cc, last_cc, out_len;
    BOOL is_compat;
    DynBuf dbuf_s, *dbuf = &dbuf_s;

    is_compat = n_type >> 1;

    dbuf_init2(dbuf, opaque, realloc_func);
    if (dbuf_claim(dbuf, sizeof(int) * src_len))
        goto fail;

    /* common case: latin1 is unaffected by NFC */
    if (n_type == UNICODE_NFC) {
        for(i = 0; i < src_len; i++) {
            if (src[i] >= 0x100)
                goto not_latin1;
        }
        buf = (int *)dbuf->buf;
        memcpy(buf, src, src_len * sizeof(int));
        *pdst = (uint32_t *)buf;
        return src_len;
    not_latin1: ;
    }

    to_nfd_rec(dbuf, (const int *)src, src_len, is_compat);
    if (dbuf_error(dbuf)) {
    fail:
        *pdst = NULL;
        return -1;
    }
    buf = (int *)dbuf->buf;
    buf_len = dbuf->size / sizeof(int);

    sort_cc(buf, buf_len);

    if (buf_len <= 1 || (n_type & 1) != 0) {
        /* NFD / NFKD */
        *pdst = (uint32_t *)buf;
        return buf_len;
    }

    i = 1;
    out_len = 1;
    while (i < buf_len) {
        /* find the starter character and test if it is blocked from
           the character at 'i' */
        last_cc = unicode_get_cc(buf[i]);
        starter_pos = out_len - 1;
        while (starter_pos >= 0) {
            cc = unicode_get_cc(buf[starter_pos]);
            if (cc == 0)
                break;
            if (cc >= last_cc)
                goto next;
            last_cc = 256;
            starter_pos--;
        }
        if (starter_pos >= 0 &&
            (p = compose_pair(buf[starter_pos], buf[i])) != 0) {
            buf[starter_pos] = p;
            i++;
        } else {
        next:
            buf[out_len++] = buf[i++];
        }
    }
    *pdst = (uint32_t *)buf;
    return out_len;
}

/* char ranges for various unicode properties */

static int unicode_find_name(const char *name_table, const char *name)
{
    const char *p, *r;
    int pos;
    size_t name_len, len;

    p = name_table;
    pos = 0;
    name_len = strlen(name);
    while (*p) {
        for(;;) {
            r = strchr(p, ',');
            if (!r)
                len = strlen(p);
            else
                len = r - p;
            if (len == name_len && !memcmp(p, name, name_len))
                return pos;
            p += len + 1;
            if (!r)
                break;
        }
        pos++;
    }
    return -1;
}

/* 'cr' must be initialized and empty. Return 0 if OK, -1 if error, -2
   if not found */
int unicode_script(CharRange *cr,
                   const char *script_name, BOOL is_ext)
{
    int script_idx;
    const uint8_t *p, *p_end;
    uint32_t c, c1, b, n, v, v_len, i, type;
    CharRange cr1_s, *cr1;
    CharRange cr2_s, *cr2 = &cr2_s;
    BOOL is_common;

    script_idx = unicode_find_name(unicode_script_name_table, script_name);
    if (script_idx < 0)
        return -2;

    is_common = (script_idx == UNICODE_SCRIPT_Common ||
                 script_idx == UNICODE_SCRIPT_Inherited);
    if (is_ext) {
        cr1 = &cr1_s;
        cr_init(cr1, cr->mem_opaque, cr->realloc_func);
        cr_init(cr2, cr->mem_opaque, cr->realloc_func);
    } else {
        cr1 = cr;
    }

    p = unicode_script_table;
    p_end = unicode_script_table + countof(unicode_script_table);
    c = 0;
    while (p < p_end) {
        b = *p++;
        type = b >> 7;
        n = b & 0x7f;
        if (n < 96) {
        } else if (n < 112) {
            n = (n - 96) << 8;
            n |= *p++;
            n += 96;
        } else {
            n = (n - 112) << 16;
            n |= *p++ << 8;
            n |= *p++;
            n += 96 + (1 << 12);
        }
        c1 = c + n + 1;
        if (type != 0) {
            v = *p++;
            if (v == script_idx || script_idx == UNICODE_SCRIPT_Unknown) {
                if (cr_add_interval(cr1, c, c1))
                    goto fail;
            }
        }
        c = c1;
    }
    if (script_idx == UNICODE_SCRIPT_Unknown) {
        /* Unknown is all the characters outside scripts */
        if (cr_invert(cr1))
            goto fail;
    }

    if (is_ext) {
        /* add the script extensions */
        p = unicode_script_ext_table;
        p_end = unicode_script_ext_table + countof(unicode_script_ext_table);
        c = 0;
        while (p < p_end) {
            b = *p++;
            if (b < 128) {
                n = b;
            } else if (b < 128 + 64) {
                n = (b - 128) << 8;
                n |= *p++;
                n += 128;
            } else {
                n = (b - 128 - 64) << 16;
                n |= *p++ << 8;
                n |= *p++;
                n += 128 + (1 << 14);
            }
            c1 = c + n + 1;
            v_len = *p++;
            if (is_common) {
                if (v_len != 0) {
                    if (cr_add_interval(cr2, c, c1))
                        goto fail;
                }
            } else {
                for(i = 0; i < v_len; i++) {
                    if (p[i] == script_idx) {
                        if (cr_add_interval(cr2, c, c1))
                            goto fail;
                        break;
                    }
                }
            }
            p += v_len;
            c = c1;
        }
        if (is_common) {
            /* remove all the characters with script extensions */
            if (cr_invert(cr2))
                goto fail;
            if (cr_op(cr, cr1->points, cr1->len, cr2->points, cr2->len,
                      CR_OP_INTER))
                goto fail;
        } else {
            if (cr_op(cr, cr1->points, cr1->len, cr2->points, cr2->len,
                      CR_OP_UNION))
                goto fail;
        }
        cr_free(cr1);
        cr_free(cr2);
    }
    return 0;
 fail:
    if (is_ext) {
        cr_free(cr1);
        cr_free(cr2);
    }
    goto fail;
}

#define M(id) (1U << UNICODE_GC_ ## id)

static int unicode_general_category1(CharRange *cr, uint32_t gc_mask)
{
    const uint8_t *p, *p_end;
    uint32_t c, c0, b, n, v;

    p = unicode_gc_table;
    p_end = unicode_gc_table + countof(unicode_gc_table);
    c = 0;
    /* Compressed range encoding:
       initial byte:
       bits 0..4: category number (special case 31)
       bits 5..7: range length (add 1)
       special case bits 5..7 == 7: read an extra byte
       - 00..7F: range length (add 7 + 1)
       - 80..BF: 6-bits plus extra byte for range length (add 7 + 128)
       - C0..FF: 6-bits plus 2 extra bytes for range length (add 7 + 128 + 16384)
     */
    while (p < p_end) {
        b = *p++;
        n = b >> 5;
        v = b & 0x1f;
        if (n == 7) {
            n = *p++;
            if (n < 128) {
                n += 7;
            } else if (n < 128 + 64) {
                n = (n - 128) << 8;
                n |= *p++;
                n += 7 + 128;
            } else {
                n = (n - 128 - 64) << 16;
                n |= *p++ << 8;
                n |= *p++;
                n += 7 + 128 + (1 << 14);
            }
        }
        c0 = c;
        c += n + 1;
        if (v == 31) {
            /* run of Lu / Ll */
            b = gc_mask & (M(Lu) | M(Ll));
            if (b != 0) {
                if (b == (M(Lu) | M(Ll))) {
                    goto add_range;
                } else {
                    c0 += ((gc_mask & M(Ll)) != 0);
                    for(; c0 < c; c0 += 2) {
                        if (cr_add_interval(cr, c0, c0 + 1))
                            return -1;
                    }
                }
            }
        } else if ((gc_mask >> v) & 1) {
        add_range:
            if (cr_add_interval(cr, c0, c))
                return -1;
        }
    }
    return 0;
}

static int unicode_prop1(CharRange *cr, int prop_idx)
{
    const uint8_t *p, *p_end;
    uint32_t c, c0, b, bit;

    p = unicode_prop_table[prop_idx];
    p_end = p + unicode_prop_len_table[prop_idx];
    c = 0;
    bit = 0;
    /* Compressed range encoding:
       00..3F: 2 packed lengths: 3-bit + 3-bit
       40..5F: 5-bits plus extra byte for length
       60..7F: 5-bits plus 2 extra bytes for length
       80..FF: 7-bit length
       lengths must be incremented to get character count
       Ranges alternate between false and true return value.
     */
    while (p < p_end) {
        c0 = c;
        b = *p++;
        if (b < 64) {
            c += (b >> 3) + 1;
            if (bit)  {
                if (cr_add_interval(cr, c0, c))
                    return -1;
            }
            bit ^= 1;
            c0 = c;
            c += (b & 7) + 1;
        } else if (b >= 0x80) {
            c += b - 0x80 + 1;
        } else if (b < 0x60) {
            c += (((b - 0x40) << 8) | p[0]) + 1;
            p++;
        } else {
            c += (((b - 0x60) << 16) | (p[0] << 8) | p[1]) + 1;
            p += 2;
        }
        if (bit)  {
            if (cr_add_interval(cr, c0, c))
                return -1;
        }
        bit ^= 1;
    }
    return 0;
}

typedef enum {
    POP_GC,
    POP_PROP,
    POP_CASE,
    POP_UNION,
    POP_INTER,
    POP_XOR,
    POP_INVERT,
    POP_END,
} PropOPEnum;

#define POP_STACK_LEN_MAX 4

static int unicode_prop_ops(CharRange *cr, ...)
{
    va_list ap;
    CharRange stack[POP_STACK_LEN_MAX];
    int stack_len, op, ret, i;
    uint32_t a;

    va_start(ap, cr);
    stack_len = 0;
    for(;;) {
        op = va_arg(ap, int);
        switch(op) {
        case POP_GC:
            assert(stack_len < POP_STACK_LEN_MAX);
            a = va_arg(ap, int);
            cr_init(&stack[stack_len++], cr->mem_opaque, cr->realloc_func);
            if (unicode_general_category1(&stack[stack_len - 1], a))
                goto fail;
            break;
        case POP_PROP:
            assert(stack_len < POP_STACK_LEN_MAX);
            a = va_arg(ap, int);
            cr_init(&stack[stack_len++], cr->mem_opaque, cr->realloc_func);
            if (unicode_prop1(&stack[stack_len - 1], a))
                goto fail;
            break;
        case POP_CASE:
            assert(stack_len < POP_STACK_LEN_MAX);
            a = va_arg(ap, int);
            cr_init(&stack[stack_len++], cr->mem_opaque, cr->realloc_func);
            if (unicode_case1(&stack[stack_len - 1], a))
                goto fail;
            break;
        case POP_UNION:
        case POP_INTER:
        case POP_XOR:
            {
                CharRange *cr1, *cr2, *cr3;
                assert(stack_len >= 2);
                assert(stack_len < POP_STACK_LEN_MAX);
                cr1 = &stack[stack_len - 2];
                cr2 = &stack[stack_len - 1];
                cr3 = &stack[stack_len++];
                cr_init(cr3, cr->mem_opaque, cr->realloc_func);
                /* CR_OP_XOR may be used here */
                if (cr_op(cr3, cr1->points, cr1->len,
                          cr2->points, cr2->len, op - POP_UNION + CR_OP_UNION))
                    goto fail;
                cr_free(cr1);
                cr_free(cr2);
                *cr1 = *cr3;
                stack_len -= 2;
            }
            break;
        case POP_INVERT:
            assert(stack_len >= 1);
            if (cr_invert(&stack[stack_len - 1]))
                goto fail;
            break;
        case POP_END:
            goto done;
        default:
            abort();
        }
    }
 done:
    assert(stack_len == 1);
    ret = cr_copy(cr, &stack[0]);
    cr_free(&stack[0]);
    return ret;
 fail:
    for(i = 0; i < stack_len; i++)
        cr_free(&stack[i]);
    return -1;
}

static const uint32_t unicode_gc_mask_table[] = {
    M(Lu) | M(Ll) | M(Lt), /* LC */
    M(Lu) | M(Ll) | M(Lt) | M(Lm) | M(Lo), /* L */
    M(Mn) | M(Mc) | M(Me), /* M */
    M(Nd) | M(Nl) | M(No), /* N */
    M(Sm) | M(Sc) | M(Sk) | M(So), /* S */
    M(Pc) | M(Pd) | M(Ps) | M(Pe) | M(Pi) | M(Pf) | M(Po), /* P */
    M(Zs) | M(Zl) | M(Zp), /* Z */
    M(Cc) | M(Cf) | M(Cs) | M(Co) | M(Cn), /* C */
};

/* 'cr' must be initialized and empty. Return 0 if OK, -1 if error, -2
   if not found */
int unicode_general_category(CharRange *cr, const char *gc_name)
{
    int gc_idx;
    uint32_t gc_mask;

    gc_idx = unicode_find_name(unicode_gc_name_table, gc_name);
    if (gc_idx < 0)
        return -2;
    if (gc_idx <= UNICODE_GC_Co) {
        gc_mask = (uint64_t)1 << gc_idx;
    } else {
        gc_mask = unicode_gc_mask_table[gc_idx - UNICODE_GC_LC];
    }
    return unicode_general_category1(cr, gc_mask);
}


/* 'cr' must be initialized and empty. Return 0 if OK, -1 if error, -2
   if not found */
int unicode_prop(CharRange *cr, const char *prop_name)
{
    int prop_idx, ret;

    prop_idx = unicode_find_name(unicode_prop_name_table, prop_name);
    if (prop_idx < 0)
        return -2;
    prop_idx += UNICODE_PROP_ASCII_Hex_Digit;

    ret = 0;
    switch(prop_idx) {
    case UNICODE_PROP_ASCII:
        if (cr_add_interval(cr, 0x00, 0x7f + 1))
            return -1;
        break;
    case UNICODE_PROP_Any:
        if (cr_add_interval(cr, 0x00000, 0x10ffff + 1))
            return -1;
        break;
    case UNICODE_PROP_Assigned:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Cn),
                               POP_INVERT,
                               POP_END);
        break;
    case UNICODE_PROP_Math:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Sm),
                               POP_PROP, UNICODE_PROP_Other_Math,
                               POP_UNION,
                               POP_END);
        break;
    case UNICODE_PROP_Lowercase:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Ll),
                               POP_PROP, UNICODE_PROP_Other_Lowercase,
                               POP_UNION,
                               POP_END);
        break;
    case UNICODE_PROP_Uppercase:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu),
                               POP_PROP, UNICODE_PROP_Other_Uppercase,
                               POP_UNION,
                               POP_END);
        break;
    case UNICODE_PROP_Cased:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu) | M(Ll) | M(Lt),
                               POP_PROP, UNICODE_PROP_Other_Uppercase,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Other_Lowercase,
                               POP_UNION,
                               POP_END);
        break;
    case UNICODE_PROP_Alphabetic:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu) | M(Ll) | M(Lt) | M(Lm) | M(Lo) | M(Nl),
                               POP_PROP, UNICODE_PROP_Other_Uppercase,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Other_Lowercase,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Other_Alphabetic,
                               POP_UNION,
                               POP_END);
        break;
    case UNICODE_PROP_Grapheme_Base:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Cc) | M(Cf) | M(Cs) | M(Co) | M(Cn) | M(Zl) | M(Zp) | M(Me) | M(Mn),
                               POP_PROP, UNICODE_PROP_Other_Grapheme_Extend,
                               POP_UNION,
                               POP_INVERT,
                               POP_END);
        break;
    case UNICODE_PROP_Grapheme_Extend:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Me) | M(Mn),
                               POP_PROP, UNICODE_PROP_Other_Grapheme_Extend,
                               POP_UNION,
                               POP_END);
        break;
    case UNICODE_PROP_XID_Start:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu) | M(Ll) | M(Lt) | M(Lm) | M(Lo) | M(Nl),
                               POP_PROP, UNICODE_PROP_Other_ID_Start,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Pattern_Syntax,
                               POP_PROP, UNICODE_PROP_Pattern_White_Space,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_XID_Start1,
                               POP_UNION,
                               POP_INVERT,
                               POP_INTER,
                               POP_END);
        break;
    case UNICODE_PROP_XID_Continue:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu) | M(Ll) | M(Lt) | M(Lm) | M(Lo) | M(Nl) |
                               M(Mn) | M(Mc) | M(Nd) | M(Pc),
                               POP_PROP, UNICODE_PROP_Other_ID_Start,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Other_ID_Continue,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Pattern_Syntax,
                               POP_PROP, UNICODE_PROP_Pattern_White_Space,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_XID_Continue1,
                               POP_UNION,
                               POP_INVERT,
                               POP_INTER,
                               POP_END);
        break;
    case UNICODE_PROP_Changes_When_Uppercased:
        ret = unicode_case1(cr, CASE_U);
        break;
    case UNICODE_PROP_Changes_When_Lowercased:
        ret = unicode_case1(cr, CASE_L);
        break;
    case UNICODE_PROP_Changes_When_Casemapped:
        ret = unicode_case1(cr, CASE_U | CASE_L | CASE_F);
        break;
    case UNICODE_PROP_Changes_When_Titlecased:
        ret = unicode_prop_ops(cr,
                               POP_CASE, CASE_U,
                               POP_PROP, UNICODE_PROP_Changes_When_Titlecased1,
                               POP_XOR,
                               POP_END);
        break;
    case UNICODE_PROP_Changes_When_Casefolded:
        ret = unicode_prop_ops(cr,
                               POP_CASE, CASE_F,
                               POP_PROP, UNICODE_PROP_Changes_When_Casefolded1,
                               POP_XOR,
                               POP_END);
        break;
    case UNICODE_PROP_Changes_When_NFKC_Casefolded:
        ret = unicode_prop_ops(cr,
                               POP_CASE, CASE_F,
                               POP_PROP, UNICODE_PROP_Changes_When_NFKC_Casefolded1,
                               POP_XOR,
                               POP_END);
        break;
#if 0
    case UNICODE_PROP_ID_Start:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu) | M(Ll) | M(Lt) | M(Lm) | M(Lo) | M(Nl),
                               POP_PROP, UNICODE_PROP_Other_ID_Start,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Pattern_Syntax,
                               POP_PROP, UNICODE_PROP_Pattern_White_Space,
                               POP_UNION,
                               POP_INVERT,
                               POP_INTER,
                               POP_END);
        break;
    case UNICODE_PROP_ID_Continue:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Lu) | M(Ll) | M(Lt) | M(Lm) | M(Lo) | M(Nl) |
                               M(Mn) | M(Mc) | M(Nd) | M(Pc),
                               POP_PROP, UNICODE_PROP_Other_ID_Start,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Other_ID_Continue,
                               POP_UNION,
                               POP_PROP, UNICODE_PROP_Pattern_Syntax,
                               POP_PROP, UNICODE_PROP_Pattern_White_Space,
                               POP_UNION,
                               POP_INVERT,
                               POP_INTER,
                               POP_END);
        break;
    case UNICODE_PROP_Case_Ignorable:
        ret = unicode_prop_ops(cr,
                               POP_GC, M(Mn) | M(Cf) | M(Lm) | M(Sk),
                               POP_PROP, UNICODE_PROP_Case_Ignorable1,
                               POP_XOR,
                               POP_END);
        break;
#else
        /* we use the existing tables */
    case UNICODE_PROP_ID_Continue:
        ret = unicode_prop_ops(cr,
                               POP_PROP, UNICODE_PROP_ID_Start,
                               POP_PROP, UNICODE_PROP_ID_Continue1,
                               POP_XOR,
                               POP_END);
        break;
#endif
    default:
        if (prop_idx >= countof(unicode_prop_table))
            return -2;
        ret = unicode_prop1(cr, prop_idx);
        break;
    }
    return ret;
}

#endif /* CONFIG_ALL_UNICODE */

/*---- lre codepoint categorizing functions ----*/

#define S  UNICODE_C_SPACE
#define D  UNICODE_C_DIGIT
#define X  UNICODE_C_XDIGIT
#define U  UNICODE_C_UPPER
#define L  UNICODE_C_LOWER
#define _  UNICODE_C_UNDER
#define d  UNICODE_C_DOLLAR

uint8_t const lre_ctype_bits[256] = {
    0, 0, 0, 0, 0, 0, 0, 0,
    0, S, S, S, S, S, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,

    S, 0, 0, 0, d, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    X|D, X|D, X|D, X|D, X|D, X|D, X|D, X|D,
    X|D, X|D, 0, 0, 0, 0, 0, 0,

    0, X|U, X|U, X|U, X|U, X|U, X|U, U,
    U, U, U, U, U, U, U, U,
    U, U, U, U, U, U, U, U,
    U, U, U, 0, 0, 0, 0, _,

    0, X|L, X|L, X|L, X|L, X|L, X|L, L,
    L, L, L, L, L, L, L, L,
    L, L, L, L, L, L, L, L,
    L, L, L, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,

    S, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,

    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
};

#undef S
#undef D
#undef X
#undef U
#undef L
#undef _
#undef d

/* code point ranges for Zs,Zl or Zp property */
static const uint16_t char_range_s[] = {
    10,
    0x0009, 0x000D + 1,
    0x0020, 0x0020 + 1,
    0x00A0, 0x00A0 + 1,
    0x1680, 0x1680 + 1,
    0x2000, 0x200A + 1,
    /* 2028;LINE SEPARATOR;Zl;0;WS;;;;;N;;;;; */
    /* 2029;PARAGRAPH SEPARATOR;Zp;0;B;;;;;N;;;;; */
    0x2028, 0x2029 + 1,
    0x202F, 0x202F + 1,
    0x205F, 0x205F + 1,
    0x3000, 0x3000 + 1,
    /* FEFF;ZERO WIDTH NO-BREAK SPACE;Cf;0;BN;;;;;N;BYTE ORDER MARK;;;; */
    0xFEFF, 0xFEFF + 1,
};

BOOL lre_is_space_non_ascii(uint32_t c)
{
    size_t i, n;

    n = countof(char_range_s);
    for(i = 5; i < n; i += 2) {
        uint32_t low = char_range_s[i];
        uint32_t high = char_range_s[i + 1];
        if (c < low)
            return FALSE;
        if (c < high)
            return TRUE;
    }
    return FALSE;
}

#define SEQ_MAX_LEN 16

static int unicode_sequence_prop1(int seq_prop_idx, UnicodeSequencePropCB *cb, void *opaque,
                                  CharRange *cr)
{
    int i, c, j;
    uint32_t seq[SEQ_MAX_LEN];
    
    switch(seq_prop_idx) {
    case UNICODE_SEQUENCE_PROP_Basic_Emoji:
        if (unicode_prop1(cr, UNICODE_PROP_Basic_Emoji1) < 0)
            return -1;
        for(i = 0; i < cr->len; i += 2) {
            for(c = cr->points[i]; c < cr->points[i + 1]; c++) {
                seq[0] = c;
                cb(opaque, seq, 1);
            }
        }

        cr->len = 0;

        if (unicode_prop1(cr, UNICODE_PROP_Basic_Emoji2) < 0)
            return -1;
        for(i = 0; i < cr->len; i += 2) {
            for(c = cr->points[i]; c < cr->points[i + 1]; c++) {
                seq[0] = c;
                seq[1] = 0xfe0f;
                cb(opaque, seq, 2);
            }
        }

        break;
    case UNICODE_SEQUENCE_PROP_RGI_Emoji_Modifier_Sequence:
        if (unicode_prop1(cr, UNICODE_PROP_Emoji_Modifier_Base) < 0)
            return -1;
        for(i = 0; i < cr->len; i += 2) {
            for(c = cr->points[i]; c < cr->points[i + 1]; c++) {
                for(j = 0; j < 5; j++) {
                    seq[0] = c;
                    seq[1] = 0x1f3fb + j;
                    cb(opaque, seq, 2);
                }
            }
        }
        break;
    case UNICODE_SEQUENCE_PROP_RGI_Emoji_Flag_Sequence:
        if (unicode_prop1(cr, UNICODE_PROP_RGI_Emoji_Flag_Sequence) < 0)
            return -1;
        for(i = 0; i < cr->len; i += 2) {
            for(c = cr->points[i]; c < cr->points[i + 1]; c++) {
                int c0, c1;
                c0 = c / 26;
                c1 = c % 26;
                seq[0] = 0x1F1E6 + c0;
                seq[1] = 0x1F1E6 + c1;
                cb(opaque, seq, 2);
            }
        }
        break;
    case UNICODE_SEQUENCE_PROP_RGI_Emoji_ZWJ_Sequence:
        {
            int len, code, pres, k, mod, mod_count, mod_pos[2], hc_pos, n_mod, n_hc, mod1;
            int mod_idx, hc_idx, i0, i1;
            const uint8_t *tab = unicode_rgi_emoji_zwj_sequence;
            
            for(i = 0; i < countof(unicode_rgi_emoji_zwj_sequence);) {
                len = tab[i++];
                k = 0;
                mod = 0;
                mod_count = 0;
                hc_pos = -1;
                for(j = 0; j < len; j++) {
                    code = tab[i++];
                    code |= tab[i++] << 8;
                    pres = code >> 15;
                    mod1 = (code >> 13) & 3;
                    code &= 0x1fff;
                    if (code < 0x1000) {
                        c = code + 0x2000;
                    } else {
                        c = 0x1f000 + (code - 0x1000);
                    }
                    if (c == 0x1f9b0)
                        hc_pos = k;
                    seq[k++] = c;
                    if (mod1 != 0) {
                        assert(mod_count < 2);
                        mod = mod1;
                        mod_pos[mod_count++] = k;
                        seq[k++] = 0; /* will be filled later */
                    }
                    if (pres) {
                        seq[k++] = 0xfe0f;
                    }
                    if (j < len - 1) {
                        seq[k++] = 0x200d;
                    }
                }

                /* genrate all the variants */
                switch(mod) {
                case 1:
                    n_mod = 5;
                    break;
                case 2:
                    n_mod = 25;
                    break;
                case 3:
                    n_mod = 20;
                    break;
                default:
                    n_mod = 1;
                    break;
                }
                if (hc_pos >= 0)
                    n_hc = 4;
                else
                    n_hc = 1;
                for(hc_idx = 0; hc_idx < n_hc; hc_idx++) {
                    for(mod_idx = 0; mod_idx < n_mod; mod_idx++) {
                        if (hc_pos >= 0)
                            seq[hc_pos] = 0x1f9b0 + hc_idx;
                        
                        switch(mod) {
                        case 1:
                            seq[mod_pos[0]] = 0x1f3fb + mod_idx;
                            break;
                        case 2:
                        case 3:
                            i0 = mod_idx / 5;
                            i1 = mod_idx % 5;
                            /* avoid identical values */
                            if (mod == 3 && i0 >= i1)
                                i0++;
                            seq[mod_pos[0]] = 0x1f3fb + i0;
                            seq[mod_pos[1]] = 0x1f3fb + i1;
                            break;
                        default:
                            break;
                        }
#if 0
                        for(j = 0; j < k; j++)
                            printf(" %04x", seq[j]);
                        printf("\n");
#endif                
                        cb(opaque, seq, k);
                    }
                }
            }
        }
        break;
    case UNICODE_SEQUENCE_PROP_RGI_Emoji_Tag_Sequence:
        {
            for(i = 0; i < countof(unicode_rgi_emoji_tag_sequence);) {
                j = 0;
                seq[j++] = 0x1F3F4;
                for(;;) {
                    c = unicode_rgi_emoji_tag_sequence[i++];
                    if (c == 0x00)
                        break;
                    seq[j++] = 0xe0000 + c;
                }
                seq[j++] = 0xe007f;
                cb(opaque, seq, j);
            }
        }
        break;
    case UNICODE_SEQUENCE_PROP_Emoji_Keycap_Sequence:
        if (unicode_prop1(cr, UNICODE_PROP_Emoji_Keycap_Sequence) < 0)
            return -1;
        for(i = 0; i < cr->len; i += 2) {
            for(c = cr->points[i]; c < cr->points[i + 1]; c++) {
                seq[0] = c;
                seq[1] = 0xfe0f;
                seq[2] = 0x20e3;
                cb(opaque, seq, 3);
            }
        }
        break;
    case UNICODE_SEQUENCE_PROP_RGI_Emoji:
        /* all prevous sequences */
        for(i = UNICODE_SEQUENCE_PROP_Basic_Emoji; i <= UNICODE_SEQUENCE_PROP_RGI_Emoji_ZWJ_Sequence; i++) {
            int ret;
            ret = unicode_sequence_prop1(i, cb, opaque, cr);
            if (ret < 0)
                return ret;
            cr->len = 0;
        }
        break;
    default:
        return -2;
    }
    return 0;
}

/* build a unicode sequence property */
/* return -2 if not found, -1 if other error. 'cr' is used as temporary memory. */
int unicode_sequence_prop(const char *prop_name, UnicodeSequencePropCB *cb, void *opaque,
                          CharRange *cr)
{
    int seq_prop_idx;
    seq_prop_idx = unicode_find_name(unicode_sequence_prop_name_table, prop_name);
    if (seq_prop_idx < 0)
        return -2;
    return unicode_sequence_prop1(seq_prop_idx, cb, opaque, cr);
}
