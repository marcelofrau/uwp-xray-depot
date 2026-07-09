#pragma once
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#include <string>
#include <cstring>
#include <cstdio>
#include <xray/struct_field.hpp>

namespace xb {

class lua_state {
    std::string print_buf_;

    static int print_lua_capture(lua_State* L)
    {
        lua_state* self = (lua_state*)lua_touserdata(L, lua_upvalueindex(1));
        int n = lua_gettop(L);
        for (int i = 1; i <= n; ++i) {
            if (i > 1) self->print_buf_ += '\t';
            const char* s = lua_tostring(L, i);
            if (s) self->print_buf_ += s;
        }
        self->print_buf_ += '\n';
        return 0;
    }

    static int lua_help(lua_State* L)
    {
        const char* msg =
            "Remote Lua functions:\n"
            "  help()       - Show this help\n"
            "  list()       - List bound variables\n"
            "  ls()         - Same as list()\n"
            "  bindings()   - Same as list()\n"
            "  pause()      - Pause the application loop\n"
            "  continue()   - Resume the application loop\n"
            "  terminate()  - Shut down the application\n"
            "\n"
            "Evaluate any Lua expression or statement.\n"
            "Use :help in the CLI for local commands.\n";
        lua_getglobal(L, "print");
        lua_pushstring(L, msg);
        lua_call(L, 1, 0);
        return 0;
    }

    static int lua_list(lua_State* L)
    {
        lua_state* self = (lua_state*)lua_touserdata(L, lua_upvalueindex(1));
        std::string listing = self->list_globals();
        lua_getglobal(L, "print");
        lua_pushstring(L, listing.c_str());
        lua_call(L, 1, 0);
        return 0;
    }

    static int lua_terminate(lua_State* L)
    {
        lua_state* self = (lua_state*)lua_touserdata(L, lua_upvalueindex(1));
        lua_getglobal(L, "print");
        if (self->on_terminate_) {
            self->on_terminate_();
            lua_pushstring(L, "terminate signal sent");
        } else {
            lua_pushstring(L, "no terminate callback registered");
        }
        lua_call(L, 1, 0);
        return 0;
    }

    static int lua_pause(lua_State* L)
    {
        lua_state* self = (lua_state*)lua_touserdata(L, lua_upvalueindex(1));
        lua_getglobal(L, "print");
        if (self->on_pause_) {
            self->on_pause_();
            lua_pushstring(L, "paused");
        } else {
            lua_pushstring(L, "no pause callback registered");
        }
        lua_call(L, 1, 0);
        return 0;
    }

    static int lua_continue(lua_State* L)
    {
        lua_state* self = (lua_state*)lua_touserdata(L, lua_upvalueindex(1));
        lua_getglobal(L, "print");
        if (self->on_continue_) {
            self->on_continue_();
            lua_pushstring(L, "continued");
        } else {
            lua_pushstring(L, "no continue callback registered");
        }
        lua_call(L, 1, 0);
        return 0;
    }

public:
    lua_state()
    {
        L_ = luaL_newstate();
        if (!L_) return;
        luaL_openlibs(L_);
        sandbox();
        create_metatables();
        capture_print();
        register_helper_functions();
    }

    void set_on_terminate(void (*fn)()) { on_terminate_ = fn; }
    void set_on_pause(void (*fn)()) { on_pause_ = fn; }
    void set_on_continue(void (*fn)()) { on_continue_ = fn; }

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

        print_buf_.clear();

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
        std::string result;
        if (nret > 0) {
            for (int i = 1; i <= nret; ++i) {
                if (i > 1) result += "\t";
                result += to_string(top + i);
            }
            lua_settop(L_, top);
        } else {
            lua_settop(L_, top);
            if (!print_buf_.empty()) {
                result = print_buf_;
                // trim trailing newline
                if (!result.empty() && result.back() == '\n')
                    result.pop_back();
            } else {
                result = "(no return)";
            }
        }
        return result;
    }

