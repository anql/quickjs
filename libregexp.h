/*
 * Regular Expression Engine - 正则表达式引擎头文件
 * 
 * 本文件提供 QuickJS 正则表达式编译与执行的公共 API
 * 支持 ECMAScript 正则表达式语法，包括：
 * - 标准标志：global/ignoreCase/multiline/dotAll/unicode/sticky
 * - 命名捕获组、反向引用、零宽断言等高级特性
 * - Unicode 属性转义 (\p{...}) 和字符类
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
#ifndef LIBREGEXP_H
#define LIBREGEXP_H

#include <stddef.h>
#include <stdint.h>

/* ==========================================================================
 * 正则表达式标志位 (与 JavaScript RegExp 标志对应)
 * ========================================================================== */
#define LRE_FLAG_GLOBAL       (1 << 0)  /* g 标志：全局匹配，查找所有匹配项 */
#define LRE_FLAG_IGNORECASE   (1 << 1)  /* i 标志：不区分大小写匹配 */
#define LRE_FLAG_MULTILINE    (1 << 2)  /* m 标志：多行模式，^和$匹配行首/行尾 */
#define LRE_FLAG_DOTALL       (1 << 3)  /* s 标志：dotAll 模式，.匹配换行符 */
#define LRE_FLAG_UNICODE      (1 << 4)  /* u 标志：Unicode 模式，正确处理代理对 */
#define LRE_FLAG_STICKY       (1 << 5)  /* y 标志：sticky 模式，从 lastIndex 位置开始匹配 */
#define LRE_FLAG_INDICES      (1 << 6)  /* d 标志：indices 属性（记录匹配位置，本库仅记录不使用）*/
#define LRE_FLAG_NAMED_GROUPS (1 << 7)  /* 命名捕获组标志：正则中包含命名组 (?<name>...) */
#define LRE_FLAG_UNICODE_SETS (1 << 8)  /* v 标志：Unicode 集合模式（ES2024 新特性）*/

/* ==========================================================================
 * 返回值定义
 * ========================================================================== */
#define LRE_RET_MEMORY_ERROR  (-1)  /* 内存分配失败 */
#define LRE_RET_TIMEOUT       (-2)  /* 执行超时（防止 ReDoS 攻击）*/

/* 分组名称后的尾部长度（包括结尾的 '\0' 和填充字节）*/
#define LRE_GROUP_NAME_TRAILER_LEN 2 

/* ==========================================================================
 * 正则表达式编译 API
 * ========================================================================== */
/* 编译正则表达式字符串为字节码
 * @param plen 输出参数：返回字节码长度
 * @param error_msg 输出缓冲区：存储错误消息（如果编译失败）
 * @param error_msg_size 错误消息缓冲区大小
 * @param buf 正则表达式字符串输入
 * @param buf_len 输入字符串长度
 * @param re_flags 正则标志位（LRE_FLAG_* 的组合）
 * @param opaque 内存管理器上下文（通常传入 JSRuntime）
 * @return 成功返回字节码缓冲区指针（需调用 lre_realloc 释放），失败返回 NULL */
uint8_t *lre_compile(int *plen, char *error_msg, int error_msg_size,
                     const char *buf, size_t buf_len, int re_flags,
                     void *opaque);

/* ==========================================================================
 * 字节码查询 API（编译后获取正则信息）
 * ========================================================================== */
/* 获取字节码的内存分配计数（用于调试和性能分析）
 * @param bc_buf 编译后的字节码缓冲区
 * @return 分配次数 */
int lre_get_alloc_count(const uint8_t *bc_buf);

/* 获取捕获组数量（包括命名组和数字组）
 * @param bc_buf 编译后的字节码缓冲区
 * @return 捕获组数量（不含整个匹配的隐式组 0）*/
int lre_get_capture_count(const uint8_t *bc_buf);

/* 获取正则表达式的标志位
 * @param bc_buf 编译后的字节码缓冲区
 * @return 标志位掩码（LRE_FLAG_* 的组合）*/
int lre_get_flags(const uint8_t *bc_buf);

/* 获取命名捕获组的名称列表
 * @param bc_buf 编译后的字节码缓冲区
 * @return 分组名称字符串指针（空分隔的字符串列表，无命名组返回 NULL）*/
const char *lre_get_groupnames(const uint8_t *bc_buf);

/* ==========================================================================
 * 正则表达式执行 API
 * ========================================================================== */
/* 执行正则匹配（在输入字符串中查找匹配）
 * @param capture 输出参数：捕获组数组（每个捕获组返回 [start, end] 位置对）
 * @param bc_buf 编译后的字节码缓冲区
 * @param cbuf 输入字符缓冲区
 * @param cindex 起始匹配索引（用于 sticky 模式）
 * @param clen 输入字符长度
 * @param cbuf_type 字符缓冲区类型（0=8 位，1=16 位 UTF-16）
 * @param opaque 运行时上下文（用于内存分配和超时检查）
 * @return 匹配成功返回 1，失败返回 0，错误返回负值（LRE_RET_*）*/
int lre_exec(uint8_t **capture,
             const uint8_t *bc_buf, const uint8_t *cbuf, int cindex, int clen,
             int cbuf_type, void *opaque);

/* ==========================================================================
 * 辅助工具函数
 * ========================================================================== */
/* 解析转义序列（用于词法分析阶段）
 * @param pp 输入输出：指向转义序列起始位置的指针（解析后前进）
 * @param allow_utf16 是否允许 UTF-16 转义（\uXXXX 和 \u{XXXXX}）
 * @return 解析后的字符码点，失败返回 -1 */
int lre_parse_escape(const uint8_t **pp, int allow_utf16);

/* ==========================================================================
 * 用户必须提供的回调函数（由 QuickJS 运行时实现）
 * ========================================================================== */
/* 检查栈溢出（防止递归过深导致崩溃）
 * @param opaque 运行时上下文
 * @param alloca_size 当前栈使用量
 * @return 栈溢出返回非零，否则返回 0 */
int lre_check_stack_overflow(void *opaque, size_t alloca_size);

/* 检查执行超时（防止 ReDoS 正则表达式拒绝服务攻击）
 * @param opaque 运行时上下文
 * @return 超时返回非零，否则返回 0 */
int lre_check_timeout(void *opaque);

/* 内存重分配函数（由调用者提供，通常封装 js_realloc_rt）
 * @param opaque 运行时上下文
 * @param ptr 原指针（NULL 表示分配新内存）
 * @param size 新大小
 * @return 新指针，失败返回 NULL */
void *lre_realloc(void *opaque, void *ptr, size_t size);

#endif /* LIBREGEXP_H */
