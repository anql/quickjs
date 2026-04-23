/**
 * @file qjs.c
 * @brief QuickJS 独立解释器入口程序
 * 
 * 本文件实现了 QuickJS 引擎的命令行解释器 (CLI)，提供以下功能：
 * 
 * - 执行 JavaScript 文件（支持 ES6 模块和脚本）
 * - 交互式 REPL (Read-Eval-Print Loop)
 * - 内存使用跟踪和统计
 * - 内存限制和栈大小限制
 * - 标准库模块加载（std, os）
 * - Promise 拒绝处理
 * 
 * 使用方法：
 * @code
 * qjs [options] [file [args]]
 * @endcode
 * 
 * 主要选项：
 * - -e, --eval EXPR: 执行表达式
 * - -i, --interactive: 进入交互模式
 * - -m, --module: 作为 ES6 模块加载
 * - -T, --trace: 跟踪内存分配
 * - -d, --dump: 输出内存使用统计
 * - --memory-limit n: 限制内存使用（支持 K/M/G 后缀）
 * - --stack-size n: 限制栈大小
 * - -q, --quit: 仅实例化解释器后退出
 * 
 * @copyright 2017-2021 Fabrice Bellard
 * @copyright 2017-2021 Charlie Gordon
 * @license MIT
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#elif defined(__linux__) || defined(__GLIBC__)
#include <malloc.h>
#elif defined(__FreeBSD__)
#include <malloc_np.h>
#endif

#include "cutils.h"
#include "quickjs-libc.h"

/** @brief REPL (交互式解释器) 的二进制代码（由 qjsc 编译生成） */
extern const uint8_t qjsc_repl[];
/** @brief REPL 二进制代码的大小 */
extern const uint32_t qjsc_repl_size;

/**
 * @brief 评估 JavaScript 代码缓冲区
 * 
 * 本函数执行 JavaScript 代码，支持模块和脚本两种模式。
 * 对于模块，先编译后运行，以便设置 import.meta。
 * 
 * @param ctx JavaScript 上下文
 * @param buf 代码缓冲区指针
 * @param buf_len 代码长度（字节）
 * @param filename 文件名（用于错误报告）
 * @param eval_flags 评估标志（JS_EVAL_TYPE_MODULE 或 JS_EVAL_TYPE_GLOBAL）
 * @return 0 成功，-1 失败（发生异常）
 * 
 * @note 模块模式下会自动设置 import.meta 的 url 和 main 字段
 * @note 发生异常时会调用 js_std_dump_error 输出错误信息
 */
static int eval_buf(JSContext *ctx, const void *buf, int buf_len,
                    const char *filename, int eval_flags)
{
    JSValue val;
    int ret;

    if ((eval_flags & JS_EVAL_TYPE_MASK) == JS_EVAL_TYPE_MODULE) {
        /* 模块模式：先编译后运行，以便设置 import.meta */
        val = JS_Eval(ctx, buf, buf_len, filename,
                      eval_flags | JS_EVAL_FLAG_COMPILE_ONLY);
        if (!JS_IsException(val)) {
            js_module_set_import_meta(ctx, val, TRUE, TRUE);
            val = JS_EvalFunction(ctx, val);
        }
        val = js_std_await(ctx, val);
    } else {
        /* 脚本模式：直接执行 */
        val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
    }
    if (JS_IsException(val)) {
        js_std_dump_error(ctx);
        ret = -1;
    } else {
        ret = 0;
    }
    JS_FreeValue(ctx, val);
    return ret;
}

/**
 * @brief 评估 JavaScript 文件
 * 
 * 从文件加载 JavaScript 代码并执行。自动检测文件类型：
 * - .mjs 后缀：作为模块处理
 * - 其他：通过 JS_DetectModule 检测是否为模块
 * 
 * @param ctx JavaScript 上下文
 * @param filename 文件路径
 * @param module 模块标志：1=模块，0=脚本，-1=自动检测
 * @param strict 严格模式标志
 * @return 0 成功，-1 失败
 * 
 * @note 文件加载失败时会调用 perror 并退出程序
 */
