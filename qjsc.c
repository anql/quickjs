/*
 * QuickJS 命令行编译器
 * 
 * 本文件实现了一个命令行工具，用于将 JavaScript 源代码编译成字节码或 C 代码。
 * 主要功能包括：
 * - 将 JS 脚本编译成字节码并嵌入到 C 文件中
 * - 生成可执行文件（将字节码与运行时链接）
 * - 支持 ES6 模块编译
 * - 支持动态模块加载
 * - 支持字节序交换（用于跨平台兼容性）
 * - 支持链接时优化（LTO）
 * 
 * 使用场景：
 * 1. 将 JS 代码预编译成字节码，加快启动速度
 * 2. 将 JS 代码嵌入到 C 项目中，无需单独分发 JS 文件
 * 3. 创建独立的 JS 可执行文件
 * 
 * Copyright (c) 2018-2021 Fabrice Bellard
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
#if !defined(_WIN32)
#include <sys/wait.h>  // waitpid 用于等待子进程
#endif

#include "cutils.h"      // 通用工具函数（DynBuf, mallocz, pstrcpy 等）
#include "quickjs-libc.h" // QuickJS 标准库绑定

/* ========== 数据结构定义 ========== */

/**
 * 名称列表条目
 * 用于存储 C 名称、模块名称等列表
 */
typedef struct {
    char *name;        // 完整名称（如文件名、模块名）
    char *short_name;  // 短名称（如 C 标识符）
    int flags;         // 标志位（如 CNameTypeEnum）
} namelist_entry_t;

/**
 * 名称列表
 * 动态数组，用于管理名称集合
 */
typedef struct namelist_t {
    namelist_entry_t *array;  // 条目数组
    int count;                // 当前条目数
    int size;                 // 数组容量
} namelist_t;

/**
 * 特性条目
 * 用于配置 QuickJS 运行时特性（如 Date、RegExp、Promise 等）
 */
typedef struct {
    const char *option_name;  // 命令行选项名（如 "date", "regexp"）
    const char *init_name;    // 初始化函数名后缀（如 "Date", "RegExp"）
} FeatureEntry;

/* ========== 全局变量 ========== */

static namelist_t cname_list;           // C 名称列表（生成的字节码变量名）
static namelist_t cmodule_list;         // C 模块列表（外部 C 模块）
static namelist_t init_module_list;     // 初始化模块列表（静态 C 模块）
static uint64_t feature_bitmap;         // 特性位图（启用/禁用哪些运行时特性）
static FILE *outfile;                   // 输出文件指针
static BOOL byte_swap;                  // 是否字节交换（用于跨平台字节码）
static BOOL dynamic_export;             // 是否需要动态导出符号（用于动态库加载）
static const char *c_ident_prefix = "qjsc_";  // 生成的 C 标识符前缀

#define FE_ALL (-1)  // 所有特性都启用（位图全 1）

/**
 * QuickJS 运行时特性列表
 * 
 * 每个特性对应一个可选的 JavaScript 功能模块。
 * 使用 -fno-<feature> 可以禁用特定特性以减小程序体积。
 * 
 * 索引位置很重要，对应 feature_bitmap 的位位置。
 */
static const FeatureEntry feature_list[] = {
    { "date", "Date" },           // 0: Date 对象支持
    { "eval", "Eval" },           // 1: eval() 函数支持
    { "string-normalize", "StringNormalize" },  // 2: Unicode 字符串规范化
    { "regexp", "RegExp" },       // 3: 正则表达式支持
    { "json", "JSON" },           // 4: JSON 对象支持
    { "proxy", "Proxy" },         // 5: Proxy 对象支持
    { "map", "MapSet" },          // 6: Map/Set 对象支持
    { "typedarray", "TypedArrays" },  // 7: TypedArray 支持
    { "promise", "Promise" },     // 8: Promise 支持
#define FE_MODULE_LOADER 9
    { "module-loader", NULL },    // 9: 模块加载器支持（无初始化函数）
    { "weakref", "WeakRef" },     // 10: WeakRef 支持
};

/**
 * 向名称列表添加条目
 * 
 * @param lp 目标列表
 * @param name 名称（如文件名、模块名）
 * @param short_name 短名称（C 标识符），可为 NULL
 * @param flags 标志位
 */
void namelist_add(namelist_t *lp, const char *name, const char *short_name,
                  int flags)
{
    namelist_entry_t *e;
    if (lp->count == lp->size) {
        // 扩容：增长 50% + 4 个元素
        size_t newsize = lp->size + (lp->size >> 1) + 4;
        namelist_entry_t *a =
            realloc(lp->array, sizeof(lp->array[0]) * newsize);
        /* XXX: check for realloc failure */
        lp->array = a;
        lp->size = newsize;
    }
    e =  &lp->array[lp->count++];
    e->name = strdup(name);
    if (short_name)
        e->short_name = strdup(short_name);
    else
        e->short_name = NULL;
    e->flags = flags;
}

