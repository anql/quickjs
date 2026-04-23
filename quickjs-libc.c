/*
 * QuickJS C library
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
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/stat.h>
#include <dirent.h>
#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#include <utime.h>
#else
#include <dlfcn.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#if defined(__FreeBSD__)
extern char **environ;
#endif

#if defined(__APPLE__) || defined(__FreeBSD__)
typedef sig_t sighandler_t;
#endif

#if defined(__APPLE__)
#if !defined(environ)
#include <crt_externs.h>
#define environ (*_NSGetEnviron())
#endif
#endif /* __APPLE__ */

#endif

/* enable the os.Worker API. It relies on POSIX threads */
#define USE_WORKER

#ifdef USE_WORKER
#include <pthread.h>
#include <stdatomic.h>
#endif

#include "cutils.h"
#include "list.h"
#include "quickjs-libc.h"

#if !defined(PATH_MAX)
#define PATH_MAX 4096
#endif

/* TODO:
   - add socket calls  // 待办：添加 Socket 支持
*/

/* ============================================================================
 * 数据结构定义
 * 用于管理 OS 事件循环、定时器、信号处理等
 * ============================================================================ */

/* OS 读写处理器：用于监听文件描述符的可读/可写事件 */
typedef struct {
    struct list_head link;  // 链表节点
    int fd;  // 文件描述符
    JSValue rw_func[2];  // [0]=读回调函数，[1]=写回调函数
} JSOSRWHandler;

/* OS 信号处理器：用于处理 POSIX 信号 */
typedef struct {
    struct list_head link;  // 链表节点
    int sig_num;  // 信号编号（如 SIGINT、SIGTERM 等）
    JSValue func;  // JS 回调函数
} JSOSSignalHandler;

/* OS 定时器：用于 setTimeout/setInterval */
typedef struct {
    struct list_head link;  // 链表节点
    int timer_id;  // 定时器 ID（用于 clearTimeout）
    int64_t timeout;  // 超时时间（毫秒）
    JSValue func;  // JS 回调函数
} JSOSTimer;

/* Worker 消息：用于线程间通信 */
typedef struct {
    struct list_head link;  // 链表节点
    uint8_t *data;  // 消息数据
    size_t data_len;  // 数据长度
    /* list of SharedArrayBuffers, necessary to free the message */
    uint8_t **sab_tab;  // SharedArrayBuffer 表（用于释放消息）
    size_t sab_tab_len;  // 表长度
} JSWorkerMessage;

/* Worker 唤醒器：用于唤醒阻塞的事件循环 */
typedef struct JSWaker {
#ifdef _WIN32
    HANDLE handle;  // Windows 句柄
#else
    int read_fd;   // 读端文件描述符
    int write_fd;  // 写端文件描述符（用于 eventfd 或 pipe）
#endif
} JSWaker;

/* Worker 消息管道：用于线程间消息传递 */
typedef struct {
    int ref_count;  // 引用计数
#ifdef USE_WORKER
    pthread_mutex_t mutex;  // 互斥锁（保护消息队列）
#endif
    struct list_head msg_queue; /* list of JSWorkerMessage.link */  // 消息队列
    JSWaker waker;  // 唤醒器
} JSWorkerMessagePipe;

/* Worker 消息处理器：处理接收到的消息 */
typedef struct {
    struct list_head link;  // 链表节点
    JSWorkerMessagePipe *recv_pipe;  // 接收管道
    JSValue on_message_func;  // onmessage 回调函数
} JSWorkerMessageHandler;

/* 被拒绝的 Promise 条目：用于跟踪未处理的 Promise 拒绝 */
typedef struct {
    struct list_head link;  // 链表节点
    JSValue promise;  // Promise 对象
    JSValue reason;  // 拒绝原因
} JSRejectedPromiseEntry;

/* 线程状态：每个线程都有一个实例，管理该线程的所有 OS 资源 */
typedef struct JSThreadState {
    struct list_head os_rw_handlers; /* list of JSOSRWHandler.link */  // OS 读写处理器链表
    struct list_head os_signal_handlers; /* list JSOSSignalHandler.link */  // OS 信号处理器链表
    struct list_head os_timers; /* list of JSOSTimer.link */  // OS 定时器链表
    struct list_head port_list; /* list of JSWorkerMessageHandler.link */  // Worker 端口链表
    struct list_head rejected_promise_list; /* list of JSRejectedPromiseEntry.link */  // 被拒绝 Promise 链表
    int eval_script_recurse; /* only used in the main thread */  // 脚本递归深度（主线程专用）
    int next_timer_id; /* for setTimeout() */  // 下一个定时器 ID
    /* not used in the main thread */
    JSWorkerMessagePipe *recv_pipe, *send_pipe;  // 接收/发送管道（Worker 线程专用）
} JSThreadState;

static uint64_t os_pending_signals;  // 待处理的信号位图
static int (*os_poll_func)(JSContext *ctx);  // 轮询函数指针

/* 初始化动态缓冲区，使用运行时内存分配器 */
static void js_std_dbuf_init(JSContext *ctx, DynBuf *s)
{
    dbuf_init2(s, JS_GetRuntime(ctx), (DynBufReallocFunc *)js_realloc_rt);
}

/* 自定义 isdigit：检查字符是否为数字（0-9） */
static BOOL my_isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

/* XXX: use 'o' and 'O' for object using JS_PrintValue() ? */
/**
 * 内部 printf 实现：格式化输出到文件或字符串
 * 
 * @param ctx JS 上下文
 * @param argc 参数个数
 * @param argv 参数数组
 * @param fp 输出文件指针（NULL 表示输出到字符串）
 * @returns 格式化后的字符串或写入的字节数
 * 
 * 支持的格式说明符：
 * - %c: 字符（支持 UTF-8）
 * - %d/%i/%o/%u/%x/%X: 整数（支持 32 位和 64 位）
 * - %s: 字符串
 * - %e/%f/%g/%a/%E/%F/%G/%A: 浮点数
 * - %%: 百分号本身
 */
static JSValue js_printf_internal(JSContext *ctx,
                                  int argc, JSValueConst *argv, FILE *fp)
{
    char fmtbuf[32];  // 格式缓冲区
    uint8_t cbuf[UTF8_CHAR_LEN_MAX+1];  // UTF-8 字符缓冲区
    JSValue res;  // 返回值
    DynBuf dbuf;  // 动态缓冲区
    const char *fmt_str = NULL;  // 格式字符串
    const uint8_t *fmt, *fmt_end;  // 格式字符串指针
    const uint8_t *p;  // 临时指针
    char *q;  // 格式缓冲区指针
    int i, c, len, mod;  // 循环变量、字符、长度、修饰符
    size_t fmt_len;  // 格式字符串长度
    int32_t int32_arg;  // 32 位整数参数
    int64_t int64_arg;  // 64 位整数参数
    double double_arg;  // 双精度浮点参数
    const char *string_arg;  // 字符串参数
    /* Use indirect call to dbuf_printf to prevent gcc warning */
    int (*dbuf_printf_fun)(DynBuf *s, const char *fmt, ...) = (void*)dbuf_printf;  // 间接调用避免警告

    js_std_dbuf_init(ctx, &dbuf);

    if (argc > 0) {
        fmt_str = JS_ToCStringLen(ctx, &fmt_len, argv[0]);
        if (!fmt_str)
            goto fail;

        i = 1;
        fmt = (const uint8_t *)fmt_str;
        fmt_end = fmt + fmt_len;
        while (fmt < fmt_end) {
            for (p = fmt; fmt < fmt_end && *fmt != '%'; fmt++)
                continue;
            dbuf_put(&dbuf, p, fmt - p);
            if (fmt >= fmt_end)
                break;
            q = fmtbuf;
            *q++ = *fmt++;  /* copy '%' */

            /* flags */
            for(;;) {
                c = *fmt;
                if (c == '0' || c == '#' || c == '+' || c == '-' || c == ' ' ||
                    c == '\'') {
                    if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                        goto invalid;
                    *q++ = c;
                    fmt++;
                } else {
                    break;
                }
            }
            /* width */
            if (*fmt == '*') {
                if (i >= argc)
                    goto missing;
                if (JS_ToInt32(ctx, &int32_arg, argv[i++]))
                    goto fail;
                q += snprintf(q, fmtbuf + sizeof(fmtbuf) - q, "%d", int32_arg);
                fmt++;
            } else {
                while (my_isdigit(*fmt)) {
                    if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                        goto invalid;
                    *q++ = *fmt++;
                }
            }
            if (*fmt == '.') {
                if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                    goto invalid;
                *q++ = *fmt++;
                if (*fmt == '*') {
                    if (i >= argc)
                        goto missing;
                    if (JS_ToInt32(ctx, &int32_arg, argv[i++]))
                        goto fail;
                    q += snprintf(q, fmtbuf + sizeof(fmtbuf) - q, "%d", int32_arg);
                    fmt++;
                } else {
                    while (my_isdigit(*fmt)) {
                        if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                            goto invalid;
                        *q++ = *fmt++;
                    }
                }
            }

            /* we only support the "l" modifier for 64 bit numbers */
            mod = ' ';
            if (*fmt == 'l') {
                mod = *fmt++;
            }

            /* type */
            c = *fmt++;
            if (q >= fmtbuf + sizeof(fmtbuf) - 1)
                goto invalid;
            *q++ = c;
            *q = '\0';

            switch (c) {
            case 'c':
                if (i >= argc)
                    goto missing;
                if (JS_IsString(argv[i])) {
                    string_arg = JS_ToCString(ctx, argv[i++]);
                    if (!string_arg)
                        goto fail;
                    int32_arg = unicode_from_utf8((const uint8_t *)string_arg, UTF8_CHAR_LEN_MAX, &p);
                    JS_FreeCString(ctx, string_arg);
                } else {
                    if (JS_ToInt32(ctx, &int32_arg, argv[i++]))
                        goto fail;
                }
                /* handle utf-8 encoding explicitly */
                if ((unsigned)int32_arg > 0x10FFFF)
                    int32_arg = 0xFFFD;
                /* ignore conversion flags, width and precision */
                len = unicode_to_utf8(cbuf, int32_arg);
                dbuf_put(&dbuf, cbuf, len);
                break;

            case 'd':
            case 'i':
            case 'o':
            case 'u':
            case 'x':
            case 'X':
                if (i >= argc)
                    goto missing;
                if (JS_ToInt64Ext(ctx, &int64_arg, argv[i++]))
                    goto fail;
                if (mod == 'l') {
                    /* 64 bit number */
#if defined(_WIN32)
                    if (q >= fmtbuf + sizeof(fmtbuf) - 3)
                        goto invalid;
                    q[2] = q[-1];
                    q[-1] = 'I';
                    q[0] = '6';
                    q[1] = '4';
                    q[3] = '\0';
                    dbuf_printf_fun(&dbuf, fmtbuf, (int64_t)int64_arg);
#else
                    if (q >= fmtbuf + sizeof(fmtbuf) - 2)
                        goto invalid;
                    q[1] = q[-1];
                    q[-1] = q[0] = 'l';
                    q[2] = '\0';
                    dbuf_printf_fun(&dbuf, fmtbuf, (long long)int64_arg);
#endif
                } else {
                    dbuf_printf_fun(&dbuf, fmtbuf, (int)int64_arg);
                }
                break;

            case 's':
                if (i >= argc)
                    goto missing;
                /* XXX: handle strings containing null characters */
                string_arg = JS_ToCString(ctx, argv[i++]);
                if (!string_arg)
                    goto fail;
                dbuf_printf_fun(&dbuf, fmtbuf, string_arg);
                JS_FreeCString(ctx, string_arg);
                break;

            case 'e':
            case 'f':
            case 'g':
            case 'a':
            case 'E':
            case 'F':
            case 'G':
            case 'A':
                if (i >= argc)
                    goto missing;
                if (JS_ToFloat64(ctx, &double_arg, argv[i++]))
                    goto fail;
                dbuf_printf_fun(&dbuf, fmtbuf, double_arg);
                break;

            case '%':
                dbuf_putc(&dbuf, '%');
                break;

            default:
                /* XXX: should support an extension mechanism */
            invalid:
                JS_ThrowTypeError(ctx, "invalid conversion specifier in format string");
                goto fail;
            missing:
                JS_ThrowReferenceError(ctx, "missing argument for conversion specifier");
                goto fail;
            }
        }
        JS_FreeCString(ctx, fmt_str);
    }
    if (dbuf.error) {
        res = JS_ThrowOutOfMemory(ctx);
    } else {
        if (fp) {
            len = fwrite(dbuf.buf, 1, dbuf.size, fp);
            res = JS_NewInt32(ctx, len);
        } else {
            res = JS_NewStringLen(ctx, (char *)dbuf.buf, dbuf.size);
        }
    }
    dbuf_free(&dbuf);
    return res;

fail:
    JS_FreeCString(ctx, fmt_str);
    dbuf_free(&dbuf);
    return JS_EXCEPTION;
}

/**
 * 加载文件到内存
 * 
 * @param ctx JS 上下文（NULL 表示使用系统 malloc）
 * @param pbuf_len 输出参数：返回缓冲区长度
 * @param filename 文件名
 * @returns 文件内容缓冲区（调用者负责释放），失败返回 NULL
 * 
 * 注意：
 * - 会在缓冲区末尾添加 '\0' 终止符
 * - 如果是目录会返回 EISDIR 错误
 */
uint8_t *js_load_file(JSContext *ctx, size_t *pbuf_len, const char *filename)
{
    FILE *f;  // 文件指针
    uint8_t *buf;  // 缓冲区
    size_t buf_len;  // 缓冲区长度
    long lret;  // 临时返回值

    f = fopen(filename, "rb");  // 以二进制模式打开
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) < 0)  // 定位到文件末尾
        goto fail;
    lret = ftell(f);  // 获取文件大小
    if (lret < 0)
        goto fail;
    /* XXX: on Linux, ftell() return LONG_MAX for directories */
    if (lret == LONG_MAX) {  // 目录的特殊情况
        errno = EISDIR;
        goto fail;
    }
    buf_len = lret;
    if (fseek(f, 0, SEEK_SET) < 0)  // 回到文件开头
        goto fail;
    if (ctx)
        buf = js_malloc(ctx, buf_len + 1);  // 使用 JS 内存分配器
    else
        buf = malloc(buf_len + 1);  // 使用系统 malloc
    if (!buf)
        goto fail;
    if (fread(buf, 1, buf_len, f) != buf_len) {  // 读取文件
        errno = EIO;  // I/O 错误
        if (ctx)
            js_free(ctx, buf);
        else
            free(buf);
    fail:
        fclose(f);
        return NULL;
    }
    buf[buf_len] = '\0';  // 添加终止符
    fclose(f);
    *pbuf_len = buf_len;
    return buf;
}

/* load and evaluate a file */
static JSValue js_loadScript(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    JSValue ret;
    size_t buf_len;

    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        JS_ThrowReferenceError(ctx, "could not load '%s'", filename);
        JS_FreeCString(ctx, filename);
        return JS_EXCEPTION;
    }
    ret = JS_Eval(ctx, (char *)buf, buf_len, filename,
                  JS_EVAL_TYPE_GLOBAL);
    js_free(ctx, buf);
    JS_FreeCString(ctx, filename);
    return ret;
}

/* load a file as a UTF-8 encoded string */
static JSValue js_std_loadFile(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    uint8_t *buf;
    const char *filename;
    JSValue ret;
    size_t buf_len;

    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;
    buf = js_load_file(ctx, &buf_len, filename);
    JS_FreeCString(ctx, filename);
    if (!buf)
        return JS_NULL;
    ret = JS_NewStringLen(ctx, (char *)buf, buf_len);
    js_free(ctx, buf);
    return ret;
}

typedef JSModuleDef *(JSInitModuleFunc)(JSContext *ctx,
                                        const char *module_name);


#if defined(_WIN32)
static JSModuleDef *js_module_loader_so(JSContext *ctx,
                                        const char *module_name)
{
    JS_ThrowReferenceError(ctx, "shared library modules are not supported yet");
    return NULL;
}
#else
static JSModuleDef *js_module_loader_so(JSContext *ctx,
                                        const char *module_name)
{
    JSModuleDef *m;
    void *hd;
    JSInitModuleFunc *init;
    char *filename;

    if (!strchr(module_name, '/')) {
        /* must add a '/' so that the DLL is not searched in the
           system library paths */
        filename = js_malloc(ctx, strlen(module_name) + 2 + 1);
        if (!filename)
            return NULL;
        strcpy(filename, "./");
        strcpy(filename + 2, module_name);
    } else {
        filename = (char *)module_name;
    }

    /* C module */
    hd = dlopen(filename, RTLD_NOW | RTLD_LOCAL);
    if (filename != module_name)
        js_free(ctx, filename);
    if (!hd) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s' as shared library",
                               module_name);
        goto fail;
    }

    init = dlsym(hd, "js_init_module");
    if (!init) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s': js_init_module not found",
                               module_name);
        goto fail;
    }

    m = init(ctx, module_name);
    if (!m) {
        JS_ThrowReferenceError(ctx, "could not load module filename '%s': initialization error",
                               module_name);
    fail:
        if (hd)
            dlclose(hd);
        return NULL;
    }
    return m;
}
#endif /* !_WIN32 */

int js_module_set_import_meta(JSContext *ctx, JSValueConst func_val,
                              JS_BOOL use_realpath, JS_BOOL is_main)
{
    JSModuleDef *m;
    char buf[PATH_MAX + 16];
    JSValue meta_obj;
    JSAtom module_name_atom;
    const char *module_name;

    assert(JS_VALUE_GET_TAG(func_val) == JS_TAG_MODULE);
    m = JS_VALUE_GET_PTR(func_val);

    module_name_atom = JS_GetModuleName(ctx, m);
    module_name = JS_AtomToCString(ctx, module_name_atom);
    JS_FreeAtom(ctx, module_name_atom);
    if (!module_name)
        return -1;
    if (!strchr(module_name, ':')) {
        strcpy(buf, "file://");
#if !defined(_WIN32)
        /* realpath() cannot be used with modules compiled with qjsc
           because the corresponding module source code is not
           necessarily present */
        if (use_realpath) {
            char *res = realpath(module_name, buf + strlen(buf));
            if (!res) {
                JS_ThrowTypeError(ctx, "realpath failure");
                JS_FreeCString(ctx, module_name);
                return -1;
            }
        } else
#endif
        {
            pstrcat(buf, sizeof(buf), module_name);
        }
    } else {
        pstrcpy(buf, sizeof(buf), module_name);
    }
    JS_FreeCString(ctx, module_name);

    meta_obj = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta_obj))
        return -1;
    JS_DefinePropertyValueStr(ctx, meta_obj, "url",
                              JS_NewString(ctx, buf),
                              JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta_obj, "main",
                              JS_NewBool(ctx, is_main),
                              JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta_obj);
    return 0;
}

static int json_module_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue val;
    val = JS_GetModulePrivateValue(ctx, m);
    JS_SetModuleExport(ctx, m, "default", val);
    return 0;
}

