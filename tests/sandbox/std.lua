local ghost = require("ghost")

local fx, err = io.tmpfile("foo-")
print(fx, err)
fx:write("HELLO TMP\n")
fx:close()

local f, err = io.open("testfile.txt", "w+")
if err ~= nil then print(err) end
f:write("Hello, world!")
f:close()
