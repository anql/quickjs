/*
 * Unicode 表生成工具
 * 
 * 本文件用于从 Unicode 官方数据文件生成压缩的 C 语言表，供 QuickJS 运行时使用。
 * 主要功能包括：
 * - 解析 UnicodeData.txt、SpecialCasing.txt、CaseFolding.txt 等官方数据
 * - 生成大小写转换表（case conversion tables）
 * - 生成 Unicode 属性表（general category, script, properties）
 * - 生成组合类表（combining class table）
 * - 生成字符分解/合成表（decomposition/composition tables）
 * - 生成 Emoji 序列表
 * 
 * 使用游程编码（RLE）和索引技术压缩数据，减少内存占用。
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <time.h>

#include "cutils.h"  // 通用工具函数（DynBuf, mallocz 等）

/* 统计表生成情况的全局计数器 */
uint32_t total_tables;        // 生成的表数量
uint32_t total_table_bytes;   // 表数据总字节数
uint32_t total_index;         // 索引表数量
uint32_t total_index_bytes;   // 索引表总字节数

/* ========== 编译选项配置 ========== */
/* define it to be able to test unicode.c */
//#define USE_TEST         // 启用测试代码（需要链接 libunicode.c）
/* profile tests */
//#define PROFILE          // 启用性能分析

//#define DUMP_CASE_CONV_TABLE   // 转储大小写转换表
//#define DUMP_TABLE_SIZE        // 打印表大小统计
//#define DUMP_CC_TABLE          // 转储组合类表
//#define DUMP_DECOMP_TABLE      // 转储分解表
//#define DUMP_CASE_FOLDING_SPECIAL_CASES  // 转储特殊大小写折叠情况

/* 优化思路（TODO）:
   - 对所有表通用化游程编码 + 索引
   - 移除 ID_start, ID_continue, Case_Ignorable, Cased 的冗余表
   
   大小写转换优化:
   - 对连续的 U/LF 运行使用单一条目
   - 允许 EXT 运行长度 > 1
   
   分解优化:
   - Greek lower case (+1f10/1f10) ?
   - 允许 B 运行中有空洞
   - 消除更多大写/小写冗余
*/

#ifdef USE_TEST
#include "libunicode.c"  // 测试时包含 libunicode.c 以进行验证
#endif

#define CHARCODE_MAX 0x10ffff  // Unicode 最大码点 (U+10FFFF)
#define CC_LEN_MAX 3           // 大小写转换结果最大长度（字符数）

/**
 * 分配并清零内存
 * @param size 请求的字节数
 * @return 已清零的内存指针
 */
void *mallocz(size_t size)
{
    void *ptr;
    ptr = malloc(size);
    memset(ptr, 0, size);
    return ptr;
}

/**
 * 获取第 n 个分号分隔的字段
 * Unicode 数据文件使用分号分隔字段，此函数跳过前 n 个字段
 * @param p 输入字符串指针
 * @param n 要跳过的字段数（从 0 开始）
 * @return 指向第 n 个字段的指针，失败返回 NULL
 */
const char *get_field(const char *p, int n)
{
    int i;
    for(i = 0; i < n; i++) {
        while (*p != ';' && *p != '\0')
            p++;
        if (*p == '\0')
            return NULL;
        p++;
    }
    return p;
}

/**
 * 获取第 n 个字段并复制到缓冲区
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param p 输入字符串指针
 * @param n 字段索引
 * @return 指向缓冲区的指针
 */
const char *get_field_buf(char *buf, size_t buf_size, const char *p, int n)
{
    char *q;
    p = get_field(p, n);
    q = buf;
    while (*p != ';' && *p != '\0') {
        if ((q - buf) < buf_size - 1)
            *q++ = *p;
        p++;
    }
    *q = '\0';
    return buf;
}

/**
 * 向动态数组添加字符（自动扩容）
 * @param pbuf 指向缓冲区指针的指针
 * @param psize 指向当前容量的指针
 * @param plen 指向当前长度的指针
 * @param c 要添加的字符
 */
void add_char(int **pbuf, int *psize, int *plen, int c)
{
    int len, size, *buf;
    buf = *pbuf;
    size = *psize;
    len = *plen;
    if (len >= size) {
        size = *psize;
        size = max_int(len + 1, size * 3 / 2);  // 扩容 1.5 倍
        buf = realloc(buf, sizeof(buf[0]) * size);
        *pbuf = buf;
        *psize = size;
    }
    buf[len++] = c;
    *plen = len;
}

/**
 * 解析字段中的十六进制字符串为整数数组
 * 用于解析 Unicode 码点序列（如 "0041 0042 0043"）
 * @param plen 输出长度
 * @param str 输入字符串
 * @param n 字段索引
 * @return 动态分配的整数数组
 */
int *get_field_str(int *plen, const char *str, int n)
{
    const char *p;
    int *buf, len, size;
    p = get_field(str, n);
    if (!p) {
        *plen = 0;
        return NULL;
    }
    len = 0;
    size = 0;
    buf = NULL;
    for(;;) {
        while (isspace(*p))
            p++;
        if (!isxdigit(*p))
            break;
        add_char(&buf, &size, &len, strtoul(p, (char **)&p, 16));
    }
    *plen = len;
    return buf;
}

/**
 * 从文件读取一行，去除末尾换行符
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param f 文件指针
 * @return buf 指针，EOF 返回 NULL
 */
char *get_line(char *buf, int buf_size, FILE *f)
{
    int len;
    if (!fgets(buf, buf_size, f))
        return NULL;
    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    return buf;
}

/* ========== 正则表达式字符串哈希表（用于 Emoji ZWJ 序列去重） ========== */

/**
 * 哈希表中的字符串节点
 * 用于存储唯一的字符串序列（如 Emoji ZWJ 序列）
 */
typedef struct REString {
    struct REString *next;   // 哈希冲突链表的下一个节点
    uint32_t hash;           // 预计算的哈希值
    uint32_t len;            // 字符串长度（字符数）
    uint32_t flags;          // 标志位（如是否已标记）
    uint32_t buf[];          // 柔性数组成员，存储实际的字符数据
} REString;

/**
 * 字符串哈希表
 */
typedef struct {
    uint32_t n_strings;      // 已存储的字符串数量
    uint32_t hash_size;      // 哈希表大小（2 的幂）
    int hash_bits;           // hash_size 的 log2
    REString **hash_table;   // 哈希桶数组
} REStringList;

/**
 * 计算字符串的哈希值
 * 使用乘法哈希算法
 * @param len 字符串长度
 * @param buf 字符串数据
 * @return 32 位哈希值
 */
static uint32_t re_string_hash(int len, const uint32_t *buf)
{
    int i;
    uint32_t h;
    h = 1;
    for(i = 0; i < len; i++)
        h = h * 263 + buf[i];  // 263 是质数
    return h * 0x61C88647;     // 乘以一个大的质数常数进行混合
}

/**
 * 初始化字符串哈希表
 * @param s 要初始化的哈希表
 */
static void re_string_list_init(REStringList *s)
{
    s->n_strings = 0;
    s->hash_size = 0;
    s->hash_bits = 0;
    s->hash_table = NULL;
}

/**
 * 释放字符串哈希表
 * @param s 要释放的哈希表
 */
static  __maybe_unused void re_string_list_free(REStringList *s)
{
    REString *p, *p_next;
    int i;
    for(i = 0; i < s->hash_size; i++) {
        for(p = s->hash_table[i]; p != NULL; p = p_next) {
            p_next = p->next;
            free(p);
        }
    }
    free(s->hash_table);
}

static void lre_print_char(int c, BOOL is_range)
{
    if (c == '\'' || c == '\\' ||
        (is_range && (c == '-' || c == ']'))) {
        printf("\\%c", c);
    } else if (c >= ' ' && c <= 126) {
        printf("%c", c);
    } else {
        printf("\\u{%04x}", c);
    }
}

static __maybe_unused void re_string_list_dump(const char *str, const REStringList *s)
{
    REString *p;
    int i, j, k;

    printf("%s:\n", str);
    
    j = 0;
    for(i = 0; i < s->hash_size; i++) {
        for(p = s->hash_table[i]; p != NULL; p = p->next) {
            printf("  %d/%d: '", j, s->n_strings);
            for(k = 0; k < p->len; k++) {
                lre_print_char(p->buf[k], FALSE);
            }
            printf("'\n");
            j++;
        }
    }
}

/**
 * 在哈希表中查找或添加字符串
 * @param s 哈希表
 * @param len 字符串长度
 * @param buf 字符串数据
 * @param h0 预计算的哈希值
 * @param add_flag 如果为 TRUE 且未找到则添加新条目
 * @return 找到的条目或新添加的条目，失败返回 NULL
 */
static REString *re_string_find2(REStringList *s, int len, const uint32_t *buf,
                                 uint32_t h0, BOOL add_flag)
{
    uint32_t h = 0; /* avoid warning */
    REString *p;
    if (s->n_strings != 0) {
        h = h0 >> (32 - s->hash_bits);  // 取哈希值的高位作为桶索引
        for(p = s->hash_table[h]; p != NULL; p = p->next) {
            if (p->hash == h0 && p->len == len &&
                !memcmp(p->buf, buf, len * sizeof(buf[0]))) {
                return p;  // 找到匹配项
            }
        }
    }
    /* not found */
    if (!add_flag)
        return NULL;
    /* increase the size of the hash table if needed */
    if (unlikely((s->n_strings + 1) > s->hash_size)) {
        // 哈希表扩容：当负载因子 > 1 时，扩大为原来的 2 倍
        REString **new_hash_table, *p_next;
        int new_hash_bits, i;
        uint32_t new_hash_size;
        new_hash_bits = max_int(s->hash_bits + 1, 4);  // 最小 4 位（16 桶）
        new_hash_size = 1 << new_hash_bits;
        new_hash_table = malloc(sizeof(new_hash_table[0]) * new_hash_size);
        if (!new_hash_table)
            return NULL;
        memset(new_hash_table, 0, sizeof(new_hash_table[0]) * new_hash_size);
        // 重新哈希所有现有条目
        for(i = 0; i < s->hash_size; i++) {
            for(p = s->hash_table[i]; p != NULL; p = p_next) {
                p_next = p->next;
                h = p->hash >> (32 - new_hash_bits);
                p->next = new_hash_table[h];
                new_hash_table[h] = p;
            }
        }
        free(s->hash_table);
        s->hash_bits = new_hash_bits;
        s->hash_size = new_hash_size;
        s->hash_table = new_hash_table;
        h = h0 >> (32 - s->hash_bits);
    }

    // 分配新节点
    p = malloc(sizeof(REString) + len * sizeof(buf[0]));
    if (!p)
        return NULL;
    p->next = s->hash_table[h];  // 插入到链表头部
    s->hash_table[h] = p;
    s->n_strings++;
    p->hash = h0;
    p->len = len;
    p->flags = 0;
    memcpy(p->buf, buf, sizeof(buf[0]) * len);
    return p;
}

/**
 * 查找或添加字符串（自动计算哈希）
 * @param s 哈希表
 * @param len 字符串长度
 * @param buf 字符串数据
 * @param add_flag 是否添加新条目
 * @return 找到的条目或新添加的条目
 */
static REString *re_string_find(REStringList *s, int len, const uint32_t *buf,
                                BOOL add_flag)
{
    uint32_t h0;
    h0 = re_string_hash(len, buf);
    return re_string_find2(s, len, buf, h0, add_flag);
}

/**
 * 向哈希表添加字符串
 * @param s 哈希表
 * @param len 字符串长度
 * @param buf 字符串数据
 */
static void re_string_add(REStringList *s, int len, const uint32_t *buf)
{
    re_string_find(s, len, buf, TRUE);
}

/* ========== Unicode 分类和属性定义（从 unicode_gen_def.h 导入） ========== */

/* 定义 Unicode 一般分类（General Category）枚举和名称表 */
#define UNICODE_GENERAL_CATEGORY

typedef enum {
#define DEF(id, str) GCAT_ ## id,  // 生成 GCAT_Lu, GCAT_Ll 等枚举值
#include "unicode_gen_def.h"
#undef DEF
    GCAT_COUNT,  // 分类总数
} UnicodeGCEnum1;

// Unicode 一般分类的全名表（如 "Lu", "Ll", "Nd" 等）
static const char *unicode_gc_name[] = {
#define DEF(id, str) #id,
#include "unicode_gen_def.h"
#undef DEF
};

// Unicode 一般分类的短名表
static const char *unicode_gc_short_name[] = {
#define DEF(id, str) str,
#include "unicode_gen_def.h"
#undef DEF
};

#undef UNICODE_GENERAL_CATEGORY

/* 定义 Unicode 脚本（Script）枚举和名称表 */
#define UNICODE_SCRIPT

typedef enum {
#define DEF(id, str) SCRIPT_ ## id,  // 生成 SCRIPT_Latin, SCRIPT_Han 等
#include "unicode_gen_def.h"
#undef DEF
    SCRIPT_COUNT,  // 脚本总数
} UnicodeScriptEnum1;

// Unicode 脚本全名表
static const char *unicode_script_name[] = {
#define DEF(id, str) #id,
#include "unicode_gen_def.h"
#undef DEF
};

// Unicode 脚本短名表（如 "Latn", "Hani" 等）
const char *unicode_script_short_name[] = {
#define DEF(id, str) str,
#include "unicode_gen_def.h"
#undef DEF
};

#undef UNICODE_SCRIPT

/* 定义 Unicode 属性列表（Property List）枚举和名称表 */
#define UNICODE_PROP_LIST

typedef enum {
#define DEF(id, str) PROP_ ## id,  // 生成 PROP_White_Space, PROP_Dash 等
#include "unicode_gen_def.h"
#undef DEF
    PROP_COUNT,  // 属性总数
} UnicodePropEnum1;

// Unicode 属性全名表
static const char *unicode_prop_name[] = {
#define DEF(id, str) #id,
#include "unicode_gen_def.h"
#undef DEF
};

// Unicode 属性短名表
static const char *unicode_prop_short_name[] = {
#define DEF(id, str) str,
#include "unicode_gen_def.h"
#undef DEF
};

#undef UNICODE_PROP_LIST

/* 定义 Unicode 序列属性（Sequence Properties）枚举和名称表 */
#define UNICODE_SEQUENCE_PROP_LIST

typedef enum {
#define DEF(id) SEQUENCE_PROP_ ## id,  // 生成序列属性如 Basic_Emoji, RGI_Emoji_ZWJ_Sequence 等
#include "unicode_gen_def.h"
#undef DEF
    SEQUENCE_PROP_COUNT,
} UnicodeSequencePropEnum1;

/* ========== Unicode 序列属性（Sequence Properties） ========== */

/**
 * Unicode 序列属性名称表
 * 用于 Emoji 序列等复杂字符的属性定义
 */
static const char *unicode_sequence_prop_name[] = {
#define DEF(id) #id,
#include "unicode_gen_def.h"
#undef DEF
};

#undef UNICODE_SEQUENCE_PROP_LIST

/* ========== 核心数据结构 ========== */

/**
 * Unicode 字符信息结构体（CCInfo - Case Conversion Info）
 * 
 * 这是本工具的核心数据结构，存储每个 Unicode 码点（0x000000-0x10FFFF）
 * 的所有属性信息。总共约 1,114,112 个字符，每个字符一个 CCInfo 结构。
 * 
 * 内存占用估算：
 * - 固定部分：约 40 字节/字符
 * - 动态部分（script_ext, decomp_data）：平均约 10 字节/字符
 * - 总计：约 50MB（全部加载到内存）
 */
typedef struct {
    /* ========== 大小写转换信息（Case Conversion） ========== */
    /* 
     * Unicode 大小写转换可能是多对多的映射：
     * - 简单情况：A (U+0041) → a (U+0061)，1 对 1
     * - 复杂情况：ß (U+00DF) → SS，1 对 2
     * - 特殊语言规则：土耳其语 i/I 处理
     */
    uint8_t u_len;                  // 大写转换（Upper）结果长度（0-3）
    uint8_t l_len;                  // 小写转换（Lower）结果长度（0-3）
    uint8_t f_len;                  // 大小写折叠（Fold）结果长度（0-3）
    int u_data[CC_LEN_MAX];         // 大写转换结果数组（最多 3 个码点）
    int l_data[CC_LEN_MAX];         // 小写转换结果数组
    int f_data[CC_LEN_MAX];         // 大小写折叠结果（用于正则表达式匹配）

    /* ========== Unicode 基本属性 ========== */
    uint8_t combining_class;        // 组合类（Canonical Combining Class, 0-255）
                                    // 0=不组合，>0=组合优先级（用于重音符号排序）
    uint8_t is_compat:1;            // 是否为兼容分解（compatibility decomposition）
                                    // 如：① → 1（语义相同但视觉不同）
    uint8_t is_excluded:1;          // 是否被排除在标准合成之外
                                    // 这些字符不能通过分解表反向合成
    uint8_t general_category;       // 一般分类索引（General Category）
                                    // 如：Lu=大写字母，Ll=小写字母，Nd=十进制数字
    uint8_t script;                 // 脚本索引（Script）
                                    // 如：Latin, Cyrillic, Han, Hiragana 等
    uint8_t script_ext_len;         // 脚本扩展列表长度（多脚本字符）
    uint8_t *script_ext;            // 脚本扩展数组（动态分配）
                                    // 如：㋡ 同时属于 CJK 和 Japanese
    uint32_t prop_bitmap_tab[3];    // 布尔属性位图（32 位 × 3 = 96 个属性）
                                    // 每位代表一个布尔属性（如 White_Space, Dash 等）
    
    /* ========== 字符分解信息（Decomposition） ========== */
    /*
     * Unicode 规范化（NFC/NFD/NFKC/NFKD）需要分解表：
     * - 标准分解：é (U+00E9) → e (U+0065) + ́ (U+0301)
     * - 兼容分解：① → 1, ½ → 1/2
     */
    int decomp_len;                 // 分解结果长度（0-18 个字符）
    int *decomp_data;               // 分解结果数组（动态分配）
} CCInfo;

/**
 * Unicode 序列属性临时结构（保留但未使用）
 * 可能用于未来的序列属性扩展
 */
typedef struct {
    int count;      // 已存储的序列数量
    int size;       // 数组容量
    int *tab;       // 序列数据数组
} UnicodeSequenceProperties;

/* ========== 全局数据（进程级单例） ========== */
/*
 * 这些全局变量在 main() 中初始化，在整个程序生命周期中使用：
 * 1. unicode_db: 主数据库，1,114,112 个 CCInfo 结构（约 50MB）
 * 2. rgi_emoji_zwj_sequence: Emoji ZWJ 序列去重哈希表
 * 3. rgi_emoji_tag_sequence: Emoji Tag 序列缓冲区
 */
CCInfo *unicode_db;                    // Unicode 字符数据库（动态数组，索引=码点）
REStringList rgi_emoji_zwj_sequence;   // RGI Emoji ZWJ（零宽连接符）序列哈希表
DynBuf rgi_emoji_tag_sequence;         // RGI Emoji Tag 序列动态缓冲区

/* ========== 名称查找与属性位图操作 ========== */