static JSModuleDef *create_json_module(JSContext *ctx, const char *module_name, JSValue val)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, json_module_init);
    if (!m) {
        JS_FreeValue(ctx, val);
        return NULL;
    }
    /* only export the "default" symbol which will contain the JSON object */
    JS_AddModuleExport(ctx, m, "default");
    JS_SetModulePrivateValue(ctx, m, val);
    return m;
}

/* in order to conform with the specification, only the keys should be
   tested and not the associated values. */
int js_module_check_attributes(JSContext *ctx, void *opaque,
                               JSValueConst attributes)
{
    JSPropertyEnum *tab;
    uint32_t i, len;
    int ret;
    const char *cstr;
    size_t cstr_len;
    
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, attributes, JS_GPN_ENUM_ONLY | JS_GPN_STRING_MASK))
        return -1;
    ret = 0;
    for(i = 0; i < len; i++) {
        cstr = JS_AtomToCStringLen(ctx, &cstr_len, tab[i].atom);
        if (!cstr) {
            ret = -1;
            break;
        }
        if (!(cstr_len == 4 && !memcmp(cstr, "type", cstr_len))) {
            JS_ThrowTypeError(ctx, "import attribute '%s' is not supported", cstr);
            ret = -1;
        }
        JS_FreeCString(ctx, cstr);
        if (ret)
            break;
    }
    JS_FreePropertyEnum(ctx, tab, len);
    return ret;
}

/* return > 0 if the attributes indicate a JSON module */
int js_module_test_json(JSContext *ctx, JSValueConst attributes)
{
    JSValue str;
    const char *cstr;
    size_t len;
    BOOL res;

    if (JS_IsUndefined(attributes))
        return FALSE;
    str = JS_GetPropertyStr(ctx, attributes, "type");
    if (!JS_IsString(str))
        return FALSE;
    cstr = JS_ToCStringLen(ctx, &len, str);
    JS_FreeValue(ctx, str);
    if (!cstr)
        return FALSE;
    /* XXX: raise an error if unknown type ? */
    if (len == 4 && !memcmp(cstr, "json", len)) {
        res = 1;
    } else if (len == 5 && !memcmp(cstr, "json5", len)) {
        res = 2;
    } else {
        res = 0;
    }
    JS_FreeCString(ctx, cstr);
    return res;
}

JSModuleDef *js_module_loader(JSContext *ctx,
                              const char *module_name, void *opaque,
                              JSValueConst attributes)
{
    JSModuleDef *m;
    int res;
    
    if (has_suffix(module_name, ".so")) {
        m = js_module_loader_so(ctx, module_name);
    } else {
        size_t buf_len;
        uint8_t *buf;

        buf = js_load_file(ctx, &buf_len, module_name);
        if (!buf) {
            JS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                                   module_name);
            return NULL;
        }
        res = js_module_test_json(ctx, attributes);
        if (has_suffix(module_name, ".json") || res > 0) {
            /* compile as JSON or JSON5 depending on "type" */
            JSValue val;
            int flags;
            if (res == 2)
                flags = JS_PARSE_JSON_EXT;
            else
                flags = 0;
            val = JS_ParseJSON2(ctx, (char *)buf, buf_len, module_name, flags);
            js_free(ctx, buf);
            if (JS_IsException(val))
                return NULL;
            m = create_json_module(ctx, module_name, val);
            if (!m)
                return NULL;
        } else {
            JSValue func_val;
            /* compile the module */
            func_val = JS_Eval(ctx, (char *)buf, buf_len, module_name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
            js_free(ctx, buf);
            if (JS_IsException(func_val))
                return NULL;
            /* XXX: could propagate the exception */
            js_module_set_import_meta(ctx, func_val, TRUE, FALSE);
            /* the module is already referenced, so we must free it */
            m = JS_VALUE_GET_PTR(func_val);
            JS_FreeValue(ctx, func_val);
        }
    }
    return m;
}

static JSValue js_std_exit(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    int status;
    if (JS_ToInt32(ctx, &status, argv[0]))
        status = -1;
    exit(status);
    return JS_UNDEFINED;
}

static JSValue js_std_getenv(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    const char *name, *str;
    name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;
    str = getenv(name);
    JS_FreeCString(ctx, name);
    if (!str)
        return JS_UNDEFINED;
    else
        return JS_NewString(ctx, str);
}

#if defined(_WIN32)
static void setenv(const char *name, const char *value, int overwrite)
{
    char *str;
    size_t name_len, value_len;
    name_len = strlen(name);
    value_len = strlen(value);
    str = malloc(name_len + 1 + value_len + 1);
    memcpy(str, name, name_len);
    str[name_len] = '=';
    memcpy(str + name_len + 1, value, value_len);
    str[name_len + 1 + value_len] = '\0';
    _putenv(str);
    free(str);
}

static void unsetenv(const char *name)
{
    setenv(name, "", TRUE);
}
#endif /* _WIN32 */

static JSValue js_std_setenv(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    const char *name, *value;
    name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;
    value = JS_ToCString(ctx, argv[1]);
    if (!value) {
        JS_FreeCString(ctx, name);
        return JS_EXCEPTION;
    }
    setenv(name, value, TRUE);
    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);
    return JS_UNDEFINED;
}

static JSValue js_std_unsetenv(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    const char *name;
    name = JS_ToCString(ctx, argv[0]);
    if (!name)
        return JS_EXCEPTION;
    unsetenv(name);
    JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

/* return an object containing the list of the available environment
   variables. */
static JSValue js_std_getenviron(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    char **envp;
    const char *name, *p, *value;
    JSValue obj;
    uint32_t idx;
    size_t name_len;
    JSAtom atom;
    int ret;

    obj = JS_NewObject(ctx);
    if (JS_IsException(obj))
        return JS_EXCEPTION;
    envp = environ;
    for(idx = 0; envp[idx] != NULL; idx++) {
        name = envp[idx];
        p = strchr(name, '=');
        name_len = p - name;
        if (!p)
            continue;
        value = p + 1;
        atom = JS_NewAtomLen(ctx, name, name_len);
        if (atom == JS_ATOM_NULL)
            goto fail;
        ret = JS_DefinePropertyValue(ctx, obj, atom, JS_NewString(ctx, value),
                                     JS_PROP_C_W_E);
        JS_FreeAtom(ctx, atom);
        if (ret < 0)
            goto fail;
    }
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_std_gc(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    JS_RunGC(JS_GetRuntime(ctx));
    return JS_UNDEFINED;
}

static int interrupt_handler(JSRuntime *rt, void *opaque)
{
    return (os_pending_signals >> SIGINT) & 1;
}

static int get_bool_option(JSContext *ctx, BOOL *pbool,
                           JSValueConst obj,
                           const char *option)
{
    JSValue val;
    val = JS_GetPropertyStr(ctx, obj, option);
    if (JS_IsException(val))
        return -1;
    if (!JS_IsUndefined(val)) {
        *pbool = JS_ToBool(ctx, val);
    }
    JS_FreeValue(ctx, val);
    return 0;
}

static JSValue js_evalScript(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    const char *str;
    size_t len;
    JSValue ret;
    JSValueConst options_obj;
    BOOL backtrace_barrier = FALSE;
    BOOL is_async = FALSE;
    int flags;

    if (argc >= 2) {
        options_obj = argv[1];
        if (get_bool_option(ctx, &backtrace_barrier, options_obj,
                            "backtrace_barrier"))
            return JS_EXCEPTION;
        if (get_bool_option(ctx, &is_async, options_obj,
                            "async"))
            return JS_EXCEPTION;
    }

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    if (!ts->recv_pipe && ++ts->eval_script_recurse == 1) {
        /* install the interrupt handler */
        JS_SetInterruptHandler(JS_GetRuntime(ctx), interrupt_handler, NULL);
    }
    flags = JS_EVAL_TYPE_GLOBAL;
    if (backtrace_barrier)
        flags |= JS_EVAL_FLAG_BACKTRACE_BARRIER;
    if (is_async)
        flags |= JS_EVAL_FLAG_ASYNC;
    ret = JS_Eval(ctx, str, len, "<evalScript>", flags);
    JS_FreeCString(ctx, str);
    if (!ts->recv_pipe && --ts->eval_script_recurse == 0) {
        /* remove the interrupt handler */
        JS_SetInterruptHandler(JS_GetRuntime(ctx), NULL, NULL);
        os_pending_signals &= ~((uint64_t)1 << SIGINT);
        /* convert the uncatchable "interrupted" error into a normal error
           so that it can be caught by the REPL */
        if (JS_IsException(ret))
            JS_SetUncatchableException(ctx, FALSE);
    }
    return ret;
}

/* JS 标准文件类 ID */
static JSClassID js_std_file_class_id;

/* JS 文件对象：封装 FILE* 指针 */
typedef struct {
    FILE *f;  // C 文件指针
    BOOL close_in_finalizer;  // 是否在析构时关闭
    BOOL is_popen;  // 是否是通过 popen 打开的
} JSSTDFile;

/**
 * JS 文件对象析构函数
 * 当 JS 对象被 GC 回收时调用
 */
static void js_std_file_finalizer(JSRuntime *rt, JSValue val)
{
    JSSTDFile *s = JS_GetOpaque(val, js_std_file_class_id);
    if (s) {
        if (s->f && s->close_in_finalizer) {  // 需要关闭文件
            if (s->is_popen)
                pclose(s->f);  // popen 打开的用 pclose
            else
                fclose(s->f);  // 普通打开的用 fclose
        }
        js_free_rt(rt, s);  // 释放内存
    }
}

/**
 * 获取错误号：将 -1 返回值转换为负的错误码
 */
static ssize_t js_get_errno(ssize_t ret)
{
    if (ret == -1)
        ret = -errno;  // 系统调用失败时返回负的错误码
    return ret;
}

/**
 * strerror 包装：将错误码转换为错误消息字符串
 * js_std_strerror(errno) → "No such file or directory"
 */
static JSValue js_std_strerror(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    int err;
    if (JS_ToInt32(ctx, &err, argv[0]))  // 获取错误码参数
        return JS_EXCEPTION;
    return JS_NewString(ctx, strerror(err));  // 返回错误消息
}

static JSValue js_std_parseExtJSON(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    JSValue obj;
    const char *str;
    size_t len;

    str = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!str)
        return JS_EXCEPTION;
    obj = JS_ParseJSON2(ctx, str, len, "<input>", JS_PARSE_JSON_EXT);
    JS_FreeCString(ctx, str);
    return obj;
}

/**
 * 创建 JS 文件对象
 * 
 * @param ctx JS 上下文
 * @param f C 文件指针
 * @param close_in_finalizer 是否在 GC 时自动关闭
 * @param is_popen 是否是通过 popen 打开的
 * @returns JS 文件对象
 */
static JSValue js_new_std_file(JSContext *ctx, FILE *f,
                               BOOL close_in_finalizer,
                               BOOL is_popen)
{
    JSSTDFile *s;
    JSValue obj;
    obj = JS_NewObjectClass(ctx, js_std_file_class_id);  // 创建文件类实例
    if (JS_IsException(obj))
        return obj;
    s = js_mallocz(ctx, sizeof(*s));  // 分配并清零
    if (!s) {
        JS_FreeValue(ctx, obj);
        return JS_EXCEPTION;
    }
    s->close_in_finalizer = close_in_finalizer;
    s->is_popen = is_popen;
    s->f = f;
    JS_SetOpaque(obj, s);  // 将 C 指针关联到 JS 对象
    return obj;
}

/**
 * 设置错误对象：给对象添加 errno 属性
 */
static void js_set_error_object(JSContext *ctx, JSValue obj, int err)
{
    if (!JS_IsUndefined(obj)) {
        JS_SetPropertyStr(ctx, obj, "errno", JS_NewInt32(ctx, err));
    }
}

/**
 * std.open(filename, mode[, errorObj]) - 打开文件
 * 
 * @param argv[0] 文件名
 * @param argv[1] 模式（r/w/a/+/b 组合）
 * @param argv[2] 可选：错误对象（用于返回 errno）
 * @returns 文件对象，失败返回 NULL
 * 
 * 示例：
 *   const f = std.open('test.txt', 'r');
 *   const f = std.open('data.bin', 'rb');
 */
static JSValue js_std_open(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    const char *filename, *mode = NULL;
    FILE *f;
    int err;

    filename = JS_ToCString(ctx, argv[0]);  // 获取文件名
    if (!filename)
        goto fail;
    mode = JS_ToCString(ctx, argv[1]);  // 获取模式
    if (!mode)
        goto fail;
    // 验证模式字符串：只允许 r/w/a/+/b 字符
    if (mode[strspn(mode, "rwa+b")] != '\0') {
        JS_ThrowTypeError(ctx, "invalid file mode");
        goto fail;
    }

    f = fopen(filename, mode);  // 打开文件
    if (!f)
        err = errno;  // 记录错误码
    else
        err = 0;
    if (argc >= 3)
        js_set_error_object(ctx, argv[2], err);  // 设置错误对象
    JS_FreeCString(ctx, filename);
    JS_FreeCString(ctx, mode);
    if (!f)
        return JS_NULL;  // 失败返回 NULL
    return js_new_std_file(ctx, f, TRUE, FALSE);  // 创建文件对象（需要关闭）
 fail:
    JS_FreeCString(ctx, filename);
    JS_FreeCString(ctx, mode);
    return JS_EXCEPTION;
}

/**
 * std.popen(command, mode[, errorObj]) - 打开管道
 * 
 * @param argv[0] 命令字符串
 * @param argv[1] 模式（r=读取管道/w=写入管道）
 * @param argv[2] 可选：错误对象
 * @returns 文件对象，失败返回 NULL
 * 
 * 示例：
 *   const f = std.popen('ls -la', 'r');  // 读取命令输出
 *   const f = std.popen('cat > out.txt', 'w');  // 写入命令
 */
static JSValue js_std_popen(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    const char *filename, *mode = NULL;
    FILE *f;
    int err;

    filename = JS_ToCString(ctx, argv[0]);  // 获取命令
    if (!filename)
        goto fail;
    mode = JS_ToCString(ctx, argv[1]);  // 获取模式
    if (!mode)
        goto fail;
    // popen 只支持 r/w 模式
    if (mode[strspn(mode, "rw")] != '\0') {
        JS_ThrowTypeError(ctx, "invalid file mode");
        goto fail;
    }

    f = popen(filename, mode);  // 打开管道
    if (!f)
        err = errno;
    else
        err = 0;
    if (argc >= 3)
        js_set_error_object(ctx, argv[2], err);
    JS_FreeCString(ctx, filename);
    JS_FreeCString(ctx, mode);
    if (!f)
        return JS_NULL;
    return js_new_std_file(ctx, f, TRUE, TRUE);  // is_popen=TRUE（用 pclose 关闭）
 fail:
    JS_FreeCString(ctx, filename);
    JS_FreeCString(ctx, mode);
    return JS_EXCEPTION;
}

/**
 * std.fdopen(fd, mode[, errorObj]) - 文件描述符转文件对象
 * 
 * @param argv[0] 文件描述符（整数）
 * @param argv[1] 模式（r/w/a/+）
 * @param argv[2] 可选：错误对象
 * @returns 文件对象，失败返回 NULL
 * 
 * 示例：
 *   const f = std.fdopen(0, 'r');  // stdin
 *   const f = std.fdopen(1, 'w');  // stdout
 */
static JSValue js_std_fdopen(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *mode;
    FILE *f;
    int fd, err;

    if (JS_ToInt32(ctx, &fd, argv[0]))  // 获取文件描述符
        return JS_EXCEPTION;
    mode = JS_ToCString(ctx, argv[1]);
    if (!mode)
        goto fail;
    // 验证模式
    if (mode[strspn(mode, "rwa+")] != '\0') {
        JS_ThrowTypeError(ctx, "invalid file mode");
        goto fail;
    }

    f = fdopen(fd, mode);  // 将 fd 转换为 FILE*
    if (!f)
        err = errno;
    else
        err = 0;
    if (argc >= 3)
        js_set_error_object(ctx, argv[2], err);
    JS_FreeCString(ctx, mode);
    if (!f)
        return JS_NULL;
    return js_new_std_file(ctx, f, TRUE, FALSE);
 fail:
    JS_FreeCString(ctx, mode);
    return JS_EXCEPTION;
}

/**
 * std.tmpfile() - 创建临时文件
 * @returns 临时文件对象（自动删除），失败返回 NULL
 */
static JSValue js_std_tmpfile(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    FILE *f;
    f = tmpfile();  // 创建临时文件（关闭时自动删除）
    if (argc >= 1)
        js_set_error_object(ctx, argv[0], f ? 0 : errno);
    if (!f)
        return JS_NULL;
    return js_new_std_file(ctx, f, TRUE, FALSE);
}

/**
 * std.sprintf(format, ...args) - 格式化字符串
 * @returns 格式化后的字符串
 * 
 * 示例：std.sprintf('Hello %s, you are %d years old', 'Alice', 25)
 */
static JSValue js_std_sprintf(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    return js_printf_internal(ctx, argc, argv, NULL);  // 输出到字符串
}

/**
 * std.printf(format, ...args) - 格式化输出到 stdout
 * @returns 写入的字符数
 * 
 * 示例：std.printf('Progress: %d%%\n', 50)
 */
static JSValue js_std_printf(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    return js_printf_internal(ctx, argc, argv, stdout);  // 输出到 stdout
}

/**
 * 从 JS 文件对象获取 C FILE 指针
 * @param obj JS 文件对象
 * @returns FILE 指针，失败返回 NULL
 */
static FILE *js_std_file_get(JSContext *ctx, JSValueConst obj)
{
    JSSTDFile *s = JS_GetOpaque2(ctx, obj, js_std_file_class_id);
    if (!s)
        return NULL;
    if (!s->f) {
        JS_ThrowTypeError(ctx, "invalid file handle");  // 文件已关闭
        return NULL;
    }
    return s->f;
}

static JSValue js_std_file_puts(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv, int magic)
{
    FILE *f;
    int i;
    const char *str;
    size_t len;

    if (magic == 0) {
        f = stdout;
    } else {
        f = js_std_file_get(ctx, this_val);
        if (!f)
            return JS_EXCEPTION;
    }

    for(i = 0; i < argc; i++) {
        str = JS_ToCStringLen(ctx, &len, argv[i]);
        if (!str)
            return JS_EXCEPTION;
        fwrite(str, 1, len, f);
        JS_FreeCString(ctx, str);
    }
    return JS_UNDEFINED;
}

/**
 * @brief 关闭文件流
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 成功返回 0，失败返回错误码
 * 
 * 关闭文件流，如果是由 popen 打开的管道则调用 pclose，
 * 否则调用 fclose。关闭后将文件指针置为 NULL。
 */
static JSValue js_std_file_close(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    JSSTDFile *s = JS_GetOpaque2(ctx, this_val, js_std_file_class_id);
    int err;
    if (!s)
        return JS_EXCEPTION;
    if (!s->f)
        return JS_ThrowTypeError(ctx, "invalid file handle");
    if (s->is_popen)
        err = js_get_errno(pclose(s->f));
    else
        err = js_get_errno(fclose(s->f));
    s->f = NULL;
    return JS_NewInt32(ctx, err);
}

/**
 * @brief 格式化输出到文件
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 格式化输出结果
 * 
 * 将格式化的字符串写入文件流，支持 printf 风格的格式说明符。
 */
static JSValue js_std_file_printf(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    return js_printf_internal(ctx, argc, argv, f);
}

static void js_print_value_write(void *opaque, const char *buf, size_t len)
{
    FILE *fo = opaque;
    fwrite(buf, 1, len, fo);
}

static JSValue js_std_file_printObject(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    JS_PrintValue(ctx, js_print_value_write, stdout, argv[0], NULL);
    return JS_UNDEFINED;
}

/**
 * @brief 刷新文件流缓冲区
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return JS_UNDEFINED
 * 
 * 强制将缓冲区中的数据写入底层文件描述符，确保数据落盘。
 */
static JSValue js_std_file_flush(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    fflush(f);
    return JS_UNDEFINED;
}

/**
 * @brief 获取文件当前位置
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @param is_bigint 是否返回 BigInt 类型
 * @return 文件偏移量 (字节)
 * 
 * 返回文件流当前的读写位置。在 Linux/GLIBC 系统上使用 ftello，
 * 其他系统使用 ftell。支持 BigInt 返回值以处理大文件。
 */
static JSValue js_std_file_tell(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv, int is_bigint)
{
    FILE *f = js_std_file_get(ctx, this_val);
    int64_t pos;
    if (!f)
        return JS_EXCEPTION;
#if defined(__linux__) || defined(__GLIBC__)
    pos = ftello(f);
#else
    pos = ftell(f);
#endif
    if (is_bigint)
        return JS_NewBigInt64(ctx, pos);
    else
        return JS_NewInt64(ctx, pos);
}

/**
 * @brief 移动文件读写位置
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组 [0]=偏移量，[1]=起始位置 (SEEK_SET/SEEK_CUR/SEEK_END)
 * @return 成功返回 0，失败返回 -1
 * 
 * 将文件流的读写位置移动到指定位置。支持三种寻址方式：
 * - SEEK_SET: 从文件开头开始计算
 * - SEEK_CUR: 从当前位置开始计算
 * - SEEK_END: 从文件末尾开始计算
 */
static JSValue js_std_file_seek(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    int64_t pos;
    int whence, ret;
    if (!f)
        return JS_EXCEPTION;
    if (JS_ToInt64Ext(ctx, &pos, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &whence, argv[1]))
        return JS_EXCEPTION;
#if defined(__linux__) || defined(__GLIBC__)
    ret = fseeko(f, pos, whence);
#else
    ret = fseek(f, pos, whence);
#endif
    if (ret < 0)
        ret = -errno;
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 检查是否到达文件末尾 (EOF)
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 到达文件末尾返回 true，否则返回 false
 */
static JSValue js_std_file_eof(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, feof(f));
}

/**
 * @brief 检查文件流是否发生错误
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 发生错误返回 true，否则返回 false
 */
static JSValue js_std_file_error(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    return JS_NewBool(ctx, ferror(f));
}

/**
 * @brief 清除文件流错误标志
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return JS_UNDEFINED
 * 
 * 清除文件流的错误指示器和 EOF 标志，使文件流可以继续使用。
 */
static JSValue js_std_file_clearerr(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    clearerr(f);
    return JS_UNDEFINED;
}

/**
 * @brief 获取文件描述符
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 底层文件描述符 (整数)
 * 
 * 返回与 FILE 流关联的底层文件描述符，可用于底层 I/O 操作。
 */
static JSValue js_std_file_fileno(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, fileno(f));
}