/**
 * 释放名称列表
 * 
 * @param lp 要释放的列表
 */
void namelist_free(namelist_t *lp)
{
    while (lp->count > 0) {
        namelist_entry_t *e = &lp->array[--lp->count];
        free(e->name);
        free(e->short_name);
    }
    free(lp->array);
    lp->array = NULL;
    lp->size = 0;
}

/**
 * 查找名称列表中的条目
 * 
 * @param lp 列表
 * @param name 要查找的名称
 * @return 找到的条目，未找到返回 NULL
 */
namelist_entry_t *namelist_find(namelist_t *lp, const char *name)
{
    int i;
    for(i = 0; i < lp->count; i++) {
        namelist_entry_t *e = &lp->array[i];
        if (!strcmp(e->name, name))
            return e;
    }
    return NULL;
}

/**
 * 从文件名生成 C 标识符
 * 
 * 将文件名转换为合法的 C 变量名：
 * - 提取文件名（去掉路径）
 * - 去掉扩展名
 * - 添加前缀（默认 "qjsc_"）
 * - 将非字母数字字符替换为下划线
 * 
 * 例如："src/utils.js" → "qjsc_utils"
 * 
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param file 输入文件名
 */
static void get_c_name(char *buf, size_t buf_size, const char *file)
{
    const char *p, *r;
    size_t len, i;
    int c;
    char *q;

    // 提取文件名（去掉目录路径）
    p = strrchr(file, '/');
    if (!p)
        p = file;
    else
        p++;
    
    // 去掉扩展名
    r = strrchr(p, '.');
    if (!r)
        len = strlen(p);
    else
        len = r - p;
    
    // 添加前缀
    pstrcpy(buf, buf_size, c_ident_prefix);
    q = buf + strlen(buf);
    
    // 转换字符：非字母数字 → 下划线
    for(i = 0; i < len; i++) {
        c = p[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z'))) {
            c = '_';
        }
        if ((q - buf) < buf_size - 1)
            *q++ = c;
    }
    *q = '\0';
}

/**
 * 以十六进制格式输出字节数组到 C 文件
 * 
 * 输出格式：
 *   0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
 *   0x09, 0x0a, ...
 * 
 * 每行 8 个字节，便于阅读。
 * 
 * @param f 输出文件
 * @param buf 字节数组
 * @param len 字节数
 */
static void dump_hex(FILE *f, const uint8_t *buf, size_t len)
{
    size_t i, col;
    col = 0;
    for(i = 0; i < len; i++) {
        fprintf(f, " 0x%02x,", buf[i]);
        if (++col == 8) {
            fprintf(f, "\n");
            col = 0;
        }
    }
    if (col != 0)
        fprintf(f, "\n");
}

/**
 * C 名称类型枚举
 * 用于区分不同类型的生成代码
 */
typedef enum {
    CNAME_TYPE_SCRIPT,      // JS 脚本（主程序代码）
    CNAME_TYPE_MODULE,      // JS 模块（ES6 模块）
    CNAME_TYPE_JSON_MODULE, // JSON 模块（JSON/JSON5 数据）
} CNameTypeEnum;

/**
 * 输出对象代码到 C 文件
 * 
 * 将 JS 对象（编译后的字节码）序列化成 C 数组格式：
 *   const uint8_t qjsc_xxx[] = { 0x01, 0x02, ... };
 *   const uint32_t qjsc_xxx_size = 123;
 * 
 * @param ctx JS 上下文
 * @param fo 输出文件
 * @param obj 要序列化的 JS 对象（编译后的字节码）
 * @param c_name C 变量名
 * @param c_name_type 名称类型（脚本/模块/JSON 模块）
 */
static void output_object_code(JSContext *ctx,
                               FILE *fo, JSValueConst obj, const char *c_name,
                               CNameTypeEnum c_name_type)
{
    uint8_t *out_buf;
    size_t out_buf_len;
    int flags;

    // JSON 模块不需要字节码标志
    if (c_name_type == CNAME_TYPE_JSON_MODULE)
        flags = 0;
    else
        flags = JS_WRITE_OBJ_BYTECODE;  // 输出字节码格式
    
    // 如果需要字节交换（用于跨平台兼容）
    if (byte_swap)
        flags |= JS_WRITE_OBJ_BSWAP;
    
    // 序列化 JS 对象为字节码
    out_buf = JS_WriteObject(ctx, &out_buf_len, obj, flags);
    if (!out_buf) {
        js_std_dump_error(ctx);
        exit(1);
    }

    // 添加到名称列表，后续用于生成 main 函数中的加载代码
    namelist_add(&cname_list, c_name, NULL, c_name_type);

    // 输出大小声明
    fprintf(fo, "const uint32_t %s_size = %u;\n\n",
            c_name, (unsigned int)out_buf_len);
    
    // 输出字节数组
    fprintf(fo, "const uint8_t %s[%u] = {\n",
            c_name, (unsigned int)out_buf_len);
    dump_hex(fo, out_buf, out_buf_len);
    fprintf(fo, "};\n\n");

    js_free(ctx, out_buf);
}