/**
 * 在 Unicode 名称表中查找名称对应的索引
 * 
 * Unicode 官方数据文件中，每个分类/脚本/属性可能有多个名称：
 * - 正式名称：如 "Uppercase_Letter"
 * - 缩写名称：如 "Lu"
 * - 别名：如 "Letter, Uppercase"
 * 
 * 这些名称用逗号分隔存储在一个表项中，本函数支持查找任一名称。
 * 
 * @param tab 名称表（每个元素是逗号分隔的名称字符串）
 * @param tab_len 表长度（元素个数）
 * @param name 要查找的名称（可以是正式名、缩写或别名）
 * @return 匹配的索引（0 到 tab_len-1），未找到返回 -1
 * 
 * 示例：
 *   tab[0] = "Uppercase_Letter,Lu"
 *   find_name(tab, count, "Lu") → 返回 0
 *   find_name(tab, count, "Uppercase_Letter") → 返回 0
 */
int find_name(const char **tab, int tab_len, const char *name)
{
    int i, len, name_len;
    const char *p, *r;

    name_len = strlen(name);
    for(i = 0; i < tab_len; i++) {
        p = tab[i];
        for(;;) {
            // 找到下一个逗号或字符串结尾
            r = strchr(p, ',');
            if (!r)
                len = strlen(p);
            else
                len = r - p;
            // 比较名称（精确匹配，不区分大小写由调用者保证）
            if (len == name_len && memcmp(p, name, len) == 0)
                return i;
            if (!r)
                break;
            p = r + 1;  // 跳过逗号，继续检查下一个别名
        }
    }
    return -1;  // 未找到
}

/* ========== 布尔属性位图操作 ========== */
/*
 * Unicode 有 96 个布尔属性（如 White_Space, Dash, Hex_Digit 等）。
 * 为节省内存，使用位图压缩存储：3 个 uint32_t = 96 位。
 * 
 * 位图布局：
 *   prop_bitmap_tab[0]: 属性 0-31
 *   prop_bitmap_tab[1]: 属性 32-63
 *   prop_bitmap_tab[2]: 属性 64-95
 * 
 * 访问公式：
 *   字索引 = prop_idx >> 5  (除以 32)
 *   位索引 = prop_idx & 0x1f (模 32)
 */

/**
 * 获取字符的布尔属性值
 * @param c Unicode 码点（0 到 0x10FFFF）
 * @param prop_idx 属性索引（0 到 95）
 * @return 属性值（0=false 或 1=true）
 */
static BOOL get_prop(uint32_t c, int prop_idx)
{
    // 从位图中提取指定位
    return (unicode_db[c].prop_bitmap_tab[prop_idx >> 5] >> (prop_idx & 0x1f)) & 1;
}

/**
 * 设置字符的布尔属性值
 * @param c Unicode 码点（0 到 0x10FFFF）
 * @param prop_idx 属性索引（0 到 95）
 * @param val 属性值（0=false 或 1=true）
 */
static void set_prop(uint32_t c, int prop_idx, int val)
{
    uint32_t mask;
    mask = 1U << (prop_idx & 0x1f);  // 创建位掩码（如第 5 位：0x20）
    if (val)
        unicode_db[c].prop_bitmap_tab[prop_idx >> 5] |= mask;   // 设置位（OR 操作）
    else
        unicode_db[c].prop_bitmap_tab[prop_idx >> 5]  &= ~mask; // 清除位（AND NOT 操作）
}

/* ========== Unicode 官方数据文件解析函数 ========== */
/*
 * Unicode 联盟发布的数据文件位于：
 * https://www.unicode.org/Public/UCD/latest/ucd/
 * 
 * 本工具解析以下核心文件：
 * 1. UnicodeData.txt - 基础字符数据（所有码点的核心属性）
 * 2. SpecialCasing.txt - 特殊大小写规则（上下文相关）
 * 3. CaseFolding.txt - 大小写折叠（用于正则匹配）
 * 4. CompositionExclusions.txt - 合成排除列表
 * 5. DerivedCoreProperties.txt - 派生核心属性
 * 6. DerivedNormalizationProps.txt - 派生规范化属性
 * 7. PropList.txt - 属性列表
 * 8. Scripts.txt - 脚本分配
 * 9. ScriptExtensions.txt - 脚本扩展
 * 10. emoji-data.txt - Emoji 属性
 * 11. emoji-sequences.txt - Emoji 序列
 * 12. emoji-zwj-sequences.txt - Emoji ZWJ 序列
 */

/**
 * 解析 UnicodeData.txt 文件
 * 
 * 这是 Unicode 字符数据库的核心文件，包含所有码点的基础信息。
 * 
 * 文件格式（分号分隔的字段）：
 *   字段 0: 码点（十六进制，如 "0041"）
 *   字段 1: 字符名称（如 "LATIN CAPITAL LETTER A"）
 *   字段 2: 一般分类（如 "Lu"=大写字母）
 *   字段 3: 组合类（0-255，0 表示不组合）
 *   字段 4: 双向类别（Bidi_Class）
 *   字段 5: 分解映射（如 "<compat> 0065 0301"）
 *   字段 6-8: 十进制数字映射（仅数字字符）
 *   字段 9: 双向镜像标记（Y/N）
 *   字段 10-11: Unicode 1.0 名称（已废弃）
 *   字段 12: 大写映射（如 "0041"）
 *   字段 13: 小写映射（如 "0061"）
 *   字段 14: 简单大小写折叠映射
 * 
 * 范围表示法：
 *   某些字符（如控制字符）以范围形式定义：
 *   - 起始：0000;<control>;Cc;0;BN;;;;;N;NULL;;;;
 *   - 结束：001F;<control, Last>;Cc;0;BN;;;;;N;APPLY FUNCTIONAL CHARACTER;;;;
 *   遇到 "Last>" 标记时，将起始字符的属性复制到整个范围。
 * 
 * @param filename UnicodeData.txt 文件路径
 */
void parse_unicode_data(const char *filename)
{
    FILE *f;
    char line[1024];      // 行缓冲区（Unicode 行通常 < 200 字符）
    char buf1[256];       // 字段缓冲区
    const char *p;
    int code, lc, uc, last_code;  // 当前码点、小写、大写、上一个码点
    CCInfo *ci, *tab = unicode_db;  // 当前字符信息、数据库指针

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    last_code = 0;  // 用于范围复制的起始码点
    for(;;) {
        // 读取一行
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#')  // 跳过注释行
            continue;

        // ========== 字段 0: 码点 ==========
        p = get_field(line, 0);
        if (!p)
            continue;
        code = strtoul(p, NULL, 16);  // 十六进制解析
        lc = 0;
        uc = 0;

        // ========== 字段 12: 大写映射 ==========
        p = get_field(line, 12);
        if (p && *p != ';') {
            uc = strtoul(p, NULL, 16);
        }

        // ========== 字段 13: 小写映射 ==========
        p = get_field(line, 13);
        if (p && *p != ';') {
            lc = strtoul(p, NULL, 16);
        }
        
        // 存储大小写映射
        ci = &tab[code];
        if (uc > 0 || lc > 0) {
            assert(code <= CHARCODE_MAX);
            if (uc > 0) {
                assert(ci->u_len == 0);  // 确保未设置
                ci->u_len = 1;
                ci->u_data[0] = uc;
            }
            if (lc > 0) {
                assert(ci->l_len == 0);
                ci->l_len = 1;
                ci->l_data[0] = lc;
            }
        }

        // ========== 字段 2: 一般分类（General Category） ==========
        {
            int i;
            get_field_buf(buf1, sizeof(buf1), line, 2);  // 提取字段到缓冲区
            i = find_name(unicode_gc_name, countof(unicode_gc_name), buf1);
            if (i < 0) {
                fprintf(stderr, "General category '%s' not found\n", buf1);
                exit(1);
            }
            ci->general_category = i;  // 存储索引
        }

        // ========== 字段 3: 组合类（Combining Class） ==========
        p = get_field(line, 3);
        if (p && *p != ';' && *p != '\0') {
            int cc;
            cc = strtoul(p, NULL, 0);
            if (cc != 0) {
                assert(code <= CHARCODE_MAX);
                ci->combining_class = cc;
                // 调试输出：printf("%05x: %d\n", code, ci->combining_class);
            }
        }

        // ========== 字段 5: 分解映射（Decomposition Mapping） ==========
        p = get_field(line, 5);
        if (p && *p != ';' && *p != '\0') {
            int size;
            assert(code <= CHARCODE_MAX);
            ci->is_compat = 0;
            
            // 检查是否为兼容分解（以 < 开头，如 <compat>, <font>, <noBreak>）
            if (*p == '<') {
                while (*p != '\0' && *p != '>')
                    p++;  // 跳过标签
                if (*p == '>')
                    p++;  // 跳过 >
                ci->is_compat = 1;  // 标记为兼容分解
            }
            
            // 解析分解后的码点序列（空格分隔的十六进制数）
            size = 0;
            for(;;) {
                while (isspace(*p))
                    p++;
                if (!isxdigit(*p))
                    break;
                add_char(&ci->decomp_data, &size, &ci->decomp_len, 
                         strtoul(p, (char **)&p, 16));
            }
            
            // 调试输出（已禁用）
#if 0
            {
                int i;
                static int count, d_count;
                printf("%05x: %c", code, ci->is_compat ? 'C': ' ');
                for(i = 0; i < ci->decomp_len; i++)
                    printf(" %05x", ci->decomp_data[i]);
                printf("\n");
                count++;
                d_count += ci->decomp_len;
            }
#endif
        }

        // ========== 字段 9: 双向镜像（Bidi Mirrored） ==========
        // 如括号字符在 RTL 文本中需要镜像显示
        p = get_field(line, 9);
        if (p && *p == 'Y') {
            set_prop(code, PROP_Bidi_Mirrored, 1);
        }

        // ========== 范围处理 ==========
        /*
         * UnicodeData.txt 使用范围表示法压缩重复数据：
         * 起始行：0000;<control>;Cc;0;BN;;;;;N;NULL;;;;
         * 结束行：001F;<control, Last>;Cc;0;BN;;;;;N;APPLY FUNCTIONAL CHARACTER;;;;
         * 
         * 当遇到名称包含 "Last>" 的行时，表示一个范围结束。
         * 将起始字符（last_code）的属性复制到范围内所有码点。
         */
        get_field_buf(buf1, sizeof(buf1), line, 1);  // 字段 1: 名称
        if (strstr(buf1, " Last>")) {
            int i;
            // 调试输出：printf("range: 0x%x-%0x\n", last_code, code);
            assert(ci->decomp_len == 0);       // 范围字符不应有分解
            assert(ci->script_ext_len == 0);   // 范围字符不应有脚本扩展
            
            // 将当前属性复制到范围内的所有码点
            for(i = last_code + 1; i < code; i++) {
                unicode_db[i] = *ci;  // 结构体拷贝
            }
        }
        last_code = code;  // 更新范围起始点
    }

    fclose(f);
}

/**
 * 解析 SpecialCasing.txt 文件
 * 
 * SpecialCasing.txt 包含 UnicodeData.txt 中无法表示的特殊大小写规则：
 * 1. 多字符映射：如 ß (U+00DF) → SS（小写转大写）
 * 2. 上下文相关规则：如 σ (U+03C3) 在词尾变为 ς (U+03C2)
 * 3. 语言特定规则：如土耳其语 i/I 处理
 * 
 * 文件格式（分号分隔）：
 *   字段 0: 码点
 *   字段 1: 小写映射
 *   字段 2: 标题大小写映射
 *   字段 3: 大写映射
 *   字段 4: 条件（语言/上下文，可选）
 * 
 * 示例：
 *   00DF; 00DF; 0053 0053; 0053 0053; # LATIN SMALL LETTER SHARP S
 *   ß 的小写是自身，大写是 "SS"
 * 
 * @param tab Unicode 字符数据库
 * @param filename SpecialCasing.txt 文件路径
 */
void parse_special_casing(CCInfo *tab, const char *filename)
{
    FILE *f;
    char line[1024];
    const char *p;
    int code;
    CCInfo *ci;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#')  // 跳过注释
            continue;

        // 字段 0: 码点
        p = get_field(line, 0);
        if (!p)
            continue;
        code = strtoul(p, NULL, 16);
        assert(code <= CHARCODE_MAX);
        ci = &tab[code];

        // 字段 4: 条件（语言/上下文）
        // 如果有条件（非空且不是注释），跳过该行
        // 我们只处理无条件的一般规则
        p = get_field(line, 4);
        if (p) {
            /* locale dependent casing - 跳过语言特定规则 */
            while (isspace(*p))
                p++;
            if (*p != '#' && *p != '\0')
                continue;  // 有条件，跳过
        }

        // 字段 1: 小写映射
        p = get_field(line, 1);
        if (p && *p != ';') {
            ci->l_len = 0;  // 重置
            for(;;) {
                while (isspace(*p))
                    p++;
                if (*p == ';')
                    break;
                assert(ci->l_len < CC_LEN_MAX);
                ci->l_data[ci->l_len++] = strtoul(p, (char **)&p, 16);
            }
            // 如果映射到自身，视为无映射
            if (ci->l_len == 1 && ci->l_data[0] == code)
                ci->l_len = 0;
        }

        // 字段 3: 大写映射
        p = get_field(line, 3);
        if (p && *p != ';') {
            ci->u_len = 0;  // 重置
            for(;;) {
                while (isspace(*p))
                    p++;
                if (*p == ';')
                    break;
                assert(ci->u_len < CC_LEN_MAX);
                ci->u_data[ci->u_len++] = strtoul(p, (char **)&p, 16);
            }
            // 如果映射到自身，视为无映射
            if (ci->u_len == 1 && ci->u_data[0] == code)
                ci->u_len = 0;
        }
    }

    fclose(f);
}

/**
 * 解析 CaseFolding.txt 文件
 * 
 * CaseFolding.txt 定义大小写折叠（Case Folding）规则，用于正则表达式
 * 的不区分大小写匹配（/i 标志）。
 * 
 * 与大小写转换的区别：
 * - 大小写转换（toLower/toUpper）：用于文本显示和编辑
 * - 大小写折叠（Case Folding）：用于字符串比较和匹配，追求最大兼容性
 * 
 * 示例：
 * - 德语 ß (U+00DF) → ss（折叠后用于匹配）
 * - 希腊语 Σ (U+03A3) → σ（折叠后统一为小写 sigma）
 * 
 * 文件格式（分号分隔）：
 *   字段 0: 码点
 *   字段 1: 状态（C=Common, S=Simple, F=Full, T=Special）
 *   字段 2: 折叠结果（空格分隔的码点序列）
 * 
 * 状态说明：
 * - C (Common): 通用规则，总是应用
 * - S (Simple): 简单映射（单字符结果）
 * - F (Full): 完整映射（可能多字符）
 * - T (Special): 特殊规则（如土耳其语 i/I），本工具忽略
 * 
 * 处理策略：
 * - 优先使用 F（完整映射）
 * - 如果有 S（简单映射），覆盖 F（因为 S 在 F 之后）
 * - 忽略 T（语言特定规则）
 * 
 * @param tab Unicode 字符数据库
 * @param filename CaseFolding.txt 文件路径
 */
void parse_case_folding(CCInfo *tab, const char *filename)
{
    FILE *f;
    char line[1024];
    const char *p;
    int code, status;  // 码点、状态类型
    CCInfo *ci;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#')
            continue;

        // 字段 0: 码点
        p = get_field(line, 0);
        if (!p)
            continue;
        code = strtoul(p, NULL, 16);
        assert(code <= CHARCODE_MAX);
        ci = &tab[code];

        // 字段 1: 状态
        p = get_field(line, 1);
        if (!p)
            continue;
        while (isspace(*p))
            p++;
        status = *p;
        // 只处理 C/S/F，忽略 T（语言特定规则）
        if (status != 'C' && status != 'S' && status != 'F')
            continue;

        // 字段 2: 折叠结果
        p = get_field(line, 2);
        assert(p != NULL);
        
        if (status == 'S') {
            /*
             * 简单映射（S）总是出现在完整映射（F）之后。
             * 我们优先使用简单映射（单字符结果，更高效）。
             * 先断言已有 F 映射（至少 2 个字符，否则不会被 F 填充），
             * 然后清空并替换为 S 映射。
             */
            assert(ci->f_len >= 2);
            ci->f_len = 0;
        } else {
            assert(ci->f_len == 0);  // F 或 C 应该是首次设置
        }
        
        // 解析折叠结果（空格分隔的码点序列）
        for(;;) {
            while (isspace(*p))
                p++;
            if (*p == ';')
                break;
            assert(ci->l_len < CC_LEN_MAX);  // 安全检查
            ci->f_data[ci->f_len++] = strtoul(p, (char **)&p, 16);
        }
    }

    fclose(f);
}

/**
 * 解析 CompositionExclusions.txt 文件
 * 
 * 该文件列出不能被标准合成（Canonical Composition）的字符。
 * 
 * Unicode 规范化（NFC）会将分解形式合成为预组合字符：
 *   e (U+0065) + ́ (U+0301) → é (U+00E9)
 * 
 * 但某些字符被排除在外，原因包括：
 * 1. 兼容性考虑（已有预组合形式但语义不同）
 * 2. 上下文相关合成（如希腊语带重音符号）
 * 3. 视觉区分（如某些数学符号）
 * 
 * 文件格式：每行一个码点（十六进制），注释行以 # 开头
 * 
 * @param filename CompositionExclusions.txt 文件路径
 */
void parse_composition_exclusions(const char *filename)
{
    FILE *f;
    char line[4096], *p;
    uint32_t c0;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        // 跳过注释行和空行
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        c0 = strtoul(p, (char **)&p, 16);
        assert(c0 > 0 && c0 <= CHARCODE_MAX);
        // 标记为排除合成
        unicode_db[c0].is_excluded = TRUE;
    }
    fclose(f);
}

/**
 * 解析 DerivedCoreProperties.txt 文件
 * 
 * 该文件定义派生的核心 Unicode 属性，这些属性不能直接从
 * UnicodeData.txt 获得，需要通过规则计算得出。
 * 
 * 常见属性：
 * - Uppercase, Lowercase, Cased
 * - White_Space, Control, Hex_Digit
 * - ID_Start, ID_Continue（标识符字符）
 * - Math, Alphabetic
 * 
 * 文件格式：
 *   码点范围 ; 属性名 # 注释
 *   如：0041..005A    ; Uppercase # Lu
 * 
 * 支持范围表示法（..）和单个码点
 * 
 * @param filename DerivedCoreProperties.txt 文件路径
 */
void parse_derived_core_properties(const char *filename)
{
    FILE *f;
    char line[4096], *p, buf[256], *q;
    uint32_t c0, c1, c;
    int i;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        // 跳过注释和空行
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        
        // 解析起始码点
        c0 = strtoul(p, (char **)&p, 16);
        
        // 检查范围表示法（..）
        if (*p == '.' && p[1] == '.') {
            p += 2;
            c1 = strtoul(p, (char **)&p, 16);
        } else {
            c1 = c0;  // 单个码点
        }
        assert(c1 <= CHARCODE_MAX);
        
        // 跳过分隔符，提取属性名
        p += strspn(p, " \t");
        if (*p == ';') {
            p++;
            p += strspn(p, " \t");
            q = buf;
            while (*p != '\0' && *p != ' ' && *p != '#' && *p != '\t' && *p != ';') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            
            // 查找属性索引
            i = find_name(unicode_prop_name, countof(unicode_prop_name), buf);
            if (i < 0) {
                // Grapheme_Link 不在标准属性列表中，跳过
                if (!strcmp(buf, "Grapheme_Link"))
                    goto next;
                fprintf(stderr, "Property not found: %s\n", buf);
                exit(1);
            }
            
            // 为范围内所有码点设置属性
            for(c = c0; c <= c1; c++) {
                set_prop(c, i, 1);
            }
next: ;
        }
    }
    fclose(f);
}