/**
 * @brief 文件读写操作
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组 [0]=ArrayBuffer, [1]=起始位置，[2]=长度
 * @param magic 0=读，1=写
 * @return 实际读写的字节数
 * 
 * 从/向文件读写 ArrayBuffer 数据。magic 参数决定操作类型：
 * - magic=0: 从文件读取数据到 ArrayBuffer
 * - magic=1: 将 ArrayBuffer 数据写入文件
 */
static JSValue js_std_file_read_write(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic)
{
    FILE *f = js_std_file_get(ctx, this_val);
    uint64_t pos, len;
    size_t size, ret;
    uint8_t *buf;

    if (!f)
        return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &pos, argv[1]))
        return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &len, argv[2]))
        return JS_EXCEPTION;
    buf = JS_GetArrayBuffer(ctx, &size, argv[0]);
    if (!buf)
        return JS_EXCEPTION;
    if (pos + len > size)
        return JS_ThrowRangeError(ctx, "read/write array buffer overflow");
    if (magic)
        ret = fwrite(buf + pos, 1, len, f);
    else
        ret = fread(buf + pos, 1, len, f);
    return JS_NewInt64(ctx, ret);
}

/**
 * @brief 从文件读取一行
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 读取的行 (不含换行符)，EOF 且无数据时返回 null
 * 
 * 逐字符读取文件直到遇到换行符或 EOF。使用 DynBuf 动态缓冲区
 * 存储结果，自动处理内存分配。返回的字符串不包含末尾的换行符。
 */
/* XXX: could use less memory and go faster */
static JSValue js_std_file_getline(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    int c;
    DynBuf dbuf;
    JSValue obj;

    if (!f)
        return JS_EXCEPTION;

    js_std_dbuf_init(ctx, &dbuf);
    for(;;) {
        c = fgetc(f);
        if (c == EOF) {
            if (dbuf.size == 0) {
                /* EOF */
                dbuf_free(&dbuf);
                return JS_NULL;
            } else {
                break;
            }
        }
        if (c == '\n')
            break;
        if (dbuf_putc(&dbuf, c)) {
            dbuf_free(&dbuf);
            return JS_ThrowOutOfMemory(ctx);
        }
    }
    obj = JS_NewStringLen(ctx, (const char *)dbuf.buf, dbuf.size);
    dbuf_free(&dbuf);
    return obj;
}

/**
 * @brief 将文件内容读取为字符串
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组 [0]=可选的最大读取字节数
 * @return 读取的字符串
 * 
 * 从文件读取内容直到 EOF 或达到指定的最大字节数。
 * 使用 DynBuf 动态缓冲区累积字符，最后转换为 JS 字符串返回。
 */
/* XXX: could use less memory and go faster */
static JSValue js_std_file_readAsString(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    int c;
    DynBuf dbuf;
    JSValue obj;
    uint64_t max_size64;
    size_t max_size;
    JSValueConst max_size_val;

    if (!f)
        return JS_EXCEPTION;

    if (argc >= 1)
        max_size_val = argv[0];
    else
        max_size_val = JS_UNDEFINED;
    max_size = (size_t)-1;
    if (!JS_IsUndefined(max_size_val)) {
        if (JS_ToIndex(ctx, &max_size64, max_size_val))
            return JS_EXCEPTION;
        if (max_size64 < max_size)
            max_size = max_size64;
    }

    js_std_dbuf_init(ctx, &dbuf);
    while (max_size != 0) {
        c = fgetc(f);
        if (c == EOF)
            break;
        if (dbuf_putc(&dbuf, c)) {
            dbuf_free(&dbuf);
            return JS_EXCEPTION;
        }
        max_size--;
    }
    obj = JS_NewStringLen(ctx, (const char *)dbuf.buf, dbuf.size);
    dbuf_free(&dbuf);
    return obj;
}

/**
 * @brief 从文件读取一个字节
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 读取的字节值 (0-255)，EOF 返回 -1
 */
static JSValue js_std_file_getByte(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    if (!f)
        return JS_EXCEPTION;
    return JS_NewInt32(ctx, fgetc(f));
}

/**
 * @brief 向文件写入一个字节
 * @param ctx JS 上下文
 * @param this_val this 对象 (FILE 实例)
 * @param argc 参数个数
 * @param argv 参数数组 [0]=要写入的字节值
 * @return 写入的字节值，错误返回 -1
 */
static JSValue js_std_file_putByte(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    FILE *f = js_std_file_get(ctx, this_val);
    int c;
    if (!f)
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &c, argv[0]))
        return JS_EXCEPTION;
    c = fputc(c, f);
    return JS_NewInt32(ctx, c);
}

/* urlGet */

#define URL_GET_PROGRAM "curl -s -i --"
#define URL_GET_BUF_SIZE 4096

/**
 * @brief 读取 HTTP 响应头的一行
 * @param f 文件流
 * @param buf 缓冲区
 * @param buf_size 缓冲区大小
 * @param dbuf 可选的动态缓冲区用于累积数据
 * @return 成功返回 0，失败返回 -1
 * 
 * 从文件流读取一行 HTTP 响应头，直到遇到换行符。
 * 如果提供了 dbuf 参数，则同时将数据累积到动态缓冲区中。
 */
static int http_get_header_line(FILE *f, char *buf, size_t buf_size,
                                DynBuf *dbuf)
{
    int c;
    char *p;

    p = buf;
    for(;;) {
        c = fgetc(f);
        if (c < 0)
            return -1;
        if ((p - buf) < buf_size - 1)
            *p++ = c;
        if (dbuf)
            dbuf_putc(dbuf, c);
        if (c == '\n')
            break;
    }
    *p = '\0';
    return 0;
}

/**
 * @brief 解析 HTTP 状态码
 * @param buf HTTP 状态行 (如 "HTTP/1.1 200 OK")
 * @return HTTP 状态码 (如 200, 404 等)
 * 
 * 从 HTTP 状态行中提取状态码数字。格式：HTTP/x.x STATUS REASON
 */
static int http_get_status(const char *buf)
{
    const char *p = buf;
    while (*p != ' ' && *p != '\0')
        p++;
    if (*p != ' ')
        return 0;
    while (*p == ' ')
        p++;
    return atoi(p);
}

/**
 * @brief 通过 curl 下载 URL 内容
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=URL, [1]=可选选项对象 {binary, full}
 * @return 下载的内容 (字符串或 ArrayBuffer)，full 模式下返回响应对象
 * 
 * 使用 curl 命令行工具下载 HTTP/HTTPS URL 内容。支持以下选项：
 * - binary: true 返回 ArrayBuffer，false 返回字符串 (默认)
 * - full: true 返回完整响应对象 {response, responseHeaders, status}
 * 
 * 实现细节：
 * 1. 构造 curl 命令，对 URL 进行 shell 转义
 * 2. 使用 popen 执行 curl，捕获输出
 * 3. 解析 HTTP 状态行和响应头
 * 4. 读取响应体数据到动态缓冲区
 * 5. 根据选项返回字符串、ArrayBuffer 或完整响应对象
 */
static JSValue js_std_urlGet(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *url;
    DynBuf cmd_buf;
    DynBuf data_buf_s, *data_buf = &data_buf_s;
    DynBuf header_buf_s, *header_buf = &header_buf_s;
    char *buf;
    size_t i, len;
    int status;
    JSValue response = JS_UNDEFINED, ret_obj;
    JSValueConst options_obj;
    FILE *f;
    BOOL binary_flag, full_flag;

    url = JS_ToCString(ctx, argv[0]);
    if (!url)
        return JS_EXCEPTION;

    binary_flag = FALSE;
    full_flag = FALSE;

    if (argc >= 2) {
        options_obj = argv[1];

        if (get_bool_option(ctx, &binary_flag, options_obj, "binary"))
            goto fail_obj;

        if (get_bool_option(ctx, &full_flag, options_obj, "full")) {
        fail_obj:
            JS_FreeCString(ctx, url);
            return JS_EXCEPTION;
        }
    }

    /* 构造 curl 命令，对 URL 进行 shell 转义 */
    js_std_dbuf_init(ctx, &cmd_buf);
    dbuf_printf(&cmd_buf, "%s '", URL_GET_PROGRAM);
    for(i = 0; url[i] != '\0'; i++) {
        unsigned char c = url[i];
        switch (c) {
        case '\'':
            /* shell 单引号字符串不支持 \' 转义，使用'\\'' 技巧 */
            dbuf_putstr(&cmd_buf, "'\\''");
            break;
        case '[': case ']': case '{': case '}': case '\\':
            /* 防止 curl 将字符解释为范围或集合指定 */
            dbuf_putc(&cmd_buf, '\\');
            /* FALLTHROUGH */
        default:
            dbuf_putc(&cmd_buf, c);
            break;
        }
    }
    JS_FreeCString(ctx, url);
    dbuf_putstr(&cmd_buf, "'");
    dbuf_putc(&cmd_buf, '\0');
    if (dbuf_error(&cmd_buf)) {
        dbuf_free(&cmd_buf);
        return JS_EXCEPTION;
    }
    //    printf("%s\n", (char *)cmd_buf.buf);
    f = popen((char *)cmd_buf.buf, "r");
    dbuf_free(&cmd_buf);
    if (!f) {
        return JS_ThrowTypeError(ctx, "could not start curl");
    }

    js_std_dbuf_init(ctx, data_buf);
    js_std_dbuf_init(ctx, header_buf);

    buf = js_malloc(ctx, URL_GET_BUF_SIZE);
    if (!buf)
        goto fail;

    /* 获取 HTTP 状态码 */
    if (http_get_header_line(f, buf, URL_GET_BUF_SIZE, NULL) < 0) {
        status = 0;
        goto bad_header;
    }
    status = http_get_status(buf);
    if (!full_flag && !(status >= 200 && status <= 299)) {
        goto bad_header;
    }

    /* 等待空行 (响应头结束) */
    for(;;) {
        if (http_get_header_line(f, buf, URL_GET_BUF_SIZE, header_buf) < 0) {
        bad_header:
            response = JS_NULL;
            goto done;
        }
        if (!strcmp(buf, "\r\n"))
            break;
    }
    if (dbuf_error(header_buf))
        goto fail;
    header_buf->size -= 2; /* 移除末尾的 CRLF */

    /* 下载响应体数据 */
    for(;;) {
        len = fread(buf, 1, URL_GET_BUF_SIZE, f);
        if (len == 0)
            break;
        dbuf_put(data_buf, (uint8_t *)buf, len);
    }
    if (dbuf_error(data_buf))
        goto fail;
    if (binary_flag) {
        response = JS_NewArrayBufferCopy(ctx,
                                         data_buf->buf, data_buf->size);
    } else {
        response = JS_NewStringLen(ctx, (char *)data_buf->buf, data_buf->size);
    }
    if (JS_IsException(response))
        goto fail;
 done:
    js_free(ctx, buf);
    buf = NULL;
    pclose(f);
    f = NULL;
    dbuf_free(data_buf);
    data_buf = NULL;

    if (full_flag) {
        /* 返回完整响应对象 */
        ret_obj = JS_NewObject(ctx);
        if (JS_IsException(ret_obj))
            goto fail;
        JS_DefinePropertyValueStr(ctx, ret_obj, "response",
                                  response,
                                  JS_PROP_C_W_E);
        if (!JS_IsNull(response)) {
            JS_DefinePropertyValueStr(ctx, ret_obj, "responseHeaders",
                                      JS_NewStringLen(ctx, (char *)header_buf->buf,
                                                      header_buf->size),
                                      JS_PROP_C_W_E);
            JS_DefinePropertyValueStr(ctx, ret_obj, "status",
                                      JS_NewInt32(ctx, status),
                                      JS_PROP_C_W_E);
        }
    } else {
        ret_obj = response;
    }
    dbuf_free(header_buf);
    return ret_obj;
 fail:
    if (f)
        pclose(f);
    js_free(ctx, buf);
    if (data_buf)
        dbuf_free(data_buf);
    if (header_buf)
        dbuf_free(header_buf);
    JS_FreeValue(ctx, response);
    return JS_EXCEPTION;
}

/**
 * @brief FILE 类定义
 * 
 * 定义 JS 文件对象的类结构，包含类名和析构函数。
 * 用于将 C 的 FILE* 封装为 JS 对象。
 */
static JSClassDef js_std_file_class = {
    "FILE",
    .finalizer = js_std_file_finalizer,
};

/**
 * @brief 标准库错误码常量
 * 
 * 将常见的 errno 值导出为 JS 常量，便于错误处理。
 */
static const JSCFunctionListEntry js_std_error_props[] = {
    /* various errno values */
#define DEF(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE )
    DEF(EINVAL),      /* 无效参数 */
    DEF(EIO),         /* I/O 错误 */
    DEF(EACCES),      /* 权限拒绝 */
    DEF(EEXIST),      /* 文件已存在 */
    DEF(ENOSPC),      /* 空间不足 */
    DEF(ENOSYS),      /* 功能未实现 */
    DEF(EBUSY),       /* 资源忙 */
    DEF(ENOENT),      /* 文件或目录不存在 */
    DEF(EPERM),       /* 操作不允许 */
    DEF(EPIPE),       /* 管道破裂 */
    DEF(EBADF),       /* 无效的文件描述符 */
#undef DEF
};

/**
 * @brief std 模块导出函数列表
 * 
 * 定义 std 模块导出的所有函数和常量：
 * - 进程控制：exit, gc
 * - 脚本执行：evalScript, loadScript
 * - 环境变量：getenv, setenv, unsetenv, getenviron
 * - 网络请求：urlGet
 * - 文件操作：loadFile, open, popen, fdopen, tmpfile
 * - 工具函数：strerror, parseExtJSON
 * - 文件常量：SEEK_SET, SEEK_CUR, SEEK_END
 * - 错误码：Error 对象包含各种 errno 值
 */
static const JSCFunctionListEntry js_std_funcs[] = {
    JS_CFUNC_DEF("exit", 1, js_std_exit ),
    JS_CFUNC_DEF("gc", 0, js_std_gc ),
    JS_CFUNC_DEF("evalScript", 1, js_evalScript ),
    JS_CFUNC_DEF("loadScript", 1, js_loadScript ),
    JS_CFUNC_DEF("getenv", 1, js_std_getenv ),
    JS_CFUNC_DEF("setenv", 1, js_std_setenv ),
    JS_CFUNC_DEF("unsetenv", 1, js_std_unsetenv ),
    JS_CFUNC_DEF("getenviron", 1, js_std_getenviron ),
    JS_CFUNC_DEF("urlGet", 1, js_std_urlGet ),
    JS_CFUNC_DEF("loadFile", 1, js_std_loadFile ),
    JS_CFUNC_DEF("strerror", 1, js_std_strerror ),
    JS_CFUNC_DEF("parseExtJSON", 1, js_std_parseExtJSON ),

    /* FILE I/O */
    JS_CFUNC_DEF("open", 2, js_std_open ),
    JS_CFUNC_DEF("popen", 2, js_std_popen ),
    JS_CFUNC_DEF("fdopen", 2, js_std_fdopen ),
    JS_CFUNC_DEF("tmpfile", 0, js_std_tmpfile ),
    JS_CFUNC_MAGIC_DEF("puts", 1, js_std_file_puts, 0 ),
    JS_CFUNC_DEF("printf", 1, js_std_printf ),
    JS_CFUNC_DEF("sprintf", 1, js_std_sprintf ),
    JS_PROP_INT32_DEF("SEEK_SET", SEEK_SET, JS_PROP_CONFIGURABLE ),
    JS_PROP_INT32_DEF("SEEK_CUR", SEEK_CUR, JS_PROP_CONFIGURABLE ),
    JS_PROP_INT32_DEF("SEEK_END", SEEK_END, JS_PROP_CONFIGURABLE ),
    JS_OBJECT_DEF("Error", js_std_error_props, countof(js_std_error_props), JS_PROP_CONFIGURABLE),
    JS_CFUNC_DEF("__printObject", 1, js_std_file_printObject ),
};

