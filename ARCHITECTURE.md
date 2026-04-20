# QuickJS 架构文档

## 概述

QuickJS 是一个轻量级、可嵌入的 JavaScript 引擎，设计目标是：
- **小巧**：核心引擎代码精简，易于理解和嵌入
- **快速**：使用字节码解释器，启动速度快
- **完整**：支持 ES2020 大部分特性，包括模块、异步函数、Proxy 等
- **可嵌入**：提供简洁的 C API，易于集成到其他项目

## 核心架构

### 1. 运行时层次结构

```
JSRuntime (运行时)
    └── JSContext (上下文)
        ├── JSValue (值)
        ├── JSObject (对象)
        └── JSFunction (函数)
```

#### JSRuntime
- 全局运行时实例，每个进程通常创建一个
- 管理内存分配、垃圾回收、原子表
- 可配置内存限制、GC 阈值、栈大小

#### JSContext
- 执行上下文，对应一个 JS 环境
- 包含全局对象、作用域链、异常状态
- 一个 Runtime 可以有多个 Context

#### JSValue
- JavaScript 值的表示
- 使用标签联合（tagged union）区分类型
- 支持 NaN-boxing 优化（32 位系统）

### 2. 值表示系统

QuickJS 使用标签值（tagged value）系统：

```c
// 标签定义
JS_TAG_FIRST       = -9  // 第一个负标签
JS_TAG_BIG_INT     = -9  // 大整数
JS_TAG_SYMBOL      = -8  // 符号
JS_TAG_STRING      = -7  // 字符串
JS_TAG_OBJECT      = -1  // 对象
JS_TAG_INT         = 0   // 整数
JS_TAG_BOOL        = 1   // 布尔
JS_TAG_NULL        = 2   // null
JS_TAG_UNDEFINED   = 3   // undefined
JS_TAG_FLOAT64     = 8   // 浮点数
```

**值存储策略：**
- 小整数、布尔值：直接存储在标签值中（立即数）
- 对象、字符串：存储指针，带引用计数
- 浮点数：根据平台使用 NaN-boxing 或直接存储

### 3. 内存管理

#### 引用计数 + 垃圾回收
- 大部分对象使用引用计数
- 循环引用通过标记 - 清除 GC 处理
- 支持自定义内存分配器

#### 内存统计
```c
typedef struct JSMemoryUsage {
    int64_t malloc_size, malloc_limit, memory_used_size;
    int64_t atom_count, atom_size;
    int64_t str_count, str_size;
    int64_t obj_count, obj_size;
    int64_t prop_count, prop_size;
    // ... 更多统计项
} JSMemoryUsage;
```

### 4. 原子（Atom）系统

Atom 是字符串的内部表示，用于：
- 属性名
- 变量名
- 字符串常量

**特点：**
- 唯一化：相同字符串共享同一个 Atom
- 快速比较：使用整数 ID 而非字符串比较
- 引用计数：自动管理生命周期

### 5. 字节码解释器

#### 编译流程
```
JavaScript 源码 → 词法分析 → 语法分析 → AST → 字节码生成
```

#### 字节码特点
- 基于栈的执行模型
- 紧凑的编码格式
- 支持异常处理、try-catch
- 支持 async/await、generator

#### 主要操作码类别
- **栈操作**：push、pop、dup、swap
- **算术运算**：add、sub、mul、div
- **类型转换**：to_bool、to_number、to_string
- **对象操作**：get_field、put_field、invoke
- **控制流**：jump、if_false、loop
- **函数调用**：call、construct、return

### 6. 对象系统

#### 对象结构
```c
typedef struct JSObject {
    JSClass *class;      // 类定义
    JSProperty *prop;    // 属性数组
    uint32_t prop_count; // 属性数量
    JSValue proto;       // 原型
    // ... 其他字段
} JSObject;
```

#### 属性描述符
- configurable: 是否可配置
- writable: 是否可写
- enumerable: 是否可枚举
- value/getter/setter: 值或访问器

#### 特殊对象类型
- **Array**：快速数组优化
- **TypedArray**：类型化数组
- **Promise**：异步操作
- **Proxy**：元编程支持
- **Map/Set**：集合类型

### 7. 函数与作用域

#### 函数类型
- **普通函数**：JavaScript 函数
- **C 函数**：原生绑定
- **Constructor**：构造函数
- **Async Function**：异步函数
- **Generator**：生成器函数

#### 作用域链
- 词法作用域
- 闭包支持
- eval 作用域处理

### 8. 模块系统

#### ES6 模块支持
- import/export 语法
- 动态 import()
- 模块缓存
- 循环依赖处理

#### 模块加载流程
```
解析模块 → 加载依赖 → 链接 → 执行
```

