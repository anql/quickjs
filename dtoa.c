/*
 * Tiny float64 printing and parsing library
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
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <sys/time.h>
#include <math.h>
#include <setjmp.h>

#include "cutils.h"
#include "dtoa.h"

/* 
   TODO:
   - test n_digits=101 instead of 100
   - simplify subnormal handling
   - reduce max memory usage
   - free format: could add shortcut if exact result
   - use 64 bit limb_t when possible
   - use another algorithm for free format dtoa in base 10 (ryu ?)
*/

#define USE_POW5_TABLE
/* use fast path to print small integers in free format */
#define USE_FAST_INT

#define LIMB_LOG2_BITS 5

#define LIMB_BITS (1 << LIMB_LOG2_BITS)

typedef int32_t slimb_t;
typedef uint32_t limb_t;
typedef uint64_t dlimb_t;

#define LIMB_DIGITS 9

#define JS_RADIX_MAX 36

#define DBIGNUM_LEN_MAX 52 /* ~ 2^(1072+53)*36^100 (dtoa) */
#define MANT_LEN_MAX 18 /* < 36^100 */

typedef intptr_t mp_size_t;

/* the represented number is sum(i, tab[i]*2^(LIMB_BITS * i)) */
typedef struct {
    int len; /* >= 1 */
    limb_t tab[];
} mpb_t;

/**
 * mp_add_ui - 多精度整数加单精度整数
 * 
 * 计算 tab[] += b，支持进位传播。
 * 
 * @tab: 多精度整数数组（小端序，tab[0] 是最低位）
 * @b: 单精度加数
 * @n: tab 数组的长度（limb 数量）
 * @return: 最终进位（0 或 1）
 * 
 * 算法说明:
 * - 从低位到高位逐位相加
 * - k 保存进位值
 * - 如果进位为 0 则提前退出（优化）
 * 
 * 示例:
 * tab = [0xFFFFFFFF, 0x00000001], b = 1
 * 结果：tab = [0x00000000, 0x00000002], 返回 0
 */
static limb_t mp_add_ui(limb_t *tab, limb_t b, size_t n)
{
    size_t i;
    limb_t k, a;

    k=b;
    for(i=0;i<n;i++) {
        if (k == 0)
            break;
        a = tab[i] + k;
        k = (a < k);  // 检测进位：如果和小于加数，说明溢出
        tab[i] = a;
    }
    return k;
}

/**
 * mp_mul1 - 多精度整数乘以单精度整数
 * 
 * 计算 tabr[] = taba[] * b + l（带初始进位）。
 * 
 * @tabr: 输出数组（多精度结果）
 * @taba: 输入数组（被乘数）
 * @n: 数组长度（limb 数量）
 * @b: 单精度乘数
 * @l: 初始进位值
 * @return: 最终进位（高位扩展）
 * 
 * 算法说明:
 * - 使用双精度中间结果 (dlimb_t) 避免溢出
 * - 逐位相乘并累加进位
 * - 结果的低 LIMB_BITS 位存入 tabr，高位作为新的进位
 * 
 * 数据流:
 * taba[i] * b + l → t (双精度) → tabr[i] = t & 0xFFFFFFFF, l = t >> 32
 */
static limb_t mp_mul1(limb_t *tabr, const limb_t *taba, limb_t n, 
                      limb_t b, limb_t l)
{
    limb_t i;
    dlimb_t t;

    for(i = 0; i < n; i++) {
        t = (dlimb_t)taba[i] * (dlimb_t)b + l;
        tabr[i] = t;
        l = t >> LIMB_BITS;  // 提取高 32 位作为进位
    }
    return l;
}

/**
 * udiv1norm_init - 初始化规范化除法的倒数
 * 
 * 计算 d 的乘法逆元近似值，用于快速除法。
 * 
 * @d: 除数（必须 >= 2^(LIMB_BITS-1)，即最高位为 1）
 * @return: 乘法逆元近似值 d_inv
 * 
 * 算法原理:
 * - 使用 Newton-Raphson 迭代法计算 1/d 的近似值
 * - 公式：d_inv = floor((2^64 - 1 - d) * 2^32 / d)
 * - 这个近似值可以用于后续的 udiv1norm 快速除法
 * 
 * 前置条件:
 * - d 必须是"规范化"的，即最高位为 1（d >= 2^31）
 * - 如果 d 不满足此条件，需要先左移使其规范化
 */
static inline limb_t udiv1norm_init(limb_t d)
{
    limb_t a0, a1;
    a1 = -d - 1;
    a0 = -1;
    return (((dlimb_t)a1 << LIMB_BITS) | a0) / d;
}

/**
 * udiv1norm - 规范化单精度除法（使用乘法逆元）
 * 
 * 计算 (a1 * 2^LIMB_BITS + a0) / d，返回商和余数。
 * 使用预计算的乘法逆元 d_inv 加速除法运算。
 * 
 * @pr: 输出参数，存储余数
 * @a1: 被除数的高 32 位（必须满足 0 <= a1 < d）
 * @a0: 被除数的低 32 位
 * @d: 除数（必须 >= 2^(LIMB_BITS-1)）
 * @d_inv: udiv1norm_init(d) 返回的乘法逆元
 * @return: 商
 * 
 * 算法说明:
 * 1. 使用乘法逆元估算商：q ≈ (a1 * 2^64 + a0) * d_inv / 2^64
 * 2. 计算余数并调整商，确保 0 <= r < d
 * 3. 通过检查余数范围修正商的误差（最多调整±1）
 * 
 * 性能优势:
 * - 避免了昂贵的硬件除法指令
 * - 使用乘法和移位代替除法
 * - 在需要多次除以同一个数时特别有效
 */
static inline limb_t udiv1norm(limb_t *pr, limb_t a1, limb_t a0,
                                limb_t d, limb_t d_inv)
{
    limb_t n1m, n_adj, q, r, ah;
    dlimb_t a;
    n1m = ((slimb_t)a0 >> (LIMB_BITS - 1));  // a0 的符号位扩展
    n_adj = a0 + (n1m & d);  // 调整 a0
    a = (dlimb_t)d_inv * (a1 - n1m) + n_adj;  // 估算商
    q = (a >> LIMB_BITS) + a1;
    /* compute a - q * r and update q so that the remainder is between
       0 and d - 1 */
    a = ((dlimb_t)a1 << LIMB_BITS) | a0;
    a = a - (dlimb_t)q * d - d;  // 计算余数并调整
    ah = a >> LIMB_BITS;
    q += 1 + ah;  // 修正商
    r = (limb_t)a + (ah & d);  // 最终余数
    *pr = r;
    return q;
}

/**
 * mp_div1 - 多精度整数除以单精度整数
 * 
 * 计算 tabr[] = taba[] / b，返回余数。
 * 
 * @tabr: 输出数组（商）
 * @taba: 输入数组（被除数）
 * @n: 数组长度
 * @b: 除数（单精度）
 * @r: 初始余数（通常为 0）
 * @return: 最终余数
 * 
 * 算法说明:
 * - 从高位到低位逐位除法（长除法）
 * - 每次将余数左移 LIMB_BITS 位，加上当前位，然后除以 b
 * - 使用双精度中间结果避免溢出
 */
static limb_t mp_div1(limb_t *tabr, const limb_t *taba, limb_t n,
                      limb_t b, limb_t r)
{
    slimb_t i;
    dlimb_t a1;
    for(i = n - 1; i >= 0; i--) {
        a1 = ((dlimb_t)r << LIMB_BITS) | taba[i];  // 余数 + 当前位
        tabr[i] = a1 / b;
        r = a1 % b;
    }
    return r;
}

/**
 * mp_shr - 多精度整数右移（带高位输入）
 * 
 * 计算 tab_r[] = (tab[] + high * B^n) >> shift。
 * 
 * @tab_r: 输出数组
 * @tab: 输入数组
 * @n: 数组长度
 * @shift: 移位位数（1 <= shift < LIMB_BITS）
 * @high: 高位输入（相当于在 tab[n] 位置的值）
 * @return: 移出的低位余数（0 <= r < 2^shift）
 * 
 * 数据流:
 * [high][tab[n-1]]...[tab[0]] >> shift → [tab_r[n-1]]...[tab_r[0]] + 余数
 */
static limb_t mp_shr(limb_t *tab_r, const limb_t *tab, mp_size_t n, 
                     int shift, limb_t high)
{
    mp_size_t i;
    limb_t l, a;

    assert(shift >= 1 && shift < LIMB_BITS);
    l = high;
    for(i = n - 1; i >= 0; i--) {
        a = tab[i];
        tab_r[i] = (a >> shift) | (l << (LIMB_BITS - shift));  // 当前位右移 + 低位补进
        l = a;  // 保存当前位，用于下一轮
    }
    return l & (((limb_t)1 << shift) - 1);  // 返回移出的最低位
}