/**
 * 解析 DerivedNormalizationProps.txt 文件
 * 
 * 该文件定义与 Unicode 规范化（Normalization）相关的派生属性。
 * 
 * Unicode 规范化形式：
 * - NFD (Canonical Decomposition): 标准分解
 * - NFC (Canonical Decomposition + Composition): 标准分解后合成
 * - NFKD (Compatibility Decomposition): 兼容分解
 * - NFKC (Compatibility Decomposition + Composition): 兼容分解后合成
 * 
 * 本函数特别关注 Changes_When_NFKC_Casefolded 属性，
 * 用于标识在 NFKC 规范化 + 大小写折叠时会发生变化的字符。
 * 
 * @param filename DerivedNormalizationProps.txt 文件路径
 */
void parse_derived_norm_properties(const char *filename)
{
    FILE *f;
    char line[4096], *p, buf[256], *q;
    uint32_t c0, c1, c;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        c0 = strtoul(p, (char **)&p, 16);
        if (*p == '.' && p[1] == '.') {
            p += 2;
            c1 = strtoul(p, (char **)&p, 16);
        } else {
            c1 = c0;
        }
        assert(c1 <= CHARCODE_MAX);
        p += strspn(p, " \t");
        if (*p == ';') {
            p++;
            p += strspn(p, " \t");
            q = buf;
            while (*p != '\0' && *p != ' ' && *p != '#' && *p != '\t') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            // 只处理 Changes_When_NFKC_Casefolded 属性
            if (!strcmp(buf, "Changes_When_NFKC_Casefolded")) {
                for(c = c0; c <= c1; c++) {
                    set_prop(c, PROP_Changes_When_NFKC_Casefolded, 1);
                }
            }
        }
    }
    fclose(f);
}

/**
 * 解析 PropList.txt 文件
 * 
 * PropList.txt 定义简单的布尔属性列表。
 * 与 DerivedCoreProperties.txt 类似，但包含更基础的属性。
 * 
 * 常见属性：
 * - ASCII_Hex_Digit (0-9, A-F, a-f)
 * - Bidi_Control (双向控制字符)
 * - Dash (破折号类标点)
 * - Hyphen (连字符)
 * - Quotation_Mark (引号)
 * - Terminal_Punctuation (句末标点)
 * - Other_Math, Other_Alphabetic
 * 
 * @param filename PropList.txt 文件路径
 */
void parse_prop_list(const char *filename)
{
    FILE *f;
    char line[4096], *p, buf[256], *q;
    uint32_t c0, c1, c;
    int i;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        c0 = strtoul(p, (char **)&p, 16);
        if (*p == '.' && p[1] == '.') {
            p += 2;
            c1 = strtoul(p, (char **)&p, 16);
        } else {
            c1 = c0;
        }
        assert(c1 <= CHARCODE_MAX);
        p += strspn(p, " \t");
        if (*p == ';') {
            p++;
            p += strspn(p, " \t");
            q = buf;
            while (*p != '\0' && *p != ' ' && *p != '#' && *p != '\t') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            i = find_name(unicode_prop_name,
                          countof(unicode_prop_name), buf);
            if (i < 0) {
                fprintf(stderr, "Property not found: %s\n", buf);
                exit(1);
            }
            // 为范围内所有码点设置属性
            for(c = c0; c <= c1; c++) {
                set_prop(c, i, 1);
            }
        }
    }
    fclose(f);
}

/* ========== Emoji 序列处理 ========== */

#define SEQ_MAX_LEN 16  // 序列最大长度（用于栈分配）

/**
 * 判断是否为 Emoji 修饰符（皮肤色调）
 * 
 * Emoji 修饰符范围：U+1F3FB 到 U+1F3FF
 * - U+1F3FB: 🏻 Emoji Modifier Fitzpatrick Type-1-2（浅色）
 * - U+1F3FC: 🏼 Type-3（中浅色）
 * - U+1F3FD: 🏽 Type-4（中等色）
 * - U+1F3FE: 🏾 Type-5（中深色）
 * - U+1F3FF: 🏿 Type-6（深色）
 * 
 * 这些修饰符用于改变人物 Emoji 的肤色，如：
 *   👍 (U+1F44D) + 🏻 (U+1F3FB) = 👍🏻（浅色大拇指）
 * 
 * @param c Unicode 码点
 * @return TRUE 如果是 Emoji 修饰符
 */
static BOOL is_emoji_modifier(uint32_t c)
{
    return (c >= 0x1f3fb && c <= 0x1f3ff);
}

/**
 * 添加序列属性
 * 
 * 处理不同类型的 Emoji 序列，将其转换为适当的内部表示：
 * 1. Basic_Emoji: 基础 Emoji（单字符或带变体选择器）
 * 2. Modifier_Sequence: 带皮肤色调的 Emoji
 * 3. Flag_Sequence: 国旗序列（地区指示符号对）
 * 4. ZWJ_Sequence: ZWJ 连接的复合 Emoji（如 👨‍👩‍👧‍👦）
 * 5. Tag_Sequence: Tag 序列（如 🏴󠁧󠁢󠁥󠁮󠁧󠁿 英格兰旗帜）
 * 6. Keycap_Sequence: 按键帽序列（如 1️⃣）
 * 
 * @param idx 序列属性索引
 * @param seq_len 序列长度
 * @param seq 序列数据（码点数组）
 */
static void add_sequence_prop(int idx, int seq_len, int *seq)
{
    int i;
    
    assert(idx < SEQUENCE_PROP_COUNT);
    switch(idx) {
    case SEQUENCE_PROP_Basic_Emoji:
        /*
         * 基础 Emoji 分为两种：
         * 1. 单字符：如 😀 (U+1F600)
         * 2. 带变体选择器：如 ❤️ (U+2764 + U+FE0F)
         * 转换为两个内部属性：PROP_Basic_Emoji1 和 PROP_Basic_Emoji2
         */
        if (seq_len == 1) {
            set_prop(seq[0], PROP_Basic_Emoji1, 1);
        } else if (seq_len == 2 && seq[1] == 0xfe0f) {
            set_prop(seq[0], PROP_Basic_Emoji2, 1);
        } else {
            abort();
        }
        break;
        
    case SEQUENCE_PROP_RGI_Emoji_Modifier_Sequence:
        /*
         * RGI (Recommended for General Interchange) Emoji 修饰符序列
         * 格式：基础字符 + 修饰符（皮肤色调）
         * 如：👍🏻 = 👍 (U+1F44D) + 🏻 (U+1F3FB)
         */
        assert(seq_len == 2);
        assert(is_emoji_modifier(seq[1]));
        assert(get_prop(seq[0], PROP_Emoji_Modifier_Base));
        set_prop(seq[0], PROP_RGI_Emoji_Modifier_Sequence, 1);
        break;
        
    case SEQUENCE_PROP_RGI_Emoji_Flag_Sequence:
        /*
         * RGI Emoji 国旗序列
         * 由两个地区指示符号（Regional Indicator）组成
         * 范围：U+1F1E6 (🇦) 到 U+1F1FF (🇿)
         * 
         * 编码方式：将两个字母编码为 0-675 的索引
         * 如：🇨🇳 = 🇨 (U+1F1E8) + 🇳 (U+1F1F3)
         *     code = (2*26) + 13 = 65
         */
        {
            int code;
            assert(seq_len == 2);
            assert(seq[0] >= 0x1F1E6 && seq[0] <= 0x1F1FF);
            assert(seq[1] >= 0x1F1E6 && seq[1] <= 0x1F1FF);
            // 将两个字母编码为 0-675 的索引（26×26=676 种组合）
            code = (seq[0] - 0x1F1E6) * 26 + (seq[1] - 0x1F1E6);
            /* XXX: 可以用位图压缩到 676 位（85 字节） */
            set_prop(code, PROP_RGI_Emoji_Flag_Sequence, 1);
        }
        break;
        
    case SEQUENCE_PROP_RGI_Emoji_ZWJ_Sequence:
        /*
         * RGI Emoji ZWJ（零宽连接符）序列
         * 如：👨‍👩‍👧‍👦 (家庭)、👨‍⚕️ (男医生)
         * 这些序列存储在全局哈希表中用于去重
         */
        re_string_add(&rgi_emoji_zwj_sequence, seq_len, (uint32_t *)seq);
        break;
        
    case SEQUENCE_PROP_RGI_Emoji_Tag_Sequence:
        /*
         * RGI Emoji Tag 序列
         * 用于表示子地区旗帜（如英格兰、苏格兰）
         * 格式：🏴 (U+1F3F4) + Tag 序列 + 取消标签 (U+E007F)
         * 
         * Tag 字符范围：U+E0001 到 U+E007E（小写字母 a-z）
         * 如：🏴󠁧󠁢󠁥󠁮󠁧󠁿 = 🏴 + g + b + e + n + g + 取消
         * 
         * 存储方式：去掉基值 0xE0000，压缩为 ASCII 存储
         */
        {
            assert(seq_len >= 3);
            assert(seq[0] == 0x1F3F4);  // 旗帜基符
            assert(seq[seq_len - 1] == 0xE007F);  // 取消标签
            for(i = 1; i < seq_len - 1; i++) {
                assert(seq[i] >= 0xe0001 && seq[i] <= 0xe007e);
                // 去掉基值，存储为 1-126 的 ASCII
                dbuf_putc(&rgi_emoji_tag_sequence, seq[i] - 0xe0000);
            }
            dbuf_putc(&rgi_emoji_tag_sequence, 0);  // 字符串结束符
        }
        break;
        
    case SEQUENCE_PROP_Emoji_Keycap_Sequence:
        /*
         * Emoji 按键帽序列
         * 格式：数字/字母 + U+FE0F (变体选择器) + U+20E3 (组合封闭键帽)
         * 如：1️⃣ = 1 + ️ (U+FE0F) + ⃣ (U+20E3)
         */
        assert(seq_len == 3);
        assert(seq[1] == 0xfe0f);  // 变体选择器
        assert(seq[2] == 0x20e3);  // 组合封闭键帽
        set_prop(seq[0], PROP_Emoji_Keycap_Sequence, 1);
        break;
        
    default:
        assert(0);  // 未知属性类型
    }
}

/**
 * 解析 emoji-sequences.txt 和 emoji-zwj-sequences.txt 文件
 * 
 * 这两个文件定义 Emoji 序列的正式规范：
 * 
 * emoji-sequences.txt:
 * - Basic_Emoji: 基础 Emoji（单字符或带变体选择器）
 * - Emoji_Keycap_Sequence: 按键帽序列（1️⃣ 2️⃣ 等）
 * - RGI_Emoji_Modifier_Sequence: 带皮肤色调的 Emoji
 * - RGI_Emoji_Flag_Sequence: 国旗序列
 * 
 * emoji-zwj-sequences.txt:
 * - RGI_Emoji_ZWJ_Sequence: ZWJ 连接的复合 Emoji
 *   如：👨‍👩‍👧‍👦 (家庭)、👨‍⚕️ (男医生)、🧑‍🤝‍🧑 (手拉手)
 * 
 * emoji-tag-sequences.txt:
 * - RGI_Emoji_Tag_Sequence: Tag 序列（子地区旗帜）
 *   如：🏴󠁧󠁢󠁥󠁮󠁧󠁿 (英格兰)、🏴󠁧󠁢󠁳󠁣󠁴󠁿 (苏格兰)
 * 
 * 文件格式：
 *   码点序列 ; 属性名 # 注释
 *   如：1F44D 1F3FB ; RGI_Emoji_Modifier_Sequence # thumbs up + light skin tone
 * 
 * @param filename 序列属性文件路径
 */
void parse_sequence_prop_list(const char *filename)
{
    FILE *f;
    char line[4096], *p, buf[256], *q, *p_start;
    uint32_t c0, c1, c;
    int idx, seq_len;
    int seq[SEQ_MAX_LEN];  // 栈分配序列缓冲区
    
    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        p_start = p;

        // 找到分号，提取属性名
        p = strchr(p, ';');
        if (!p)
            continue;
        p++;
        p += strspn(p, " \t");
        q = buf;
        while (*p != '\0' && *p != ' ' && *p != '#' && *p != '\t' && *p != ';') {
            if ((q - buf) < sizeof(buf) - 1)
                *q++ = *p;
            p++;
        }
        *q = '\0';
        
        // 查找属性索引
        idx = find_name(unicode_sequence_prop_name,
                      countof(unicode_sequence_prop_name), buf);
        if (idx < 0) {
            fprintf(stderr, "Property not found: %s\n", buf);
            exit(1);
        }
        
        // 解析码点序列
        p = p_start;
        c0 = strtoul(p, (char **)&p, 16);
        assert(c0 <= CHARCODE_MAX);
        
        // 检查是否为范围（..）
        if (*p == '.' && p[1] == '.') {
            p += 2;
            c1 = strtoul(p, (char **)&p, 16);
            assert(c1 <= CHARCODE_MAX);
            // 范围：为每个码点创建单元素序列
            for(c = c0; c <= c1; c++) {
                seq[0] = c;
                add_sequence_prop(idx, 1, seq);
            }
        } else {
            // 序列：收集所有码点
            seq_len = 0;
            seq[seq_len++] = c0;
            for(;;) {
                while (isspace(*p))
                    p++;
                if (*p == ';' || *p == '\0')
                    break;
                c0 = strtoul(p, (char **)&p, 16);
                assert(c0 <= CHARCODE_MAX);
                assert(seq_len < countof(seq));
                seq[seq_len++] = c0;
            }
            add_sequence_prop(idx, seq_len, seq);
        }
    }
    fclose(f);
}

/**
 * 解析 Scripts.txt 文件
 * 
 * Scripts.txt 定义每个 Unicode 字符所属的脚本（Script）。
 * 
 * 脚本是书写系统的分类，如：
 * - Latin: 拉丁字母（英语、法语、德语等）
 * - Cyrillic: 西里尔字母（俄语、保加利亚语等）
 * - Han: 汉字（中文、日文、韩文）
 * - Hiragana/Katakana: 日文假名
 * - Arabic: 阿拉伯字母
 * - Hebrew: 希伯来字母
 * - Devanagari: 天城文（印地语、梵语）
 * - Common: 多脚本共用字符（标点、数字等）
 * - Inherited: 继承字符（组合标记等）
 * 
 * 文件格式：
 *   码点范围 ; 脚本名 # 注释
 *   如：0041..005A    ; Latin # Lu
 * 
 * @param filename Scripts.txt 文件路径
 */
void parse_scripts(const char *filename)
{
    FILE *f;
    char line[4096], *p, buf[256], *q;
    uint32_t c0, c1, c;
    int i;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        c0 = strtoul(p, (char **)&p, 16);
        if (*p == '.' && p[1] == '.') {
            p += 2;
            c1 = strtoul(p, (char **)&p, 16);
        } else {
            c1 = c0;
        }
        assert(c1 <= CHARCODE_MAX);
        p += strspn(p, " \t");
        if (*p == ';') {
            p++;
            p += strspn(p, " \t");
            q = buf;
            while (*p != '\0' && *p != ' ' && *p != '#' && *p != '\t') {
                if ((q - buf) < sizeof(buf) - 1)
                    *q++ = *p;
                p++;
            }
            *q = '\0';
            i = find_name(unicode_script_name,
                          countof(unicode_script_name), buf);
            if (i < 0) {
                fprintf(stderr, "Unknown script: '%s'\n", buf);
                exit(1);
            }
            // 为范围内所有字符设置脚本
            for(c = c0; c <= c1; c++)
                unicode_db[c].script = i;
        }
    }
    fclose(f);
}

/**
 * 解析 ScriptExtensions.txt 文件
 * 
 * ScriptExtensions.txt 定义字符的脚本扩展（Script Extensions）。
 * 
 * 背景：
 * 某些字符可以用于多个脚本。例如：
 * - 句号 "." 用于拉丁文、西里尔文、希腊文等几乎所有脚本
 * - 数字 "0-9" 用于多种脚本
 * - 某些标点符号在多种脚本中通用
 * 
 * 主脚本（script）字段只存储一个主要脚本，
 * 脚本扩展（script_ext）字段存储该字符可用的所有其他脚本。
 * 
 * 用途：
 * - 文本分段（确定一段文本属于哪种脚本）
 * - 字体选择（选择能覆盖该脚本的字体）
 * - 拼写检查（使用正确的脚本报典）
 * 
 * 文件格式：
 *   码点范围 ; 脚本列表 # 注释
 *   脚本列表：空格分隔的脚本缩写
 *   如：00B7          ; Latn Grek Ital # 中点
 * 
 * @param filename ScriptExtensions.txt 文件路径
 */
void parse_script_extensions(const char *filename)
{
    FILE *f;
    char line[4096], *p, buf[256], *q;
    uint32_t c0, c1, c;
    int i;
    uint8_t script_ext[255];  // 临时缓冲区（最大 255 个脚本）
    int script_ext_len;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }

    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '@' || *p == '\0')
            continue;
        c0 = strtoul(p, (char **)&p, 16);
        if (*p == '.' && p[1] == '.') {
            p += 2;
            c1 = strtoul(p, (char **)&p, 16);
        } else {
            c1 = c0;
        }
        assert(c1 <= CHARCODE_MAX);
        p += strspn(p, " \t");
        script_ext_len = 0;
        if (*p == ';') {
            p++;
            // 解析空格分隔的脚本列表
            for(;;) {
                p += strspn(p, " \t");
                q = buf;
                while (*p != '\0' && *p != ' ' && *p != '#' && *p != '\t') {
                    if ((q - buf) < sizeof(buf) - 1)
                        *q++ = *p;
                    p++;
                }
                *q = '\0';
                if (buf[0] == '\0')
                    break;  // 空字符串表示结束
                i = find_name(unicode_script_short_name,
                              countof(unicode_script_short_name), buf);
                if (i < 0) {
                    fprintf(stderr, "Script not found: %s\n", buf);
                    exit(1);
                }
                assert(script_ext_len < sizeof(script_ext));
                script_ext[script_ext_len++] = i;
            }
            // 为范围内所有字符分配脚本扩展数组
            for(c = c0; c <= c1; c++) {
                CCInfo *ci = &unicode_db[c];
                ci->script_ext_len = script_ext_len;
                ci->script_ext = malloc(sizeof(ci->script_ext[0]) * script_ext_len);
                for(i = 0; i < script_ext_len; i++)
                    ci->script_ext[i] = script_ext[i];
            }
        }
    }
    fclose(f);
}

/* ========== 调试输出函数 ========== */

/**
 * 打印单个字符的大小写转换信息（调试用）
 * @param ci 字符信息结构
 * @param i 码点
 */