/**
 * 虚拟模块初始化函数
 * 
 * 用于动态加载的模块或系统模块。
 * 编译时不应该被调用，仅用于创建模块桩。
 * 
 * @param ctx JS 上下文
 * @param m 模块对象
 * @return 永远不会返回（调用 abort）
 */
static int js_module_dummy_init(JSContext *ctx, JSModuleDef *m)
{
    /* should never be called when compiling JS code */
    abort();
}

/**
 * 生成唯一的 C 名称
 * 
 * 当多个文件生成相同的 C 名称时，通过添加数字后缀来避免冲突。
 * 例如：如果 "qjsc_utils" 已存在，则生成 "qjsc_utils_1"、"qjsc_utils_2" 等。
 * 
 * @param cname 输入/输出缓冲区（包含原始名称，返回唯一名称）
 * @param cname_size 缓冲区大小
 */
static void find_unique_cname(char *cname, size_t cname_size)
{
    char cname1[1024];
    int suffix_num;
    size_t len, max_len;
    assert(cname_size >= 32);
    /* find a C name not matching an existing module C name by
       adding a numeric suffix */
    len = strlen(cname);
    max_len = cname_size - 16;
    if (len > max_len)
        cname[max_len] = '\0';
    suffix_num = 1;
    for(;;) {
        snprintf(cname1, sizeof(cname1), "%s_%d", cname, suffix_num);
        if (!namelist_find(&cname_list, cname1))
            break;
        suffix_num++;
    }
    pstrcpy(cname, cname_size, cname1);
}

/**
 * 模块加载器回调函数
 * 
 * 当编译的 JS 代码中遇到 import 语句时，QuickJS 会调用此函数加载模块。
 * 支持三种模块类型：
 * 1. C 模块/系统模块（std、os 等）- 添加到静态初始化列表
 * 2. 动态库模块（.so 文件）- 运行时动态加载
 * 3. JS 模块（.js/.mjs/.json 文件）- 编译成字节码嵌入
 * 
 * @param ctx JS 上下文
 * @param module_name 模块名称（文件名或模块标识符）
 * @param opaque 不透明指针（未使用）
 * @param attributes 模块属性（用于 JSON5 检测）
 * @return 模块对象，失败返回 NULL
 */
