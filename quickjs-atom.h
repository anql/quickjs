/*
 * QuickJS atom definitions
 * QuickJS 原子定义
 * 
 * 本文件定义了 QuickJS 引擎中所有预定义的"原子"（atom）。
 * 原子是字符串的唯一标识符，用于：
 * - JavaScript 关键字（if, else, function 等）
 * - 内置对象属性名（length, prototype, constructor 等）
 * - 内置类名（Object, Array, Function 等）
 * - 内置 Symbol（Symbol.iterator, Symbol.match 等）
 * 
 * 原子通过数值索引访问，比直接使用字符串更高效。
 * 前 38 个原子被视为关键字，在解析器中有特殊处理。
 *
 * Copyright (c) 2017-2018 Fabrice Bellard
 * Copyright (c) 2017-2018 Charlie Gordon
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

/* ==================== JavaScript 关键字（解析器特殊处理） ==================== */
/* 注意：前 38 个原子被视为关键字，在解析器中有特殊含义 */

/**
 * null - 空值关键字
 * 必须是第一个原子，原子索引为 0
 */
DEF(null, "null") /* must be first */

/** false - 布尔假值 */
DEF(false, "false")

/** true - 布尔真值 */
DEF(true, "true")

/** if - 条件语句 */
DEF(if, "if")

/** else - 条件语句分支 */
DEF(else, "else")

/** return - 函数返回 */
DEF(return, "return")

/** var - 变量声明 */
DEF(var, "var")

/** this - 当前上下文对象 */
DEF(this, "this")

/** delete - 删除对象属性 */
DEF(delete, "delete")

/** void - 返回 undefined */
DEF(void, "void")

/** typeof - 类型检查操作符 */
DEF(typeof, "typeof")

/** new - 构造函数调用 */
DEF(new, "new")

/** in - 属性存在性检查 */
DEF(in, "in")

/** instanceof - 原型链检查 */
DEF(instanceof, "instanceof")

/** do - do-while 循环 */
DEF(do, "do")

/** while - while 循环 */
DEF(while, "while")

/** for - for 循环 */
DEF(for, "for")

/** break - 跳出循环/switch */
DEF(break, "break")

/** continue - 继续下一次循环 */
DEF(continue, "continue")

/** switch - 多分支选择 */
DEF(switch, "switch")

/** case - switch 分支 */
DEF(case, "case")

/** default - switch 默认分支 */
DEF(default, "default")

/** throw - 抛出异常 */
DEF(throw, "throw")

/** try - 异常捕获 */
DEF(try, "try")

/** catch - 异常处理 */
DEF(catch, "catch")

/** finally - 最终执行块 */
DEF(finally, "finally")

/** function - 函数定义 */
DEF(function, "function")

/** debugger - 调试器断点 */
DEF(debugger, "debugger")

/** with - 作用域扩展（已废弃） */
DEF(with, "with")

/* ==================== 未来保留字（FutureReservedWord） ==================== */

/** class - 类定义（ES6） */
DEF(class, "class")

/** const - 常量声明 */
DEF(const, "const")

/** enum - 枚举（保留但未实现） */
DEF(enum, "enum")

/** export - 模块导出 */
DEF(export, "export")

/** extends - 类继承 */
DEF(extends, "extends")

/** import - 模块导入 */
DEF(import, "import")

/** super - 父类引用 */
DEF(super, "super")

/* ==================== 严格模式下的未来保留字 ==================== */

/** implements - 接口实现（严格模式保留） */
DEF(implements, "implements")

/** interface - 接口定义（严格模式保留） */
DEF(interface, "interface")

/** let - 块级变量声明（严格模式保留） */
DEF(let, "let")

/** package - 包（严格模式保留） */
DEF(package, "package")

/** private - 私有成员（严格模式保留） */
DEF(private, "private")

/** protected - 保护成员（严格模式保留） */
DEF(protected, "protected")

/** public - 公共成员（严格模式保留） */
DEF(public, "public")

/** static - 静态成员 */
DEF(static, "static")

/** yield - 生成器产出值 */
DEF(yield, "yield")

/** await - 异步等待 */
DEF(await, "await")

/* ==================== 基础标识符 ==================== */

/** 空字符串 - 特殊用途 */
DEF(empty_string, "")