### 9. 异步编程

#### Promise 实现
- 微任务队列
- then/catch/finally
- async/await 语法糖

#### Job 队列
- 微任务（microtask）
- 宏任务（macrotask）
- 事件循环集成

### 10. 正则表达式

#### libregexp 模块
- 独立的正则引擎
- 支持 Unicode
- JIT 编译优化（可选）

#### 正则编译流程
```
正则字符串 → 解析 → NFA → DFA/字节码 → 执行
```

### 11. Unicode 支持

#### libunicode 模块
- Unicode 字符属性
- 字符串规范化
- 大小写转换
- 正则表达式 Unicode 支持

## 核心 API

### 初始化和清理
```c
JSRuntime *JS_NewRuntime(void);
void JS_FreeRuntime(JSRuntime *rt);
JSContext *JS_NewContext(JSRuntime *rt);
void JS_FreeContext(JSContext *ctx);
```

### 代码执行
```c
JSValue JS_Eval(JSContext *ctx, const char *input, size_t input_len,
                const char *filename, int eval_flags);
JSValue JS_EvalFunction(JSContext *ctx, JSValue fun);
```

### 值操作
```c
JSValue JS_NewInt32(JSContext *ctx, int32_t val);
JSValue JS_NewFloat64(JSContext *ctx, double d);
JSValue JS_NewString(JSContext *ctx, const char *str);
JSValue JS_NewObject(JSContext *ctx);
void JS_FreeValue(JSContext *ctx, JSValue v);
```

### 属性操作
```c
int JS_SetProperty(JSContext *ctx, JSValueConst this_obj,
                   JSAtom prop, JSValue val);
JSValue JS_GetProperty(JSContext *ctx, JSValueConst this_obj,
                       JSAtom prop);
```

### C 函数绑定
```c
typedef JSValue JSCFunction(JSContext *ctx, JSValueConst this_val,
                            int argc, JSValueConst *argv);

JSValue JS_NewCFunction(JSContext *ctx, JSCFunction *func,
                        const char *name, int length);
```

## 文件组织

```
quickjs/
├── quickjs.h          # 公共 API 头文件
├── quickjs.c          # 核心引擎实现
├── quickjs-libc.c     # 标准库绑定
├── quickjs-atom.h     # 原子表定义
├── quickjs-opcode.h   # 字节码操作码定义
├── libregexp.c        # 正则表达式引擎
├── libunicode.c       # Unicode 支持
├── cutils.c/h         # C 工具函数
├── dtoa.c/h           # 浮点数转换
├── qjs.c              # CLI 解释器
├── qjsc.c             # 字节码编译器
├── repl.js            # REPL 实现
├── examples/          # 示例代码
├── tests/             # 测试用例
└── doc/               # 文档
```

## 性能优化

### 1. 快速路径
- 小整数直接使用标签值
- 快速数组（fast array）优化
- 内联缓存（inline cache）

### 2. 内存优化
- 字符串驻留（string interning）
- 形状（shape）共享
- 延迟初始化

### 3. 执行优化
- 字节码紧凑编码
- 操作码预测分支
- 内置函数 C 实现

## 安全特性

### 1. 资源限制
- 内存限制
- 栈大小限制
- GC 阈值控制

### 2. 异常处理
- 捕获所有异常
- 未捕获异常处理
- 严格模式支持

### 3. 隔离
- 多上下文隔离
- 全局状态不共享
- 模块作用域隔离

## 扩展机制

### 1. 自定义类
```c
typedef struct JSClassDef {
    const char *class_name;
    JSClassFinalizer *finalizer;
    JSClassGCMark *gc_mark;
    JSClassCall *call;
    JSClassExoticMethods *exotic;
} JSClassDef;
```

### 2. 自定义内存分配器
```c
typedef struct JSMallocFunctions {
    void *(*js_malloc)(JSMallocState *s, size_t size);
    void (*js_free)(JSMallocState *s, void *ptr);
    void *(*js_realloc)(JSMallocState *s, void *ptr, size_t size);
    size_t (*js_malloc_usable_size)(const void *ptr);
} JSMallocFunctions;
```

### 3. Host 绑定
- 文件系统
- 网络
- 定时器
- 其他系统 API

## 设计哲学

1. **简单优先**：代码可读性优于性能
2. **轻量级**：最小化依赖，易于嵌入
3. **标准兼容**：遵循 ECMAScript 规范
4. **实用主义**：在完整性和简洁性之间平衡

## 参考资料

- [ECMAScript 规范](https://tc39.es/ecma262/)
- [QuickJS 官方文档](https://bellard.org/quickjs/)
- [JavaScript 引擎架构对比](https://github.com/davidbau/braindump/blob/master/jit.md)
