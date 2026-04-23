/*
 * ECMA Test 262 Runner for QuickJS
 * 
 * Test262 是 ECMAScript 官方测试套件，用于验证 JavaScript 引擎是否符合 ECMA-262 规范。
 * 本文件实现了 Test262 测试运行器，支持：
 * - 严格模式/非严格模式测试
 * - 异步测试（async/await、Promise）
 * - 模块测试（ES6 modules）
 * - 多线程 Agent 测试（SharedArrayBuffer、Atomics）
 * - 错误检测与报告
 * - 内存使用统计
 *
 * Copyright (c) 2017-2021 Fabrice Bellard
 * Copyright (c) 2017-2021 Charlie Gordon
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
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <dirent.h>
#include <ftw.h>

#include "cutils.h"
#include "list.h"
#include "quickjs-libc.h"

/* 启用 Test262 多线程 Agent 支持，用于测试 SharedArrayBuffer 和 Atomics */
#define CONFIG_AGENT

#define CMD_NAME "run-test262"

/**
 * 名称列表数据结构 - 用于管理测试文件列表、排除列表等
 * 支持动态扩容、排序、去重、二分查找
 */
typedef struct namelist_t {
    char **array;     /* 字符串数组 */
    int count;        /* 当前元素个数 */
    int size;         /* 数组容量 */
    unsigned int sorted : 1;  /* 是否已排序标记 */
} namelist_t;

/* 全局列表变量 */
namelist_t test_list;         /* 测试文件列表 */
namelist_t exclude_list;      /* 排除的测试文件列表 */
namelist_t exclude_dir_list;  /* 排除的目录列表 */

FILE *outfile;  /* 输出文件句柄（测试报告） */

/**
 * 测试模式枚举
 * 控制测试在严格模式/非严格模式下运行
 */
enum test_mode_t {
    TEST_DEFAULT_NOSTRICT,  /* 默认非严格模式，除非测试标记为 strictonly */
    TEST_DEFAULT_STRICT,    /* 默认严格模式，除非测试标记为 nostrict */
    TEST_NOSTRICT,          /* 仅非严格模式，跳过 strictonly 测试 */
    TEST_STRICT,            /* 仅严格模式，跳过 nostrict 测试 */
    TEST_ALL,               /* 严格和非严格模式都运行（除非规范限制） */
} test_mode = TEST_DEFAULT_NOSTRICT;

/* 全局配置选项 */
int compact;                /* 紧凑输出模式（进度指示器） */
int show_timings;           /* 显示测试耗时 */
int skip_async;             /* 跳过异步测试 */
int skip_module;            /* 跳过模块测试 */
int new_style;              /* 使用新样式 harness（YAML frontmatter） */
int dump_memory;            /* 输出内存使用统计 */
int stats_count;            /* 测试计数（用于统计平均） */
JSMemoryUsage stats_all, stats_avg, stats_min, stats_max;  /* 内存统计 */
char *stats_min_filename;   /* 最小内存使用文件名 */
char *stats_max_filename;   /* 最大内存使用文件名 */
int verbose;                /* 详细模式（输出错误信息） */
char *harness_dir;          /* harness 文件目录 */
char *harness_exclude;      /* 排除的 harness 文件 */
char *harness_features;     /* 启用的特性列表 */
char *harness_skip_features;  /* 跳过的特性列表 */
int *harness_skip_features_count;  /* 跳过特性计数数组 */
char *error_filename;       /* 错误文件名 */
char *error_file;           /* 错误文件内容 */
FILE *error_out;            /* 错误输出文件句柄 */
char *report_filename;      /* 报告文件名 */
int update_errors;          /* 更新错误文件标记 */
int test_count, test_failed, test_index, test_skipped, test_excluded;  /* 测试统计 */
int new_errors, changed_errors, fixed_errors;  /* 错误变化统计 */
int async_done;             /* 异步测试完成计数器（$DONE() 调用次数） */

/* 函数声明（带 printf 格式检查属性） */
void warning(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
void fatal(int, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));

/**
 * 警告输出函数
 * 格式：run-test262: <消息>
 */
void warning(const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    fprintf(stderr, "%s: ", CMD_NAME);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/**
 * 致命错误输出函数 - 打印错误消息并退出程序
 * @param errcode 退出码
 * @param fmt 格式化字符串
 */
void fatal(int errcode, const char *fmt, ...)
{
    va_list ap;

    fflush(stdout);
    fprintf(stderr, "%s: ", CMD_NAME);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);

    exit(errcode);
}

/**
 * POSIX 错误输出并退出
 * 用于处理 fopen/fread 等系统调用失败
 */
void perror_exit(int errcode, const char *s)
{
    fflush(stdout);
    fprintf(stderr, "%s: ", CMD_NAME);
    perror(s);
    exit(errcode);
}

/**
 * 复制指定长度的字符串
 * @param str 源字符串
 * @param len 复制长度
 * @return 新分配的字符串（需手动 free）
 */
char *strdup_len(const char *str, int len)
{
    char *p = malloc(len + 1);
    memcpy(p, str, len);
    p[len] = '\0';
    return p;
}

/**
 * 字符串比较（内部使用，避免重复包含 strcmp）
 */
static inline int str_equal(const char *a, const char *b) {
    return !strcmp(a, b);
}

/**
 * 字符串追加函数 - 将字符串追加到现有字符串后，带分隔符
 * @param pp 指向字符串指针的指针（会自动更新）
 * @param sep 分隔符（如空格、逗号）
 * @param str 要追加的字符串
 * @return 新字符串指针
 * 
 * 示例：str_append(&features, " ", "async") => "async"
 *       str_append(&features, " ", "module") => "async module"
 */
char *str_append(char **pp, const char *sep, const char *str) {
    char *res, *p;
    size_t len = 0;
    p = *pp;
    if (p) {
        len = strlen(p) + strlen(sep);
    }
    res = malloc(len + strlen(str) + 1);
    if (p) {
        strcpy(res, p);
        strcat(res, sep);
    }
    strcpy(res + len, str);
    free(p);
    return *pp = res;
}

/**
 * 去除字符串首尾空白字符（原地修改）
 * @param p 输入字符串
 * @return 去除空白后的字符串指针
 */
char *str_strip(char *p)
{
    size_t len = strlen(p);
    while (len > 0 && isspace((unsigned char)p[len - 1]))
        p[--len] = '\0';
    while (isspace((unsigned char)*p))
        p++;
    return p;
}

/**
 * 检查字符串是否以指定前缀开头
 */
int has_prefix(const char *str, const char *prefix)
{
    return !strncmp(str, prefix, strlen(prefix));
}

/**
 * 跳过字符串前缀（如果匹配）
 * @param str 原字符串
 * @param prefix 要跳过的前缀
 * @return 跳过前缀后的指针（如果不匹配则返回原指针）
 */
char *skip_prefix(const char *str, const char *prefix)
{
    int i;
    for (i = 0;; i++) {
        if (prefix[i] == '\0') {  /* 前缀匹配完成，跳过 */
            str += i;
            break;
        }
        if (str[i] != prefix[i])
            break;
    }
    return (char *)str;
}

/**
 * 获取文件名的目录部分
 * 示例：get_basename("/path/to/file.js") => "/path/to"
 */
char *get_basename(const char *filename)
{
    char *p;

    p = strrchr(filename, '/');
    if (!p)
        return NULL;
    return strdup_len(filename, p - filename);
}

/**
 * 组合路径 - 将目录路径和文件名组合成完整路径
 * @param path 目录路径
 * @param name 文件名
 * @return 完整路径（需手动 free）
 * 
 * 示例：compose_path("/test262", "test.js") => "/test262/test.js"
 *       compose_path("", "/abs.js") => "/abs.js"  (绝对路径直接返回)
 */
char *compose_path(const char *path, const char *name)
{
    int path_len, name_len;
    char *d, *q;

    if (!path || path[0] == '\0' || *name == '/') {
        d = strdup(name);
    } else {
        path_len = strlen(path);
        name_len = strlen(name);
        d = malloc(path_len + 1 + name_len + 1);
        if (d) {
            q = d;
            memcpy(q, path, path_len);
            q += path_len;
            if (path[path_len - 1] != '/')
                *q++ = '/';
            memcpy(q, name, name_len + 1);
        }
    }
    return d;
}

/**
 * 名称列表比较函数 - 改进的字典序比较
 * 特点：数字部分按数值大小比较（而非字符比较）
 * 示例："test2.js" < "test10.js" （正确）
 *      而非 "test10.js" < "test2.js" （错误，普通字典序）
 */
int namelist_cmp(const char *a, const char *b)
{
    /* 按改进的字典序比较字符串 */
    for (;;) {
        int ca = (unsigned char)*a++;
        int cb = (unsigned char)*b++;
        if (isdigit(ca) && isdigit(cb)) {
            /* 数字部分：解析完整数字后比较数值 */
            int na = ca - '0';
            int nb = cb - '0';
            while (isdigit(ca = (unsigned char)*a++))
                na = na * 10 + ca - '0';
            while (isdigit(cb = (unsigned char)*b++))
                nb = nb * 10 + cb - '0';
            if (na < nb)
                return -1;
            if (na > nb)
                return +1;
        }
        if (ca < cb)
            return -1;
        if (ca > cb)
            return +1;
        if (ca == '\0')
            return 0;
    }
}

/**
 * qsort 回调函数 - 间接比较（用于指针数组）
 */
int namelist_cmp_indirect(const void *a, const void *b)
{
    return namelist_cmp(*(const char **)a, *(const char **)b);
}

/**
 * 名称列表排序 - 使用改进字典序，排序后去重
 * @param lp 要排序的列表
 */
void namelist_sort(namelist_t *lp)
{
    int i, count;
    if (lp->count > 1) {
        qsort(lp->array, lp->count, sizeof(*lp->array), namelist_cmp_indirect);
        /* 去重：删除重复项 */
        for (count = i = 1; i < lp->count; i++) {
            if (namelist_cmp(lp->array[count - 1], lp->array[i]) == 0) {
                free(lp->array[i]);  /* 释放重复项 */
            } else {
                lp->array[count++] = lp->array[i];
            }
        }
        lp->count = count;
    }
    lp->sorted = 1;
}