static int eval_file(JSContext *ctx, const char *filename, int module, int strict)
{
    uint8_t *buf;
    int ret, eval_flags;
    size_t buf_len;

    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        perror(filename);
        exit(1);
    }

    /* 自动检测模块类型 */
    if (module < 0) {
        module = (has_suffix(filename, ".mjs") ||
                  JS_DetectModule((const char *)buf, buf_len));
    }
    if (module) {
        eval_flags = JS_EVAL_TYPE_MODULE;
    } else {
        eval_flags = JS_EVAL_TYPE_GLOBAL;
        if (strict)
            eval_flags |= JS_EVAL_FLAG_STRICT;
    }
    ret = eval_buf(ctx, buf, buf_len, filename, eval_flags);
    js_free(ctx, buf);
    return ret;
}

/**
 * @brief 创建自定义 JavaScript 上下文
 * 
 * 初始化上下文并注册系统模块（std 和 os）。
 * 此函数也用于初始化 Worker 上下文。
 * 
 * @param rt JavaScript 运行时
 * @return 新创建的上下文，失败返回 NULL
 * 
 * @note 注册的模块：
 *       - std: 标准库（文件 I/O、文本处理等）
 *       - os: 操作系统接口（进程、信号、定时器等）
 */
static JSContext *JS_NewCustomContext(JSRuntime *rt)
{
    JSContext *ctx;
    ctx = JS_NewContext(rt);
    if (!ctx)
        return NULL;
    /* 系统模块 */
    js_init_module_std(ctx, "std");
    js_init_module_os(ctx, "os");
    return ctx;
}

#if defined(__APPLE__)
#define MALLOC_OVERHEAD  0
#else
#define MALLOC_OVERHEAD  8
#endif

/**
 * @struct trace_malloc_data
 * @brief 内存跟踪数据结构
 * 
 * 用于记录内存分配的基准地址，计算分配偏移量。
 */
struct trace_malloc_data {
    uint8_t *base;  /**< 内存分配基准地址 */
};

/**
 * @brief 计算指针相对于基准的偏移量
 * 
 * @param ptr 目标指针
 * @param dp 跟踪数据
 * @return 偏移量（字节）
 */
static inline unsigned long long js_trace_malloc_ptr_offset(uint8_t *ptr,
                                                struct trace_malloc_data *dp)
{
    return ptr - dp->base;
}

/**
 * @brief 获取可分配的内存大小（平台相关）
 * 
 * 不同平台提供不同的 API 查询已分配内存块的实际大小。
 * 本函数封装了各平台的实现差异。
 * 
 * @param ptr 内存块指针
 * @return 可使用的字节数
 * 
 * @note 平台支持：
 *       - macOS: malloc_size()
 *       - Windows: _msize()
 *       - Linux/glibc: malloc_usable_size()
 *       - Emscripten: 返回 0（不支持）
 */
static size_t js_trace_malloc_usable_size(const void *ptr)
{
#if defined(__APPLE__)
    return malloc_size(ptr);
#elif defined(_WIN32)
    return _msize((void *)ptr);
#elif defined(EMSCRIPTEN)
    return 0;
#elif defined(__linux__) || defined(__GLIBC__)
    return malloc_usable_size((void *)ptr);
#else
    /* 如果编译失败，改为 return 0; */
    return malloc_usable_size((void *)ptr);
#endif
}

/**
 * @brief 格式化输出内存跟踪信息
 * 
 * 自定义 printf 函数，支持 %p（指针）和 %zd（size_t）格式说明符。
 * 指针输出格式：H+ 偏移量。大小（如 H+000123.64）
 * 
 * @param s 内存状态
 * @param fmt 格式字符串
 * @param ... 可变参数
 * 
 * @note 使用 __attribute__((format(printf, 2, 3))) 进行格式检查
 */
