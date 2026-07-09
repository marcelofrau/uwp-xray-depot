#pragma once
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <string>
#include <cstring>
#include <cstdio>

namespace xb {

class lua_state {
public:
    lua_state()
    {
        L_ = luaL_newstate();
        if (!L_) return;
        luaL_openlibs(L_);
        sandbox();
        create_metatables();
    }

    ~lua_state()
    {
        if (L_) lua_close(L_);
    }

    bool valid() const { return L_ != nullptr; }

    std::string exec(const char* code)
    {
        if (!L_ || !code) return "error: no state";

        instruction_count_ = 0;
        lua_sethook(L_, hook, LUA_MASKCOUNT, MAX_OPS);

        int top = lua_gettop(L_);

        if (luaL_loadstring(L_, code) != LUA_OK) {
            std::string err = lua_tostring(L_, -1);
            lua_settop(L_, top);
            lua_sethook(L_, nullptr, 0, 0);
            return err;
        }

        if (lua_pcall(L_, 0, LUA_MULTRET, 0) != LUA_OK) {
            std::string err = lua_tostring(L_, -1);
            lua_settop(L_, top);
            lua_sethook(L_, nullptr, 0, 0);
            return err;
        }

        lua_sethook(L_, nullptr, 0, 0);

        // Collect return values
        int nret = lua_gettop(L_) - top;
        if (nret == 0) {
            return "(no return)";
        }

        std::string result;
        for (int i = 1; i <= nret; ++i) {
            if (i > 1) result += "\t";
            result += to_string(top + i);
        }
        lua_settop(L_, top);
        return result;
    }

    void bind_int(const char* name, int* ptr)
    {
        if (!L_ || !name || !ptr) return;
        auto* ref = static_cast<var_ref*>(lua_newuserdata(L_, sizeof(var_ref)));
        ref->ptr = ptr;
        ref->type = 'i';
        luaL_setmetatable(L_, "xray_int");
        lua_setglobal(L_, name);
    }

    void bind_float(const char* name, float* ptr)
    {
        if (!L_ || !name || !ptr) return;
        auto* ref = static_cast<var_ref*>(lua_newuserdata(L_, sizeof(var_ref)));
        ref->ptr = ptr;
        ref->type = 'f';
        luaL_setmetatable(L_, "xray_float");
        lua_setglobal(L_, name);
    }

    void bind_bool(const char* name, bool* ptr)
    {
        if (!L_ || !name || !ptr) return;
        auto* ref = static_cast<var_ref*>(lua_newuserdata(L_, sizeof(var_ref)));
        ref->ptr = ptr;
        ref->type = 'b';
        luaL_setmetatable(L_, "xray_bool");
        lua_setglobal(L_, name);
    }

    void bind_f32a(const char* name, float* ptr, int len)
    {
        if (!L_ || !name || !ptr || len <= 0) return;
        auto* ref = static_cast<array_ref*>(lua_newuserdata(L_, sizeof(array_ref)));
        ref->ptr = ptr;
        ref->type = 'f';
        ref->len = len;
        luaL_setmetatable(L_, "xray_f32a");
        lua_setglobal(L_, name);
    }

    void bind_i32a(const char* name, int* ptr, int len)
    {
        if (!L_ || !name || !ptr || len <= 0) return;
        auto* ref = static_cast<array_ref*>(lua_newuserdata(L_, sizeof(array_ref)));
        ref->ptr = ptr;
        ref->type = 'i';
        ref->len = len;
        luaL_setmetatable(L_, "xray_i32a");
        lua_setglobal(L_, name);
    }

private:
    struct var_ref {
        void* ptr;
        char type;
    };

    struct array_ref {
        void* ptr;
        char type;
        int len;
    };

    lua_State* L_ = nullptr;
    int instruction_count_ = 0;
    static constexpr int MAX_OPS = 100'000;

    static int panic(lua_State* L)
    {
        const char* msg = lua_tostring(L, -1);
        if (!msg) msg = "unknown panic";
        fprintf(stderr, "[xray] Lua panic: %s\n", msg);
        return 0;
    }

    static void hook(lua_State* L, lua_Debug*)
    {
        lua_sethook(L, nullptr, 0, 0);
        luaL_error(L, "timeout: exceeded %d instructions", MAX_OPS);
    }