/** keys - 键集合 */
DEF(keys, "keys")

/** size - 大小（Map/Set 等） */
DEF(size, "size")

/** length - 长度（数组/字符串） */
DEF(length, "length")

/** fileName - 文件名（错误信息） */
DEF(fileName, "fileName")

/** lineNumber - 行号（错误信息） */
DEF(lineNumber, "lineNumber")

/** columnNumber - 列号（错误信息） */
DEF(columnNumber, "columnNumber")

/** message - 错误消息 */
DEF(message, "message")

/** cause - 错误原因（ES2022） */
DEF(cause, "cause")

/** errors - 错误集合（AggregateError） */
DEF(errors, "errors")

/** stack - 调用栈 */
DEF(stack, "stack")

/** name - 名称 */
DEF(name, "name")

/** toString - 对象转字符串 */
DEF(toString, "toString")

/** toLocaleString - 本地化转字符串 */
DEF(toLocaleString, "toLocaleString")

/** valueOf - 对象转原始值 */
DEF(valueOf, "valueOf")

/** eval - eval 函数 */
DEF(eval, "eval")

/** prototype - 原型对象 */
DEF(prototype, "prototype")

/** constructor - 构造函数 */
DEF(constructor, "constructor")

/** configurable - 属性描述符：可配置 */
DEF(configurable, "configurable")

/** writable - 属性描述符：可写 */
DEF(writable, "writable")

/** enumerable - 属性描述符：可枚举 */
DEF(enumerable, "enumerable")

/** value - 属性描述符：值 */
DEF(value, "value")

/** get - 获取器函数 */
DEF(get, "get")

/** set - 设置器函数 */
DEF(set, "set")

/** of - 用于 Array.of 等 */
DEF(of, "of")

/** __proto__ - 原型访问（已废弃但支持） */
DEF(__proto__, "__proto__")

/** undefined - 未定义值 */
DEF(undefined, "undefined")

/** number - 数字类型 */
DEF(number, "number")

/** boolean - 布尔类型 */
DEF(boolean, "boolean")

/** string - 字符串类型 */
DEF(string, "string")

/** object - 对象类型 */
DEF(object, "object")

/** symbol - Symbol 类型 */
DEF(symbol, "symbol")

/** integer - 整数（内部使用） */
DEF(integer, "integer")

/** unknown - 未知类型 */
DEF(unknown, "unknown")

/** arguments - 函数参数对象 */
DEF(arguments, "arguments")

/** callee - 当前函数（arguments.callee，已废弃） */
DEF(callee, "callee")

/** caller - 调用者（function.caller，已废弃） */
DEF(caller, "caller")

/** <eval> - eval 作用域标记 */
DEF(_eval_, "<eval>")

/** <ret> - 返回值标记 */
DEF(_ret_, "<ret>")

/** <var> - 变量标记 */
DEF(_var_, "<var>")

/** <arg_var> - 参数变量标记 */
DEF(_arg_var_, "<arg_var>")

/** <with> - with 作用域标记 */
DEF(_with_, "<with>")

/** lastIndex - 正则表达式最后匹配位置 */
DEF(lastIndex, "lastIndex")

/** target - 目标对象（Reflect/Proxy） */
DEF(target, "target")

/** index - 索引位置 */
DEF(index, "index")

/** input - 输入字符串（正则匹配） */
DEF(input, "input")

/** defineProperties - Object.defineProperties */
DEF(defineProperties, "defineProperties")

/** apply - Function.prototype.apply */
DEF(apply, "apply")

/** join - 数组连接 */
DEF(join, "join")

/** concat - 连接操作 */
DEF(concat, "concat")

/** split - 分割操作 */
DEF(split, "split")

/** construct - Proxy construct 陷阱 */
DEF(construct, "construct")

/** getPrototypeOf - 获取原型 */
DEF(getPrototypeOf, "getPrototypeOf")

/** setPrototypeOf - 设置原型 */
DEF(setPrototypeOf, "setPrototypeOf")

/** isExtensible - 检查是否可扩展 */
DEF(isExtensible, "isExtensible")

/** preventExtensions - 阻止扩展 */
DEF(preventExtensions, "preventExtensions")

/** has - Proxy has 陷阱 */
DEF(has, "has")

