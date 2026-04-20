# QuickJS 工作原理详解

## 1. JavaScript 引擎基础

### 1.1 引擎的核心职责

JavaScript 引擎的核心任务是将人类可读的 JavaScript 代码转换为机器可执行的指令：

```
源码 → 词法分析 → 语法分析 → AST → 字节码 → 执行
```

### 1.2 为什么选择字节码解释器？

QuickJS 选择字节码解释器而非 JIT 编译器的原因：

| 特性 | 解释器 | JIT 编译器 |
|------|--------|-----------|
| 启动速度 | 快（无需编译） | 慢（需要预热） |
| 内存占用 | 低 | 高（需要存储机器码） |
| 代码大小 | 小 | 大 |
| 峰值性能 | 中等 | 高 |
| 实现复杂度 | 低 | 高 |

QuickJS 的设计目标是**轻量级和快速启动**，因此选择了解释器架构。

## 2. 词法分析（Lexer）

### 2.1 任务

将字符流转换为 Token 流：

```javascript
// 源码
let x = 42 + "hello";

// Token 流
[LET, IDENT(x), EQ, NUM(42), PLUS, STR("hello"), SEMI]
```

### 2.2 关键挑战

**1. 上下文相关的词法分析**
```javascript
// 正则表达式 vs 除法
a / b / c;        // 除法
a / b/g;          // 正则表达式
```

QuickJS 通过跟踪语法上下文来解决这个问题。

**2. 模板字符串解析**
```javascript
`Hello ${name}!`  // 需要处理插值
```

**3. Unicode 标识符**
```javascript
let 你好 = "world";  // Unicode 变量名
```

### 2.3 实现位置

主要在 `quickjs.c` 中的词法分析函数处理。

## 3. 语法分析（Parser）

### 3.1 递归下降解析

QuickJS 使用递归下降解析器，为每种语法结构编写对应的解析函数：

```c
static JSFunctionDef *parse_function(JSContext *ctx, ...) {
    // 解析函数体
}

static JSValue parse_expression(JSContext *ctx, ...) {
    // 解析表达式
}

static JSValue parse_statement(JSContext *ctx, ...) {
    // 解析语句
}
```

### 3.2 AST 表示

解析器生成抽象语法树（AST），QuickJS 在解析过程中直接生成字节码，不显式构建 AST。

### 3.3 关键语法特性

**1. 表达式解析**
- 运算符优先级处理
- 三元运算符
- 箭头函数

**2. 语句解析**
- 块级作用域
- 循环语句
- 条件语句
- try-catch-finally

**3. 声明解析**
- var/let/const
- 函数声明
- 类声明
- import/export

## 4. 字节码生成

### 4.1 字节码格式

QuickJS 字节码是变长编码：

```
[操作码 1 字节] [操作数 0-4 字节] ...
```

### 4.2 示例

```javascript
// JavaScript
let x = 1 + 2;

// 字节码（伪代码）
push_1          // 压入 1
push_2          // 压入 2
add             // 相加
store_var(x)    // 存储到变量 x
```

### 4.3 操作码分类

**栈操作码**
- `OP_push`: 压栈
- `OP_pop`: 弹栈
- `OP_dup`: 复制栈顶
- `OP_swap`: 交换栈顶两元素

**算术操作码**
- `OP_add`, `OP_sub`, `OP_mul`, `OP_div`
- `OP_mod`, `OP_pow`
- `OP_neg`, `OP_not`, `OP_complement`

**类型转换**
- `OP_to_bool`: 转布尔
- `OP_to_number`: 转数字
- `OP_to_string`: 转字符串
- `OP_to_object`: 转对象

**对象操作**
- `OP_get_field`: 获取属性
- `OP_put_field`: 设置属性
- `OP_invoke`: 方法调用
- `OP_new`: 创建对象

**控制流**
- `OP_jump`: 无条件跳转
- `OP_if_false`: 条件跳转
- `OP_loop`: 循环
- `OP_return`: 返回

**函数调用**
- `OP_call`: 函数调用
- `OP_construct`: 构造函数调用
- `OP_yield`: 生成器 yield
- `OP_await`: async await

### 4.4 字节码生成示例

```javascript
// JavaScript
function add(a, b) {
    return a + b;
}

// 字节码生成过程
1. 创建函数作用域
2. 分配参数 a, b
3. 生成 a + b 的字节码
4. 生成 return 字节码
5. 设置函数元数据（参数数量、变量数量等）
```

## 5. 字节码解释执行

### 5.1 解释器主循环

```c
JSValue js_interpreter(JSContext *ctx, JSFunctionDef *fd) {
    uint8_t *pc = fd->bytecode;
    JSValue *stack = ctx->stack;
    int sp = 0;  // 栈指针
    
    for (;;) {
        uint8_t opcode = *pc++;
        
        switch (opcode) {
            case OP_push:
                stack[sp++] = read_operand(pc);
                break;
            case OP_add:
                stack[sp-2] = add_values(stack[sp-2], stack[sp-1]);
                sp--;
                break;
            case OP_return:
                return stack[--sp];
            // ... 其他操作码
        }
    }
}
```