/**
 * 名称列表查找 - 二分查找（自动排序如果未排序）
 * @param lp 列表
 * @param name 要查找的名称
 * @return 找到返回索引，否则返回 -1
 */
int namelist_find(namelist_t *lp, const char *name)
{
    int a, b, m, cmp;

    if (!lp->sorted) {
        namelist_sort(lp);
    }
    /* 二分查找 */
    for (a = 0, b = lp->count; a < b;) {
        m = a + (b - a) / 2;
        cmp = namelist_cmp(lp->array[m], name);
        if (cmp < 0)
            a = m + 1;
        else if (cmp > 0)
            b = m;
        else
            return m;  /* 找到 */
    }
    return -1;
}

/**
 * 名称列表添加 - 添加文件路径到列表
 * @param lp 目标列表
 * @param base 基础路径（目录）
 * @param name 文件名
 * 
 * 示例：namelist_add(lp, "/test262", "test.js") => 添加 "/test262/test.js"
 */
void namelist_add(namelist_t *lp, const char *base, const char *name)
{
    char *s;

    s = compose_path(base, name);
    if (!s)
        goto fail;
    if (lp->count == lp->size) {
        /* 扩容：1.5 倍 + 4 个余量 */
        size_t newsize = lp->size + (lp->size >> 1) + 4;
        char **a = realloc(lp->array, sizeof(lp->array[0]) * newsize);
        if (!a)
            goto fail;
        lp->array = a;
        lp->size = newsize;
    }
    lp->array[lp->count] = s;
    lp->count++;
    return;
fail:
    fatal(1, "allocation failure\n");
}

/**
 * 从文件加载名称列表
 * 文件格式：每行一个路径，支持 # 和 ; 注释
 * @param lp 目标列表
 * @param filename 列表文件路径
 */
void namelist_load(namelist_t *lp, const char *filename)
{
    char buf[1024];
    char *base_name;
    FILE *f;

    f = fopen(filename, "rb");
    if (!f) {
        perror_exit(1, filename);
    }
    base_name = get_basename(filename);

    while (fgets(buf, sizeof(buf), f) != NULL) {
        char *p = str_strip(buf);
        if (*p == '#' || *p == ';' || *p == '\0')
            continue;  /* 跳过注释行和空行 */

        namelist_add(lp, base_name, p);
    }
    free(base_name);
    fclose(f);
}

/**
 * 从错误文件中提取测试文件名
 * 解析格式：filename.js:line: message
 * @param lp 目标列表
 * @param file 错误文件内容（整个文件读入内存）
 */
void namelist_add_from_error_file(namelist_t *lp, const char *file)
{
    const char *p, *p0;
    char *pp;

    /* 查找所有 ".js:" 模式，提取文件名 */
    for (p = file; (p = strstr(p, ".js:")) != NULL; p++) {
        /* 向前找到行首 */
        for (p0 = p; p0 > file && p0[-1] != '\n'; p0--)
            continue;
        pp = strdup_len(p0, p + 3 - p0);
        namelist_add(lp, NULL, pp);
        free(pp);
    }
}

/**
 * 释放名称列表
 */
void namelist_free(namelist_t *lp)
{
    while (lp->count > 0) {
        free(lp->array[--lp->count]);
    }
    free(lp->array);
    lp->array = NULL;
    lp->size = 0;
}

/**
 * ftw 回调函数 - 添加测试文件到列表
 * 过滤条件：.js 文件，排除 _FIXTURE.js
 */
static int add_test_file(const char *filename, const struct stat *ptr, int flag)
{
    namelist_t *lp = &test_list;
    if (has_suffix(filename, ".js") && !has_suffix(filename, "_FIXTURE.js"))
        namelist_add(lp, NULL, filename);
    return 0;
}

/**
 * 遍历目录树，查找所有测试文件并排序
 * @param path 起始目录
 */
static void enumerate_tests(const char *path)
{
    namelist_t *lp = &test_list;
    int start = lp->count;
    ftw(path, add_test_file, 100);  /* 最多同时打开 100 个文件描述符 */
    qsort(lp->array + start, lp->count - start, sizeof(*lp->array),
          namelist_cmp_indirect);
}

/**
 * JS_PrintValue 回调 - 写入文件
 */
static void js_print_value_write(void *opaque, const char *buf, size_t len)
{
    FILE *fo = opaque;
    fwrite(buf, 1, len, fo);
}

/**
 * JS print() 函数实现 - 用于测试输出
 * 特殊处理 Test262 异步测试完成标记：
 * - "Test262:AsyncTestComplete" => async_done++
 * - "Test262:AsyncTestFailure*" => async_done=2（强制错误）
 */
static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    int i;
    JSValueConst v;
    
    if (outfile) {
        for (i = 0; i < argc; i++) {
            if (i != 0)
                fputc(' ', outfile);
            v = argv[i];
            if (JS_IsString(v)) {
                const char *str;
                size_t len;
                str = JS_ToCStringLen(ctx, &len, v);
                if (!str)
                    return JS_EXCEPTION;
                /* 检测异步测试完成标记 */
                if (!strcmp(str, "Test262:AsyncTestComplete")) {
                    async_done++;
                } else if (strstart(str, "Test262:AsyncTestFailure", NULL)) {
                    async_done = 2; /* 强制标记为错误 */
                }
                fwrite(str, 1, len, outfile);
                JS_FreeCString(ctx, str);
            } else {
                /* 非字符串值：使用 JS_PrintValue 格式化输出 */
                JS_PrintValue(ctx, js_print_value_write, outfile, v, NULL);
            }
        }
        fputc('\n', outfile);
    }
    return JS_UNDEFINED;
}

/**
 * $262.detachArrayBuffer() 实现 - 分离 ArrayBuffer
 * 用于测试分离后的 ArrayBuffer 访问行为
 */
static JSValue js_detachArrayBuffer(JSContext *ctx, JSValue this_val,
                                    int argc, JSValue *argv)
{
    JS_DetachArrayBuffer(ctx, argv[0]);
    return JS_UNDEFINED;
}

/**
 * $262.evalScript() 实现 - 执行脚本代码
 * 用于测试动态代码执行
 */
static JSValue js_evalScript(JSContext *ctx, JSValue this_val,
                             int argc, JSValue *argv)
{
    const char *str;
    size_t len;
    JSValue ret;
    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    ret = JS_Eval(ctx, str, len, "<evalScript>", JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(ctx, str);
    return ret;
}

#ifdef CONFIG_AGENT

/*
 * Test262 Agent 多线程支持
 * 
 * 用于测试 ECMAScript SharedArrayBuffer 和 Atomics 特性。
 * Test262 测试用例通过 $262.agent 对象创建和管理多个并发 Agent（线程），
 * 每个 Agent 有自己的 JS 运行时和上下文，通过 SharedArrayBuffer 共享内存。
 * 
 * 核心 API:
 * - $262.agent.start(script): 启动新 Agent
 * - $262.agent.report(msg): 发送消息到主 Agent
 * - $262.agent.getReport(): 接收消息
 * - $262.agent.broadcast(sab, val): 广播消息到所有 Agent
 * - $262.agent.receiveBroadcast(func): 设置广播回调
 * - $262.agent.leaving(): Agent 退出通知
 * - $262.agent.sleep(ms): 休眠
 * - $262.agent.monotonicNow(): 单调时钟
 */

#include <pthread.h>

/**
 * Test262 Agent 结构体 - 表示一个测试线程
 */
typedef struct {
    struct list_head link;      /* 链表节点 */
    pthread_t tid;              /* 线程 ID */
    char *script;               /* 要执行的脚本 */
    JSValue broadcast_func;     /* 广播回调函数 */
    BOOL broadcast_pending;     /* 是否有待处理的广播 */
    JSValue broadcast_sab;      /* 共享 ArrayBuffer（主上下文中） */
    uint8_t *broadcast_sab_buf; /* 共享缓冲区指针 */
    size_t broadcast_sab_size;  /* 缓冲区大小 */
    int32_t broadcast_val;      /* 广播值 */
} Test262Agent;

/**
 * Agent 报告结构体 - 用于 Agent 间通信
 */
typedef struct {
    struct list_head link;
    char *str;  /* 报告内容 */
} AgentReport;

/* 前向声明 */
static JSValue add_helpers1(JSContext *ctx);
static void add_helpers(JSContext *ctx);

/* 全局同步原语 */
static pthread_mutex_t agent_mutex = PTHREAD_MUTEX_INITIALIZER;  /* Agent 列表锁 */
static pthread_cond_t agent_cond = PTHREAD_COND_INITIALIZER;     /* Agent 条件变量 */
static struct list_head agent_list = LIST_HEAD_INIT(agent_list); /* Agent 链表 */

static pthread_mutex_t report_mutex = PTHREAD_MUTEX_INITIALIZER;  /* 报告列表锁 */
static struct list_head report_list = LIST_HEAD_INIT(report_list); /* 报告链表 */

/**
 * Agent 线程入口函数
 * 
 * 每个 Agent 线程执行流程：
 * 1. 创建独立的 JSRuntime 和 JSContext
 * 2. 执行传入的脚本代码
 * 3. 进入事件循环，等待广播消息
 * 4. 收到广播后调用回调函数处理
 * 5. 无更多任务时退出
 */
static void *agent_start(void *arg)
{
    Test262Agent *agent = arg;
    JSRuntime *rt;
    JSContext *ctx;
    JSValue ret_val;
    int ret;

    /* 创建独立的运行时和上下文 */
    rt = JS_NewRuntime();
    if (rt == NULL) {
        fatal(1, "JS_NewRuntime failure");
    }
    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        JS_FreeRuntime(rt);
        fatal(1, "JS_NewContext failure");
    }
    JS_SetContextOpaque(ctx, agent);  /* 关联 Agent 结构体 */
    JS_SetRuntimeInfo(rt, "agent");
    JS_SetCanBlock(rt, TRUE);  /* 允许阻塞操作（Atomics.wait 等） */

    /* 注入辅助函数 */
    add_helpers(ctx);
    
    /* 执行脚本 */
    ret_val = JS_Eval(ctx, agent->script, strlen(agent->script),
                      "<evalScript>", JS_EVAL_TYPE_GLOBAL);
    free(agent->script);
    agent->script = NULL;
    if (JS_IsException(ret_val))
        js_std_dump_error(ctx);
    JS_FreeValue(ctx, ret_val);

    /* 事件循环 - 处理异步任务和广播 */
    for(;;) {
        ret = JS_ExecutePendingJob(JS_GetRuntime(ctx), NULL);
        if (ret < 0) {
            js_std_dump_error(ctx);
            break;
        } else if (ret == 0) {
            /* 无待处理任务 */
            if (JS_IsUndefined(agent->broadcast_func)) {
                break;  /* 无广播回调，退出 */
            } else {
                JSValue args[2];

                /* 等待广播 */
                pthread_mutex_lock(&agent_mutex);
                while (!agent->broadcast_pending) {
                    pthread_cond_wait(&agent_cond, &agent_mutex);
                }

                agent->broadcast_pending = FALSE;
                pthread_cond_signal(&agent_cond);  /* 通知广播完成 */

                pthread_mutex_unlock(&agent_mutex);

                /* 调用广播回调：func(sharedArrayBuffer, value) */
                args[0] = JS_NewArrayBuffer(ctx, agent->broadcast_sab_buf,
                                            agent->broadcast_sab_size,
                                            NULL, NULL, TRUE);
                args[1] = JS_NewInt32(ctx, agent->broadcast_val);
                ret_val = JS_Call(ctx, agent->broadcast_func, JS_UNDEFINED,
                                  2, (JSValueConst *)args);
                JS_FreeValue(ctx, args[0]);
                JS_FreeValue(ctx, args[1]);
                if (JS_IsException(ret_val))
                    js_std_dump_error(ctx);
                JS_FreeValue(ctx, ret_val);
                JS_FreeValue(ctx, agent->broadcast_func);
                agent->broadcast_func = JS_UNDEFINED;
            }
        }
    }
    JS_FreeValue(ctx, agent->broadcast_func);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return NULL;
}