/**
 * mp_shl - 多精度整数左移（带低位输入）
 * 
 * 计算 tab_r[] = (tab[] << shift) + low。
 * 
 * @tab_r: 输出数组
 * @tab: 输入数组
 * @n: 数组长度
 * @shift: 移位位数（1 <= shift < LIMB_BITS）
 * @low: 低位输入（填充到 tab_r[0] 的低位）
 * @return: 移出的高位进位
 * 
 * 数据流:
 * [tab[n-1]]...[tab[0]][low] << shift → 进位 + [tab_r[n-1]]...[tab_r[0]]
 */
static limb_t mp_shl(limb_t *tab_r, const limb_t *tab, mp_size_t n, 
              int shift, limb_t low)
{
    mp_size_t i;
    limb_t l, a;

    assert(shift >= 1 && shift < LIMB_BITS);
    l = low;
    for(i = 0; i < n; i++) {
        a = tab[i];
        tab_r[i] = (a << shift) | l;  // 当前位左移 + 低位补进
        l = (a >> (LIMB_BITS - shift));  // 提取移出的高位
    }
    return l;  // 返回最终进位
}

/**
 * mp_div1norm - 规范化多精度除法（使用乘法逆元优化）
 * 
 * 计算 tabr[] = taba[] / b，使用 udiv1norm 加速。
 * 
 * @tabr: 输出数组（商）
 * @taba: 输入数组（被除数）
 * @n: 数组长度
 * @b: 除数（必须已规范化，即最高位为 1）
 * @r: 初始余数
 * @b_inv: udiv1norm_init(b) 返回的乘法逆元
 * @shift: 规范化移位量（b 左移了多少位才达到规范化）
 * @return: 最终余数
 * 
 * 算法流程:
 * 1. 如果需要，先将被除数左移 shift 位（与除数对齐）
 * 2. 使用 udiv1norm 逐位除法（避免硬件除法）
 * 3. 将余数右移 shift 位（还原）
 */
static no_inline limb_t mp_div1norm(limb_t *tabr, const limb_t *taba, limb_t n,
                                    limb_t b, limb_t r, limb_t b_inv, int shift)
{
    slimb_t i;

    if (shift != 0) {
        r = (r << shift) | mp_shl(tabr, taba, n, shift, 0);  // 被除数左移对齐
    }
    for(i = n - 1; i >= 0; i--) {
        tabr[i] = udiv1norm(&r, r, taba[i], b, b_inv);  // 快速除法
    }
    r >>= shift;  // 余数右移还原
    return r;
}

static __maybe_unused void mpb_dump(const char *str, const mpb_t *a)
{
    int i;
    
    printf("%s= 0x", str);
    for(i = a->len - 1; i >= 0; i--) {
        printf("%08x", a->tab[i]);
        if (i != 0)
            printf("_");
    }
    printf("\n");
}

/**
 * mpb_renorm - 多精度数规范化
 * 
 * 移除前导零，确保最高位非零（除非值为 0）。
 * 
 * @r: 多精度数（输入输出）
 * 
 * 不变量:
 * - r->len >= 1
 * - 如果 r->len > 1，则 r->tab[r->len-1] != 0
 * - 如果值为 0，则 r->len = 1 且 r->tab[0] = 0
 */
static void mpb_renorm(mpb_t *r)
{
    while (r->len > 1 && r->tab[r->len - 1] == 0)
        r->len--;
}

#ifdef USE_POW5_TABLE
static const uint32_t pow5_table[17] = {
    0x00000005, 0x00000019, 0x0000007d, 0x00000271, 
    0x00000c35, 0x00003d09, 0x0001312d, 0x0005f5e1, 
    0x001dcd65, 0x009502f9, 0x02e90edd, 0x0e8d4a51, 
    0x48c27395, 0x6bcc41e9, 0x1afd498d, 0x86f26fc1, 
    0xa2bc2ec5, 
};

static const uint8_t pow5h_table[4] = {
    0x00000001, 0x00000007, 0x00000023, 0x000000b1, 
};

static const uint32_t pow5_inv_table[13] = {
    0x99999999, 0x47ae147a, 0x0624dd2f, 0xa36e2eb1,
    0x4f8b588e, 0x0c6f7a0b, 0xad7f29ab, 0x5798ee23,
    0x12e0be82, 0xb7cdfd9d, 0x5fd7fe17, 0x19799812,
    0xc25c2684,
};
#endif

/**
 * pow_ui - 计算整数幂 a^b
 * 
 * 使用快速幂算法（平方 - 乘法）。
 * 
 * @a: 底数
 * @b: 指数
 * @return: a 的 b 次幂
 * 
 * 优化策略:
 * 1. 特殊情况快速返回：b=0 返回 1，b=1 返回 a
 * 2. 对于 5^b 和 10^b（b<=17），使用预计算表（pow5_table）
 * 3. 一般情况使用二进制快速幂：
 *    - 从最高位到最低位扫描指数 b
 *    - 每次平方结果
 *    - 如果当前位为 1，再乘以 a
 * 
 * 示例:
 * 5^3 = 5 * 5 * 5 = 125
 * 10^2 = 100
 */
static uint64_t pow_ui(uint32_t a, uint32_t b)
{
    int i, n_bits;
    uint64_t r;
    if (b == 0)
        return 1;
    if (b == 1)
        return a;
#ifdef USE_POW5_TABLE
    if ((a == 5 || a == 10) && b <= 17) {
        r = pow5_table[b - 1];
        if (b >= 14) {
            r |= (uint64_t)pow5h_table[b - 14] << 32;  // 高 32 位
        }
        if (a == 10)
            r <<= b;  // 10^b = 5^b * 2^b
        return r;
    }
#endif
    r = a;
    n_bits = 32 - clz32(b);  // b 的位数
    for(i = n_bits - 2; i >= 0; i--) {
        r *= r;  // 平方
        if ((b >> i) & 1)
            r *= a;  // 如果当前位为 1，乘以 a
    }
    return r;
}

/**
 * pow_ui_inv - 计算整数幂的规范化形式和乘法逆元
 * 
 * 计算 r = a^b，并返回其规范化形式和乘法逆元。
 * 
 * @pr_inv: 输出参数，存储乘法逆元（用于快速除法）
 * @pshift: 输出参数，存储规范化移位量
 * @a: 底数
 * @b: 指数
 * @return: a^b 的规范化形式（左移 shift 位后）
 * 
 * 输出说明:
 * - 返回值 r：a^b 左移 shift 位后的值（最高位为 1）
 * - *pr_inv：r 的乘法逆元（用于 udiv1norm）
 * - *pshift：规范化移位量
 * 
 * 用途:
 * - 为后续的 mp_div1norm 做准备
 * - 在浮点数转换中用于计算 5^(-b) 等倒数
 */
static uint32_t pow_ui_inv(uint32_t *pr_inv, int *pshift, uint32_t a, uint32_t b)
{
    uint32_t r_inv, r;
    int shift;
#ifdef USE_POW5_TABLE
    if (a == 5 && b >= 1 && b <= 13) {
        r = pow5_table[b - 1];
        shift = clz32(r);  // 前导零计数
        r <<= shift;  // 左移规范化
        r_inv = pow5_inv_table[b - 1];  // 预计算的逆元
    } else
#endif
    {
        r = pow_ui(a, b);
        shift = clz32(r);
        r <<= shift;
        r_inv = udiv1norm_init(r);  // 计算逆元
    }
    *pshift = shift;
    *pr_inv = r_inv;
    return r;
}

enum {
    JS_RNDN, /* round to nearest, ties to even - 四舍五入到最近，平局时取偶数 */
    JS_RNDNA, /* round to nearest, ties away from zero - 四舍五入到最近，平局时远离零 */
    JS_RNDZ, /* round toward zero - 向零舍入（截断） */
};

/**
 * mpb_get_bit - 获取多精度数的指定位
 * 
 * @r: 多精度数
 * @k: 位索引（0 = 最低位）
 * @return: 第 k 位的值（0 或 1）
 * 
 * 实现说明:
 * - 计算位所在的 limb 索引：l = k / LIMB_BITS
 * - 计算在 limb 内的位偏移：k % LIMB_BITS
 * - 如果超出范围，返回 0
 */
static int mpb_get_bit(const mpb_t *r, int k)
{
    int l;
    
    l = (unsigned)k / LIMB_BITS;
    k = k & (LIMB_BITS - 1);  // k % LIMB_BITS
    if (l >= r->len)
        return 0;
    else
        return (r->tab[l] >> k) & 1;
}