/**
 * @brief FILE 原型对象方法列表
 * 
 * 定义 FILE 对象原型上的所有方法：
 * - 基本操作：close, flush, fileno
 * - 读写操作：read, write, getByte, putByte
 * - 字符串操作：puts, printf, getline, readAsString
 * - 定位操作：tell, tello, seek
 * - 状态查询：eof, error, clearerr
 */
static const JSCFunctionListEntry js_std_file_proto_funcs[] = {
    JS_CFUNC_DEF("close", 0, js_std_file_close ),
    JS_CFUNC_MAGIC_DEF("puts", 1, js_std_file_puts, 1 ),
    JS_CFUNC_DEF("printf", 1, js_std_file_printf ),
    JS_CFUNC_DEF("flush", 0, js_std_file_flush ),
    JS_CFUNC_MAGIC_DEF("tell", 0, js_std_file_tell, 0 ),
    JS_CFUNC_MAGIC_DEF("tello", 0, js_std_file_tell, 1 ),
    JS_CFUNC_DEF("seek", 2, js_std_file_seek ),
    JS_CFUNC_DEF("eof", 0, js_std_file_eof ),
    JS_CFUNC_DEF("fileno", 0, js_std_file_fileno ),
    JS_CFUNC_DEF("error", 0, js_std_file_error ),
    JS_CFUNC_DEF("clearerr", 0, js_std_file_clearerr ),
    JS_CFUNC_MAGIC_DEF("read", 3, js_std_file_read_write, 0 ),
    JS_CFUNC_MAGIC_DEF("write", 3, js_std_file_read_write, 1 ),
    JS_CFUNC_DEF("getline", 0, js_std_file_getline ),
    JS_CFUNC_DEF("readAsString", 0, js_std_file_readAsString ),
    JS_CFUNC_DEF("getByte", 0, js_std_file_getByte ),
    JS_CFUNC_DEF("putByte", 1, js_std_file_putByte ),
    /* setvbuf, ...  */
};

/**
 * @brief std 模块初始化函数
 * @param ctx JS 上下文
 * @param m 模块对象
 * @return 成功返回 0，失败返回 -1
 * 
 * 初始化 std 模块，包括：
 * 1. 创建 FILE 类 ID 和类结构
 * 2. 创建 FILE 原型对象并绑定方法
 * 3. 导出所有 std 函数和常量
 * 4. 创建标准文件流对象 (in/out/err)
 */
static int js_std_init(JSContext *ctx, JSModuleDef *m)
{
    JSValue proto;

    /* FILE 类 */
    /* 类 ID 只创建一次 */
    JS_NewClassID(&js_std_file_class_id);
    /* 类结构每个 runtime 创建一次 */
    JS_NewClass(JS_GetRuntime(ctx), js_std_file_class_id, &js_std_file_class);
    proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, js_std_file_proto_funcs,
                               countof(js_std_file_proto_funcs));
    JS_SetClassProto(ctx, js_std_file_class_id, proto);

    JS_SetModuleExportList(ctx, m, js_std_funcs,
                           countof(js_std_funcs));
    JS_SetModuleExport(ctx, m, "in", js_new_std_file(ctx, stdin, FALSE, FALSE));
    JS_SetModuleExport(ctx, m, "out", js_new_std_file(ctx, stdout, FALSE, FALSE));
    JS_SetModuleExport(ctx, m, "err", js_new_std_file(ctx, stderr, FALSE, FALSE));
    return 0;
}

/**
 * @brief 初始化 std 模块入口
 * @param ctx JS 上下文
 * @param module_name 模块名称
 * @return 模块对象指针
 * 
 * 创建 C 模块并注册导出项，供 JS 代码 import 使用。
 */
JSModuleDef *js_init_module_std(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_std_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_std_funcs, countof(js_std_funcs));
    JS_AddModuleExport(ctx, m, "in");
    JS_AddModuleExport(ctx, m, "out");
    JS_AddModuleExport(ctx, m, "err");
    return m;
}

/**********************************************************/
/* 'os' object */

/**
 * @brief 打开文件 (os.open)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件名，[1]=标志，[2]=可选权限 (默认 0666)
 * @return 文件描述符 (整数)，错误返回负数错误码
 * 
 * 打开文件并返回文件描述符。支持 O_RDONLY, O_WRONLY, O_RDWR 等标志。
 * Windows 上默认强制使用二进制模式。
 */
static JSValue js_os_open(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    const char *filename;
    int flags, mode, ret;

    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &flags, argv[1]))
        goto fail;
    if (argc >= 3 && !JS_IsUndefined(argv[2])) {
        if (JS_ToInt32(ctx, &mode, argv[2])) {
        fail:
            JS_FreeCString(ctx, filename);
            return JS_EXCEPTION;
        }
    } else {
        mode = 0666;
    }
#if defined(_WIN32)
    /* 强制使用二进制模式 */
    if (!(flags & O_TEXT))
        flags |= O_BINARY;
#endif
    ret = js_get_errno(open(filename, flags, mode));
    JS_FreeCString(ctx, filename);
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 关闭文件描述符 (os.close)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件描述符
 * @return 成功返回 0，失败返回负数错误码
 */
static JSValue js_os_close(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    int fd, ret;
    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    ret = js_get_errno(close(fd));
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 移动文件读写位置 (os.seek)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件描述符，[1]=偏移量，[2]=起始位置
 * @return 新的位置，错误返回负数错误码
 * 
 * 使用 lseek 系统调用移动文件描述符的读写位置。
 * 支持 BigInt 类型的偏移量和返回值。
 */
static JSValue js_os_seek(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    int fd, whence;
    int64_t pos, ret;
    BOOL is_bigint;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    is_bigint = JS_IsBigInt(ctx, argv[1]);
    if (JS_ToInt64Ext(ctx, &pos, argv[1]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &whence, argv[2]))
        return JS_EXCEPTION;
    ret = lseek(fd, pos, whence);
    if (ret == -1)
        ret = -errno;
    if (is_bigint)
        return JS_NewBigInt64(ctx, ret);
    else
        return JS_NewInt64(ctx, ret);
}

/**
 * @brief 文件描述符读写 (os.read/os.write)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=fd, [1]=ArrayBuffer, [2]=起始位置，[3]=长度
 * @param magic 0=读，1=写
 * @return 实际读写的字节数，错误返回负数错误码
 * 
 * 使用底层 read/write 系统调用对文件描述符进行 I/O 操作。
 * 数据在 ArrayBuffer 和文件描述符之间传输。
 */
static JSValue js_os_read_write(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv, int magic)
{
    int fd;
    uint64_t pos, len;
    size_t size;
    ssize_t ret;
    uint8_t *buf;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &pos, argv[2]))
        return JS_EXCEPTION;
    if (JS_ToIndex(ctx, &len, argv[3]))
        return JS_EXCEPTION;
    buf = JS_GetArrayBuffer(ctx, &size, argv[1]);
    if (!buf)
        return JS_EXCEPTION;
    if (pos + len > size)
        return JS_ThrowRangeError(ctx, "read/write array buffer overflow");
    if (magic)
        ret = js_get_errno(write(fd, buf + pos, len));
    else
        ret = js_get_errno(read(fd, buf + pos, len));
    return JS_NewInt64(ctx, ret);
}

/**
 * @brief 检查文件描述符是否为终端 (os.isatty)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件描述符
 * @return 是终端返回 true，否则返回 false
 */
static JSValue js_os_isatty(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    int fd;
    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    return JS_NewBool(ctx, isatty(fd));
}

#if defined(_WIN32)
static JSValue js_os_ttyGetWinSize(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    int fd;
    HANDLE handle;
    CONSOLE_SCREEN_BUFFER_INFO info;
    JSValue obj;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    handle = (HANDLE)_get_osfhandle(fd);

    if (!GetConsoleScreenBufferInfo(handle, &info))
        return JS_NULL;
    obj = JS_NewArray(ctx);
    if (JS_IsException(obj))
        return obj;
    JS_DefinePropertyValueUint32(ctx, obj, 0, JS_NewInt32(ctx, info.dwSize.X), JS_PROP_C_W_E);
    JS_DefinePropertyValueUint32(ctx, obj, 1, JS_NewInt32(ctx, info.dwSize.Y), JS_PROP_C_W_E);
    return obj;
}

/* Windows 10 built-in VT100 emulation */
#define __ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#define __ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200

static JSValue js_os_ttySetRaw(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    int fd;
    HANDLE handle;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    handle = (HANDLE)_get_osfhandle(fd);
    SetConsoleMode(handle, ENABLE_WINDOW_INPUT | __ENABLE_VIRTUAL_TERMINAL_INPUT);
    _setmode(fd, _O_BINARY);
    if (fd == 0) {
        handle = (HANDLE)_get_osfhandle(1); /* corresponding output */
        SetConsoleMode(handle, ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT | __ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    return JS_UNDEFINED;
}
#else
static JSValue js_os_ttyGetWinSize(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    int fd;
    struct winsize ws;
    JSValue obj;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col >= 4 && ws.ws_row >= 4) {
        obj = JS_NewArray(ctx);
        if (JS_IsException(obj))
            return obj;
        JS_DefinePropertyValueUint32(ctx, obj, 0, JS_NewInt32(ctx, ws.ws_col), JS_PROP_C_W_E);
        JS_DefinePropertyValueUint32(ctx, obj, 1, JS_NewInt32(ctx, ws.ws_row), JS_PROP_C_W_E);
        return obj;
    } else {
        return JS_NULL;
    }
}

static struct termios oldtty;

static void term_exit(void)
{
    tcsetattr(0, TCSANOW, &oldtty);
}

/* XXX: should add a way to go back to normal mode */
static JSValue js_os_ttySetRaw(JSContext *ctx, JSValueConst this_val,
                               int argc, JSValueConst *argv)
{
    struct termios tty;
    int fd;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;

    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);
    oldtty = tty;

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);

    atexit(term_exit);
    return JS_UNDEFINED;
}

#endif /* !_WIN32 */

/**
 * @brief 删除文件或目录 (os.remove)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件路径
 * @return 成功返回 0，失败返回负数错误码
 * 
 * Windows 上根据文件类型自动选择 rmdir 或 unlink，
 * POSIX 系统直接使用 remove 系统调用。
 */
static JSValue js_os_remove(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    const char *filename;
    int ret;

    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        return JS_EXCEPTION;
#if defined(_WIN32)
    {
        struct stat st;
        if (stat(filename, &st) == 0 && S_ISDIR(st.st_mode)) {
            ret = rmdir(filename);
        } else {
            ret = unlink(filename);
        }
    }
#else
    ret = remove(filename);
#endif
    ret = js_get_errno(ret);
    JS_FreeCString(ctx, filename);
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 重命名文件或目录 (os.rename)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=旧路径，[1]=新路径
 * @return 成功返回 0，失败返回负数错误码
 */
static JSValue js_os_rename(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    const char *oldpath, *newpath;
    int ret;

    oldpath = JS_ToCString(ctx, argv[0]);
    if (!oldpath)
        return JS_EXCEPTION;
    newpath = JS_ToCString(ctx, argv[1]);
    if (!newpath) {
        JS_FreeCString(ctx, oldpath);
        return JS_EXCEPTION;
    }
    ret = js_get_errno(rename(oldpath, newpath));
    JS_FreeCString(ctx, oldpath);
    JS_FreeCString(ctx, newpath);
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 检查当前是否为主线程
 * @param rt JS 运行时
 * @return 主线程返回 TRUE，否则返回 FALSE
 * 
 * 通过检查 recv_pipe 是否存在来判断：
 * - 主线程：recv_pipe 为 NULL
 * - Worker 线程：recv_pipe 指向消息接收管道
 */
static BOOL is_main_thread(JSRuntime *rt)
{
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    return !ts->recv_pipe;
}

/**
 * @brief 查找文件描述符对应的读写处理器
 * @param ts 线程状态
 * @param fd 文件描述符
 * @return 找到返回处理器指针，否则返回 NULL
 */
static JSOSRWHandler *find_rh(JSThreadState *ts, int fd)
{
    JSOSRWHandler *rh;
    struct list_head *el;

    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, JSOSRWHandler, link);
        if (rh->fd == fd)
            return rh;
    }
    return NULL;
}

/**
 * @brief 释放读写处理器资源
 * @param rt JS 运行时
 * @param rh 读写处理器指针
 * 
 * 从链表中移除处理器，释放读/写回调函数的引用，
 * 最后释放处理器本身占用的内存。
 */
static void free_rw_handler(JSRuntime *rt, JSOSRWHandler *rh)
{
    int i;
    list_del(&rh->link);
    for(i = 0; i < 2; i++) {
        JS_FreeValueRT(rt, rh->rw_func[i]);
    }
    js_free_rt(rt, rh);
}

/**
 * @brief 设置文件描述符的读/写回调处理器 (os.setReadHandler/os.setWriteHandler)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件描述符，[1]=回调函数或 null
 * @param magic 0=读处理器，1=写处理器
 * @return 成功返回 undefined
 * 
 * 为指定的文件描述符注册读写事件回调函数。
 * 当 fd 可读/可写时，事件循环会自动调用对应的回调函数。
 * 传入 null 则移除对应的处理器，当读写处理器都为 null 时彻底删除该处理器。
 */
static JSValue js_os_setReadHandler(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    JSOSRWHandler *rh;
    int fd;
    JSValueConst func;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    func = argv[1];
    if (JS_IsNull(func)) {
        rh = find_rh(ts, fd);
        if (rh) {
            JS_FreeValue(ctx, rh->rw_func[magic]);
            rh->rw_func[magic] = JS_NULL;
            if (JS_IsNull(rh->rw_func[0]) &&
                JS_IsNull(rh->rw_func[1])) {
                /* remove the entry */
                free_rw_handler(JS_GetRuntime(ctx), rh);
            }
        }
    } else {
        if (!JS_IsFunction(ctx, func))
            return JS_ThrowTypeError(ctx, "not a function");
        rh = find_rh(ts, fd);
        if (!rh) {
            rh = js_mallocz(ctx, sizeof(*rh));
            if (!rh)
                return JS_EXCEPTION;
            rh->fd = fd;
            rh->rw_func[0] = JS_NULL;
            rh->rw_func[1] = JS_NULL;
            list_add_tail(&rh->link, &ts->os_rw_handlers);
        }
        JS_FreeValue(ctx, rh->rw_func[magic]);
        rh->rw_func[magic] = JS_DupValue(ctx, func);
    }
    return JS_UNDEFINED;
}

/**
 * @brief 查找信号处理器
 * @param ts 线程状态
 * @param sig_num 信号编号
 * @return 找到返回处理器指针，否则返回 NULL
 */
static JSOSSignalHandler *find_sh(JSThreadState *ts, int sig_num)
{
    JSOSSignalHandler *sh;
    struct list_head *el;
    list_for_each(el, &ts->os_signal_handlers) {
        sh = list_entry(el, JSOSSignalHandler, link);
        if (sh->sig_num == sig_num)
            return sh;
    }
    return NULL;
}

/**
 * @brief 释放信号处理器资源
 * @param rt JS 运行时
 * @param sh 信号处理器指针
 * 
 * 从链表中移除处理器，释放回调函数引用，释放处理器内存。
 */
static void free_sh(JSRuntime *rt, JSOSSignalHandler *sh)
{
    list_del(&sh->link);
    JS_FreeValueRT(rt, sh->func);
    js_free_rt(rt, sh);
}

/**
 * @brief 信号处理回调函数
 * @param sig_num 信号编号
 * 
 * 将收到的信号标记到 os_pending_signals 位图中，
 * 真正的事件处理会在 js_os_poll 中执行。
 * 使用位图可以支持最多 64 种信号。
 */
static void os_signal_handler(int sig_num)
{
    os_pending_signals |= ((uint64_t)1 << sig_num);
}

#if defined(_WIN32)
typedef void (*sighandler_t)(int sig_num);
#endif

/**
 * @brief 注册信号处理器 (os.signal)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=信号编号，[1]=回调函数/null/undefined
 * @return 成功返回 undefined
 * 
 * 为指定的信号注册处理函数：
 * - func = null: 恢复默认处理 (SIG_DFL)
 * - func = undefined: 忽略信号 (SIG_IGN)
 * - func = 函数：注册自定义处理函数
 * 
 * 只能在主线程中调用，Worker 线程不支持信号处理。
 * 信号编号范围为 0-63，使用 64 位位图存储待处理信号。
 */
static JSValue js_os_signal(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    JSOSSignalHandler *sh;
    uint32_t sig_num;
    JSValueConst func;
    sighandler_t handler;

    if (!is_main_thread(rt))
        return JS_ThrowTypeError(ctx, "signal handler can only be set in the main thread");

    if (JS_ToUint32(ctx, &sig_num, argv[0]))
        return JS_EXCEPTION;
    if (sig_num >= 64)
        return JS_ThrowRangeError(ctx, "invalid signal number");
    func = argv[1];
    /* func = null: SIG_DFL, func = undefined, SIG_IGN */
    if (JS_IsNull(func) || JS_IsUndefined(func)) {
        sh = find_sh(ts, sig_num);
        if (sh) {
            free_sh(JS_GetRuntime(ctx), sh);
        }
        if (JS_IsNull(func))
            handler = SIG_DFL;
        else
            handler = SIG_IGN;
        signal(sig_num, handler);
    } else {
        if (!JS_IsFunction(ctx, func))
            return JS_ThrowTypeError(ctx, "not a function");
        sh = find_sh(ts, sig_num);
        if (!sh) {
            sh = js_mallocz(ctx, sizeof(*sh));
            if (!sh)
                return JS_EXCEPTION;
            sh->sig_num = sig_num;
            list_add_tail(&sh->link, &ts->os_signal_handlers);
        }
        JS_FreeValue(ctx, sh->func);
        sh->func = JS_DupValue(ctx, func);
        signal(sig_num, os_signal_handler);
    }
    return JS_UNDEFINED;
}

/**
 * @brief 获取单调递增的时间戳（毫秒/纳秒）
 * @return 从某个固定起点开始的毫秒/纳秒数
 * 
 * Linux/macOS 使用 clock_gettime(CLOCK_MONOTONIC)，
 * 其他系统使用 gettimeofday（可能受系统时间调整影响）。
 * CLOCK_MONOTONIC 不受系统时间修改影响，适合计时。
 */
#if defined(__linux__) || defined(__APPLE__)
static int64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000);
}

static int64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
#else
/* more portable, but does not work if the date is updated */
static int64_t get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + (tv.tv_usec / 1000);
}

static int64_t get_time_ns(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000000 + (tv.tv_usec * 1000);
}
#endif

/**
 * @brief 获取当前时间戳（毫秒）(os.now)
 * @param ctx JS 上下文
 * @return 从固定起点开始的毫秒数（浮点数）
 * 
 * 基于单调时钟返回当前时间戳，用于性能测量和定时。
 */
static JSValue js_os_now(JSContext *ctx, JSValue this_val,
                         int argc, JSValue *argv)
{
    return JS_NewFloat64(ctx, (double)get_time_ns() / 1e6);
}