### 5.2 执行上下文

每个函数调用创建一个执行上下文：

```c
typedef struct JSFunctionDef {
    uint8_t *bytecode;      // 字节码
    uint32_t bytecode_len;  // 字节码长度
    
    JSAtom *var_names;      // 变量名表
    uint32_t var_count;     // 变量数量
    
    JSAtom *arg_names;      // 参数名表
    uint32_t arg_count;     // 参数数量
    
    // ... 其他元数据
} JSFunctionDef;
```

### 5.3 异常处理

QuickJS 使用异常表处理 try-catch：

```c
typedef struct JSCatchBlock {
    uint32_t start;     // try 块起始位置
    uint32_t end;       // try 块结束位置
    uint32_t handler;   // catch 块位置
    JSAtom exception;   // 异常变量
} JSCatchBlock;
```

当异常发生时：
1. 查找当前 PC 所在的异常表项
2. 跳转到 catch 块
3. 设置异常变量
4. 继续执行

## 6. 垃圾回收

### 6.1 引用计数

大多数对象使用引用计数：

```c
typedef struct JSRefCountHeader {
    int ref_count;
} JSRefCountHeader;

// 增加引用
JSValue JS_DupValue(JSContext *ctx, JSValueConst v) {
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        p->ref_count++;
    }
    return v;
}

// 减少引用
void JS_FreeValue(JSContext *ctx, JSValue v) {
    if (JS_VALUE_HAS_REF_COUNT(v)) {
        JSRefCountHeader *p = (JSRefCountHeader *)JS_VALUE_GET_PTR(v);
        if (--p->ref_count <= 0) {
            __JS_FreeValue(ctx, v);  // 释放对象
        }
    }
}
```

### 6.2 循环引用处理

引用计数无法处理循环引用，QuickJS 使用标记 - 清除 GC：

```
1. 从根对象开始标记所有可达对象
2. 扫描所有对象，释放未标记的对象
3. 清除标记，准备下次 GC
```

### 6.3 GC 触发条件

- 内存使用超过阈值
- 显式调用 `JS_RunGC()`
- 分配失败时

## 7. 对象模型

### 7.1 对象内部结构

```c
typedef struct JSObject {
    JSClass *class;           // 类信息
    JSProperty *prop;         // 属性数组
    uint32_t prop_count;      // 属性数量
    uint32_t prop_size;       // 属性数组大小
    JSValue proto;            // 原型
    uint32_t extensible:1;    // 是否可扩展
    // ... 其他标志位
} JSObject;
```

### 7.2 属性存储

**普通属性**
```c
typedef struct JSProperty {
    JSAtom atom;      // 属性名
    JSValue value;    // 属性值
    int flags;        // 属性标志
} JSProperty;
```

**访问器属性**
```c
typedef struct JSPropertyDescriptor {
    int flags;
    JSValue value;
    JSValue getter;
    JSValue setter;
} JSPropertyDescriptor;
```

### 7.3 原型链查找

```c
JSValue JS_GetPropertyInternal(JSContext *ctx, JSValueConst obj,
                               JSAtom prop, JSValueConst receiver,
                               JS_BOOL throw_ref_error) {
    JSObject *p = JS_VALUE_GET_OBJ(obj);
    
    // 1. 在对象自身查找
    for (uint32_t i = 0; i < p->prop_count; i++) {
        if (p->prop[i].atom == prop) {
            return p->prop[i].value;
        }
    }
    
    // 2. 在原型链查找
    if (!JS_IsNull(p->proto)) {
        return JS_GetPropertyInternal(ctx, p->proto, prop, receiver, throw_ref_error);
    }
    
    return JS_UNDEFINED;
}
```

## 8. 函数调用机制

### 8.1 调用类型

**普通调用**
```javascript
func(arg1, arg2)
```

**构造调用**
```javascript
new Constructor(arg1, arg2)
```

**方法调用**
```javascript
obj.method(arg1, arg2)
```

**Apply/Call 调用**
```javascript
func.call(thisArg, arg1, arg2)
```

### 8.2 调用过程

```c
JSValue JS_Call(JSContext *ctx, JSValueConst func_obj,
                JSValueConst this_obj, int argc, JSValueConst *argv) {
    JSObject *p = JS_VALUE_GET_OBJ(func_obj);
    
    if (p->class->call) {
        // C 函数
        return p->class->call(ctx, func_obj, this_obj, argc, argv, 0);
    } else {
        // JavaScript 函数 - 字节码执行
        return js_interpreter(ctx, p->u.func);
    }
}
```

### 8.3 参数处理

- 参数不足：补 undefined
- 参数过多：忽略多余参数
- 严格模式：检查参数重复

## 9. 异步编程实现

### 9.1 Promise 状态

```c
typedef enum JSPromiseState {
    JS_PROMISE_PENDING,
    JS_PROMISE_FULFILLED,
    JS_PROMISE_REJECTED,
} JSPromiseState;
```