/**
 * $262.agent.start(script) - 启动新 Agent 线程
 * 
 * @param script 要执行的脚本代码
 * @note 只能在主 Agent 中调用，不能在 Agent 内部调用
 */
static JSValue js_agent_start(JSContext *ctx, JSValue this_val,
                              int argc, JSValue *argv)
{
    const char *script;
    Test262Agent *agent;
    pthread_attr_t attr;

    /* 检查是否已在 Agent 内部（不允许嵌套） */
    if (JS_GetContextOpaque(ctx) != NULL)
        return JS_ThrowTypeError(ctx, "cannot be called inside an agent");

    script = JS_ToCString(ctx, argv[0]);
    if (!script)
        return JS_EXCEPTION;
    
    agent = malloc(sizeof(*agent));
    memset(agent, 0, sizeof(*agent));
    agent->broadcast_func = JS_UNDEFINED;
    agent->broadcast_sab = JS_UNDEFINED;
    agent->script = strdup(script);
    JS_FreeCString(ctx, script);
    
    list_add_tail(&agent->link, &agent_list);
    
    /* 创建线程 - 设置 2MB 栈空间（musl libc 默认仅 80KB，不够用） */
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2 << 20);  /* 2 MB, glibc 默认值 */
    pthread_create(&agent->tid, &attr, agent_start, agent);
    pthread_attr_destroy(&attr);
    
    return JS_UNDEFINED;
}

/**
 * 清理所有 Agent 线程
 * 等待所有线程结束并释放资源
 */
static void js_agent_free(JSContext *ctx)
{
    struct list_head *el, *el1;
    Test262Agent *agent;

    list_for_each_safe(el, el1, &agent_list) {
        agent = list_entry(el, Test262Agent, link);
        pthread_join(agent->tid, NULL);  /* 等待线程结束 */
        JS_FreeValue(ctx, agent->broadcast_sab);
        list_del(&agent->link);
        free(agent);
    }
}

/**
 * $262.agent.leaving() - Agent 退出通知
 * 空操作，仅用于测试规范兼容性
 */
static JSValue js_agent_leaving(JSContext *ctx, JSValue this_val,
                                int argc, JSValue *argv)
{
    Test262Agent *agent = JS_GetContextOpaque(ctx);
    if (!agent)
        return JS_ThrowTypeError(ctx, "must be called inside an agent");
    /* 无需实际操作 */
    return JS_UNDEFINED;
}

/**
 * 检查是否有待处理的广播
 */
static BOOL is_broadcast_pending(void)
{
    struct list_head *el;
    Test262Agent *agent;
    list_for_each(el, &agent_list) {
        agent = list_entry(el, Test262Agent, link);
        if (agent->broadcast_pending)
            return TRUE;
    }
    return FALSE;
}

/**
 * $262.agent.broadcast(sab, val) - 向所有 Agent 广播消息
 * 
 * @param sab SharedArrayBuffer 对象
 * @param val 要广播的整数值
 * 
 * 流程：
 * 1. 设置所有 Agent 的 broadcast_pending 标志
 * 2. 通知所有等待的 Agent
 * 3. 等待所有 Agent 处理完成
 */
static JSValue js_agent_broadcast(JSContext *ctx, JSValue this_val,
                                  int argc, JSValue *argv)
{
    JSValueConst sab = argv[0];
    struct list_head *el;
    Test262Agent *agent;
    uint8_t *buf;
    size_t buf_size;
    int32_t val;

    /* 只能在主 Agent 中调用 */
    if (JS_GetContextOpaque(ctx) != NULL)
        return JS_ThrowTypeError(ctx, "cannot be called inside an agent");

    buf = JS_GetArrayBuffer(ctx, &buf_size, sab);
    if (!buf)
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &val, argv[1]))
        return JS_EXCEPTION;

    /* 广播并等待所有 Agent 处理完成 */
    pthread_mutex_lock(&agent_mutex);
    list_for_each(el, &agent_list) {
        agent = list_entry(el, Test262Agent, link);
        agent->broadcast_pending = TRUE;
        /* 增加共享 ArrayBuffer 引用计数（线程会持有引用） */
        agent->broadcast_sab = JS_DupValue(ctx, sab);
        agent->broadcast_sab_buf = buf;
        agent->broadcast_sab_size = buf_size;
        agent->broadcast_val = val;
    }
    pthread_cond_broadcast(&agent_cond);  /* 唤醒所有等待的 Agent */

    /* 等待所有 Agent 处理完成 */
    while (is_broadcast_pending()) {
        pthread_cond_wait(&agent_cond, &agent_mutex);
    }
    pthread_mutex_unlock(&agent_mutex);
    return JS_UNDEFINED;
}

/**
 * $262.agent.receiveBroadcast(func) - 设置广播回调函数
 * 
 * @param func 回调函数，签名：func(sharedArrayBuffer, value)
 */
static JSValue js_agent_receiveBroadcast(JSContext *ctx, JSValue this_val,
                                         int argc, JSValue *argv)
{
    Test262Agent *agent = JS_GetContextOpaque(ctx);
    if (!agent)
        return JS_ThrowTypeError(ctx, "must be called inside an agent");
    if (!JS_IsFunction(ctx, argv[0]))
        return JS_ThrowTypeError(ctx, "expecting function");
    JS_FreeValue(ctx, agent->broadcast_func);
    agent->broadcast_func = JS_DupValue(ctx, argv[0]);
    return JS_UNDEFINED;
}

/**
 * $262.agent.sleep(ms) - 休眠指定毫秒
 */
static JSValue js_agent_sleep(JSContext *ctx, JSValue this_val,
                              int argc, JSValue *argv)
{
    uint32_t duration;
    if (JS_ToUint32(ctx, &duration, argv[0]))
        return JS_EXCEPTION;
    usleep(duration * 1000);
    return JS_UNDEFINED;
}

/**
 * 获取单调时钟（毫秒）
 * 用于 $262.agent.monotonicNow()
 */
static int64_t get_clock_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

/**
 * $262.agent.monotonicNow() - 返回单调时钟当前时间（毫秒）
 */
static JSValue js_agent_monotonicNow(JSContext *ctx, JSValue this_val,
                                     int argc, JSValue *argv)
{
    return JS_NewInt64(ctx, get_clock_ms());
}

/**
 * $262.agent.getReport() - 获取待处理报告
 * @return 报告字符串或 null（无报告）
 */
static JSValue js_agent_getReport(JSContext *ctx, JSValue this_val,
                                  int argc, JSValue *argv)
{
    AgentReport *rep;
    JSValue ret;

    pthread_mutex_lock(&report_mutex);
    if (list_empty(&report_list)) {
        rep = NULL;
    } else {
        rep = list_entry(report_list.next, AgentReport, link);
        list_del(&rep->link);
    }
    pthread_mutex_unlock(&report_mutex);
    if (rep) {
        ret = JS_NewString(ctx, rep->str);
        free(rep->str);
        free(rep);
    } else {
        ret = JS_NULL;
    }
    return ret;
}

/**
 * $262.agent.report(msg) - 发送报告到主 Agent
 * 
 * @param msg 报告消息
 */
static JSValue js_agent_report(JSContext *ctx, JSValue this_val,
                               int argc, JSValue *argv)
{
    const char *str;
    AgentReport *rep;

    str = JS_ToCString(ctx, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    rep = malloc(sizeof(*rep));
    rep->str = strdup(str);
    JS_FreeCString(ctx, str);

    pthread_mutex_lock(&report_mutex);
    list_add_tail(&rep->link, &report_list);
    pthread_mutex_unlock(&report_mutex);
    return JS_UNDEFINED;
}

/**
 * $262.agent 对象函数列表
 */
static const JSCFunctionListEntry js_agent_funcs[] = {
    /* 仅在主 Agent 中可用 */
    JS_CFUNC_DEF("start", 1, js_agent_start ),
    JS_CFUNC_DEF("getReport", 0, js_agent_getReport ),
    JS_CFUNC_DEF("broadcast", 2, js_agent_broadcast ),
    /* 仅在子 Agent 中可用 */
    JS_CFUNC_DEF("report", 1, js_agent_report ),
    JS_CFUNC_DEF("leaving", 0, js_agent_leaving ),
    JS_CFUNC_DEF("receiveBroadcast", 1, js_agent_receiveBroadcast ),
    /* 两者都可用 */
    JS_CFUNC_DEF("sleep", 1, js_agent_sleep ),
    JS_CFUNC_DEF("monotonicNow", 0, js_agent_monotonicNow ),
};

/**
 * 创建 $262.agent 对象
 */
static JSValue js_new_agent(JSContext *ctx)
{
    JSValue agent;
    agent = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, agent, js_agent_funcs,
                               countof(js_agent_funcs));
    return agent;
}
#endif  /* CONFIG_AGENT */