/**
 * @brief 释放定时器资源
 * @param rt JS 运行时
 * @param th 定时器指针
 * 
 * 从定时器链表中移除，释放回调函数引用，释放定时器内存。
 */
static void free_timer(JSRuntime *rt, JSOSTimer *th)
{
    list_del(&th->link);
    JS_FreeValueRT(rt, th->func);
    js_free_rt(rt, th);
}

/**
 * @brief 设置一次性定时器 (os.setTimeout)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=回调函数，[1]=延迟时间（毫秒）
 * @return 定时器 ID，用于取消定时器
 * 
 * 创建一个一次性定时器，在指定的延迟后调用回调函数。
 * 定时器 ID 从 1 开始递增，达到 INT32_MAX 后回绕到 1。
 * 定时器到期时会在 js_os_poll 中被触发执行。
 */
static JSValue js_os_setTimeout(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    int64_t delay;
    JSValueConst func;
    JSOSTimer *th;

    func = argv[0];
    if (!JS_IsFunction(ctx, func))
        return JS_ThrowTypeError(ctx, "not a function");
    if (JS_ToInt64(ctx, &delay, argv[1]))
        return JS_EXCEPTION;
    th = js_mallocz(ctx, sizeof(*th));
    if (!th)
        return JS_EXCEPTION;
    th->timer_id = ts->next_timer_id;
    if (ts->next_timer_id == INT32_MAX)
        ts->next_timer_id = 1;
    else
        ts->next_timer_id++;
    th->timeout = get_time_ms() + delay;
    th->func = JS_DupValue(ctx, func);
    list_add_tail(&th->link, &ts->os_timers);
    return JS_NewInt32(ctx, th->timer_id);
}

/**
 * @brief 根据 ID 查找定时器
 * @param ts 线程状态
 * @param timer_id 定时器 ID
 * @return 找到返回定时器指针，否则返回 NULL
 */
static JSOSTimer *find_timer_by_id(JSThreadState *ts, int timer_id)
{
    struct list_head *el;
    if (timer_id <= 0)
        return NULL;
    list_for_each(el, &ts->os_timers) {
        JSOSTimer *th = list_entry(el, JSOSTimer, link);
        if (th->timer_id == timer_id)
            return th;
    }
    return NULL;
}

/**
 * @brief 取消定时器 (os.clearTimeout)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=定时器 ID
 * @return 成功返回 undefined，定时器不存在也返回 undefined
 * 
 * 根据定时器 ID 查找并删除定时器，释放相关资源。
 * 如果定时器 ID 不存在或无效，静默返回 undefined。
 */
static JSValue js_os_clearTimeout(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    JSOSTimer *th;
    int timer_id;

    if (JS_ToInt32(ctx, &timer_id, argv[0]))
        return JS_EXCEPTION;
    th = find_timer_by_id(ts, timer_id);
    if (!th)
        return JS_UNDEFINED;
    free_timer(rt, th);
    return JS_UNDEFINED;
}

/**
 * @brief 异步睡眠（返回 Promise）(os.sleepAsync)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=延迟时间（毫秒）
 * @return Promise 对象，到期后 resolve
 * 
 * 创建一个基于 Promise 的异步睡眠，适合 async/await 使用。
 * 定时器到期时会调用 Promise 的 resolve 函数。
 * 定时器 ID 设为 -1 表示这是内部定时器，不暴露给用户。
 */
/* return a promise */
static JSValue js_os_sleepAsync(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    int64_t delay;
    JSOSTimer *th;
    JSValue promise, resolving_funcs[2];

    if (JS_ToInt64(ctx, &delay, argv[0]))
        return JS_EXCEPTION;
    promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise))
        return JS_EXCEPTION;

    th = js_mallocz(ctx, sizeof(*th));
    if (!th) {
        JS_FreeValue(ctx, promise);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return JS_EXCEPTION;
    }
    th->timer_id = -1;
    th->timeout = get_time_ms() + delay;
    th->func = JS_DupValue(ctx, resolving_funcs[0]);
    list_add_tail(&th->link, &ts->os_timers);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}

/**
 * @brief 调用事件处理函数
 * @param ctx JS 上下文
 * @param func 要调用的函数
 * 
 * 安全地调用事件处理回调函数。
 * 先增加引用计数再调用，防止回调函数释放自身导致悬空指针。
 * 如果调用抛出异常，输出错误信息但不中断程序。
 */
static void call_handler(JSContext *ctx, JSValueConst func)
{
    JSValue ret, func1;
    /* 'func' might be destroyed when calling itself (if it frees the
       handler), so must take extra care */
    func1 = JS_DupValue(ctx, func);
    ret = JS_Call(ctx, func1, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, func1);
    if (JS_IsException(ret))
        js_std_dump_error(ctx);
    JS_FreeValue(ctx, ret);
}

#ifdef USE_WORKER

#ifdef _WIN32

static int js_waker_init(JSWaker *w)
{
    w->handle = CreateEvent(NULL, TRUE, FALSE, NULL);
    return w->handle ? 0 : -1;
}

static void js_waker_signal(JSWaker *w)
{
    SetEvent(w->handle);
}

static void js_waker_clear(JSWaker *w)
{
    ResetEvent(w->handle);
}

static void js_waker_close(JSWaker *w)
{
    CloseHandle(w->handle);
    w->handle = INVALID_HANDLE_VALUE;
}

#else // !_WIN32

static int js_waker_init(JSWaker *w)
{
    int fds[2];

    if (pipe(fds) < 0)
        return -1;
    w->read_fd = fds[0];
    w->write_fd = fds[1];
    return 0;
}

static void js_waker_signal(JSWaker *w)
{
    int ret;

    for(;;) {
        ret = write(w->write_fd, "", 1);
        if (ret == 1)
            break;
        if (ret < 0 && (errno != EAGAIN || errno != EINTR))
            break;
    }
}

static void js_waker_clear(JSWaker *w)
{
    uint8_t buf[16];
    int ret;

    for(;;) {
        ret = read(w->read_fd, buf, sizeof(buf));
        if (ret >= 0)
            break;
        if (errno != EAGAIN && errno != EINTR)
            break;
    }
}

static void js_waker_close(JSWaker *w)
{
    close(w->read_fd);
    close(w->write_fd);
    w->read_fd = -1;
    w->write_fd = -1;
}

#endif // _WIN32

static void js_free_message(JSWorkerMessage *msg);

/* return 1 if a message was handled, 0 if no message */
static int handle_posted_message(JSRuntime *rt, JSContext *ctx,
                                 JSWorkerMessageHandler *port)
{
    JSWorkerMessagePipe *ps = port->recv_pipe;
    int ret;
    struct list_head *el;
    JSWorkerMessage *msg;
    JSValue obj, data_obj, func, retval;

    pthread_mutex_lock(&ps->mutex);
    if (!list_empty(&ps->msg_queue)) {
        el = ps->msg_queue.next;
        msg = list_entry(el, JSWorkerMessage, link);

        /* remove the message from the queue */
        list_del(&msg->link);

        if (list_empty(&ps->msg_queue))
            js_waker_clear(&ps->waker);

        pthread_mutex_unlock(&ps->mutex);

        data_obj = JS_ReadObject(ctx, msg->data, msg->data_len,
                                 JS_READ_OBJ_SAB | JS_READ_OBJ_REFERENCE);

        js_free_message(msg);

        if (JS_IsException(data_obj))
            goto fail;
        obj = JS_NewObject(ctx);
        if (JS_IsException(obj)) {
            JS_FreeValue(ctx, data_obj);
            goto fail;
        }
        JS_DefinePropertyValueStr(ctx, obj, "data", data_obj, JS_PROP_C_W_E);

        /* 'func' might be destroyed when calling itself (if it frees the
           handler), so must take extra care */
        func = JS_DupValue(ctx, port->on_message_func);
        retval = JS_Call(ctx, func, JS_UNDEFINED, 1, (JSValueConst *)&obj);
        JS_FreeValue(ctx, obj);
        JS_FreeValue(ctx, func);
        if (JS_IsException(retval)) {
        fail:
            js_std_dump_error(ctx);
        } else {
            JS_FreeValue(ctx, retval);
        }
        ret = 1;
    } else {
        pthread_mutex_unlock(&ps->mutex);
        ret = 0;
    }
    return ret;
}
#else
static int handle_posted_message(JSRuntime *rt, JSContext *ctx,
                                 JSWorkerMessageHandler *port)
{
    return 0;
}
#endif /* !USE_WORKER */

#if defined(_WIN32)

static int js_os_poll(JSContext *ctx)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    int min_delay, count;
    int64_t cur_time, delay;
    JSOSRWHandler *rh;
    struct list_head *el;
    HANDLE handles[MAXIMUM_WAIT_OBJECTS]; // 64

    /* XXX: handle signals if useful */

    if (list_empty(&ts->os_rw_handlers) && list_empty(&ts->os_timers) &&
        list_empty(&ts->port_list)) {
        return -1; /* no more events */
    }
    
    if (!list_empty(&ts->os_timers)) {
        cur_time = get_time_ms();
        min_delay = 10000;
        list_for_each(el, &ts->os_timers) {
            JSOSTimer *th = list_entry(el, JSOSTimer, link);
            delay = th->timeout - cur_time;
            if (delay <= 0) {
                JSValue func;
                /* the timer expired */
                func = th->func;
                th->func = JS_UNDEFINED;
                free_timer(rt, th);
                call_handler(ctx, func);
                JS_FreeValue(ctx, func);
                return 0;
            } else if (delay < min_delay) {
                min_delay = delay;
            }
        }
    } else {
        min_delay = -1;
    }

    count = 0;
    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, JSOSRWHandler, link);
        if (rh->fd == 0 && !JS_IsNull(rh->rw_func[0])) {
            handles[count++] = (HANDLE)_get_osfhandle(rh->fd); // stdin
            if (count == (int)countof(handles))
                break;
        }
    }

    list_for_each(el, &ts->port_list) {
        JSWorkerMessageHandler *port = list_entry(el, JSWorkerMessageHandler, link);
        if (JS_IsNull(port->on_message_func))
            continue;
        handles[count++] = port->recv_pipe->waker.handle;
        if (count == (int)countof(handles))
            break;
    }

    if (count > 0) {
        DWORD ret, timeout = INFINITE;
        if (min_delay != -1)
            timeout = min_delay;
        ret = WaitForMultipleObjects(count, handles, FALSE, timeout);

        if (ret < count) {
            list_for_each(el, &ts->os_rw_handlers) {
                rh = list_entry(el, JSOSRWHandler, link);
                if (rh->fd == 0 && !JS_IsNull(rh->rw_func[0])) {
                    call_handler(ctx, rh->rw_func[0]);
                    /* must stop because the list may have been modified */
                    goto done;
                }
            }

            list_for_each(el, &ts->port_list) {
                JSWorkerMessageHandler *port = list_entry(el, JSWorkerMessageHandler, link);
                if (!JS_IsNull(port->on_message_func)) {
                    JSWorkerMessagePipe *ps = port->recv_pipe;
                    if (ps->waker.handle == handles[ret]) {
                        if (handle_posted_message(rt, ctx, port))
                            goto done;
                    }
                }
            }
        }
    } else {
        Sleep(min_delay);
    }
 done:
    return 0;
}

#else

static int js_os_poll(JSContext *ctx)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    int ret, fd_max, min_delay;
    int64_t cur_time, delay;
    fd_set rfds, wfds;
    JSOSRWHandler *rh;
    struct list_head *el;
    struct timeval tv, *tvp;

    /* only check signals in the main thread */
    if (!ts->recv_pipe &&
        unlikely(os_pending_signals != 0)) {
        JSOSSignalHandler *sh;
        uint64_t mask;

        list_for_each(el, &ts->os_signal_handlers) {
            sh = list_entry(el, JSOSSignalHandler, link);
            mask = (uint64_t)1 << sh->sig_num;
            if (os_pending_signals & mask) {
                os_pending_signals &= ~mask;
                call_handler(ctx, sh->func);
                return 0;
            }
        }
    }

    if (list_empty(&ts->os_rw_handlers) && list_empty(&ts->os_timers) &&
        list_empty(&ts->port_list))
        return -1; /* no more events */

    if (!list_empty(&ts->os_timers)) {
        cur_time = get_time_ms();
        min_delay = 10000;
        list_for_each(el, &ts->os_timers) {
            JSOSTimer *th = list_entry(el, JSOSTimer, link);
            delay = th->timeout - cur_time;
            if (delay <= 0) {
                JSValue func;
                /* the timer expired */
                func = th->func;
                th->func = JS_UNDEFINED;
                free_timer(rt, th);
                call_handler(ctx, func);
                JS_FreeValue(ctx, func);
                return 0;
            } else if (delay < min_delay) {
                min_delay = delay;
            }
        }
        tv.tv_sec = min_delay / 1000;
        tv.tv_usec = (min_delay % 1000) * 1000;
        tvp = &tv;
    } else {
        tvp = NULL;
    }

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    fd_max = -1;
    list_for_each(el, &ts->os_rw_handlers) {
        rh = list_entry(el, JSOSRWHandler, link);
        fd_max = max_int(fd_max, rh->fd);
        if (!JS_IsNull(rh->rw_func[0]))
            FD_SET(rh->fd, &rfds);
        if (!JS_IsNull(rh->rw_func[1]))
            FD_SET(rh->fd, &wfds);
    }

    list_for_each(el, &ts->port_list) {
        JSWorkerMessageHandler *port = list_entry(el, JSWorkerMessageHandler, link);
        if (!JS_IsNull(port->on_message_func)) {
            JSWorkerMessagePipe *ps = port->recv_pipe;
            fd_max = max_int(fd_max, ps->waker.read_fd);
            FD_SET(ps->waker.read_fd, &rfds);
        }
    }

    ret = select(fd_max + 1, &rfds, &wfds, NULL, tvp);
    if (ret > 0) {
        list_for_each(el, &ts->os_rw_handlers) {
            rh = list_entry(el, JSOSRWHandler, link);
            if (!JS_IsNull(rh->rw_func[0]) &&
                FD_ISSET(rh->fd, &rfds)) {
                call_handler(ctx, rh->rw_func[0]);
                /* must stop because the list may have been modified */
                goto done;
            }
            if (!JS_IsNull(rh->rw_func[1]) &&
                FD_ISSET(rh->fd, &wfds)) {
                call_handler(ctx, rh->rw_func[1]);
                /* must stop because the list may have been modified */
                goto done;
            }
        }

        list_for_each(el, &ts->port_list) {
            JSWorkerMessageHandler *port = list_entry(el, JSWorkerMessageHandler, link);
            if (!JS_IsNull(port->on_message_func)) {
                JSWorkerMessagePipe *ps = port->recv_pipe;
                if (FD_ISSET(ps->waker.read_fd, &rfds)) {
                    if (handle_posted_message(rt, ctx, port))
                        goto done;
                }
            }
        }
    }
 done:
    return 0;
}
#endif /* !_WIN32 */

static JSValue make_obj_error(JSContext *ctx,
                              JSValue obj,
                              int err)
{
    JSValue arr;
    if (JS_IsException(obj))
        return obj;
    arr = JS_NewArray(ctx);
    if (JS_IsException(arr))
        return JS_EXCEPTION;
    JS_DefinePropertyValueUint32(ctx, arr, 0, obj,
                                 JS_PROP_C_W_E);
    JS_DefinePropertyValueUint32(ctx, arr, 1, JS_NewInt32(ctx, err),
                                 JS_PROP_C_W_E);
    return arr;
}

static JSValue make_string_error(JSContext *ctx,
                                 const char *buf,
                                 int err)
{
    return make_obj_error(ctx, JS_NewString(ctx, buf), err);
}

/* return [cwd, errorcode] */
static JSValue js_os_getcwd(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    char buf[PATH_MAX];
    int err;

    if (!getcwd(buf, sizeof(buf))) {
        buf[0] = '\0';
        err = errno;
    } else {
        err = 0;
    }
    return make_string_error(ctx, buf, err);
}

static JSValue js_os_chdir(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    const char *target;
    int err;

    target = JS_ToCString(ctx, argv[0]);
    if (!target)
        return JS_EXCEPTION;
    err = js_get_errno(chdir(target));
    JS_FreeCString(ctx, target);
    return JS_NewInt32(ctx, err);
}

static JSValue js_os_mkdir(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    int mode, ret;
    const char *path;

    if (argc >= 2) {
        if (JS_ToInt32(ctx, &mode, argv[1]))
            return JS_EXCEPTION;
    } else {
        mode = 0777;
    }
    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
#if defined(_WIN32)
    (void)mode;
    ret = js_get_errno(mkdir(path));
#else
    ret = js_get_errno(mkdir(path, mode));
#endif
    JS_FreeCString(ctx, path);
    return JS_NewInt32(ctx, ret);
}

