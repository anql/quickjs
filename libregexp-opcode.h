/*
 * Regular Expression Engine
 * 正则表达式引擎字节码操作码定义
 * 
 * 本文件定义了正则表达式编译后生成的字节码指令集。
 * 每个 DEF 宏定义一个操作码，包含：
 * - 操作码名称
 * - 操作码占用的字节数
 * 
 * 这些操作码在 libregexp.c 中用于构建和执行正则匹配。
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

#ifdef DEF

/* ==================== 基础字符匹配操作码 ==================== */

/**
 * 无效操作码（占位符，永不使用）
 * 字节数：1
 */
DEF(invalid, 1) /* never used */

/**
 * 匹配单个字符（区分大小写）
 * 字节数：3（操作码 1 字节 + 字符值 2 字节）
 * 用于匹配 ASCII 或 BMP 平面字符
 */
DEF(char, 3)

/**
 * 匹配单个字符（不区分大小写）
 * 字节数：3
 * 忽略大小写进行匹配
 */
DEF(char_i, 3)

/**
 * 匹配 32 位字符（区分大小写）
 * 字节数：5（操作码 1 字节 + 字符值 4 字节）
 * 用于匹配超出 BMP 平面的 Unicode 字符（如 Emoji）
 */
DEF(char32, 5)

/**
 * 匹配 32 位字符（不区分大小写）
 * 字节数：5
 */
DEF(char32_i, 5)

/**
 * 匹配除换行符外的任意字符（.）
 * 字节数：1
 * 对应正则表达式中的 . 元字符
 */
DEF(dot, 1)

/**
 * 匹配任意字符（包括换行符）
 * 字节数：1
 * 与 dot 类似，但匹配包括行终止符在内的所有字符
 */
DEF(any, 1) /* same as dot but match any character including line terminator */

/**
 * 匹配空白字符（\s）
 * 字节数：1
 * 匹配空格、制表符、换行符等
 */
DEF(space, 1)

/**
 * 匹配非空白字符（\S）
 * 字节数：1
 * 必须紧跟在 space 之后定义
 */
DEF(not_space, 1) /* must come after */

/* ==================== 位置锚点操作码 ==================== */

/**
 * 匹配行首（^）
 * 字节数：1
 * 多行模式下使用
 */
DEF(line_start, 1)

/**
 * 匹配行首（多行模式）
 * 字节数：1
 */
DEF(line_start_m, 1)

/**
 * 匹配行尾（$）
 * 字节数：1
 */
DEF(line_end, 1)

/**
 * 匹配行尾（多行模式）
 * 字节数：1
 */
DEF(line_end_m, 1)

/* ==================== 控制流操作码 ==================== */

/**
 * 无条件跳转
 * 字节数：5（操作码 1 字节 + 偏移量 4 字节）
 * 用于实现分支、循环等控制结构
 */
DEF(goto, 5)

/**
 * 分裂：先执行 goto 分支，再回溯执行后续分支
 * 字节数：5
 * 用于实现交替（|）操作，优先尝试第一个分支
 */
DEF(split_goto_first, 5)

/**
 * 分裂：先执行后续分支，再回溯执行 goto 分支
 * 字节数：5
 * 用于实现交替（|）操作，优先尝试第二个分支
 */
DEF(split_next_first, 5)

/**
 * 匹配成功
 * 字节数：1
 * 表示整个正则表达式匹配完成
 */
DEF(match, 1)

/* ==================== 零宽断言操作码 ==================== */

/**
 * 肯定预查匹配（(?=...)）
 * 字节数：1
 * 向前查看，不消耗字符
 */
DEF(lookahead_match, 1)

/**
 * 否定预查匹配（(?!...)）
 * 字节数：1
 * 必须紧跟在 lookahead_match 之后定义
 */
DEF(negative_lookahead_match, 1) /* must come after */

/* ==================== 捕获组操作码 ==================== */

/**
 * 保存捕获组开始位置
 * 字节数：2（操作码 1 字节 + 组号 1 字节）
 * 用于记录捕获组的起始索引
 */
DEF(save_start, 2) /* save start position */

/**
 * 保存捕获组结束位置
 * 字节数：2
 * 必须紧跟在 save_start 之后定义
 */
DEF(save_end, 2) /* save end position, must come after saved_start */

/**
 * 重置捕获组保存位置
 * 字节数：3（操作码 1 字节 + 起始组号 1 字节 + 结束组号 1 字节）
 * 用于清除指定范围内的捕获组记录
 */
DEF(save_reset, 3) /* reset save positions */

/* ==================== 循环操作码 ==================== */