/**
 * $262.createRealm() - 创建新的 Realm（全局环境）
 * 
 * Realm 是 JavaScript 的全局环境，包含全局对象、内置对象等。
 * 此函数用于测试跨 Realm 的对象交互。
 * 
 * @return 新 Realm 的 $262 对象
 */
static JSValue js_createRealm(JSContext *ctx, JSValue this_val,
                              int argc, JSValue *argv)
{
    JSContext *ctx1;
    JSValue ret;

    /* 创建新上下文（同一运行时内） */
    ctx1 = JS_NewContext(JS_GetRuntime(ctx));
    if (!ctx1)
        return JS_ThrowOutOfMemory(ctx);
    
    /* 注入辅助函数 */
    ret = add_helpers1(ctx1);
    /* ctx1 有引用计数，不会立即释放 */
    JS_FreeContext(ctx1);
    return ret;
}

/**
 * $262.IsHTMLDDA - 特殊对象，用于测试 typeof null 边界情况
 * 返回 null，但 typeof 返回 "object"（历史遗留问题）
 */
static JSValue js_IsHTMLDDA(JSContext *ctx, JSValue this_val,
                            int argc, JSValue *argv)
{
    return JS_NULL;
}

/**
 * $262.gc() - 触发垃圾回收
 * 用于测试内存管理和 FinalizationRegistry
 */
static JSValue js_gc(JSContext *ctx, JSValueConst this_val,
                     int argc, JSValueConst *argv)
{
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

/**
 * 向上下文注入辅助函数（内部版本）
 * 创建 $262 特殊对象并注入全局作用域
 */
static JSValue add_helpers1(JSContext *ctx)
{
    JSValue global_obj;
    JSValue obj262, obj;

    global_obj = JS_GetGlobalObject(ctx);

    /* 注入 print 函数 */
    JS_SetPropertyStr(ctx, global_obj, "print",
                      JS_NewCFunction(ctx, js_print, "print", 1));

    /* 创建 $262 特殊对象 - Test262 测试使用的 API 集合 */
    obj262 = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj262, "detachArrayBuffer",
                      JS_NewCFunction(ctx, js_detachArrayBuffer,
                                      "detachArrayBuffer", 1));
    JS_SetPropertyStr(ctx, obj262, "evalScript",
                      JS_NewCFunction(ctx, js_evalScript,
                                      "evalScript", 1));
    JS_SetPropertyStr(ctx, obj262, "codePointRange",
                      JS_NewCFunction(ctx, js_string_codePointRange,
                                      "codePointRange", 2));
#ifdef CONFIG_AGENT
    /* 注入 $262.agent 对象（多线程支持） */
    JS_SetPropertyStr(ctx, obj262, "agent", js_new_agent(ctx));
#endif

    /* $262.global - 全局对象引用 */
    JS_SetPropertyStr(ctx, obj262, "global",
                      JS_DupValue(ctx, global_obj));
    /* $262.createRealm - 创建新 Realm */
    JS_SetPropertyStr(ctx, obj262, "createRealm",
                      JS_NewCFunction(ctx, js_createRealm,
                                      "createRealm", 0));
    /* $262.IsHTMLDDA - 特殊对象（typeof 测试） */
    obj = JS_NewCFunction(ctx, js_IsHTMLDDA, "IsHTMLDDA", 0);
    JS_SetIsHTMLDDA(ctx, obj);  /* 标记为 IsHTMLDDA 类型 */
    JS_SetPropertyStr(ctx, obj262, "IsHTMLDDA", obj);
    /* $262.gc - 垃圾回收 */
    JS_SetPropertyStr(ctx, obj262, "gc",
                      JS_NewCFunction(ctx, js_gc, "gc", 0));

    /* 将 $262 对象注入全局作用域 */
    JS_SetPropertyStr(ctx, global_obj, "$262", JS_DupValue(ctx, obj262));

    JS_FreeValue(ctx, global_obj);
    return obj262;
}

/**
 * 向上下文注入辅助函数（公共接口）
 */
static void add_helpers(JSContext *ctx)
{
    JS_FreeValue(ctx, add_helpers1(ctx));
}

/**
 * 加载文件到内存
 * @param filename 文件路径
 * @param lenp 输出参数：文件长度
 * @return 文件内容（需手动 free）
 */
static char *load_file(const char *filename, size_t *lenp)
{
    char *buf;
    size_t buf_len;
    buf = (char *)js_load_file(NULL, &buf_len, filename);
    if (!buf)
        perror_exit(1, filename);
    if (lenp)
        *lenp = buf_len;
    return buf;
}

/**
 * JSON 模块初始化函数
 * 将 JSON 对象设置为模块的 default 导出
 */
static int json_module_init_test(JSContext *ctx, JSModuleDef *m)
{
    JSValue val;
    val = JS_GetModulePrivateValue(ctx, m);
    JS_SetModuleExport(ctx, m, "default", val);
    return 0;
}

/**
 * 测试用模块加载器
 * 
 * 支持：
 * - ES6 模块（.js）
 * - JSON 模块（通过 attributes 判断）
 * 
 * 特殊处理：相对路径解析
 * - import("bar.js") from "path/to/foo.js" => "path/to/bar.js"
 * - import("./bar.js") => 保持不变
 */
static JSModuleDef *js_module_loader_test(JSContext *ctx,
                                          const char *module_name, void *opaque,
                                          JSValueConst attributes)
{
    size_t buf_len;
    uint8_t *buf;
    JSModuleDef *m;
    char *filename, *slash, path[1024];

    /* 解析模块路径 */
    filename = opaque;
    if (!strchr(module_name, '/')) {
        /* 非相对路径：添加父目录前缀 */
        slash = strrchr(filename, '/');
        if (slash) {
            snprintf(path, sizeof(path), "%.*s/%s",
                     (int)(slash - filename), filename, module_name);
            module_name = path;
        }
    }

    buf = js_load_file(ctx, &buf_len, module_name);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                               module_name);
        return NULL;
    }

    /* 检查是否为 JSON 模块 */
    if (js_module_test_json(ctx, attributes) == 1) {
        /* 编译为 JSON 模块 */
        JSValue val;
        val = JS_ParseJSON(ctx, (char *)buf, buf_len, module_name);
        js_free(ctx, buf);
        if (JS_IsException(val))
            return NULL;
        m = JS_NewCModule(ctx, module_name, json_module_init_test);
        if (!m) {
            JS_FreeValue(ctx, val);
            return NULL;
        }
        /* 仅导出 "default" 符号（包含 JSON 对象） */
        JS_AddModuleExport(ctx, m, "default");
        JS_SetModulePrivateValue(ctx, m, val);
    } else {
        JSValue func_val;
        /* 编译为 ES6 模块 */
        func_val = JS_Eval(ctx, (char *)buf, buf_len, module_name,
                           JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
        js_free(ctx, buf);
        if (JS_IsException(func_val))
            return NULL;
        /* 模块已引用，释放临时值 */
        m = JS_VALUE_GET_PTR(func_val);
        JS_FreeValue(ctx, func_val);
    }
    return m;
}

/**
 * 判断是否为行分隔符
 */
int is_line_sep(char c)
{
    return (c == '\0' || c == '\n' || c == '\r');
}

/**
 * 在字符串中查找独立行
 * 用于查找错误文件中的特定文件名
 * @param str 文本内容
 * @param line 要查找的行
 * @return 找到的位置或 NULL
 */
char *find_line(const char *str, const char *line)
{
    if (str) {
        const char *p;
        int len = strlen(line);
        for (p = str; (p = strstr(p, line)) != NULL; p += len + 1) {
            /* 检查是否为独立行（前后都是行分隔符） */
            if ((p == str || is_line_sep(p[-1])) && is_line_sep(p[len]))
                return (char *)p;
        }
    }
    return NULL;
}

/**
 * 判断是否为单词分隔符
 */
int is_word_sep(char c)
{
    return (c == '\0' || isspace((unsigned char)c) || c == ',');
}

/**
 * 在字符串中查找独立单词
 * 用于查找特性名称、排除列表等
 * @param str 文本内容
 * @param word 要查找的单词
 * @return 找到的位置或 NULL
 */
char *find_word(const char *str, const char *word)
{
    const char *p;
    int len = strlen(word);
    if (str && len) {
        for (p = str; (p = strstr(p, word)) != NULL; p += len) {
            /* 检查是否为独立单词（前后都是分隔符） */
            if ((p == str || is_word_sep(p[-1])) && is_word_sep(p[len]))
                return (char *)p;
        }
    }
    return NULL;
}

/**
 * 处理排除目录列表
 * 
 * 将 exclude_list 中的目录项（以/结尾）移到 exclude_dir_list，
 * 然后从 test_list 中过滤掉这些目录下的测试文件。
 */
void update_exclude_dirs(void)
{
    namelist_t *lp = &test_list;
    namelist_t *ep = &exclude_list;
    namelist_t *dp = &exclude_dir_list;
    char *name;
    int i, j, count;

    /* 从 exclude_list 分离出目录项 */
    for (count = i = 0; i < ep->count; i++) {
        name = ep->array[i];
        if (has_suffix(name, "/")) {
            namelist_add(dp, NULL, name);
            free(name);
        } else {
            ep->array[count++] = name;
        }
    }
    ep->count = count;

    namelist_sort(dp);

    /* 从测试列表中过滤掉排除的目录 */
    for (count = i = 0; i < lp->count; i++) {
        name = lp->array[i];
        for (j = 0; j < dp->count; j++) {
            if (has_prefix(name, dp->array[j])) {
                test_excluded++;
                free(name);
                name = NULL;
                break;
            }
        }
        if (name) {
            lp->array[count++] = name;
        }
    }
    lp->count = count;
}