static void
#ifdef _WIN32
/* mingw printf 使用 gnu_printf 格式 */
__attribute__((format(gnu_printf, 2, 3)))
#else
__attribute__((format(printf, 2, 3)))
#endif
    js_trace_malloc_printf(JSMallocState *s, const char *fmt, ...)
{
    va_list ap;
    int c;

    va_start(ap, fmt);
    while ((c = *fmt++) != '\0') {
        if (c == '%') {
            /* 仅处理 %p 和 %zd */
            if (*fmt == 'p') {
                uint8_t *ptr = va_arg(ap, void *);
                if (ptr == NULL) {
                    printf("NULL");
                } else {
                    printf("H%+06lld.%zd",
                           js_trace_malloc_ptr_offset(ptr, s->opaque),
                           js_trace_malloc_usable_size(ptr));
                }
                fmt++;
                continue;
            }
            if (fmt[0] == 'z' && fmt[1] == 'd') {
                size_t sz = va_arg(ap, size_t);
                printf("%zd", sz);
                fmt += 2;
                continue;
            }
        }
        putc(c, stdout);
    }
    va_end(ap);
}

/**
 * @brief 初始化内存跟踪
 * 
 * 分配一个小内存块作为基准地址，用于计算后续分配的偏移量。
 * 
 * @param s 跟踪数据结构
 */
static void js_trace_malloc_init(struct trace_malloc_data *s)
{
    free(s->base = malloc(8));
}

/**
 * @brief 跟踪的 malloc 实现
 * 
 * 包装标准 malloc，记录分配信息并检查内存限制。
 * 
 * @param s 内存状态
 * @param size 请求的大小
 * @return 分配的指针，失败返回 NULL
 * 
 * @note 输出格式：A 大小 -> 指针（如 A 64 -> H+000123.64）
 * @note 不分配零字节（平台行为不一致）
 */
static void *js_trace_malloc(JSMallocState *s, size_t size)
{
    void *ptr;

    /* 不分配零字节：平台行为不一致 */
    assert(size != 0);

    if (unlikely(s->malloc_size + size > s->malloc_limit))
        return NULL;
    ptr = malloc(size);
    js_trace_malloc_printf(s, "A %zd -> %p\n", size, ptr);
    if (ptr) {
        s->malloc_count++;
        s->malloc_size += js_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
    }
    return ptr;
}

/**
 * @brief 跟踪的 free 实现
 * 
 * 包装标准 free，记录释放信息。
 * 
 * @param s 内存状态
 * @param ptr 要释放的指针
 * 
 * @note 输出格式：F 指针（如 F H+000123）
 */
static void js_trace_free(JSMallocState *s, void *ptr)
{
    if (!ptr)
        return;

    js_trace_malloc_printf(s, "F %p\n", ptr);
    s->malloc_count--;
    s->malloc_size -= js_trace_malloc_usable_size(ptr) + MALLOC_OVERHEAD;
    free(ptr);
}

/**
 * @brief 跟踪的 realloc 实现
 * 
 * 包装标准 realloc，记录重新分配信息。
 * 
 * @param s 内存状态
 * @param ptr 原指针
 * @param size 新大小
 * @return 新指针，失败返回 NULL
 * 
 * @note 输出格式：R 旧大小 旧指针 -> 新指针
 * @note ptr=NULL 时等同于 malloc
 * @note size=0 时等同于 free
 */
static void *js_trace_realloc(JSMallocState *s, void *ptr, size_t size)
{
    size_t old_size;

    if (!ptr) {
        if (size == 0)
            return NULL;
        return js_trace_malloc(s, size);
    }
    old_size = js_trace_malloc_usable_size(ptr);
    if (size == 0) {
        js_trace_malloc_printf(s, "R %zd %p\n", size, ptr);
        s->malloc_count--;
        s->malloc_size -= old_size + MALLOC_OVERHEAD;
        free(ptr);
        return NULL;
    }
    if (s->malloc_size + size - old_size > s->malloc_limit)
        return NULL;

    js_trace_malloc_printf(s, "R %zd %p", size, ptr);

    ptr = realloc(ptr, size);
    js_trace_malloc_printf(s, " -> %p\n", ptr);
    if (ptr) {
        s->malloc_size += js_trace_malloc_usable_size(ptr) - old_size;
    }
    return ptr;
}

