#include "skynet.h"

#include "skynet_imp.h"
#include "skynet_env.h"
#include "skynet_server.h"
#include "luashrtbl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <signal.h>
#include <assert.h>

static int
optint(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		char tmp[20];
		sprintf(tmp,"%d",opt);
		skynet_setenv(key, tmp);
		return opt;
	}
	return strtol(str, NULL, 10);
}

static int
optboolean(const char *key, int opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		skynet_setenv(key, opt ? "true" : "false");
		return opt;
	}
	return strcmp(str,"true")==0;
}

static const char *
optstring(const char *key,const char * opt) {
	const char * str = skynet_getenv(key);
	if (str == NULL) {
		if (opt) {
			skynet_setenv(key, opt);
			opt = skynet_getenv(key);
		}
		return opt;
	}
	return str;
}

static void
_init_env(lua_State *L) {
	lua_pushnil(L);  /* first key */
	while (lua_next(L, -2) != 0) {
		int keyt = lua_type(L, -2);
		if (keyt != LUA_TSTRING) {
			fprintf(stderr, "Invalid config table\n");
			exit(1);
		}
		const char * key = lua_tostring(L,-2);
		fprintf(stderr, "get config %s = ", key);
		if (lua_type(L,-1) == LUA_TBOOLEAN) {
			int b = lua_toboolean(L,-1);
			skynet_setenv(key,b ? "true" : "false" );
			fprintf(stderr, key,b ? "true" : "false");
			fprintf(stderr, "\n");
		} else {
			const char * value = lua_tostring(L,-1);
			if (value == NULL) {
				fprintf(stderr, "Invalid config table key = %s\n", key);
				exit(1);
			}
			skynet_setenv(key,value);
			fprintf(stderr, value);
			fprintf(stderr, "\n");
		}
		lua_pop(L,1);
	}
	lua_pop(L,1);
}

int sigign() {
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGPIPE, &sa, 0);
	return 0;
}

static const char * load_config = "\
	local result = {}\n\
	local function getenv(name) return assert(os.getenv(name), [[os.getenv() failed: ]] .. name) end\n\
	-- package.config 是lua5.3里面的一个系统变量，一共5行，分别是/;?!-, 所以下面这句话就是取的 / \n\
	local sep = package.config:sub(1,1) \n\
	\n\
	-- current_path 就是 ./ \n\
	local current_path = [[.]]..sep\n\
	\n\
	local function include(filename)\n\
        	print(\"will include = \" .. filename .. \", current_path = \" .. current_path) \n\
		\n\
		local last_path = current_path\n\
		local path, name = filename:match([[(.*]]..sep..[[)(.*)$]])\n\
		\n\
		print(\"path = \" .. tostring(path) .. \", name = \" .. tostring(name)) \n\
		\n\
		\n\
		if path then\n\
			if path:sub(1,1) == sep then	-- root\n\
				current_path = path\n\
			else\n\
				current_path = current_path .. path\n\
			end\n\
		else\n\
			name = filename\n\
		end\n\
		\n\
		\n\
		-- 打开include的文件并读取\n\
		local f = assert(io.open(current_path .. name))\n\
		local code = assert(f:read [[*a]])\n\
		\n\
		\n\
		-- 对include文件中的系统环境变量字符串进行退换\n\
		code = string.gsub(code, [[%$([%w_%d]+)]], getenv)\n\
		f:close()\n\
		\n\
		\n\
		-- load 代码\n\
		-- load (chunk [, chunkname [, mode [, env]]]) \n\
		-- chunk是代码字符串，chunkname是预留调试信息，mode 是 t 表示chunk是字符串 b 表示代码是编译后的二进制， env是值得load后chunk的环境 \n\
		-- 注意load本身只是把代码加载进虚拟机，并不执行代码，所以最后要来一个()执行这些代码 \n\
		\n\
		\n\
		local func_body = load(code, [[@]]..filename, [[t]], result) \n\
		assert(func_body) \n\
		func_body() \n\
		current_path = last_path\n\
	end\n\
	setmetatable(result, { __index = { include = include } })\n\
	local config_name = ...\n\
	include(config_name)\n\
	setmetatable(result, nil)\n\
	return result\n\
";

int
main(int argc, char *argv[]) {
	// 获取config配置文件，必须传递一个config文件用来启动
	const char * config_file = NULL ;
	if (argc > 1) {
		config_file = argv[1];
	} else {
		fprintf(stderr, "Need a config file. Please read skynet wiki : https://github.com/cloudwu/skynet/wiki/Config\n"
			"usage: skynet configfilename\n");
		return 1;
	}

	luaS_initshr();
	skynet_globalinit();
	skynet_env_init();

	sigign();

	/* -------------------- 下面开始读取配置文件中的配置 ----------------------- */
	struct skynet_config config;

	struct lua_State *L = luaL_newstate();
	luaL_openlibs(L);	// link lua lib

	int err =  luaL_loadbufferx(L, load_config, strlen(load_config), "=[skynet config]", "t");
	assert(err == LUA_OK);
	lua_pushstring(L, config_file);

	err = lua_pcall(L, 1, 1, 0);
	if (err) {
		fprintf(stderr,"%s\n",lua_tostring(L,-1));
		lua_close(L);
		return 1;
	}

	// 把从config文件中读取的诸多信息安装到env里面
	// 从临时的L把conf都导到全局二代evn L里面
	_init_env(L);

	config.thread =  optint("thread",8);
	config.module_path = optstring("cpath","./cservice/?.so");
	config.harbor = optint("harbor", 1);
	config.bootstrap = optstring("bootstrap","snlua bootstrap");
	config.daemon = optstring("daemon", NULL);
	config.logger = optstring("logger", NULL);
	config.logservice = optstring("logservice", "logger");
	config.profile = optboolean("profile", 1);

	lua_close(L);
	/* ------------------------------------------- */
	

	skynet_start(&config);
	skynet_globalexit();
	luaS_exitshr();

	return 0;
}
