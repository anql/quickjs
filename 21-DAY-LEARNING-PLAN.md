# QuickJS 21 天学习计划

## 学习目标

通过 21 天的系统学习，你将：
- 深入理解 JavaScript 引擎的工作原理
- 掌握 QuickJS 的架构设计和核心算法
- 能够阅读和修改 QuickJS 源码
- 具备使用 QuickJS 进行嵌入式开发的能力
- 能够扩展 QuickJS 添加自定义功能

## 学习前提

### 必备知识
- ✅ 熟练掌握 C 语言（指针、内存管理、数据结构）
- ✅ 熟悉 JavaScript 语法和 ES6+ 特性
- ✅ 了解基本的编译原理（词法分析、语法分析）
- ✅ 理解操作系统基础（内存、进程、线程）

### 推荐准备
- 📖 阅读《编译原理》（龙书）相关章节
- 📖 复习 JavaScript 语言规范（ECMAScript 2020）
- 🔧 搭建 C 语言开发环境（gcc/clang, gdb, valgrind）

## 学习资源

### 核心资料
- 📁 QuickJS 源码：`/Users/anql/Documents/JDAI/quickjs/`
- 📄 架构文档：`ARCHITECTURE.md`
- 📄 原理文档：`PRINCIPLES.md`
- 📄 注释头文件：`quickjs-annotated.h`