void dump_cc_info(CCInfo *ci, int i)
{
    int j;
    printf("%05x:", i);
    if (ci->u_len != 0) {
        printf(" U:");
        for(j = 0; j < ci->u_len; j++)
            printf(" %05x", ci->u_data[j]);
    }
    if (ci->l_len != 0) {
        printf(" L:");
        for(j = 0; j < ci->l_len; j++)
            printf(" %05x", ci->l_data[j]);
    }
    if (ci->f_len != 0) {
        printf(" F:");
        for(j = 0; j < ci->f_len; j++)
            printf(" %05x", ci->f_data[j]);
    }
    printf("\n");
}

/**
 * 打印所有有大小写转换的字符信息（调试用）
 * @param tab Unicode 字符数据库
 */
void dump_unicode_data(CCInfo *tab)
{
    int i;
    CCInfo *ci;
    for(i = 0; i <= CHARCODE_MAX; i++) {
        ci = &tab[i];
        if (ci->u_len != 0 || ci->l_len != 0 || ci->f_len != 0) {
            dump_cc_info(ci, i);
        }
    }
}

/* ========== 大小写转换表压缩算法 ========== */
/*
 * 背景：
 * Unicode 有 1,114,112 个码点，如果为每个字符存储完整的大小写转换信息，
 * 需要大量内存。但实际上：
 * 1. 大部分字符（如汉字、标点）没有大小写转换
 * 2. 许多字符的转换遵循简单规律（如 A→a, B→c, C→c 是连续的）
 * 
 * 压缩策略：
 * 1. 游程编码（RLE）：将连续相同模式的字符合并为一个"运行"（run）
 * 2. 类型编码：定义 14 种运行类型（RUN_TYPE_*），每种用最紧凑的方式编码
 * 3. 索引表：使用二级索引（表 1+ 表 2）进一步压缩
 * 
 * 运行类型示例：
 * - RUN_TYPE_U: 大写字母连续运行（A→a, B→b, C→c...）
 * - RUN_TYPE_L: 小写字母连续运行
 * - RUN_TYPE_UL: 交替运行（a→A, B→b, c→C, D→d...）
 * - RUN_TYPE_LSU: 特殊三字符运行（希腊语最终 sigma）
 * - RUN_TYPE_UF_EXT2: 带 2 个扩展字符的大写 + 折叠
 */

/**
 * 判断是否为"复杂"大小写情况
 * 
 * 简单情况：
 * - 单字符映射到单字符
 * - 大写和小写映射相同（f_data == l_data）
 * 
 * 复杂情况（需要特殊处理）：
 * - 多字符映射：如 ß → SS
 * - 大写和小写不同：如 Σ → σ (小写) 但 Σ 本身不是小写
 * - 折叠和小写不同：某些语言特定规则
 * 
 * @param ci 字符信息结构
 * @return TRUE 如果是复杂情况
 */
BOOL is_complicated_case(const CCInfo *ci)
{
    return (ci->u_len > 1 || ci->l_len > 1 ||  // 多字符映射
            (ci->u_len > 0 && ci->l_len > 0) ||  // 同时有大写和小写
            (ci->f_len != ci->l_len) ||  // 折叠和小写长度不同
            (memcmp(ci->f_data, ci->l_data, ci->f_len * sizeof(ci->f_data[0])) != 0));  // 折叠和小写内容不同
}

#ifndef USE_TEST
/**
 * 大小写转换运行类型枚举（14 种）
 * 
 * 这些类型用于压缩编码连续的大小写转换模式。
 * 每个类型的编码格式不同，以最小化存储空间。
 * 
 * 类型说明：
 * - U: 大写字母运行（code→code+1, code+1→code+2, ...）
 * - L: 小写字母运行
 * - UF: 大写 + 折叠相同（大多数拉丁字母）
 * - LF: 小写 + 折叠相同
 * - UL: 交替运行（小写→大写，大写→小写，如希腊语）
 * - LSU: 特殊三字符运行（希腊语最终 sigma：Σ→σ, σ→ς, ς→Σ）
 * - U2L_399_EXT2: 大写→小写 + 2 扩展字符（特殊希腊字母）
 * - UF_D20: 大写 + 折叠，差值为 0x20（ASCII 大小写）
 * - UF_D1_EXT: 大写 + 折叠，差值为 1 + 扩展
 * - U_EXT/LF_EXT: 带扩展数据的 U/LF 类型
 * - UF_EXT2/UF_EXT3: 带 2/3 个扩展字符的 UF 类型
 * - LF_EXT2: 带 2 个扩展字符的 LF 类型
 */
enum {
    RUN_TYPE_U,           // 0: 大写字母运行
    RUN_TYPE_L,           // 1: 小写字母运行
    RUN_TYPE_UF,          // 2: 大写 + 折叠相同
    RUN_TYPE_LF,          // 3: 小写 + 折叠相同
    RUN_TYPE_UL,          // 4: 交替运行（小写↔大写）
    RUN_TYPE_LSU,         // 5: 希腊语最终 sigma 特殊三字符
    RUN_TYPE_U2L_399_EXT2, // 6: 大写→小写 + 2 扩展（0x0399）
    RUN_TYPE_UF_D20,      // 7: 大写 + 折叠，差值 0x20
    RUN_TYPE_UF_D1_EXT,   // 8: 大写 + 折叠，差值 1 + 扩展
    RUN_TYPE_U_EXT,       // 9: 带扩展的 U
    RUN_TYPE_LF_EXT,      // 10: 带扩展的 LF
    RUN_TYPE_UF_EXT2,     // 11: 带 2 扩展的 UF
    RUN_TYPE_LF_EXT2,     // 12: 带 2 扩展的 LF
    RUN_TYPE_UF_EXT3,     // 13: 带 3 扩展的 UF
};
#endif

/**
 * 运行类型名称表（调试用）
 */
const char *run_type_str[] = {
    "U",
    "L",
    "UF",
    "LF",
    "UL",
    "LSU",
    "U2L_399_EXT2",
    "UF_D20",
    "UF_D1_EXT",
    "U_EXT",
    "LF_EXT",
    "UF_EXT2",
    "LF_EXT2",
    "UF_EXT3",
};

/**
 * 大小写转换表条目结构
 * 
 * 这个结构体在 build_conv_table() 中用于临时存储
 * 分析结果，最终会被压缩编码到 case_conv_table1/2 中。
 * 
 * 字段说明：
 * - code: 运行起始码点（17 位，0x000000-0x10FFFF）
 * - len: 运行长度（7 位，1-127 个字符）
 * - type: 运行类型（4 位，0-13）
 * - data: 目标码点或索引（取决于类型）
 * - ext_len: 扩展数据长度（0-3）
 * - ext_data: 扩展数据数组（用于复杂映射）
 * - data_index: 压缩后的数据索引（用于间接引用）
 * 
 * 压缩后格式（32 位）：
 *   [31:15] code (17 位)
 *   [14:8]  len (7 位)
 *   [7:4]   type (4 位)
 *   [3:0]   data_index 高位 (4 位)
 *   data_index 低 8 位在 case_conv_table2 中
 */
typedef struct {
    int code;           // 运行起始码点
    int len;            // 运行长度（字符数）
    int type;           // 运行类型（RUN_TYPE_*）
    int data;           // 目标码点或临时数据
    int ext_len;        // 扩展数据长度
    int ext_data[3];    // 扩展数据（最多 3 个码点）
    int data_index;     // 压缩后的数据索引
} TableEntry;

/**
 * 获取简单小写形式
 * 
 * 用于某些运行类型判断。如果字符的小写映射是单字符，
 * 返回该码点；否则返回自身（表示无简单小写）。
 * 
 * @param tab Unicode 字符数据库
 * @param c 码点
 * @return 小写码点或自身
 */
static int simple_to_lower(CCInfo *tab, int c)
{
    if (tab[c].l_len != 1)
        return c;
    return tab[c].l_data[0];
}

/* code (17), len (7), type (4) - 位域布局注释 */

/**
 * 查找并识别大小写转换运行类型
 * 
 * 这是整个压缩算法的核心函数。它分析从 code 开始的字符序列，
 * 识别出符合哪种预定义运行类型（RUN_TYPE_*），并填充 TableEntry。
 * 
 * 算法流程：
 * 1. 特殊检查：首先检查是否为希腊语最终 sigma（LSU）三字符模式
 * 2. 复杂情况：如果当前字符是"复杂"大小写，尝试匹配 13 种复杂类型
 * 3. 简单情况：如果是简单 1:1 映射，查找连续运行（UL/U/LF）
 * 
 * 运行类型优先级（从上到下）：
 * 1. LSU: 希腊语最终 sigma 特殊三字符循环（Σ→σ→ς→Σ）
 * 2. UF: 大写 + 折叠相同，连续运行
 * 3. UF_EXT2: 带 2 扩展的大写 + 折叠（希腊语带 iota 下标）
 * 4. U2L_399_EXT2: 大写→小写 + 0x0399 扩展
 * 5. L: 小写连续运行
 * 6. UF_D20: ASCII 大小写（差值 0x20）
 * 7. UF_D1_EXT: 差值 1 + 扩展
 * 8. LF_EXT2/UF_EXT2/UF_EXT3: 各种扩展类型
 * 9. UL: 交替运行（小写↔大写）
 * 10. U/LF: 简单大写或小写运行
 * 
 * @param te 输出表条目
 * @param tab Unicode 字符数据库
 * @param code 起始码点
 */
void find_run_type(TableEntry *te, CCInfo *tab, int code)
{
    int is_lower, len;
    CCInfo *ci, *ci1, *ci2;

    ci = &tab[code];
    ci1 = &tab[code + 1];
    ci2 = &tab[code + 2];
    te->code = code;

    /* ========== 特殊情况 1: 希腊语最终 sigma（LSU） ==========
     * 
     * 希腊语 sigma 有三种形式：
     * - Σ (U+03A3): 大写 Sigma
     * - σ (U+03C3): 小写 sigma（词中形式）
     * - ς (U+03C2): 小写最终 sigma（词尾形式）
     * 
     * 转换关系形成三字符循环：
     * - Σ (code)   → 小写：σ (code+2), 折叠：σ (code+2), 无大写
     * - σ (code+1) → 小写：σ (code+2), 折叠：σ (code+2), 大写：Σ (code)
     * - ς (code+2) → 无小写，无折叠，大写：Σ (code)
     * 
     * 这是一个独特的三字符模式，用 RUN_TYPE_LSU 编码。
     */
    if (ci->l_len == 1 && ci->l_data[0] == code + 2 &&
        ci->f_len == 1 && ci->f_data[0] == ci->l_data[0] &&
        ci->u_len == 0 &&

        ci1->l_len == 1 && ci1->l_data[0] == code + 2 &&
        ci1->f_len == 1 && ci1->f_data[0] == ci1->l_data[0] &&
        ci1->u_len == 1 && ci1->u_data[0] == code &&

        ci2->l_len == 0 &&
        ci2->f_len == 0 &&
        ci2->u_len == 1 && ci2->u_data[0] == code) {
        te->len = 3;
        te->data = 0;
        te->type = RUN_TYPE_LSU;
        return;
    }

    /* ========== 复杂大小写情况处理 ==========
     * 
     * 复杂情况包括：
     * - 多字符映射（如 ß → SS）
     * - 大写和小写同时存在
     * - 折叠和小写不同
     * 
     * 按优先级尝试匹配各种运行类型：
     */
    if (is_complicated_case(ci)) {
        /* ----- 类型 1: UF (大写 + 折叠相同，连续运行) -----
         * 模式：每个字符的大写和小写折叠相同，且连续递增
         * 如：某些希腊字母的大写形式
         */
        len = 1;
        while (code + len <= CHARCODE_MAX) {
            ci1 = &tab[code + len];
            if (ci1->u_len != 1 ||
                ci1->u_data[0] != ci->u_data[0] + len ||
                ci1->l_len != 0 ||
                ci1->f_len != 1 || ci1->f_data[0] != ci1->u_data[0])
                break;
            len++;
        }
        if (len > 1) {
            te->len = len;
            te->type = RUN_TYPE_UF;
            te->data = ci->u_data[0];
            return;
        }

        /* ----- 类型 2: UF_EXT2 (带 2 扩展的大写 + 折叠) -----
         * 模式：大写和折叠都是 2 字符，第二个字符固定
         * 特例：希腊语带 iota 下标的字母
         * - 第二个字符：0x0399 (GREEK CAPITAL LETTER IOTA)
         * - 折叠第二个字符：0x03B9 (GREEK SMALL LETTER IOTA)
         */
        if (ci->l_len == 0 &&
            ci->u_len == 2 && ci->u_data[1] == 0x399 &&
            ci->f_len == 2 && ci->f_data[1] == 0x3B9 &&
            ci->f_data[0] == simple_to_lower(tab, ci->u_data[0])) {
            len = 1;
            while (code + len <= CHARCODE_MAX) {
                ci1 = &tab[code + len];
                if (!(ci1->u_len == 2 &&
                      ci1->u_data[1] == ci->u_data[1] &&
                      ci1->u_data[0] == ci->u_data[0] + len &&
                      ci1->f_len == 2 &&
                      ci1->f_data[1] == ci->f_data[1] &&
                      ci1->f_data[0] == ci->f_data[0] + len &&
                      ci1->l_len == 0))
                    break;
                len++;
            }
            te->len = len;
            te->type = RUN_TYPE_UF_EXT2;
            te->ext_data[0] = ci->u_data[0];
            te->ext_data[1] = ci->u_data[1];
            te->ext_len = 2;
            return;
        }

        /* ----- 类型 3: U2L_399_EXT2 (大写→小写 + 0x0399 扩展) -----
         * 模式：大写 2 字符（含 0x0399），小写 1 字符，折叠=小写
         * 特例：希腊语带 iota 下标的字母的小写形式
         */
        if (ci->u_len == 2 && ci->u_data[1] == 0x399 &&
            ci->l_len == 1 &&
            ci->f_len == 1 && ci->f_data[0] == ci->l_data[0]) {
            len = 1;
            while (code + len <= CHARCODE_MAX) {
                ci1 = &tab[code + len];
                if (!(ci1->u_len == 2 &&
                      ci1->u_data[1] == 0x399 &&
                      ci1->u_data[0] == ci->u_data[0] + len &&
                      ci1->l_len == 1 &&
                      ci1->l_data[0] == ci->l_data[0] + len &&
                      ci1->f_len == 1 && ci1->f_data[0] == ci1->l_data[0]))
                    break;
                len++;
            }
            te->len = len;
            te->type = RUN_TYPE_U2L_399_EXT2;
            te->ext_data[0] = ci->u_data[0];
            te->ext_data[1] = ci->l_data[0];
            te->ext_len = 2;
            return;
        }

        /* ----- 类型 4: L (小写连续运行) -----
         * 模式：只有小写映射，无大写和折叠，连续递增
         */
        if (ci->l_len == 1 && ci->u_len == 0 && ci->f_len == 0) {
            len = 1;
            while (code + len <= CHARCODE_MAX) {
                ci1 = &tab[code + len];
                if (!(ci1->l_len == 1 &&
                      ci1->l_data[0] == ci->l_data[0] + len &&
                      ci1->u_len == 0 && ci1->f_len == 0))
                    break;
                len++;
            }
            te->len = len;
            te->type = RUN_TYPE_L;
            te->data = ci->l_data[0];
            return;
        }

        /* ----- 类型 5-13: 单字符特殊情况 -----
         * 这些类型长度都为 1，用不同的编码方式优化存储
         */
        
        /* 类型 5: UF_D20 (ASCII 大小写，差值 0x20)
         * 如：A (0x41) → a (0x61), 差值 0x20
         */
        if (ci->l_len == 0 &&
            ci->u_len == 1 &&
            ci->u_data[0] < 0x1000 &&
            ci->f_len == 1 && ci->f_data[0] == ci->u_data[0] + 0x20) {
            te->len = 1;
            te->type = RUN_TYPE_UF_D20;
            te->data = ci->u_data[0];
        } 
        /* 类型 6: UF_D1_EXT (差值 1 + 扩展) */
        else if (ci->l_len == 0 &&
                   ci->u_len == 1 &&
                   ci->f_len == 1 && ci->f_data[0] == ci->u_data[0] + 1) {
            te->len = 1;
            te->type = RUN_TYPE_UF_D1_EXT;
            te->ext_data[0] = ci->u_data[0];
            te->ext_len = 1;
        } 
        /* 类型 7: LF_EXT2 (小写 + 折叠，2 扩展字符) */
        else if (ci->l_len == 2 && ci->u_len == 0 && ci->f_len == 2 &&
                   ci->l_data[0] == ci->f_data[0] &&
                   ci->l_data[1] == ci->f_data[1]) {
            te->len = 1;
            te->type = RUN_TYPE_LF_EXT2;
            te->ext_data[0] = ci->l_data[0];
            te->ext_data[1] = ci->l_data[1];
            te->ext_len = 2;
        } 
        /* 类型 8: UF_EXT2 (大写 + 折叠，2 扩展字符) */
        else if (ci->u_len == 2 && ci->l_len == 0 && ci->f_len == 2 &&
                   ci->f_data[0] == simple_to_lower(tab, ci->u_data[0]) &&
                   ci->f_data[1] == simple_to_lower(tab, ci->u_data[1])) {
            te->len = 1;
            te->type = RUN_TYPE_UF_EXT2;
            te->ext_data[0] = ci->u_data[0];
            te->ext_data[1] = ci->u_data[1];
            te->ext_len = 2;
        } 
        /* 类型 9: UF_EXT3 (大写 + 折叠，3 扩展字符) */
        else if (ci->u_len == 3 && ci->l_len == 0 && ci->f_len == 3 &&
                   ci->f_data[0] == simple_to_lower(tab, ci->u_data[0]) &&
                   ci->f_data[1] == simple_to_lower(tab, ci->u_data[1]) &&
                   ci->f_data[2] == simple_to_lower(tab, ci->u_data[2])) {
            te->len = 1;
            te->type = RUN_TYPE_UF_EXT3;
            te->ext_data[0] = ci->u_data[0];
            te->ext_data[1] = ci->u_data[1];
            te->ext_data[2] = ci->u_data[2];
            te->ext_len = 3;
        } 
        /* 类型 10: 特殊连字 U+FB05 (LATIN SMALL LIGATURE LONG S T) */
        else if (ci->u_len == 2 && ci->l_len == 0 && ci->f_len == 1) {
            // U+FB05 LATIN SMALL LIGATURE LONG S T
            assert(code == 0xFB05);
            te->len = 1;
            te->type = RUN_TYPE_UF_EXT2;
            te->ext_data[0] = ci->u_data[0];
            te->ext_data[1] = ci->u_data[1];
            te->ext_len = 2;
        } 
        /* 类型 11: 特殊希腊字母 U+1FD3/U+1FE3 */
        else if (ci->u_len == 3 && ci->l_len == 0 && ci->f_len == 1) {
            // U+1FD3 GREEK SMALL LETTER IOTA WITH DIALYTIKA AND OXIA
            // U+1FE3 GREEK SMALL LETTER UPSILON WITH DIALYTIKA AND OXIA
            assert(code == 0x1FD3 || code == 0x1FE3);
            te->len = 1;
            te->type = RUN_TYPE_UF_EXT3;
            te->ext_data[0] = ci->u_data[0];
            te->ext_data[1] = ci->u_data[1];
            te->ext_data[2] = ci->u_data[2];
            te->ext_len = 3;
        } 
        else {
            printf("unsupported encoding case:\n");
            dump_cc_info(ci, code);
            abort();  // 遇到未支持的编码情况，终止
        }
    } 
    /* ========== 简单大小写情况处理 ==========
     * 
     * 简单情况：1:1 映射，无多字符转换
     * 尝试查找连续相同模式的运行
     */
    else {
        /* ----- 类型：UL (交替运行：小写↔大写) -----
         * 模式：连续的小写 - 大写对，如：
         * a (小写) → A (大写), A (大写) → a (小写)
         * b (小写) → B (大写), B (大写) → b (小写)
         * 
         * 这种模式在希腊语等脚本中常见。
         */
        len = 0;
        for(;;) {
            if (code >= CHARCODE_MAX || len >= 126)
                break;
            ci = &tab[code + len];
            ci1 = &tab[code + len + 1];
            if (is_complicated_case(ci) || is_complicated_case(ci1)) {
                break;
            }
            if (ci->l_len != 1 || ci->l_data[0] != code + len + 1)
                break;
            if (ci1->u_len != 1 || ci1->u_data[0] != code + len)
                break;
            len += 2;  // 一次处理一对
        }
        if (len > 0) {
            te->len = len;
            te->type = RUN_TYPE_UL;
            te->data = 0;
            return;
        }

        /* ----- 类型：U 或 LF (简单大写或小写运行) -----
         * 模式：连续的大写或小写映射，递增
         */
        ci = &tab[code];
        is_lower = ci->l_len > 0;  // 判断是小写运行还是大写运行
        len = 1;
        while (code + len <= CHARCODE_MAX) {
            ci1 = &tab[code + len];
            if (is_complicated_case(ci1))
                break;
            if (is_lower) {
                if (ci1->l_len != 1 ||
                    ci1->l_data[0] != ci->l_data[0] + len)
                    break;
            } else {
                if (ci1->u_len != 1 ||
                    ci1->u_data[0] != ci->u_data[0] + len)
                    break;
            }
            len++;
        }
        te->len = len;
        if (is_lower) {
            te->type = RUN_TYPE_LF;  // 小写 + 折叠相同
            te->data = ci->l_data[0];
        } else {
            te->type = RUN_TYPE_U;  // 大写运行
            te->data = ci->u_data[0];
        }
    }
}

