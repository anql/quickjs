/*
 * QuickJS C library - QuickJS 标准库绑定头文件
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
#ifndef QUICKJS_LIBC_H
#define QUICKJS_LIBC_H

#include <stdio.h>
#include <stdlib.h>

#include "quickjs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化标准库模块 (std)
 * 
 * 注册 QuickJS 的标准库模块，提供文件操作、I/O、基础工具等函数。
 * 这是 quickjs-libc.c 中实现的 std 模块的入口函数。
 * 
 * @param ctx JS 上下文指针
 * @param module_name 模块名称 (通常为 "std")
 * @return 模块定义指针，失败返回 NULL
 * 
 * @see js_init_module_os - OS 模块初始化
 */
JSModuleDef *js_init_module_std(JSContext *ctx, const char *module_name);

/**
 * @brief 初始化操作系统模块 (os)
 * 
 * 注册 QuickJS 的 OS 模块，提供系统调用接口，如：
 * - 文件描述符操作 (open/close/read/write/seek)
 * - 进程管理 (exec/waitpid/getpid)
 * - 信号处理
 * - 定时器
 * - 事件轮询
 * 
 * @param ctx JS 上下文指针
 * @param module_name 模块名称 (通常为 "os")
 * @return 模块定义指针，失败返回 NULL
 * 
 * @see js_init_module_std - 标准库模块初始化
 */
JSModuleDef *js_init_module_os(JSContext *ctx, const char *module_name);

/**
 * @brief 添加标准辅助函数到全局对象
 * 
 * 向 JS 上下文添加辅助函数，包括：
 * - print/printf: 打印输出
 * - load/eval: 加载和执行脚本
 * - 命令行参数处理
 * - 错误处理辅助
 * 
 * 通常在 REPL 或脚本执行前调用，提供便利的全局函数。
 * 
 * @param ctx JS 上下文指针
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * 
 * @see js_std_loop - 主事件循环
 */
void js_std_add_helpers(JSContext *ctx, int argc, char **argv);

/**
 * @brief 运行 JS 主事件循环
 * 
 * 执行 JS 代码的主循环，处理：
 * - Promise 微任务
 * - 定时器回调
 * - 信号处理
 * - 异步 I/O 事件
 * 
 * 该函数会阻塞直到所有待处理的作业完成或运行时被销毁。
 * 适用于需要事件驱动的场景 (如服务器、GUI 应用)。
 * 
 * @param ctx JS 上下文指针
 * 
 * @see js_std_await - 等待单个 Promise
 * @see js_std_init_handlers - 初始化事件处理器
 */
void js_std_loop(JSContext *ctx);

/**
 * @brief 等待 Promise 完成并返回结果
 * 
 * 阻塞等待一个 Promise 对象完成 (resolve 或 reject)，
 * 返回最终结果或抛出异常。
 * 
 * 用于同步风格地处理异步操作，避免回调地狱。
 * 
 * @param ctx JS 上下文指针
 * @param obj 要等待的 Promise 对象
 * @return Promise 的 resolve 值，reject 时抛出异常
 * 
 * @see js_std_loop - 运行完整事件循环
 */
JSValue js_std_await(JSContext *ctx, JSValue obj);

/**
 * @brief 初始化运行时的事件处理器
 * 
 * 设置运行时 (JSRuntime) 级别的事件处理机制，包括：
 * - 信号处理器注册
 * - 定时器管理
 * - 文件描述符轮询
 * 
 * 应在创建运行时后、执行任何 JS 代码前调用。
 * 
 * @param rt JS 运行时指针
 * 
 * @see js_std_free_handlers - 释放事件处理器
 * @see js_std_loop - 运行事件循环
 */
void js_std_init_handlers(JSRuntime *rt);

/**
 * @brief 释放运行时的事件处理器资源
 * 
 * 清理由 js_std_init_handlers 注册的所有事件处理器，
 * 包括取消信号处理、关闭定时器、释放文件描述符监听等。
 * 
 * 应在销毁运行时前调用，避免资源泄漏。
 * 
 * @param rt JS 运行时指针
 * 
 * @see js_std_init_handlers - 初始化事件处理器
 */
void js_std_free_handlers(JSRuntime *rt);

/**
 * @brief 打印错误信息到 stderr
 * 
 * 将 JS 异常/错误信息格式化输出到标准错误流，
 * 包括错误类型、消息和堆栈跟踪。
 * 
 * 用于调试和错误报告，通常在捕获异常后调用。
 * 
 * @param ctx JS 上下文指针
 * 
 * @see JS_Throw - 抛出异常
 * @see JS_GetException - 获取异常
 */
void js_std_dump_error(JSContext *ctx);

/**
 * @brief 加载文件内容为字节数组
 * 
 * 读取指定文件的全部内容到内存缓冲区。
 * 自动处理文件打开、读取、关闭，返回分配的缓冲区。
 * 
 * @param ctx JS 上下文指针 (用于内存分配)
 * @param pbuf_len 输出参数：返回的缓冲区长度
 * @param filename 文件路径
 * @return 分配的缓冲区指针，失败返回 NULL
 * @note 调用者负责使用 js_free 释放返回的缓冲区
 * 
 * @see js_free - 释放 JS 分配的内存
 */
