local ghost = require("ghost")
local ffi = require("ffi")

local udptr = ghost._udptr

ffi.cdef[[
    static const int ESPIPE = 29;
    char *strerror(int errnum);

    typedef ssize_t off_t;

    static const int O_RDONLY = 0;
    static const int O_WRONLY = 1;
    static const int O_RDWR = 2;
    static const int O_CREAT = 64;
    static const int O_TRUNC = 512;
    static const int O_APPEND = 1024;

    static const int SEEK_SET = 0;
    static const int SEEK_CUR = 1;
    static const int SEEK_END = 2;

    static const int EOF = -1;

    void * malloc(size_t size);
    void * realloc(void * ptr, size_t new_size);
    void free(void * ptr);

    typedef void FILE;

    FILE * stdin;
    FILE * stdout;
    FILE * stderr;

    FILE *fdopen(int fd, const char *mode);
    size_t fread(char * ptr, size_t size, size_t nmemb, FILE *restrict stream);
    size_t fwrite(const char * ptr, size_t size, size_t nmemb, FILE *restrict stream);
    ssize_t getline(char **restrict lineptr, size_t *restrict n, FILE *restrict stream);
    int fflush(FILE * stream);
    int fseek(FILE *stream, long offset, int whence);
    long ftell(FILE *stream);
    int fflush(FILE * stream);
    int fscanf(FILE *restrict stream, const char *restrict format, ...);


    static const int _IOFBF = 0;
    static const int _IOLBF = 1;
    static const int _IONBF = 2;
    int setvbuf(FILE *restrict stream, char * buf, int mode, size_t size);
    bool feof(FILE *stream);
    int fclose(FILE *stream);

    ssize_t read(int fd, char * buf, size_t count);
    ssize_t write(int fd, const char * buf, size_t count);
    off_t lseek(int fildes, off_t offset, int whence);
    int fsync(int fd);
    int close(int fd);

    static const int PROT_READ = 1;
    static const int MAP_PRIVATE = 2;

    void *mmap(void * addr, size_t length, int prot, int flags, int fd, off_t offset);
    int munmap(void * addr, size_t length);

    struct file {
        FILE * _ptr;
        // -- int _fd;
    };
]]

local MAP_FAILED = ffi.cast("void*", -1)

local function c_error(val)
    if val == nil then
        val = ffi.errno()
    end
    return ffi.string(ffi.C.strerror(val))
end

local ghost_io = {}
ghost.io = ghost_io

local ghost_os = {}
ghost.os = ghost_os

local ghfile_vtable = {}

local function check_fileptr(ptr)
    if ptr == nil then
        error("attempt to use a closed file")
    end
end