    std::string to_string(int idx)
    {
        switch (lua_type(L_, idx)) {
            case LUA_TNIL:         return "nil";
            case LUA_TBOOLEAN:     return lua_toboolean(L_, idx) ? "true" : "false";
            case LUA_TNUMBER: {
                char buf[64];
                double d = lua_tonumber(L_, idx);
                if (d == static_cast<long long>(d))
                    std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
                else
                    std::snprintf(buf, sizeof(buf), "%.14g", d);
                return buf;
            }
            case LUA_TSTRING: {
                size_t len;
                const char* s = lua_tolstring(L_, idx, &len);
                if (!s) return "nil";
                if (len > 1024)
                    return std::string(s, 128) + "...(truncated " + std::to_string(len) + " chars)";
                return std::string(s, len);
            }
            case LUA_TTABLE: {
                // Check if array-like
                lua_len(L_, idx);
                lua_Integer arr_len = lua_tointeger(L_, -1);
                lua_pop(L_, 1);

                if (arr_len > 0 && arr_len <= 32) {
                    std::string r = "{";
                    for (lua_Integer i = 1; i <= arr_len; ++i) {
                        if (i > 1) r += ", ";
                        lua_rawgeti(L_, idx, i);
                        r += to_string(-1);
                        lua_pop(L_, 1);
                    }
                    r += "}";
                    return r;
                }

                // Push metatable to check __name or just show address
                if (lua_getmetatable(L_, idx)) {
                    lua_pushstring(L_, "__name");
                    lua_rawget(L_, -2);
                    if (lua_isstring(L_, -1)) {
                        std::string r = lua_tostring(L_, -1);
                        lua_pop(L_, 2);
                        return r + ": " + fmt_ptr(idx);
                    }
                    lua_pop(L_, 2);
                }
                return "table: " + fmt_ptr(idx);
            }
            case LUA_TUSERDATA: {
                if (lua_getmetatable(L_, idx)) {
                    lua_pushstring(L_, "__name");
                    lua_rawget(L_, -2);
                    if (lua_isstring(L_, -1)) {
                        std::string r = lua_tostring(L_, -1);
                        lua_pop(L_, 2);
                        return r;
                    }
                    lua_pop(L_, 2);
                }
                return "userdata: " + fmt_ptr(idx);
            }
            case LUA_TFUNCTION: return "function";
            case LUA_TTHREAD:   return "thread";
            default:            return "unknown";
        }
    }

    std::string fmt_ptr(int idx)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%p", lua_touserdata(L_, idx));
        return buf;
    }

    void sandbox()
    {
        // Remove dangerous globals
        const char* remove[] = {
            "dofile", "loadfile", "require", "load",
            "io", "os", "package", "debug",
            nullptr
        };
        for (int i = 0; remove[i]; ++i) {
            lua_pushnil(L_);
            lua_setglobal(L_, remove[i]);
        }

        // Protect _G from metatable inspection (Lua 5.2+)
        lua_pushglobaltable(L_);
        lua_newtable(L_);
        lua_pushstring(L_, "locked");
        lua_setfield(L_, -2, "__metatable");
        lua_setmetatable(L_, -2);
        lua_pop(L_, 1);
    }

    void create_metatables()
    {
        // Scalar int
        create_scalar_mt("xray_int", 'i');
        // Scalar float
        create_scalar_mt("xray_float", 'f');
        // Scalar bool
        create_scalar_mt("xray_bool", 'b');
        // Array float[]
        create_array_mt("xray_f32a", 'f');
        // Array int[]
        create_array_mt("xray_i32a", 'i');
    }

