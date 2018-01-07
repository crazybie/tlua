#include "tlua.h"

namespace tlua 
{

    LuaMgr::LuaMgr()
    {
        get() = this;
        L() = luaL_newstate();

        luaL_openlibs(L());
        luaopen_socket_core(L());

        LuaRef loaders = getGlobal("package")["loaders"];
        if (loaders.type() != LUA_TTABLE) {
            loaders = (LuaRef)getGlobal("package")["searchers"];
        }
        loaders.append(&luaLoader);

        setGlobal("__traceback", &traceback);
        //lua_gc(L(), LUA_GCSETSTEPMUL, 1);
        for (auto i : getRegisters()) {
            i.second();
        }
        logError = [](const string& err) {
            fprintf(stderr, "%s\n", err.c_str());
        };
        fileLoader = loadFile;

        wrapClasses();
    }

    void LuaMgr::wrapClasses()
    {
        auto wrap = doString(R"(
            return function(className)
                local class = _G[className]
                local mt = {}
                if class.New then
                    mt.__call = function(class, ...)
                        return debug.setmetatable(class.New(...), class)
                    end
                end
                if class.base then
                    class.base = _G[class.base]
                    mt.__index = class.base
                end
                class.__index = class
                setmetatable(class, mt)
            end                            
        )");
        for (auto i : getRegisters()) {
            wrap.call(i.first.c_str());
        }
    }

    tlua::LuaRef LuaMgr::doFile(const char *name)
    {
        auto cmd = string("return require('") + name + "')";
        if (luaL_loadstring(L(), cmd.c_str())) {
            logError(lua_tostring(L(), -1));
            return LuaRef();
        }
        return LuaCaller::callLuaFromStack<LuaRef>();
    }

    tlua::LuaRef LuaMgr::doString(const char* name)
    {
        if (luaL_loadstring(L(), name)) {
            logError(lua_tostring(L(), -1));
            return LuaRef();
        }
        return LuaCaller::callLuaFromStack<LuaRef>();
    }

    std::string LuaMgr::loadFile(const char* name)
    {
        string ret;
        if (FILE* f = fopen(name, "rb")) {
            fseek(f, 0, SEEK_END);
            ret.resize(ftell(f));
            fseek(f, 0, SEEK_SET);
            fread((void*)ret.data(), ret.size(), 1, f);
            fclose(f);
        }
        return ret;
    }

    void LuaMgr::traceback(const char* msg)
    {
        auto stack = get()->getGlobal("debug")["traceback"].call<const char*>(msg, 0);
        get()->logError(stack);
    }

    int LuaMgr::luaLoader(lua_State* L)
    {
        string requireFile = lua_tostring(L, -1);
        while (auto c = strchr(&requireFile[0], '.')) *c = '/';
        requireFile += ".lua";
        auto filePath = get()->srcDir + "/" + requireFile;
        auto chunk = get()->loadFile(filePath.c_str());
        if (chunk.size() == 0) {
            get()->logError(string("can not get file data of ") + filePath);
            return 0;
        }
        auto err = luaL_loadbuffer(L, chunk.data(), chunk.size(), requireFile.c_str());
        if (err == LUA_ERRSYNTAX) {
            get()->logError(string("syntax error in ") + filePath);
            return 0;
        }
        return 1;
    }

}