uint8_t *js_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename);

/**
 * @brief 设置模块的 import.meta 元信息
 * 
 * 为 ES 模块设置 import.meta 对象，包括：
 * - url: 模块的 URL 或文件路径
 * - main: 是否为主模块
 * 
 * 在模块初始化时调用，使模块能访问自身的元数据。
 * 
 * @param ctx JS 上下文指针
 * @param func_val 模块函数值
 * @param use_realpath 是否使用绝对路径
 * @param is_main 是否为主模块
 * @return 0 成功，-1 失败
 */
int js_module_set_import_meta(JSContext *ctx, JSValueConst func_val,
                              JS_BOOL use_realpath, JS_BOOL is_main);

/**
 * @brief 测试模块导入属性是否为 JSON 模块
 * 
 * 检查 import 语句中的属性对象，判断是否请求 JSON 模块加载。
 * 用于支持 `import json from "./data.json" with { type: "json" }` 语法。
 * 
 * @param ctx JS 上下文指针
 * @param attributes 导入属性对象
 * @return 1 是 JSON 模块，0 不是
 */
int js_module_test_json(JSContext *ctx, JSValueConst attributes);

/**
 * @brief 检查模块导入属性的有效性
 * 
 * 验证 import 语句中的属性对象是否合法，
 * 支持的属性类型包括 "json" 等。
 * 
 * @param ctx JS 上下文指针
 * @param opaque 不透明指针 (未使用)
 * @param attributes 导入属性对象
 * @return 0 有效，-1 无效
 */
int js_module_check_attributes(JSContext *ctx, void *opaque, JSValueConst attributes);

/**
 * @brief 模块加载器函数
 * 
 * QuickJS 的模块加载入口，负责：
 * - 解析模块路径
 * - 读取模块文件内容
 * - 编译并返回模块定义
 * 
 * 支持标准 JS 模块和 JSON 模块。
 * 通过 js_std_add_helpers 注册为默认加载器。
 * 
 * @param ctx JS 上下文指针
 * @param module_name 模块名称 (导入路径)
 * @param opaque 不透明指针 (未使用)
 * @param attributes 导入属性
 * @return 模块定义指针，失败返回 NULL
 * 
 * @see js_module_set_import_meta - 设置模块元信息
 */
JSModuleDef *js_module_loader(JSContext *ctx,
                              const char *module_name, void *opaque,
                              JSValueConst attributes);

/**
 * @brief 执行二进制字节码
 * 
 * 加载并执行预编译的 JS 字节码 (由 qjsc 编译器生成)。
 * 支持同步和异步执行模式。
 * 
 * @param ctx JS 上下文指针
 * @param buf 字节码缓冲区
 * @param buf_len 缓冲区长度
 * @param flags 执行标志 (如 JS_EVAL_FLAG_STRICT)
 * 
 * @see qjsc - QuickJS 编译器
 * @see js_std_eval_binary_json_module - 执行 JSON 模块字节码
 */
void js_std_eval_binary(JSContext *ctx, const uint8_t *buf, size_t buf_len,
                        int flags);

/**
 * @brief 执行 JSON 模块的二进制字节码
 * 
 * 专门用于加载预编译的 JSON 模块字节码。
 * 将 JSON 数据作为 ES 模块导出。
 * 
 * @param ctx JS 上下文指针
 * @param buf 字节码缓冲区
 * @param buf_len 缓冲区长度
 * @param module_name 模块名称
 * 
 * @see js_std_eval_binary - 执行普通字节码
 */
void js_std_eval_binary_json_module(JSContext *ctx,
                                    const uint8_t *buf, size_t buf_len,
                                    const char *module_name);

/**
 * @brief Promise 拒绝追踪器
 * 
 * 跟踪未处理的 Promise 拒绝 (rejection)，
 * 用于检测未捕获的异步错误。
 * 
 * 当 Promise 被拒绝且没有 .catch() 处理器时调用，
 * 可打印警告信息帮助调试。
 * 
 * @param ctx JS 上下文指针
 * @param promise 被拒绝的 Promise 对象
 * @param reason 拒绝原因 (错误对象)
 * @param is_handled 是否已被处理 (有 .catch())
 * @param opaque 不透明指针 (未使用)
 * 
 * @see JS_SetHostPromiseRejectionTracker - 设置追踪器
 */
void js_std_promise_rejection_tracker(JSContext *ctx, JSValueConst promise,
                                      JSValueConst reason,
                                      JS_BOOL is_handled, void *opaque);

/**
 * @brief 设置 Worker 新上下文创建函数
 * 
 * 为 Web Worker 支持设置自定义的上下文创建函数。
 * 当创建 Worker 时，使用此函数创建新的 JSContext。
 * 
 * @param func 上下文创建函数指针
 * @note 仅在使用 Worker 功能时需要设置
 * 
 * @see JS_NewContext - 创建上下文
 */
void js_std_set_worker_new_context_func(JSContext *(*func)(JSRuntime *rt));

#ifdef __cplusplus
} /* extern "C" { */
#endif

#endif /* QUICKJS_LIBC_H */