/** @brief 跟踪内存分配函数表 */
static const JSMallocFunctions trace_mf = {
    js_trace_malloc,
    js_trace_free,
    js_trace_realloc,
    js_trace_malloc_usable_size,
};

/**
 * @brief 解析带后缀的大小字符串
 * 
 * 支持 SI 后缀：K/k (千), M (兆), G (吉)
 * 
 * @param str 输入字符串（如 "1024", "1K", "2M", "1G"）
 * @return 字节数
 * 
 * @note 无效后缀会导致程序退出
 */
static size_t get_suffixed_size(const char *str)
{
    char *p;
    size_t v;
    v = (size_t)strtod(str, &p);
    switch(*p) {
    case 'G':
        v <<= 30;  /* 1G = 2^30 bytes */
        break;
    case 'M':
        v <<= 20;  /* 1M = 2^20 bytes */
        break;
    case 'k':
    case 'K':
        v <<= 10;  /* 1K = 2^10 bytes */
        break;
    default:
        if (*p != '\0') {
            fprintf(stderr, "qjs: invalid suffix: %s\n", p);
            exit(1);
        }
        break;
    }
    return v;
}

#define PROG_NAME "qjs"

/**
 * @brief 显示帮助信息
 * 
 * 输出命令行选项说明并退出程序。
 * 
 * @note 使用 exit(1) 退出，因为这是帮助信息而非错误
 */
void help(void)
{
    printf("QuickJS version " CONFIG_VERSION "\n"
           "usage: " PROG_NAME " [options] [file [args]]\n"
           "-h  --help         list options\n"
           "-e  --eval EXPR    evaluate EXPR\n"
           "-i  --interactive  go to interactive mode\n"
           "-m  --module       load as ES6 module (default=autodetect)\n"
           "    --script       load as ES6 script (default=autodetect)\n"
           "    --strict       force strict mode\n"
           "-I  --include file include an additional file\n"
           "    --std          make 'std' and 'os' available to the loaded script\n"
           "-T  --trace        trace memory allocation\n"
           "-d  --dump         dump the memory usage stats\n"
           "    --memory-limit n  limit the memory usage to 'n' bytes (SI suffixes allowed)\n"
           "    --stack-size n    limit the stack size to 'n' bytes (SI suffixes allowed)\n"
           "    --no-unhandled-rejection  ignore unhandled promise rejections\n"
           "-s                    strip all the debug info\n"
           "    --strip-source    strip the source code\n"
           "-q  --quit         just instantiate the interpreter and quit\n");
    exit(1);
}

/**
 * @brief QuickJS 解释器主入口
 * 
 * 主函数负责：
 * 1. 解析命令行参数
 * 2. 创建运行时和上下文
 * 3. 加载并执行 JavaScript 代码
 * 4. 进入事件循环（交互模式或模块执行）
 * 5. 清理资源并退出
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 0 成功，1 失败
 * 
 * @note 不使用 getopt 是因为需要将剩余参数传递给脚本
 * @note 支持多种执行模式：文件执行、表达式执行、交互模式
 */