/**
 * mpb_shr_round - 多精度数右移并舍入
 * 
 * 计算 r = round(r / 2^shift)，支持负 shift（即左移）。
 * 
 * @r: 多精度数（输入输出）
 * @shift: 移位量（正=右移，负=左移）
 * @rnd_mode: 舍入模式
 *   - JS_RNDZ: 向零舍入（直接截断）
 *   - JS_RNDN: 四舍五入到最近，平局时取偶数（IEEE 754 默认）
 *   - JS_RNDNA: 四舍五入到最近，平局时远离零
 * 
 * 左移情况 (shift < 0):
 * 1. 计算需要移动的 limb 数 l 和位偏移 shift
 * 2. 先处理位偏移：调用 mp_shl 左移
 * 3. 再处理 limb 偏移：整体移动数组
 * 
 * 右移情况 (shift > 0):
 * 1. 确定舍入模式，计算是否需要 +1
 *    - JS_RNDZ: 直接截断，不加 1
 *    - JS_RNDN/JS_RNDNA: 检查被移出的位
 *      * bit1 = 第 (shift-1) 位（最高被移出位）
 *      * bit2 = 所有更低位的 OR（是否有非零位）
 *      * 如果 bit1=1 且 bit2=1：向上舍入
 *      * 如果 bit1=1 且 bit2=0：平局，检查保留位（取偶）
 * 2. 计算 limb 偏移 l 和位偏移 shift
 * 3. 移动数组并右移
 * 4. 如果需要，加 1（处理进位）
 * 
 * 示例:
 * r = 0b1101 (13), shift = 2, JS_RNDN
 * bit1 = 第 1 位 = 0, bit2 = 第 0 位 = 1
 * 结果：0b11 (3)，因为 13/4 = 3.25 ≈ 3
 */
static void mpb_shr_round(mpb_t *r, int shift, int rnd_mode)
{
    int l, i;

    if (shift == 0)
        return;
    if (shift < 0) {
        // 左移情况
        shift = -shift;
        l = (unsigned)shift / LIMB_BITS;
        shift = shift & (LIMB_BITS - 1);
        if (shift != 0) {
            r->tab[r->len] = mp_shl(r->tab, r->tab, r->len, shift, 0);
            r->len++;
            mpb_renorm(r);
        }
        if (l > 0) {
            for(i = r->len - 1; i >= 0; i--)
                r->tab[i + l] = r->tab[i];
            for(i = 0; i < l; i++)
                r->tab[i] = 0;
            r->len += l;
        }
    } else {
        // 右移情况（带舍入）
        limb_t bit1, bit2;
        int k, add_one;
        
        switch(rnd_mode) {
        default:
        case JS_RNDZ:
            add_one = 0;  // 向零舍入：直接截断
            break;
        case JS_RNDN:
        case JS_RNDNA:
            bit1 = mpb_get_bit(r, shift - 1);  // 最高被移出位
            if (bit1) {
                if (rnd_mode == JS_RNDNA) {
                    bit2 = 1;  // 平局时总是远离零
                } else {
                    /* bit2 = oring of all the bits after bit1 */
                    bit2 = 0;
                    if (shift >= 2) {
                        k = shift - 1;
                        l = (unsigned)k / LIMB_BITS;
                        k = k & (LIMB_BITS - 1);
                        for(i = 0; i < min_int(l, r->len); i++)
                            bit2 |= r->tab[i];
                        if (l < r->len)
                            bit2 |= r->tab[l] & (((limb_t)1 << k) - 1);
                    }
                }
                if (bit2) {
                    add_one = 1;  // 大于一半，向上舍入
                } else {
                    /* round to even */
                    add_one = mpb_get_bit(r, shift);  // 平局时取偶数
                }
            } else {
                add_one = 0;  // 小于一半，向下舍入
            }
            break;
        }

        l = (unsigned)shift / LIMB_BITS;
        shift = shift & (LIMB_BITS - 1);
        if (l >= r->len) {
            r->len = 1;
            r->tab[0] = add_one;
        } else {
            if (l > 0) {
                r->len -= l;
                for(i = 0; i < r->len; i++)
                    r->tab[i] = r->tab[i + l];
            }
            if (shift != 0) {
                mp_shr(r->tab, r->tab, r->len, shift, 0);
                mpb_renorm(r);
            }
            if (add_one) {
                limb_t a;
                a = mp_add_ui(r->tab, 1, r->len);
                if (a)
                    r->tab[r->len++] = a;
            }
        }
    }
}

/**
 * mpb_cmp - 比较多精度数大小
 * 
 * @a: 第一个多精度数
 * @b: 第二个多精度数
 * @return: -1 (a<b), 0 (a==b), 1 (a>b)
 * 
 * 比较策略:
 * 1. 先比较长度（limb 数量）- 长度不同直接判断
 * 2. 长度相同则从高位到低位逐位比较
 */
static int mpb_cmp(const mpb_t *a, const mpb_t *b)
{
    mp_size_t i;
    if (a->len < b->len)
        return -1;
    else if (a->len > b->len)
        return 1;
    for(i = a->len - 1; i >= 0; i--) {
        if (a->tab[i] != b->tab[i]) {
            if (a->tab[i] < b->tab[i])
                return -1;
            else
                return 1;
        }
    }
    return 0;
}

/**
 * mpb_set_u64 - 将 uint64 转换为多精度数
 * 
 * @r: 输出多精度数
 * @m: 输入 64 位整数
 * 
 * 根据 LIMB_BITS 大小决定占用几个 limb：
 * - LIMB_BITS=64: 只需 1 个 limb
 * - LIMB_BITS=32: 可能需要 2 个 limb
 */
static void mpb_set_u64(mpb_t *r, uint64_t m)
{
#if LIMB_BITS == 64
    r->tab[0] = m;
    r->len = 1;
#else
    r->tab[0] = m;
    r->tab[1] = m >> LIMB_BITS;
    if (r->tab[1] == 0)
        r->len = 1;
    else
        r->len = 2;
#endif
}

/**
 * mpb_get_u64 - 将多精度数转换为 uint64
 * 
 * @r: 输入多精度数
 * @return: 64 位整数值
 * 
 * 注意：如果多精度数超过 64 位，高位会被截断
 */
static uint64_t mpb_get_u64(mpb_t *r)
{
#if LIMB_BITS == 64
    return r->tab[0];
#else
    if (r->len == 1) {
        return r->tab[0];
    } else {
        return r->tab[0] | ((uint64_t)r->tab[1] << LIMB_BITS);
    }
#endif
}

/**
 * mpb_floor_log2 - 计算多精度数的 floor(log2(x))
 * 
 * @a: 多精度数
 * @return: 最高非零位的位置，或 -1（如果为零）
 * 
 * 计算公式:
 * floor_log2(x) = a->len * LIMB_BITS - 1 - clz32(最高 limb)
 * 
 * 示例:
 * - x = 0 → 返回 -1
 * - x = 1 (2^0) → 返回 0
 * - x = 8 (2^3) → 返回 3
 * - x = 2^100 → 返回 100
 */
static int mpb_floor_log2(mpb_t *a)
{
    limb_t v;
    v = a->tab[a->len - 1];
    if (v == 0)
        return -1;
    else
        return a->len * LIMB_BITS - 1 - clz32(v);
}

#define MUL_LOG2_RADIX_BASE_LOG2 24

/* round((1 << MUL_LOG2_RADIX_BASE_LOG2)/log2(i + 2)) */
static const uint32_t mul_log2_radix_table[JS_RADIX_MAX - 1] = {
    0x000000, 0xa1849d, 0x000000, 0x6e40d2, 
    0x6308c9, 0x5b3065, 0x000000, 0x50c24e, 
    0x4d104d, 0x4a0027, 0x4768ce, 0x452e54, 
    0x433d00, 0x418677, 0x000000, 0x3ea16b, 
    0x3d645a, 0x3c43c2, 0x3b3b9a, 0x3a4899, 
    0x39680b, 0x3897b3, 0x37d5af, 0x372069, 
    0x367686, 0x35d6df, 0x354072, 0x34b261, 
    0x342bea, 0x33ac62, 0x000000, 0x32bfd9, 
    0x3251dd, 0x31e8d6, 0x318465,
};

/**
 * mul_log2_radix - 计算 floor(a / log2(radix))
 * 
 * 用于进制转换时估算结果位数。
 * 
 * @a: 输入值（通常是对数域的指数，范围 -2048 到 2047）
 * @radix: 目标进制（2-36）
 * @return: floor(a / log2(radix))
 * 
 * 算法说明:
 * 1. 如果 radix 是 2 的幂（2,4,8,16,32）：
 *    - 直接使用位移除法：a / log2(radix)
 *    - 例如：radix=16 时，log2(16)=4，返回 a/4
 * 2. 其他进制：
 *    - 使用预计算的乘法表 mul_log2_radix_table
 *    - 公式：(a * mult) >> 24，其中 mult ≈ 2^24 / log2(radix)
 *    - 避免浮点运算，提高性能
 * 
 * 应用场景:
 * - 浮点数转字符串时估算输出长度
 * - 例如：2^100 转 10 进制大约有 100/log2(10) ≈ 30 位
 */