TableEntry conv_table[1000];
int conv_table_len;
int ext_data[1000];
int ext_data_len;

void dump_case_conv_table1(void)
{
    int i, j;
    const TableEntry *te;

    for(i = 0; i < conv_table_len; i++) {
        te = &conv_table[i];
        printf("%05x %02x %-10s %05x",
               te->code, te->len, run_type_str[te->type], te->data);
        for(j = 0; j < te->ext_len; j++) {
            printf(" %05x", te->ext_data[j]);
        }
        printf("\n");
    }
    printf("table_len=%d ext_len=%d\n", conv_table_len, ext_data_len);
}

int find_data_index(const TableEntry *conv_table, int len, int data)
{
    int i;
    const TableEntry *te;
    for(i = 0; i < len; i++) {
        te = &conv_table[i];
        if (te->code == data)
            return i;
    }
    return -1;
}

int find_ext_data_index(int data)
{
    int i;
    for(i = 0; i < ext_data_len; i++) {
        if (ext_data[i] == data)
            return i;
    }
    assert(ext_data_len < countof(ext_data));
    ext_data[ext_data_len++] = data;
    return ext_data_len - 1;
}

void build_conv_table(CCInfo *tab)
{
    int code, i, j;
    CCInfo *ci;
    TableEntry *te;

    te = conv_table;
    for(code = 0; code <= CHARCODE_MAX; code++) {
        ci = &tab[code];
        if (ci->u_len == 0 && ci->l_len == 0 && ci->f_len == 0)
            continue;
        assert(te - conv_table < countof(conv_table));
        find_run_type(te, tab, code);
#if 0
        if (te->type == RUN_TYPE_TODO) {
            printf("TODO: ");
            dump_cc_info(ci, code);
        }
#endif
        assert(te->len <= 127);
        code += te->len - 1;
        te++;
    }
    conv_table_len = te - conv_table;

    /* find the data index */
    for(i = 0; i < conv_table_len; i++) {
        int data_index;
        te = &conv_table[i];

        switch(te->type) {
        case RUN_TYPE_U:
        case RUN_TYPE_L:
        case RUN_TYPE_UF:
        case RUN_TYPE_LF:
            data_index = find_data_index(conv_table, conv_table_len, te->data);
            if (data_index < 0) {
                switch(te->type) {
                case RUN_TYPE_U:
                    te->type = RUN_TYPE_U_EXT;
                    te->ext_len = 1;
                    te->ext_data[0] = te->data;
                    break;
                case RUN_TYPE_LF:
                    te->type = RUN_TYPE_LF_EXT;
                    te->ext_len = 1;
                    te->ext_data[0] = te->data;
                    break;
                default:
                    printf("%05x: index not found\n", te->code);
                    exit(1);
                }
            } else {
                te->data_index = data_index;
            }
            break;
        case RUN_TYPE_UF_D20:
            te->data_index = te->data;
            break;
        }
    }

    /* find the data index for ext_data */
    for(i = 0; i < conv_table_len; i++) {
        te = &conv_table[i];
        if (te->type == RUN_TYPE_UF_EXT3) {
            int p, v;
            v = 0;
            for(j = 0; j < 3; j++) {
                p = find_ext_data_index(te->ext_data[j]);
                assert(p < 16);
                v = (v << 4) | p;
            }
            te->data_index = v;
        }
    }

    for(i = 0; i < conv_table_len; i++) {
        te = &conv_table[i];
        if (te->type == RUN_TYPE_LF_EXT2 ||
            te->type == RUN_TYPE_UF_EXT2 ||
            te->type == RUN_TYPE_U2L_399_EXT2) {
            int p, v;
            v = 0;
            for(j = 0; j < 2; j++) {
                p = find_ext_data_index(te->ext_data[j]);
                assert(p < 64);
                v = (v << 6) | p;
            }
            te->data_index = v;
        }
    }

    for(i = 0; i < conv_table_len; i++) {
        te = &conv_table[i];
        if (te->type == RUN_TYPE_UF_D1_EXT ||
            te->type == RUN_TYPE_U_EXT ||
            te->type == RUN_TYPE_LF_EXT) {
            te->data_index = find_ext_data_index(te->ext_data[0]);
        }
    }
#ifdef DUMP_CASE_CONV_TABLE
    dump_case_conv_table1();
#endif
}

void dump_case_conv_table(FILE *f)
{
    int i;
    uint32_t v;
    const TableEntry *te;

    total_tables++;
    total_table_bytes += conv_table_len * sizeof(uint32_t);
    fprintf(f, "static const uint32_t case_conv_table1[%d] = {", conv_table_len);
    for(i = 0; i < conv_table_len; i++) {
        if (i % 4 == 0)
            fprintf(f, "\n   ");
        te = &conv_table[i];
        v = te->code << (32 - 17);
        v |= te->len << (32 - 17 - 7);
        v |= te->type << (32 - 17 - 7 - 4);
        v |= te->data_index >> 8;
        fprintf(f, " 0x%08x,", v);
    }
    fprintf(f, "\n};\n\n");

    total_tables++;
    total_table_bytes += conv_table_len;
    fprintf(f, "static const uint8_t case_conv_table2[%d] = {", conv_table_len);
    for(i = 0; i < conv_table_len; i++) {
        if (i % 8 == 0)
            fprintf(f, "\n   ");
        te = &conv_table[i];
        fprintf(f, " 0x%02x,", te->data_index & 0xff);
    }
    fprintf(f, "\n};\n\n");

    total_tables++;
    total_table_bytes += ext_data_len * sizeof(uint16_t);
    fprintf(f, "static const uint16_t case_conv_ext[%d] = {", ext_data_len);
    for(i = 0; i < ext_data_len; i++) {
        if (i % 8 == 0)
            fprintf(f, "\n   ");
        fprintf(f, " 0x%04x,", ext_data[i]);
    }
    fprintf(f, "\n};\n\n");
}


static CCInfo *global_tab;

static int sp_cc_cmp(const void *p1, const void *p2)
{
    CCInfo *c1 = &global_tab[*(const int *)p1];
    CCInfo *c2 = &global_tab[*(const int *)p2];
    if (c1->f_len < c2->f_len) {
        return -1;
    } else if (c2->f_len < c1->f_len) {
        return 1;
    } else {
        return memcmp(c1->f_data, c2->f_data, sizeof(c1->f_data[0]) * c1->f_len);
    }
}

/* dump the case special cases (multi character results which are
   identical and need specific handling in lre_canonicalize() */
void dump_case_folding_special_cases(CCInfo *tab)
{
    int i, len, j;
    int *perm;

    perm = malloc(sizeof(perm[0]) * (CHARCODE_MAX + 1));
    for(i = 0; i <= CHARCODE_MAX; i++)
        perm[i] = i;
    global_tab = tab;
    qsort(perm, CHARCODE_MAX + 1, sizeof(perm[0]), sp_cc_cmp);
    for(i = 0; i <= CHARCODE_MAX;) {
        if (tab[perm[i]].f_len <= 1) {
            i++;
        } else {
            len = 1;
            while ((i + len) <= CHARCODE_MAX && !sp_cc_cmp(&perm[i], &perm[i + len]))
                len++;

            if (len > 1) {
                for(j = i; j < i + len; j++)
                    dump_cc_info(&tab[perm[j]], perm[j]);
            }
            i += len;
        }
    }
    free(perm);
    global_tab = NULL;
}


int tabcmp(const int *tab1, const int *tab2, int n)
{
    int i;
    for(i = 0; i < n; i++) {
        if (tab1[i] != tab2[i])
            return -1;
    }
    return 0;
}

void dump_str(const char *str, const int *buf, int len)
{
    int i;
    printf("%s=", str);
    for(i = 0; i < len; i++)
        printf(" %05x", buf[i]);
    printf("\n");
}

void compute_internal_props(void)
{
    int i;
    BOOL has_ul;

    for(i = 0; i <= CHARCODE_MAX; i++) {
        CCInfo *ci = &unicode_db[i];
        has_ul = (ci->u_len != 0 || ci->l_len != 0 || ci->f_len != 0);
        if (has_ul) {
            assert(get_prop(i, PROP_Cased));
        } else {
            set_prop(i, PROP_Cased1, get_prop(i, PROP_Cased));
        }
        set_prop(i, PROP_ID_Continue1,
                 get_prop(i, PROP_ID_Continue) & (get_prop(i, PROP_ID_Start) ^ 1));
        set_prop(i, PROP_XID_Start1,
                 get_prop(i, PROP_ID_Start) ^ get_prop(i, PROP_XID_Start));
        set_prop(i, PROP_XID_Continue1,
                 get_prop(i, PROP_ID_Continue) ^ get_prop(i, PROP_XID_Continue));
        set_prop(i, PROP_Changes_When_Titlecased1,
                 get_prop(i, PROP_Changes_When_Titlecased) ^ (ci->u_len != 0));
        set_prop(i, PROP_Changes_When_Casefolded1,
                 get_prop(i, PROP_Changes_When_Casefolded) ^ (ci->f_len != 0));
        /* XXX: reduce table size (438 bytes) */
        set_prop(i, PROP_Changes_When_NFKC_Casefolded1,
                 get_prop(i, PROP_Changes_When_NFKC_Casefolded) ^ (ci->f_len != 0));
#if 0
        /* TEST */
#define M(x) (1U << GCAT_ ## x)
        {
            int b;
            b = ((M(Mn) | M(Cf) | M(Lm) | M(Sk)) >>
                 unicode_db[i].general_category) & 1;
            set_prop(i, PROP_Cased1,
                     get_prop(i, PROP_Case_Ignorable) ^ b);
        }
#undef M
#endif
    }
}

void dump_byte_table(FILE *f, const char *cname, const uint8_t *tab, int len)
{
    int i;

    total_tables++;
    total_table_bytes += len;
    fprintf(f, "static const uint8_t %s[%d] = {", cname, len);
    for(i = 0; i < len; i++) {
        if (i % 8 == 0)
            fprintf(f, "\n   ");
        fprintf(f, " 0x%02x,", tab[i]);
    }
    fprintf(f, "\n};\n\n");
}

void dump_index_table(FILE *f, const char *cname, const uint8_t *tab, int len)
{
    int i, code, offset;

    total_index++;
    total_index_bytes += len;
    fprintf(f, "static const uint8_t %s[%d] = {\n", cname, len);
    for(i = 0; i < len; i += 3) {
        code = tab[i] + (tab[i+1] << 8) + ((tab[i+2] & 0x1f) << 16);
        offset = ((i / 3) + 1) * 32 + (tab[i+2] >> 5);
        fprintf(f, "    0x%02x, 0x%02x, 0x%02x,", tab[i], tab[i+1], tab[i+2]);
        fprintf(f, "  // %6.5X at %d%s\n", code, offset,
                i == len - 3 ? " (upper bound)" : "");
    }
    fprintf(f, "};\n\n");
}

#define PROP_BLOCK_LEN 32

void build_prop_table(FILE *f, const char *name, int prop_index, BOOL add_index)
{
    int i, j, n, v, offset, code;
    DynBuf dbuf_s, *dbuf = &dbuf_s;
    DynBuf dbuf1_s, *dbuf1 = &dbuf1_s;
    DynBuf dbuf2_s, *dbuf2 = &dbuf2_s;
    const uint32_t *buf;
    int buf_len, block_end_pos, bit;
    char cname[128];

    dbuf_init(dbuf1);

    for(i = 0; i <= CHARCODE_MAX;) {
        v = get_prop(i, prop_index);
        j = i + 1;
        while (j <= CHARCODE_MAX && get_prop(j, prop_index) == v) {
            j++;
        }
        n = j - i;
        if (j == (CHARCODE_MAX + 1) && v == 0)
            break; /* no need to encode last zero run */
        //printf("%05x: %d %d\n", i, n, v);
        dbuf_put_u32(dbuf1, n - 1);
        i += n;
    }

    dbuf_init(dbuf);
    dbuf_init(dbuf2);
    buf = (uint32_t *)dbuf1->buf;
    buf_len = dbuf1->size / sizeof(buf[0]);

    /* the first value is assumed to be 0 */
    assert(get_prop(0, prop_index) == 0);

    block_end_pos = PROP_BLOCK_LEN;
    i = 0;
    code = 0;
    bit = 0;
    while (i < buf_len) {
        if (add_index && dbuf->size >= block_end_pos && bit == 0) {
            offset = (dbuf->size - block_end_pos);
            /* XXX: offset could be larger in case of runs of small
               lengths. Could add code to change the encoding to
               prevent it at the expense of one byte loss */
            assert(offset <= 7);
            v = code | (offset << 21);
            dbuf_putc(dbuf2, v);
            dbuf_putc(dbuf2, v >> 8);
            dbuf_putc(dbuf2, v >> 16);
            block_end_pos += PROP_BLOCK_LEN;
        }

        /* Compressed byte encoding:
           00..3F: 2 packed lengths: 3-bit + 3-bit
           40..5F: 5-bits plus extra byte for length
           60..7F: 5-bits plus 2 extra bytes for length
           80..FF: 7-bit length
           lengths must be incremented to get character count
           Ranges alternate between false and true return value.
         */
        v = buf[i];
        code += v + 1;
        bit ^= 1;
        if (v < 8 && (i + 1) < buf_len && buf[i + 1] < 8) {
            code += buf[i + 1] + 1;
            bit ^= 1;
            dbuf_putc(dbuf, (v << 3) | buf[i + 1]);
            i += 2;
        } else if (v < 128) {
            dbuf_putc(dbuf, 0x80 + v);
            i++;
        } else if (v < (1 << 13)) {
            dbuf_putc(dbuf, 0x40 + (v >> 8));
            dbuf_putc(dbuf, v);
            i++;
        } else {
            assert(v < (1 << 21));
            dbuf_putc(dbuf, 0x60 + (v >> 16));
            dbuf_putc(dbuf, v >> 8);
            dbuf_putc(dbuf, v);
            i++;
        }
    }

    if (add_index) {
        /* last index entry */
        v = code;
        dbuf_putc(dbuf2, v);
        dbuf_putc(dbuf2, v >> 8);
        dbuf_putc(dbuf2, v >> 16);
    }

#ifdef DUMP_TABLE_SIZE
    printf("prop %s: length=%d bytes\n", unicode_prop_name[prop_index],
           (int)(dbuf->size + dbuf2->size));
#endif
    snprintf(cname, sizeof(cname), "unicode_prop_%s_table", unicode_prop_name[prop_index]);
    dump_byte_table(f, cname, dbuf->buf, dbuf->size);
    if (add_index) {
        snprintf(cname, sizeof(cname), "unicode_prop_%s_index", unicode_prop_name[prop_index]);
        dump_index_table(f, cname, dbuf2->buf, dbuf2->size);
    }

    dbuf_free(dbuf);
    dbuf_free(dbuf1);
    dbuf_free(dbuf2);
}

void build_flags_tables(FILE *f)
{
    build_prop_table(f, "Cased1", PROP_Cased1, TRUE);
    build_prop_table(f, "Case_Ignorable", PROP_Case_Ignorable, TRUE);
    build_prop_table(f, "ID_Start", PROP_ID_Start, TRUE);
    build_prop_table(f, "ID_Continue1", PROP_ID_Continue1, TRUE);
}

void dump_name_table(FILE *f, const char *cname, const char **tab_name, int len,
                     const char **tab_short_name)
{
    int i, w, maxw;

    maxw = 0;
    for(i = 0; i < len; i++) {
        w = strlen(tab_name[i]);
        if (tab_short_name && tab_short_name[i][0] != '\0') {
            w += 1 + strlen(tab_short_name[i]);
        }
        if (maxw < w)
            maxw = w;
    }

    /* generate a sequence of strings terminated by an empty string */
    fprintf(f, "static const char %s[] =\n", cname);
    for(i = 0; i < len; i++) {
        fprintf(f, "    \"");
        w = fprintf(f, "%s", tab_name[i]);
        if (tab_short_name && tab_short_name[i][0] != '\0') {
            w += fprintf(f, ",%s", tab_short_name[i]);
        }
        fprintf(f, "\"%*s\"\\0\"\n", 1 + maxw - w, "");
    }
    fprintf(f, ";\n\n");
}

