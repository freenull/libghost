local ghost = require("ghost")
ghost.call("setsecretkey", nil, "root", "changed!")
local key = ghost.call("getsecretkey", "string", 16, "root")
print(key)
print("Hello, world!")