static int mul_log2_radix(int a, int radix)
{
    int radix_bits, mult;

    if ((radix & (radix - 1)) == 0) {
        /* if the radix is a power of two better to do it exactly */
        radix_bits = 31 - clz32(radix);  // log2(radix)
        if (a < 0)
            a -= radix_bits - 1;  // 负数调整
        return a / radix_bits;
    } else {
        mult = mul_log2_radix_table[radix - 2];
        return ((int64_t)a * mult) >> MUL_LOG2_RADIX_BASE_LOG2;  // 定点数乘法
    }
}

#if 0
static void build_mul_log2_radix_table(void)
{
    int base, radix, mult, col, base_log2;

    base_log2 = 24;
    base = 1 << base_log2;
    col = 0;
    for(radix = 2; radix <= 36; radix++) {
        if ((radix & (radix - 1)) == 0)
            mult = 0;
        else
            mult = lrint((double)base / log2(radix));
        printf("0x%06x, ", mult);
        if (++col == 4) {
            printf("\n");
            col = 0;
        }
    }
    printf("\n");
}

static void mul_log2_radix_test(void)
{
    int radix, i, ref, r;
    
    for(radix = 2; radix <= 36; radix++) {
        for(i = -2048; i <= 2047; i++) {
            ref = (int)floor((double)i / log2(radix));
            r = mul_log2_radix(i, radix);
            if (ref != r) {
                printf("ERROR: radix=%d i=%d r=%d ref=%d\n",
                       radix, i, r, ref);
                exit(1);
            }
        }
    }
    if (0)
        build_mul_log2_radix_table();
}
#endif

/**
 * u32toa_len - 将 uint32 转换为固定长度的十进制字符串
 * 
 * @buf: 输出缓冲区（必须至少有 len 字节）
 * @n: 输入整数
 * @len: 输出字符串长度（不足时前面补 0）
 * 
 * 示例:
 * n=123, len=5 → "00123"
 * n=42, len=2 → "42"
 */
static void u32toa_len(char *buf, uint32_t n, size_t len)
{
    int digit, i;
    for(i = len - 1; i >= 0; i--) {
        digit = n % 10;
        n = n / 10;
        buf[i] = digit + '0';
    }
}

/**
 * u64toa_bin_len - 将 uint64 转换为二进制进制的字符串（2/4/8/16/32 进制）
 * 
 * @buf: 输出缓冲区
 * @n: 输入整数
 * @radix_bits: 每个字符表示的位数（1=2 进制，4=16 进制，5=32 进制）
 * @len: 输出字符串长度
 * 
 * 示例:
 * n=255, radix_bits=4 (16 进制), len=2 → "ff"
 * n=7, radix_bits=3 (8 进制), len=1 → "7"
 */
static void u64toa_bin_len(char *buf, uint64_t n, unsigned int radix_bits, int len)
{
    int digit, i;
    unsigned int mask;

    mask = (1 << radix_bits) - 1;
    for(i = len - 1; i >= 0; i--) {
        digit = n & mask;  // 提取最低 radix_bits 位
        n >>= radix_bits;
        if (digit < 10)
            digit += '0';
        else
            digit += 'a' - 10;
        buf[i] = digit;
    }
}

/**
 * limb_to_a - 将 limb_t 转换为任意进制字符串（2-36 进制）
 * 
 * @buf: 输出缓冲区
 * @n: 输入整数
 * @radix: 目标进制（2-36）
 * @len: 输出字符串长度

    if (radix == 10) {
        /* specific case with constant divisor */
#if LIMB_BITS == 32
        u32toa_len(buf, n, len);
#else
        /* XXX: optimize */
        for(i = len - 1; i >= 0; i--) {
            digit = (limb_t)n % 10;
            n = (limb_t)n / 10;
            buf[i] = digit + '0';
        }
#endif
    } else {
        for(i = len - 1; i >= 0; i--) {
            digit = (limb_t)n % radix;
            n = (limb_t)n / radix;
            if (digit < 10)
                digit += '0';
            else
                digit += 'a' - 10;
            buf[i] = digit;
        }
    }
}

/**
 * u32toa - 将 uint32 转换为十进制字符串（无前导零）
 * 
 * @buf: 输出缓冲区（至少 10 字节）
 * @n: 输入整数
 * @return: 字符串长度
 * 
 * 算法：从低位到高位逐位提取，然后反转
 */
size_t u32toa(char *buf, uint32_t n)
{
    char buf1[10], *q;
    size_t len;
    
    q = buf1 + sizeof(buf1);
    do {
        *--q = n % 10 + '0';
        n /= 10;
    } while (n != 0);
    len = buf1 + sizeof(buf1) - q;
    memcpy(buf, q, len);
    return len;
}

/**
 * i32toa - 将 int32 转换为十进制字符串（带符号）
 * 
 * @buf: 输出缓冲区
 * @n: 输入整数
 * @return: 字符串长度（含负号）
 */
size_t i32toa(char *buf, int32_t n)
{
    if (n >= 0) {
        return u32toa(buf, n);
    } else {
        buf[0] = '-';
        return u32toa(buf + 1, -(uint32_t)n) + 1;
    }
}

#ifdef USE_FAST_INT
/**
 * u64toa - 将 uint64 转换为十进制字符串（快速路径优化）
 * 
 * @buf: 输出缓冲区（至少 20 字节）
 * @n: 输入 64 位整数
 * @return: 字符串长度
 * 
 * 优化策略:
 * 1. 如果 n < 2^32，直接使用 u32toa（快速路径）
 * 2. 否则，将 n 分解为高 9 位和低 9 位（以 10^9 为基数）
 *    - n = n1 * 10^9 + n2
 *    - 分别转换 n1 和 n2
 *    - n2 固定输出 9 位（不足补零）
 * 
 * 示例:
 * n = 12345678901234567890
 * n1 = 12345678901, n2 = 234567890
 * 输出："12345678901" + "234567890"
 */
size_t u64toa(char *buf, uint64_t n)
{
    if (n < 0x100000000) {
        return u32toa(buf, n);  // 快速路径
    } else {
        uint64_t n1;
        char *q = buf;
        uint32_t n2;
        
        n1 = n / 1000000000;
        n %= 1000000000;
        if (n1 >= 0x100000000) {
            n2 = n1 / 1000000000;
            n1 = n1 % 1000000000;
            /* at most two digits */
            if (n2 >= 10) {
                *q++ = n2 / 10 + '0';
                n2 %= 10;
            }
            *q++ = n2 + '0';
            u32toa_len(q, n1, 9);
            q += 9;
        } else {
            q += u32toa(q, n1);
        }
        u32toa_len(q, n, 9);  // 低 9 位固定长度（补零）
        q += 9;
        return q - buf;
    }
}

size_t i64toa(char *buf, int64_t n)
{
    if (n >= 0) {
        return u64toa(buf, n);
    } else {
        buf[0] = '-';
        return u64toa(buf + 1, -(uint64_t)n) + 1;
    }
}

/* XXX: only tested for 1 <= n < 2^53 */
/**
 * u64toa_radix - 将 uint64 转换为任意进制字符串（2-36 进制）
 * 
 * @buf: 输出缓冲区
 * @n: 输入 64 位整数
 * @radix: 目标进制（2-36）
 * @return: 字符串长度
 * 
 * 优化策略:
 * 1. radix=10：使用优化的 u64toa
 * 2. radix 是 2 的幂（2/4/8/16/32）：
 *    - 使用位运算代替除法
 *    - 长度 = ceil(64 / radix_bits)
 *    - 调用 u64toa_bin_len（快速路径）
 * 3. 其他进制：
 *    - 使用通用除法算法
 *    - 从低位到高位逐位提取
 *    - 最大长度 41 位（3 进制时最长）
 */
size_t u64toa_radix(char *buf, uint64_t n, unsigned int radix)
{
    int radix_bits, l;
    if (likely(radix == 10))
        return u64toa(buf, n);
    if ((radix & (radix - 1)) == 0) {
        radix_bits = 31 - clz32(radix);  // log2(radix)
        if (n == 0)
            l = 1;
        else
            l = (64 - clz64(n) + radix_bits - 1) / radix_bits;  // ceil(位数/radix_bits)
        u64toa_bin_len(buf, n, radix_bits, l);
        return l;
    } else {
        char buf1[41], *q; /* maximum length for radix = 3 */
        size_t len;
        int digit;
        q = buf1 + sizeof(buf1);
        do {
            digit = n % radix;
            n /= radix;
            if (digit < 10)
                digit += '0';
            else
                digit += 'a' - 10;
            *--q = digit;
        } while (n != 0);
        len = buf1 + sizeof(buf1) - q;
        memcpy(buf, q, len);
        return len;
    }
}

