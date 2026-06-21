#include "lia.h"

static const char *lia_runtime_platform(void) {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

static int lia_lua_cwd(lua_State *L) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, strerror(errno));
        return 2;
    }

    lua_pushstring(L, cwd);
    return 1;
}

static int lia_lua_env(lua_State *L) {
    const char *name = luaL_checkstring(L, 1);
    const char *value = getenv(name);
    if (value == NULL) {
        lua_pushnil(L);
        return 1;
    }

    lua_pushstring(L, value);
    return 1;
}

static int lia_fs_read(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    char *content = read_file_to_string(path);
    if (content == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "failed to read %s", path);
        return 2;
    }

    lua_pushstring(L, content);
    free(content);
    return 1;
}

static int lia_fs_write(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    size_t length = 0;
    const char *content = luaL_checklstring(L, 2, &length);

    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        lua_pushnil(L);
        lua_pushfstring(L, "failed to open %s", path);
        return 2;
    }

    size_t written = fwrite(content, 1, length, file);
    if (fclose(file) != 0 || written != length) {
        lua_pushnil(L);
        lua_pushfstring(L, "failed to write %s", path);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lia_fs_exists(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, file_exists(path) || directory_exists(path));
    return 1;
}

static int lia_fs_is_dir(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    lua_pushboolean(L, directory_exists(path));
    return 1;
}

static int lia_fs_mkdir(lua_State *L) {
    const char *path = luaL_checkstring(L, 1);
    if (make_directory_p(path) != 0) {
        lua_pushnil(L);
        lua_pushfstring(L, "failed to create %s", path);
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

static int lia_path_join(lua_State *L) {
    const char *left = luaL_checkstring(L, 1);
    const char *right = luaL_checkstring(L, 2);
    char *path = join_path(left, right);
    if (path == NULL) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to join path");
        return 2;
    }

    lua_pushstring(L, path);
    free(path);
    return 1;
}

static int lia_process_platform(lua_State *L) {
    lua_pushstring(L, lia_runtime_platform());
    return 1;
}

static const luaL_Reg lia_fs_functions[] = {
    {"read", lia_fs_read},
    {"write", lia_fs_write},
    {"exists", lia_fs_exists},
    {"is_dir", lia_fs_is_dir},
    {"mkdir", lia_fs_mkdir},
    {NULL, NULL}
};

static const luaL_Reg lia_path_functions[] = {
    {"join", lia_path_join},
    {NULL, NULL}
};

static const luaL_Reg lia_process_functions[] = {
    {"cwd", lia_lua_cwd},
    {"env", lia_lua_env},
    {"platform", lia_process_platform},
    {NULL, NULL}
};

static int luaopen_lia_fs(lua_State *L) {
    luaL_newlib(L, lia_fs_functions);
    return 1;
}

static int luaopen_lia_path(lua_State *L) {
    luaL_newlib(L, lia_path_functions);
    return 1;
}

static int luaopen_lia_process(lua_State *L) {
    luaL_newlib(L, lia_process_functions);
    return 1;
}

static void push_lia_runtime_table(lua_State *L) {
    lua_createtable(L, 0, 12);

    lua_pushstring(L, "Lia");
    lua_setfield(L, -2, "name");

    lua_pushstring(L, LIA_VERSION);
    lua_setfield(L, -2, "version");

    lua_pushstring(L, LUA_VERSION);
    lua_setfield(L, -2, "lua_version");

    lua_pushstring(L, LUA_RELEASE);
    lua_setfield(L, -2, "lua_release");

    lua_pushstring(L, lia_runtime_platform());
    lua_setfield(L, -2, "platform");

    lua_pushstring(L, LIA_MANIFEST_FILE);
    lua_setfield(L, -2, "manifest_file");

    lua_pushstring(L, LIA_LOCK_FILE);
    lua_setfield(L, -2, "lock_file");

    lua_pushstring(L, LIA_PACKAGES_DIR);
    lua_setfield(L, -2, "packages_dir");

    lua_getglobal(L, "arg");
    if (lua_istable(L, -1)) {
        lua_setfield(L, -2, "args");
    } else {
        lua_pop(L, 1);
    }

    lua_pushcfunction(L, lia_lua_cwd);
    lua_setfield(L, -2, "cwd");

    lua_pushcfunction(L, lia_lua_env);
    lua_setfield(L, -2, "env");

    luaopen_lia_fs(L);
    lua_setfield(L, -2, "fs");

    luaopen_lia_path(L);
    lua_setfield(L, -2, "path");

    luaopen_lia_process(L);
    lua_setfield(L, -2, "process");
}

static int luaopen_lia(lua_State *L) {
    push_lia_runtime_table(L);
    return 1;
}

static int register_lia_runtime(lua_State *L) {
    push_lia_runtime_table(L);

    lua_pushvalue(L, -1);
    lua_setglobal(L, "_LIA");

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return 1;
    }

    lua_getfield(L, -1, "loaded");
    if (lua_istable(L, -1)) {
        lua_pushvalue(L, -3);
        lua_setfield(L, -2, "lia");
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "preload");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, luaopen_lia);
        lua_setfield(L, -2, "lia");
        lua_pushcfunction(L, luaopen_lia_fs);
        lua_setfield(L, -2, "lia.fs");
        lua_pushcfunction(L, luaopen_lia_path);
        lua_setfield(L, -2, "lia.path");
        lua_pushcfunction(L, luaopen_lia_process);
        lua_setfield(L, -2, "lia.process");
    }
    lua_pop(L, 1);

    lua_pop(L, 2);
    return 0;
}

