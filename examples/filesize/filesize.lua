local file_list = {}
for i, v in ipairs(host.argv) do
    if i >= 2 then
        table.insert(file_list, v)
    end
end

local all_have_read = true
for i, file in ipairs(file_list) do
    if not perm.filehas(file, "read") then
        all_have_read = false
        break
    end
end

if not all_have_read then
    perm.askdir(".", nil, "read")
end

local total_size = 0

local succeeded = 0
local failed = 0

for i, file in ipairs(file_list) do
    local f, err = io.open(file, "r")
    if f == nil then
        failed = failed + 1
    else
        local size = f:seek("end", 0)
        total_size = size + total_size
        f:close()
        succeeded = succeeded + 1
    end
end
total_size = tonumber(total_size)

print("TOTAL SIZE OF " .. tostring(succeeded) .. " FILE(s): " .. tostring(total_size) .. " bytes")
if failed > 0 then
    print("FAILED OPENING " .. tostring(failed) .. " FILE(s)")
end