size_t i64toa_radix(char *buf, int64_t n, unsigned int radix)
{
    if (n >= 0) {
        return u64toa_radix(buf, n, radix);
    } else {
        buf[0] = '-';
        return u64toa_radix(buf + 1, -(uint64_t)n, radix) + 1;
    }
}
#endif /* USE_FAST_INT */

static const uint8_t digits_per_limb_table[JS_RADIX_MAX - 1] = {
#if LIMB_BITS == 32
32,20,16,13,12,11,10,10, 9, 9, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
#else
64,40,32,27,24,22,21,20,19,18,17,17,16,16,16,15,15,15,14,14,14,14,13,13,13,13,13,13,13,12,12,12,12,12,12,
#endif
};

static const uint32_t radix_base_table[JS_RADIX_MAX - 1] = {
 0x00000000, 0xcfd41b91, 0x00000000, 0x48c27395,
 0x81bf1000, 0x75db9c97, 0x40000000, 0xcfd41b91,
 0x3b9aca00, 0x8c8b6d2b, 0x19a10000, 0x309f1021,
 0x57f6c100, 0x98c29b81, 0x00000000, 0x18754571,
 0x247dbc80, 0x3547667b, 0x4c4b4000, 0x6b5a6e1d,
 0x94ace180, 0xcaf18367, 0x0b640000, 0x0e8d4a51,
 0x1269ae40, 0x17179149, 0x1cb91000, 0x23744899,
 0x2b73a840, 0x34e63b41, 0x40000000, 0x4cfa3cc1,
 0x5c13d840, 0x6d91b519, 0x81bf1000,
};

/* XXX: remove the table ? */
/**
 * dtoa_max_digits_table - 各进制下 double 的最大有效位数
 * 
 * 计算公式：ceil(53 / log2(radix)) + 1
 * 53 是 IEEE 754 double 的尾数位数（包括隐含的 1）
 * 
 * 用途：限制 dtoa 输出的最大位数，避免无限循环
 */
static uint8_t dtoa_max_digits_table[JS_RADIX_MAX - 1] = {
    54, 35, 28, 24, 22, 20, 19, 18, 17, 17, 16, 16, 15, 15, 15, 14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 12,
};

/**
 * atod_max_digits_table - 各进制下 atod 解析的最大有效位数
 * 
 * 限制精度到约 128 位（非 2 幂进制），JavaScript 要求至少 20 位（10 进制）
 * 2 的幂进制使用精确舍入
 * 
 * 计算公式:
 * - 2 的幂：floor(64 / log2(radix))
 * - 其他：floor(128 / log2(radix))
 */
static uint8_t atod_max_digits_table[JS_RADIX_MAX - 1] = {
     64, 80, 32, 55, 49, 45, 21, 40, 38, 37, 35, 34, 33, 32, 16, 31, 30, 30, 29, 29, 28, 28, 27, 27, 27, 26, 26, 26, 26, 25, 12, 25, 25, 24, 24,
};

/**
 * max_exponent - 各进制下的溢出阈值指数
 * 
 * 如果 |d| >= radix^max_exponent，则判定为溢出（返回 Infinity）
 * 
 * 计算公式：ceil(1024 / log2(radix))
 * 1024 接近 double 的最大指数（1023）
 */
static const int16_t max_exponent[JS_RADIX_MAX - 1] = {
 1024,   647,   512,   442,   397,   365,   342,   324, 
  309,   297,   286,   277,   269,   263,   256,   251, 
  246,   242,   237,   234,   230,   227,   224,   221, 
  218,   216,   214,   211,   209,   207,   205,   203, 
  202,   200,   199, 
};

/**
 * min_exponent - 各进制下的下溢阈值指数
 * 
 * 如果 |d| <= radix^min_exponent，则判定为下溢（返回 0 或次正规数）
 * 
 * 计算公式：floor(-1075 / log2(radix))
 * -1075 接近 double 的最小指数（-1022）减去尾数位数（53）
 */
static const int16_t min_exponent[JS_RADIX_MAX - 1] = {
-1075,  -679,  -538,  -463,  -416,  -383,  -359,  -340, 
 -324,  -311,  -300,  -291,  -283,  -276,  -269,  -263, 
 -258,  -254,  -249,  -245,  -242,  -238,  -235,  -232, 
 -229,  -227,  -224,  -222,  -220,  -217,  -215,  -214, 
 -212,  -210,  -208, 
};

#if 0
void build_tables(void)
{
    int r, j, radix, n, col, i;
    
    /* radix_base_table */
    for(radix = 2; radix <= 36; radix++) {
        r = 1;
        for(j = 0; j < digits_per_limb_table[radix - 2]; j++) {
            r *= radix;
        }
        printf(" 0x%08x,", r);
        if ((radix % 4) == 1)
            printf("\n");
    }
    printf("\n");

    /* dtoa_max_digits_table */
    for(radix = 2; radix <= 36; radix++) {
        /* Note: over estimated when the radix is a power of two */
        printf(" %d,", 1 + (int)ceil(53.0 / log2(radix)));
    }
    printf("\n");

    /* atod_max_digits_table */
    for(radix = 2; radix <= 36; radix++) {
        if ((radix & (radix - 1)) == 0) {
            /* 64 bits is more than enough */
            n = (int)floor(64.0 / log2(radix));
        } else {
            n = (int)floor(128.0 / log2(radix));
        }
        printf(" %d,", n);
    }
    printf("\n");

    printf("static const int16_t max_exponent[JS_RADIX_MAX - 1] = {\n");
    col = 0;
    for(radix = 2; radix <= 36; radix++) {
        printf("%5d, ", (int)ceil(1024 / log2(radix)));
        if (++col == 8) {
            col = 0;
            printf("\n");
        }
    }
    printf("\n};\n\n");

    printf("static const int16_t min_exponent[JS_RADIX_MAX - 1] = {\n");
    col = 0; 
    for(radix = 2; radix <= 36; radix++) {
        printf("%5d, ", (int)floor(-1075 / log2(radix)));
        if (++col == 8) {
            col = 0;
            printf("\n");
        }
    }
    printf("\n};\n\n");

    printf("static const uint32_t pow5_table[16] = {\n");
    col = 0; 
    for(i = 2; i <= 17; i++) {
        r = 1;
        for(j = 0; j < i; j++) {
            r *= 5;
        }
        printf("0x%08x, ", r);
        if (++col == 4) {
            col = 0;
            printf("\n");
        }
    }
    printf("\n};\n\n");

    /* high part */
    printf("static const uint8_t pow5h_table[4] = {\n");
    col = 0; 
    for(i = 14; i <= 17; i++) {
        uint64_t r1;
        r1 = 1;
        for(j = 0; j < i; j++) {
            r1 *= 5;
        }
        printf("0x%08x, ", (uint32_t)(r1 >> 32));
        if (++col == 4) {
            col = 0;
            printf("\n");
        }
    }
    printf("\n};\n\n");
}
#endif

/**
 * output_digits - 将多精度整数转换为指定进制的字符串
 * 
 * @buf: 输出缓冲区
 * @a: 多精度整数（会被修改/销毁）
 * @radix: 目标进制（2-36）
 * @n_digits1: 输出数字位数
 * @dot_pos: 小数点位置（0-based，等于 n_digits1 时不显示小数点）
 * @return: 输出字符串长度（含小数点）
 * 
 * 算法说明:
 * 1. 2 的幂进制（2/4/8/16/32）：
 *    - 使用位运算快速提取
 *    - 每次处理 digits_per_limb 位
 *    - 右移并舍入
 * 2. 其他进制：
 *    - 使用除法逐段提取
 *    - 每次除以 radix^digits_per_limb
 *    - 余数转换为字符串
 * 3. 插入小数点（如果需要）
 * 
 * 示例:
 * a=12345, radix=10, n_digits1=5, dot_pos=2
 * 输出："123.45"（长度 6）
 */
static int output_digits(char *buf,
                         mpb_t *a, int radix, int n_digits1,
                         int dot_pos)
{
    int n_digits, digits_per_limb, radix_bits, n, len;

    n_digits = n_digits1;
    if ((radix & (radix - 1)) == 0) {
        /* radix = 2^radix_bits */
        radix_bits = 31 - clz32(radix);
    } else {
        radix_bits = 0;
    }
    digits_per_limb = digits_per_limb_table[radix - 2];
    if (radix_bits != 0) {
        // 2 的幂进制：快速路径
        for(;;) {
            n = min_int(n_digits, digits_per_limb);
            n_digits -= n;
            u64toa_bin_len(buf + n_digits, a->tab[0], radix_bits, n);
            if (n_digits == 0)
                break;
            mpb_shr_round(a, digits_per_limb * radix_bits, JS_RNDZ);
        }
    } else {
        // 其他进制：除法路径
        limb_t r;
        while (n_digits != 0) {
            n = min_int(n_digits, digits_per_limb);
            n_digits -= n;
            r = mp_div1(a->tab, a->tab, a->len, radix_base_table[radix - 2], 0);
            mpb_renorm(a);
            limb_to_a(buf + n_digits, r, radix, n);
        }
    }

    /* add the dot */
    len = n_digits1;
    if (dot_pos != n_digits1) {
        memmove(buf + dot_pos + 1, buf + dot_pos, n_digits1 - dot_pos);
        buf[dot_pos] = '.';
        len++;
    }
    return len;
}

