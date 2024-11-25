local ffi = require('ffi')

-- objects that have to be passed in through the C API (like the IPC pointer or
-- magic fdopen function) are passed through the global table 'c_support'
local c_support = c_support
_G.c_support = nil

local IPC = c_support.ipc

local ghost = {}
package.loaded.ghost = ghost

ffi.cdef[[
    typedef struct {
        uintptr_t addr;
        size_t size;
    } gh_ipcmsg_functioncall_arg;

    typedef uint32_t gh_result;
    void ghr_stringify(char * buf, size_t max_size, gh_result value);

    typedef void gh_ipc;
    gh_result gh_ipc_call(
        gh_ipc * ipc,
        const char * name,
        size_t arg_count,
        gh_ipcmsg_functioncall_arg * args,
        int * return_fd,
        void * return_arg,
        size_t return_arg_size
    );

    char * strcpy(char * restrict dst, const char * restrict src);
    struct FILE * fdopen(int fd, const char * mode);
]]

local function retbuffer_ctype(t, size)
    if t == "string" then
        return "char [" .. tostring(size) .. "]"
    elseif t == "number" then
        return "double[1]"
    elseif t == "boolean" then
        return "bool[1]"
    -- custom
    elseif t == "int" then
        return "int[1]"
    else
        error("can't convert return type: " .. tostring(t))
    end
end

local function retbuffer_read(ret_cdata, t)
    if t == "string" then
        return ffi.string(ret_cdata)
    elseif t == "number" then
        return ret_cdata[0]
    elseif t == "boolean" then
        return ret_cdata[0] == 0 and true or false
    -- custom
    elseif t == "int" then
        return ret_cdata[0]
    else
        error("can't convert return type to lua type: " .. tostring(t))
    end
end

local function argbuffer_args2struct(...)
    local s = ""
    local n = select("#", ...)
    for i = 1, n do
        local a = select(i, ...)
        s = s .. argbuffer_value2structfield(a, i)
    end

    if s == "" then return nil end
    return "struct { " .. s .. " }"
end

local function arg_obj(value, args, i)
    local t = type(value)
    if t == "cdata" then
        t = tostring(ffi.typeof(value))
    end

    if t == "string" then
        args[i].addr = ffi.cast("uintptr_t", value)
        args[i].size = #value + 1
        return nil
    elseif t == "number" then
        local obj = ffi.new("double[1]")
        obj[0] = value
        args[i].addr = ffi.cast("uintptr_t", obj)
        args[i].size = ffi.sizeof("double")
        return obj
    elseif t == "boolean" then
        local obj = ffi.new("bool[1]")
        obj[0] = value
        args[i].addr = ffi.cast("uintptr_t", obj)
        args[i].size = ffi.sizeof("bool")
        return obj
    -- custom
    elseif t == "int" or t == "ctype<int>" then
        local obj = ffi.new("int[1]")
        obj[0] = value
        args[i].addr = ffi.cast("uintptr_t", obj)
        args[i].size = ffi.sizeof("int")
        return obj
    else
        error("can't convert parameter type to lua type: " .. tostring(t))
    end
end

local function handle_ghr(result)
    if result == 0 then
        return
    end

    local buf = ffi.new("char[1024]")
    ffi.C.ghr_stringify(buf, 1024, result)
    error(ffi.string(buf))
end

function ghost.call(name, ret_type, ...)
    local ret_obj = nil
    local ret_size = 0

    if ret_type ~= nil then
        local ctype = retbuffer_ctype(ret_type, 128)

        ret_obj = ffi.new(ctype)
        ret_size = ffi.sizeof(ctype)
    end

    local arg_count = select("#", ...)
    local args = ffi.new("gh_ipcmsg_functioncall_arg[" .. tostring(arg_count) .. "]")

    local gc_protect = {}
    local n = 1
    for i = 1, arg_count do
        local obj = arg_obj(select(i, ...), args, i - 1)
        if obj ~= nil then
            gc_protect[n] = obj
            n = n + 1
        end
    end

    local fd_ret = ffi.new("int[1]")

    local result = ffi.C.gh_ipc_call(IPC, name, arg_count, args, fd_ret, ret_obj, ret_size)
    handle_ghr(result)

    if ret_obj ~= nil then
        local ret_value = retbuffer_read(ret_obj, ret_type)
        if fd_ret[0] ~= -1 then
            return ret_value, fd_ret[0]
        else
            return ret_value
        end
    elseif fd_ret[0] ~= -1 then
        return fd_ret[0]
    end

    return nil
end

ghost._udptr = c_support.udptr
