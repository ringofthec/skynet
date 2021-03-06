#ifndef SKYNET_MODULE_H
#define SKYNET_MODULE_H

struct skynet_context;

// 一个module必须实现四大回调
// 创建，初始化，释放，信号处理
typedef void * (*skynet_dl_create)(void);
typedef int (*skynet_dl_init)(void * inst, struct skynet_context *, const char * parm);
typedef void (*skynet_dl_release)(void * inst);
typedef void (*skynet_dl_signal)(void * inst, int signal);

struct skynet_module {
	const char * name; // 一个模块的名字 
	void * module;     // 模块的 so 句柄 
	skynet_dl_create create;
	skynet_dl_init init;
	skynet_dl_release release;
	skynet_dl_signal signal;
};


// 沙雕接口，没人用，也不知道为啥暴露
void skynet_module_insert(struct skynet_module *mod);

// 查询（没加载就加载）指定的服务模块
struct skynet_module * skynet_module_query(const char * name);

// 四大接口

// 创建一个服务模块的实例
void * skynet_module_instance_create(struct skynet_module *);

// 初始化实例
int skynet_module_instance_init(struct skynet_module *, void * inst, struct skynet_context *ctx, const char * parm);

//
void skynet_module_instance_release(struct skynet_module *, void *inst);

//
void skynet_module_instance_signal(struct skynet_module *, void *inst, int signal);

void skynet_module_init(const char *path);

#endif