/**
 * 加载配置文件
 * 
 * 配置文件格式（INI 风格）:
 * [config]
 *   testdir=test262/test
 *   harnessdir=test262/harness
 *   mode=default-nostrict
 *   features=async,module
 * [exclude]
 *   test262/test/annexB/
 * [features]
 *   async=yes
 *   module=yes
 * [tests]
 *   test262/test/built-ins/Array
 * 
 * @param filename 配置文件路径
 * @param ignore 忽略的配置项（逗号分隔）
 */
void load_config(const char *filename, const char *ignore)
{
    char buf[1024];
    FILE *f;
    char *base_name;
    enum {
        SECTION_NONE = 0,
        SECTION_CONFIG,    /* [config] 节 */
        SECTION_EXCLUDE,   /* [exclude] 节 */
        SECTION_FEATURES,  /* [features] 节 */
        SECTION_TESTS,     /* [tests] 节 */
    } section = SECTION_NONE;
    int lineno = 0;

    f = fopen(filename, "rb");
    if (!f) {
        perror_exit(1, filename);
    }
    base_name = get_basename(filename);

    while (fgets(buf, sizeof(buf), f) != NULL) {
        char *p, *q;
        lineno++;
        p = str_strip(buf);
        if (*p == '#' || *p == ';' || *p == '\0')
            continue;  /* 跳过注释和空行 */

        if (*p == "[]"[0]) {
            /* 新的节 */
            p++;
            p[strcspn(p, "]")] = '\0';
            if (str_equal(p, "config"))
                section = SECTION_CONFIG;
            else if (str_equal(p, "exclude"))
                section = SECTION_EXCLUDE;
            else if (str_equal(p, "features"))
                section = SECTION_FEATURES;
            else if (str_equal(p, "tests"))
                section = SECTION_TESTS;
            else
                section = SECTION_NONE;
            continue;
        }
        q = strchr(p, '=');
        if (q) {
            /* 配置项：name=value */
            *q++ = '\0';
            q = str_strip(q);
        }
        switch (section) {
        case SECTION_CONFIG:
            if (!q) {
                printf("%s:%d: syntax error\n", filename, lineno);
                continue;
            }
            if (strstr(ignore, p)) {
                printf("%s:%d: ignoring %s=%s\n", filename, lineno, p, q);
                continue;
            }
            /* 解析配置项 */
            if (str_equal(p, "style")) {
                new_style = str_equal(q, "new");
                continue;
            }
            if (str_equal(p, "testdir")) {
                char *testdir = compose_path(base_name, q);
                enumerate_tests(testdir);
                free(testdir);
                continue;
            }
            if (str_equal(p, "harnessdir")) {
                harness_dir = compose_path(base_name, q);
                continue;
            }
            if (str_equal(p, "harnessexclude")) {
                str_append(&harness_exclude, " ", q);
                continue;
            }
            if (str_equal(p, "features")) {
                str_append(&harness_features, " ", q);
                continue;
            }
            if (str_equal(p, "skip-features")) {
                str_append(&harness_skip_features, " ", q);
                continue;
            }
            if (str_equal(p, "mode")) {
                /* 测试模式配置 */
                if (str_equal(q, "default") || str_equal(q, "default-nostrict"))
                    test_mode = TEST_DEFAULT_NOSTRICT;
                else if (str_equal(q, "default-strict"))
                    test_mode = TEST_DEFAULT_STRICT;
                else if (str_equal(q, "nostrict"))
                    test_mode = TEST_NOSTRICT;
                else if (str_equal(q, "strict"))
                    test_mode = TEST_STRICT;
                else if (str_equal(q, "all") || str_equal(q, "both"))
                    test_mode = TEST_ALL;
                else
                    fatal(2, "unknown test mode: %s", q);
                continue;
            }
            if (str_equal(p, "strict")) {
                if (str_equal(q, "skip") || str_equal(q, "no"))
                    test_mode = TEST_NOSTRICT;
                continue;
            }
            if (str_equal(p, "nostrict")) {
                if (str_equal(q, "skip") || str_equal(q, "no"))
                    test_mode = TEST_STRICT;
                continue;
            }
            if (str_equal(p, "async")) {
                skip_async = !str_equal(q, "yes");
                continue;
            }
            if (str_equal(p, "module")) {
                skip_module = !str_equal(q, "yes");
                continue;
            }
            if (str_equal(p, "verbose")) {
                verbose = str_equal(q, "yes");
                continue;
            }
            if (str_equal(p, "errorfile")) {
                error_filename = compose_path(base_name, q);
                continue;
            }
            if (str_equal(p, "excludefile")) {
                char *path = compose_path(base_name, q);
                namelist_load(&exclude_list, path);
                free(path);
                continue;
            }
            if (str_equal(p, "reportfile")) {
                report_filename = compose_path(base_name, q);
                continue;
            }
        case SECTION_EXCLUDE:
            /* [exclude] 节：直接添加文件名 */
            namelist_add(&exclude_list, base_name, p);
            break;
        case SECTION_FEATURES:
            /* [features] 节：启用/禁用特性 */
            if (!q || str_equal(q, "yes"))
                str_append(&harness_features, " ", p);
            else
                str_append(&harness_skip_features, " ", p);
            break;
        case SECTION_TESTS:
            /* [tests] 节：指定测试文件 */
            namelist_add(&test_list, base_name, p);
            break;
        default:
            /* 忽略其他节的设置 */
            break;
        }
    }
    fclose(f);
    free(base_name);
}

/**
 * 在错误文件中查找指定文件的错误记录
 * 
 * 解析格式：filename.js:line: [strict mode: ] [unexpected error: ]message
 * 
 * @param filename 测试文件名
 * @param pline 输出参数：错误行号
 * @param is_strict 是否严格模式
 * @return 错误消息（需手动 free），未找到返回 NULL
 */
char *find_error(const char *filename, int *pline, int is_strict)
{
    if (error_file) {
        size_t len = strlen(filename);
        const char *p, *q, *r;
        int line;

        /* 在错误文件中搜索文件名 */
        for (p = error_file; (p = strstr(p, filename)) != NULL; p += len) {
            /* 检查是否为独立的文件名（前后是换行或开头） */
            if ((p == error_file || p[-1] == '\n' || p[-1] == '(') && p[len] == ':') {
                q = p + len;
                line = 1;
                /* 解析行号 */
                if (*q == ':') {
                    line = strtol(q + 1, (char**)&q, 10);
                    if (*q == ':')
                        q++;
                }
                while (*q == ' ') {
                    q++;
                }
                /* 检查严格模式标记是否匹配 */
                if (!strstart(q, "strict mode: ", &q) != !is_strict)
                    continue;
                /* 提取错误消息 */
                r = q = skip_prefix(q, "unexpected error: ");
                r += strcspn(r, "\n");
                /* 跳过连续的错误消息行 */
                while (r[0] == '\n' && r[1] && strncmp(r + 1, filename, 8)) {
                    r++;
                    r += strcspn(r, "\n");
                }
                if (pline)
                    *pline = line;
                return strdup_len(q, r - q);
            }
        }
    }
    return NULL;
}

int skip_comments(const char *str, int line, int *pline)
{
    const char *p;
    int c;

    p = str;
    while ((c = (unsigned char)*p++) != '\0') {
        if (isspace(c)) {
            if (c == '\n')
                line++;
            continue;
        }
        if (c == '/' && *p == '/') {
            while (*++p && *p != '\n')
                continue;
            continue;
        }
        if (c == '/' && *p == '*') {
            for (p += 1; *p; p++) {
                if (*p == '\n') {
                    line++;
                    continue;
                }
                if (*p == '*' && p[1] == '/') {
                    p += 2;
                    break;
                }
            }
            continue;
        }
        break;
    }
    if (pline)
        *pline = line;

    return p - str;
}

int longest_match(const char *str, const char *find, int pos, int *ppos, int line, int *pline)
{
    int len, maxlen;

    maxlen = 0;

    if (*find) {
        const char *p;
        for (p = str + pos; *p; p++) {
            if (*p == *find) {
                for (len = 1; p[len] && p[len] == find[len]; len++)
                    continue;
                if (len > maxlen) {
                    maxlen = len;
                    if (ppos)
                        *ppos = p - str;
                    if (pline)
                        *pline = line;
                    if (!find[len])
                        break;
                }
            }
            if (*p == '\n')
                line++;
        }
    }
    return maxlen;
}

