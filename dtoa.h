/*
 * Tiny float64 printing and parsing library - 浮点数打印与解析库
 * 
 * 本文件提供双精度浮点数 (float64) 与字符串之间的转换功能
 * 支持任意进制（2-36）转换，包括：
 * - 浮点数转字符串：js_dtoa (Dragon4 算法变种，保证最短精确表示)
 * - 字符串转浮点数：js_atod (支持多种格式和进制)
 * - 整数快速转换：u32toa/i32toa/u64toa/i64toa 等
 *
 * Copyright (c) 2024 Fabrice Bellard
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

//#define JS_DTOA_DUMP_STATS  /* 定义此宏可启用统计信息输出（调试用）*/

/* 固定格式和小数格式支持的最大位数（101 位足以表示 float64 的所有精度）*/
#define JS_DTOA_MAX_DIGITS 101

/* ==========================================================================
 * js_dtoa 格式化标志（控制浮点数转字符串的输出格式）
 * ========================================================================== */
/* 非 10 进制转换仅支持 JS_DTOA_FORMAT_FREE 模式 */

/* JS_DTOA_FORMAT_FREE (0 << 0): 自由格式
 * 使用尽可能多的位数以保证精确往返转换
 * 自动选择最短的精确表示（类似 JavaScript 的 toString()）*/
#define JS_DTOA_FORMAT_FREE  (0 << 0)

/* JS_DTOA_FORMAT_FIXED (1 << 0): 固定有效数字格式
 * 使用 n_digits 位有效数字（1 <= n_digits <= JS_DTOA_MAX_DIGITS）
 * 类似 JavaScript 的 toPrecision() */
#define JS_DTOA_FORMAT_FIXED (1 << 0)

/* JS_DTOA_FORMAT_FRAC (2 << 0): 强制小数格式
 * 格式：[-]dd.dd，n_digits 位小数（0 <= n_digits <= JS_DTOA_MAX_DIGITS）
 * 类似 JavaScript 的 toFixed() */
#define JS_DTOA_FORMAT_FRAC  (2 << 0)

/* 格式掩码：用于提取格式类型 (flags & JS_DTOA_FORMAT_MASK) */
#define JS_DTOA_FORMAT_MASK  (3 << 0)

/* ==========================================================================
 * 指数记号控制标志
 * ========================================================================== */
/* JS_DTOA_EXP_AUTO (0 << 2): 自动指数记号
 * 根据数值大小自动决定是否使用指数形式（默认行为）*/
#define JS_DTOA_EXP_AUTO     (0 << 2)

/* JS_DTOA_EXP_ENABLED (1 << 2): 强制启用指数记号
 * 总是使用指数形式（如 1.23e+10）*/
#define JS_DTOA_EXP_ENABLED  (1 << 2)

/* JS_DTOA_EXP_DISABLED (2 << 2): 强制禁用指数记号
 * 从不使用指数形式，总是用小数形式 */
#define JS_DTOA_EXP_DISABLED (2 << 2)

/* 指数记号掩码：用于提取指数控制类型 (flags & JS_DTOA_EXP_MASK) */
#define JS_DTOA_EXP_MASK     (3 << 2)

/* JS_DTOA_MINUS_ZERO (1 << 4): 显示 -0 的负号
 * 默认情况下 -0.0 可能显示为 "0"，此标志强制显示为 "-0" */
#define JS_DTOA_MINUS_ZERO   (1 << 4)

/* ==========================================================================
 * js_atod 解析标志（控制字符串转浮点数的解析行为）
 * ========================================================================== */
/* JS_ATOD_INT_ONLY (1 << 0): 仅接受整数
 * 不允许小数点和指数部分（用于 parseInt 风格的解析）*/
#define JS_ATOD_INT_ONLY       (1 << 0)

/* JS_ATOD_ACCEPT_BIN_OCT (1 << 1): 接受二进制和八进制前缀
 * 当 radix=0 时，接受 0o/0O (八进制) 和 0b/0B (二进制) 前缀
 * 标准已支持 0x/0X (十六进制) 前缀 */
#define JS_ATOD_ACCEPT_BIN_OCT (1 << 1)

/* JS_ATOD_ACCEPT_LEGACY_OCTAL (1 << 2): 接受传统八进制表示（Annex B）
 * 当 radix=0 且格式正确时，接受前导 0 表示八进制（如 0755 = 493）
 * 这是 ECMAScript Annex B 的兼容性扩展 */
#define JS_ATOD_ACCEPT_LEGACY_OCTAL  (1 << 2)

/* JS_ATOD_ACCEPT_UNDERSCORES (1 << 3): 接受数字分隔符
 * 允许在数字之间使用下划线 _ 作为分隔符（如 1_000_000）
 * 这是 ES2021 引入的数字分隔符特性 */