void build_general_category_table(FILE *f)
{
    int i, v, j, n, n1;
    DynBuf dbuf_s, *dbuf = &dbuf_s;
#ifdef DUMP_TABLE_SIZE
    int cw_count, cw_len_count[4], cw_start;
#endif

    fprintf(f, "typedef enum {\n");
    for(i = 0; i < GCAT_COUNT; i++)
        fprintf(f, "    UNICODE_GC_%s,\n", unicode_gc_name[i]);
    fprintf(f, "    UNICODE_GC_COUNT,\n");
    fprintf(f, "} UnicodeGCEnum;\n\n");

    dump_name_table(f, "unicode_gc_name_table",
                    unicode_gc_name, GCAT_COUNT,
                    unicode_gc_short_name);


    dbuf_init(dbuf);
#ifdef DUMP_TABLE_SIZE
    cw_count = 0;
    for(i = 0; i < 4; i++)
        cw_len_count[i] = 0;
#endif
    for(i = 0; i <= CHARCODE_MAX;) {
        v = unicode_db[i].general_category;
        j = i + 1;
        while (j <= CHARCODE_MAX && unicode_db[j].general_category == v)
            j++;
        n = j - i;
        /* compress Lu/Ll runs */
        if (v == GCAT_Lu) {
            n1 = 1;
            while ((i + n1) <= CHARCODE_MAX && unicode_db[i + n1].general_category == (v + (n1 & 1))) {
                n1++;
            }
            if (n1 > n) {
                v = 31;
                n = n1;
            }
        }
        //        printf("%05x %05x %d\n", i, n, v);
        n--;
#ifdef DUMP_TABLE_SIZE
        cw_count++;
        cw_start = dbuf->size;
#endif
        if (n < 7) {
            dbuf_putc(dbuf, (n << 5) | v);
        } else if (n < 7 + 128) {
            n1 = n - 7;
            assert(n1 < 128);
            dbuf_putc(dbuf, (0xf << 5) | v);
            dbuf_putc(dbuf, n1);
        } else if (n < 7 + 128 + (1 << 14)) {
            n1 = n - (7 + 128);
            assert(n1 < (1 << 14));
            dbuf_putc(dbuf, (0xf << 5) | v);
            dbuf_putc(dbuf, (n1 >> 8) + 128);
            dbuf_putc(dbuf, n1);
        } else {
            n1 = n - (7 + 128 + (1 << 14));
            assert(n1 < (1 << 22));
            dbuf_putc(dbuf, (0xf << 5) | v);
            dbuf_putc(dbuf, (n1 >> 16) + 128 + 64);
            dbuf_putc(dbuf, n1 >> 8);
            dbuf_putc(dbuf, n1);
        }
#ifdef DUMP_TABLE_SIZE
        cw_len_count[dbuf->size - cw_start - 1]++;
#endif
        i += n + 1;
    }
#ifdef DUMP_TABLE_SIZE
    printf("general category: %d entries [", cw_count);
    for(i = 0; i < 4; i++)
        printf(" %d", cw_len_count[i]);
    printf(" ], length=%d bytes\n", (int)dbuf->size);
#endif

    dump_byte_table(f, "unicode_gc_table", dbuf->buf, dbuf->size);

    dbuf_free(dbuf);
}

void build_script_table(FILE *f)
{
    int i, v, j, n, n1, type;
    DynBuf dbuf_s, *dbuf = &dbuf_s;
#ifdef DUMP_TABLE_SIZE
    int cw_count, cw_len_count[4], cw_start;
#endif

    fprintf(f, "typedef enum {\n");
    for(i = 0; i < SCRIPT_COUNT; i++)
        fprintf(f, "    UNICODE_SCRIPT_%s,\n", unicode_script_name[i]);
    fprintf(f, "    UNICODE_SCRIPT_COUNT,\n");
    fprintf(f, "} UnicodeScriptEnum;\n\n");

    dump_name_table(f, "unicode_script_name_table",
                    unicode_script_name, SCRIPT_COUNT,
                    unicode_script_short_name);

    dbuf_init(dbuf);
#ifdef DUMP_TABLE_SIZE
    cw_count = 0;
    for(i = 0; i < 4; i++)
        cw_len_count[i] = 0;
#endif
    for(i = 0; i <= CHARCODE_MAX;) {
        v = unicode_db[i].script;
        j = i + 1;
        while (j <= CHARCODE_MAX && unicode_db[j].script == v)
            j++;
        n = j - i;
        if (v == 0 && j == (CHARCODE_MAX + 1))
            break;
        //        printf("%05x %05x %d\n", i, n, v);
        n--;
#ifdef DUMP_TABLE_SIZE
        cw_count++;
        cw_start = dbuf->size;
#endif
        if (v == 0)
            type = 0;
        else
            type = 1;
        if (n < 96) {
            dbuf_putc(dbuf, n | (type << 7));
        } else if (n < 96 + (1 << 12)) {
            n1 = n - 96;
            assert(n1 < (1 << 12));
            dbuf_putc(dbuf, ((n1 >> 8) + 96) | (type << 7));
            dbuf_putc(dbuf, n1);
        } else {
            n1 = n - (96 + (1 << 12));
            assert(n1 < (1 << 20));
            dbuf_putc(dbuf, ((n1 >> 16) + 112) | (type << 7));
            dbuf_putc(dbuf, n1 >> 8);
            dbuf_putc(dbuf, n1);
        }
        if (type != 0)
            dbuf_putc(dbuf, v);

#ifdef DUMP_TABLE_SIZE
        cw_len_count[dbuf->size - cw_start - 1]++;
#endif
        i += n + 1;
    }
#ifdef DUMP_TABLE_SIZE
    printf("script: %d entries [", cw_count);
    for(i = 0; i < 4; i++)
        printf(" %d", cw_len_count[i]);
    printf(" ], length=%d bytes\n", (int)dbuf->size);
#endif

    dump_byte_table(f, "unicode_script_table", dbuf->buf, dbuf->size);

    dbuf_free(dbuf);
}

void build_script_ext_table(FILE *f)
{
    int i, j, n, n1, script_ext_len;
    DynBuf dbuf_s, *dbuf = &dbuf_s;
#if defined(DUMP_TABLE_SIZE)
    int cw_count = 0;
#endif

    dbuf_init(dbuf);
    for(i = 0; i <= CHARCODE_MAX;) {
        script_ext_len = unicode_db[i].script_ext_len;
        j = i + 1;
        while (j <= CHARCODE_MAX &&
               unicode_db[j].script_ext_len == script_ext_len &&
               !memcmp(unicode_db[j].script_ext, unicode_db[i].script_ext,
                       script_ext_len)) {
            j++;
        }
        n = j - i;
#if defined(DUMP_TABLE_SIZE)
        cw_count++;
#endif
        n--;
        if (n < 128) {
            dbuf_putc(dbuf, n);
        } else if (n < 128 + (1 << 14)) {
            n1 = n - 128;
            assert(n1 < (1 << 14));
            dbuf_putc(dbuf, (n1 >> 8) + 128);
            dbuf_putc(dbuf, n1);
        } else {
            n1 = n - (128 + (1 << 14));
            assert(n1 < (1 << 22));
            dbuf_putc(dbuf, (n1 >> 16) + 128 + 64);
            dbuf_putc(dbuf, n1 >> 8);
            dbuf_putc(dbuf, n1);
        }
        dbuf_putc(dbuf, script_ext_len);
        for(j = 0; j < script_ext_len; j++)
            dbuf_putc(dbuf, unicode_db[i].script_ext[j]);
        i += n + 1;
    }
#ifdef DUMP_TABLE_SIZE
    printf("script_ext: %d entries", cw_count);
    printf(", length=%d bytes\n", (int)dbuf->size);
#endif

    dump_byte_table(f, "unicode_script_ext_table", dbuf->buf, dbuf->size);

    dbuf_free(dbuf);
}

/* the following properties are synthetized so no table is necessary */
#define PROP_TABLE_COUNT PROP_ASCII

void build_prop_list_table(FILE *f)
{
    int i;

    for(i = 0; i < PROP_TABLE_COUNT; i++) {
        if (i == PROP_ID_Start ||
            i == PROP_Case_Ignorable ||
            i == PROP_ID_Continue1) {
            /* already generated */
        } else {
            build_prop_table(f, unicode_prop_name[i], i, FALSE);
        }
    }

    fprintf(f, "typedef enum {\n");
    for(i = 0; i < PROP_COUNT; i++)
        fprintf(f, "    UNICODE_PROP_%s,\n", unicode_prop_name[i]);
    fprintf(f, "    UNICODE_PROP_COUNT,\n");
    fprintf(f, "} UnicodePropertyEnum;\n\n");

    i = PROP_ASCII_Hex_Digit;
    dump_name_table(f, "unicode_prop_name_table",
                    unicode_prop_name + i, PROP_XID_Start - i + 1,
                    unicode_prop_short_name + i);

    fprintf(f, "static const uint8_t * const unicode_prop_table[] = {\n");
    for(i = 0; i < PROP_TABLE_COUNT; i++) {
        fprintf(f, "    unicode_prop_%s_table,\n", unicode_prop_name[i]);
    }
    fprintf(f, "};\n\n");

    fprintf(f, "static const uint16_t unicode_prop_len_table[] = {\n");
    for(i = 0; i < PROP_TABLE_COUNT; i++) {
        fprintf(f, "    countof(unicode_prop_%s_table),\n", unicode_prop_name[i]);
    }
    fprintf(f, "};\n\n");
}

static BOOL is_emoji_hair_color(uint32_t c)
{
    return (c >= 0x1F9B0 && c <= 0x1F9B3);
}

#define EMOJI_MOD_NONE   0
#define EMOJI_MOD_TYPE1  1
#define EMOJI_MOD_TYPE2  2
#define EMOJI_MOD_TYPE2D 3

static BOOL mark_zwj_string(REStringList *sl, uint32_t *buf, int len, int mod_type, int *mod_pos,
                            int hc_pos, BOOL mark_flag)
{
    REString *p;
    int i, n_mod, i0, i1, hc_count, j;

#if 0
    if (mark_flag)
        printf("mod_type=%d\n", mod_type);
#endif
    
    switch(mod_type) {
    case EMOJI_MOD_NONE:
        n_mod = 1;
        break;
    case EMOJI_MOD_TYPE1:
        n_mod = 5;
        break;
    case EMOJI_MOD_TYPE2:
        n_mod = 25;
        break;
    case EMOJI_MOD_TYPE2D:
        n_mod = 20;
        break;
    default:
        assert(0);
    }
    if (hc_pos >= 0)
        hc_count = 4;
    else
        hc_count = 1;
    /* check that all the related strings are present */
    for(j = 0; j < hc_count; j++) {
        for(i = 0; i < n_mod; i++) {
            switch(mod_type) {
            case EMOJI_MOD_NONE:
                break;
            case EMOJI_MOD_TYPE1:
                buf[mod_pos[0]] = 0x1f3fb + i;
                break;
            case EMOJI_MOD_TYPE2:
            case EMOJI_MOD_TYPE2D:
                i0 = i / 5;
                i1 = i % 5;
                /* avoid identical values */
                if (mod_type == EMOJI_MOD_TYPE2D && i0 >= i1)
                    i0++;
                buf[mod_pos[0]] = 0x1f3fb + i0;
                buf[mod_pos[1]] = 0x1f3fb + i1;
                break;
            default:
                assert(0);
            }

            if (hc_pos >= 0)
                buf[hc_pos] = 0x1F9B0 + j;
            
            p = re_string_find(sl, len, buf, FALSE);
            if (!p)
                return FALSE;
            if (mark_flag)
                p->flags |= 1;
        }
    }
    return TRUE;
}

static void zwj_encode_string(DynBuf *dbuf, const uint32_t *buf, int len, int mod_type, int *mod_pos,
                              int hc_pos)
{
    int i, j;
    int c, code;
    uint32_t buf1[SEQ_MAX_LEN];
    
    j = 0;
    for(i = 0; i < len;) {
        c = buf[i++];
        if (c >= 0x2000 && c <= 0x2fff) {
            code = c - 0x2000;
        } else if (c >= 0x1f000 && c <= 0x1ffff) {
            code = c - 0x1f000 + 0x1000;
        } else {
            assert(0);
        }
        if (i < len && is_emoji_modifier(buf[i])) {
            /* modifier */
            code |= (mod_type << 13);
            i++;
        }
        if (i < len && buf[i] == 0xfe0f) {
            /* presentation selector present */
            code |= 0x8000;
            i++;
        }
        if (i < len) {
            /* zero width join */
            assert(buf[i] == 0x200d);
            i++;
        }
        buf1[j++] = code;
    }
    dbuf_putc(dbuf, j);
    for(i = 0; i < j; i++) {
        dbuf_putc(dbuf, buf1[i]);
        dbuf_putc(dbuf, buf1[i] >> 8);
    }
}

static void build_rgi_emoji_zwj_sequence(FILE *f, REStringList *sl)
{
    int mod_pos[2], mod_count, hair_color_pos, j, h;
    REString *p;
    uint32_t buf[SEQ_MAX_LEN];
    DynBuf dbuf;

#if 0
    {
        for(h = 0; h < sl->hash_size; h++) {
            for(p = sl->hash_table[h]; p != NULL; p = p->next) {
                for(j = 0; j < p->len; j++)
                    printf(" %04x", p->buf[j]);
                printf("\n");
            }
        }
        exit(0);
    }
#endif
    //    printf("rgi_emoji_zwj_sequence: n=%d\n", sl->n_strings);

    dbuf_init(&dbuf);
    
    /* avoid duplicating strings with emoji modifiers or hair colors */
    for(h = 0; h < sl->hash_size; h++) {
        for(p = sl->hash_table[h]; p != NULL; p = p->next) {
            if (p->flags) /* already examined */
                continue;
            mod_count = 0;
            hair_color_pos = -1;
            for(j = 0; j < p->len; j++) {
                if (is_emoji_modifier(p->buf[j])) {
                    assert(mod_count < 2);
                    mod_pos[mod_count++] = j;
                } else if (is_emoji_hair_color(p->buf[j])) {
                    hair_color_pos = j;
                }
                buf[j] = p->buf[j];
            }
            
            if (mod_count != 0 || hair_color_pos >= 0) {
                int mod_type;
                if (mod_count == 0)
                    mod_type = EMOJI_MOD_NONE;
                else if (mod_count == 1)
                    mod_type = EMOJI_MOD_TYPE1;
                else
                    mod_type = EMOJI_MOD_TYPE2;
                
                if (mark_zwj_string(sl, buf, p->len, mod_type, mod_pos, hair_color_pos, FALSE)) {
                    mark_zwj_string(sl, buf, p->len, mod_type, mod_pos, hair_color_pos, TRUE);
                } else if (mod_type == EMOJI_MOD_TYPE2) {
                    mod_type = EMOJI_MOD_TYPE2D;
                    if (mark_zwj_string(sl, buf, p->len, mod_type, mod_pos, hair_color_pos, FALSE)) {
                        mark_zwj_string(sl, buf, p->len, mod_type, mod_pos, hair_color_pos, TRUE);
                    } else {
                        dump_str("not_found", (int *)p->buf, p->len);
                        goto keep;
                    }
                }
                if (hair_color_pos >= 0)
                    buf[hair_color_pos] = 0x1f9b0;
                /* encode the string */
                zwj_encode_string(&dbuf, buf, p->len, mod_type, mod_pos, hair_color_pos);
            } else {
            keep:
                zwj_encode_string(&dbuf, buf, p->len, EMOJI_MOD_NONE, NULL, -1);
            }
        }
    }
    
    /* Encode */
    dump_byte_table(f, "unicode_rgi_emoji_zwj_sequence", dbuf.buf, dbuf.size);

    dbuf_free(&dbuf);
}

void build_sequence_prop_list_table(FILE *f)
{
    int i;
    fprintf(f, "typedef enum {\n");
    for(i = 0; i < SEQUENCE_PROP_COUNT; i++)
        fprintf(f, "    UNICODE_SEQUENCE_PROP_%s,\n", unicode_sequence_prop_name[i]);
    fprintf(f, "    UNICODE_SEQUENCE_PROP_COUNT,\n");
    fprintf(f, "} UnicodeSequencePropertyEnum;\n\n");

    dump_name_table(f, "unicode_sequence_prop_name_table",
                    unicode_sequence_prop_name, SEQUENCE_PROP_COUNT, NULL);

    dump_byte_table(f, "unicode_rgi_emoji_tag_sequence", rgi_emoji_tag_sequence.buf, rgi_emoji_tag_sequence.size);

    build_rgi_emoji_zwj_sequence(f, &rgi_emoji_zwj_sequence);
}

#ifdef USE_TEST
int check_conv(uint32_t *res, uint32_t c, int conv_type)
{
    return lre_case_conv(res, c, conv_type);
}

void check_case_conv(void)
{
    CCInfo *tab = unicode_db;
    uint32_t res[3];
    int l, error;
    CCInfo ci_s, *ci1, *ci = &ci_s;
    int code;

    for(code = 0; code <= CHARCODE_MAX; code++) {
        ci1 = &tab[code];
        *ci = *ci1;
        if (ci->l_len == 0) {
            ci->l_len = 1;
            ci->l_data[0] = code;
        }
        if (ci->u_len == 0) {
            ci->u_len = 1;
            ci->u_data[0] = code;
        }
        if (ci->f_len == 0) {
            ci->f_len = 1;
            ci->f_data[0] = code;
        }

        error = 0;
        l = check_conv(res, code, 0);
        if (l != ci->u_len || tabcmp((int *)res, ci->u_data, l)) {
            printf("ERROR: L\n");
            error++;
        }
        l = check_conv(res, code, 1);
        if (l != ci->l_len || tabcmp((int *)res, ci->l_data, l)) {
            printf("ERROR: U\n");
            error++;
        }
        l = check_conv(res, code, 2);
        if (l != ci->f_len || tabcmp((int *)res, ci->f_data, l)) {
            printf("ERROR: F\n");
            error++;
        }
        if (error) {
            dump_cc_info(ci, code);
            exit(1);
        }
    }
}

#ifdef PROFILE
static int64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
#endif


void check_flags(void)
{
    int c;
    BOOL flag_ref, flag;
    for(c = 0; c <= CHARCODE_MAX; c++) {
        flag_ref = get_prop(c, PROP_Cased);
        flag = !!lre_is_cased(c);
        if (flag != flag_ref) {
            printf("ERROR: c=%05x cased=%d ref=%d\n",
                   c, flag, flag_ref);
            exit(1);
        }

        flag_ref = get_prop(c, PROP_Case_Ignorable);
        flag = !!lre_is_case_ignorable(c);
        if (flag != flag_ref) {
            printf("ERROR: c=%05x case_ignorable=%d ref=%d\n",
                   c, flag, flag_ref);
            exit(1);
        }

        flag_ref = get_prop(c, PROP_ID_Start);
        flag = !!lre_is_id_start(c);
        if (flag != flag_ref) {
            printf("ERROR: c=%05x id_start=%d ref=%d\n",
                   c, flag, flag_ref);
            exit(1);
        }

        flag_ref = get_prop(c, PROP_ID_Continue);
        flag = !!lre_is_id_continue(c);
        if (flag != flag_ref) {
            printf("ERROR: c=%05x id_cont=%d ref=%d\n",
                   c, flag, flag_ref);
            exit(1);
        }
    }
#ifdef PROFILE
    {
        int64_t ti, count;
        ti = get_time_ns();
        count = 0;
        for(c = 0x20; c <= 0xffff; c++) {
            flag_ref = get_prop(c, PROP_ID_Start);
            flag = !!lre_is_id_start(c);
            assert(flag == flag_ref);
            count++;
        }
        ti = get_time_ns() - ti;
        printf("flags time=%0.1f ns/char\n",
               (double)ti / count);
    }
#endif
}

