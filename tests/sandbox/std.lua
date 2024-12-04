local ghost = require("ghost")

local f = io.open("/tmp/dane.txt", "w+")
print("F", f)

local hasperms = perm.dirhas("/tmp", nil, "createfile,read,write,unlink,access.u+r,access.u+w,access.g+r,access.o+r")

print("HASPERMS?", hasperms)
print("ASKDIR", perm.askdir("/tmp", nil, "createfile,read,write,unlink,access.u+r,access.u+w,access.g+r,access.o+r"))

local fx, err = io.tmpfile("foo-")
print(fx, err)
fx:write("HELLO TMP\n")
fx:close()

local f, err = io.open("testfile.txt", "w+")
if err ~= nil then print(err) end
f:write("Hello, world!")
f:close()

callbacks = {}
callbacks.luaprint = function(str)
    print("LUAPRINT", str)
    return "Abc"
end
