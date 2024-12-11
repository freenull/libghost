local ghost = require("ghost")

local ok, err = pcall(function()
    local f = io.popen("yes")
    assert(f ~= nil)
    assert(f:read(2) == "y\n")
    assert(f:read("*l") == "y")
    assert(f:read("*l") == "y")
    f:close()

    assert(os.execute("true") == 0)
    assert(os.execute("false") == 256)
    assert(os.execute("exit 42") == 10752)
    assert(os.execute("NON-EXISTING-FOO") == 32512)
end)

if not ok and not string.find(err, "PERMS_REJECTEDPOLICY") then
    error(err)
end

local f = io.open("/tmp/dane.txt", "w+")
print("F", f)

local hasperms = perm.dirhas("/tmp", nil, "createfile,read,write,unlink,access.u+r,access.u+w,access.g+r,access.o+r")

print("HASPERMS?", hasperms)
print("ASKDIR", perm.askdir("/tmp", nil, "createfile,read,write,unlink,access.u+r,access.u+w,access.g+r,access.o+r"))

local fx, err = io.tmpfile("foo-")
print(fx, err)
fx:write("HELLO TMP\n")
fx:close()

local f, err = io.open("/tmp/ghost-std-testfile.txt", "w+")
if err ~= nil then print(err) end
f:write("Hello, world!")
f:close()

ghost.callbacks.luaprint = function(str)
    print("LUAPRINT", str)
    return "Abc"
end