/* return [array, errorcode] */
static JSValue js_os_readdir(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *path;
    DIR *f;
    struct dirent *d;
    JSValue obj;
    int err;
    uint32_t len;

    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    obj = JS_NewArray(ctx);
    if (JS_IsException(obj)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    f = opendir(path);
    if (!f)
        err = errno;
    else
        err = 0;
    JS_FreeCString(ctx, path);
    if (!f)
        goto done;
    len = 0;
    for(;;) {
        errno = 0;
        d = readdir(f);
        if (!d) {
            err = errno;
            break;
        }
        JS_DefinePropertyValueUint32(ctx, obj, len++,
                                     JS_NewString(ctx, d->d_name),
                                     JS_PROP_C_W_E);
    }
    closedir(f);
 done:
    return make_obj_error(ctx, obj, err);
}

#if !defined(_WIN32)
static int64_t timespec_to_ms(const struct timespec *tv)
{
    return (int64_t)tv->tv_sec * 1000 + (tv->tv_nsec / 1000000);
}
#endif

/* return [obj, errcode] */
static JSValue js_os_stat(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv, int is_lstat)
{
    const char *path;
    int err, res;
    struct stat st;
    JSValue obj;

    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
#if defined(_WIN32)
    res = stat(path, &st);
#else
    if (is_lstat)
        res = lstat(path, &st);
    else
        res = stat(path, &st);
#endif
    if (res < 0)
        err = errno;
    else
        err = 0;
    JS_FreeCString(ctx, path);
    if (res < 0) {
        obj = JS_NULL;
    } else {
        obj = JS_NewObject(ctx);
        if (JS_IsException(obj))
            return JS_EXCEPTION;
        JS_DefinePropertyValueStr(ctx, obj, "dev",
                                  JS_NewInt64(ctx, st.st_dev),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "ino",
                                  JS_NewInt64(ctx, st.st_ino),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "mode",
                                  JS_NewInt32(ctx, st.st_mode),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "nlink",
                                  JS_NewInt64(ctx, st.st_nlink),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "uid",
                                  JS_NewInt64(ctx, st.st_uid),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "gid",
                                  JS_NewInt64(ctx, st.st_gid),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "rdev",
                                  JS_NewInt64(ctx, st.st_rdev),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "size",
                                  JS_NewInt64(ctx, st.st_size),
                                  JS_PROP_C_W_E);
#if !defined(_WIN32)
        JS_DefinePropertyValueStr(ctx, obj, "blocks",
                                  JS_NewInt64(ctx, st.st_blocks),
                                  JS_PROP_C_W_E);
#endif
#if defined(_WIN32)
        JS_DefinePropertyValueStr(ctx, obj, "atime",
                                  JS_NewInt64(ctx, (int64_t)st.st_atime * 1000),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "mtime",
                                  JS_NewInt64(ctx, (int64_t)st.st_mtime * 1000),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "ctime",
                                  JS_NewInt64(ctx, (int64_t)st.st_ctime * 1000),
                                  JS_PROP_C_W_E);
#elif defined(__APPLE__)
        JS_DefinePropertyValueStr(ctx, obj, "atime",
                                  JS_NewInt64(ctx, timespec_to_ms(&st.st_atimespec)),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "mtime",
                                  JS_NewInt64(ctx, timespec_to_ms(&st.st_mtimespec)),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "ctime",
                                  JS_NewInt64(ctx, timespec_to_ms(&st.st_ctimespec)),
                                  JS_PROP_C_W_E);
#else
        JS_DefinePropertyValueStr(ctx, obj, "atime",
                                  JS_NewInt64(ctx, timespec_to_ms(&st.st_atim)),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "mtime",
                                  JS_NewInt64(ctx, timespec_to_ms(&st.st_mtim)),
                                  JS_PROP_C_W_E);
        JS_DefinePropertyValueStr(ctx, obj, "ctime",
                                  JS_NewInt64(ctx, timespec_to_ms(&st.st_ctim)),
                                  JS_PROP_C_W_E);
#endif
    }
    return make_obj_error(ctx, obj, err);
}

#if !defined(_WIN32)
static void ms_to_timeval(struct timeval *tv, uint64_t v)
{
    tv->tv_sec = v / 1000;
    tv->tv_usec = (v % 1000) * 1000;
}
#endif

static JSValue js_os_utimes(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    const char *path;
    int64_t atime, mtime;
    int ret;

    if (JS_ToInt64(ctx, &atime, argv[1]))
        return JS_EXCEPTION;
    if (JS_ToInt64(ctx, &mtime, argv[2]))
        return JS_EXCEPTION;
    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
#if defined(_WIN32)
    {
        struct _utimbuf times;
        times.actime = atime / 1000;
        times.modtime = mtime / 1000;
        ret = js_get_errno(_utime(path, &times));
    }
#else
    {
        struct timeval times[2];
        ms_to_timeval(&times[0], atime);
        ms_to_timeval(&times[1], mtime);
        ret = js_get_errno(utimes(path, times));
    }
#endif
    JS_FreeCString(ctx, path);
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 同步睡眠（阻塞）(os.sleep)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=延迟时间（毫秒）
 * @return 成功返回 0，失败返回负数错误码
 * 
 * 阻塞当前线程指定的毫秒数。
 * Windows 使用 Sleep()，POSIX 系统使用 nanosleep()。
 * 负延迟会被截断为 0，Windows 上最大延迟限制为 INT32_MAX 毫秒。
 */
/* sleep(delay_ms) */
static JSValue js_os_sleep(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv)
{
    int64_t delay;
    int ret;

    if (JS_ToInt64(ctx, &delay, argv[0]))
        return JS_EXCEPTION;
    if (delay < 0)
        delay = 0;
#if defined(_WIN32)
    {
        if (delay > INT32_MAX)
            delay = INT32_MAX;
        Sleep(delay);
        ret = 0;
    }
#else
    {
        struct timespec ts;

        ts.tv_sec = delay / 1000;
        ts.tv_nsec = (delay % 1000) * 1000000;
        ret = js_get_errno(nanosleep(&ts, NULL));
    }
#endif
    return JS_NewInt32(ctx, ret);
}

#if defined(_WIN32)
static char *realpath(const char *path, char *buf)
{
    if (!_fullpath(buf, path, PATH_MAX)) {
        errno = ENOENT;
        return NULL;
    } else {
        return buf;
    }
}
#endif

/**
 * @brief 获取文件绝对路径 (os.realpath)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=文件路径
 * @return 返回 [绝对路径，错误码] 数组
 * 
 * 将相对路径转换为绝对路径，解析符号链接。
 * Windows 上使用 _fullpath 实现，POSIX 使用 realpath。
 * 失败时返回空字符串和错误码。
 */
/* return [path, errorcode] */
static JSValue js_os_realpath(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    const char *path;
    char buf[PATH_MAX], *res;
    int err;

    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    res = realpath(path, buf);
    JS_FreeCString(ctx, path);
    if (!res) {
        buf[0] = '\0';
        err = errno;
    } else {
        err = 0;
    }
    return make_string_error(ctx, buf, err);
}

#if !defined(_WIN32)
/**
 * @brief 创建符号链接 (os.symlink)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=目标路径，[1]=链接路径
 * @return 成功返回 0，失败返回负数错误码
 * 
 * 创建一个指向目标文件的符号链接。
 * 仅在 POSIX 系统上可用，Windows 不支持。
 */
static JSValue js_os_symlink(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    const char *target, *linkpath;
    int err;

    target = JS_ToCString(ctx, argv[0]);
    if (!target)
        return JS_EXCEPTION;
    linkpath = JS_ToCString(ctx, argv[1]);
    if (!linkpath) {
        JS_FreeCString(ctx, target);
        return JS_EXCEPTION;
    }
    err = js_get_errno(symlink(target, linkpath));
    JS_FreeCString(ctx, target);
    JS_FreeCString(ctx, linkpath);
    return JS_NewInt32(ctx, err);
}

/**
 * @brief 读取符号链接目标 (os.readlink)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=符号链接路径
 * @return 返回 [目标路径，错误码] 数组
 * 
 * 获取符号链接指向的目标文件路径。
 * 仅在 POSIX 系统上可用，返回路径不包含末尾的 null 终止符。
 */
/* return [path, errorcode] */
static JSValue js_os_readlink(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    const char *path;
    char buf[PATH_MAX];
    int err;
    ssize_t res;

    path = JS_ToCString(ctx, argv[0]);
    if (!path)
        return JS_EXCEPTION;
    res = readlink(path, buf, sizeof(buf) - 1);
    if (res < 0) {
        buf[0] = '\0';
        err = errno;
    } else {
        buf[res] = '\0';
        err = 0;
    }
    JS_FreeCString(ctx, path);
    return make_string_error(ctx, buf, err);
}

/**
 * @brief 从 JS 对象构建环境变量数组
 * @param ctx JS 上下文
 * @param obj JS 对象，属性名为环境变量名，属性值为环境变量值
 * @return 环境变量数组（NULL 结尾），失败返回 NULL
 * 
 * 将 JS 对象转换为 execve 可用的 char** 环境变量数组。
 * 每个环境变量格式为 "KEY=VALUE"，数组末尾为 NULL 指针。
 * 调用者负责使用 js_free 释放返回的数组和每个字符串。
 */
static char **build_envp(JSContext *ctx, JSValueConst obj)
{
    uint32_t len, i;
    JSPropertyEnum *tab;
    char **envp, *pair;
    const char *key, *str;
    JSValue val;
    size_t key_len, str_len;

    if (JS_GetOwnPropertyNames(ctx, &tab, &len, obj,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
        return NULL;
    envp = js_mallocz(ctx, sizeof(envp[0]) * ((size_t)len + 1));
    if (!envp)
        goto fail;
    for(i = 0; i < len; i++) {
        val = JS_GetProperty(ctx, obj, tab[i].atom);
        if (JS_IsException(val))
            goto fail;
        str = JS_ToCString(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str)
            goto fail;
        key = JS_AtomToCString(ctx, tab[i].atom);
        if (!key) {
            JS_FreeCString(ctx, str);
            goto fail;
        }
        key_len = strlen(key);
        str_len = strlen(str);
        pair = js_malloc(ctx, key_len + str_len + 2);
        if (!pair) {
            JS_FreeCString(ctx, key);
            JS_FreeCString(ctx, str);
            goto fail;
        }
        memcpy(pair, key, key_len);
        pair[key_len] = '=';
        memcpy(pair + key_len + 1, str, str_len);
        pair[key_len + 1 + str_len] = '\0';
        envp[i] = pair;
        JS_FreeCString(ctx, key);
        JS_FreeCString(ctx, str);
    }
 done:
    JS_FreePropertyEnum(ctx, tab, len);
    return envp;
 fail:
    if (envp) {
        for(i = 0; i < len; i++)
            js_free(ctx, envp[i]);
        js_free(ctx, envp);
        envp = NULL;
    }
    goto done;
}

/**
 * @brief 模拟 execvpe 函数（在 PATH 中搜索可执行文件）
 * @param filename 可执行文件名
 * @param argv 参数数组
 * @param envp 环境变量数组
 * @return 成功不返回，失败返回 -1
 * 
 * 在非 GNU 系统上模拟 execvpe 的行为：
 * - 如果 filename 包含 '/'，直接 execve
 * - 否则在 PATH 环境变量的各个目录中搜索
 * - 找到第一个可执行的文件并 execve
 * - 如果所有路径都失败，返回最后一个错误（EACCES 优先）
 */
/* execvpe is not available on non GNU systems */
static int my_execvpe(const char *filename, char **argv, char **envp)
{
    char *path, *p, *p_next, *p1;
    char buf[PATH_MAX];
    size_t filename_len, path_len;
    BOOL eacces_error;

    filename_len = strlen(filename);
    if (filename_len == 0) {
        errno = ENOENT;
        return -1;
    }
    if (strchr(filename, '/'))
        return execve(filename, argv, envp);

    path = getenv("PATH");
    if (!path)
        path = (char *)"/bin:/usr/bin";
    eacces_error = FALSE;
    p = path;
    for(p = path; p != NULL; p = p_next) {
        p1 = strchr(p, ':');
        if (!p1) {
            p_next = NULL;
            path_len = strlen(p);
        } else {
            p_next = p1 + 1;
            path_len = p1 - p;
        }
        /* path too long */
        if ((path_len + 1 + filename_len + 1) > PATH_MAX)
            continue;
        memcpy(buf, p, path_len);
        buf[path_len] = '/';
        memcpy(buf + path_len + 1, filename, filename_len);
        buf[path_len + 1 + filename_len] = '\0';

        execve(buf, argv, envp);

        switch(errno) {
        case EACCES:
            eacces_error = TRUE;
            break;
        case ENOENT:
        case ENOTDIR:
            break;
        default:
            return -1;
        }
    }
    if (eacces_error)
        errno = EACCES;
    return -1;
}

/**
 * @brief 执行外部进程 (os.exec)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=参数数组，[1]=选项对象（可选）
 * @return 阻塞模式下返回退出码，非阻塞模式返回 PID
 * 
 * 创建子进程执行指定的可执行文件。
 * 选项对象可包含：
 * - block: 是否阻塞等待（默认 true）
 * - usePath: 是否在 PATH 中搜索（默认 true）
 * - file: 可执行文件路径（默认使用 args[0]）
 * - cwd: 工作目录
 * - env: 环境变量对象
 * - uid/gid: 用户/组 ID
 * - stdin/stdout/stderr: 文件描述符重定向
 * 
 * 子进程会关闭所有继承的文件描述符（3 及以上）。
 * 支持 closefrom() 的系统会使用它，否则手动关闭到 1024。
 */
/* exec(args[, options]) -> exitcode */
static JSValue js_os_exec(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    JSValueConst options, args = argv[0];
    JSValue val, ret_val;
    const char **exec_argv, *file = NULL, *str, *cwd = NULL;
    char **envp = environ;
    uint32_t exec_argc, i;
    int ret, pid, status;
    BOOL block_flag = TRUE, use_path = TRUE;
    static const char *std_name[3] = { "stdin", "stdout", "stderr" };
    int std_fds[3];
    uint32_t uid = -1, gid = -1;

    val = JS_GetPropertyStr(ctx, args, "length");
    if (JS_IsException(val))
        return JS_EXCEPTION;
    ret = JS_ToUint32(ctx, &exec_argc, val);
    JS_FreeValue(ctx, val);
    if (ret)
        return JS_EXCEPTION;
    /* arbitrary limit to avoid overflow */
    if (exec_argc < 1 || exec_argc > 65535) {
        return JS_ThrowTypeError(ctx, "invalid number of arguments");
    }
    exec_argv = js_mallocz(ctx, sizeof(exec_argv[0]) * (exec_argc + 1));
    if (!exec_argv)
        return JS_EXCEPTION;
    for(i = 0; i < exec_argc; i++) {
        val = JS_GetPropertyUint32(ctx, args, i);
        if (JS_IsException(val))
            goto exception;
        str = JS_ToCString(ctx, val);
        JS_FreeValue(ctx, val);
        if (!str)
            goto exception;
        exec_argv[i] = str;
    }
    exec_argv[exec_argc] = NULL;

    for(i = 0; i < 3; i++)
        std_fds[i] = i;

    /* get the options, if any */
    if (argc >= 2) {
        options = argv[1];

        if (get_bool_option(ctx, &block_flag, options, "block"))
            goto exception;
        if (get_bool_option(ctx, &use_path, options, "usePath"))
            goto exception;

        val = JS_GetPropertyStr(ctx, options, "file");
        if (JS_IsException(val))
            goto exception;
        if (!JS_IsUndefined(val)) {
            file = JS_ToCString(ctx, val);
            JS_FreeValue(ctx, val);
            if (!file)
                goto exception;
        }

        val = JS_GetPropertyStr(ctx, options, "cwd");
        if (JS_IsException(val))
            goto exception;
        if (!JS_IsUndefined(val)) {
            cwd = JS_ToCString(ctx, val);
            JS_FreeValue(ctx, val);
            if (!cwd)
                goto exception;
        }

        /* stdin/stdout/stderr handles */
        for(i = 0; i < 3; i++) {
            val = JS_GetPropertyStr(ctx, options, std_name[i]);
            if (JS_IsException(val))
                goto exception;
            if (!JS_IsUndefined(val)) {
                int fd;
                ret = JS_ToInt32(ctx, &fd, val);
                JS_FreeValue(ctx, val);
                if (ret)
                    goto exception;
                std_fds[i] = fd;
            }
        }

        val = JS_GetPropertyStr(ctx, options, "env");
        if (JS_IsException(val))
            goto exception;
        if (!JS_IsUndefined(val)) {
            envp = build_envp(ctx, val);
            JS_FreeValue(ctx, val);
            if (!envp)
                goto exception;
        }

        val = JS_GetPropertyStr(ctx, options, "uid");
        if (JS_IsException(val))
            goto exception;
        if (!JS_IsUndefined(val)) {
            ret = JS_ToUint32(ctx, &uid, val);
            JS_FreeValue(ctx, val);
            if (ret)
                goto exception;
        }

        val = JS_GetPropertyStr(ctx, options, "gid");
        if (JS_IsException(val))
            goto exception;
        if (!JS_IsUndefined(val)) {
            ret = JS_ToUint32(ctx, &gid, val);
            JS_FreeValue(ctx, val);
            if (ret)
                goto exception;
        }
    }

    pid = fork();
    if (pid < 0) {
        JS_ThrowTypeError(ctx, "fork error");
        goto exception;
    }
    if (pid == 0) {
        /* child */

        /* remap the stdin/stdout/stderr handles if necessary */
        for(i = 0; i < 3; i++) {
            if (std_fds[i] != i) {
                if (dup2(std_fds[i], i) < 0)
                    _exit(127);
            }
        }
#if defined(HAVE_CLOSEFROM)
        /* closefrom() is available on many recent unix systems:
           Linux with glibc 2.34+, Solaris 9+, FreeBSD 7.3+,
           NetBSD 3.0+, OpenBSD 3.5+.
           Linux with the musl libc and macOS don't have it.
         */

        closefrom(3);
#else
        {
            /* Close the file handles manually, limit to 1024 to avoid
               costly loop on linux Alpine where sysconf(_SC_OPEN_MAX)
               returns a huge value 1048576.
               Patch inspired by nicolas-duteil-nova. See also:
               https://stackoverflow.com/questions/73229353/
               https://stackoverflow.com/questions/899038/#918469
             */
            int fd_max = min_int(sysconf(_SC_OPEN_MAX), 1024);
            for(i = 3; i < fd_max; i++)
                close(i);
        }
#endif
        if (cwd) {
            if (chdir(cwd) < 0)
                _exit(127);
        }
        if (uid != -1) {
            if (setuid(uid) < 0)
                _exit(127);
        }
        if (gid != -1) {
            if (setgid(gid) < 0)
                _exit(127);
        }

        if (!file)
            file = exec_argv[0];
        if (use_path)
            ret = my_execvpe(file, (char **)exec_argv, envp);
        else
            ret = execve(file, (char **)exec_argv, envp);
        _exit(127);
    }
    /* parent */
    if (block_flag) {
        for(;;) {
            ret = waitpid(pid, &status, 0);
            if (ret == pid) {
                if (WIFEXITED(status)) {
                    ret = WEXITSTATUS(status);
                    break;
                } else if (WIFSIGNALED(status)) {
                    ret = -WTERMSIG(status);
                    break;
                }
            }
        }
    } else {
        ret = pid;
    }
    ret_val = JS_NewInt32(ctx, ret);
 done:
    JS_FreeCString(ctx, file);
    JS_FreeCString(ctx, cwd);
    for(i = 0; i < exec_argc; i++)
        JS_FreeCString(ctx, exec_argv[i]);
    js_free(ctx, exec_argv);
    if (envp != environ) {
        char **p;
        p = envp;
        while (*p != NULL) {
            js_free(ctx, *p);
            p++;
        }
        js_free(ctx, envp);
    }
    return ret_val;
 exception:
    ret_val = JS_EXCEPTION;
    goto done;
}

/**
 * @brief 获取当前进程 ID (os.getpid)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 当前进程 ID
 * 
 * 返回调用进程的 PID（进程标识符）。
 * 仅在 POSIX 系统上可用。
 */
/* getpid() -> pid */
static JSValue js_os_getpid(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv)
{
    return JS_NewInt32(ctx, getpid());
}

/**
 * @brief 等待子进程状态变化 (os.waitpid)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=进程 ID，[1]=选项标志
 * @return 返回 [PID, 状态码] 数组，失败时 PID 为负数错误码
 * 
 * 等待指定的子进程状态发生变化（终止或停止）。
 * 选项标志：
 * - WNOHANG: 非阻塞模式，无状态变化时立即返回
 * 
 * 返回值说明：
 * - 正常退出：状态码为退出码（0-255）
 * - 信号终止：状态码为负的信号编号
 */
/* waitpid(pid, block) -> [pid, status] */
static JSValue js_os_waitpid(JSContext *ctx, JSValueConst this_val,
                             int argc, JSValueConst *argv)
{
    int pid, status, options, ret;
    JSValue obj;

    if (JS_ToInt32(ctx, &pid, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &options, argv[1]))
        return JS_EXCEPTION;

    ret = waitpid(pid, &status, options);
    if (ret < 0) {
        ret = -errno;
        status = 0;
    }

    obj = JS_NewArray(ctx);
    if (JS_IsException(obj))
        return obj;
    JS_DefinePropertyValueUint32(ctx, obj, 0, JS_NewInt32(ctx, ret),
                                 JS_PROP_C_W_E);
    JS_DefinePropertyValueUint32(ctx, obj, 1, JS_NewInt32(ctx, status),
                                 JS_PROP_C_W_E);
    return obj;
}

/**
 * @brief 创建匿名管道 (os.pipe)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 返回 [读端 FD, 写端 FD] 数组，失败返回 null
 * 
 * 创建一对相连的文件描述符，用于进程间通信。
 * - pipe_fds[0]: 读端，用于从管道读取数据
 * - pipe_fds[1]: 写端，用于向管道写入数据
 * 
 * 数据流动是单向的：写端 → 管道 → 读端。
 * 通常在 fork() 前创建，用于父子进程通信。
 */
/* pipe() -> [read_fd, write_fd] or null if error */
static JSValue js_os_pipe(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    int pipe_fds[2], ret;
    JSValue obj;

    ret = pipe(pipe_fds);
    if (ret < 0)
        return JS_NULL;
    obj = JS_NewArray(ctx);
    if (JS_IsException(obj))
        return obj;
    JS_DefinePropertyValueUint32(ctx, obj, 0, JS_NewInt32(ctx, pipe_fds[0]),
                                 JS_PROP_C_W_E);
    JS_DefinePropertyValueUint32(ctx, obj, 1, JS_NewInt32(ctx, pipe_fds[1]),
                                 JS_PROP_C_W_E);
    return obj;
}

/**
 * @brief 发送信号到进程 (os.kill)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=目标 PID，[1]=信号编号
 * @return 成功返回 0，失败返回负数错误码
 * 
 * 向指定进程发送信号。
 * 常用信号：
 * - SIGINT (2): 中断信号（Ctrl+C）
 * - SIGTERM (15): 终止信号
 * - SIGKILL (9): 强制杀死（不可捕获）
 * - SIGUSR1/SIGUSR2: 用户自定义信号
 * 
 * 如果 pid 为负数，信号发送到进程组。
 * 仅在 POSIX 系统上可用。
 */
/* kill(pid, sig) */
static JSValue js_os_kill(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv)
{
    int pid, sig, ret;

    if (JS_ToInt32(ctx, &pid, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &sig, argv[1]))
        return JS_EXCEPTION;
    ret = js_get_errno(kill(pid, sig));
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 复制文件描述符 (os.dup)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=原文件描述符
 * @return 新的文件描述符，失败返回负数错误码
 * 
 * 创建指定文件描述符的副本。
 * 返回的新 FD 是系统可用的最小非负整数。
 * 两个 FD 共享相同的文件偏移量和状态标志。
 * 常用于重定向前保存原 FD。
 */
/* dup(fd) */
static JSValue js_os_dup(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    int fd, ret;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    ret = js_get_errno(dup(fd));
    return JS_NewInt32(ctx, ret);
}

/**
 * @brief 复制文件描述符到指定编号 (os.dup2)
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=原 FD，[1]=目标 FD
 * @return 目标 FD，失败返回负数错误码
 * 
 * 将原 FD 复制到指定的目标编号。
 * 如果目标 FD 已打开，会先关闭它。
 * 如果原 FD 和目标 FD 相同，直接返回不做任何操作。
 * 常用于将 FD 重定向到标准输入/输出/错误（0/1/2）。
 */
/* dup2(fd, fd2) */
static JSValue js_os_dup2(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv)
{
    int fd, fd2, ret;

    if (JS_ToInt32(ctx, &fd, argv[0]))
        return JS_EXCEPTION;
    if (JS_ToInt32(ctx, &fd2, argv[1]))
        return JS_EXCEPTION;
    ret = js_get_errno(dup2(fd, fd2));
    return JS_NewInt32(ctx, ret);
}

#endif /* !_WIN32 */

#ifdef USE_WORKER

/* Worker */

typedef struct {
    JSWorkerMessagePipe *recv_pipe;
    JSWorkerMessagePipe *send_pipe;
    JSWorkerMessageHandler *msg_handler;
} JSWorkerData;

typedef struct {
    char *filename; /* module filename */
    char *basename; /* module base name */
    JSWorkerMessagePipe *recv_pipe, *send_pipe;
    int strip_flags;
} WorkerFuncArgs;

typedef struct {
    int ref_count;
    uint64_t buf[0];
} JSSABHeader;

static JSClassID js_worker_class_id;
static JSContext *(*js_worker_new_context_func)(JSRuntime *rt);

static int atomic_add_int(int *ptr, int v)
{
    return atomic_fetch_add((_Atomic(uint32_t) *)ptr, v) + v;
}

/* shared array buffer allocator */
static void *js_sab_alloc(void *opaque, size_t size)
{
    JSSABHeader *sab;
    sab = malloc(sizeof(JSSABHeader) + size);
    if (!sab)
        return NULL;
    sab->ref_count = 1;
    return sab->buf;
}

static void js_sab_free(void *opaque, void *ptr)
{
    JSSABHeader *sab;
    int ref_count;
    sab = (JSSABHeader *)((uint8_t *)ptr - sizeof(JSSABHeader));
    ref_count = atomic_add_int(&sab->ref_count, -1);
    assert(ref_count >= 0);
    if (ref_count == 0) {
        free(sab);
    }
}

static void js_sab_dup(void *opaque, void *ptr)
{
    JSSABHeader *sab;
    sab = (JSSABHeader *)((uint8_t *)ptr - sizeof(JSSABHeader));
    atomic_add_int(&sab->ref_count, 1);
}

static JSWorkerMessagePipe *js_new_message_pipe(void)
{
    JSWorkerMessagePipe *ps;

    ps = malloc(sizeof(*ps));
    if (!ps)
        return NULL;
    if (js_waker_init(&ps->waker)) {
        free(ps);
        return NULL;
    }
    ps->ref_count = 1;
    init_list_head(&ps->msg_queue);
    pthread_mutex_init(&ps->mutex, NULL);
    return ps;
}

static JSWorkerMessagePipe *js_dup_message_pipe(JSWorkerMessagePipe *ps)
{
    atomic_add_int(&ps->ref_count, 1);
    return ps;
}

static void js_free_message(JSWorkerMessage *msg)
{
    size_t i;
    /* free the SAB */
    for(i = 0; i < msg->sab_tab_len; i++) {
        js_sab_free(NULL, msg->sab_tab[i]);
    }
    free(msg->sab_tab);
    free(msg->data);
    free(msg);
}

static void js_free_message_pipe(JSWorkerMessagePipe *ps)
{
    struct list_head *el, *el1;
    JSWorkerMessage *msg;
    int ref_count;

    if (!ps)
        return;

    ref_count = atomic_add_int(&ps->ref_count, -1);
    assert(ref_count >= 0);
    if (ref_count == 0) {
        list_for_each_safe(el, el1, &ps->msg_queue) {
            msg = list_entry(el, JSWorkerMessage, link);
            js_free_message(msg);
        }
        pthread_mutex_destroy(&ps->mutex);
        js_waker_close(&ps->waker);
        free(ps);
    }
}

static void js_free_port(JSRuntime *rt, JSWorkerMessageHandler *port)
{
    if (port) {
        js_free_message_pipe(port->recv_pipe);
        JS_FreeValueRT(rt, port->on_message_func);
        if (port->link.prev)
            list_del(&port->link);
        js_free_rt(rt, port);
    }
}

static void js_worker_finalizer(JSRuntime *rt, JSValue val)
{
    JSWorkerData *worker = JS_GetOpaque(val, js_worker_class_id);
    if (worker) {
        js_free_message_pipe(worker->recv_pipe);
        js_free_message_pipe(worker->send_pipe);
        js_free_port(rt, worker->msg_handler);
        js_free_rt(rt, worker);
    }
}

static void js_worker_mark(JSRuntime *rt, JSValueConst val,
                           JS_MarkFunc *mark_func)
{
    JSWorkerData *worker = JS_GetOpaque(val, js_worker_class_id);
    if (worker) {
        JSWorkerMessageHandler *port = worker->msg_handler;
        if (port) {
            JS_MarkValue(rt, port->on_message_func, mark_func);
        }
    }
}

static JSClassDef js_worker_class = {
    "Worker",
    .finalizer = js_worker_finalizer,
    .gc_mark = js_worker_mark,
};

static void *worker_func(void *opaque)
{
    WorkerFuncArgs *args = opaque;
    JSRuntime *rt;
    JSThreadState *ts;
    JSContext *ctx;
    JSValue val;

    rt = JS_NewRuntime();
    if (rt == NULL) {
        fprintf(stderr, "JS_NewRuntime failure");
        exit(1);
    }
    JS_SetStripInfo(rt, args->strip_flags);
    js_std_init_handlers(rt);

    JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader, js_module_check_attributes, NULL);

    /* set the pipe to communicate with the parent */
    ts = JS_GetRuntimeOpaque(rt);
    ts->recv_pipe = args->recv_pipe;
    ts->send_pipe = args->send_pipe;

    /* function pointer to avoid linking the whole JS_NewContext() if
       not needed */
    ctx = js_worker_new_context_func(rt);
    if (ctx == NULL) {
        fprintf(stderr, "JS_NewContext failure");
    }

    JS_SetCanBlock(rt, TRUE);

    js_std_add_helpers(ctx, -1, NULL);

    val = JS_LoadModule(ctx, args->basename, args->filename);
    free(args->filename);
    free(args->basename);
    free(args);
    val = js_std_await(ctx, val);
    if (JS_IsException(val))
        js_std_dump_error(ctx);
    JS_FreeValue(ctx, val);

    js_std_loop(ctx);

    JS_FreeContext(ctx);
    js_std_free_handlers(rt);
    JS_FreeRuntime(rt);
    return NULL;
}

static JSValue js_worker_ctor_internal(JSContext *ctx, JSValueConst new_target,
                                       JSWorkerMessagePipe *recv_pipe,
                                       JSWorkerMessagePipe *send_pipe)
{
    JSValue obj = JS_UNDEFINED, proto;
    JSWorkerData *s;

    /* create the object */
    if (JS_IsUndefined(new_target)) {
        proto = JS_GetClassProto(ctx, js_worker_class_id);
    } else {
        proto = JS_GetPropertyStr(ctx, new_target, "prototype");
        if (JS_IsException(proto))
            goto fail;
    }
    obj = JS_NewObjectProtoClass(ctx, proto, js_worker_class_id);
    JS_FreeValue(ctx, proto);
    if (JS_IsException(obj))
        goto fail;
    s = js_mallocz(ctx, sizeof(*s));
    if (!s)
        goto fail;
    s->recv_pipe = js_dup_message_pipe(recv_pipe);
    s->send_pipe = js_dup_message_pipe(send_pipe);

    JS_SetOpaque(obj, s);
    return obj;
 fail:
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

static JSValue js_worker_ctor(JSContext *ctx, JSValueConst new_target,
                              int argc, JSValueConst *argv)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    WorkerFuncArgs *args = NULL;
    pthread_t tid;
    pthread_attr_t attr;
    JSValue obj = JS_UNDEFINED;
    int ret;
    const char *filename = NULL, *basename;
    JSAtom basename_atom;

    /* XXX: in order to avoid problems with resource liberation, we
       don't support creating workers inside workers */
    if (!is_main_thread(rt))
        return JS_ThrowTypeError(ctx, "cannot create a worker inside a worker");

    /* base name, assuming the calling function is a normal JS
       function */
    basename_atom = JS_GetScriptOrModuleName(ctx, 1);
    if (basename_atom == JS_ATOM_NULL) {
        return JS_ThrowTypeError(ctx, "could not determine calling script or module name");
    }
    basename = JS_AtomToCString(ctx, basename_atom);
    JS_FreeAtom(ctx, basename_atom);
    if (!basename)
        goto fail;

    /* module name */
    filename = JS_ToCString(ctx, argv[0]);
    if (!filename)
        goto fail;

    args = malloc(sizeof(*args));
    if (!args)
        goto oom_fail;
    memset(args, 0, sizeof(*args));
    args->filename = strdup(filename);
    args->basename = strdup(basename);

    /* ports */
    args->recv_pipe = js_new_message_pipe();
    if (!args->recv_pipe)
        goto oom_fail;
    args->send_pipe = js_new_message_pipe();
    if (!args->send_pipe)
        goto oom_fail;

    args->strip_flags = JS_GetStripInfo(rt);
    
    obj = js_worker_ctor_internal(ctx, new_target,
                                  args->send_pipe, args->recv_pipe);
    if (JS_IsException(obj))
        goto fail;

    pthread_attr_init(&attr);
    /* no join at the end */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&tid, &attr, worker_func, args);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        JS_ThrowTypeError(ctx, "could not create worker");
        goto fail;
    }
    JS_FreeCString(ctx, basename);
    JS_FreeCString(ctx, filename);
    return obj;
 oom_fail:
    JS_ThrowOutOfMemory(ctx);
 fail:
    JS_FreeCString(ctx, basename);
    JS_FreeCString(ctx, filename);
    if (args) {
        free(args->filename);
        free(args->basename);
        js_free_message_pipe(args->recv_pipe);
        js_free_message_pipe(args->send_pipe);
        free(args);
    }
    JS_FreeValue(ctx, obj);
    return JS_EXCEPTION;
}

/**
 * @brief Worker 发送消息到父线程 (worker.postMessage)
 * @param ctx JS 上下文
 * @param this_val Worker 对象
 * @param argc 参数个数
 * @param argv 参数数组 [0]=要发送的消息对象
 * @return 成功返回 JS_UNDEFINED，失败返回异常
 * 
 * 将 JS 对象序列化为字节流，通过管道发送到父线程。
 * 支持 SharedArrayBuffer 和引用对象的传输。
 * 消息被添加到队列并唤醒接收方。
 */
static JSValue js_worker_postMessage(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    JSWorkerData *worker = JS_GetOpaque2(ctx, this_val, js_worker_class_id);
    JSWorkerMessagePipe *ps;
    size_t data_len, sab_tab_len, i;
    uint8_t *data;
    JSWorkerMessage *msg;
    uint8_t **sab_tab;

    if (!worker)
        return JS_EXCEPTION;

    data = JS_WriteObject2(ctx, &data_len, argv[0],
                           JS_WRITE_OBJ_SAB | JS_WRITE_OBJ_REFERENCE,
                           &sab_tab, &sab_tab_len);
    if (!data)
        return JS_EXCEPTION;

    msg = malloc(sizeof(*msg));
    if (!msg)
        goto fail;
    msg->data = NULL;
    msg->sab_tab = NULL;

    /* must reallocate because the allocator may be different */
    msg->data = malloc(data_len);
    if (!msg->data)
        goto fail;
    memcpy(msg->data, data, data_len);
    msg->data_len = data_len;

    if (sab_tab_len > 0) {
        msg->sab_tab = malloc(sizeof(msg->sab_tab[0]) * sab_tab_len);
        if (!msg->sab_tab)
            goto fail;
        memcpy(msg->sab_tab, sab_tab, sizeof(msg->sab_tab[0]) * sab_tab_len);
    }
    msg->sab_tab_len = sab_tab_len;

    js_free(ctx, data);
    js_free(ctx, sab_tab);

    /* increment the SAB reference counts */
    for(i = 0; i < msg->sab_tab_len; i++) {
        js_sab_dup(NULL, msg->sab_tab[i]);
    }

    ps = worker->send_pipe;
    pthread_mutex_lock(&ps->mutex);
    /* indicate that data is present */
    if (list_empty(&ps->msg_queue))
        js_waker_signal(&ps->waker);
    list_add_tail(&msg->link, &ps->msg_queue);
    pthread_mutex_unlock(&ps->mutex);
    return JS_UNDEFINED;
 fail:
    if (msg) {
        free(msg->data);
        free(msg->sab_tab);
        free(msg);
    }
    js_free(ctx, data);
    js_free(ctx, sab_tab);
    return JS_EXCEPTION;

}

/**
 * @brief 设置 Worker 消息接收处理函数 (worker.onmessage = func)
 * @param ctx JS 上下文
 * @param this_val Worker 对象
 * @param func 消息处理函数或 null
 * @return 成功返回 JS_UNDEFINED，失败返回异常
 * 
 * 设置或清除 Worker 的消息接收回调函数。
 * 当父线程发送消息时，该函数会被调用。
 * 设置为 null 时移除监听器并释放资源。
 */
static JSValue js_worker_set_onmessage(JSContext *ctx, JSValueConst this_val,
                                   JSValueConst func)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    JSWorkerData *worker = JS_GetOpaque2(ctx, this_val, js_worker_class_id);
    JSWorkerMessageHandler *port;

    if (!worker)
        return JS_EXCEPTION;

    port = worker->msg_handler;
    if (JS_IsNull(func)) {
        if (port) {
            js_free_port(rt, port);
            worker->msg_handler = NULL;
        }
    } else {
        if (!JS_IsFunction(ctx, func))
            return JS_ThrowTypeError(ctx, "not a function");
        if (!port) {
            port = js_mallocz(ctx, sizeof(*port));
            if (!port)
                return JS_EXCEPTION;
            port->recv_pipe = js_dup_message_pipe(worker->recv_pipe);
            port->on_message_func = JS_NULL;
            list_add_tail(&port->link, &ts->port_list);
            worker->msg_handler = port;
        }
        JS_FreeValue(ctx, port->on_message_func);
        port->on_message_func = JS_DupValue(ctx, func);
    }
    return JS_UNDEFINED;
}

/**
 * @brief 获取 Worker 消息接收处理函数 (worker.onmessage)
 * @param ctx JS 上下文
 * @param this_val Worker 对象
 * @return 消息处理函数或 null
 * 
 * 返回当前设置的 onmessage 回调函数。
 * 如果未设置则返回 null。
 */
static JSValue js_worker_get_onmessage(JSContext *ctx, JSValueConst this_val)
{
    JSWorkerData *worker = JS_GetOpaque2(ctx, this_val, js_worker_class_id);
    JSWorkerMessageHandler *port;
    if (!worker)
        return JS_EXCEPTION;
    port = worker->msg_handler;
    if (port) {
        return JS_DupValue(ctx, port->on_message_func);
    } else {
        return JS_NULL;
    }
}

static const JSCFunctionListEntry js_worker_proto_funcs[] = {
    JS_CFUNC_DEF("postMessage", 1, js_worker_postMessage ),
    JS_CGETSET_DEF("onmessage", js_worker_get_onmessage, js_worker_set_onmessage ),
};

#endif /* USE_WORKER */

/**
 * @brief 设置 Worker 上下文创建函数（全局 API）
 * @param func 上下文创建函数指针
 * 
 * 用于设置 Worker 线程创建新 JS 上下文的回调函数。
 * 仅在启用 USE_WORKER 时有效。
 */
void js_std_set_worker_new_context_func(JSContext *(*func)(JSRuntime *rt))
{
#ifdef USE_WORKER
    js_worker_new_context_func = func;
#endif
}

#if defined(_WIN32)
#define OS_PLATFORM "win32"
#elif defined(__APPLE__)
#define OS_PLATFORM "darwin"
#elif defined(EMSCRIPTEN)
#define OS_PLATFORM "js"
#else
#define OS_PLATFORM "linux"
#endif

#define OS_FLAG(x) JS_PROP_INT32_DEF(#x, x, JS_PROP_CONFIGURABLE )

static const JSCFunctionListEntry js_os_funcs[] = {
    JS_CFUNC_DEF("open", 2, js_os_open ),
    OS_FLAG(O_RDONLY),
    OS_FLAG(O_WRONLY),
    OS_FLAG(O_RDWR),
    OS_FLAG(O_APPEND),
    OS_FLAG(O_CREAT),
    OS_FLAG(O_EXCL),
    OS_FLAG(O_TRUNC),
#if defined(_WIN32)
    OS_FLAG(O_BINARY),
    OS_FLAG(O_TEXT),
#endif
    JS_CFUNC_DEF("close", 1, js_os_close ),
    JS_CFUNC_DEF("seek", 3, js_os_seek ),
    JS_CFUNC_MAGIC_DEF("read", 4, js_os_read_write, 0 ),
    JS_CFUNC_MAGIC_DEF("write", 4, js_os_read_write, 1 ),
    JS_CFUNC_DEF("isatty", 1, js_os_isatty ),
    JS_CFUNC_DEF("ttyGetWinSize", 1, js_os_ttyGetWinSize ),
    JS_CFUNC_DEF("ttySetRaw", 1, js_os_ttySetRaw ),
    JS_CFUNC_DEF("remove", 1, js_os_remove ),
    JS_CFUNC_DEF("rename", 2, js_os_rename ),
    JS_CFUNC_MAGIC_DEF("setReadHandler", 2, js_os_setReadHandler, 0 ),
    JS_CFUNC_MAGIC_DEF("setWriteHandler", 2, js_os_setReadHandler, 1 ),
    JS_CFUNC_DEF("signal", 2, js_os_signal ),
    OS_FLAG(SIGINT),
    OS_FLAG(SIGABRT),
    OS_FLAG(SIGFPE),
    OS_FLAG(SIGILL),
    OS_FLAG(SIGSEGV),
    OS_FLAG(SIGTERM),
#if !defined(_WIN32)
    OS_FLAG(SIGQUIT),
    OS_FLAG(SIGPIPE),
    OS_FLAG(SIGALRM),
    OS_FLAG(SIGUSR1),
    OS_FLAG(SIGUSR2),
    OS_FLAG(SIGCHLD),
    OS_FLAG(SIGCONT),
    OS_FLAG(SIGSTOP),
    OS_FLAG(SIGTSTP),
    OS_FLAG(SIGTTIN),
    OS_FLAG(SIGTTOU),
#endif
    JS_CFUNC_DEF("now", 0, js_os_now ),
    JS_CFUNC_DEF("setTimeout", 2, js_os_setTimeout ),
    JS_CFUNC_DEF("clearTimeout", 1, js_os_clearTimeout ),
    JS_CFUNC_DEF("sleepAsync", 1, js_os_sleepAsync ),
    JS_PROP_STRING_DEF("platform", OS_PLATFORM, 0 ),
    JS_CFUNC_DEF("getcwd", 0, js_os_getcwd ),
    JS_CFUNC_DEF("chdir", 0, js_os_chdir ),
    JS_CFUNC_DEF("mkdir", 1, js_os_mkdir ),
    JS_CFUNC_DEF("readdir", 1, js_os_readdir ),
    /* st_mode constants */
    OS_FLAG(S_IFMT),
    OS_FLAG(S_IFIFO),
    OS_FLAG(S_IFCHR),
    OS_FLAG(S_IFDIR),
    OS_FLAG(S_IFBLK),
    OS_FLAG(S_IFREG),
#if !defined(_WIN32)
    OS_FLAG(S_IFSOCK),
    OS_FLAG(S_IFLNK),
    OS_FLAG(S_ISGID),
    OS_FLAG(S_ISUID),
#endif
    JS_CFUNC_MAGIC_DEF("stat", 1, js_os_stat, 0 ),
    JS_CFUNC_DEF("utimes", 3, js_os_utimes ),
    JS_CFUNC_DEF("sleep", 1, js_os_sleep ),
    JS_CFUNC_DEF("realpath", 1, js_os_realpath ),
#if !defined(_WIN32)
    JS_CFUNC_MAGIC_DEF("lstat", 1, js_os_stat, 1 ),
    JS_CFUNC_DEF("symlink", 2, js_os_symlink ),
    JS_CFUNC_DEF("readlink", 1, js_os_readlink ),
    JS_CFUNC_DEF("exec", 1, js_os_exec ),
    JS_CFUNC_DEF("getpid", 0, js_os_getpid ),
    JS_CFUNC_DEF("waitpid", 2, js_os_waitpid ),
    OS_FLAG(WNOHANG),
    JS_CFUNC_DEF("pipe", 0, js_os_pipe ),
    JS_CFUNC_DEF("kill", 2, js_os_kill ),
    JS_CFUNC_DEF("dup", 1, js_os_dup ),
    JS_CFUNC_DEF("dup2", 2, js_os_dup2 ),
#endif
};

/**
 * @brief OS 模块初始化函数
 * @param ctx JS 上下文
 * @param m 模块对象
 * @return 成功返回 0，失败返回负数
 * 
 * 初始化 os 模块，注册 Worker 类（如果启用）并导出所有 OS API 函数。
 * 设置 os_poll_func 为 js_os_poll 以支持事件轮询。
 */
static int js_os_init(JSContext *ctx, JSModuleDef *m)
{
    os_poll_func = js_os_poll;

#ifdef USE_WORKER
    {
        JSRuntime *rt = JS_GetRuntime(ctx);
        JSThreadState *ts = JS_GetRuntimeOpaque(rt);
        JSValue proto, obj;
        /* Worker class */
        JS_NewClassID(&js_worker_class_id);
        JS_NewClass(JS_GetRuntime(ctx), js_worker_class_id, &js_worker_class);
        proto = JS_NewObject(ctx);
        JS_SetPropertyFunctionList(ctx, proto, js_worker_proto_funcs, countof(js_worker_proto_funcs));

        obj = JS_NewCFunction2(ctx, js_worker_ctor, "Worker", 1,
                               JS_CFUNC_constructor, 0);
        JS_SetConstructor(ctx, obj, proto);

        JS_SetClassProto(ctx, js_worker_class_id, proto);

        /* set 'Worker.parent' if necessary */
        if (ts->recv_pipe && ts->send_pipe) {
            JS_DefinePropertyValueStr(ctx, obj, "parent",
                                      js_worker_ctor_internal(ctx, JS_UNDEFINED, ts->recv_pipe, ts->send_pipe),
                                      JS_PROP_C_W_E);
        }

        JS_SetModuleExport(ctx, m, "Worker", obj);
    }
#endif /* USE_WORKER */

    return JS_SetModuleExportList(ctx, m, js_os_funcs,
                                  countof(js_os_funcs));
}

/**
 * @brief 创建并初始化 OS 模块
 * @param ctx JS 上下文
 * @param module_name 模块名称
 * @return 模块对象，失败返回 NULL
 * 
 * 创建 C 模块并注册所有 OS 导出项。
 * 如果启用 Worker，还添加 Worker 导出项。
 */
JSModuleDef *js_init_module_os(JSContext *ctx, const char *module_name)
{
    JSModuleDef *m;
    m = JS_NewCModule(ctx, module_name, js_os_init);
    if (!m)
        return NULL;
    JS_AddModuleExportList(ctx, m, js_os_funcs, countof(js_os_funcs));
#ifdef USE_WORKER
    JS_AddModuleExport(ctx, m, "Worker");
#endif
    return m;
}

/**********************************************************/

/**
 * @brief print 函数实现（全局 print）
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组（要打印的值列表）
 * @return 返回 JS_UNDEFINED
 * 
 * 将参数打印到 stdout，字符串直接输出，其他类型使用 JS_PrintValue。
 * 参数之间用空格分隔，末尾添加换行符。
 */
static JSValue js_print(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv)
{
    int i;
    JSValueConst v;
    
    for(i = 0; i < argc; i++) {
        if (i != 0)
            putchar(' ');
        v = argv[i];
        if (JS_IsString(v)) {
            const char *str;
            size_t len;
            str = JS_ToCStringLen(ctx, &len, v);
            if (!str)
                return JS_EXCEPTION;
            fwrite(str, 1, len, stdout);
            JS_FreeCString(ctx, str);
        } else {
            JS_PrintValue(ctx, js_print_value_write, stdout, v, NULL);
        }
    }
    putchar('\n');
    return JS_UNDEFINED;
}

/**
 * @brief console.log 实现
 * @param ctx JS 上下文
 * @param this_val this 对象
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 调用 js_print 的结果
 * 
 * 调用 print 函数并刷新 stdout 缓冲区。
 */
static JSValue js_console_log(JSContext *ctx, JSValueConst this_val,
                              int argc, JSValueConst *argv)
{
    JSValue ret;
    ret = js_print(ctx, this_val, argc, argv);
    fflush(stdout);
    return ret;
}

/**
 * @brief 添加标准辅助函数到全局对象
 * @param ctx JS 上下文
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * 
 * 向全局对象添加以下属性和函数：
 * - console: 包含 log 方法的控制台对象
 * - performance: 包含 now 方法的性能对象
 * - scriptArgs: 命令行参数数组
 * - print: 打印函数
 * - __loadScript: 加载脚本的内部函数
 * 
 * 用于初始化 JS shell 环境。
 */
void js_std_add_helpers(JSContext *ctx, int argc, char **argv)
{
    JSValue global_obj, console, args, performance;
    int i;

    /* XXX: should these global definitions be enumerable? */
    global_obj = JS_GetGlobalObject(ctx);

    console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log",
                      JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, global_obj, "console", console);

    performance = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, performance, "now",
                      JS_NewCFunction(ctx, js_os_now, "now", 0));
    JS_SetPropertyStr(ctx, global_obj, "performance", performance);

    /* same methods as the mozilla JS shell */
    if (argc >= 0) {
        args = JS_NewArray(ctx);
        for(i = 0; i < argc; i++) {
            JS_SetPropertyUint32(ctx, args, i, JS_NewString(ctx, argv[i]));
        }
        JS_SetPropertyStr(ctx, global_obj, "scriptArgs", args);
    }

    JS_SetPropertyStr(ctx, global_obj, "print",
                      JS_NewCFunction(ctx, js_print, "print", 1));
    JS_SetPropertyStr(ctx, global_obj, "__loadScript",
                      JS_NewCFunction(ctx, js_loadScript, "__loadScript", 1));

    JS_FreeValue(ctx, global_obj);
}