#define JS_ATOD_ACCEPT_UNDERSCORES  (1 << 3)

/* ==========================================================================
 * 临时内存结构体（避免动态分配，提高性能）
 * ========================================================================== */
/* js_dtoa 临时工作内存（37 * 8 = 296 字节）
 * 用于存储大整数运算的中间结果（Dragon4 算法需要多精度算术）*/
typedef struct {
    uint64_t mem[37];
} JSDTOATempMem;

/* js_atod 临时工作内存（27 * 8 = 216 字节）
 * 用于解析过程中的大整数存储 */
typedef struct {
    uint64_t mem[27];
} JSATODTempMem;

/* ==========================================================================
 * 浮点数转字符串 API
 * ========================================================================== */
/* 计算转换后字符串的最大长度（用于预分配缓冲区）
 * @param d 双精度浮点数
 * @param radix 进制（2-36）
 * @param n_digits 位数（根据格式标志解释）
 * @param flags 格式化标志
 * @return 最大字符串长度（不含结尾 '\\0'）*/
int js_dtoa_max_len(double d, int radix, int n_digits, int flags);

/* 将双精度浮点数转换为字符串
 * @param buf 输出缓冲区（需预先分配足够空间，使用 js_dtoa_max_len 计算）
 * @param d 双精度浮点数
 * @param radix 进制（2-36，10 为十进制）
 * @param n_digits 位数（根据格式标志解释）
 * @param flags 格式化标志（JS_DTOA_FORMAT_* | JS_DTOA_EXP_* | JS_DTOA_MINUS_ZERO）
 * @param tmp_mem 临时工作内存（调用者提供，避免动态分配）
 * @return 实际输出的字符串长度（不含结尾 '\\0'）*/
int js_dtoa(char *buf, double d, int radix, int n_digits, int flags,
            JSDTOATempMem *tmp_mem);

/* ==========================================================================
 * 字符串转浮点数 API
 * ========================================================================== */
/* 将字符串解析为双精度浮点数
 * @param str 输入字符串
 * @param pnext 输出参数：指向第一个未解析字符的指针（可为 NULL）
 * @param radix 进制（0=自动检测，2-36=指定进制）
 * @param flags 解析标志（JS_ATOD_* 的组合）
 * @param tmp_mem 临时工作内存（调用者提供，避免动态分配）
 * @return 解析后的双精度浮点数（解析失败返回 NaN）*/
double js_atod(const char *str, const char **pnext, int radix, int flags,
               JSATODTempMem *tmp_mem);

/* ==========================================================================
 * 统计信息（调试用，需定义 JS_DTOA_DUMP_STATS）
 * ========================================================================== */
#ifdef JS_DTOA_DUMP_STATS
/* 输出 dtoa 库的统计信息（内存分配次数、缓冲区使用情况等）*/
void js_dtoa_dump_stats(void);
#endif

/* ==========================================================================
 * 整数转字符串辅助函数（快速路径，无需临时内存）
 * ========================================================================== */
/* 无符号 32 位整数转十进制字符串
 * @param buf 输出缓冲区（至少 11 字节：10 位数字 + '\\0'）
 * @param n 输入整数
 * @return 输出的字符串长度 */
size_t u32toa(char *buf, uint32_t n);

/* 有符号 32 位整数转十进制字符串
 * @param buf 输出缓冲区（至少 12 字节：符号 + 10 位数字 + '\\0'）
 * @param n 输入整数
 * @return 输出的字符串长度 */
size_t i32toa(char *buf, int32_t n);

/* 无符号 64 位整数转十进制字符串
 * @param buf 输出缓冲区（至少 21 字节：20 位数字 + '\\0'）
 * @param n 输入整数
 * @return 输出的字符串长度 */
size_t u64toa(char *buf, uint64_t n);

/* 有符号 64 位整数转十进制字符串
 * @param buf 输出缓冲区（至少 22 字节：符号 + 20 位数字 + '\\0'）
 * @param n 输入整数
 * @return 输出的字符串长度 */
size_t i64toa(char *buf, int64_t n);

/* 无符号 64 位整数转任意进制字符串
 * @param buf 输出缓冲区（至少 65 字节：64 位二进制 + '\\0'）
 * @param n 输入整数
 * @param radix 进制（2-36）
 * @return 输出的字符串长度 */
size_t u64toa_radix(char *buf, uint64_t n, unsigned int radix);

/* 有符号 64 位整数转任意进制字符串
 * @param buf 输出缓冲区（至少 66 字节：符号 + 64 位二进制 + '\\0'）
 * @param n 输入整数
 * @param radix 进制（2-36）
 * @return 输出的字符串长度 */
size_t i64toa_radix(char *buf, int64_t n, unsigned int radix);