static int eval_buf(JSContext *ctx, const char *buf, size_t buf_len,
                    const char *filename, int is_test, int is_negative,
                    const char *error_type, FILE *outfile, int eval_flags,
                    int is_async)
{
    JSValue res_val, exception_val;
    int ret, error_line, pos, pos_line;
    BOOL is_error, has_error_line, ret_promise;
    const char *error_name;

    pos = skip_comments(buf, 1, &pos_line);
    error_line = pos_line;
    has_error_line = FALSE;
    exception_val = JS_UNDEFINED;
    error_name = NULL;

    /* a module evaluation returns a promise */
    ret_promise = ((eval_flags & JS_EVAL_TYPE_MODULE) != 0);
    async_done = 0; /* counter of "Test262:AsyncTestComplete" messages */

    res_val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);

    if ((is_async || ret_promise) && !JS_IsException(res_val)) {
        JSValue promise = JS_UNDEFINED;
        if (ret_promise) {
            promise = res_val;
        } else {
            JS_FreeValue(ctx, res_val);
        }
        for(;;) {
            ret = JS_ExecutePendingJob(JS_GetRuntime(ctx), NULL);
            if (ret < 0) {
                res_val = JS_EXCEPTION;
                break;
            } else if (ret == 0) {
                if (is_async) {
                    /* test if the test called $DONE() once */
                    if (async_done != 1) {
                        res_val = JS_ThrowTypeError(ctx, "$DONE() not called");
                    } else {
                        res_val = JS_UNDEFINED;
                    }
                } else {
                    /* check that the returned promise is fulfilled */
                    JSPromiseStateEnum state = JS_PromiseState(ctx, promise);
                    if (state == JS_PROMISE_FULFILLED)
                        res_val = JS_UNDEFINED;
                    else if (state == JS_PROMISE_REJECTED)
                        res_val = JS_Throw(ctx, JS_PromiseResult(ctx, promise));
                    else
                        res_val = JS_ThrowTypeError(ctx, "promise is pending");
                }
                break;
            }
        }
        JS_FreeValue(ctx, promise);
    }

    if (JS_IsException(res_val)) {
        exception_val = JS_GetException(ctx);
        is_error = JS_IsError(ctx, exception_val);
        /* XXX: should get the filename and line number */
        if (outfile) {
            if (!is_error)
                fprintf(outfile, "%sThrow: ", (eval_flags & JS_EVAL_FLAG_STRICT) ?
                        "strict mode: " : "");
            js_print(ctx, JS_NULL, 1, &exception_val);
        }
        if (is_error) {
            JSValue name, stack;
            const char *stack_str;

            name = JS_GetPropertyStr(ctx, exception_val, "name");
            error_name = JS_ToCString(ctx, name);
            stack = JS_GetPropertyStr(ctx, exception_val, "stack");
            if (!JS_IsUndefined(stack)) {
                stack_str = JS_ToCString(ctx, stack);
                if (stack_str) {
                    const char *p;
                    int len;

                    if (outfile)
                        fprintf(outfile, "%s", stack_str);

                    len = strlen(filename);
                    p = strstr(stack_str, filename);
                    if (p != NULL && p[len] == ':') {
                        error_line = atoi(p + len + 1);
                        has_error_line = TRUE;
                    }
                    JS_FreeCString(ctx, stack_str);
                }
            }
            JS_FreeValue(ctx, stack);
            JS_FreeValue(ctx, name);
        }
        if (is_negative) {
            ret = 0;
            if (error_type) {
                char *error_class;
                const char *msg;

                msg = JS_ToCString(ctx, exception_val);
                error_class = strdup_len(msg, strcspn(msg, ":"));
                if (!str_equal(error_class, error_type))
                    ret = -1;
                free(error_class);
                JS_FreeCString(ctx, msg);
            }
        } else {
            ret = -1;
        }
    } else {
        if (is_negative)
            ret = -1;
        else
            ret = 0;
    }

    if (verbose && is_test) {
        JSValue msg_val = JS_UNDEFINED;
        const char *msg = NULL;
        int s_line;
        char *s = find_error(filename, &s_line, eval_flags & JS_EVAL_FLAG_STRICT);
        const char *strict_mode = (eval_flags & JS_EVAL_FLAG_STRICT) ? "strict mode: " : "";

        if (!JS_IsUndefined(exception_val)) {
            msg_val = JS_ToString(ctx, exception_val);
            msg = JS_ToCString(ctx, msg_val);
        }
        if (is_negative) {  // expect error
            if (ret == 0) {
                if (msg && s &&
                    (str_equal(s, "expected error") ||
                     strstart(s, "unexpected error type:", NULL) ||
                     str_equal(s, msg))) {     // did not have error yet
                    if (!has_error_line) {
                        longest_match(buf, msg, pos, &pos, pos_line, &error_line);
                    }
                    printf("%s:%d: %sOK, now has error %s\n",
                           filename, error_line, strict_mode, msg);
                    fixed_errors++;
                }
            } else {
                if (!s) {   // not yet reported
                    if (msg) {
                        fprintf(error_out, "%s:%d: %sunexpected error type: %s\n",
                                filename, error_line, strict_mode, msg);
                    } else {
                        fprintf(error_out, "%s:%d: %sexpected error\n",
                                filename, error_line, strict_mode);
                    }
                    new_errors++;
                }
            }
        } else {            // should not have error
            if (msg) {
                if (!s || !str_equal(s, msg)) {
                    if (!has_error_line) {
                        char *p = skip_prefix(msg, "Test262 Error: ");
                        if (strstr(p, "Test case returned non-true value!")) {
                            longest_match(buf, "runTestCase", pos, &pos, pos_line, &error_line);
                        } else {
                            longest_match(buf, p, pos, &pos, pos_line, &error_line);
                        }
                    }
                    fprintf(error_out, "%s:%d: %s%s%s\n", filename, error_line, strict_mode,
                            error_file ? "unexpected error: " : "", msg);

                    if (s && (!str_equal(s, msg) || error_line != s_line)) {
                        printf("%s:%d: %sprevious error: %s\n", filename, s_line, strict_mode, s);
                        changed_errors++;
                    } else {
                        new_errors++;
                    }
                }
            } else {
                if (s) {
                    printf("%s:%d: %sOK, fixed error: %s\n", filename, s_line, strict_mode, s);
                    fixed_errors++;
                }
            }
        }
        JS_FreeValue(ctx, msg_val);
        JS_FreeCString(ctx, msg);
        free(s);
    }
    JS_FreeCString(ctx, error_name);
    JS_FreeValue(ctx, exception_val);
    JS_FreeValue(ctx, res_val);
    return ret;
}

static int eval_file(JSContext *ctx, const char *base, const char *p,
                     int eval_flags)
{
    char *buf;
    size_t buf_len;
    char *filename = compose_path(base, p);

    buf = load_file(filename, &buf_len);
    if (!buf) {
        warning("cannot load %s", filename);
        goto fail;
    }
    if (eval_buf(ctx, buf, buf_len, filename, FALSE, FALSE, NULL, stderr,
                 eval_flags, FALSE)) {
        warning("error evaluating %s", filename);
        goto fail;
    }
    free(buf);
    free(filename);
    return 0;

fail:
    free(buf);
    free(filename);
    return 1;
}

char *extract_desc(const char *buf, char style)
{
    const char *p, *desc_start;
    char *desc;
    int len;

    p = buf;
    while (*p != '\0') {
        if (p[0] == '/' && p[1] == '*' && p[2] == style && p[3] != '/') {
            p += 3;
            desc_start = p;
            while (*p != '\0' && (p[0] != '*' || p[1] != '/'))
                p++;
            if (*p == '\0') {
                warning("Expecting end of desc comment");
                return NULL;
            }
            len = p - desc_start;
            desc = malloc(len + 1);
            memcpy(desc, desc_start, len);
            desc[len] = '\0';
            return desc;
        } else {
            p++;
        }
    }
    return NULL;
}

static char *find_tag(char *desc, const char *tag, int *state)
{
    char *p;
    p = strstr(desc, tag);
    if (p) {
        p += strlen(tag);
        *state = 0;
    }
    return p;
}

static char *get_option(char **pp, int *state)
{
    char *p, *p0, *option = NULL;
    if (*pp) {
        for (p = *pp;; p++) {
            switch (*p) {
            case '[':
                *state += 1;
                continue;
            case ']':
                *state -= 1;
                if (*state > 0)
                    continue;
                p = NULL;
                break;
            case ' ':
            case '\t':
            case '\r':
            case ',':
            case '-':
                continue;
            case '\n':
                if (*state > 0 || p[1] == ' ')
                    continue;
                p = NULL;
                break;
            case '\0':
                p = NULL;
                break;
            default:
                p0 = p;
                p += strcspn(p0, " \t\r\n,]");
                option = strdup_len(p0, p - p0);
                break;
            }
            break;
        }
        *pp = p;
    }
    return option;
}

void update_stats(JSRuntime *rt, const char *filename) {
    JSMemoryUsage stats;
    JS_ComputeMemoryUsage(rt, &stats);
    if (stats_count++ == 0) {
        stats_avg = stats_all = stats_min = stats_max = stats;
        stats_min_filename = strdup(filename);
        stats_max_filename = strdup(filename);
    } else {
        if (stats_max.malloc_size < stats.malloc_size) {
            stats_max = stats;
            free(stats_max_filename);
            stats_max_filename = strdup(filename);
        }
        if (stats_min.malloc_size > stats.malloc_size) {
            stats_min = stats;
            free(stats_min_filename);
            stats_min_filename = strdup(filename);
        }

#define update(f)  stats_avg.f = (stats_all.f += stats.f) / stats_count
        update(malloc_count);
        update(malloc_size);
        update(memory_used_count);
        update(memory_used_size);
        update(atom_count);
        update(atom_size);
        update(str_count);
        update(str_size);
        update(obj_count);
        update(obj_size);
        update(prop_count);
        update(prop_size);
        update(shape_count);
        update(shape_size);
        update(js_func_count);
        update(js_func_size);
        update(js_func_code_size);
        update(js_func_pc2line_count);
        update(js_func_pc2line_size);
        update(c_func_count);
        update(array_count);
        update(fast_array_count);
        update(fast_array_elements);
    }
#undef update
}

int run_test_buf(const char *filename, const char *harness, namelist_t *ip,
                 char *buf, size_t buf_len, const char* error_type,
                 int eval_flags, BOOL is_negative, BOOL is_async,
                 BOOL can_block)
{
    JSRuntime *rt;
    JSContext *ctx;
    int i, ret;

    rt = JS_NewRuntime();
    if (rt == NULL) {
        fatal(1, "JS_NewRuntime failure");
    }
    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        JS_FreeRuntime(rt);
        fatal(1, "JS_NewContext failure");
    }
    JS_SetRuntimeInfo(rt, filename);

    JS_SetCanBlock(rt, can_block);

    /* loader for ES6 modules */
    JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader_test, NULL, (void *)filename);

    add_helpers(ctx);

    for (i = 0; i < ip->count; i++) {
        if (eval_file(ctx, harness, ip->array[i],
                      JS_EVAL_TYPE_GLOBAL)) {
            fatal(1, "error including %s for %s", ip->array[i], filename);
        }
    }

    ret = eval_buf(ctx, buf, buf_len, filename, TRUE, is_negative,
                   error_type, outfile, eval_flags, is_async);
    ret = (ret != 0);

    if (dump_memory) {
        update_stats(rt, filename);
    }
#ifdef CONFIG_AGENT
    js_agent_free(ctx);