/**
 * @brief 初始化运行时处理器
 * @param rt JS 运行时
 * 
 * 创建并初始化 JSThreadState 结构，包含：
 * - OS 读写处理器链表
 * - 信号处理器链表
 * - 定时器链表
 * - Worker 端口链表
 * - 被拒绝的 Promise 链表
 * 
 * 如果启用 Worker，还设置 SharedArrayBuffer 内存管理函数。
 */
void js_std_init_handlers(JSRuntime *rt)
{
    JSThreadState *ts;

    ts = malloc(sizeof(*ts));
    if (!ts) {
        fprintf(stderr, "Could not allocate memory for the worker");
        exit(1);
    }
    memset(ts, 0, sizeof(*ts));
    init_list_head(&ts->os_rw_handlers);
    init_list_head(&ts->os_signal_handlers);
    init_list_head(&ts->os_timers);
    init_list_head(&ts->port_list);
    init_list_head(&ts->rejected_promise_list);
    ts->next_timer_id = 1;

    JS_SetRuntimeOpaque(rt, ts);

#ifdef USE_WORKER
    /* set the SharedArrayBuffer memory handlers */
    {
        JSSharedArrayBufferFunctions sf;
        memset(&sf, 0, sizeof(sf));
        sf.sab_alloc = js_sab_alloc;
        sf.sab_free = js_sab_free;
        sf.sab_dup = js_sab_dup;
        JS_SetSharedArrayBufferFunctions(rt, &sf);
    }
#endif
}

