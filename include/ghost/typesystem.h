/** @defgroup typesystem Typesystem
 *
 * @brief Dynamic runtime C-like typesystem implementation. Currently not used in libghost.
 *
 * @{
 */

#ifndef GHOST_TYPESYSTEM_H
#define GHOST_TYPESYSTEM_H

#include <stdlib.h>
#include <sys/types.h>
#include <ghost/result.h>
#include <ghost/alloc.h>
#include <ghost/byte_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GH_TSTYPE_MAXNAME 256
#define GH_TSTYPE_MAXFIELDS 32

#define GH_TSLIST_INITIALCAPACITY 128
#define GH_TSLIST_MAXCAPACITY 1024 * 1024 * 10

typedef enum {
    GH_TSTYPE_INVALID,
    GH_TSTYPE_PRIMITIVE,
    GH_TSTYPE_STATICARRAY,
    GH_TSTYPE_STRUCT,
    GH_TSTYPE_TAGGEDUNION,
    GH_TSTYPE_POINTER,
    GH_TSTYPE_ARRAYPOINTER,
} gh_tstype_kind;

typedef char gh_ts_typename[GH_TSTYPE_MAXNAME];
typedef char gh_ts_name[GH_TSTYPE_MAXNAME];

typedef struct {
    size_t size;
} gh_tstype_primitive;

typedef struct {
    size_t count;
    gh_ts_typename element_type;
} gh_tstype_staticarray;

typedef struct {
    gh_ts_name name;
    gh_ts_typename type;
    off_t offset;
} gh_tstype_struct_field;

typedef struct {
    size_t size;
    size_t field_count;
    gh_tstype_struct_field fields[GH_TSTYPE_MAXFIELDS];
} gh_tstype_struct;

typedef struct {
    gh_ts_name name;
    gh_ts_typename type;
    int64_t tag;
} gh_tstype_taggedunion_field;

typedef struct {
    int64_t tag;
    char data[];
} gh_ts_taggedunion;

typedef struct {
    size_t union_size;
    size_t field_count;
    gh_tstype_taggedunion_field fields[GH_TSTYPE_MAXFIELDS];
} gh_tstype_taggedunion;

typedef struct {
    gh_ts_typename element_type;
} gh_tstype_pointer;

typedef struct {
    size_t count;
    void * ptr;
} gh_ts_arraypointer;

typedef struct {
    gh_ts_typename element_type;
} gh_tstype_arraypointer;

typedef struct {
    gh_tstype_kind kind;
    gh_ts_name name;
    union {
        gh_tstype_primitive t_primitive;
        gh_tstype_staticarray t_staticarray;
        gh_tstype_struct t_struct;
        gh_tstype_taggedunion t_taggedunion;
        gh_tstype_pointer t_pointer;
        gh_tstype_arraypointer t_arraypointer;
    };
} gh_tstype;

typedef struct {
    gh_alloc * alloc;
    gh_tstype * buffer;
    size_t size;
    size_t capacity;
} gh_ts;

typedef gh_result gh_ts_retrieveaddr_func(uintptr_t addr, size_t size, void * buffer, void * userdata);

gh_result gh_ts_ctor(gh_ts * ts, gh_alloc * alloc);
gh_result gh_ts_findbyname(gh_ts * ts, const char * name, size_t name_len, const gh_tstype ** const out_type);
gh_result gh_ts_findbynamec(gh_ts * ts, const char * name, const gh_tstype ** const out_type);
gh_result gh_ts_append(gh_ts * ts, gh_tstype type);
gh_result gh_ts_remove(gh_ts * ts, const char * name, size_t name_len);
gh_result gh_ts_sizet(gh_ts * ts, const gh_tstype * type, size_t * out_size);
gh_result gh_ts_size(gh_ts * ts, const char * name, size_t name_len, size_t * out_size);
gh_result gh_ts_sizec(gh_ts * ts, const char * name, size_t * out_size);
gh_result gh_ts_dtor(gh_ts * ts);

gh_result gh_ts_retrievet(gh_ts * ts, const gh_tstype * type, gh_bytebuffer * buffer, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr, void ** out_ptr);
gh_result gh_ts_retrieve(gh_ts * ts, const char * name, size_t name_len, gh_bytebuffer * buffer, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr, void ** out_ptr);
gh_result gh_ts_retrievec(gh_ts * ts, const char * name, gh_bytebuffer * buffer, gh_ts_retrieveaddr_func * callback, void * userdata, uintptr_t addr, void ** out_ptr);

gh_result gh_ts_makederivatives(gh_ts * ts);

gh_result gh_ts_loadbuiltin(gh_ts * ts);

#ifdef __cplusplus
}
#endif

#endif

/** @} */