/** deleteProperty - Proxy deleteProperty 陷阱 */
DEF(deleteProperty, "deleteProperty")

/** defineProperty - Proxy defineProperty 陷阱 */
DEF(defineProperty, "defineProperty")

/** getOwnPropertyDescriptor - 获取属性描述符 */
DEF(getOwnPropertyDescriptor, "getOwnPropertyDescriptor")

/** ownKeys - 获取自有属性键 */
DEF(ownKeys, "ownKeys")

/** add - 添加操作（Set 等） */
DEF(add, "add")

/** done - 迭代完成标记 */
DEF(done, "done")

/** next - 迭代器下一步 */
DEF(next, "next")

/** values - 值集合 */
DEF(values, "values")

/** source - 正则表达式源码 */
DEF(source, "source")

/** flags - 正则表达式标志 */
DEF(flags, "flags")

/** global - 全局标志（g） */
DEF(global, "global")

/** unicode - Unicode 标志（u） */
DEF(unicode, "unicode")

/** raw - 原始字符串（模板字符串） */
DEF(raw, "raw")

/** rawJSON - 原始 JSON（内部使用） */
DEF(rawJSON, "rawJSON")

/** new.target - 当前构造函数 */
DEF(new_target, "new.target")

/** this.active_func - 当前活动函数（内部使用） */
DEF(this_active_func, "this.active_func")

/** <home_object> - 方法的家对象（内部使用） */
DEF(home_object, "<home_object>")

/** <computed_field> - 计算字段（内部使用） */
DEF(computed_field, "<computed_field>")

/** <static_computed_field> - 静态计算字段（内部使用） */
DEF(static_computed_field, "<static_computed_field>") /* must come after computed_fields */

/** <class_fields_init> - 类字段初始化（内部使用） */
DEF(class_fields_init, "<class_fields_init>")

/** <brand> - 类品牌检查（内部使用） */
DEF(brand, "<brand>")

/** #constructor - 私有构造函数（内部使用） */
DEF(hash_constructor, "#constructor")

/** as - 模块重命名（import X as Y） */
DEF(as, "as")

/** from - 模块来源（import from） */
DEF(from, "from")

/** meta - import.meta */
DEF(meta, "meta")

/** *default* - 默认导出标记 */
DEF(_default_, "*default*")

/** * - 通配符导入 */
DEF(_star_, "*")

/** Module - 模块对象 */
DEF(Module, "Module")

/** then - Promise 的 then 方法 */
DEF(then, "then")

/** resolve - Promise 的 resolve 方法 */
DEF(resolve, "resolve")

/** reject - Promise 的 reject 方法 */
DEF(reject, "reject")

/** promise - Promise 对象 */
DEF(promise, "promise")

/** proxy - Proxy 对象 */
DEF(proxy, "proxy")

/** revoke - 撤销 Proxy */
DEF(revoke, "revoke")

/** async - 异步函数标记 */
DEF(async, "async")

/** exec - 正则表达式执行 */
DEF(exec, "exec")

/** groups - 命名捕获组（正则） */
DEF(groups, "groups")

/** indices - 匹配索引（正则） */
DEF(indices, "indices")

/** status - 状态（Promise 等） */
DEF(status, "status")

/** reason - 拒绝原因 */
DEF(reason, "reason")

/** globalThis - 全局对象 */
DEF(globalThis, "globalThis")

/** bigint - BigInt 类型 */
DEF(bigint, "bigint")

/** -0 - 负零（内部使用） */
DEF(minus_zero, "-0")

/** Infinity - 无穷大 */
DEF(Infinity, "Infinity")

/** -Infinity - 负无穷大 */
DEF(minus_Infinity, "-Infinity")

/** NaN - 非数字 */
DEF(NaN, "NaN")

/** hasIndices - 正则标志（d） */
DEF(hasIndices, "hasIndices")

/** ignoreCase - 正则标志（i） */
DEF(ignoreCase, "ignoreCase")

/** multiline - 正则标志（m） */
DEF(multiline, "multiline")

/** dotAll - 正则标志（s） */
DEF(dotAll, "dotAll")

/** sticky - 正则标志（y） */
DEF(sticky, "sticky")

/** unicodeSets - 正则标志（v） */
DEF(unicodeSets, "unicodeSets")

