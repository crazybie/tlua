#include "stdafx.h"
#include "tlua.h"

namespace tlua
{
    LuaMgr* LuaMgr::instance = nullptr;
    lua_State* LuaObj::L = nullptr;


    LuaMgr::LuaMgr()
    {
        instance = this;
        L = luaL_newstate();

        luaL_openlibs(L);

#ifndef TLUA_NO_SOCKET
        luaopen_socket_core(L);
#endif

        LuaRef loaders = getGlobal("package")["loaders"];
        if (loaders.type() != LUA_TTABLE) {
            loaders = (LuaRef)getGlobal("package")["searchers"];
        }
        loaders.append(&luaLoader);

        setGlobal("__traceback", &traceback);

        //lua_gc(L, LUA_GCSETSTEPMUL, 1);

        logError = [](const string& err) {
            fprintf(stderr, "LUA ERROR: %s\n", err.c_str());
        };
        fileLoader = loadFile;

        auto setupType = doString(R"(
            function string.split(s, p)
                local r = {}                
                string.gsub(s, p, function(c) r[#r+1] = c end)
                return r
            end

            local function setupOverloads(class) 
                local o = {}
                for k,v in pairs(class) do
                    if type(v)=='function' then
                        local p = string.split(k, '([^#]+)')
                        if #p > 1 then
                            local name, argcnt = p[1], tonumber(p[2])
                            o[name] = o[name] or {}
                            table.insert(o[name], argcnt)
                        end
                    end
                end
                for k,v in pairs(o) do
                    class[k] = function(...)
                        local nargs = select('#',...)
                        for _, needArgs in ipairs(v) do 
                            if nargs == needArgs then
                                return class[k..'#'..nargs](...)
                            end
                        end
                        error('invalid arguments count')
                    end
                end
            end

            local function setupType(typeName)
				local class = _G[typeName]
				local pget = class['__prop_get']
				local pset = class['__prop_set']
                
				class._name = typeName
                if class._lifetime ~= 'cpp' then
                    class.__gc = class.Delete
                end
				
				if class.base then
					local baseName = class.base
					class.base = _G[baseName]
                    assert(class.base, 'base class not exported:'..baseName)                    				
                end
                
				class.__index = function(t, k)
					local f = pget[k]
					if f then return f(t) end
					local base = class.base
					return class[k] or (base and base.__index(t, k))
				end
				
				class.__newindex = function(t,k,v)
					local f = pset[k]
					local c = class
					while not f and c.base do 
						c = c.base
						f = c.__prop_set[k]
					end
					if f then return f(t,v) end
				end
                
				setupOverloads(class)

                local classMt = {}
                if class.New then
                    classMt.__call = function(class, ...)
                        return class.New(...)
                    end
                end
				setmetatable(class, classMt)
            end
              
            return setupType 
        )");

        for (auto i : getRegisters()) { i.second(); }
        for (auto i : getRegisters()) { setupType.call(i.first); }
    }

    void LuaMgr::setSourceRoot(string luaRoot /*= ""*/)
    {
        srcDir = luaRoot;
    }

    LuaMgr::~LuaMgr()
    {
        lua_close(L);
        L = nullptr;
    }

    std::vector<std::pair<std::string, tlua::LuaMgr::Register>>& LuaMgr::getRegisters()
    {
        static std::vector<std::pair<std::string, tlua::LuaMgr::Register>> registers;
        return registers;
    }

    tlua::LuaRef LuaMgr::doFile(const char *name)
    {
        auto cmd = string("return require('") + name + "')";
        if (luaL_loadstring(L, cmd.c_str())) {
            logError(lua_tostring(L, -1));
            return LuaRef();
        }
        return FuncHelper::callLua<LuaRef>();
    }

    tlua::LuaRef LuaMgr::doString(const char* name)
    {
        if (luaL_loadstring(L, name)) {
            logError(lua_tostring(L, -1));
            return LuaRef();
        }
        return FuncHelper::callLua<LuaRef>();
    }

    tlua::LuaRef LuaMgr::newTable()
    {
        lua_newtable(L);
        return LuaRef::fromStack();
    }

    tlua::LuaRef LuaMgr::getGlobal(const char* name)
    {
        lua_getglobal(L, name);
        return LuaRef::fromStack();
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
        auto ignoreFuncStackCnt = 2;// debug.traceback + __traceback        
        instance->logError(instance->getCallStack(msg, ignoreFuncStackCnt));
    }

    const char* LuaMgr::getCallStack(const char* msg, int ignoreFuncStackCnt)
    {
        auto stack = instance->getGlobal("debug")["traceback"].call<const char*>(msg, ignoreFuncStackCnt);
        return stack;
    }

    int LuaMgr::luaLoader(lua_State* L)
    {
        string requireFile = lua_tostring(L, -1);
        while (auto c = strchr(&requireFile[0], '.')) *c = '/';
        requireFile += ".lua";
        auto filePath = instance->srcDir + "/" + requireFile;
        auto chunk = instance->loadFile(filePath.c_str());
        if (chunk.size() == 0) {
            instance->logError(Sprintf("can not get file data of %s", filePath.c_str()).c_str());
            return 0;
        }
        auto err = luaL_loadbuffer(L, chunk.data(), chunk.size(), requireFile.c_str());
        if (err == LUA_ERRSYNTAX) {
            instance->logError(Sprintf("syntax error in %s", filePath.c_str()).c_str());
            return 0;
        }
        return 1;
    }

    void LuaRefBase::iniFromStack()
    {
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    LuaRefBase::~LuaRefBase()
    {
        if (m_ref != LUA_REFNIL && L)
            luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
    }

    void LuaRefBase::push() const
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref);
    }

    int LuaRefBase::type() const
    {
        if (m_ref == LUA_REFNIL) return LUA_TNIL;
        PopOnExit p;
        push();
        return lua_type(L, -1);
    }

    int LuaRefBase::createRef() const
    {
        if (m_ref == LUA_REFNIL) return LUA_REFNIL;
        push();
        return luaL_ref(L, LUA_REGISTRYINDEX);
    }

    void LuaRefBase::pop()
    {
        luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
        m_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    bool LuaRefBase::isNil() const
    {
        return type() == LUA_TNIL;
    }

    LuaRefBase::operator bool() const
    {
        return !isNil();
    }

    int LuaRefBase::length() const
    {
        PopOnExit p{};
        push();
        return (int)lua_objlen(L, -1);
    }

    TableProxy::TableProxy(int tableRef) : m_tableRef(tableRef)
    {
        iniFromStack();
    }

    TableProxy::TableProxy(TableProxy&& other)
    {
        m_tableRef = other.m_tableRef;
        m_ref = other.m_ref;
        other.m_ref = LUA_REFNIL;
    }

    void TableProxy::push() const
    {
        lua_rawgeti(L, LUA_REGISTRYINDEX, m_tableRef);
        lua_rawgeti(L, LUA_REGISTRYINDEX, m_ref);
        lua_gettable(L, -2);
        lua_remove(L, -2); // remove the table
    }

    LuaRef::LuaRef(LuaRef&& other)
    {
        m_ref = other.m_ref;
        other.m_ref = LUA_REFNIL;
    }

    LuaRef::LuaRef(TableProxy const& other)
    {
        m_ref = other.createRef();
    }

    LuaRef::LuaRef(LuaRef const& other)
    {
        m_ref = other.createRef();
    }

    tlua::LuaRef LuaRef::fromIndex(int index)
    {
        lua_pushvalue(L, index);
        return fromStack();
    }

    tlua::LuaRef LuaRef::fromStack()
    {
        LuaRef r;
        r.iniFromStack();
        return r;
    }

    tlua::LuaRef& LuaRef::operator=(LuaRef&& other)
    {
        luaL_unref(L, LUA_REGISTRYINDEX, m_ref);
        m_ref = other.m_ref;
        other.m_ref = LUA_REFNIL;
        return *this;
    }

    tlua::Iterator LuaRef::begin() const
    {
        return Iterator(*this);
    }

    tlua::Iterator LuaRef::end() const
    {
        return Iterator();
    }

    Iterator::Iterator(const LuaRef& table) : m_table(table)
    {
        next();
    }

    tlua::Iterator& Iterator::operator++()
    {
        if (valid) next();
        return *this;
    }

    bool Iterator::operator!=(const Iterator& r) const
    {
        return !(*this == r);
    }

    bool Iterator::operator==(const Iterator& r) const
    {
        if (!valid && !r.valid) return true;
        return false;
    }

    std::pair<tlua::LuaRef, tlua::LuaRef> Iterator::operator*()
    {
        return std::pair<LuaRef, LuaRef>(m_key, m_value);
    }

    void Iterator::next()
    {
        m_table.push();
        m_key.push();
        valid = false;
        if (lua_next(L, -2)) {
            valid = true;
            m_value.pop();
            m_key.pop();
        }
        lua_pop(L, 1);
    }

}