JSModuleDef *jsc_module_loader(JSContext *ctx,
                               const char *module_name, void *opaque,
                               JSValueConst attributes)
{
    JSModuleDef *m;
    namelist_entry_t *e;

    /* check if it is a declared C or system module */
    e = namelist_find(&cmodule_list, module_name);
    if (e) {
        /* add in the static init module list */
        namelist_add(&init_module_list, e->name, e->short_name, 0);
        /* create a dummy module */
        m = JS_NewCModule(ctx, module_name, js_module_dummy_init);
    } else if (has_suffix(module_name, ".so")) {
        // 动态库模块：运行时加载
        fprintf(stderr, "Warning: binary module '%s' will be dynamically loaded\n", module_name);
        /* create a dummy module */
        m = JS_NewCModule(ctx, module_name, js_module_dummy_init);
        /* the resulting executable will export its symbols for the
           dynamic library */
        dynamic_export = TRUE;  // 需要导出符号供动态库使用
    } else {
        // JS 或 JSON 模块：编译成字节码
        size_t buf_len;
        uint8_t *buf;
        char cname[1024];
        int res;
        
        // 读取文件内容
        buf = js_load_file(ctx, &buf_len, module_name);
        if (!buf) {
            JS_ThrowReferenceError(ctx, "could not load module filename '%s'",
                                   module_name);
            return NULL;
        }

        // 检测是否为 JSON/JSON5 模块
        res = js_module_test_json(ctx, attributes);
        if (has_suffix(module_name, ".json") || res > 0) {
            /* compile as JSON or JSON5 depending on "type" */
            JSValue val;
            int flags;

            // res == 2 表示 JSON5（扩展 JSON）
            if (res == 2)
                flags = JS_PARSE_JSON_EXT;
            else
                flags = 0;
            
            // 解析 JSON
            val = JS_ParseJSON2(ctx, (char *)buf, buf_len, module_name, flags);
            js_free(ctx, buf);
            if (JS_IsException(val))
                return NULL;
            
            /* create a dummy module */
            m = JS_NewCModule(ctx, module_name, js_module_dummy_init);
            if (!m) {
                JS_FreeValue(ctx, val);
                return NULL;
            }

            // 生成 C 名称
            get_c_name(cname, sizeof(cname), module_name);
            if (namelist_find(&cname_list, cname)) {
                find_unique_cname(cname, sizeof(cname));
            }

            /* output the module name */
            fprintf(outfile, "static const uint8_t %s_module_name[] = {\n",
                    cname);
            dump_hex(outfile, (const uint8_t *)module_name, strlen(module_name) + 1);
            fprintf(outfile, "};\n\n");

            // 输出 JSON 数据字节码
            output_object_code(ctx, outfile, val, cname, CNAME_TYPE_JSON_MODULE);
            JS_FreeValue(ctx, val);
        } else {
            // 普通 JS 模块
            JSValue func_val;

            /* compile the module */
            func_val = JS_Eval(ctx, (char *)buf, buf_len, module_name,
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
            js_free(ctx, buf);
            if (JS_IsException(func_val))
                return NULL;
            
            // 生成 C 名称
            get_c_name(cname, sizeof(cname), module_name);
            if (namelist_find(&cname_list, cname)) {
                find_unique_cname(cname, sizeof(cname));
            }
            
            // 输出模块字节码
            output_object_code(ctx, outfile, func_val, cname, CNAME_TYPE_MODULE);
            
            /* the module is already referenced, so we must free it */
            m = JS_VALUE_GET_PTR(func_val);
            JS_FreeValue(ctx, func_val);
        }
    }
    return m;
}

/**
 * 编译单个文件
 * 
 * 读取 JS 文件，编译成字节码，输出到 C 文件。
 * 自动检测模块类型（.mjs 后缀或 JS_DetectModule）。
 * 
 * @param ctx JS 上下文
 * @param fo 输出文件
 * @param filename 输入文件名
 * @param c_name1 指定的 C 名称（可选）
 * @param module 模块标志（-1=自动检测，0=脚本，1=模块）
 */
static void compile_file(JSContext *ctx, FILE *fo,
                         const char *filename,
                         const char *c_name1,
                         int module)
{
    uint8_t *buf;
    char c_name[1024];
    int eval_flags;
    JSValue obj;
    size_t buf_len;

    // 读取文件
    buf = js_load_file(ctx, &buf_len, filename);
    if (!buf) {
        fprintf(stderr, "Could not load '%s'\n", filename);
        exit(1);
    }
    
    // 只编译不执行
    eval_flags = JS_EVAL_FLAG_COMPILE_ONLY;
    
    // 自动检测模块类型
    if (module < 0) {
        module = (has_suffix(filename, ".mjs") ||
                  JS_DetectModule((const char *)buf, buf_len));
    }
    
    // 设置评估类型
    if (module)
        eval_flags |= JS_EVAL_TYPE_MODULE;  // ES6 模块
    else
        eval_flags |= JS_EVAL_TYPE_GLOBAL;  // 普通脚本
    
    // 编译成字节码
    obj = JS_Eval(ctx, (const char *)buf, buf_len, filename, eval_flags);
    if (JS_IsException(obj)) {
        js_std_dump_error(ctx);
        exit(1);
    }
    js_free(ctx, buf);
    
    // 确定 C 名称
    if (c_name1) {
        pstrcpy(c_name, sizeof(c_name), c_name1);
    } else {
        get_c_name(c_name, sizeof(c_name), filename);
        if (namelist_find(&cname_list, c_name)) {
            find_unique_cname(c_name, sizeof(c_name));
        }
    }
    
    // 输出字节码
    output_object_code(ctx, fo, obj, c_name, CNAME_TYPE_SCRIPT);
    JS_FreeValue(ctx, obj);
}

/* ========== 生成的 main 函数模板 ========== */

/**
 * main 函数模板（第一部分）
 * 包含运行时初始化、事件处理器注册等
 */
static const char main_c_template1[] =
    "int main(int argc, char **argv)\n"
    "{\n"
    "  JSRuntime *rt;\n"
    "  JSContext *ctx;\n"
    "  rt = JS_NewRuntime();\n"
    "  js_std_set_worker_new_context_func(JS_NewCustomContext);\n"
    "  js_std_init_handlers(rt);\n"
    ;

/**
 * main 函数模板（第二部分）
 * 包含事件循环、资源清理等
 */
static const char main_c_template2[] =
    "  js_std_loop(ctx);\n"
    "  js_std_free_handlers(rt);\n"
    "  JS_FreeContext(ctx);\n"
    "  JS_FreeRuntime(rt);\n"
    "  return 0;\n"
    "}\n";

#define PROG_NAME "qjsc"

/**
 * 显示帮助信息
 * 
 * 输出命令行选项说明，然后退出程序。
 */
void help(void)
{
    printf("QuickJS Compiler version " CONFIG_VERSION "\n"
           "usage: " PROG_NAME " [options] [files]\n"
           "\n"
           "options are:\n"
           "-c          only output bytecode to a C file\n"
           "-e          output main() and bytecode to a C file (default = executable output)\n"
           "-o output   set the output filename\n"
           "-N cname    set the C name of the generated data\n"
           "-m          compile as Javascript module (default=autodetect)\n"
           "-D module_name         compile a dynamically loaded module or worker\n"
           "-M module_name[,cname] add initialization code for an external C module\n"
           "-x          byte swapped output\n"
           "-p prefix   set the prefix of the generated C names\n"
           "-S n        set the maximum stack size to 'n' bytes (default=%d)\n"
           "-s            strip all the debug info\n"
           "--keep-source keep the source code\n",
           JS_DEFAULT_STACK_SIZE);
#ifdef CONFIG_LTO
    {
        int i;
        printf("-flto       use link time optimization\n");
        printf("-fno-[");
        for(i = 0; i < countof(feature_list); i++) {
            if (i != 0)
                printf("|");
            printf("%s", feature_list[i].option_name);
        }
        printf("]\n"
               "            disable selected language features (smaller code size)\n");
    }
#endif
    exit(1);
}

#if defined(CONFIG_CC) && !defined(_WIN32)

/**
 * 执行外部命令
 * 
 * 使用 fork/exec 运行编译器（如 gcc）生成可执行文件。
 * 
 * @param argv 命令行参数数组（NULL 结尾）
 * @return 命令退出状态
 */
int exec_cmd(char **argv)
{
    int pid, status, ret;

    pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        exit(1);
    }

    for(;;) {
        ret = waitpid(pid, &status, 0);
        if (ret == pid && WIFEXITED(status))
            break;
    }
    return WEXITSTATUS(status);
}

