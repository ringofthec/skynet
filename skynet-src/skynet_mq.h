#ifndef SKYNET_MESSAGE_QUEUE_H
#define SKYNET_MESSAGE_QUEUE_H

#include <stdlib.h>
#include <stdint.h>

struct skynet_message {
	uint32_t source; // 消息来源
	int session;	
	void * data;	// 数据
	size_t sz;
};

// type is encoding in skynet_message.sz high 8bit
#define MESSAGE_TYPE_MASK (SIZE_MAX >> 8)
#define MESSAGE_TYPE_SHIFT ((sizeof(size_t)-1) * 8)

struct message_queue;

// 把一个消息队列压入全局消息队列中
void skynet_globalmq_push(struct message_queue * queue);
// 从全局消息队列中弹出一个消息队列
struct message_queue * skynet_globalmq_pop(void);


// 为一个服务创建一个消息队列，这个服务是由handle指定的
struct message_queue * skynet_mq_create(uint32_t handle);
// 标记一个消息队列为删除状态（注意不是删除一个消息队列只是标记）
void skynet_mq_mark_release(struct message_queue *q);

typedef void (*message_drop)(struct skynet_message *, void *);

// 删除一个消息队列，参数是一个删除后的回调函数，可以附带一个自定义参数
void skynet_mq_release(struct message_queue *q, message_drop drop_func, void *ud);
// 获取消息队列所属的服务
uint32_t skynet_mq_handle(struct message_queue *);

// 0 for success
// 从消息队列中弹出一个消息
int skynet_mq_pop(struct message_queue *q, struct skynet_message *message);

// 向消息队列中压入一个消息
void skynet_mq_push(struct message_queue *q, struct skynet_message *message);

// return the length of message queue, for debug
// 获取消息列表中的消息数量
int skynet_mq_length(struct message_queue *q);
int skynet_mq_overload(struct message_queue *q);

// 初始化
void skynet_mq_init();

#endif
