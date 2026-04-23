#!/usr/bin/env python3
"""
QuickJS 自动注释脚本
为缺少文档注释的函数添加中文注释
"""

import re
import os

def get_function_comment(func_name, func_type, line_num, content):
    """根据函数名和上下文生成合适的注释"""
    
    # 基于函数名模式的注释模板
    comment_templates = {
        r'^JS_Set.*': '设置 {name} 配置',
        r'^JS_Get.*': '获取 {name} 配置',
        r'^JS_Free.*': '释放 {name} 资源',
        r'^JS_New.*': '创建新的 {name} 实例',
        r'^JS_Dup.*': '增加 {name} 引用计数',
        r'^js_free.*': '释放 {name} 内存',
        r'^free_.*': '释放 {name} 资源',
        r'^js_.*_finalizer': '{name} 析构函数',
        r'^js_.*_mark': '{name} GC 标记函数',
        r'^js_.*_init': '{name} 初始化函数',
        r'^JS_Resize.*': '调整 {name} 大小',
        r'^JS_Init.*': '初始化 {name}',
        r'^js_.*_concat': '{name} 拼接函数',
        r'^js_.*_string': '字符串{action}函数',
        r'^js_.*_rope': '绳索字符串{action}函数',
        r'^shape_.*': '形状{action}函数',
        r'^compact_.*': '压缩{action}函数',
        r'^expand_.*': '扩展{action}函数',
        r'^delete_.*': '删除{action}函数',
        r'^check_.*': '检查{action}函数',
        r'^get_.*': '获取{action}函数',
        r'^is_.*': '判断{action}函数',
        r'^JS_Is.*': '判断是否为{action}',
        r'^JS_Has.*': '检查是否包含{action}',
        r'^JS_Prevent.*': '阻止{action}',
        r'^JS_Check.*': '检查{action}',
        r'^JS_Add.*': '添加{action}',
        r'^JS_Delete.*': '删除{action}',
        r'^JS_To.*': '转换为{action}',
        r'^JS_New.*': '创建{action}',
        r'^JS_ValueTo.*': '将值转换为{action}',
        r'^JS_Number.*': '数字{action}函数',
        r'^js_print.*': '打印{action}函数',
        r'^js_putc': '输出单个字符',
        r'^js_puts': '输出字符串',
        r'^mp_.*': '多精度算术{action}函数',
        r'^js_bigint_.*': '大整数{action}函数',
        r'^js_def_.*': '默认{action}函数',
        r'^__JS_Free.*': '释放{action}（内部函数）',
        r'^JS_RunGC.*': '执行垃圾回收',
        r'^JS_IsLive.*': '检查对象是否存活',
        r'^JS_Dump.*': '调试输出{action}信息',
        r'^is_backtrace_needed': '判断是否需要回溯信息',
        r'^num_keys_.*': '数字键{action}函数',
        r'^get_prop_flags': '获取属性标志',
        r'^JS_CheckDefineGlobalVar': '检查全局变量定义',
        r'^JS_GetGlobalVarRef': '获取全局变量引用',
        r'^JS_DeleteGlobalVar': '删除全局变量',
        r'^JS_DeleteProperty': '删除对象属性',
        r'^JS_IsFunction': '判断是否为函数',
        r'^JS_ToPrimitiveFree': '将值转换为原始类型',
        r'^js_limb_.*': '大数 limb{action}函数',
        r'^js_slimb_.*': '有符号 limb{action}函数',
        r'^shr_rndn': '右移舍入函数',
        r'^JS_ToInt.*': '转换为整数',
        r'^JS_ToUint.*': '转换为无符号整数',
        r'^is_safe_integer': '判断是否为安全整数',
        r'^JS_ToIndex': '转换为数组索引',
        r'^JS_NumberIsInteger': '判断数字是否为整数',
        r'^JS_NumberIsNegativeOrMinusZero': '判断是否为负数或负零',
        r'^js_bigint_to_string': '大整数转字符串',
        r'^JS_ToStringInternal': '内部字符串转换',
        r'^JS_ToString': '转换为字符串',
        r'^JS_ToStringFree': '转换为字符串并释放',
        r'^JS_ToLocaleStringFree': '转换为本地字符串',
        r'^JS_ToPropertyKey': '转换为属性键',
        r'^JS_ToStringCheckObject': '转换为字符串（检查对象）',
        r'^js_putc': '输出字符',
        r'^js_puts': '输出字符串',
        r'^js_print_float64': '打印浮点数',
        r'^js_string_get_length': '获取字符串长度',
        r'^js_print_string': '打印字符串',
        r'^is_ascii_ident': '判断是否为 ASCII 标识符',
    }
    
    # 尝试匹配模板
    for pattern, template in comment_templates.items():
        if re.match(pattern, func_name):
            # 提取函数名中的关键部分
            name_parts = re.sub(r'([A-Z])', r' \1', func_name).split()
            name = '_'.join(name_parts[1:]).lower() if len(name_parts) > 1 else func_name
            
            # 提取 action
            action = name_parts[-1].lower() if len(name_parts) > 1 else '处理'
            
            comment = template.format(name=name, action=action)
            return comment
    
    # 默认注释
    return f'{func_name} 函数'