### 参考资料
- [ECMAScript 规范](https://tc39.es/ecma262/)
- [QuickJS 官方文档](https://bellard.org/quickjs/)
- [JavaScript 引擎工作原理](https://github.com/davidbau/braindump/blob/master/jit.md)

---

## 第一周：基础入门（第 1-7 天）

### 第 1 天：环境搭建与初识 QuickJS

**学习目标**
- 成功编译 QuickJS
- 运行示例程序
- 了解项目结构

**学习内容**
```bash
# 1. 编译 QuickJS
cd /Users/anql/Documents/JDAI/quickjs
make

# 2. 运行 REPL
./qjs

# 3. 运行测试
make test

# 4. 查看示例
ls examples/
./qjs examples/hello.js
```

**阅读材料**
- `readme.txt` - 项目简介
- `Makefile` - 构建系统
- `VERSION` - 版本信息

**实践任务**
1. 编译 QuickJS 并运行 REPL
2. 在 REPL 中执行简单的 JavaScript 代码
3. 运行至少 3 个示例程序

**时间分配**：2-3 小时

---

### 第 2 天：JSValue - JavaScript 值的表示

**学习目标**
- 理解标签值（tagged value）系统
- 掌握 JSValue 的三种实现策略
- 能够创建和操作基本类型

**学习内容**

**核心概念**
```c
// JSValue 标签系统
JS_TAG_INT         = 0   // 整数
JS_TAG_BOOL        = 1   // 布尔
JS_TAG_NULL        = 2   // null
JS_TAG_UNDEFINED   = 3   // undefined
JS_TAG_OBJECT      = -1  // 对象
JS_TAG_STRING      = -7  // 字符串
JS_TAG_FLOAT64     = 8   // 浮点数
```

**阅读代码**
- `quickjs.h` (1-300 行) - 值类型定义
- `quickjs.h` (600-750 行) - 值操作 API

**实践任务**
```c
// 创建测试程序 test_value.c
#include "quickjs.h"
#include <stdio.h>

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    // 创建各种类型的值
    JSValue intVal = JS_NewInt32(ctx, 42);
    JSValue floatVal = JS_NewFloat64(ctx, 3.14);
    JSValue boolVal = JS_NewBool(ctx, JS_TRUE);
    JSValue strVal = JS_NewString(ctx, "Hello");
    
    // 类型检查
    printf("Is int: %d\n", JS_IsNumber(intVal));
    printf("Is string: %d\n", JS_IsString(strVal));
    
    // 释放资源
    JS_FreeValue(ctx, intVal);
    JS_FreeValue(ctx, floatVal);
    JS_FreeValue(ctx, boolVal);
    JS_FreeValue(ctx, strVal);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**思考题**
1. 为什么 QuickJS 使用标签值而不是联合体？
2. NaN-boxing 优化的原理是什么？
3. 引用计数如何工作？

**时间分配**：3-4 小时

---

### 第 3 天：Runtime 和 Context - 运行时架构

**学习目标**
- 理解 Runtime 和 Context 的关系
- 掌握内存管理和 GC 配置
- 学会创建和销毁运行时

**学习内容**

**核心概念**
```
JSRuntime (全局运行时)
    └── JSContext (执行上下文)
        ├── 全局对象
        ├── 作用域链
        └── 异常状态
```

**阅读代码**
- `quickjs.h` (350-500 行) - Runtime/Context API
- `quickjs.c` 搜索 `JS_NewRuntime` 实现

**实践任务**
```c
// test_runtime.c
#include "quickjs.h"
#include <stdio.h>

int main() {
    // 1. 创建运行时
    JSRuntime *rt = JS_NewRuntime();
    
    // 2. 配置内存限制
    JS_SetMemoryLimit(rt, 1024 * 1024);  // 1MB
    JS_SetGCThreshold(rt, 256 * 1024);   // 256KB 触发 GC
    
    // 3. 创建上下文
    JSContext *ctx = JS_NewContext(rt);
    
    // 4. 执行代码
    const char *code = "let x = 10; x * 2;";
    JSValue result = JS_Eval(ctx, code, strlen(code), 
                             "<input>", JS_EVAL_TYPE_GLOBAL);
    
    // 5. 查看内存使用
    JSMemoryUsage usage;
    JS_ComputeMemoryUsage(rt, &usage);
    printf("Memory used: %ld bytes\n", usage.memory_used_size);
    
    // 6. 手动触发 GC
    JS_RunGC(rt);
    
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**思考题**
1. Runtime 和 Context 是一对多关系吗？
2. 什么时候应该创建多个 Context？
3. GC 阈值如何影响性能？

**时间分配**：3-4 小时

---

### 第 4 天：代码执行 - Eval 和编译

**学习目标**
- 掌握 JS_Eval 的使用
- 理解编译和执行流程
- 学会错误处理

**学习内容**

**Eval 标志**
```c
JS_EVAL_TYPE_GLOBAL    // 全局代码
JS_EVAL_TYPE_MODULE    // 模块代码
JS_EVAL_FLAG_STRICT    // 严格模式
JS_EVAL_FLAG_COMPILE_ONLY  // 仅编译不执行
JS_EVAL_FLAG_ASYNC     // 允许顶层 await
```

**阅读代码**
- `quickjs.h` 搜索 `JS_Eval`
- `quickjs.c` 搜索 `js_eval` 实现

**实践任务**
```c
// test_eval.c
#include "quickjs.h"
#include <stdio.h>

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    // 1. 执行全局代码
    JSValue r1 = JS_Eval(ctx, "let x = 10; x + 5;", 17, 
                         "test.js", JS_EVAL_TYPE_GLOBAL);
    
    // 2. 执行严格模式代码
    JSValue r2 = JS_Eval(ctx, "'use strict'; let y = 20;", 27,
                         "test.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_STRICT);
    
    // 3. 仅编译不执行
    JSValue r3 = JS_Eval(ctx, "function add(a,b){return a+b;}", 30,
                         "test.js", JS_EVAL_FLAG_COMPILE_ONLY);
    
    // 4. 错误处理
    JSValue r4 = JS_Eval(ctx, "throw new Error('test');", 24,
                         "test.js", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(r4)) {
        JSValue exc = JS_GetException(ctx);
        // 处理异常
        printf("Exception caught!\n");
        JS_FreeValue(ctx, exc);
    }
    
    // 清理
    JS_FreeValue(ctx, r1);
    JS_FreeValue(ctx, r2);
    JS_FreeValue(ctx, r3);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**思考题**
1. 全局代码和模块代码有什么区别？
2. 编译仅模式有什么用途？
3. 如何正确处理异常？

**时间分配**：3-4 小时

---

### 第 5 天：对象和属性操作

**学习目标**
- 掌握对象的创建和操作
- 理解属性描述符
- 学会原型链操作

**学习内容**

**核心 API**
```c
JS_NewObject(ctx)              // 创建对象
JS_SetProperty(ctx, obj, atom, val)  // 设置属性
JS_GetProperty(ctx, obj, atom)       // 获取属性
JS_DefineProperty(ctx, obj, atom, val, getter, setter, flags)
```

**阅读代码**
- `quickjs.h` 搜索 `JS_SetProperty`, `JS_GetProperty`
- `quickjs.c` 搜索对象相关实现

**实践任务**
```c
// test_object.c
#include "quickjs.h"
#include <stdio.h>

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    // 1. 创建对象
    JSValue obj = JS_NewObject(ctx);
    
    // 2. 设置属性
    JSAtom nameAtom = JS_NewAtom(ctx, "name");
    JSValue nameVal = JS_NewString(ctx, "Alice");
    JS_SetProperty(ctx, obj, nameAtom, nameVal);
    
    JSAtom ageAtom = JS_NewAtom(ctx, "age");
    JSValue ageVal = JS_NewInt32(ctx, 25);
    JS_SetProperty(ctx, obj, ageAtom, ageVal);
    
    // 3. 获取属性
    JSAtom getNameAtom = JS_NewAtom(ctx, "name");
    JSValue getName = JS_GetProperty(ctx, obj, getNameAtom);
    
    // 4. 枚举属性
    JSPropertyEnum *props;
    uint32_t count;
    JS_GetOwnPropertyNames(ctx, &props, &count, obj, 
                          JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY);
    
    for (uint32_t i = 0; i < count; i++) {
        const char *propName = JS_AtomToCString(ctx, props[i].atom);
        printf("Property: %s\n", propName);
        JS_FreeAtom(ctx, props[i].atom);
    }
    
    // 5. 设置原型
    JSValue proto = JS_NewObject(ctx);
    JS_SetPrototype(ctx, obj, proto);
    
    // 清理
    JS_FreeValue(ctx, obj);
    JS_FreeValue(ctx, proto);
    JS_FreeValue(ctx, getName);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**思考题**
1. 属性描述符有哪些标志位？
2. 原型链查找的过程是怎样的？
3. 访问器属性如何实现？

**时间分配**：3-4 小时

---

### 第 6 天：函数和 C 绑定

**学习目标**
- 理解 JavaScript 函数内部表示
- 掌握 C 函数绑定
- 学会创建自定义函数

**学习内容**

**C 函数签名**
```c
typedef JSValue JSCFunction(JSContext *ctx, JSValueConst this_val, 
                            int argc, JSValueConst *argv);

// 带 magic 值的函数
typedef JSValue JSCFunctionMagic(..., int magic);

// 带函数数据的函数
typedef JSValue JSCFunctionData(..., JSValue *func_data);
```

**阅读代码**
- `quickjs.h` 搜索 `JSCFunction`
- `quickjs-libc.c` 查看标准库绑定示例

**实践任务**
```c
// test_function.c
#include "quickjs.h"
#include <stdio.h>
#include <string.h>

// 1. 简单 C 函数
static JSValue js_add(JSContext *ctx, JSValueConst this_val,
                      int argc, JSValueConst *argv) {
    if (argc < 2) return JS_ThrowTypeError(ctx, "Need 2 arguments");
    
    int32_t a, b;
    if (JS_ToInt32(ctx, &a, argv[0]) || JS_ToInt32(ctx, &b, argv[1]))
        return JS_EXCEPTION;
    
    return JS_NewInt32(ctx, a + b);
}

// 2. 带 magic 值的函数
static JSValue js_math_func(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv, int magic) {
    double a;
    if (JS_ToFloat64(ctx, &a, argv[0]))
        return JS_EXCEPTION;
    
    double result;
    switch (magic) {
        case 0: result = sqrt(a); break;   // Math.sqrt
        case 1: result = sin(a); break;    // Math.sin
        case 2: result = cos(a); break;    // Math.cos
        default: return JS_ThrowInternalError(ctx, "Invalid magic");
    }
    
    return JS_NewFloat64(ctx, result);
}

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    // 1. 注册简单函数
    JSValue global = JS_GetGlobalObject(ctx);
    JSAtom addAtom = JS_NewAtom(ctx, "add");
    JSValue addFunc = JS_NewCFunction(ctx, js_add, "add", 2);
    JS_SetProperty(ctx, global, addAtom, addFunc);
    
    // 2. 注册 magic 函数
    JSAtom mathAtom = JS_NewAtom(ctx, "Math2");
    JSValue mathObj = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, mathObj, "sqrt",
        JS_NewCFunctionMagic(ctx, js_math_func, "sqrt", 1, 0));
    JS_SetPropertyStr(ctx, mathObj, "sin",
        JS_NewCFunctionMagic(ctx, js_math_func, "sin", 1, 1));
    
    JS_SetProperty(ctx, global, mathAtom, mathObj);
    
    // 3. 测试调用
    JSValue result = JS_Eval(ctx, "add(3, 5)", 10, 
                             "test", JS_EVAL_TYPE_GLOBAL);
    
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**思考题**
1. C 函数如何处理 JavaScript 参数？
2. magic 值有什么用途？
3. 如何处理 C 函数的异常？

**时间分配**：4-5 小时

---

### 第 7 天：第一周复习与实践

**学习目标**
- 巩固第一周所学知识
- 完成综合实践项目
- 解决遗留问题

**复习内容**
- ✅ JSValue 标签系统
- ✅ Runtime/Context 架构
- ✅ 代码执行流程
- ✅ 对象和属性操作
- ✅ C 函数绑定

**实践项目：创建简易计算器模块**

```c
// calculator.c - 综合练习
#include "quickjs.h"
#include <stdio.h>
#include <math.h>

// 实现以下功能：
// 1. add(a, b) - 加法
// 2. sub(a, b) - 减法
// 3. mul(a, b) - 乘法
// 4. div(a, b) - 除法
// 5. pow(a, b) - 幂运算
// 6. sqrt(a) - 平方根
// 7. sin(a), cos(a), tan(a) - 三角函数

static JSValue calc_add(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv) {
    // TODO: 实现
}

// ... 其他函数

// 初始化模块
static const JSCFunctionListEntry calculator_funcs[] = {
    JS_CFUNC_DEF("add", 2, calc_add),
    JS_CFUNC_DEF("sub", 2, calc_sub),
    // ... 其他函数
    JS_PROP_STRING_DEF("name", "Calculator", 0),
};

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    // 1. 注册计算器模块
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue calcObj = JS_NewObject(ctx);
    
    JS_SetPropertyFunctionList(ctx, calcObj, calculator_funcs, 
                               countof(calculator_funcs));
    
    JS_SetPropertyStr(ctx, global, "Calculator", calcObj);
    
    // 2. 测试
    const char *testCode = 
        "console.log('Calculator Test:');\n"
        "console.log('add(2, 3) =', Calculator.add(2, 3));\n"
        "console.log('pow(2, 10) =', Calculator.pow(2, 10));\n";
    
    JS_Eval(ctx, testCode, strlen(testCode), "test.js", 
            JS_EVAL_TYPE_GLOBAL);
    
    // 清理
    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**本周总结**
- 写学习日记：记录遇到的问题和解决方案
- 整理笔记：形成自己的知识体系
- 提出问题：标记不理解的概念

**时间分配**：4-6 小时

---

## 第二周：深入核心（第 8-14 天）

### 第 8 天：词法分析（Lexer）

**学习目标**
- 理解词法分析原理
- 掌握 Token 生成过程
- 了解上下文相关的词法分析

**学习内容**

**词法分析流程**
```
字符流 → 跳过空白 → 识别 Token → 输出 Token 流

示例：
"let x = 42 + 'hello';"
→ [LET] [IDENT:x] [EQ] [NUM:42] [PLUS] [STR:hello] [SEMI]
```

**阅读代码**
- `quickjs.c` 搜索词法分析相关函数
- 关注 `next_token`, `parse_string` 等函数

**实践任务**
1. 阅读词法分析代码，画出流程图
2. 理解正则表达式和除法的歧义处理
3. 尝试添加自定义 Token 类型

**思考题**
1. 如何区分 `/` 是除法还是正则表达式？
2. 模板字符串如何词法分析？
3. Unicode 标识符如何处理？

**时间分配**：4-5 小时

---

### 第 9 天：语法分析（Parser）

**学习目标**
- 理解递归下降解析
- 掌握表达式解析
- 了解语句解析

**学习内容**

**解析器结构**
```c
parse_function()
    └── parse_statement()
        ├── parse_if_statement()
        ├── parse_while_statement()
        └── parse_expression()
            ├── parse_assignment()
                └── parse_binary_op()
                    └── parse_unary_op()
                        └── parse_primary()
```

**阅读代码**
- `quickjs.c` 搜索 `parse_` 开头的函数
- 关注表达式优先级处理

**实践任务**
1. 画出表达式解析的调用树
2. 理解运算符优先级处理
3. 尝试添加自定义运算符

**思考题**
1. 递归下降解析的优缺点？
2. 如何处理左递归？
3. 箭头函数解析的特殊性？

**时间分配**：4-5 小时

---

### 第 10 天：字节码生成

**学习目标**
- 理解字节码格式
- 掌握操作码分类
- 学会阅读字节码

**学习内容**

**字节码示例**
```javascript
// JavaScript
let x = 1 + 2 * 3;

// 字节码（伪代码）
push_2          // 压入 2
push_3          // 压入 3
mul             // 乘法
push_1          // 压入 1
add             // 加法
store_var(x)    // 存储到 x
```

**操作码分类**
- 栈操作：push, pop, dup, swap
- 算术：add, sub, mul, div
- 控制流：jump, if_false, loop
- 对象：get_field, put_field, invoke

**阅读代码**
- `quickjs-opcode.h` - 操作码定义
- `quickjs.c` 搜索字节码生成函数

**实践任务**
1. 阅读 `quickjs-opcode.h`，分类所有操作码
2. 编写简单程序，观察生成的字节码
3. 尝试手动编写字节码

**思考题**
1. 为什么使用基于栈的字节码？
2. 字节码如何支持异常处理？
3. 闭包的字节码表示？

**时间分配**：4-5 小时

---

### 第 11 天：字节码解释执行

**学习目标**
- 理解解释器主循环
- 掌握执行上下文
- 了解异常处理机制

**学习内容**

**解释器主循环**
```c
JSValue js_interpreter(JSContext *ctx, JSFunctionDef *fd) {
    uint8_t *pc = fd->bytecode;
    JSValue *stack = ctx->stack;
    int sp = 0;
    
    for (;;) {
        uint8_t opcode = *pc++;
        switch (opcode) {
            case OP_push: /* ... */ break;
            case OP_add:  /* ... */ break;
            case OP_return: return stack[--sp];
            // ...
        }
    }
}
```

**阅读代码**
- `quickjs.c` 搜索 `js_interpreter` 或类似函数
- 关注异常处理代码

**实践任务**
1. 画出解释器执行流程图
2. 理解栈帧结构
3. 跟踪一个函数调用的完整过程

**思考题**
1. 解释器如何支持递归调用？
2. try-catch-finally 如何实现？
3. 生成器函数如何暂停和恢复？

**时间分配**：4-5 小时

---

### 第 12 天：垃圾回收

**学习目标**
- 理解引用计数原理
- 掌握循环引用处理
- 了解 GC 优化技术

**学习内容**

**引用计数**
```c
// 增加引用
JSValue JS_DupValue(JSContext *ctx, JSValueConst v) {
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        p->ref_count++;
    }
    return (JSValue)v;
}

// 减少引用
void JS_FreeValue(JSContext *ctx, JSValue v) {
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        if (--p->ref_count <= 0) {
            __JS_FreeValue(ctx, v);  // 真正释放
        }
    }
}
```

**GC 流程**
```
1. 从根对象开始标记
2. 遍历所有可达对象
3. 释放未标记对象
4. 清除标记
```

**阅读代码**
- `quickjs.c` 搜索 `JS_RunGC`, `__JS_FreeValue`
- 关注 GC 标记函数

**实践任务**
1. 创建循环引用测试用例
2. 观察 GC 触发条件
3. 使用 valgrind 检测内存泄漏

**思考题**
1. 引用计数的优缺点？
2. 如何检测循环引用？
3. GC 停顿如何优化？

**时间分配**：4-5 小时

---

### 第 13 天：Promise 和异步编程

**学习目标**
- 理解 Promise 内部实现
- 掌握微任务队列
- 了解 async/await 编译

**学习内容**

**Promise 状态**
```c
typedef enum JSPromiseState {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED,
} JSPromiseState;
```

**Async/Await 编译**
```javascript
// 源代码
async function foo() {
    let x = await bar();
    return x;
}

// 编译后（概念）
function foo() {
    return Promise.resolve().then(() => {
        return bar().then(x => {
            return x;
        });
    });
}
```

**阅读代码**
- `quickjs.c` 搜索 Promise 相关实现
- 关注微任务队列处理

**实践任务**
1. 创建 Promise 测试用例
2. 观察微任务执行顺序
3. 实现简单的 async/await 包装

**思考题**
1. Promise 链如何实现？
2. 微任务和宏任务的区别？
3. async/await 的错误处理？

**时间分配**：4-5 小时

---

### 第 14 天：第二周复习与实践

**学习目标**
- 巩固第二周所学知识
- 完成综合实践项目
- 深入理解引擎核心

**复习内容**
- ✅ 词法分析
- ✅ 语法分析
- ✅ 字节码生成
- ✅ 解释执行
- ✅ 垃圾回收
- ✅ 异步编程

**实践项目：实现自定义模块系统**

```c
// module_system.c
#include "quickjs.h"
#include <stdio.h>
#include <string.h>

// 实现一个简单的模块加载器
// 支持：
// 1. require('./module.js')
// 2. module.exports = { ... }
// 3. 模块缓存

typedef struct {
    char *name;
    JSValue exports;
    int loaded;
} Module;

// 模块缓存
Module module_cache[100];
int module_count = 0;

// require 函数实现
static JSValue js_require(JSContext *ctx, JSValueConst this_val,
                          int argc, JSValueConst *argv) {
    // TODO: 实现模块加载逻辑
}

// 初始化模块系统
void init_module_system(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "require",
        JS_NewCFunction(ctx, js_require, "require", 1));
    JS_FreeValue(ctx, global);
}

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    init_module_system(ctx);
    
    // 测试
    const char *testCode = 
        "const mod = require('./test.js');\n"
        "console.log(mod);\n";
    
    JS_Eval(ctx, testCode, strlen(testCode), "main.js",
            JS_EVAL_TYPE_GLOBAL);
    
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

**本周总结**
- 整理核心算法笔记
- 绘制引擎架构图
- 记录未解决问题

**时间分配**：4-6 小时

---

## 第三周：高级主题（第 15-21 天）

### 第 15 天：模块系统（ES6 Modules）

**学习目标**
- 理解 ES6 模块语法
- 掌握模块加载流程
- 了解循环依赖处理

**学习内容**

**模块加载流程**
```
1. 解析模块路径
2. 加载模块源码
3. 编译为模块字节码
4. 链接依赖
5. 执行模块
```

**阅读代码**
- `quickjs.c` 搜索模块相关实现
- 关注 `JS_LoadModule`, `JS_ResolveModule`

**实践任务**
1. 创建多模块项目
2. 测试循环依赖
3. 实现自定义模块解析器

**思考题**
1. ES6 模块和 CommonJS 的区别？
2. 循环依赖如何解决？
3. 动态 import() 如何实现？

**时间分配**：4-5 小时

---

### 第 16 天：Proxy 和元编程

**学习目标**
- 理解 Proxy 内部实现
- 掌握陷阱（trap）机制
- 了解元编程应用

**学习内容**

**Proxy 陷阱**
```javascript
const handler = {
    get(target, prop, receiver) { /* ... */ },
    set(target, prop, value, receiver) { /* ... */ },
    has(target, prop) { /* ... */ },
    // ... 其他陷阱
};

const proxy = new Proxy(target, handler);
```

**阅读代码**
- `quickjs.c` 搜索 Proxy 相关实现
- 关注 `JSClassExoticMethods`

**实践任务**
1. 实现访问日志 Proxy
2. 实现数据验证 Proxy
3. 实现虚拟属性 Proxy

**思考题**
1. Proxy 的性能开销？
2. Proxy 和 Reflect 的关系？
3. Proxy 的安全考虑？

**时间分配**：4-5 小时

---

### 第 17 天：正则表达式引擎

**学习目标**
- 理解正则编译流程
- 掌握匹配算法
- 了解优化技术

**学习内容**

**正则编译流程**
```
正则字符串 → 解析 → NFA → 字节码 → 执行
```

**阅读代码**
- `libregexp.c` - 正则引擎实现
- `libregexp-opcode.h` - 正则操作码

**实践任务**
1. 分析正则编译过程
2. 测试各种正则模式
3. 性能基准测试

**思考题**
1. NFA 和 DFA 的区别？
2. 回溯算法的复杂度？
3. 如何防止 ReDoS 攻击？

**时间分配**：4-5 小时

---

### 第 18 天：Unicode 支持

**学习目标**
- 理解 Unicode 编码
- 掌握字符串处理
- 了解国际化支持

**学习内容**

**字符串编码**
- CESU-8（类似 UTF-8）
- Unicode 码点
- 代理对（surrogate pair）

**阅读代码**
- `libunicode.c` - Unicode 支持
- `libunicode-table.h` - Unicode 数据表

**实践任务**
1. 测试 Unicode 字符串操作
2. 实现 Unicode 规范化
3. 测试正则表达式 Unicode 支持

**思考题**
1. CESU-8 和 UTF-8 的区别？
2. 字符串长度如何计算？
3. 大小写转换的复杂性？

**时间分配**：4-5 小时

---

### 第 19 天：性能优化

**学习目标**
- 了解性能瓶颈
- 掌握优化技术
- 学会性能分析

**学习内容**

**优化技术**
- 快速数组（fast array）
- 字符串驻留（string interning）
- 内联缓存（inline cache）
- 形状（shape）共享

**阅读代码**
- `quickjs.c` 搜索优化相关代码
- 关注快速路径实现

**实践任务**
1. 编写性能基准测试
2. 使用 perf/vtune 分析
3. 尝试优化热点代码

**思考题**
1. 解释器和 JIT 的性能差距？
2. 内存和速度的权衡？
3. 缓存友好的数据结构？

**时间分配**：4-5 小时

---

### 第 20 天：安全与沙箱

**学习目标**
- 理解安全限制
- 掌握资源控制
- 了解沙箱技术

**学习内容**

**安全机制**
- 内存限制
- 栈大小限制
- 超时控制
- 中断处理

**阅读代码**
- `quickjs.c` 搜索安全相关代码
- 关注 `JS_SetInterruptHandler`

**实践任务**
1. 测试内存限制
2. 实现超时控制
3. 创建安全沙箱

**思考题**
1. 如何防止 DoS 攻击？
2. 沙箱逃逸的可能性？
3. 安全审计要点？

**时间分配**：4-5 小时

---

### 第 21 天：综合项目与总结

**学习目标**
- 完成综合项目
- 总结学习成果
- 规划后续学习

**综合项目：实现嵌入式脚本引擎**

```c
// embedded_engine.c
// 目标：创建一个完整的嵌入式脚本引擎
// 功能要求：
// 1. 加载和执行脚本
// 2. C/JS 双向调用
// 3. 模块系统
// 4. 错误处理
// 5. 资源限制
// 6. 日志系统

#include "quickjs.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    JSRuntime *rt;
    JSContext *ctx;
    int initialized;
} ScriptEngine;

// 引擎初始化
ScriptEngine* engine_create() {
    ScriptEngine *eng = malloc(sizeof(ScriptEngine));
    eng->rt = JS_NewRuntime();
    eng->ctx = JS_NewContext(eng->rt);
    
    // 配置限制
    JS_SetMemoryLimit(eng->rt, 2 * 1024 * 1024);
    JS_SetMaxStackSize(eng->rt, 256 * 1024);
    
    // 注册内置模块
    // TODO: 添加自定义 API
    
    eng->initialized = 1;
    return eng;
}

// 执行脚本
JSValue engine_eval(ScriptEngine *eng, const char *code, 
                    const char *filename) {
    return JS_Eval(eng->ctx, code, strlen(code), filename,
                   JS_EVAL_TYPE_GLOBAL);
}

// 调用 JS 函数
JSValue engine_call(ScriptEngine *eng, const char *func_name,
                    int argc, JSValueConst *argv) {
    // TODO: 实现
}

// 引擎销毁
void engine_destroy(ScriptEngine *eng) {
    JS_FreeContext(eng->ctx);
    JS_FreeRuntime(eng->rt);
    free(eng);
}

int main() {
    ScriptEngine *eng = engine_create();
    
    // 测试
    const char *testCode = 
        "function greet(name) {\n"
        "    return 'Hello, ' + name + '!';\n"
        "}\n"
        "greet('World');\n";
    
    JSValue result = engine_eval(eng, testCode, "test.js");
    
    // 处理结果
    // ...
    
    engine_destroy(eng);
    return 0;
}
```

**21 天总结**

**知识体系**
```
QuickJS 知识树
├── 基础（第 1 周）
│   ├── JSValue 表示
│   ├── Runtime/Context
│   ├── 代码执行
│   ├── 对象系统
│   └── C 绑定
├── 核心（第 2 周）
│   ├── 词法分析
│   ├── 语法分析
│   ├── 字节码
│   ├── 解释器
│   ├── GC
│   └── 异步
└── 高级（第 3 周）
    ├── 模块系统
    ├── Proxy
    ├── 正则
    ├── Unicode
    ├── 优化
    └── 安全
```

**后续学习方向**
1. 🔬 深入研究 JIT 编译技术
2. 🔧 扩展 QuickJS 添加自定义功能
3. 📦 使用 QuickJS 开发实际项目
4. 📖 阅读其他 JS 引擎源码（V8, SpiderMonkey）
5. 🎯 参与 QuickJS 社区贡献

**时间分配**：6-8 小时

---

## 附录

### A. 调试技巧

**GDB 调试**
```bash
# 编译调试版本
make clean
make CONFIG_DEBUG=1

# GDB 调试
gdb ./qjs
(gdb) break js_interpreter
(gdb) run script.js
```

**打印调试**
```c
// 在关键位置添加打印
printf("DEBUG: pc=%p, opcode=%d\n", pc, opcode);
fflush(stdout);
```

**内存调试**
```bash
# Valgrind 检测内存泄漏
valgrind --leak-check=full ./qjs script.js

# AddressSanitizer
make CONFIG_ASAN=1
./qjs script.js
```

### B. 常见问题

**Q1: 段错误（Segmentation Fault）**
- 检查指针是否为 NULL
- 检查数组边界
- 使用 valgrind 定位

**Q2: 内存泄漏**
- 确保 JS_FreeValue 配对调用
- 检查循环引用
- 使用 JS_ComputeMemoryUsage 监控

**Q3: 异常处理**
- 始终检查 JS_IsException
- 使用 JS_GetException 获取异常
- 清理异常避免内存泄漏

### C. 参考代码

**完整示例：最小嵌入**
```c
#include "quickjs.h"
#include <stdio.h>

int main(int argc, char **argv) {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    
    const char *code = "console.log('Hello from QuickJS!');";
    JSValue result = JS_Eval(ctx, code, strlen(code), 
                             "<input>", JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(ctx);
        // 处理异常
        JS_FreeValue(ctx, exc);
    }
    
    JS_FreeValue(ctx, result);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    
    return 0;
}
```

### D. 学习检查清单

**第一周检查**
- [ ] 能编译和运行 QuickJS
- [ ] 理解 JSValue 标签系统
- [ ] 会创建 Runtime 和 Context
- [ ] 能执行 JavaScript 代码
- [ ] 会操作对象和属性
- [ ] 能绑定 C 函数

**第二周检查**
- [ ] 理解词法分析流程
- [ ] 理解语法分析流程
- [ ] 能阅读字节码
- [ ] 理解解释器工作原理
- [ ] 理解 GC 机制
- [ ] 理解 Promise 实现

**第三周检查**
- [ ] 理解模块系统
- [ ] 理解 Proxy 机制
- [ ] 了解正则引擎
- [ ] 了解 Unicode 支持
- [ ] 了解优化技术
- [ ] 了解安全机制
- [ ] 能完成综合项目

---

## 结语

恭喜你完成 21 天的 QuickJS 学习之旅！🎉

记住：
- **实践胜于理论**：多写代码，多调试
- **循序渐进**：不要急于求成，理解每个概念
- **保持好奇**：深入源码，探索未知
- **分享知识**：写博客，做贡献，教别人

祝你在 JavaScript 引擎的世界里探索愉快！