/**
 * mul_pow - 计算 a * (radix1 * 2^radix_shift)^f
 * 
 * 用于浮点数进制转换中的幂运算。
 * 
 * @a: 多精度数（输入输出，会被修改）
 * @radix1: 进制的奇数部分（radix = radix1 * 2^radix_shift）
 * @radix_shift: 进制的 2 的幂次
 * @f: 指数（可正可负）
 * @is_int: 是否为整数运算
 * @e: 目标精度位数
 * @return: e_offset（指数偏移量）
 * 
 * 数学公式:
 * a_new = a * (radix1 * 2^radix_shift)^f * 2^(-e_offset)
 * 
 * 算法说明:
 * 1. f >= 0（乘法）：
 *    - 分段计算 radix1^n
 *    - 使用 mp_mul1 逐段相乘
 * 2. f < 0（除法）：
 *    - 计算额外的精度位（用于舍入）
 *    - 左移对齐
 *    - 使用 mp_div1norm 快速除法
 *    - 余数 OR 到最低位（用于舍入）
 * 
 * 应用场景:
 * - 将浮点数从二进制转换为其他进制
 * - 计算 m * 2^e * radix^f
 */
static int mul_pow(mpb_t *a, int radix1, int radix_shift, int f, BOOL is_int, int e)
{
    int e_offset, d, n, n0;

    e_offset = -f * radix_shift;
    if (radix1 != 1) {
        d = digits_per_limb_table[radix1 - 2];
        if (f >= 0) {
            limb_t h, b;
            
            b = 0;
            n0 = 0;
            while (f != 0) {
                n = min_int(f, d);
                if (n != n0) {
                    b = pow_ui(radix1, n);
                    n0 = n;
                }
                h = mp_mul1(a->tab, a->tab, a->len, b, 0);
                if (h != 0) {
                    a->tab[a->len++] = h;  // 处理进位
                }
                f -= n;
            }
        } else {
            int extra_bits, l, shift;
            limb_t r, rem, b, b_inv;
            
            f = -f;
            l = (f + d - 1) / d; /* high bound for the number of limbs (XXX: make it better) */
            e_offset += l * LIMB_BITS;
            if (!is_int) {
                /* at least 'e' bits are needed in the final result for rounding */
                extra_bits = max_int(e - mpb_floor_log2(a), 0);
            } else {
                /* at least two extra bits are needed in the final result
                   for rounding */
                extra_bits = max_int(2 + e - e_offset, 0);
            }
            e_offset += extra_bits;
            mpb_shr_round(a, -(l * LIMB_BITS + extra_bits), JS_RNDZ);
            
            b = 0;
            b_inv = 0;
            shift = 0;
            n0 = 0;
            rem = 0;
            while (f != 0) {
                n = min_int(f, d);
                if (n != n0) {
                    b = pow_ui_inv(&b_inv, &shift, radix1, n);
                    n0 = n;
                }
                r = mp_div1norm(a->tab, a->tab, a->len, b, 0, b_inv, shift);
                rem |= r;  // 累积余数（用于舍入）
                mpb_renorm(a);
                f -= n;
            }
            /* if the remainder is non zero, use it for rounding */
            a->tab[0] |= (rem != 0);
        }
    }
    return e_offset;
}

/* tmp1 = round(m*2^e*radix^f). 'tmp0' is a temporary storage */
static void mul_pow_round(mpb_t *tmp1, uint64_t m, int e, int radix1, int radix_shift, int f,
                          int rnd_mode)
{
    int e_offset;

    mpb_set_u64(tmp1, m);
    e_offset = mul_pow(tmp1, radix1, radix_shift, f, TRUE, e);
    mpb_shr_round(tmp1, -e + e_offset, rnd_mode);
}

/**
 * round_to_d - 将多精度数舍入为 IEEE 754 double 格式
 * 
 * @pe: 输出参数，存储指数
 * @a: 多精度数（输入输出，会被修改/销毁）
 * @e_offset: 指数偏移量
 * @rnd_mode: 舍入模式（JS_RNDN/JS_RNDNA/JS_RNDZ）
 * @return: 尾数 m（52 位，隐含最高位 1）
 * 
 * 输出格式:
 * 返回 (m, e) 使得 m * 2^(e-53) = round(a * 2^e_offset)
 * 其中 2^52 <= m < 2^53（规格化数）或 m=0（零）
 * 
 * 处理情况:
 * 1. 零：a=0 时返回 m=0
 * 2. 次正规数：e < -1021 时，精度降低
 * 3. 规格化数：标准 53 位精度
 * 4. 尾数溢出：舍入后 m >= 2^53 时，右移并增加指数
 * 
 * IEEE 754 double 格式:
 * - 符号位：1 位（由调用者处理）
 * - 指数：11 位（偏移 1023）
 * - 尾数：52 位（隐含最高位 1）
 */
static uint64_t round_to_d(int *pe, mpb_t *a, int e_offset, int rnd_mode)
{
    int e;
    uint64_t m;

    if (a->tab[0] == 0 && a->len == 1) {
        /* zero result */
        m = 0;
        e = 0; /* don't care */
    } else {
        int prec, prec1, e_min;
        e = mpb_floor_log2(a) + 1 - e_offset;
        prec1 = 53;  // double 的尾数位数
        e_min = -1021;  // 次正规数的最小指数
        if (e < e_min) {
            /* subnormal result or zero */
            prec = prec1 - (e_min - e);  // 精度降低
        } else {
            prec = prec1;
        }
        mpb_shr_round(a, e + e_offset - prec, rnd_mode);
        m = mpb_get_u64(a);
        m <<= (53 - prec);  // 左移到标准位置
        /* mantissa overflow due to rounding */
        if (m >= (uint64_t)1 << 53) {
            m >>= 1;
            e++;
        }
    }
    *pe = e;
    return m;
}

/* return (m, e) such that m*2^(e-53) = round(a * radix^f) with 2^52
   <= m < 2^53 or m = 0.
   'a' is modified. */
static uint64_t mul_pow_round_to_d(int *pe, mpb_t *a,
                                   int radix1, int radix_shift, int f, int rnd_mode)
{
    int e_offset;

    e_offset = mul_pow(a, radix1, radix_shift, f, FALSE, 55);
    return round_to_d(pe, a, e_offset, rnd_mode);
}

#ifdef JS_DTOA_DUMP_STATS
static int out_len_count[17];

void js_dtoa_dump_stats(void)
{
    int i, sum;
    sum = 0;
    for(i = 0; i < 17; i++)
        sum += out_len_count[i];
    for(i = 0; i < 17; i++) {
        printf("%2d %8d %5.2f%%\n",
               i + 1, out_len_count[i], (double)out_len_count[i] / sum * 100);
    }
}
#endif

/* return a maximum bound of the string length. The bound depends on
   'd' only if format = JS_DTOA_FORMAT_FRAC or if JS_DTOA_EXP_DISABLED
   is enabled. */
int js_dtoa_max_len(double d, int radix, int n_digits, int flags)
{
    int fmt = flags & JS_DTOA_FORMAT_MASK;
    int n, e;
    uint64_t a;

    if (fmt != JS_DTOA_FORMAT_FRAC) {
        if (fmt == JS_DTOA_FORMAT_FREE) {
            n = dtoa_max_digits_table[radix - 2];
        } else {
            n = n_digits;
        }
        if ((flags & JS_DTOA_EXP_MASK) == JS_DTOA_EXP_DISABLED) {
            /* no exponential */
            a = float64_as_uint64(d);
            e = (a >> 52) & 0x7ff;
            if (e == 0x7ff) {
                /* NaN, Infinity */
                n = 0;
            } else {
                e -= 1023;
                /* XXX: adjust */
                n += 10 + abs(mul_log2_radix(e - 1, radix));
            }
        } else {
            /* extra: sign, 1 dot and exponent "e-1000" */
            n += 1 + 1 + 6;
        }
    } else {
        a = float64_as_uint64(d);
        e = (a >> 52) & 0x7ff;
        if (e == 0x7ff) {
            /* NaN, Infinity */
            n = 0;
        } else {
            /* high bound for the integer part */
            e -= 1023;
            /* x < 2^(e + 1) */
            if (e < 0) {
                n = 1;
            } else {
                n = 2 + mul_log2_radix(e - 1, radix);
            }
            /* sign, extra digit, 1 dot */
            n += 1 + 1 + 1 + n_digits;
        }
    }
    return max_int(n, 9); /* also include NaN and [-]Infinity */
}

