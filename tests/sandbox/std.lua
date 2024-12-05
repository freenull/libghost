local ghost = require("ghost")

local f = io.popen("yes")
print("GOT YES FILE:", f)
print("yes out: ", f:read(3))
print("one yes line: ", f:read("*l"))
print("one yes line: ", f:read("*l"))
print("one yes line: ", f:read("*l"))
f:close()
print("YES FILE:", f)

print("os.execute =", os.execute)
local result = os.execute("echo one")
print("os.execute result:", result)

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