#endif

#define CC_BLOCK_LEN 32

void build_cc_table(FILE *f)
{
    // Compress combining class table
    // see: https://www.unicode.org/reports/tr44/#Canonical_Combining_Class_Values
    int i, cc, n, type, n1, block_end_pos;
    DynBuf dbuf_s, *dbuf = &dbuf_s;
    DynBuf dbuf1_s, *dbuf1 = &dbuf1_s;
#if defined(DUMP_CC_TABLE) || defined(DUMP_TABLE_SIZE)
    int cw_len_tab[3], cw_start, cc_table_len;
#endif
    uint32_t v;

    dbuf_init(dbuf);
    dbuf_init(dbuf1);
#if defined(DUMP_CC_TABLE) || defined(DUMP_TABLE_SIZE)
    cc_table_len = 0;
    for(i = 0; i < countof(cw_len_tab); i++)
        cw_len_tab[i] = 0;
#endif
    block_end_pos = CC_BLOCK_LEN;
    for(i = 0; i <= CHARCODE_MAX;) {
        cc = unicode_db[i].combining_class;
        assert(cc <= 255);
        /* check increasing values */
        n = 1;
        while ((i + n) <= CHARCODE_MAX &&
               unicode_db[i + n].combining_class == (cc + n))
            n++;
        if (n >= 2) {
            type = 1;
        } else {
            type = 0;
            n = 1;
            while ((i + n) <= CHARCODE_MAX &&
                   unicode_db[i + n].combining_class == cc)
                n++;
        }
        /* no need to encode the last run */
        if (cc == 0 && (i + n - 1) == CHARCODE_MAX)
            break;
#ifdef DUMP_CC_TABLE
        printf("%05x %6d %d %d\n", i, n, type, cc);
#endif
        if (type == 0) {
            if (cc == 0)
                type = 2;
            else if (cc == 230)
                type = 3;
        }
        n1 = n - 1;

        /* add an entry to the index if necessary */
        if (dbuf->size >= block_end_pos) {
            v = i | ((dbuf->size - block_end_pos) << 21);
            dbuf_putc(dbuf1, v);
            dbuf_putc(dbuf1, v >> 8);
            dbuf_putc(dbuf1, v >> 16);
            block_end_pos += CC_BLOCK_LEN;
        }
#if defined(DUMP_CC_TABLE) || defined(DUMP_TABLE_SIZE)
        cw_start = dbuf->size;
#endif
        /* Compressed run length encoding:
           - 2 high order bits are combining class type
           -         0:0, 1:230, 2:extra byte linear progression, 3:extra byte
           - 00..2F: range length (add 1)
           - 30..37: 3-bit range-length + 1 extra byte
           - 38..3F: 3-bit range-length + 2 extra byte
         */
        if (n1 < 48) {
            dbuf_putc(dbuf, n1 | (type << 6));
        } else if (n1 < 48 + (1 << 11)) {
            n1 -= 48;
            dbuf_putc(dbuf, ((n1 >> 8) + 48) | (type << 6));
            dbuf_putc(dbuf, n1);
        } else {
            n1 -= 48 + (1 << 11);
            assert(n1 < (1 << 20));
            dbuf_putc(dbuf, ((n1 >> 16) + 56) | (type << 6));
            dbuf_putc(dbuf, n1 >> 8);
            dbuf_putc(dbuf, n1);
        }
#if defined(DUMP_CC_TABLE) || defined(DUMP_TABLE_SIZE)
        cw_len_tab[dbuf->size - cw_start - 1]++;
        cc_table_len++;
#endif
        if (type == 0 || type == 1)
            dbuf_putc(dbuf, cc);
        i += n;
    }

    /* last index entry */
    v = i;
    dbuf_putc(dbuf1, v);
    dbuf_putc(dbuf1, v >> 8);
    dbuf_putc(dbuf1, v >> 16);

    dump_byte_table(f, "unicode_cc_table", dbuf->buf, dbuf->size);
    dump_index_table(f, "unicode_cc_index", dbuf1->buf, dbuf1->size);

#if defined(DUMP_CC_TABLE) || defined(DUMP_TABLE_SIZE)
    printf("CC table: size=%d (%d entries) [",
           (int)(dbuf->size + dbuf1->size),
           cc_table_len);
    for(i = 0; i < countof(cw_len_tab); i++)
        printf(" %d", cw_len_tab[i]);
    printf(" ]\n");
#endif
    dbuf_free(dbuf);
    dbuf_free(dbuf1);
}

/* maximum length of decomposition: 18 chars (1), then 8 */
#ifndef USE_TEST
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
#endif

const char *decomp_type_str[] = {
    "C1",
    "L1",
    "L2",
    "L3",
    "L4",
    "L5",
    "L6",
    "L7",
    "LL1",
    "LL2",
    "S1",
    "S2",
    "S3",
    "S4",
    "S5",
    "I1",
    "I2_0",
    "I2_1",
    "I3_1",
    "I3_2",
    "I4_1",
    "I4_2",
    "B1",
    "B2",
    "B3",
    "B4",
    "B5",
    "B6",
    "B7",
    "B8",
    "B18",
    "LS2",
    "PAT3",
    "S2_UL",
    "LS2_UL",
};

const int decomp_incr_tab[4][4] = {
    { DECOMP_TYPE_I1, 0, -1 },
    { DECOMP_TYPE_I2_0, 0, 1, -1 },
    { DECOMP_TYPE_I3_1, 1, 2, -1 },
    { DECOMP_TYPE_I4_1, 1, 2, -1 },
};

/*
  entry size:
  type   bits
  code   18
  len    7
  compat 1
  type   5
  index  16
  total  47
*/

typedef struct {
    int code;
    uint8_t len;
    uint8_t type;
    uint8_t c_len;
    uint16_t c_min;
    uint16_t data_index;
    int cost; /* size in bytes from this entry to the end */
} DecompEntry;

int get_decomp_run_size(const DecompEntry *de)
{
    int s;
    s = 6;
    if (de->type <= DECOMP_TYPE_C1) {
        /* nothing more */
    } else if (de->type <= DECOMP_TYPE_L7) {
        s += de->len * de->c_len * 2;
    } else if (de->type <= DECOMP_TYPE_LL2) {
        /* 18 bits per char */
        s += (de->len * de->c_len * 18 + 7) / 8;
    } else if (de->type <= DECOMP_TYPE_S5) {
        s += de->len * de->c_len;
    } else if (de->type <= DECOMP_TYPE_I4_2) {
        s += de->c_len * 2;
    } else if (de->type <= DECOMP_TYPE_B18) {
        s += 2 + de->len * de->c_len;
    } else if (de->type <= DECOMP_TYPE_LS2) {
        s += de->len * 3;
    } else if (de->type <= DECOMP_TYPE_PAT3) {
        s += 4 + de->len * 2;
    } else if (de->type <= DECOMP_TYPE_S2_UL) {
        s += de->len;
    } else if (de->type <= DECOMP_TYPE_LS2_UL) {
        s += (de->len / 2) * 3;
    } else {
        abort();
    }
    return s;
}

static const uint16_t unicode_short_table[2] = { 0x2044, 0x2215 };

/* return -1 if not found */
int get_short_code(int c)
{
    int i;
    if (c < 0x80) {
        return c;
    } else if (c >= 0x300 && c < 0x350) {
        return c - 0x300 + 0x80;
    } else {
        for(i = 0; i < countof(unicode_short_table); i++) {
            if (c == unicode_short_table[i])
                return i + 0x80 + 0x50;
        }
        return -1;
    }
}

static BOOL is_short(int code)
{
    return get_short_code(code) >= 0;
}

static BOOL is_short_tab(const int *tab, int len)
{
    int i;
    for(i = 0; i < len; i++) {
        if (!is_short(tab[i]))
            return FALSE;
    }
    return TRUE;
}

static BOOL is_16bit(const int *tab, int len)
{
    int i;
    for(i = 0; i < len; i++) {
        if (tab[i] > 0xffff)
            return FALSE;
    }
    return TRUE;
}

static uint32_t to_lower_simple(uint32_t c)
{
    /* Latin1 and Cyrillic */
    if (c < 0x100 || (c >= 0x410 && c <= 0x42f))
        c += 0x20;
    else
        c++;
    return c;
}

/* select best encoding with dynamic programming */
void find_decomp_run(DecompEntry *tab_de, int i)
{
    DecompEntry de_s, *de = &de_s;
    CCInfo *ci, *ci1, *ci2;
    int l, j, n, len_max;

    ci = &unicode_db[i];
    l = ci->decomp_len;
    if (l == 0) {
        tab_de[i].cost = tab_de[i + 1].cost;
        return;
    }

    /* the offset for the compose table has only 6 bits, so we must
       limit if it can be used by the compose table */
    if (!ci->is_compat && !ci->is_excluded && l == 2)
        len_max = 64;
    else
        len_max = 127;

    tab_de[i].cost = 0x7fffffff;

    if (!is_16bit(ci->decomp_data, l)) {
        assert(l <= 2);

        n = 1;
        for(;;) {
            de->code = i;
            de->len = n;
            de->type = DECOMP_TYPE_LL1 + l - 1;
            de->c_len = l;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }
            if (!((i + n) <= CHARCODE_MAX && n < len_max))
                break;
            ci1 = &unicode_db[i + n];
            /* Note: we accept a hole */
            if (!(ci1->decomp_len == 0 ||
                  (ci1->decomp_len == l &&
                   ci1->is_compat == ci->is_compat)))
                break;
            n++;
        }
        return;
    }

    if (l <= 7) {
        n = 1;
        for(;;) {
            de->code = i;
            de->len = n;
            if (l == 1 && n == 1) {
                de->type = DECOMP_TYPE_C1;
            } else {
                assert(l <= 8);
                de->type = DECOMP_TYPE_L1 + l - 1;
            }
            de->c_len = l;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }

            if (!((i + n) <= CHARCODE_MAX && n < len_max))
                break;
            ci1 = &unicode_db[i + n];
            /* Note: we accept a hole */
            if (!(ci1->decomp_len == 0 ||
                  (ci1->decomp_len == l &&
                   ci1->is_compat == ci->is_compat &&
                   is_16bit(ci1->decomp_data, l))))
                break;
            n++;
        }
    }

    if (l <= 8 || l == 18) {
        int c_min, c_max, c;
        c_min = c_max = -1;
        n = 1;
        for(;;) {
            ci1 = &unicode_db[i + n - 1];
            for(j = 0; j < l; j++) {
                c = ci1->decomp_data[j];
                if (c == 0x20) {
                    /* we accept space for Arabic */
                } else if (c_min == -1) {
                    c_min = c_max = c;
                } else {
                    c_min = min_int(c_min, c);
                    c_max = max_int(c_max, c);
                }
            }
            if ((c_max - c_min) > 254)
                break;
            de->code = i;
            de->len = n;
            if (l == 18)
                de->type = DECOMP_TYPE_B18;
            else
                de->type = DECOMP_TYPE_B1 + l - 1;
            de->c_len = l;
            de->c_min = c_min;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }
            if (!((i + n) <= CHARCODE_MAX && n < len_max))
                break;
            ci1 = &unicode_db[i + n];
            if (!(ci1->decomp_len == l &&
                  ci1->is_compat == ci->is_compat))
                break;
            n++;
        }
    }

    /* find an ascii run */
    if (l <= 5 && is_short_tab(ci->decomp_data, l)) {
        n = 1;
        for(;;) {
            de->code = i;
            de->len = n;
            de->type = DECOMP_TYPE_S1 + l - 1;
            de->c_len = l;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }

            if (!((i + n) <= CHARCODE_MAX && n < len_max))
                break;
            ci1 = &unicode_db[i + n];
            /* Note: we accept a hole */
            if (!(ci1->decomp_len == 0 ||
                  (ci1->decomp_len == l &&
                   ci1->is_compat == ci->is_compat &&
                   is_short_tab(ci1->decomp_data, l))))
                break;
            n++;
        }
    }

    /* check if a single char is increasing */
    if (l <= 4) {
        int idx1, idx;

        for(idx1 = 1; (idx = decomp_incr_tab[l - 1][idx1]) >= 0; idx1++) {
            n = 1;
            for(;;) {
                de->code = i;
                de->len = n;
                de->type = decomp_incr_tab[l - 1][0] + idx1 - 1;
                de->c_len = l;
                de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
                if (de->cost < tab_de[i].cost) {
                    tab_de[i] = *de;
                }

                if (!((i + n) <= CHARCODE_MAX && n < len_max))
                    break;
                ci1 = &unicode_db[i + n];
                if (!(ci1->decomp_len == l &&
                      ci1->is_compat == ci->is_compat))
                    goto next1;
                for(j = 0; j < l; j++) {
                    if (j == idx) {
                        if (ci1->decomp_data[j] != ci->decomp_data[j] + n)
                            goto next1;
                    } else {
                        if (ci1->decomp_data[j] != ci->decomp_data[j])
                            goto next1;
                    }
                }
                n++;
            }
        next1: ;
        }
    }

    if (l == 3) {
        n = 1;
        for(;;) {
            de->code = i;
            de->len = n;
            de->type = DECOMP_TYPE_PAT3;
            de->c_len = l;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }
            if (!((i + n) <= CHARCODE_MAX && n < len_max))
                break;
            ci1 = &unicode_db[i + n];
            if (!(ci1->decomp_len == l &&
                  ci1->is_compat == ci->is_compat &&
                  ci1->decomp_data[1] <= 0xffff &&
                  ci1->decomp_data[0] == ci->decomp_data[0] &&
                  ci1->decomp_data[l - 1] == ci->decomp_data[l - 1]))
                break;
            n++;
        }
    }

    if (l == 2 && is_short(ci->decomp_data[1])) {
        n = 1;
        for(;;) {
            de->code = i;
            de->len = n;
            de->type = DECOMP_TYPE_LS2;
            de->c_len = l;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }
            if (!((i + n) <= CHARCODE_MAX && n < len_max))
                break;
            ci1 = &unicode_db[i + n];
            if (!(ci1->decomp_len == 0 ||
                  (ci1->decomp_len == l &&
                   ci1->is_compat == ci->is_compat &&
                   ci1->decomp_data[0] <= 0xffff &&
                   is_short(ci1->decomp_data[1]))))
                break;
            n++;
        }
    }

    if (l == 2) {
        BOOL is_16bit;

        n = 0;
        is_16bit = FALSE;
        for(;;) {
            if (!((i + n + 1) <= CHARCODE_MAX && n + 2 <= len_max))
                break;
            ci1 = &unicode_db[i + n];
            if (!(ci1->decomp_len == l &&
                  ci1->is_compat == ci->is_compat &&
                  is_short(ci1->decomp_data[1])))
                break;
            if (!is_16bit && !is_short(ci1->decomp_data[0]))
                is_16bit = TRUE;
            ci2 = &unicode_db[i + n + 1];
            if (!(ci2->decomp_len == l &&
                  ci2->is_compat == ci->is_compat &&
                  ci2->decomp_data[0] == to_lower_simple(ci1->decomp_data[0])  &&
                  ci2->decomp_data[1] == ci1->decomp_data[1]))
                break;
            n += 2;
            de->code = i;
            de->len = n;
            de->type = DECOMP_TYPE_S2_UL + is_16bit;
            de->c_len = l;
            de->cost = get_decomp_run_size(de) + tab_de[i + n].cost;
            if (de->cost < tab_de[i].cost) {
                tab_de[i] = *de;
            }
        }
    }
}

void put16(uint8_t *data_buf, int *pidx, uint16_t c)
{
    int idx;
    idx = *pidx;
    data_buf[idx++] = c;
    data_buf[idx++] = c >> 8;
    *pidx = idx;
}

void add_decomp_data(uint8_t *data_buf, int *pidx, DecompEntry *de)
{
    int i, j, idx, c;
    CCInfo *ci;

    idx = *pidx;
    de->data_index = idx;
    if (de->type <= DECOMP_TYPE_C1) {
        ci = &unicode_db[de->code];
        assert(ci->decomp_len == 1);
        de->data_index = ci->decomp_data[0];
    } else if (de->type <= DECOMP_TYPE_L7) {
        for(i = 0; i < de->len; i++) {
            ci = &unicode_db[de->code + i];
            for(j = 0; j < de->c_len; j++) {
                if (ci->decomp_len == 0)
                    c = 0;
                else
                    c = ci->decomp_data[j];
                put16(data_buf, &idx,  c);
            }
        }
    } else if (de->type <= DECOMP_TYPE_LL2) {
        int n, p, k;
        n = (de->len * de->c_len * 18 + 7) / 8;
        p = de->len * de->c_len * 2;
        memset(data_buf + idx, 0, n);
        k = 0;
        for(i = 0; i < de->len; i++) {
            ci = &unicode_db[de->code + i];
            for(j = 0; j < de->c_len; j++) {
                if (ci->decomp_len == 0)
                    c = 0;
                else
                    c = ci->decomp_data[j];
                data_buf[idx + k * 2] = c;
                data_buf[idx + k * 2 + 1] = c >> 8;
                data_buf[idx + p + (k / 4)] |= (c >> 16) << ((k % 4) * 2);
                k++;
            }
        }
        idx += n;
    } else if (de->type <= DECOMP_TYPE_S5) {
        for(i = 0; i < de->len; i++) {
            ci = &unicode_db[de->code + i];
            for(j = 0; j < de->c_len; j++) {
                if (ci->decomp_len == 0)
                    c = 0;
                else
                    c = ci->decomp_data[j];
                c = get_short_code(c);
                assert(c >= 0);
                data_buf[idx++] = c;
            }
        }
    } else if (de->type <= DECOMP_TYPE_I4_2) {
        ci = &unicode_db[de->code];
        assert(ci->decomp_len == de->c_len);
        for(j = 0; j < de->c_len; j++)
            put16(data_buf, &idx, ci->decomp_data[j]);
    } else if (de->type <= DECOMP_TYPE_B18) {
        c = de->c_min;
        data_buf[idx++] = c;
        data_buf[idx++] = c >> 8;
        for(i = 0; i < de->len; i++) {
            ci = &unicode_db[de->code + i];
            for(j = 0; j < de->c_len; j++) {
                assert(ci->decomp_len == de->c_len);
                c = ci->decomp_data[j];
                if (c == 0x20) {
                    c = 0xff;
                } else {
                    c -= de->c_min;
                    assert((uint32_t)c <= 254);
                }
                data_buf[idx++] = c;
            }
        }
    } else if (de->type <= DECOMP_TYPE_LS2) {
        assert(de->c_len == 2);
        for(i = 0; i < de->len; i++) {
            ci = &unicode_db[de->code + i];
            if (ci->decomp_len == 0)
                c = 0;
            else
                c = ci->decomp_data[0];
            put16(data_buf, &idx,  c);

            if (ci->decomp_len == 0)
                c = 0;
            else
                c = ci->decomp_data[1];
            c = get_short_code(c);
            assert(c >= 0);
            data_buf[idx++] = c;
        }
    } else if (de->type <= DECOMP_TYPE_PAT3) {
        ci = &unicode_db[de->code];
        assert(ci->decomp_len == 3);
        put16(data_buf, &idx,  ci->decomp_data[0]);
        put16(data_buf, &idx,  ci->decomp_data[2]);
        for(i = 0; i < de->len; i++) {
            ci = &unicode_db[de->code + i];
            assert(ci->decomp_len == 3);
            put16(data_buf, &idx,  ci->decomp_data[1]);
        }
    } else if (de->type <= DECOMP_TYPE_S2_UL) {
        for(i = 0; i < de->len; i += 2) {
            ci = &unicode_db[de->code + i];
            c = ci->decomp_data[0];
            c = get_short_code(c);
            assert(c >= 0);
            data_buf[idx++] = c;
            c = ci->decomp_data[1];
            c = get_short_code(c);
            assert(c >= 0);
            data_buf[idx++] = c;
        }
    } else if (de->type <= DECOMP_TYPE_LS2_UL) {
        for(i = 0; i < de->len; i += 2) {
            ci = &unicode_db[de->code + i];
            c = ci->decomp_data[0];
            put16(data_buf, &idx,  c);
            c = ci->decomp_data[1];
            c = get_short_code(c);
            assert(c >= 0);
            data_buf[idx++] = c;
        }
    } else {
        abort();
    }
    *pidx = idx;
}