#if defined(__SANITIZE_ADDRESS__) && 0
static void *dtoa_malloc(uint64_t **pptr, size_t size)
{
    return malloc(size);
}
static void dtoa_free(void *ptr)
{
    free(ptr);
}
#else
static void *dtoa_malloc(uint64_t **pptr, size_t size)
{
    void *ret;
    ret = *pptr;
    *pptr += (size + 7) / 8;
    return ret;
}

static void dtoa_free(void *ptr)
{
}
#endif

/* return the length */
int js_dtoa(char *buf, double d, int radix, int n_digits, int flags,
            JSDTOATempMem *tmp_mem)
{
    uint64_t a, m, *mptr = tmp_mem->mem;
    int e, sgn, l, E, P, i, E_max, radix1, radix_shift;
    char *q;
    mpb_t *tmp1, *mant_max;
    int fmt = flags & JS_DTOA_FORMAT_MASK;

    tmp1 = dtoa_malloc(&mptr, sizeof(mpb_t) + sizeof(limb_t) * DBIGNUM_LEN_MAX);
    mant_max = dtoa_malloc(&mptr, sizeof(mpb_t) + sizeof(limb_t) * MANT_LEN_MAX);
    assert((mptr - tmp_mem->mem) <= sizeof(JSDTOATempMem) / sizeof(mptr[0]));

    radix_shift = ctz32(radix);
    radix1 = radix >> radix_shift;
    a = float64_as_uint64(d);
    sgn = a >> 63;
    e = (a >> 52) & 0x7ff;
    m = a & (((uint64_t)1 << 52) - 1);
    q = buf;
    if (e == 0x7ff) {
        if (m == 0) {
            if (sgn)
                *q++ = '-';
            memcpy(q, "Infinity", 8);
            q += 8;
        } else {
            memcpy(q, "NaN", 3);
            q += 3;
        }
        goto done;
    } else if (e == 0) {
        if (m == 0) {
            tmp1->len = 1;
            tmp1->tab[0] = 0;
            E = 1;
            if (fmt == JS_DTOA_FORMAT_FREE)
                P = 1;
            else if (fmt == JS_DTOA_FORMAT_FRAC)
                P = n_digits + 1;
            else
                P = n_digits;
            /* "-0" is displayed as "0" if JS_DTOA_MINUS_ZERO is not present */
            if (sgn && (flags & JS_DTOA_MINUS_ZERO))
                *q++ = '-';
            goto output;
        }
        /* denormal number: convert to a normal number */
        l = clz64(m) - 11;
        e -= l - 1;
        m <<= l;
    } else {
        m |= (uint64_t)1 << 52;
    }
    if (sgn)
        *q++ = '-';
    /* remove the bias */
    e -= 1022;
    /* d = 2^(e-53)*m */
    //    printf("m=0x%016" PRIx64 " e=%d\n", m, e);
#ifdef USE_FAST_INT
    if (fmt == JS_DTOA_FORMAT_FREE &&
        e >= 1 && e <= 53 &&
        (m & (((uint64_t)1 << (53 - e)) - 1)) == 0 &&
        (flags & JS_DTOA_EXP_MASK) != JS_DTOA_EXP_ENABLED) {
        m >>= 53 - e;
        /* 'm' is never zero */
        q += u64toa_radix(q, m, radix);
        goto done;
    }
#endif
    
    /* this choice of E implies F=round(x*B^(P-E) is such as: 
       B^(P-1) <= F < 2.B^P. */
    E = 1 + mul_log2_radix(e - 1, radix);
    
    if (fmt == JS_DTOA_FORMAT_FREE) {
        int P_max, E0, e1, E_found, P_found;
        uint64_t m1, mant_found, mant, mant_max1;
        /* P_max is guarranteed to work by construction */
        P_max = dtoa_max_digits_table[radix - 2];
        E0 = E;
        E_found = 0;
        P_found = 0;
        mant_found = 0;
        /* find the minimum number of digits by successive tries */
        P = P_max; /* P_max is guarateed to work */
        for(;;) {
            /* mant_max always fits on 64 bits */
            mant_max1 = pow_ui(radix, P);
            /* compute the mantissa in base B */
            E = E0;
            for(;;) {
                /* XXX: add inexact flag */
                mul_pow_round(tmp1, m, e - 53, radix1, radix_shift, P - E, JS_RNDN);
                mant = mpb_get_u64(tmp1);
                if (mant < mant_max1)
                    break;
                E++; /* at most one iteration is possible */
            }
            /* remove useless trailing zero digits */
            while ((mant % radix) == 0) {
                mant /= radix;
                P--;
            }
            /* garanteed to work for P = P_max */
            if (P_found == 0)
                goto prec_found;
            /* convert back to base 2 */
            mpb_set_u64(tmp1, mant);
            m1 = mul_pow_round_to_d(&e1, tmp1, radix1, radix_shift, E - P, JS_RNDN);
            //            printf("P=%2d: m=0x%016" PRIx64 " e=%d m1=0x%016" PRIx64 " e1=%d\n", P, m, e, m1, e1);
            /* Note: (m, e) is never zero here, so the exponent for m1
               = 0 does not matter */
            if (m1 == m && e1 == e) {
            prec_found:
                P_found = P;
                E_found = E;
                mant_found = mant;
                if (P == 1)
                    break;
                P--; /* try lower exponent */
            } else {
                break;
            }
        }
        P = P_found;
        E = E_found;
        mpb_set_u64(tmp1, mant_found);
#ifdef JS_DTOA_DUMP_STATS
        if (radix == 10) {
            out_len_count[P - 1]++;
        }
#endif        
    } else if (fmt == JS_DTOA_FORMAT_FRAC) {
        int len;

        assert(n_digits >= 0 && n_digits <= JS_DTOA_MAX_DIGITS);
        /* P = max_int(E, 1) + n_digits; */
        /* frac is rounded using RNDNA */
        mul_pow_round(tmp1, m, e - 53, radix1, radix_shift, n_digits, JS_RNDNA);

        /* we add one extra digit on the left and remove it if needed
           to avoid testing if the result is < radix^P */
        len = output_digits(q, tmp1, radix, max_int(E + 1, 1) + n_digits,
                            max_int(E + 1, 1));
        if (q[0] == '0' && len >= 2 && q[1] != '.') {
            len--;
            memmove(q, q + 1, len);
        }
        q += len;
        goto done;
    } else {
        int pow_shift;
        assert(n_digits >= 1 && n_digits <= JS_DTOA_MAX_DIGITS);
        P = n_digits;
        /* mant_max = radix^P */
        mant_max->len = 1;
        mant_max->tab[0] = 1;
        pow_shift = mul_pow(mant_max, radix1, radix_shift, P, FALSE, 0);
        mpb_shr_round(mant_max, pow_shift, JS_RNDZ);
        
        for(;;) {
            /* fixed and frac are rounded using RNDNA */
            mul_pow_round(tmp1, m, e - 53, radix1, radix_shift, P - E, JS_RNDNA);
            if (mpb_cmp(tmp1, mant_max) < 0)
                break;
            E++; /* at most one iteration is possible */
        }
    }
 output:
    if (fmt == JS_DTOA_FORMAT_FIXED)
        E_max = n_digits;
    else
        E_max = dtoa_max_digits_table[radix - 2] + 4;
    if ((flags & JS_DTOA_EXP_MASK) == JS_DTOA_EXP_ENABLED ||
        ((flags & JS_DTOA_EXP_MASK) == JS_DTOA_EXP_AUTO && (E <= -6 || E > E_max))) {
        q += output_digits(q, tmp1, radix, P, 1);
        E--;
        if (radix == 10) {
            *q++ = 'e';
        } else if (radix1 == 1 && radix_shift <= 4) {
            E *= radix_shift;
            *q++ = 'p';
        } else {
            *q++ = '@';
        }
        if (E < 0) {
            *q++ = '-';
            E = -E;
        } else {
            *q++ = '+';
        }
        q += u32toa(q, E);
    } else if (E <= 0) {
        *q++ = '0';
        *q++ = '.';
        for(i = 0; i < -E; i++)
            *q++ = '0';
        q += output_digits(q, tmp1, radix, P, P);
    } else {
        q += output_digits(q, tmp1, radix, P, min_int(P, E));
        for(i = 0; i < E - P; i++)
            *q++ = '0';
    }
 done:
    *q = '\0';
    dtoa_free(mant_max);
    dtoa_free(tmp1);
    return q - buf;
}

static inline int to_digit(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    else
        return 36;
}

/* r = r * radix_base + a. radix_base = 0 means radix_base = 2^32 */
static void mpb_mul1_base(mpb_t *r, limb_t radix_base, limb_t a)
{
    int i;
    if (r->tab[0] == 0 && r->len == 1) {
        r->tab[0] = a;
    } else {
        if (radix_base == 0) {
            for(i = r->len; i >= 0; i--) {
                r->tab[i + 1] = r->tab[i];
            }
            r->tab[0] = a;
        } else {
            r->tab[r->len] = mp_mul1(r->tab, r->tab, r->len,
                                     radix_base, a);
        }
        r->len++;
        mpb_renorm(r);
    }
}