def generate_doc_comment(func_name, func_type, line_num, lines, start_idx):
    """生成完整的文档注释"""
    
    # 提取参数信息
    params = []
    return_type = func_type.strip()
    
    # 查找函数签名
    func_line = lines[start_idx]
    
    # 简单的参数提取
    param_match = re.search(r'\(([^)]*)\)', func_line)
    if param_match:
        param_str = param_match.group(1)
        if param_str.strip() and param_str.strip() != 'void':
            # 分割参数
            param_parts = param_str.split(',')
            for part in param_parts:
                part = part.strip()
                if part:
                    # 提取参数名
                    param_tokens = part.split()
                    if param_tokens:
                        param_name = param_tokens[-1].strip('*')
                        if param_name and param_name != 'void':
                            params.append(param_name)
    
    # 生成注释
    comment = get_function_comment(func_name, func_type, line_num, func_line)
    
    # 构建文档注释
    doc_lines = []
    doc_lines.append('/**')
    doc_lines.append(f' * {comment}')
    
    # 添加参数说明
    for param in params:
        doc_lines.append(f' * @param {param} 参数说明')
    
    # 添加返回值说明（如果不是 void）
    if return_type and return_type != 'void':
        doc_lines.append(f' * @return 返回值说明')
    
    doc_lines.append(' */')
    
    return '\n'.join(doc_lines) + '\n'


def process_file(filename, max_functions=50):
    """处理文件，为前 max_functions 个缺少注释的函数添加注释"""
    
    with open(filename, 'r', encoding='utf-8', errors='ignore') as f:
        lines = f.readlines()
    
    missing_functions = []
    i = 0
    
    while i < len(lines):
        line = lines[i].rstrip()
        line_num = i + 1
        
        # 检查是否是函数定义
        match = re.match(r'^(static\s+)?([\w\*]+)\s+(\w+)\s*\([^;]*\)\s*\{', line)
        if not match:
            if re.match(r'^(static\s+)?([\w\*]+)\s+(\w+)\s*\([^;]*\)\s*$', line):
                if i+1 < len(lines) and lines[i+1].strip() == '{':
                    match = True
                else:
                    i += 1
                    continue
            else:
                i += 1
                continue
        
        if match:
            func_type = re.match(r'^(static\s+)?([\w\*]+)\s+(\w+)', line).group(2).strip()
            func_name = re.match(r'^(static\s+)?([\w\*]+)\s+(\w+)', line).group(3).strip()
            
            if len(func_name) < 3:
                i += 1
                continue
            
            # 检查是否有文档注释
            has_doc_comment = False
            for j in range(max(0, i-10), i):
                if '/**' in lines[j]:
                    has_doc_comment = True
                    break
            
            if not has_doc_comment:
                missing_functions.append((line_num, func_type, func_name, i))
        
        i += 1
    
    print(f"找到 {len(missing_functions)} 个缺少注释的函数")
    
    # 处理前 max_functions 个
    processed = 0
    insertions = []  # 存储需要插入的注释 (位置，注释内容)
    
    for line_num, func_type, func_name, idx in missing_functions[:max_functions]:
        comment = generate_doc_comment(func_name, func_type, line_num, lines, idx)
        insertions.append((idx, comment))
        processed += 1
        print(f"  [{processed}] {line_num}: {func_type} {func_name}")
    
    # 从后往前插入注释（避免影响索引）
    insertions.sort(key=lambda x: x[0], reverse=True)
    
    for idx, comment in insertions:
        lines.insert(idx, comment)
    
    # 写回文件
    with open(filename, 'w', encoding='utf-8') as f:
        f.writelines(lines)
    
    print(f"\n已为 {processed} 个函数添加注释")
    return processed


if __name__ == '__main__':
    os.chdir('/Users/anql/Documents/JDAI/quickjs')
    
    # 处理 quickjs.c
    count = process_file('quickjs.c', max_functions=50)
    print(f"\n处理完成！共添加 {count} 个函数注释")