#endif
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    test_count++;
    if (ret) {
        test_failed++;
        if (outfile) {
            /* do not output a failure number to minimize diff */
            fprintf(outfile, "  FAILED\n");
        }
    }
    return ret;
}

int run_test(const char *filename, int index)
{
    char harnessbuf[1024];
    char *harness;
    char *buf;
    size_t buf_len;
    char *desc, *p;
    char *error_type;
    int ret, eval_flags, use_strict, use_nostrict;
    BOOL is_negative, is_nostrict, is_onlystrict, is_async, is_module, skip;
    BOOL can_block;
    namelist_t include_list = { 0 }, *ip = &include_list;

    is_nostrict = is_onlystrict = is_negative = is_async = is_module = skip = FALSE;
    can_block = TRUE;
    error_type = NULL;
    buf = load_file(filename, &buf_len);

    harness = harness_dir;

    if (new_style) {
        if (!harness) {
            p = strstr(filename, "test/");
            if (p) {
                snprintf(harnessbuf, sizeof(harnessbuf), "%.*s%s",
                         (int)(p - filename), filename, "harness");
            } else {
                pstrcpy(harnessbuf, sizeof(harnessbuf), "");
            }
            harness = harnessbuf;
        }
        namelist_add(ip, NULL, "sta.js");
        namelist_add(ip, NULL, "assert.js");
        /* extract the YAML frontmatter */
        desc = extract_desc(buf, '-');
        if (desc) {
            char *ifile, *option;
            int state;
            p = find_tag(desc, "includes:", &state);
            if (p) {
                while ((ifile = get_option(&p, &state)) != NULL) {
                    // skip unsupported harness files
                    if (find_word(harness_exclude, ifile)) {
                        skip |= 1;
                    } else {
                        namelist_add(ip, NULL, ifile);
                    }
                    free(ifile);
                }
            }
            p = find_tag(desc, "flags:", &state);
            if (p) {
                while ((option = get_option(&p, &state)) != NULL) {
                    if (str_equal(option, "noStrict") ||
                        str_equal(option, "raw")) {
                        is_nostrict = TRUE;
                        skip |= (test_mode == TEST_STRICT);
                    }
                    else if (str_equal(option, "onlyStrict")) {
                        is_onlystrict = TRUE;
                        skip |= (test_mode == TEST_NOSTRICT);
                    }
                    else if (str_equal(option, "async")) {
                        is_async = TRUE;
                        skip |= skip_async;
                    }
                    else if (str_equal(option, "module")) {
                        is_module = TRUE;
                        skip |= skip_module;
                    }
                    else if (str_equal(option, "CanBlockIsFalse")) {
                        can_block = FALSE;
                    }
                    free(option);
                }
            }
            p = find_tag(desc, "negative:", &state);
            if (p) {
                /* XXX: should extract the phase */
                char *q = find_tag(p, "type:", &state);
                if (q) {
                    while (isspace((unsigned char)*q))
                        q++;
                    error_type = strdup_len(q, strcspn(q, " \n"));
                }
                is_negative = TRUE;
            }
            p = find_tag(desc, "features:", &state);
            if (p) {
                while ((option = get_option(&p, &state)) != NULL) {
                    char *p1;
                    if (find_word(harness_features, option)) {
                        /* feature is enabled */
                    } else if ((p1 = find_word(harness_skip_features, option)) != NULL) {
                        /* skip disabled feature */
                        if (harness_skip_features_count)
                            harness_skip_features_count[p1 - harness_skip_features]++;
                        skip |= 1;
                    } else {
                        /* feature is not listed: skip and warn */
                        printf("%s:%d: unknown feature: %s\n", filename, 1, option);
                        skip |= 1;
                    }
                    free(option);
                }
            }
            free(desc);
        }
        if (is_async)
            namelist_add(ip, NULL, "doneprintHandle.js");
    } else {
        char *ifile;

        if (!harness) {
            p = strstr(filename, "test/");
            if (p) {
                snprintf(harnessbuf, sizeof(harnessbuf), "%.*s%s",
                         (int)(p - filename), filename, "test/harness");
            } else {
                pstrcpy(harnessbuf, sizeof(harnessbuf), "");
            }
            harness = harnessbuf;
        }

        namelist_add(ip, NULL, "sta.js");

        /* include extra harness files */
        for (p = buf; (p = strstr(p, "$INCLUDE(\"")) != NULL; p++) {
            p += 10;
            ifile = strdup_len(p, strcspn(p, "\""));
            // skip unsupported harness files
            if (find_word(harness_exclude, ifile)) {
                skip |= 1;
            } else {
                namelist_add(ip, NULL, ifile);
            }
            free(ifile);
        }

        /* locate the old style configuration comment */
        desc = extract_desc(buf, '*');
        if (desc) {
            if (strstr(desc, "@noStrict")) {
                is_nostrict = TRUE;
                skip |= (test_mode == TEST_STRICT);
            }
            if (strstr(desc, "@onlyStrict")) {
                is_onlystrict = TRUE;
                skip |= (test_mode == TEST_NOSTRICT);
            }
            if (strstr(desc, "@negative")) {
                /* XXX: should extract the regex to check error type */
                is_negative = TRUE;
            }
            free(desc);
        }
    }

    if (outfile && index >= 0) {
        fprintf(outfile, "%d: %s%s%s%s%s%s%s\n", index, filename,
                is_nostrict ? "  @noStrict" : "",
                is_onlystrict ? "  @onlyStrict" : "",
                is_async ? "  async" : "",
                is_module ? "  module" : "",
                is_negative ? "  @negative" : "",
                skip ? "  SKIPPED" : "");
        fflush(outfile);
    }

    use_strict = use_nostrict = 0;
    /* XXX: should remove 'test_mode' or simplify it just to force
       strict or non strict mode for single file tests */
    switch (test_mode) {
    case TEST_DEFAULT_NOSTRICT:
        if (is_onlystrict)
            use_strict = 1;
        else
            use_nostrict = 1;
        break;
    case TEST_DEFAULT_STRICT:
        if (is_nostrict)
            use_nostrict = 1;
        else
            use_strict = 1;
        break;
    case TEST_NOSTRICT:
        if (!is_onlystrict)
            use_nostrict = 1;
        break;
    case TEST_STRICT:
        if (!is_nostrict)
            use_strict = 1;
        break;
    case TEST_ALL:
        if (is_module) {
            use_nostrict = 1;
        } else {
            if (!is_nostrict)
                use_strict = 1;
            if (!is_onlystrict)
                use_nostrict = 1;
        }
        break;
    }

    if (skip || use_strict + use_nostrict == 0) {
        test_skipped++;
        ret = -2;
    } else {
        clock_t clocks;

        if (is_module) {
            eval_flags = JS_EVAL_TYPE_MODULE;
        } else {
            eval_flags = JS_EVAL_TYPE_GLOBAL;
        }
        clocks = clock();
        ret = 0;
        if (use_nostrict) {
            ret = run_test_buf(filename, harness, ip, buf, buf_len,
                               error_type, eval_flags, is_negative, is_async,
                               can_block);
        }
        if (use_strict) {
            ret |= run_test_buf(filename, harness, ip, buf, buf_len,
                                error_type, eval_flags | JS_EVAL_FLAG_STRICT,
                                is_negative, is_async, can_block);
        }
        clocks = clock() - clocks;
        if (outfile && index >= 0 && clocks >= CLOCKS_PER_SEC / 10) {
            /* output timings for tests that take more than 100 ms */
            fprintf(outfile, " time: %d ms\n", (int)(clocks * 1000LL / CLOCKS_PER_SEC));
        }
    }
    namelist_free(&include_list);
    free(error_type);
    free(buf);

    return ret;
}

/* run a test when called by test262-harness+eshost */
int run_test262_harness_test(const char *filename, BOOL is_module)
{
    JSRuntime *rt;
    JSContext *ctx;
    char *buf;
    size_t buf_len;
    int eval_flags, ret_code, ret;
    JSValue res_val;
    BOOL can_block;

    outfile = stdout; /* for js_print */

    rt = JS_NewRuntime();
    if (rt == NULL) {
        fatal(1, "JS_NewRuntime failure");
    }
    ctx = JS_NewContext(rt);
    if (ctx == NULL) {
        JS_FreeRuntime(rt);
        fatal(1, "JS_NewContext failure");
    }
    JS_SetRuntimeInfo(rt, filename);

    can_block = TRUE;
    JS_SetCanBlock(rt, can_block);

    /* loader for ES6 modules */
    JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader_test, NULL, (void *)filename);

    add_helpers(ctx);

    buf = load_file(filename, &buf_len);

    if (is_module) {
      eval_flags = JS_EVAL_TYPE_MODULE;
    } else {
      eval_flags = JS_EVAL_TYPE_GLOBAL;
    }
    res_val = JS_Eval(ctx, buf, buf_len, filename, eval_flags);
    ret_code = 0;
    if (JS_IsException(res_val)) {
       js_std_dump_error(ctx);
       ret_code = 1;
    } else {
        JSValue promise = JS_UNDEFINED;
        if (is_module) {
            promise = res_val;
        } else {
            JS_FreeValue(ctx, res_val);
        }
        for(;;) {
            ret = JS_ExecutePendingJob(JS_GetRuntime(ctx), NULL);
            if (ret < 0) {
                js_std_dump_error(ctx);
                ret_code = 1;
            } else if (ret == 0) {
                break;
            }
        }
        /* dump the error if the module returned an error. */
        if (is_module) {
            JSPromiseStateEnum state = JS_PromiseState(ctx, promise);
            if (state == JS_PROMISE_REJECTED) {
                JS_Throw(ctx, JS_PromiseResult(ctx, promise));
                js_std_dump_error(ctx);
                ret_code = 1;
            }
        }
        JS_FreeValue(ctx, promise);
    }
    free(buf);
#ifdef CONFIG_AGENT
    js_agent_free(ctx);
#endif
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return ret_code;
}

clock_t last_clock;