/* 以下 3 个原子仅在 CONFIG_ATOMICS 启用时使用 */
DEF(not_equal, "not-equal")
DEF(timed_out, "timed-out")
DEF(ok, "ok")

/** toJSON - 对象转 JSON */
DEF(toJSON, "toJSON")

/** maxByteLength - 最大字节长度（TypedArray） */
DEF(maxByteLength, "maxByteLength")

/* ==================== 内置类名 ==================== */

/** Object - 对象构造函数 */
DEF(Object, "Object")

/** Array - 数组构造函数 */
DEF(Array, "Array")

/** Error - 错误基类 */
DEF(Error, "Error")

/** Number - 数字构造函数 */
DEF(Number, "Number")

/** String - 字符串构造函数 */
DEF(String, "String")

/** Boolean - 布尔构造函数 */
DEF(Boolean, "Boolean")

/** Symbol - Symbol 构造函数 */
DEF(Symbol, "Symbol")

/** Arguments - 参数对象类名 */
DEF(Arguments, "Arguments")

/** Math - 数学对象 */
DEF(Math, "Math")

/** JSON - JSON 对象 */
DEF(JSON, "JSON")

/** Date - 日期构造函数 */
DEF(Date, "Date")

/** Function - 函数构造函数 */
DEF(Function, "Function")

/** GeneratorFunction - 生成器函数类名 */
DEF(GeneratorFunction, "GeneratorFunction")

/** ForInIterator - for-in 迭代器类名 */
DEF(ForInIterator, "ForInIterator")

/** RegExp - 正则表达式构造函数 */
DEF(RegExp, "RegExp")

/** ArrayBuffer - 数组缓冲区 */
DEF(ArrayBuffer, "ArrayBuffer")

/** SharedArrayBuffer - 共享数组缓冲区 */
DEF(SharedArrayBuffer, "SharedArrayBuffer")

/* 必须与类型化数组的类 ID 保持相同顺序 */

/** Uint8ClampedArray - 无符号 8 位整数数组（钳位） */
DEF(Uint8ClampedArray, "Uint8ClampedArray")

/** Int8Array - 有符号 8 位整数数组 */
DEF(Int8Array, "Int8Array")

/** Uint8Array - 无符号 8 位整数数组 */
DEF(Uint8Array, "Uint8Array")

/** Int16Array - 有符号 16 位整数数组 */
DEF(Int16Array, "Int16Array")

/** Uint16Array - 无符号 16 位整数数组 */
DEF(Uint16Array, "Uint16Array")

/** Int32Array - 有符号 32 位整数数组 */
DEF(Int32Array, "Int32Array")

/** Uint32Array - 无符号 32 位整数数组 */
DEF(Uint32Array, "Uint32Array")

/** BigInt64Array - 有符号 64 位大整数数组 */
DEF(BigInt64Array, "BigInt64Array")

/** BigUint64Array - 无符号 64 位大整数数组 */
DEF(BigUint64Array, "BigUint64Array")

/** Float16Array - 16 位浮点数数组 */
DEF(Float16Array, "Float16Array")

/** Float32Array - 32 位浮点数数组 */
DEF(Float32Array, "Float32Array")

/** Float64Array - 64 位浮点数数组 */
DEF(Float64Array, "Float64Array")

/** DataView - 数据视图 */
DEF(DataView, "DataView")

/** BigInt - BigInt 构造函数 */
DEF(BigInt, "BigInt")

/** WeakRef - 弱引用（ES2021） */
DEF(WeakRef, "WeakRef")

/** FinalizationRegistry - 最终化注册表（ES2021） */
DEF(FinalizationRegistry, "FinalizationRegistry")

/** Map - 映射数据结构 */
DEF(Map, "Map")

/** Set - 集合数据结构 */
DEF(Set, "Set") /* Map + 1 */

/** WeakMap - 弱映射 */
DEF(WeakMap, "WeakMap") /* Map + 2 */

/** WeakSet - 弱集合 */
DEF(WeakSet, "WeakSet") /* Map + 3 */

/** Iterator - 迭代器 */
DEF(Iterator, "Iterator")

/** Iterator Helper - 迭代器辅助对象 */
DEF(IteratorHelper, "Iterator Helper")

/** Iterator Concat - 迭代器连接对象 */
DEF(IteratorConcat, "Iterator Concat")

