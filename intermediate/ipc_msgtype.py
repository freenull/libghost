# TODO TEMPORARILY SHELVED
# May get used in the future

# see tools/codegen.py and root CMakeLists.txt 'GhostCodegenTarget' function

### entries

c(
"name",      "value",       "desc",
"shortname", "data_fields", "va_data"
)

e(
"PING",      None,          "Test ping message",
"ping",      ["int a"],     None,

"PONG",      None,          "Test pong message",
"pong",      ["int b"],     [ "char", "a" ]
)

### gh_ipcmsg_type.h

o("header")
n()

w("#include <string.h>")
w("#include <ghost/result.h>")
w("#include <ghost/alloc.h>")
b()

lastvalue = -1

w("typedef enum {")
with ind():
    for s in i():
        value = s("value")
        if value is None: value = lastvalue + 1

        w("GH_IPCMSG_", s("name"), " = ", value, ",")

        lastvalue = value
w("} gh_ipcmsg_type;")

b()
w("typedef struct {")
with ind():
    w("gh_ipcmsg_type type;")
    w("size_t size;")
    w("char _data[];")
w("} gh_ipcmsg;")

for s in i():
    b()
    w("// GH_IPCMSG_", s("name"))
    b()
    w("typedef struct {")

    structname = "gh_ipcmsg_" + s("shortname")

    va_data = s("va_data")
    is_va = va_data is not None

    if is_va:
        va_type = va_data[0]
        va_name = va_data[1]
        va_elem_size = f"sizeof({va_type})"

    with ind():
        w("gh_ipcmsg_type type;")
        w("size_t size;")
        for field in s("data_fields"):
            w(field, ";")
        if is_va:
            w(va_type, " ", va_name, "[];")
    w("} ", structname, ";")
    b()

    a("__attribute__((always_inline)) static inline ")
    w(structname, " * gh_ipcmsg_", s("shortname"), "_get(gh_ipcmsg * msg) {")
    with ind():
        w("if (msg->type != GH_IPCMSG_", s("name"), ") return NULL;")
        w("return (", structname, " *)msg;")
    w("}")

    a(
        "__attribute__((always_inline)) static inline ",
        "size_t gh_ipcmsg_",
        s("shortname"), "_size ("
    )
    
    if is_va:
        w("size_t count) { return sizeof(", structname, ") + (count * ", va_elem_size, "); }")
    else:
        w("void) { return sizeof(", structname, "); }")

    b()
    a("__attribute__((always_inline)) static inline ")
    # a(structname, " * gh_ipcmsg_", s("shortname"), "_ctor(", structname, " * msg")
    a("void gh_ipcmsg_", s("shortname"), "_ctor(", structname, " * msg")

    for field in s("data_fields"):
        a(", ", field)

    if is_va:
        a(", ", va_type, " * ", va_name)
        a(", size_t ", va_name, "_count")

    w(") {")
    with ind():
        w("msg->type = GH_IPCMSG_", s("name"), ";")
        a("msg->size = sizeof(", structname, ")")
        if is_va:
            a(" + ", va_name, "_count")
        w(";")

        for field in s("data_fields"):
            field_name = field.split(" ")[-1]
            w("msg->", field_name, " = ", field_name, ";");
        if is_va:
            w("memmove(msg->", va_name, ", ", va_name, ", ", va_name, "_count);")
    w("}")

    b()
    a("__attribute__((always_inline)) static inline ")
    # a(structname, " * gh_ipcmsg_", s("shortname"), "_ctor(", structname, " * msg")
    a("gh_result gh_ipcmsg_", s("shortname"), "_new(", structname, " ** out_msg, gh_alloc * allocator")

    for field in s("data_fields"):
        a(", ", field)

    if is_va:
        a(", ", va_type, " * ", va_name)
        a(", size_t ", va_name, "_count")

    w(") {")
    with ind():
        a("gh_result alloc_result = gh_alloc_new(allocator, (void**)out_msg, gh_ipcmsg_", s("shortname"), "_size(")
        if is_va:
            a(va_name, "_count")
        
        w("));")
        w("if (alloc_result != GHR_OK) return alloc_result;")
        w("if (*out_msg == NULL) return ghr_errno(GHR_IPC_MSGNEWFAIL);")
        a("gh_ipcmsg_", s("shortname"), "_ctor(*out_msg")
        for field in s("data_fields"):
            field_name = field.split(" ")[-1]
            a(", ", field_name)

        if is_va:
            a(", ", va_name)
            a(", ", va_name, "_count")
        w(");")
        w("return GHR_OK;")
    w("}")


### gh_ipcmsg_type.c

# o("source")
# w("#include \"", rel("header"), "\"")
# for s in i():
#     b()
#     structname = "gh_ipc_msgdata_" + s("shortname")

#     w(structname, " * gh_ipc_msg_", s("shortname"), "get(gh_ipcmsg * msg) {")
#     with ind():
#         w("if (msg->type != GH_IPCMSG_", s("name"), ") return NULL;")
#         w("return (", structname, " *)msg;")
#     w("}")

#     va_elem_size = s("variable_payload_entry_size")
#     is_va = va_elem_size is not None

#     if is_va:
#         w(", size_t count);")
#     else:
#         w(") { return sizeof(", structname, "); }")
