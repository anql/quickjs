/*
 * Linux klist like system
 * 双向链表实现，类似 Linux 内核的链表结构
 * 
 * 设计说明：
 * - 这是一个无头节点的循环双向链表实现
 * - 链表头就是一个 struct list_head，不包含数据
 * - 通过 list_entry 宏可以从链表节点反推包含它的数据结构
 * - 所有操作都是 O(1) 时间复杂度
 *
 * Copyright (c) 2016-2017 Fabrice Bellard
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
#ifndef LIST_H
#define LIST_H

#ifndef NULL
#include <stddef.h>
#endif

/**
 * 链表节点结构
 * 
 * 这是一个通用的双向链表节点，不包含任何数据字段。
 * 实际使用时，将它嵌入到数据结构中，通过 list_entry 宏反推父结构。
 * 
 * 示例用法：
 * struct MyData {
 *     int value;
 *     struct list_head link;  // 嵌入链表节点
 * };
 * 
 * struct MyData data;
 * struct list_head *el = &data.link;
 * struct MyData *container = list_entry(el, struct MyData, link);
 */
struct list_head {
    struct list_head *prev;  ///< 指向前一个节点
    struct list_head *next;  ///< 指向后一个节点
};

/**
 * 静态初始化宏
 * 
 * 用于在编译时初始化链表头，创建一个指向自己的空链表。
 * 
 * 示例：
 * struct list_head my_list = LIST_HEAD_INIT(my_list);
 */
#define LIST_HEAD_INIT(el) { &(el), &(el) }

/**
 * 从链表节点获取包含它的父结构指针
 * 
 * 这是 Linux 内核风格的 container_of 宏，通过成员地址反推结构体起始地址。
 * 
 * @param el     链表节点指针
 * @param type   父结构体类型
 * @param member 父结构体中链表成员的字段名
 * @return       父结构体指针
 * 
 * 原理：父结构地址 = 成员地址 - 成员在结构体中的偏移
 */
#define list_entry(el, type, member) container_of(el, type, member)

/**
 * 初始化链表头
 * 
 * 将链表头初始化为空链表（自己指向自己）。
 * 
 * @param head 链表头指针
 * 
 * 示例：
 * struct list_head head;
 * init_list_head(&head);  // 现在 head 是一个空链表
 */
static inline void init_list_head(struct list_head *head)
{
    head->prev = head;  // 前驱指向自己
    head->next = head;  // 后继指向自己
}

/**
 * 在两个节点之间插入新节点（内部函数）
 * 
 * 将 el 插入到 prev 和 next 之间，形成：prev <-> el <-> next
 * 
 * @param el    要插入的新节点
 * @param prev  前一个节点
 * @param next  后一个节点
 * 
 * 注意：这是内部函数，不检查参数有效性，应通过 list_add/list_add_tail 调用
 */
static inline void __list_add(struct list_head *el,
                              struct list_head *prev, struct list_head *next)
{
    prev->next = el;   // prev 的后继改为 el
    el->prev = prev;   // el 的前驱指向 prev
    el->next = next;   // el 的后继指向 next
    next->prev = el;   // next 的前驱改为 el
}

/**
 * 在链表头部插入节点
 * 
 * 将 el 插入到 head 之后（即成为第一个元素）。
 * 
 * @param el    要插入的节点
 * @param head  链表头
 * 
 * 示例：
 * list_add(&new_node, &head);  // new_node 成为第一个元素
 */
static inline void list_add(struct list_head *el, struct list_head *head)
{
    __list_add(el, head, head->next);  // 插在 head 和 head->next 之间
}

/**
 * 在链表尾部插入节点
 * 
 * 将 el 插入到 head 之前（即成为最后一个元素）。
 * 
 * @param el    要插入的节点
 * @param head  链表头
 * 
 * 示例：
 * list_add_tail(&new_node, &head);  // new_node 成为最后一个元素
 */
static inline void list_add_tail(struct list_head *el, struct list_head *head)
{
    __list_add(el, head->prev, head);  // 插在 head->prev 和 head 之间
}

/**
 * 从链表中删除节点
 * 
 * 将 el 从链表中移除，并将其前后节点连接起来。
 * 删除后将 el 的指针设为 NULL 作为安全保护（fail safe）。
 * 
 * @param el 要删除的节点
 * 
 * 示例：
 * list_del(&node);  // node 从链表中移除
 * 
 * 注意：删除后 el 不再属于任何链表，再次使用需要重新初始化
 */
static inline void list_del(struct list_head *el)
{
    struct list_head *prev, *next;
    prev = el->prev;   // 保存前驱
    next = el->next;   // 保存后继
    prev->next = next; // 前驱直接连到后继
    next->prev = prev; // 后继直接连到前驱
    el->prev = NULL;   // 清空前驱指针（安全保护）
    el->next = NULL;   // 清空后继指针（安全保护）
}

/**
 * 判断链表是否为空
 * 
 * 空链表的特征是 head->next == head（自己指向自己）。
 * 
 * @param el 链表头
 * @return   1 表示空，0 表示非空
 * 
 * 示例：
 * if (list_empty(&head)) {
 *     // 链表为空
 * }
 */
static inline int list_empty(struct list_head *el)
{
    return el->next == el;  // 如果后继是自己，说明是空链表
}

/**
 * 正向遍历链表
 * 
 * 从头节点的下一个节点开始，遍历到回到头节点为止。
 * 
 * @param el   当前节点指针（遍历变量）
 * @param head 链表头
 * 
 * 示例：
 * struct list_head *el;
 * list_for_each(el, &head) {
 *     // 处理 el 指向的节点
 *     struct MyData *data = list_entry(el, struct MyData, link);
 * }
 */
#define list_for_each(el, head) \
  for(el = (head)->next; el != (head); el = el->next)

/**
 * 安全地正向遍历链表（允许删除当前节点）
 * 
 * 使用两个变量，提前保存下一个节点，这样即使在循环中删除当前节点也不会影响遍历。
 * 
 * @param el   当前节点指针
 * @param el1  临时变量，保存下一个节点
 * @param head 链表头
 * 
 * 示例：
 * struct list_head *el, *el1;
 * list_for_each_safe(el, el1, &head) {
 *     list_del(el);  // 安全删除当前节点
 * }
 */
#define list_for_each_safe(el, el1, head)                \
    for(el = (head)->next, el1 = el->next; el != (head); \
        el = el1, el1 = el->next)

/**
 * 反向遍历链表
 * 
 * 从头节点的前一个节点开始，反向遍历到回到头节点为止。
 * 
 * @param el   当前节点指针（遍历变量）
 * @param head 链表头
 * 
 * 示例：
 * struct list_head *el;
 * list_for_each_prev(el, &head) {
 *     // 从后往前处理节点
 * }
 */
#define list_for_each_prev(el, head) \
  for(el = (head)->prev; el != (head); el = el->prev)

/**
 * 安全地反向遍历链表（允许删除当前节点）
 * 
 * 使用两个变量，提前保存前一个节点，这样即使在循环中删除当前节点也不会影响遍历。
 * 
 * @param el   当前节点指针
 * @param el1  临时变量，保存前一个节点
 * @param head 链表头
 * 
 * 示例：
 * struct list_head *el, *el1;
 * list_for_each_prev_safe(el, el1, &head) {
 *     list_del(el);  // 安全删除当前节点
 * }
 */
#define list_for_each_prev_safe(el, el1, head)           \
    for(el = (head)->prev, el1 = el->prev; el != (head); \
        el = el1, el1 = el->prev)

#endif /* LIST_H */