function ghfile_vtable.write(file, str)
    check_fileptr(file._ptr)
    if str == "" then return end

    writen = ffi.C.fwrite(str, 1, #str, file._ptr)
    if writen == 0 then
        if not ffi.C.feof(file._ptr) then
            error("failed writing to file")
        end
    end
end

function ghfile_vtable.flush(file, str)
    check_fileptr(file._ptr)
    ffi.C.fflush(file._ptr)
end

local LINE_BUF_SIZE = 1024
function ghfile_vtable.lines(file, str)
    check_fileptr(file._ptr)

    return function()
        local buffer = ffi.new("char *[1]")
        local buffer_size = ffi.new("size_t[1]")
        local readn = ffi.C.getline(buffer, buffer_size, file._ptr)

        if readn < 0 then
            ffi.C.free(buffer[0])
            local errno = ffi.errno()
            if ffi.C.feof(file._ptr) then
                return nil
            else
                error(c_error(errno))
            end
        end

        local size = readn
        if size > 0 and buffer[0][size - 1] == string.byte("\n") then
            size = size - 1
        end
        local s = ffi.string(buffer[0], size)
        ffi.C.free(buffer[0])
        return s
    end
end

function ghfile_vtable.seek(file, whence, offs)
    check_fileptr(file._ptr)
    c_whence = nil
    if whence == "set" then c_whence = ffi.C.SEEK_SET end
    if whence == "cur" then c_whence = ffi.C.SEEK_CUR end
    if whence == "end" then c_whence = ffi.C.SEEK_END end

    if c_whence == nil then
        error("invalid whence parameter: " .. tostring(whence))
    end

    ffi.C.fseek(file._ptr, offs or 0, c_whence)

    return ffi.C.ftell(file._ptr)
end

local READBUFFER_INITIALSIZE = 512
function ghfile_vtable.read(file, format)
    check_fileptr(file._ptr)

    if type(format) == "number" then
        local buf = ffi.new("char[?]", format)
        local nread = ffi.C.fread(buf, 1, format, file._ptr)
        return ffi.string(buf, nread)
    elseif format:sub(1, 2) == "*l" then
        for line in file:lines() do
            return line
        end
    elseif format:sub(1, 2) == "*n" then
        local num = ffi.new("double[1]")
        local nread = ffi.C.fscanf(file._ptr, "%lf", num)
        if nread < 0 then
            error(c_error())
        end
        if nread == 0 then
            return nil
        end
        return num[0]
    elseif format:sub(1, 2) == "*a" then
        local ptr = file._ptr

        res = ffi.C.fseek(ptr, 0, ffi.C.SEEK_END)
        local errno = ffi.errno()
        if res < 0 and errno == ffi.C.ESPIPE then
            local buf_size = READBUFFER_INITIALSIZE
            -- seeking unsupported, go with less efficient buffer building
            local buf = ffi.cast("char *", ffi.C.malloc(buf_size))
            if buf == nil then
                error("failed allocating memory for read buffer: " .. c_error())
            end

            local len = 0

            while true do
                local nread = ffi.C.fread(buf, 1, buf_size - len, ptr)
                len = len + nread
                if nread < buf_size - len then break end

                if len > buf_size then
                    buf_size = buf_size * 2
                    local new_buf = ffi.cast("char *", ffi.C.realloc(buf, buf_size))

                    if new_buf == nil then
                        ffi.free(buf)
                        error("failed allocating memory for read buffer: " .. c_error())
                    end

                    buf = new_buf
                end
            end

            local str = ffi.string(buf, len)
            ffi.C.free(buf)
            return str
        elseif res < 0 then
            error("failed seeking file: " .. c_error(errno))
        else
            local len = ffi.C.ftell(ptr)
            ffi.C.fseek(ptr, 0, ffi.C.SEEK_SET)

            local buf = ffi.new("char[?]", len)
            ffi.C.fread(buf, 1, len, ptr)

            return ffi.string(buf, len)
        end
    else
        error("unknown file:read format: " .. tostring(format))
    end
end

local VBUF_DEFAULT_SIZE = 1024
function ghfile_vtable.setvbuf(file, mode, size)
    check_fileptr(file._ptr)

    if mode == "no" then
        ffi.C.setvbuf(file._ptr, NULL, ffi.C._IONBF, 0)
    elseif mode == "full" then
        ffi.C.setvbuf(file._ptr, NULL, ffi.C._IOFBF, size or VBUF_DEFAULT_SIZE)
    elseif mode == "line" then
        ffi.C.setvbuf(file._ptr, NULL, ffi.C._IOLBF, size or VBUF_DEFAULT_SIZE)
    else
        error("unknown setvbuf mode: " .. tostring(mode))
    end
end

function ghfile_vtable.close(file)
    check_fileptr(file._ptr)
    if ffi.C.fclose(file._ptr) == ffi.C.EOF then
        error("failed closing file: " .. c_error())
    end
    file._ptr = nil
end

local ghfile_mt = {
    __index = ghfile_vtable,
    __gc = function(self)
        if self == ghost_io.stdin then
            return
        end

        if self == ghost_io.stdout then
            return
        end

        if self == ghost_io.stderr then
            return
        end

        if self._ptr ~= nil then
            ghfile_vtable.flush(self)
            ghfile_vtable.close(self)
        end
    end
}

function ghfile_mt.__tostring(self)
    return "file (0x" .. tostring(self._ptr):sub(18) .. ")"
end

local FILE = ffi.metatype("struct file", ghfile_mt)

local function c_fopenmode(read, write, append, binary)
    local c_mode = "r"

    if     read and write then c_mode = "w+"
    elseif read           then c_mode = "r"
    elseif write          then c_mode = "w"
    end

    if append then
        if c_mode == "r" then
            c_mode = "a+"
        else
            c_mode = "a"
        end
    end

    if binary then
        c_mode = c_mode .. "b"
    end

    return c_mode
end

local function c_fopenmode(mode)
    local suffix = ""
    if mode:sub(-1) == "b" then
        mode = mode:sub(1, -2)
        suffix = "b"
    end

    if     mode == "r"  then return mode .. suffix
    elseif mode == "r+" then return mode .. suffix
    elseif mode == "a"  then return mode .. suffix
    elseif mode == "w"  then return mode .. suffix
    elseif mode == "w+" then return mode .. suffix
    elseif mode == "a+" then return mode .. suffix
    else   error("unknown open mode: " .. tostring(mode))
    end
end

local function c_openmode(mode)
    if mode:sub(-1) == "b" then
        mode = mode:sub(1, -2)
    end

    if     mode == "r"  then return ffi.C.O_RDONLY
    elseif mode == "r+" then return ffi.C.O_RDWR
    elseif mode == "a"  then return ffi.C.O_WRONLY + ffi.C.O_APPEND + ffi.C.O_CREAT
    elseif mode == "w"  then return ffi.C.O_WRONLY + ffi.C.O_CREAT
    elseif mode == "w+" then return ffi.C.O_RDWR + ffi.C.O_TRUNC + ffi.C.O_CREAT
    elseif mode == "a+" then return ffi.C.O_RDWR + ffi.C.O_APPEND + ffi.C.O_CREAT
    else   return nil, c_error()
    end
end

local DEFAULT_FILE_MODE = 420 -- 0644

ghost_io.stdin = FILE(ffi.C.stdin)
ghost_io.stdout = FILE(ffi.C.stdout)
ghost_io.stderr = FILE(ffi.C.stderr)

local default_input_file = ghost_io.stdin
local default_output_file = ghost_io.stdout

ghost_io.close = function(file)
    if file == nil then
        return default_output_file:close()
    end

    return file:close()
end

ghost_io.flush = function()
    default_output_file:flush()
end

ghost_io.input = function(file)
    if file == nil then
        return default_input_file
    elseif type(file) == "string" then
        default_input_file = ghost_io.open(file, "r")
    elseif type(file) == "cdata" and ffi.typeof(file) == FILE then
        default_input_file = file
    else
        error("bar argument #1 to 'input' (neither a path nor a file handle: " .. tostring(file) .. ")")
    end
end

ghost_io.lines = function(filename)
    local f = default_input_file

    if filename ~= nil then
        local err
        f, err = ghost_io.open(filename, "r")
        if f == nil then
            error("bad argument #1 to 'lines' (" .. err .. ")")
        end
    end

    local iter = f:lines()

    -- print("CREATEITER", f)
    -- print("CREATEITER", f._ptr)

    return function()
        local line = iter()
        -- if line == nil and filename ~= nil then
        --     f:close()
        -- end
        return line
    end
end

ghost_io.open = function(path, mode)
    local open_mode, err = c_openmode(mode)

    if open_mode == nil then
        return nil, err
    end

    local ok, fd = pcall(function()
        return ghost.call("ghost.open", "int", path, ffi.new("int", open_mode), ffi.new("int", DEFAULT_FILE_MODE))
    end)

    if not ok then
        return nil, fd
    end

    if fd < 0 then
        return ni, "got fd < 0 from open"
    end

    local fileptr = ffi.C.fdopen(fd, c_fopenmode(mode))
    return FILE(fileptr)
end

ghost_io.output = function(file)
    if file == nil then
        return default_output_file
    elseif type(file) == "string" then
        default_output_file = ghost_io.open(file, "w+")
    elseif type(file) == "cdata" and ffi.typeof(file) == FILE then
        default_output_file = file
    else
        error("bar argument #1 to 'output' (neither a path nor a file handle: " .. tostring(file) .. ")")
    end
end

ghost_io.popen = function()
    error("popen is currently not supported")
end

ghost_io.read = function(...)
    return ghost_io.input():read(...)
end

local tmp_files = {}

ghost_io.tmpfile = function(prefix)
    prefix = tostring(prefix)

    local ok, path, fd = pcall(function()
        return ghost.call("ghost.opentemp", "string", prefix or "", ffi.new("int", ffi.C.O_RDONLY + ffi.C.O_CREAT))
    end)

    if not ok then
        return nil, path
    end

    if fd < 0 then
        return nil, "got fd < 0 from opentemp"
    end

    local fileptr = ffi.C.fdopen(fd, "r+")
    local file = FILE(fileptr)

    local tmp_proxy = newproxy(true)
    local tmp_meta = getmetatable(tmp_proxy)
    tmp_meta.__gc = function(self)
        ghost_os.remove(path)
        -- TODO UNLINK TEMP FILE
    end
    table.insert(tmp_files, tmp_proxy)
    
    return file
end

ghost_io.type = function(obj)
    if type(obj) == "cdata" and ffi.typeof(obj) == FILE then
        if obj._ptr == nil then
            return "closed file"
        else
            return "file"
        end
    else
        return nil
    end
end

ghost_io.write = function(...)
    return ghost_io.output():write(...)
end

local function unimplemented()
    error("unimplemented")
end

local lua_os = require("os")

ghost.os.clock = unimplemented
ghost.os.date = unimplemented
ghost.os.difftime = unimplemented
ghost.os.execute = unimplemented

ghost.os.exit = os.exit
ghost.os.getenv = os.getenv

ghost.os.remove = function(path)
    local ok, fd = pcall(function()
        return ghost.call("ghost.remove", nil, path)
    end)

    if not ok then
        return nil, fd
    end

    return true
end

ghost.os.rename = unimplemented
ghost.os.setlocale = os.setlocale
ghost.os.time = unimplemented

ghost.os.tmpname = function(prefix)
    prefix = tostring(prefix)

    local ok, path, fd = pcall(function()
        return ghost.call("ghost.opentemp", "string", prefix or "", ffi.new("int", ffi.C.O_CREAT))
    end)

    if not ok then
        return nil, fd, path
    end

    if fd < 0 then
        return nil, "got fd < 0 from opentemp"
    end

    if ffi.C.close(fd) < 0 then
        error("failed closing temp file in os.tmpname: " .. c_error())
    end

    return path
end

io = ghost_io
package.loaded.io = ghost_io

os = ghost_os
package.loaded.os = ghost_os