#if 0
void dump_large_char(void)
{
    int i, j;
    for(i = 0; i <= CHARCODE_MAX; i++) {
        CCInfo *ci = &unicode_db[i];
        for(j = 0; j < ci->decomp_len; j++) {
            if (ci->decomp_data[j] > 0xffff)
                printf("%05x\n", ci->decomp_data[j]);
        }
    }
}
#endif

void build_compose_table(FILE *f, const DecompEntry *tab_de);

void build_decompose_table(FILE *f)
{
    int i, array_len, code_max, data_len, count;
    DecompEntry *tab_de, de_s, *de = &de_s;
    uint8_t *data_buf;

    code_max = CHARCODE_MAX;

    tab_de = mallocz((code_max + 2) * sizeof(*tab_de));

    for(i = code_max; i >= 0; i--) {
        find_decomp_run(tab_de, i);
    }

    /* build the data buffer */
    data_buf = malloc(100000);
    data_len = 0;
    array_len = 0;
    for(i = 0; i <= code_max; i++) {
        de = &tab_de[i];
        if (de->len != 0) {
            add_decomp_data(data_buf, &data_len, de);
            i += de->len - 1;
            array_len++;
        }
    }

#ifdef DUMP_DECOMP_TABLE
    /* dump */
    {
        int size, size1;

        printf("START LEN   TYPE  L C SIZE\n");
        size = 0;
        for(i = 0; i <= code_max; i++) {
            de = &tab_de[i];
            if (de->len != 0) {
                size1 = get_decomp_run_size(de);
                printf("%05x %3d %6s %2d %1d %4d\n", i, de->len,
                       decomp_type_str[de->type], de->c_len,
                       unicode_db[i].is_compat, size1);
                i += de->len - 1;
                size += size1;
            }
        }

        printf("array_len=%d estimated size=%d bytes actual=%d bytes\n",
               array_len, size, array_len * 6 + data_len);
    }
#endif

    total_tables++;
    total_table_bytes += array_len * sizeof(uint32_t);
    fprintf(f, "static const uint32_t unicode_decomp_table1[%d] = {", array_len);
    count = 0;
    for(i = 0; i <= code_max; i++) {
        de = &tab_de[i];
        if (de->len != 0) {
            uint32_t v;
            if (count++ % 4 == 0)
                fprintf(f, "\n   ");
            v = (de->code << (32 - 18)) |
                (de->len << (32 - 18 - 7)) |
                (de->type << (32 - 18 - 7 - 6)) |
                unicode_db[de->code].is_compat;
            fprintf(f, " 0x%08x,", v);
            i += de->len - 1;
        }
    }
    fprintf(f, "\n};\n\n");

    total_tables++;
    total_table_bytes += array_len * sizeof(uint16_t);
    fprintf(f, "static const uint16_t unicode_decomp_table2[%d] = {", array_len);
    count = 0;
    for(i = 0; i <= code_max; i++) {
        de = &tab_de[i];
        if (de->len != 0) {
            if (count++ % 8 == 0)
                fprintf(f, "\n   ");
            fprintf(f, " 0x%04x,", de->data_index);
            i += de->len - 1;
        }
    }
    fprintf(f, "\n};\n\n");

    total_tables++;
    total_table_bytes += data_len;
    fprintf(f, "static const uint8_t unicode_decomp_data[%d] = {", data_len);
    for(i = 0; i < data_len; i++) {
        if (i % 8 == 0)
            fprintf(f, "\n   ");
        fprintf(f, " 0x%02x,", data_buf[i]);
    }
    fprintf(f, "\n};\n\n");

    build_compose_table(f, tab_de);

    free(data_buf);

    free(tab_de);
}

typedef struct {
    uint32_t c[2];
    uint32_t p;
} ComposeEntry;

#define COMPOSE_LEN_MAX 10000

static int ce_cmp(const void *p1, const void *p2)
{
    const ComposeEntry *ce1 = p1;
    const ComposeEntry *ce2 = p2;
    int i;

    for(i = 0; i < 2; i++) {
        if (ce1->c[i] < ce2->c[i])
            return -1;
        else if (ce1->c[i] > ce2->c[i])
            return 1;
    }
    return 0;
}


static int get_decomp_pos(const DecompEntry *tab_de, int c)
{
    int i, v, k;
    const DecompEntry *de;

    k = 0;
    for(i = 0; i <= CHARCODE_MAX; i++) {
        de = &tab_de[i];
        if (de->len != 0) {
            if (c >= de->code && c < de->code + de->len) {
                v = c - de->code;
                assert(v < 64);
                v |= k << 6;
                assert(v < 65536);
                return v;
            }
            i += de->len - 1;
            k++;
        }
    }
    return -1;
}

void build_compose_table(FILE *f, const DecompEntry *tab_de)
{
    int i, v, tab_ce_len;
    ComposeEntry *ce, *tab_ce;

    tab_ce = malloc(sizeof(*tab_ce) * COMPOSE_LEN_MAX);
    tab_ce_len = 0;
    for(i = 0; i <= CHARCODE_MAX; i++) {
        CCInfo *ci = &unicode_db[i];
        if (ci->decomp_len == 2 && !ci->is_compat &&
            !ci->is_excluded) {
            assert(tab_ce_len < COMPOSE_LEN_MAX);
            ce = &tab_ce[tab_ce_len++];
            ce->c[0] = ci->decomp_data[0];
            ce->c[1] = ci->decomp_data[1];
            ce->p = i;
        }
    }
    qsort(tab_ce, tab_ce_len, sizeof(*tab_ce), ce_cmp);

#if 0
    {
        printf("tab_ce_len=%d\n", tab_ce_len);
        for(i = 0; i < tab_ce_len; i++) {
            ce = &tab_ce[i];
            printf("%05x %05x %05x\n", ce->c[0], ce->c[1], ce->p);
        }
    }
#endif

    total_tables++;
    total_table_bytes += tab_ce_len * sizeof(uint16_t);
    fprintf(f, "static const uint16_t unicode_comp_table[%u] = {", tab_ce_len);
    for(i = 0; i < tab_ce_len; i++) {
        if (i % 8 == 0)
            fprintf(f, "\n   ");
        v = get_decomp_pos(tab_de, tab_ce[i].p);
        if (v < 0) {
            printf("ERROR: entry for c=%04x not found\n",
                   tab_ce[i].p);
            exit(1);
        }
        fprintf(f, " 0x%04x,", v);
    }
    fprintf(f, "\n};\n\n");

    free(tab_ce);
}

#ifdef USE_TEST
void check_decompose_table(void)
{
    int c;
    CCInfo *ci;
    int res[UNICODE_DECOMP_LEN_MAX], *ref;
    int len, ref_len, is_compat;

    for(is_compat = 0; is_compat <= 1; is_compat++) {
        for(c = 0; c < CHARCODE_MAX; c++) {
            ci = &unicode_db[c];
            ref_len = ci->decomp_len;
            ref = ci->decomp_data;
            if (!is_compat && ci->is_compat) {
                ref_len = 0;
            }
            len = unicode_decomp_char((uint32_t *)res, c, is_compat);
            if (len != ref_len ||
                tabcmp(res, ref, ref_len) != 0) {
                printf("ERROR c=%05x compat=%d\n", c, is_compat);
                dump_str("res", res, len);
                dump_str("ref", ref, ref_len);
                exit(1);
            }
        }
    }
}

void check_compose_table(void)
{
    int i, p;
    /* XXX: we don't test all the cases */

    for(i = 0; i <= CHARCODE_MAX; i++) {
        CCInfo *ci = &unicode_db[i];
        if (ci->decomp_len == 2 && !ci->is_compat &&
            !ci->is_excluded) {
            p = unicode_compose_pair(ci->decomp_data[0], ci->decomp_data[1]);
            if (p != i) {
                printf("ERROR compose: c=%05x %05x -> %05x ref=%05x\n",
                       ci->decomp_data[0], ci->decomp_data[1], p, i);
                exit(1);
            }
        }
    }



}

#endif



#ifdef USE_TEST

void check_str(const char *msg, int num, const int *in_buf, int in_len,
               const int *buf1, int len1,
               const int *buf2, int len2)
{
    if (len1 != len2 || tabcmp(buf1, buf2, len1) != 0) {
        printf("%d: ERROR %s:\n", num, msg);
        dump_str(" in", in_buf, in_len);
        dump_str("res", buf1, len1);
        dump_str("ref", buf2, len2);
        exit(1);
    }
}

void check_cc_table(void)
{
    int cc, cc_ref, c;

    for(c = 0; c <= CHARCODE_MAX; c++) {
        cc_ref = unicode_db[c].combining_class;
        cc = unicode_get_cc(c);
        if (cc != cc_ref) {
            printf("ERROR: c=%04x cc=%d cc_ref=%d\n",
                   c, cc, cc_ref);
            exit(1);
        }
    }
#ifdef PROFILE
    {
        int64_t ti, count;

        ti = get_time_ns();
        count = 0;
        /* only do it on meaningful chars */
        for(c = 0x20; c <= 0xffff; c++) {
            cc_ref = unicode_db[c].combining_class;
            cc = unicode_get_cc(c);
            count++;
        }
        ti = get_time_ns() - ti;
        printf("cc time=%0.1f ns/char\n",
               (double)ti / count);
    }
#endif
}

void normalization_test(const char *filename)
{
    FILE *f;
    char line[4096], *p;
    int *in_str, *nfc_str, *nfd_str, *nfkc_str, *nfkd_str;
    int in_len, nfc_len, nfd_len, nfkc_len, nfkd_len;
    int *buf, buf_len, pos;

    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    pos = 0;
    for(;;) {
        if (!get_line(line, sizeof(line), f))
            break;
        pos++;
        p = line;
        while (isspace(*p))
            p++;
        if (*p == '#' || *p == '@')
            continue;
        in_str = get_field_str(&in_len, p, 0);
        nfc_str = get_field_str(&nfc_len, p, 1);
        nfd_str = get_field_str(&nfd_len, p, 2);
        nfkc_str = get_field_str(&nfkc_len, p, 3);
        nfkd_str = get_field_str(&nfkd_len, p, 4);

        //        dump_str("in", in_str, in_len);

        buf_len = unicode_normalize((uint32_t **)&buf, (uint32_t *)in_str, in_len, UNICODE_NFD, NULL, NULL);
        check_str("nfd", pos, in_str, in_len, buf, buf_len, nfd_str, nfd_len);
        free(buf);

        buf_len = unicode_normalize((uint32_t **)&buf, (uint32_t *)in_str, in_len, UNICODE_NFKD, NULL, NULL);
        check_str("nfkd", pos, in_str, in_len, buf, buf_len, nfkd_str, nfkd_len);
        free(buf);

        buf_len = unicode_normalize((uint32_t **)&buf, (uint32_t *)in_str, in_len, UNICODE_NFC, NULL, NULL);
        check_str("nfc", pos, in_str, in_len, buf, buf_len, nfc_str, nfc_len);
        free(buf);

        buf_len = unicode_normalize((uint32_t **)&buf, (uint32_t *)in_str, in_len, UNICODE_NFKC, NULL, NULL);
        check_str("nfkc", pos, in_str, in_len, buf, buf_len, nfkc_str, nfkc_len);
        free(buf);

        free(in_str);
        free(nfc_str);
        free(nfd_str);
        free(nfkc_str);
        free(nfkd_str);
    }
    fclose(f);
}
#endif

/* ========== 主函数 ========== */

/**
 * 程序入口
 * 
 * 用法：unicode_gen PATH [OUTPUT]
 * - PATH: Unicode 数据库目录（包含 UnicodeData.txt 等文件）
 * - OUTPUT: 输出文件名（可选，不提供则运行自测）
 * 
 * 处理流程：
 * 1. 解析所有 Unicode 数据文件
 * 2. 构建大小写转换表
 * 3. 构建各种 Unicode 属性表
 * 4. 输出 C 语言格式的压缩表
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 0 表示成功
 */
int main(int argc, char *argv[])
{
    const char *unicode_db_path, *outfilename;
    char filename[1024];
    int arg = 1;

    // 显示帮助信息
    if (arg >= argc || (!strcmp(argv[arg], "-h") || !strcmp(argv[arg], "--help"))) {
        printf("usage: %s PATH [OUTPUT]\n"
               "  PATH    path to the Unicode database directory\n"
               "  OUTPUT  name of the output file.  If omitted, a self test is performed\n"
               "          using the files from the Unicode library\n"
               , argv[0]);
        return 1;
    }
    unicode_db_path = argv[arg++];
    outfilename = NULL;
    if (arg < argc)
        outfilename = argv[arg++];

    // 初始化全局数据结构
    unicode_db = mallocz(sizeof(unicode_db[0]) * (CHARCODE_MAX + 1));  // 分配 1M+ 个字符信息
    re_string_list_init(&rgi_emoji_zwj_sequence);  // 初始化 ZWJ 序列哈希表
    dbuf_init(&rgi_emoji_tag_sequence);            // 初始化 Tag 序列缓冲区

    // ========== 阶段 1: 解析所有 Unicode 数据文件 ==========
    
    // 1. UnicodeData.txt - 基础字符数据（码点、名称、分类、大小写等）
    snprintf(filename, sizeof(filename), "%s/UnicodeData.txt", unicode_db_path);
    parse_unicode_data(filename);

    // 2. SpecialCasing.txt - 特殊大小写规则（上下文相关）
    snprintf(filename, sizeof(filename), "%s/SpecialCasing.txt", unicode_db_path);
    parse_special_casing(unicode_db, filename);

    // 3. CaseFolding.txt - 大小写折叠（用于正则匹配）
    snprintf(filename, sizeof(filename), "%s/CaseFolding.txt", unicode_db_path);
    parse_case_folding(unicode_db, filename);

    // 4. CompositionExclusions.txt - 合成排除列表
    snprintf(filename, sizeof(filename), "%s/CompositionExclusions.txt", unicode_db_path);
    parse_composition_exclusions(filename);

    // 5. DerivedCoreProperties.txt - 派生核心属性
    snprintf(filename, sizeof(filename), "%s/DerivedCoreProperties.txt", unicode_db_path);
    parse_derived_core_properties(filename);

    // 6. DerivedNormalizationProps.txt - 派生规范化属性
    snprintf(filename, sizeof(filename), "%s/DerivedNormalizationProps.txt", unicode_db_path);
    parse_derived_norm_properties(filename);

    // 7. PropList.txt - 属性列表
    snprintf(filename, sizeof(filename), "%s/PropList.txt", unicode_db_path);
    parse_prop_list(filename);

    // 8. Scripts.txt - 脚本分配
    snprintf(filename, sizeof(filename), "%s/Scripts.txt", unicode_db_path);
    parse_scripts(filename);

    // 9. ScriptExtensions.txt - 脚本扩展
    snprintf(filename, sizeof(filename), "%s/ScriptExtensions.txt",
             unicode_db_path);
    parse_script_extensions(filename);

    // 10. emoji-data.txt - Emoji 属性
    snprintf(filename, sizeof(filename), "%s/emoji-data.txt",
             unicode_db_path);
    parse_prop_list(filename);

    // 11. emoji-sequences.txt - Emoji 序列
    snprintf(filename, sizeof(filename), "%s/emoji-sequences.txt",
             unicode_db_path);
    parse_sequence_prop_list(filename);

    // 12. emoji-zwj-sequences.txt - Emoji ZWJ 序列
    snprintf(filename, sizeof(filename), "%s/emoji-zwj-sequences.txt",
             unicode_db_path);
    parse_sequence_prop_list(filename);

    // ========== 阶段 2: 构建各种表 ==========
    
    //    dump_unicode_data(unicode_db);  // 调试用
    build_conv_table(unicode_db);  // 构建大小写转换表

#ifdef DUMP_CASE_FOLDING_SPECIAL_CASES
    dump_case_folding_special_cases(unicode_db);
#endif

    // 如果没有指定输出文件，运行自测
    if (!outfilename) {
#ifdef USE_TEST
        check_case_conv();           // 测试大小写转换
        check_flags();               // 测试属性标志
        check_decompose_table();     // 测试分解表
        check_compose_table();       // 测试合成表
        check_cc_table();            // 测试组合类表
        // 运行 Unicode 规范化测试
        snprintf(filename, sizeof(filename), "%s/NormalizationTest.txt", unicode_db_path);
        normalization_test(filename);
#else
        fprintf(stderr, "Tests are not compiled\n");
        exit(1);
#endif
    } else
    {
        // 生成输出文件
        FILE *fo = fopen(outfilename, "wb");

        if (!fo) {
            perror(outfilename);
            exit(1);
        }
        // 输出文件头
        fprintf(fo,
                "/* Compressed unicode tables */\n"
                "/* Automatically generated file - do not edit */\n"
                "\n"
                "#include <stdint.h>\n"
                "\n");
        
        // ========== 输出所有生成的表 ==========
        dump_case_conv_table(fo);        // 大小写转换表
        compute_internal_props();        // 计算内部属性
        build_flags_tables(fo);          // 标志表（Cased, Case_Ignorable, ID_Start, ID_Continue）
        fprintf(fo, "#ifdef CONFIG_ALL_UNICODE\n\n");
        build_cc_table(fo);              // 组合类表
        build_decompose_table(fo);       // 分解表
        build_general_category_table(fo); // 一般分类表
        build_script_table(fo);          // 脚本表
        build_script_ext_table(fo);      // 脚本扩展表
        build_prop_list_table(fo);       // 属性列表表
        build_sequence_prop_list_table(fo); // 序列属性表
        fprintf(fo, "#endif /* CONFIG_ALL_UNICODE */\n");
        
        // 输出统计信息
        fprintf(fo, "/* %u tables / %u bytes, %u index / %u bytes */\n",
                total_tables, total_table_bytes, total_index, total_index_bytes);
        fclose(fo);
    }
    
    // 清理资源
    re_string_list_free(&rgi_emoji_zwj_sequence);
    return 0;
}