    void capture_print()
    {
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, print_lua_capture, 1);
        lua_setglobal(L_, "print");
    }

    void register_helper_functions()
    {
        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_help, 1);
        lua_setglobal(L_, "help");

        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_list, 1);
        lua_setglobal(L_, "list");

        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_list, 1);
        lua_setglobal(L_, "ls");

        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_list, 1);
        lua_setglobal(L_, "bindings");

        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_terminate, 1);
        lua_setglobal(L_, "terminate");

        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_pause, 1);
        lua_setglobal(L_, "pause");

        lua_pushlightuserdata(L_, this);
        lua_pushcclosure(L_, lua_continue, 1);
        lua_setglobal(L_, "continue");
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

    void bind_double(const char* name, double* ptr)
    {
        if (!L_ || !name || !ptr) return;
        auto* ref = static_cast<var_ref*>(lua_newuserdata(L_, sizeof(var_ref)));
        ref->ptr = ptr;
        ref->type = 'd';
        luaL_setmetatable(L_, "xray_double");
        lua_setglobal(L_, name);
    }

    void bind_string(const char* name, char* buf, size_t len)
    {
        if (!L_ || !name || !buf || len == 0) return;
        auto* ref = static_cast<string_ref*>(lua_newuserdata(L_, sizeof(string_ref)));
        ref->buf = buf;
        ref->max_len = len;
        luaL_setmetatable(L_, "xray_str");
        lua_setglobal(L_, name);
    }

    void bind_struct(const char* name, void* base, const struct_field* fields, int count)
    {
        if (!L_ || !name || !base || !fields || count <= 0) return;
        auto* ref = static_cast<struct_ref*>(lua_newuserdata(L_, sizeof(struct_ref)));
        ref->base = base;
        ref->fields = fields;
        ref->count = count;
        luaL_setmetatable(L_, "xray_struct");
        lua_setglobal(L_, name);
    }

    std::string list_globals()
    {
        if (!L_) return "[]\n";

        std::string result;
        result += "[\n";
        bool first = true;

        lua_pushglobaltable(L_);
        lua_pushnil(L_);
        while (lua_next(L_, -2) != 0) {
            if (lua_type(L_, -1) == LUA_TUSERDATA) {
                const char* name = lua_tostring(L_, -2);
                if (!name) { lua_pop(L_, 1); continue; }

                if (lua_getmetatable(L_, -1)) {
                    lua_pushstring(L_, "__name");
                    lua_rawget(L_, -2);

                    if (lua_isstring(L_, -1)) {
                        const char* type_str = lua_tostring(L_, -1);

                        // Get string value via __tostring
                        std::string val;
                        lua_pushvalue(L_, -4);
                        if (luaL_callmeta(L_, -1, "__tostring")) {
                            const char* s = lua_tostring(L_, -1);
                            if (s) val = s;
                            lua_pop(L_, 1);
                        } else {
                            lua_pop(L_, 1);
                        }

                        if (!first) result += ",\n";
                        first = false;
                        result += "  {\"name\":\"" + escape_json(name) +
                                  "\",\"type\":\"" + escape_json(type_str) +
                                  "\",\"value\":\"" + escape_json(val) + "\"}";
                    }
                    lua_pop(L_, 2);
                }
            }
            lua_pop(L_, 1);
        }
        lua_pop(L_, 1);

        result += "\n]\n";
        return result;
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

    struct string_ref {
        char* buf;
        size_t max_len;
    };

    struct struct_ref {
        void* base;
        const struct_field* fields;
        int count;
    };

    static std::string escape_json(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 4);
        for (char c : s) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    }

    lua_State* L_ = nullptr;
    int instruction_count_ = 0;
    void (*on_terminate_)() = nullptr;
    void (*on_pause_)() = nullptr;
    void (*on_continue_)() = nullptr;
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
        // Scalar double
        create_scalar_mt("xray_double", 'd');
        // String (char buffer)
        create_string_mt();
        // Struct (composite object)
        create_struct_mt();
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
                case 'd': lua_pushnumber(L, *static_cast<double*>(ref->ptr)); break;
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
                case 'd': *static_cast<double*>(ref->ptr) = static_cast<double>(luaL_checknumber(L, 3)); break;
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
                case 'd': std::snprintf(buf, sizeof(buf), "%.14g", *static_cast<double*>(ref->ptr)); break;
                case 'b': std::snprintf(buf, sizeof(buf), "%s", *static_cast<bool*>(ref->ptr) ? "true" : "false"); break;
                default: return 0;
            }
            lua_pushstring(L, buf);
            return 1;
        });
        lua_setfield(L_, -2, "__tostring");

        lua_pop(L_, 1);
    }

    void create_string_mt()
    {
        luaL_newmetatable(L_, "xray_str");

        lua_pushstring(L_, "str");
        lua_setfield(L_, -2, "__name");

        // __index — read null-terminated buffer as Lua string
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<string_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->buf) return 0;
            lua_pushstring(L, ref->buf);
            return 1;
        });
        lua_setfield(L_, -2, "__index");

        // __newindex — write from Lua string, truncate to buffer size
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<string_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->buf) return 0;
            const char* s = luaL_checkstring(L, 3);
            if (!s) return 0;
            size_t slen = strlen(s);
            size_t copy_len = (slen >= ref->max_len) ? ref->max_len - 1 : slen;
            memcpy(ref->buf, s, copy_len);
            ref->buf[copy_len] = '\0';
            return 0;
        });
        lua_setfield(L_, -2, "__newindex");

        // __tostring
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<string_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->buf) return 0;
            lua_pushstring(L, ref->buf);
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

    void create_struct_mt()
    {
        luaL_newmetatable(L_, "xray_struct");

        lua_pushstring(L_, "struct");
        lua_setfield(L_, -2, "__name");

        // __index — lookup field by name, read value
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<struct_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->base) return 0;
            const char* key = luaL_checkstring(L, 2);
            if (!key) return 0;
            for (int i = 0; i < ref->count; ++i) {
                if (std::strcmp(key, ref->fields[i].name) != 0) continue;
                auto& f = ref->fields[i];
                void* addr = static_cast<char*>(ref->base) + f.offset;
                switch (f.type) {
                    case 'i': lua_pushinteger(L, *static_cast<int*>(addr)); return 1;
                    case 'f': lua_pushnumber(L, *static_cast<float*>(addr)); return 1;
                    case 'd': lua_pushnumber(L, *static_cast<double*>(addr)); return 1;
                    case 'b': lua_pushboolean(L, *static_cast<bool*>(addr)); return 1;
                    case 's': lua_pushstring(L, static_cast<char*>(addr)); return 1;
                }
                return 0;
            }
            return 0;
        });
        lua_setfield(L_, -2, "__index");

        // __newindex — lookup field by name, write value
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<struct_ref*>(lua_touserdata(L, 1));
            if (!ref || !ref->base) return 0;
            const char* key = luaL_checkstring(L, 2);
            if (!key) return 0;
            for (int i = 0; i < ref->count; ++i) {
                if (std::strcmp(key, ref->fields[i].name) != 0) continue;
                auto& f = ref->fields[i];
                void* addr = static_cast<char*>(ref->base) + f.offset;
                switch (f.type) {
                    case 'i': *static_cast<int*>(addr) = static_cast<int>(luaL_checkinteger(L, 3)); return 0;
                    case 'f': *static_cast<float*>(addr) = static_cast<float>(luaL_checknumber(L, 3)); return 0;
                    case 'd': *static_cast<double*>(addr) = static_cast<double>(luaL_checknumber(L, 3)); return 0;
                    case 'b': *static_cast<bool*>(addr) = lua_toboolean(L, 3) != 0; return 0;
                    case 's': {
                        const char* s = luaL_checkstring(L, 3);
                        if (!s) return 0;
                        size_t slen = std::strlen(s);
                        size_t copy = (slen >= f.str_len) ? f.str_len - 1 : slen;
                        std::memcpy(addr, s, copy);
                        static_cast<char*>(addr)[copy] = '\0';
                        return 0;
                    }
                }
                return 0;
            }
            return 0;
        });
        lua_setfield(L_, -2, "__newindex");

        // __pairs — iterate fields
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<struct_ref*>(lua_touserdata(L, 1));
            if (!ref) return 0;
            // Returns: next_func, state (userdata), initial_index (0)
            lua_pushcfunction(L, [](lua_State* L2) -> int {
                auto* r = static_cast<struct_ref*>(lua_touserdata(L2, 1));
                if (!r) return 0;
                int idx = static_cast<int>(lua_tointeger(L2, 2));
                if (idx >= r->count) return 0;
                lua_pushstring(L2, r->fields[idx].name);
                void* addr = static_cast<char*>(r->base) + r->fields[idx].offset;
                switch (r->fields[idx].type) {
                    case 'i': lua_pushinteger(L2, *static_cast<int*>(addr)); break;
                    case 'f': lua_pushnumber(L2, *static_cast<float*>(addr)); break;
                    case 'd': lua_pushnumber(L2, *static_cast<double*>(addr)); break;
                    case 'b': lua_pushboolean(L2, *static_cast<bool*>(addr)); break;
                    case 's': lua_pushstring(L2, static_cast<char*>(addr)); break;
                }
                return 2;
            });
            lua_pushvalue(L, 1);
            lua_pushinteger(L, 0);
            return 3;
        });
        lua_setfield(L_, -2, "__pairs");

        // __tostring
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* ref = static_cast<struct_ref*>(lua_touserdata(L, 1));
            if (!ref) return 0;
            std::string r = "{";
            for (int i = 0; i < ref->count; ++i) {
                if (i > 0) r += ", ";
                r += ref->fields[i].name;
                r += "=";
                void* addr = static_cast<char*>(ref->base) + ref->fields[i].offset;
                char buf[64];
                switch (ref->fields[i].type) {
                    case 'i': std::snprintf(buf, sizeof(buf), "%d", *static_cast<int*>(addr)); break;
                    case 'f': std::snprintf(buf, sizeof(buf), "%.14g", *static_cast<float*>(addr)); break;
                    case 'd': std::snprintf(buf, sizeof(buf), "%.14g", *static_cast<double*>(addr)); break;
                    case 'b': std::snprintf(buf, sizeof(buf), "%s", *static_cast<bool*>(addr) ? "true" : "false"); break;
                    case 's': std::snprintf(buf, sizeof(buf), "%s", static_cast<char*>(addr)); break;
                }
                r += buf;
            }
            r += "}";
            lua_pushstring(L, r.c_str());
            return 1;
        });
        lua_setfield(L_, -2, "__tostring");

        lua_pop(L_, 1);
    }
};

} // namespace xb