    void create_scalar_mt(const char* mt_name, char type)
    {
        luaL_newmetatable(L_, mt_name);

        // __name for pretty-print
        lua_pushstring(L_, mt_name + 5); // skip "xray_"
        lua_setfield(L_, -2, "__name");

        // __index
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<var_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->ptr) return 0;
            switch (ref->type) {
                case 'i': lua_pushinteger(L, *static_cast<int*>(ref->ptr)); break;
                case 'f': lua_pushnumber(L, *static_cast<float*>(ref->ptr)); break;
                case 'b': lua_pushboolean(L, *static_cast<bool*>(ref->ptr)); break;
                default: return 0;
            }
            return 1;
        });
        lua_setfield(L_, -2, "__index");

        // __newindex
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<var_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->ptr) return 0;
            switch (ref->type) {
                case 'i': *static_cast<int*>(ref->ptr) = static_cast<int>(luaL_checkinteger(L, 3)); break;
                case 'f': *static_cast<float*>(ref->ptr) = static_cast<float>(luaL_checknumber(L, 3)); break;
                case 'b': *static_cast<bool*>(ref->ptr) = lua_toboolean(L, 3) != 0; break;
            }
            return 0;
        });
        lua_setfield(L_, -2, "__newindex");

        // __tostring
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<var_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->ptr) return 0;
            char buf[64];
            switch (ref->type) {
                case 'i': std::snprintf(buf, sizeof(buf), "%d", *static_cast<int*>(ref->ptr)); break;
                case 'f': std::snprintf(buf, sizeof(buf), "%.14g", *static_cast<float*>(ref->ptr)); break;
                case 'b': std::snprintf(buf, sizeof(buf), "%s", *static_cast<bool*>(ref->ptr) ? "true" : "false"); break;
                default: return 0;
            }
            lua_pushstring(L, buf);
            return 1;
        });
        lua_setfield(L_, -2, "__tostring");

        lua_pop(L_, 1);
    }

    void create_array_mt(const char* mt_name, char type)
    {
        luaL_newmetatable(L_, mt_name);

        lua_pushstring(L_, mt_name + 5);
        lua_setfield(L_, -2, "__name");

        // __index (numeric access, 1-based)
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<array_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->ptr) return 0;
            lua_Integer idx = luaL_checkinteger(L, 2);
            if (idx < 1 || idx > ref->len) {
                return luaL_error(L, "index out of range [1,%d]", ref->len);
            }
            int i = static_cast<int>(idx) - 1;
            switch (ref->type) {
                case 'f': lua_pushnumber(L, static_cast<float*>(ref->ptr)[i]); break;
                case 'i': lua_pushinteger(L, static_cast<int*>(ref->ptr)[i]); break;
                default: return 0;
            }
            return 1;
        });
        lua_setfield(L_, -2, "__index");

        // __newindex
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<array_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->ptr) return 0;
            lua_Integer idx = luaL_checkinteger(L, 2);
            if (idx < 1 || idx > ref->len) {
                return luaL_error(L, "index out of range [1,%d]", ref->len);
            }
            int i = static_cast<int>(idx) - 1;
            switch (ref->type) {
                case 'f': static_cast<float*>(ref->ptr)[i] = static_cast<float>(luaL_checknumber(L, 3)); break;
                case 'i': static_cast<int*>(ref->ptr)[i] = static_cast<int>(luaL_checkinteger(L, 3)); break;
            }
            return 0;
        });
        lua_setfield(L_, -2, "__newindex");

        // __len
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<array_ref*>(lua_touserdata(L, 1));
            if (!ref) return 0;
            lua_pushinteger(L, ref->len);
            return 1;
        });
        lua_setfield(L_, -2, "__len");

        // __pairs — enable iteration
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            // Returns: next_func, state (userdata), initial_key (0)
            lua_pushcfunction(L, [](lua_State* L2) -> int {
                // Args: (state=userdata, key)
                auto* ref = static_cast<array_ref*>(lua_touserdata(L2, 1));
                if (!ref) return 0;
                lua_Integer i = lua_tointeger(L2, 2);
                if (i >= ref->len) return 0;
                lua_pushinteger(L2, i + 1);
                switch (ref->type) {
                    case 'f': lua_pushnumber(L2, static_cast<float*>(ref->ptr)[static_cast<int>(i)]); break;
                    case 'i': lua_pushinteger(L2, static_cast<int*>(ref->ptr)[static_cast<int>(i)]); break;
                }
                return 2;
            });
            lua_pushvalue(L, 1); // state = the userdata itself
            lua_pushinteger(L, 0);
            return 3;
        });
        lua_setfield(L_, -2, "__pairs");

        // __tostring
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<array_ref*>(lua_touserdata(L, 1));
            if (!ref) return 0;
            std::string r = "{";
            for (int i = 0; i < ref->len && i < 8; ++i) {
                if (i > 0) r += ", ";
                char buf[32];
                switch (ref->type) {
                    case 'f': std::snprintf(buf, sizeof(buf), "%.14g", static_cast<float*>(ref->ptr)[i]); break;
                    case 'i': std::snprintf(buf, sizeof(buf), "%d", static_cast<int*>(ref->ptr)[i]); break;
                }
                r += buf;
            }
            if (ref->len > 8) r += ", ...";
            r += "}";
            lua_pushstring(L, r.c_str());
            return 1;
        });
        lua_setfield(L_, -2, "__tostring");

        lua_pop(L_, 1);
    }
};

} // namespace xb