static void set_arg_table(lua_State *L, int argc, char **argv, int script_index) {
    int arg_count = argc - script_index - 1;

    lua_createtable(L, arg_count, 1);
    lua_pushstring(L, argv[script_index]);
    lua_rawseti(L, -2, 0);

    for (int i = 0; i < arg_count; i++) {
        lua_pushstring(L, argv[script_index + 1 + i]);
        lua_rawseti(L, -2, i + 1);
    }

    lua_setglobal(L, "arg");
}

static int prepend_package_path(lua_State *L, const char *prefix) {
    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return 1;
    }

    lua_getfield(L, -1, "path");
    const char *current = lua_tostring(L, -1);
    if (current == NULL) {
        current = "";
    }

    size_t prefix_len = strlen(prefix);
    size_t current_len = strlen(current);
    char *combined = malloc(prefix_len + current_len + 1);
    if (combined == NULL) {
        lua_pop(L, 2);
        return 1;
    }

    memcpy(combined, prefix, prefix_len);
    memcpy(combined + prefix_len, current, current_len + 1);

    lua_pop(L, 1);
    lua_pushstring(L, combined);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);

    free(combined);
    return 0;
}

static int report_lua_error(lua_State *L) {
    const char *message = lua_tostring(L, -1);
    if (message == NULL) {
        message = "unknown Lua error";
    }

    fprintf(stderr, "lia: %s\n", message);
    lua_pop(L, 1);
    return 1;
}


int run_lia_script(int argc, char **argv, int script_index) {
    lua_State *L = luaL_newstate();
    if (L == NULL) {
        fprintf(stderr, "lia: failed to create Lua state\n");
        return 1;
    }

    luaL_openlibs(L);
    if (prepend_package_path(L, "./?.lua;./?/init.lua;packages/?.lua;packages/?/init.lua;packages/?/?.lua;packages/?/src/?.lua;packages/?/src/init.lua;") != 0) {
        fprintf(stderr, "lia: failed to configure package.path\n");
        lua_close(L);
        return 1;
    }

    set_arg_table(L, argc, argv, script_index);
    if (register_lia_runtime(L) != 0) {
        fprintf(stderr, "lia: failed to configure Lia runtime API\n");
        lua_close(L);
        return 1;
    }

    int status = luaL_loadfile(L, argv[script_index]);
    if (status != LUA_OK) {
        int exit_code = report_lua_error(L);
        lua_close(L);
        return exit_code;
    }

    status = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (status != LUA_OK) {
        int exit_code = report_lua_error(L);
        lua_close(L);
        return exit_code;
    }

    lua_close(L);
    return 0;
}