### 9.2 Promise 结构

```c
typedef struct JSPromise {
    JSPromiseState state;
    JSValue result;           // 结果或异常
    JSValue reactions;        // then/catch 回调队列
} JSPromise;
```

### 9.3 微任务队列

```c
typedef struct JSRuntime {
    JSValue *job_queue;
    int job_count;
    int job_size;
} JSRuntime;

// 执行微任务
int JS_ExecutePendingJob(JSRuntime *rt, JSContext **pctx) {
    if (rt->job_count > 0) {
        JSValue job = rt->job_queue[0];
        // 移除并执行
        // ...
    }
    return 0;
}
```

### 9.4 Async/Await 实现

```javascript
// JavaScript
async function foo() {
    let x = await bar();
    return x;
}

// 编译为
function foo() {
    return Promise.resolve().then(() => {
        return bar().then(x => {
            return x;
        });
    });
}
```

## 10. 模块系统

### 10.1 模块解析

```
import { a } from './mod.js';
         ↓
1. 解析模块路径
2. 加载模块源码
3. 编译为模块字节码
4. 链接依赖
5. 执行模块
```

### 10.2 模块缓存

```c
typedef struct JSModuleDef {
    char *module_name;        // 模块名
    JSValue func_obj;         // 模块函数
    JSValue ns_obj;           // 命名空间对象
    int resolve_status;       // 解析状态
    int instantiate_status;   // 实例化状态
} JSModuleDef;
```

### 10.3 循环依赖处理

```javascript
// a.js
import { b } from './b.js';
export let a = 'a';

// b.js
import { a } from './a.js';  // 此时 a 还未初始化
export let b = 'b';
```

QuickJS 使用两阶段加载：
1. **解析阶段**：加载所有模块，建立依赖图
2. **执行阶段**：按拓扑序执行

## 11. 正则表达式引擎

### 11.1 编译流程

```
正则字符串 → 解析 → NFA → 字节码
```

### 11.2 匹配算法

使用回溯算法：

```c
int re_exec(REContext *ctx, const uint8_t *input, int input_len) {
    // 尝试匹配
    if (match_failed) {
        // 回溯
        goto previous_choice_point;
    }
    return success;
}
```

### 11.3 优化技术

- 字符类编译为位图
- 贪婪匹配优化
- 锚点快速失败

## 12. Unicode 支持

### 12.1 字符串编码

QuickJS 内部使用 CESU-8 编码（类似 UTF-8）：

```c
typedef struct JSString {
    int ref_count;
    uint32_t hash;
    uint32_t len;
    uint8_t flags;
    uint8_t data[0];  // CESU-8 编码
} JSString;
```

### 12.2 Unicode 操作

- 字符属性查询
- 大小写转换
- 规范化（NFC/NFD/NFKC/NFKD）
- 正则表达式 Unicode 支持

## 13. 性能优化技术

### 13.1 快速数组

对于密集数组，使用连续存储：

```c
typedef struct JSArray {
    JSValue *elements;
    uint32_t length;
    uint32_t size;
} JSArray;
```

### 13.2 字符串驻留

相同字符串共享存储：

```c
JSAtom JS_NewAtom(JSContext *ctx, const char *str) {
    // 查找原子表
    JSAtom atom = atom_table_lookup(str);
    if (atom) {
        return JS_DupAtom(ctx, atom);
    }
    // 创建新原子
    atom = atom_table_insert(str);
    return atom;
}
```

### 13.3 内联缓存

属性访问缓存：

```c
typedef struct {
    JSAtom prop;
    JSValue proto;
    int offset;
} InlineCache;

JSValue get_property_cached(JSContext *ctx, JSValue obj, JSAtom prop) {
    InlineCache *ic = lookup_cache(prop);
    if (ic->proto == obj->proto) {
        // 缓存命中
        return obj->prop[ic->offset];
    }
    // 缓存未命中，慢速路径
    return get_property_slow(ctx, obj, prop);
}
```

## 14. 安全与沙箱

### 14.1 资源限制

```c
JS_SetMemoryLimit(rt, 1024 * 1024);  // 1MB 限制
JS_SetMaxStackSize(rt, 256 * 1024);  // 256KB 栈限制
JS_SetGCThreshold(rt, 256 * 1024);   // GC 阈值
```

### 14.2 超时控制

```c
JS_SetInterruptHandler(rt, interrupt_handler, opaque);

// 在解释器循环中检查
for (;;) {
    if (JS_IsInterrupted(rt)) {
        return JS_ThrowInterruptError(ctx);
    }
    // ... 执行字节码
}
```

## 总结

QuickJS 的设计体现了以下原则：

1. **简洁性**：代码清晰，易于理解和维护
2. **实用性**：在性能和复杂度之间取得平衡
3. **标准兼容**：遵循 ECMAScript 规范
4. **可嵌入性**：提供简洁的 C API

通过理解这些原理，你可以更好地使用 QuickJS 或参考其设计实现自己的语言解释器。