/**
 * 循环：递减栈顶值，非零则跳转
 * 字节数：6（操作码 1 字节 + 计数 1 字节 + 偏移量 4 字节）
 * 用于实现量词 {n,m} 的精确次数控制
 */
DEF(loop, 6) /* decrement the top the stack and goto if != 0 */

/**
 * 循环分裂：先执行循环体，再分裂
 * 字节数：10
 * 用于贪婪量词的循环实现
 */
DEF(loop_split_goto_first, 10) /* loop and then split */

/**
 * 循环分裂：先分裂，再执行循环体
 * 字节数：10
 * 用于非贪婪量词的循环实现
 */
DEF(loop_split_next_first, 10)

/**
 * 循环检查前进并分裂（先执行循环）
 * 字节数：10
 * 防止空匹配的无限循环
 */
DEF(loop_check_adv_split_goto_first, 10) /* loop and then check advance and split */

/**
 * 循环检查前进并分裂（先分裂）
 * 字节数：10
 */
DEF(loop_check_adv_split_next_first, 10)

/* ==================== 寄存器操作码 ==================== */

/**
 * 设置 32 位立即值到寄存器
 * 字节数：6（操作码 1 字节 + 寄存器号 1 字节 + 值 4 字节）
 * 用于保存位置、计数等中间状态
 */
DEF(set_i32, 6) /* store the immediate value to a register */

/* ==================== 单词边界操作码 ==================== */

/**
 * 匹配单词边界（\b）
 * 字节数：1
 * 区分大小写
 */
DEF(word_boundary, 1)

/**
 * 匹配单词边界（不区分大小写）
 * 字节数：1
 */
DEF(word_boundary_i, 1)

/**
 * 匹配非单词边界（\B）
 * 字节数：1
 */
DEF(not_word_boundary, 1)

/**
 * 匹配非单词边界（不区分大小写）
 * 字节数：1
 */
DEF(not_word_boundary_i, 1)

/* ==================== 反向引用操作码 ==================== */

/**
 * 反向引用（\1, \2, ...）
 * 字节数：2（操作码 1 字节 + 组号 1 字节）
 * 可变长度，引用之前捕获组的内容
 */
DEF(back_reference, 2) /* variable length */

/**
 * 反向引用（不区分大小写）
 * 字节数：2
 * 必须紧跟在 back_reference 之后定义
 */
DEF(back_reference_i, 2) /* must come after */

/**
 * 向后反向引用
 * 字节数：2
 * 用于某些特殊的反向引用场景
 */
DEF(backward_back_reference, 2) /* must come after */

/**
 * 向后反向引用（不区分大小写）
 * 字节数：2
 */
DEF(backward_back_reference_i, 2) /* must come after */

/* ==================== 字符范围操作码 ==================== */

/**
 * 匹配字符范围（[a-z]）
 * 字节数：3（操作码 1 字节 + 范围长度 2 字节）
 * 可变长度，后跟具体的范围数据
 */
DEF(range, 3) /* variable length */

/**
 * 匹配字符范围（不区分大小写）
 * 字节数：3
 */
DEF(range_i, 3) /* variable length */

/**
 * 匹配 32 位字符范围
 * 字节数：3
 * 用于 Unicode 扩展平面的范围匹配
 */
DEF(range32, 3) /* variable length */

/**
 * 匹配 32 位字符范围（不区分大小写）
 * 字节数：3
 */
DEF(range32_i, 3) /* variable length */

/* ==================== 预查操作码 ==================== */

/**
 * 肯定预查（(?=...)）
 * 字节数：5（操作码 1 字节 + 偏移量 4 字节）
 * 执行子表达式匹配，成功则继续
 */
DEF(lookahead, 5)

/**
 * 否定预查（(?!...)）
 * 字节数：5
 * 必须紧跟在 lookahead 之后定义
 */
DEF(negative_lookahead, 5) /* must come after */

/* ==================== 位置管理操作码 ==================== */

/**
 * 设置字符位置到寄存器
 * 字节数：2（操作码 1 字节 + 寄存器号 1 字节）
 * 用于记录当前匹配位置
 */
DEF(set_char_pos, 2) /* store the character position to a register */

/**
 * 检查是否前进（防止空匹配）
 * 字节数：2（操作码 1 字节 + 寄存器号 1 字节）
 * 比较当前位置与寄存器中的位置，确保有进展
 */
DEF(check_advance, 2) /* check that the register is different from the character position */

/**
 * 回退到前一个字符
 * 字节数：1
 * 用于某些需要回溯的场景
 */
DEF(prev, 1) /* go to the previous char */

#endif /* DEF */