/**
 * 生成可执行文件
 * 
 * 调用 C 编译器（gcc/clang）将生成的 C 文件与 QuickJS 运行时库链接，
 * 创建最终的可执行文件。
 * 
 * @param out_filename 输出可执行文件名
 * @param cfilename 中间 C 文件名
 * @param use_lto 是否使用链接时优化
 * @param verbose 是否显示详细输出
 * @param exename 编译器可执行文件名
 * @return 0 表示成功
 */
static int output_executable(const char *out_filename, const char *cfilename,
                             BOOL use_lto, BOOL verbose, const char *exename)
{
    const char *argv[64];
    const char **arg, *bn_suffix, *lto_suffix;
    char libjsname[1024];
    char exe_dir[1024], inc_dir[1024], lib_dir[1024], buf[1024], *p;
    int ret;

    /* get the directory of the executable */
    pstrcpy(exe_dir, sizeof(exe_dir), exename);
    p = strrchr(exe_dir, '/');
    if (p) {
        *p = '\0';
    } else {
        pstrcpy(exe_dir, sizeof(exe_dir), ".");
    }

    /* if 'quickjs.h' is present at the same path as the executable, we
       use it as include and lib directory */
    snprintf(buf, sizeof(buf), "%s/quickjs.h", exe_dir);
    if (access(buf, R_OK) == 0) {
        // 使用可执行文件所在目录作为 include/lib 目录
        pstrcpy(inc_dir, sizeof(inc_dir), exe_dir);
        pstrcpy(lib_dir, sizeof(lib_dir), exe_dir);
    } else {
        // 使用系统安装目录
        snprintf(inc_dir, sizeof(inc_dir), "%s/include/quickjs", CONFIG_PREFIX);
        snprintf(lib_dir, sizeof(lib_dir), "%s/lib/quickjs", CONFIG_PREFIX);
    }

    lto_suffix = "";
    bn_suffix = "";

    arg = argv;
    *arg++ = CONFIG_CC;  // 编译器（如 gcc）
    *arg++ = "-O2";      // 优化级别
#ifdef CONFIG_LTO
    if (use_lto) {
        *arg++ = "-flto";
        lto_suffix = ".lto";
    }
#endif
    /* XXX: use the executable path to find the includes files and
       libraries */
    *arg++ = "-D";
    *arg++ = "_GNU_SOURCE";
    *arg++ = "-I";
    *arg++ = inc_dir;
    *arg++ = "-o";
    *arg++ = out_filename;
    if (dynamic_export)
        *arg++ = "-rdynamic";  // 导出符号供动态库使用
    *arg++ = cfilename;
    // 链接 QuickJS 静态库
    snprintf(libjsname, sizeof(libjsname), "%s/libquickjs%s%s.a",
             lib_dir, bn_suffix, lto_suffix);
    *arg++ = libjsname;
    *arg++ = "-lm";       // 数学库
    *arg++ = "-ldl";      // 动态加载库
    *arg++ = "-lpthread"; // 线程库
    *arg = NULL;

    if (verbose) {
        for(arg = argv; *arg != NULL; arg++)
            printf("%s ", *arg);
        printf("\n");
    }

    ret = exec_cmd((char **)argv);
    unlink(cfilename);  // 删除中间 C 文件
    return ret;
}
#else
// 不支持生成可执行文件的平台
static int output_executable(const char *out_filename, const char *cfilename,
                             BOOL use_lto, BOOL verbose, const char *exename)
{
    fprintf(stderr, "Executable output is not supported for this target\n");
    exit(1);
    return 0;
}
#endif