void show_progress(int force) {
    clock_t t = clock();
    if (force || !last_clock || (t - last_clock) > CLOCKS_PER_SEC / 20) {
        last_clock = t;
        if (compact) {
            static int last_test_skipped;
            static int last_test_failed;
            static int dots;
            char c = '.';
            if (test_skipped > last_test_skipped)
                c = '-';
            if (test_failed > last_test_failed)
                c = '!';
            last_test_skipped = test_skipped;
            last_test_failed = test_failed;
            fputc(c, stderr);
            if (force || ++dots % 60 == 0) {
                fprintf(stderr, " %d/%d/%d\n",
                        test_failed, test_count, test_skipped);
            }
        } else {
            /* output progress indicator: erase end of line and return to col 0 */
            fprintf(stderr, "%d/%d/%d\033[K\r",
                    test_failed, test_count, test_skipped);
        }
        fflush(stderr);
    }
}

static int slow_test_threshold;

void run_test_dir_list(namelist_t *lp, int start_index, int stop_index)
{
    int i;

    namelist_sort(lp);
    for (i = 0; i < lp->count; i++) {
        const char *p = lp->array[i];
        if (namelist_find(&exclude_list, p) >= 0) {
            test_excluded++;
        } else if (test_index < start_index) {
            test_skipped++;
        } else if (stop_index >= 0 && test_index > stop_index) {
            test_skipped++;
        } else {
            int ti;
            if (slow_test_threshold != 0) {
                ti = get_clock_ms();
            } else {
                ti = 0;
            }
            run_test(p, test_index);
            if (slow_test_threshold != 0) {
                ti = get_clock_ms() - ti;
                if (ti >= slow_test_threshold)
                    fprintf(stderr, "\n%s (%d ms)\n", p, ti);
            }
            show_progress(FALSE);
        }
        test_index++;
    }
    show_progress(TRUE);
}

void help(void)
{
    printf("run-test262 version " CONFIG_VERSION "\n"
           "usage: run-test262 [options] {-f file ... | [dir_list] [index range]}\n"
           "-h             help\n"
           "-a             run tests in strict and nostrict modes\n"
           "-m             print memory usage summary\n"
           "-n             use new style harness\n"
           "-N             run test prepared by test262-harness+eshost\n"
           "-s             run tests in strict mode, skip @nostrict tests\n"
           "-E             only run tests from the error file\n"
           "-C             use compact progress indicator\n"
           "-t             show timings\n"
           "-u             update error file\n"
           "-v             verbose: output error messages\n"
           "-T duration    display tests taking more than 'duration' ms\n"
           "-c file        read configuration from 'file'\n"
           "-d dir         run all test files in directory tree 'dir'\n"
           "-e file        load the known errors from 'file'\n"
           "-f file        execute single test from 'file'\n"
           "-r file        set the report file name (default=none)\n"
           "-x file        exclude tests listed in 'file'\n");
    exit(1);
}

char *get_opt_arg(const char *option, char *arg)
{
    if (!arg) {
        fatal(2, "missing argument for option %s", option);
    }
    return arg;
}

int main(int argc, char **argv)
{
    int optind, start_index, stop_index;
    BOOL is_dir_list;
    BOOL only_check_errors = FALSE;
    const char *filename;
    const char *ignore = "";
    BOOL is_test262_harness = FALSE;
    BOOL is_module = FALSE;
    BOOL count_skipped_features = FALSE;
    clock_t clocks;

#if !defined(_WIN32)
    compact = !isatty(STDERR_FILENO);
    /* Date tests assume California local time */
    setenv("TZ", "America/Los_Angeles", 1);
#endif

    optind = 1;
    while (optind < argc) {
        char *arg = argv[optind];
        if (*arg != '-')
            break;
        optind++;
        if (strstr("-c -d -e -x -f -r -E -T", arg))
            optind++;
        if (strstr("-d -f", arg))
            ignore = "testdir"; // run only the tests from -d or -f
    }

    /* cannot use getopt because we want to pass the command line to
       the script */
    optind = 1;
    is_dir_list = TRUE;
    while (optind < argc) {
        char *arg = argv[optind];
        if (*arg != '-')
            break;
        optind++;
        if (str_equal(arg, "-h")) {
            help();
        } else if (str_equal(arg, "-m")) {
            dump_memory++;
        } else if (str_equal(arg, "-n")) {
            new_style++;
        } else if (str_equal(arg, "-s")) {
            test_mode = TEST_STRICT;
        } else if (str_equal(arg, "-a")) {
            test_mode = TEST_ALL;
        } else if (str_equal(arg, "-t")) {
            show_timings++;
        } else if (str_equal(arg, "-u")) {
            update_errors++;
        } else if (str_equal(arg, "-v")) {
            verbose++;
        } else if (str_equal(arg, "-C")) {
            compact = 1;
        } else if (str_equal(arg, "-c")) {
            load_config(get_opt_arg(arg, argv[optind++]), ignore);
        } else if (str_equal(arg, "-d")) {
            enumerate_tests(get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-e")) {
            error_filename = get_opt_arg(arg, argv[optind++]);
        } else if (str_equal(arg, "-x")) {
            namelist_load(&exclude_list, get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-f")) {
            is_dir_list = FALSE;
        } else if (str_equal(arg, "-r")) {
            report_filename = get_opt_arg(arg, argv[optind++]);
        } else if (str_equal(arg, "-E")) {
            only_check_errors = TRUE;
        } else if (str_equal(arg, "-T")) {
            slow_test_threshold = atoi(get_opt_arg(arg, argv[optind++]));
        } else if (str_equal(arg, "-N")) {
            is_test262_harness = TRUE;
        } else if (str_equal(arg, "--module")) {
            is_module = TRUE;
        } else if (str_equal(arg, "--count_skipped_features")) {
            count_skipped_features = TRUE;
        } else {
            fatal(1, "unknown option: %s", arg);
            break;
        }
    }

    if (optind >= argc && !test_list.count)
        help();

    if (is_test262_harness) {
        return run_test262_harness_test(argv[optind], is_module);
    }

    error_out = stdout;
    if (error_filename) {
        error_file = load_file(error_filename, NULL);
        if (only_check_errors && error_file) {
            namelist_free(&test_list);
            namelist_add_from_error_file(&test_list, error_file);
        }
        if (update_errors) {
            free(error_file);
            error_file = NULL;
            error_out = fopen(error_filename, "w");
            if (!error_out) {
                perror_exit(1, error_filename);
            }
        }
    }

    update_exclude_dirs();

    clocks = clock();

    if (count_skipped_features) {
        /* not storage efficient but it is simple */
        size_t size;
        size = sizeof(harness_skip_features_count[0]) * strlen(harness_skip_features);
        harness_skip_features_count = malloc(size);
        memset(harness_skip_features_count, 0, size);
    }
    
    if (is_dir_list) {
        if (optind < argc && !isdigit((unsigned char)argv[optind][0])) {
            filename = argv[optind++];
            namelist_load(&test_list, filename);
        }
        start_index = 0;
        stop_index = -1;
        if (optind < argc) {
            start_index = atoi(argv[optind++]);
            if (optind < argc) {
                stop_index = atoi(argv[optind++]);
            }
        }
        if (!report_filename || str_equal(report_filename, "none")) {
            outfile = NULL;
        } else if (str_equal(report_filename, "-")) {
            outfile = stdout;
        } else {
            outfile = fopen(report_filename, "wb");
            if (!outfile) {
                perror_exit(1, report_filename);
            }
        }
        run_test_dir_list(&test_list, start_index, stop_index);

        if (outfile && outfile != stdout) {
            fclose(outfile);
            outfile = NULL;
        }
    } else {
        outfile = stdout;
        while (optind < argc) {
            run_test(argv[optind++], -1);
        }
    }

    clocks = clock() - clocks;

    if (dump_memory) {
        if (dump_memory > 1 && stats_count > 1) {
            printf("\nMininum memory statistics for %s:\n\n", stats_min_filename);
            JS_DumpMemoryUsage(stdout, &stats_min, NULL);
            printf("\nMaximum memory statistics for %s:\n\n", stats_max_filename);
            JS_DumpMemoryUsage(stdout, &stats_max, NULL);
        }
        printf("\nAverage memory statistics for %d tests:\n\n", stats_count);
        JS_DumpMemoryUsage(stdout, &stats_avg, NULL);
        printf("\n");
    }

    if (count_skipped_features) {
        size_t i, n, len = strlen(harness_skip_features);
        BOOL disp = FALSE;
        int c;
        for(i = 0; i < len; i++) {
            if (harness_skip_features_count[i] != 0) {
                if (!disp) {
                    disp = TRUE;
                    printf("%-30s %7s\n", "SKIPPED FEATURE", "COUNT");
                }
                for(n = 0; n < 30; n++) {
                    c = harness_skip_features[i + n];
                    if (is_word_sep(c))
                        break;
                    putchar(c);
                }
                for(; n < 30; n++)
                    putchar(' ');
                printf(" %7d\n", harness_skip_features_count[i]);
            }
        }
        printf("\n");
    }
    
    if (is_dir_list) {
        fprintf(stderr, "Result: %d/%d error%s",
                test_failed, test_count, test_count != 1 ? "s" : "");
        if (test_excluded)
            fprintf(stderr, ", %d excluded", test_excluded);
        if (test_skipped)
            fprintf(stderr, ", %d skipped", test_skipped);
        if (error_file) {
            if (new_errors)
                fprintf(stderr, ", %d new", new_errors);
            if (changed_errors)
                fprintf(stderr, ", %d changed", changed_errors);
            if (fixed_errors)
                fprintf(stderr, ", %d fixed", fixed_errors);
        }
        fprintf(stderr, "\n");
        if (show_timings)
            fprintf(stderr, "Total time: %.3fs\n", (double)clocks / CLOCKS_PER_SEC);
    }

    if (error_out && error_out != stdout) {
        fclose(error_out);
        error_out = NULL;
    }

    namelist_free(&test_list);
    namelist_free(&exclude_list);
    namelist_free(&exclude_dir_list);
    free(harness_dir);
    free(harness_skip_features);
    free(harness_skip_features_count);
    free(harness_features);
    free(harness_exclude);
    free(error_file);

    /* Signal that the error file is out of date. */
    return new_errors || changed_errors || fixed_errors;
}
