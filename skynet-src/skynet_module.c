#include "skynet.h"

#include "skynet_module.h"
#include "spinlock.h"

#include <assert.h>
#include <string.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

// 最多添加32个模块
// 其实这个限制没意义，也没卵用
#define MAX_MODULE_TYPE 32

struct modules {
	int count;
	struct spinlock lock;
	const char * path;
	struct skynet_module m[MAX_MODULE_TYPE];
};

static struct modules * M = NULL;

// 打开moudle，本质上就是加载 so 动态库
static void *
_try_open(struct modules *m, const char * name) {
	const char *l;
	const char * path = m->path;
	size_t path_size = strlen(path);
	size_t name_size = strlen(name);

	int sz = path_size + name_size;
	//search path
	void * dl = NULL;
	char tmp[sz];
	do
	{
		memset(tmp,0,sz);
		while (*path == ';') path++;
		if (*path == '\0') break;
		l = strchr(path, ';');
		if (l == NULL) l = path + strlen(path);
		int len = l - path;
		int i;
		for (i=0;path[i]!='?' && i < len ;i++) {
			tmp[i] = path[i];
		}
		memcpy(tmp+i,name,name_size);
		if (path[i] == '?') {
			strncpy(tmp+i+name_size,path+i+1,len - i - 1);
		} else {
			fprintf(stderr,"Invalid C service path\n");
			exit(1);
		}
		dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
		path = l;
	}while(dl == NULL);

	if (dl == NULL) {
		fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
	}

	return dl;
}

// 按名字查找模块
static struct skynet_module * 
_query(const char * name) {
	int i;
	for (i=0;i<M->count;i++) {
		if (strcmp(M->m[i].name,name)==0) {
			return &M->m[i];
		}
	}
	return NULL;
}

static void *
get_api(struct skynet_module *mod, const char *api_name) {

	// 这就是C语言无语的地方，就是个简单的字符串拼接和判断，这里需要写这么长的代码
	// 这很手动挡
	size_t name_size = strlen(mod->name);
	size_t api_size = strlen(api_name);
	char tmp[name_size + api_size + 1];
	memcpy(tmp, mod->name, name_size);
	memcpy(tmp+name_size, api_name, api_size+1);

	// 细节: 如果是 AAA.BBB_CCC，下面的函数会返回 BBB_CCC
	char *ptr = strrchr(tmp, '.');


	if (ptr == NULL) {
		ptr = tmp;
		fprintf(stderr, "tmp=%s\n", tmp);
	} else {
		ptr = ptr + 1;
		fprintf(stderr, "tmp=%s, ptr=%s\n", tmp, ptr);
	}

	// dlsym(A, F) 是从A中（dlopen的一个so文件，类似windows上面的dll文件）获取一个名为F的函数指针
	return dlsym(mod->module, ptr);
}

static int
open_sym(struct skynet_module *mod) {
	// 获取4大接口函数
	// module_create
	// module_init
	// module_release
	// module_signal
	//
	mod->create = get_api(mod, "_create");
	mod->init = get_api(mod, "_init");
	mod->release = get_api(mod, "_release");
	mod->signal = get_api(mod, "_signal");

	return mod->init == NULL;
}

// 按名字查找模块
// 如果模块已经加载了，就返回模块
// 如果没有加载，就尝试加载模块
struct skynet_module * 
skynet_module_query(const char * name) {
	struct skynet_module * result = _query(name);
	if (result)
		return result;

	SPIN_LOCK(M)

	result = _query(name); // double check

	if (result == NULL && M->count < MAX_MODULE_TYPE) {
		int index = M->count;

		// 加载模块
		void * dl = _try_open(M,name);
		if (dl) {
			M->m[index].name = name;
			M->m[index].module = dl;

			// 获取模块4大接口
			if (open_sym(&M->m[index]) == 0) {
				M->m[index].name = skynet_strdup(name);
				M->count ++;
				result = &M->m[index];
			}
		}
	}

	SPIN_UNLOCK(M)

	return result;
}

// 插入一个模块，莫名感觉这个函数很沙雕
// skynet_module的管理本身就应该是在这个文件中进行，给外部暴露这样一个接口
// 有个卵儿用
void 
skynet_module_insert(struct skynet_module *mod) {
	SPIN_LOCK(M)

	fprintf(stderr,"skynet_module_insert %s\n", mod->name);

	struct skynet_module * m = _query(mod->name);
	assert(m == NULL && M->count < MAX_MODULE_TYPE);
	int index = M->count;
	M->m[index] = *mod;
	++M->count;

	SPIN_UNLOCK(M)
}


// 四大接口的接口
void * 
skynet_module_instance_create(struct skynet_module *m) {
	if (m->create) {
		return m->create();
	} else {
		return (void *)(intptr_t)(~0);
	}
}

int
skynet_module_instance_init(struct skynet_module *m, void * inst, struct skynet_context *ctx, const char * parm) {
	return m->init(inst, ctx, parm);
}

void 
skynet_module_instance_release(struct skynet_module *m, void *inst) {
	if (m->release) {
		m->release(inst);
	}
}

void
skynet_module_instance_signal(struct skynet_module *m, void *inst, int signal) {
	if (m->signal) {
		m->signal(inst, signal);
	}
}

void 
skynet_module_init(const char *path) {
	struct modules *m = skynet_malloc(sizeof(*m));
	m->count = 0;
	m->path = skynet_strdup(path);

	SPIN_INIT(m)

	M = m;
}