/** Iterator Wrap - 迭代器包装对象 */
DEF(IteratorWrap, "Iterator Wrap")

/** Map Iterator - Map 迭代器 */
DEF(Map_Iterator, "Map Iterator")

/** Set Iterator - Set 迭代器 */
DEF(Set_Iterator, "Set Iterator")

/** Array Iterator - 数组迭代器 */
DEF(Array_Iterator, "Array Iterator")

/** String Iterator - 字符串迭代器 */
DEF(String_Iterator, "String Iterator")

/** RegExp String Iterator - 正则字符串迭代器 */
DEF(RegExp_String_Iterator, "RegExp String Iterator")

/** Generator - 生成器 */
DEF(Generator, "Generator")

/** Proxy - 代理 */
DEF(Proxy, "Proxy")

/** Promise - 承诺对象 */
DEF(Promise, "Promise")

/** PromiseResolveFunction - Promise resolve 函数类名 */
DEF(PromiseResolveFunction, "PromiseResolveFunction")

/** PromiseRejectFunction - Promise reject 函数类名 */
DEF(PromiseRejectFunction, "PromiseRejectFunction")

/** AsyncFunction - 异步函数类名 */
DEF(AsyncFunction, "AsyncFunction")

/** AsyncFunctionResolve - 异步函数 resolve 类名 */
DEF(AsyncFunctionResolve, "AsyncFunctionResolve")

/** AsyncFunctionReject - 异步函数 reject 类名 */
DEF(AsyncFunctionReject, "AsyncFunctionReject")

/** AsyncGeneratorFunction - 异步生成器函数类名 */
DEF(AsyncGeneratorFunction, "AsyncGeneratorFunction")

/** AsyncGenerator - 异步生成器 */
DEF(AsyncGenerator, "AsyncGenerator")

/** EvalError - eval 错误 */
DEF(EvalError, "EvalError")

/** RangeError - 范围错误 */
DEF(RangeError, "RangeError")

/** ReferenceError - 引用错误 */
DEF(ReferenceError, "ReferenceError")

/** SyntaxError - 语法错误 */
DEF(SyntaxError, "SyntaxError")

/** TypeError - 类型错误 */
DEF(TypeError, "TypeError")

/** URIError - URI 错误 */
DEF(URIError, "URIError")

/** InternalError - 内部错误 */
DEF(InternalError, "InternalError")

/** AggregateError - 聚合错误（ES2021） */
DEF(AggregateError, "AggregateError")

/* ==================== 私有符号 ==================== */

/** <brand> - 私有品牌标记 */
DEF(Private_brand, "<brand>")

/* ==================== 内置 Symbol ==================== */

/** Symbol.toPrimitive - 对象转原始值 */
DEF(Symbol_toPrimitive, "Symbol.toPrimitive")

/** Symbol.iterator - 默认迭代器 */
DEF(Symbol_iterator, "Symbol.iterator")

/** Symbol.match - 字符串匹配 */
DEF(Symbol_match, "Symbol.match")

/** Symbol.matchAll - 字符串全匹配 */
DEF(Symbol_matchAll, "Symbol.matchAll")

/** Symbol.replace - 字符串替换 */
DEF(Symbol_replace, "Symbol.replace")

/** Symbol.search - 字符串搜索 */
DEF(Symbol_search, "Symbol.search")

/** Symbol.split - 字符串分割 */
DEF(Symbol_split, "Symbol.split")

/** Symbol.toStringTag - 对象类型标签 */
DEF(Symbol_toStringTag, "Symbol.toStringTag")

/** Symbol.isConcatSpreadable - 数组连接时是否展开 */
DEF(Symbol_isConcatSpreadable, "Symbol.isConcatSpreadable")

/** Symbol.hasInstance - instanceof 检查 */
DEF(Symbol_hasInstance, "Symbol.hasInstance")

/** Symbol.species - 派生对象构造函数 */
DEF(Symbol_species, "Symbol.species")

/** Symbol.unscopables - with 作用域排除 */
DEF(Symbol_unscopables, "Symbol.unscopables")

/** Symbol.asyncIterator - 异步迭代器 */
DEF(Symbol_asyncIterator, "Symbol.asyncIterator")

#endif /* DEF */