/* XXX: add fast path for small integers */
double js_atod(const char *str, const char **pnext, int radix, int flags,
               JSATODTempMem *tmp_mem)
{
    uint64_t *mptr = tmp_mem->mem;
    const char *p, *p_start;
    limb_t cur_limb, radix_base, extra_digits;
    int is_neg, digit_count, limb_digit_count, digits_per_limb, sep, radix1, radix_shift;
    int radix_bits, expn, e, max_digits, expn_offset, dot_pos, sig_pos, pos;
    mpb_t *tmp0;
    double dval;
    BOOL is_bin_exp, is_zero, expn_overflow;
    uint64_t m, a;

    tmp0 = dtoa_malloc(&mptr, sizeof(mpb_t) + sizeof(limb_t) * DBIGNUM_LEN_MAX);
    assert((mptr - tmp_mem->mem) <= sizeof(JSATODTempMem) / sizeof(mptr[0]));
    /* optional separator between digits */
    sep = (flags & JS_ATOD_ACCEPT_UNDERSCORES) ? '_' : 256;

    p = str;
    is_neg = 0;
    if (p[0] == '+') {
        p++;
        p_start = p;
    } else if (p[0] == '-') {
        is_neg = 1;
        p++;
        p_start = p;
    } else {
        p_start = p;
    }
    
    if (p[0] == '0') {
        if ((p[1] == 'x' || p[1] == 'X') &&
            (radix == 0 || radix == 16)) {
            p += 2;
            radix = 16;
        } else if ((p[1] == 'o' || p[1] == 'O') &&
                   radix == 0 && (flags & JS_ATOD_ACCEPT_BIN_OCT)) {
            p += 2;
            radix = 8;
        } else if ((p[1] == 'b' || p[1] == 'B') &&
                   radix == 0 && (flags & JS_ATOD_ACCEPT_BIN_OCT)) {
            p += 2;
            radix = 2;
        } else if ((p[1] >= '0' && p[1] <= '9') &&
                   radix == 0 && (flags & JS_ATOD_ACCEPT_LEGACY_OCTAL)) {
            int i;
            sep = 256;
            for (i = 1; (p[i] >= '0' && p[i] <= '7'); i++)
                continue;
            if (p[i] == '8' || p[i] == '9')
                goto no_prefix;
            p += 1;
            radix = 8;
        } else {
            goto no_prefix;
        }
        /* there must be a digit after the prefix */
        if (to_digit((uint8_t)*p) >= radix)
            goto fail;
    no_prefix: ;
    } else {
        if (!(flags & JS_ATOD_INT_ONLY) && strstart(p, "Infinity", &p))
            goto overflow;
    }
    if (radix == 0)
        radix = 10;

    cur_limb = 0;
    expn_offset = 0;
    digit_count = 0;
    limb_digit_count = 0;
    max_digits = atod_max_digits_table[radix - 2];
    digits_per_limb = digits_per_limb_table[radix - 2];
    radix_base = radix_base_table[radix - 2];
    radix_shift = ctz32(radix);
    radix1 = radix >> radix_shift;
    if (radix1 == 1) {
        /* radix = 2^radix_bits */
        radix_bits = radix_shift;
    } else {
        radix_bits = 0;
    }
    tmp0->len = 1;
    tmp0->tab[0] = 0;
    extra_digits = 0;
    pos = 0;
    dot_pos = -1;
    /* skip leading zeros */
    for(;;) {
        if (*p == '.' && (p > p_start || to_digit(p[1]) < radix) &&
            !(flags & JS_ATOD_INT_ONLY)) {
            if (*p == sep)
                goto fail;
            if (dot_pos >= 0)
                break;
            dot_pos = pos;
            p++;
        }
        if (*p == sep && p > p_start && p[1] == '0')
            p++;
        if (*p != '0')
            break;
        p++;
        pos++;
    }
    
    sig_pos = pos;
    for(;;) {
        limb_t c;
        if (*p == '.' && (p > p_start || to_digit(p[1]) < radix) &&
            !(flags & JS_ATOD_INT_ONLY)) {
            if (*p == sep)
                goto fail;
            if (dot_pos >= 0)
                break;
            dot_pos = pos;
            p++;
        }
        if (*p == sep && p > p_start && to_digit(p[1]) < radix)
            p++;
        c = to_digit(*p);
        if (c >= radix)
            break;
        p++;
        pos++;
        if (digit_count < max_digits) {
            /* XXX: could be faster when radix_bits != 0 */
            cur_limb = cur_limb * radix + c;
            limb_digit_count++;
            if (limb_digit_count == digits_per_limb) {
                mpb_mul1_base(tmp0, radix_base, cur_limb);
                cur_limb = 0;
                limb_digit_count = 0;
            }
            digit_count++;
        } else {
            extra_digits |= c;
        }
    }
    if (limb_digit_count != 0) {
        mpb_mul1_base(tmp0, pow_ui(radix, limb_digit_count), cur_limb);
    }
    if (digit_count == 0) {
        is_zero = TRUE;
        expn_offset = 0;
    } else {
        is_zero = FALSE;
        if (dot_pos < 0)
            dot_pos = pos;
        expn_offset = sig_pos + digit_count - dot_pos;
    }
    
    /* Use the extra digits for rounding if the base is a power of
       two. Otherwise they are just truncated. */
    if (radix_bits != 0 && extra_digits != 0) {
        tmp0->tab[0] |= 1;
    }
    
    /* parse the exponent, if any */
    expn = 0;
    expn_overflow = FALSE;
    is_bin_exp = FALSE;
    if (!(flags & JS_ATOD_INT_ONLY) &&
        ((radix == 10 && (*p == 'e' || *p == 'E')) ||
         (radix != 10 && (*p == '@' ||
                          (radix_bits >= 1 && radix_bits <= 4 && (*p == 'p' || *p == 'P'))))) &&
        p > p_start) {
        BOOL exp_is_neg;
        int c;
        is_bin_exp = (*p == 'p' || *p == 'P');
        p++;
        exp_is_neg = 0;
        if (*p == '+') {
            p++;
        } else if (*p == '-') {
            exp_is_neg = 1;
            p++;
        }
        c = to_digit(*p);
        if (c >= 10)
            goto fail; /* XXX: could stop before the exponent part */
        expn = c;
        p++;
        for(;;) {
            if (*p == sep && to_digit(p[1]) < 10)
                p++;
            c = to_digit(*p);
            if (c >= 10)
                break;
            if (!expn_overflow) {
                if (unlikely(expn > ((INT32_MAX - 2 - 9) / 10))) {
                    expn_overflow = TRUE;
                } else {
                    expn = expn * 10 + c;
                }
            }
            p++;
        }
        if (exp_is_neg)
            expn = -expn;
        /* if zero result, the exponent can be arbitrarily large */
        if (!is_zero && expn_overflow) {
            if (exp_is_neg)
                a = 0;
            else
                a = (uint64_t)0x7ff << 52; /* infinity */
            goto done;
        }
    }

    if (p == p_start)
        goto fail;

    if (is_zero) {
        a = 0;
    } else {
        int expn1;
        if (radix_bits != 0) {
            if (!is_bin_exp)
                expn *= radix_bits;
            expn -= expn_offset * radix_bits;
            expn1 = expn + digit_count * radix_bits;
            if (expn1 >= 1024 + radix_bits)
                goto overflow;
            else if (expn1 <= -1075)
                goto underflow;
            m = round_to_d(&e, tmp0, -expn, JS_RNDN);
        } else {
            expn -= expn_offset;
            expn1 = expn + digit_count;
            if (expn1 >= max_exponent[radix - 2] + 1)
                goto overflow;
            else if (expn1 <= min_exponent[radix - 2])
                goto underflow;
            m = mul_pow_round_to_d(&e, tmp0, radix1, radix_shift, expn, JS_RNDN);
        }
        if (m == 0) {
        underflow:
            a = 0;
        } else if (e > 1024) {
        overflow:
            /* overflow */
            a = (uint64_t)0x7ff << 52;
        } else if (e < -1073) {
            /* underflow */
            /* XXX: check rounding */
            a = 0;
        } else if (e < -1021) {
            /* subnormal */
            a = m >> (-e - 1021);
        } else {
            a = ((uint64_t)(e + 1022) << 52) | (m & (((uint64_t)1 << 52) - 1));
        }
    }
 done:
    a |= (uint64_t)is_neg << 63;
    dval = uint64_as_float64(a);
 done1:
    if (pnext)
        *pnext = p;
    dtoa_free(tmp0);
    return dval;
 fail:
    dval = NAN;
    goto done1;
}