int main(int argc, char **argv)
{
    JSRuntime *rt;
    JSContext *ctx;
    struct trace_malloc_data trace_data = { NULL };
    int optind;
    char *expr = NULL;
    int interactive = 0;
    int dump_memory = 0;
    int trace_memory = 0;
    int empty_run = 0;
    int module = -1;
    int strict = 0;
    int load_std = 0;
    int dump_unhandled_promise_rejection = 1;
    size_t memory_limit = 0;
    char *include_list[32];
    int i, include_count = 0;
    int strip_flags = 0;
    size_t stack_size = 0;

    /* 不能使用 getopt，因为需要将命令行参数传递给脚本 */
    optind = 1;
    while (optind < argc && *argv[optind] == '-') {
        char *arg = argv[optind] + 1;
        const char *longopt = "";
        /* a single - is not an option, it also stops argument scanning */
        if (!*arg)
            break;
        optind++;
        if (*arg == '-') {
            longopt = arg + 1;
            arg += strlen(arg);
            /* -- stops argument scanning */
            if (!*longopt)
                break;
        }
        for (; *arg || *longopt; longopt = "") {
            char opt = *arg;
            if (opt)
                arg++;
            if (opt == 'h' || opt == '?' || !strcmp(longopt, "help")) {
                help();
                continue;
            }
            if (opt == 'e' || !strcmp(longopt, "eval")) {
                if (*arg) {
                    expr = arg;
                    break;
                }
                if (optind < argc) {
                    expr = argv[optind++];
                    break;
                }
                fprintf(stderr, "qjs: missing expression for -e\n");
                exit(2);
            }
            if (opt == 'I' || !strcmp(longopt, "include")) {
                if (optind >= argc) {
                    fprintf(stderr, "expecting filename");
                    exit(1);
                }
                if (include_count >= countof(include_list)) {
                    fprintf(stderr, "too many included files");
                    exit(1);
                }
                include_list[include_count++] = argv[optind++];
                continue;
            }
            if (opt == 'i' || !strcmp(longopt, "interactive")) {
                interactive++;
                continue;
            }
            if (opt == 'm' || !strcmp(longopt, "module")) {
                module = 1;
                continue;
            }
            if (!strcmp(longopt, "script")) {
                module = 0;
                continue;
            }
            if (!strcmp(longopt, "strict")) {
                strict = 1;
                continue;
            }
            if (opt == 'd' || !strcmp(longopt, "dump")) {
                dump_memory++;
                continue;
            }
            if (opt == 'T' || !strcmp(longopt, "trace")) {
                trace_memory++;
                continue;
            }
            if (!strcmp(longopt, "std")) {
                load_std = 1;
                continue;
            }
            if (!strcmp(longopt, "no-unhandled-rejection")) {
                dump_unhandled_promise_rejection = 0;
                continue;
            }
            if (opt == 'q' || !strcmp(longopt, "quit")) {
                empty_run++;
                continue;
            }
            if (!strcmp(longopt, "memory-limit")) {
                if (optind >= argc) {
                    fprintf(stderr, "expecting memory limit");
                    exit(1);
                }
                memory_limit = get_suffixed_size(argv[optind++]);
                continue;
            }
            if (!strcmp(longopt, "stack-size")) {
                if (optind >= argc) {
                    fprintf(stderr, "expecting stack size");
                    exit(1);
                }
                stack_size = get_suffixed_size(argv[optind++]);
                continue;
            }
            if (opt == 's') {
                strip_flags = JS_STRIP_DEBUG;
                continue;
            }
            if (!strcmp(longopt, "strip-source")) {
                strip_flags = JS_STRIP_SOURCE;
                continue;
            }
            if (opt) {
                fprintf(stderr, "qjs: unknown option '-%c'\n", opt);
            } else {
                fprintf(stderr, "qjs: unknown option '--%s'\n", longopt);
            }
            help();
        }
    }

    if (trace_memory) {
        js_trace_malloc_init(&trace_data);
        rt = JS_NewRuntime2(&trace_mf, &trace_data);
    } else {
        rt = JS_NewRuntime();
    }
    if (!rt) {
        fprintf(stderr, "qjs: cannot allocate JS runtime\n");
        exit(2);
    }
    /* 设置内存限制 */
    if (memory_limit != 0)
        JS_SetMemoryLimit(rt, memory_limit);
    /* 设置栈大小限制 */
    if (stack_size != 0)
        JS_SetMaxStackSize(rt, stack_size);
    /* 设置剥离信息（调试信息/源代码） */
    JS_SetStripInfo(rt, strip_flags);
    /* 设置 Worker 上下文创建函数 */
    js_std_set_worker_new_context_func(JS_NewCustomContext);
    /* 初始化标准处理器（信号、定时器等） */
    js_std_init_handlers(rt);
    /* 创建 JavaScript 上下文 */
    ctx = JS_NewCustomContext(rt);
    if (!ctx) {
        fprintf(stderr, "qjs: cannot allocate JS context\n");
        exit(2);
    }

    /* 设置 ES6 模块加载器 */
    JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader, js_module_check_attributes, NULL);

    /* 设置 Promise 拒绝追踪器 */
    if (dump_unhandled_promise_rejection) {
        JS_SetHostPromiseRejectionTracker(rt, js_std_promise_rejection_tracker,
                                          NULL);
    }

    if (!empty_run) {
        /* 添加命令行参数到全局对象 (globalThis.argv) */
        js_std_add_helpers(ctx, argc - optind, argv + optind);

        /* 使 'std' 和 'os' 模块对非模块代码可见 */
        if (load_std) {
            const char *str = "import * as std from 'std';\n"
                "import * as os from 'os';\n"
                "globalThis.std = std;\n"
                "globalThis.os = os;\n";
            eval_buf(ctx, str, strlen(str), "<input>", JS_EVAL_TYPE_MODULE);
        }

        /* 执行 include 文件 */
        for(i = 0; i < include_count; i++) {
            if (eval_file(ctx, include_list[i], 0, strict))
                goto fail;
        }

        /* 执行表达式或文件 */
        if (expr) {
            int eval_flags;
            if (module > 0) {
                eval_flags = JS_EVAL_TYPE_MODULE;
            } else {
                eval_flags = JS_EVAL_TYPE_GLOBAL;
                if (strict)
                    eval_flags |= JS_EVAL_FLAG_STRICT;
            }
            if (eval_buf(ctx, expr, strlen(expr), "<cmdline>", eval_flags))
                goto fail;
        } else
        if (optind >= argc) {
            /* 没有文件参数，进入交互模式 */
            interactive = 1;
        } else {
            const char *filename;
            filename = argv[optind];
            if (eval_file(ctx, filename, module, strict))
                goto fail;
        }
        /* 启动交互式 REPL */
        if (interactive) {
            JS_SetHostPromiseRejectionTracker(rt, NULL, NULL);
            js_std_eval_binary(ctx, qjsc_repl, qjsc_repl_size, 0);
        }
        /* 进入事件循环 */
        js_std_loop(ctx);
    }

    /* 输出内存使用统计 */
    if (dump_memory) {
        JSMemoryUsage stats;
        JS_ComputeMemoryUsage(rt, &stats);
        JS_DumpMemoryUsage(stdout, &stats, rt);
    }
    /* 清理资源 */
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    /* 空运行性能测试：测量实例化时间 */
    if (empty_run && dump_memory) {
        clock_t t[5];
        double best[5];
        int i, j;
        for (i = 0; i < 100; i++) {
            t[0] = clock();
            rt = JS_NewRuntime();
            t[1] = clock();
            ctx = JS_NewContext(rt);
            t[2] = clock();
            JS_FreeContext(ctx);
            t[3] = clock();
            JS_FreeRuntime(rt);
            t[4] = clock();
            for (j = 4; j > 0; j--) {
                double ms = 1000.0 * (t[j] - t[j - 1]) / CLOCKS_PER_SEC;
                if (i == 0 || best[j] > ms)
                    best[j] = ms;
            }
        }
        printf("\nInstantiation times (ms): %.3f = %.3f+%.3f+%.3f+%.3f\n",
               best[1] + best[2] + best[3] + best[4],
               best[1], best[2], best[3], best[4]);
    }
    return 0;
    
fail:
    /* 错误处理：清理资源 */
    js_std_free_handlers(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 1;
}