/**
 * 解析带后缀的大小值
 * 
 * 支持后缀：G (GB), M (MB), k/K (KB)
 * 例如："1M" → 1048576, "512k" → 524288
 * 
 * @param str 输入字符串
 * @return 字节数
 */
static size_t get_suffixed_size(const char *str)
{
    char *p;
    size_t v;
    v = (size_t)strtod(str, &p);
    switch(*p) {
    case 'G':
        v <<= 30;  // GB
        break;
    case 'M':
        v <<= 20;  // MB
        break;
    case 'k':
    case 'K':
        v <<= 10;  // KB
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

/**
 * 输出类型枚举
 */
typedef enum {
    OUTPUT_C,           // 仅输出 C 文件（字节码数组）
    OUTPUT_C_MAIN,      // 输出 C 文件 + main 函数
    OUTPUT_EXECUTABLE,  // 输出可执行文件（默认）
} OutputTypeEnum;

/**
 * 获取短选项的参数
 * 
 * 处理 -o output 或 -ooutput 两种形式。
 * 
 * @param poptind 当前参数索引（输入/输出）
 * @param opt 选项字符
 * @param arg 选项后的剩余字符串
 * @param argc 参数总数
 * @param argv 参数数组
 * @return 选项参数值
 */
static const char *get_short_optarg(int *poptind, int opt,
                                    const char *arg, int argc, char **argv)
{
    const char *optarg;
    if (*arg) {
        optarg = arg;  // -ooutput 形式
    } else if (*poptind < argc) {
        optarg = argv[(*poptind)++];  // -o output 形式
    } else {
        fprintf(stderr, "qjsc: expecting parameter for -%c\n", opt);
        exit(1);
    }
    return optarg;
}

/**
 * 主函数：QuickJS 编译器入口
 * 
 * 命令行语法：
 *   qjsc [options] [files]
 * 
 * 主要流程：
 * 1. 解析命令行参数
 * 2. 创建 JS 运行时和上下文
 * 3. 编译所有输入文件
 * 4. 生成 C 代码或可执行文件
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 0 表示成功
 */
int main(int argc, char **argv)
{
    int i, verbose, strip_flags;
    const char *out_filename, *cname;
    char cfilename[1024];
    FILE *fo;
    JSRuntime *rt;
    JSContext *ctx;
    BOOL use_lto;
    int module;
    OutputTypeEnum output_type;
    size_t stack_size;
    namelist_t dynamic_module_list;

    // 初始化默认值
    out_filename = NULL;
    output_type = OUTPUT_EXECUTABLE;  // 默认生成可执行文件
    cname = NULL;
    feature_bitmap = FE_ALL;          // 启用所有特性
    module = -1;                      // 自动检测模块类型
    byte_swap = FALSE;
    verbose = 0;
    strip_flags = JS_STRIP_SOURCE;    // 默认剥离源码信息
    use_lto = FALSE;
    stack_size = 0;
    memset(&dynamic_module_list, 0, sizeof(dynamic_module_list));

    /* add system modules */
    // 添加系统模块（std、os）
    namelist_add(&cmodule_list, "std", "std", 0);
    namelist_add(&cmodule_list, "os", "os", 0);

    optind = 1;
    // ========== 解析命令行参数 ==========
    while (optind < argc && *argv[optind] == '-') {
        char *arg = argv[optind] + 1;
        const char *longopt = "";
        const char *optarg;
        /* a single - is not an option, it also stops argument scanning */
        if (!*arg)
            break;
        optind++;
        if (*arg == '-') {
            longopt = arg + 1;  // 长选项（--option）
            arg += strlen(arg);
            /* -- stops argument scanning */
            if (!*longopt)
                break;
        }
        for (; *arg || *longopt; longopt = "") {
            char opt = *arg;
            if (opt)
                arg++;
            
            // 帮助选项
            if (opt == 'h' || opt == '?' || !strcmp(longopt, "help")) {
                help();
                continue;
            }
            // 输出文件名
            if (opt == 'o') {
                out_filename = get_short_optarg(&optind, opt, arg, argc, argv);
                break;
            }
            // 仅输出 C 文件（不生成 main）
            if (opt == 'c') {
                output_type = OUTPUT_C;
                continue;
            }
            // 输出 C 文件 + main 函数
            if (opt == 'e') {
                output_type = OUTPUT_C_MAIN;
                continue;
            }
            // 指定 C 名称
            if (opt == 'N') {
                cname = get_short_optarg(&optind, opt, arg, argc, argv);
                break;
            }
            // 特性控制（-flto, -fno-date 等）
            if (opt == 'f') {
                const char *p;
                optarg = get_short_optarg(&optind, opt, arg, argc, argv);
                p = optarg;
                if (!strcmp(p, "lto")) {
                    use_lto = TRUE;  // 启用链接时优化
                } else if (strstart(p, "no-", &p)) {
                    // 禁用特定特性
                    use_lto = TRUE;
                    for(i = 0; i < countof(feature_list); i++) {
                        if (!strcmp(p, feature_list[i].option_name)) {
                            feature_bitmap &= ~((uint64_t)1 << i);
                            break;
                        }
                    }
                    if (i == countof(feature_list))
                        goto bad_feature;
                } else {
                bad_feature:
                    fprintf(stderr, "unsupported feature: %s\n", optarg);
                    exit(1);
                }
                break;
            }
            // 强制模块模式
            if (opt == 'm') {
                module = 1;
                continue;
            }
            // 添加外部 C 模块
            if (opt == 'M') {
                char *p;
                char path[1024];
                char cname[1024];

                optarg = get_short_optarg(&optind, opt, arg, argc, argv);
                pstrcpy(path, sizeof(path), optarg);
                p = strchr(path, ',');
                if (p) {
                    *p = '\0';
                    pstrcpy(cname, sizeof(cname), p + 1);
                } else {
                    get_c_name(cname, sizeof(cname), path);
                }
                namelist_add(&cmodule_list, path, cname, 0);
                break;
            }
            // 动态模块/worker
            if (opt == 'D') {
                optarg = get_short_optarg(&optind, opt, arg, argc, argv);
                namelist_add(&dynamic_module_list, optarg, NULL, 0);
                break;
            }
            // 字节交换输出
            if (opt == 'x') {
                byte_swap = 1;
                continue;
            }
            // 详细输出
            if (opt == 'v') {
                verbose++;
                continue;
            }
            // C 名称前缀
            if (opt == 'p') {
                c_ident_prefix = get_short_optarg(&optind, opt, arg, argc, argv);
                break;
            }
            // 栈大小
            if (opt == 'S') {
                optarg = get_short_optarg(&optind, opt, arg, argc, argv);
                stack_size = get_suffixed_size(optarg);
                break;
            }
            // 剥离调试信息
            if (opt == 's') {
                strip_flags = JS_STRIP_DEBUG;
                continue;
            }
            // 保留源码
            if (!strcmp(longopt, "keep-source")) {
                strip_flags = 0;
                continue;
            }
            // 未知选项
            if (opt) {
                fprintf(stderr, "qjsc: unknown option '-%c'\n", opt);
            } else {
                fprintf(stderr, "qjsc: unknown option '--%s'\n", longopt);
            }
            help();
        }
    }

    if (optind >= argc)
        help();  // 没有输入文件

    // 设置默认输出文件名
    if (!out_filename) {
        if (output_type == OUTPUT_EXECUTABLE) {
            out_filename = "a.out";
        } else {
            out_filename = "out.c";
        }
    }

    // 确定中间 C 文件名
    if (output_type == OUTPUT_EXECUTABLE) {
#if defined(_WIN32) || defined(__ANDROID__)
        /* XXX: find a /tmp directory ? */
        snprintf(cfilename, sizeof(cfilename), "out%d.c", getpid());
#else
        snprintf(cfilename, sizeof(cfilename), "/tmp/out%d.c", getpid());
#endif
    } else {
        pstrcpy(cfilename, sizeof(cfilename), out_filename);
    }

    // 打开输出文件
    fo = fopen(cfilename, "w");
    if (!fo) {
        perror(cfilename);
        exit(1);
    }
    outfile = fo;

    // 创建 JS 运行时和上下文
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);

    // 设置源码剥离级别
    JS_SetStripInfo(rt, strip_flags);

    /* loader for ES6 modules */
    // 设置模块加载器
    JS_SetModuleLoaderFunc2(rt, NULL, jsc_module_loader, NULL, NULL);

    // 输出文件头
    fprintf(fo, "/* File generated automatically by the QuickJS compiler. */\n"
            "\n"
            );

    // 输出头文件包含
    if (output_type != OUTPUT_C) {
        fprintf(fo, "#include \"quickjs-libc.h\"\n"
                "\n"
                );
    } else {
        fprintf(fo, "#include <inttypes.h>\n"
                "\n"
                );
    }

    // ========== 编译所有输入文件 ==========
    for(i = optind; i < argc; i++) {
        const char *filename = argv[i];
        compile_file(ctx, fo, filename, cname, module);
        cname = NULL;  // 只第一个文件使用指定的 cname
    }

    // 加载动态模块
    for(i = 0; i < dynamic_module_list.count; i++) {
        if (!jsc_module_loader(ctx, dynamic_module_list.array[i].name, NULL, JS_UNDEFINED)) {
            fprintf(stderr, "Could not load dynamic module '%s'\n",
                    dynamic_module_list.array[i].name);
            exit(1);
        }
    }

    // ========== 生成 main 函数和初始化代码 ==========
    if (output_type != OUTPUT_C) {
        // 生成 JS_NewCustomContext 函数
        fprintf(fo,
                "static JSContext *JS_NewCustomContext(JSRuntime *rt)\n"
                "{\n"
                "  JSContext *ctx = JS_NewContextRaw(rt);\n"
                "  if (!ctx)\n"
                "    return NULL;\n");
        
        /* add the basic objects */
        // 添加基础对象
        fprintf(fo, "  JS_AddIntrinsicBaseObjects(ctx);\n");
        
        // 根据特性位图添加可选对象
        for(i = 0; i < countof(feature_list); i++) {
            if ((feature_bitmap & ((uint64_t)1 << i)) &&
                feature_list[i].init_name) {
                fprintf(fo, "  JS_AddIntrinsic%s(ctx);\n",
                        feature_list[i].init_name);
            }
        }
        
        /* add the precompiled modules (XXX: could modify the module
           loader instead) */
        // 添加静态 C 模块初始化代码
        for(i = 0; i < init_module_list.count; i++) {
            namelist_entry_t *e = &init_module_list.array[i];
            /* initialize the static C modules */

            fprintf(fo,
                    "  {\n"
                    "    extern JSModuleDef *js_init_module_%s(JSContext *ctx, const char *name);\n"
                    "    js_init_module_%s(ctx, \"%s\");\n"
                    "  }\n",
                    e->short_name, e->short_name, e->name);
        }
        
        // 添加预编译模块的加载代码
        for(i = 0; i < cname_list.count; i++) {
            namelist_entry_t *e = &cname_list.array[i];
            if (e->flags == CNAME_TYPE_MODULE) {
                fprintf(fo, "  js_std_eval_binary(ctx, %s, %s_size, 1);\n",
                        e->name, e->name);
            } else if (e->flags == CNAME_TYPE_JSON_MODULE) {
                fprintf(fo, "  js_std_eval_binary_json_module(ctx, %s, %s_size, (const char *)%s_module_name);\n",
                        e->name, e->name, e->name);
            }
        }
        fprintf(fo,
                "  return ctx;\n"
                "}\n\n");

        // 输出 main 函数模板第一部分
        fputs(main_c_template1, fo);

        // 设置栈大小（如果指定）
        if (stack_size != 0) {
            fprintf(fo, "  JS_SetMaxStackSize(rt, %u);\n",
                    (unsigned int)stack_size);
        }

        /* add the module loader if necessary */
        // 添加模块加载器（如果启用了 module-loader 特性）
        if (feature_bitmap & (1 << FE_MODULE_LOADER)) {
            fprintf(fo, "  JS_SetModuleLoaderFunc2(rt, NULL, js_module_loader, js_module_check_attributes, NULL);\n");
        }

        // 创建自定义上下文并添加辅助函数
        fprintf(fo,
                "  ctx = JS_NewCustomContext(rt);\n"
                "  js_std_add_helpers(ctx, argc, argv);\n");

        // 添加脚本执行代码
        for(i = 0; i < cname_list.count; i++) {
            namelist_entry_t *e = &cname_list.array[i];
            if (e->flags == CNAME_TYPE_SCRIPT) {
                fprintf(fo, "  js_std_eval_binary(ctx, %s, %s_size, 0);\n",
                        e->name, e->name);
            }
        }
        
        // 输出 main 函数模板第二部分（事件循环和清理）
        fputs(main_c_template2, fo);
    }

    // 清理 JS 资源
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    fclose(fo);

    // 如果是可执行文件模式，调用编译器生成最终可执行文件
    if (output_type == OUTPUT_EXECUTABLE) {
        return output_executable(out_filename, cfilename, use_lto, verbose,
                                 argv[0]);
    }
    
    // 释放名称列表
    namelist_free(&cname_list);
    namelist_free(&cmodule_list);
    namelist_free(&init_module_list);
    return 0;
}