/**
 * @brief 释放运行时处理器资源
 * @param rt JS 运行时
 * 
 * 清理并释放所有运行时资源：
 * - 所有 OS 读写处理器
 * - 所有信号处理器
 * - 所有定时器
 * - 所有被拒绝的 Promise 条目
 * - Worker 消息管道（如果启用）
 * 
 * 最后释放 JSThreadState 并清除运行时 opaque 指针。
 */
void js_std_free_handlers(JSRuntime *rt)
{
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    struct list_head *el, *el1;

    list_for_each_safe(el, el1, &ts->os_rw_handlers) {
        JSOSRWHandler *rh = list_entry(el, JSOSRWHandler, link);
        free_rw_handler(rt, rh);
    }

    list_for_each_safe(el, el1, &ts->os_signal_handlers) {
        JSOSSignalHandler *sh = list_entry(el, JSOSSignalHandler, link);
        free_sh(rt, sh);
    }

    list_for_each_safe(el, el1, &ts->os_timers) {
        JSOSTimer *th = list_entry(el, JSOSTimer, link);
        free_timer(rt, th);
    }

    list_for_each_safe(el, el1, &ts->rejected_promise_list) {
        JSRejectedPromiseEntry *rp = list_entry(el, JSRejectedPromiseEntry, link);
        JS_FreeValueRT(rt, rp->promise);
        JS_FreeValueRT(rt, rp->reason);
        free(rp);
    }

#ifdef USE_WORKER
    js_free_message_pipe(ts->recv_pipe);
    js_free_message_pipe(ts->send_pipe);

    list_for_each_safe(el, el1, &ts->port_list) {
        JSWorkerMessageHandler *port = list_entry(el, JSWorkerMessageHandler, link);
        /* unlink the message ports. They are freed by the Worker object */
        port->link.prev = NULL;
        port->link.next = NULL;
    }
#endif

    free(ts);
    JS_SetRuntimeOpaque(rt, NULL); /* fail safe */
}

static void js_std_dump_error1(JSContext *ctx, JSValueConst exception_val)
{
    JS_PrintValue(ctx, js_print_value_write, stderr, exception_val, NULL);
    fputc('\n', stderr);
}

void js_std_dump_error(JSContext *ctx)
{
    JSValue exception_val;

    exception_val = JS_GetException(ctx);
    js_std_dump_error1(ctx, exception_val);
    JS_FreeValue(ctx, exception_val);
}

static JSRejectedPromiseEntry *find_rejected_promise(JSContext *ctx, JSThreadState *ts,
                                                     JSValueConst promise)
{
    struct list_head *el;

    list_for_each(el, &ts->rejected_promise_list) {
        JSRejectedPromiseEntry *rp = list_entry(el, JSRejectedPromiseEntry, link);
        if (JS_SameValue(ctx, rp->promise, promise))
            return rp;
    }
    return NULL;
}

void js_std_promise_rejection_tracker(JSContext *ctx, JSValueConst promise,
                                      JSValueConst reason,
                                      BOOL is_handled, void *opaque)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    JSRejectedPromiseEntry *rp;

    if (!is_handled) {
        /* add a new entry if needed */
        rp = find_rejected_promise(ctx, ts, promise);
        if (!rp) {
            rp = malloc(sizeof(*rp));
            if (rp) {
                rp->promise = JS_DupValue(ctx, promise);
                rp->reason = JS_DupValue(ctx, reason);
                list_add_tail(&rp->link, &ts->rejected_promise_list);
            }
        }
    } else {
        /* the rejection is handled, so the entry can be removed if present */
        rp = find_rejected_promise(ctx, ts, promise);
        if (rp) {
            JS_FreeValue(ctx, rp->promise);
            JS_FreeValue(ctx, rp->reason);
            list_del(&rp->link);
            free(rp);
        }
    }
}

/* check if there are pending promise rejections. It must be done
   asynchrously in case a rejected promise is handled later. Currently
   we do it once the application is about to sleep. It could be done
   more often if needed. */
static void js_std_promise_rejection_check(JSContext *ctx)
{
    JSRuntime *rt = JS_GetRuntime(ctx);
    JSThreadState *ts = JS_GetRuntimeOpaque(rt);
    struct list_head *el;

    if (unlikely(!list_empty(&ts->rejected_promise_list))) {
        list_for_each(el, &ts->rejected_promise_list) {
            JSRejectedPromiseEntry *rp = list_entry(el, JSRejectedPromiseEntry, link);
            fprintf(stderr, "Possibly unhandled promise rejection: ");
            js_std_dump_error1(ctx, rp->reason);
        }
        exit(1);
    }
}

/* main loop which calls the user JS callbacks */
void js_std_loop(JSContext *ctx)
{
    int err;

    for(;;) {
        /* execute the pending jobs */
        for(;;) {
            err = JS_ExecutePendingJob(JS_GetRuntime(ctx), NULL);
            if (err <= 0) {
                if (err < 0)
                    js_std_dump_error(ctx);
                break;
            }
        }

        js_std_promise_rejection_check(ctx);
        
        if (!os_poll_func || os_poll_func(ctx))
            break;
    }
}

/* Wait for a promise and execute pending jobs while waiting for
   it. Return the promise result or JS_EXCEPTION in case of promise
   rejection. */
JSValue js_std_await(JSContext *ctx, JSValue obj)
{
    JSValue ret;
    int state;

    for(;;) {
        state = JS_PromiseState(ctx, obj);
        if (state == JS_PROMISE_FULFILLED) {
            ret = JS_PromiseResult(ctx, obj);
            JS_FreeValue(ctx, obj);
            break;
        } else if (state == JS_PROMISE_REJECTED) {
            ret = JS_Throw(ctx, JS_PromiseResult(ctx, obj));
            JS_FreeValue(ctx, obj);
            break;
        } else if (state == JS_PROMISE_PENDING) {
            int err;
            err = JS_ExecutePendingJob(JS_GetRuntime(ctx), NULL);
            if (err < 0) {
                js_std_dump_error(ctx);
            }
            if (err == 0) {
                js_std_promise_rejection_check(ctx);

                if (os_poll_func)
                    os_poll_func(ctx);
            }
        } else {
            /* not a promise */
            ret = obj;
            break;
        }
    }
    return ret;
}

void js_std_eval_binary(JSContext *ctx, const uint8_t *buf, size_t buf_len,
                        int load_only)
{
    JSValue obj, val;
    obj = JS_ReadObject(ctx, buf, buf_len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj))
        goto exception;
    if (load_only) {
        if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
            js_module_set_import_meta(ctx, obj, FALSE, FALSE);
        }
        JS_FreeValue(ctx, obj);
    } else {
        if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
            if (JS_ResolveModule(ctx, obj) < 0) {
                JS_FreeValue(ctx, obj);
                goto exception;
            }
            js_module_set_import_meta(ctx, obj, FALSE, TRUE);
            val = JS_EvalFunction(ctx, obj);
            val = js_std_await(ctx, val);
        } else {
            val = JS_EvalFunction(ctx, obj);
        }
        if (JS_IsException(val)) {
        exception:
            js_std_dump_error(ctx);
            exit(1);
        }
        JS_FreeValue(ctx, val);
    }
}

void js_std_eval_binary_json_module(JSContext *ctx,
                                    const uint8_t *buf, size_t buf_len,
                                    const char *module_name)
{
    JSValue obj;
    JSModuleDef *m;
    
    obj = JS_ReadObject(ctx, buf, buf_len, 0);
    if (JS_IsException(obj))
        goto exception;
    m = create_json_module(ctx, module_name, obj);
    if (!m) {
    exception:
        js_std_dump_error(ctx);
        exit(1);
    }
}

