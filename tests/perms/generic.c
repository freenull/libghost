#include "ghost/alloc.h"
#include "ghost/perms/request.h"
#include "ghost/perms/prompt.h"
#include "ghost/rpc.h"
#include <ghost/result.h>
#include <ghost/perms/perms.h>
#include <ghost/sandbox.h>
#include <ghost/thread.h>

/* Example resource - secret keys assigned to "users" */

#define KEYLEN 1024
#define NAMELEN 1024
typedef struct {
    char username[NAMELEN];
    char key[KEYLEN];
} secretkey;

#define MAXKEYS 16
typedef struct {
    secretkey keys[MAXKEYS];
} secretkey_store;

static secretkey_store secretkey_store_new(void) {
    return (secretkey_store) {0};
}

static const secretkey * secretkey_store_add(secretkey_store * store, secretkey key) {
    for (secretkey * k = store->keys; k < (store->keys) + MAXKEYS; k++) {
        if (k->username[0] == '\0') {
            strcpy(k->username, key.username);
            strcpy(k->key, key.key);
            return k;
        }
    }

    abort();
}

static const secretkey * secretkey_store_set(secretkey_store * store, secretkey key) {
    for (secretkey * k = store->keys; k < (store->keys) + MAXKEYS; k++) {
        if (strcmp(k->username, key.username) == 0) {
            strcpy(k->key, key.key);
            return k;
        }
    }

    return secretkey_store_add(store, key);
}

static bool secretkey_store_get(secretkey_store * store, secretkey * key) {
    for (secretkey * k = store->keys; k < (store->keys) + MAXKEYS; k++) {
        if (strcmp(k->username, key->username) == 0) {
            strcpy(key->key, k->key);
            return true;
        }
    }

    return false;
}

static void secretkey_store_printall(const secretkey_store * store) {
    for (const secretkey * k = store->keys; k < (store->keys) + MAXKEYS; k++) {
        if (k->username[0] == '\0') return;
        printf("%s: '%s'\n", k->username, k->key);
    }
}

typedef enum {
    SECRETKEY_PERMS_UNSET,
    SECRETKEY_PERMS_TRUE,
    SECRETKEY_PERMS_FALSE
} secretkey_perms_flag;

// Example permission store - either can or cannot read key for specific username
typedef struct {
    char username[NAMELEN];
    bool occupied;

    secretkey_perms_flag read_key;
    secretkey_perms_flag set_key;
} secretkey_perms_entry;

typedef struct {
    secretkey_store * store;
    secretkey_perms_entry entries[MAXKEYS];
} secretkey_perms;

static secretkey_perms_entry * secretkey_perms_newentry(secretkey_perms * perms, const char * username, size_t username_len) {
    if (username_len > NAMELEN - 1) username_len = NAMELEN - 1;
    for (secretkey_perms_entry * ent = perms->entries; ent < perms->entries + MAXKEYS; ent++) {
        if (!ent->occupied) {
            *ent = (secretkey_perms_entry) {0};
            ent->occupied = true;
            strncpy(ent->username, username, username_len);
            ent->username[NAMELEN - 1] = '\0';
            ent->read_key = SECRETKEY_PERMS_UNSET;
            ent->set_key = SECRETKEY_PERMS_UNSET;
            return ent;
        }
    }

    abort();
}

static secretkey_perms_entry * secretkey_perms_getentry(secretkey_perms * perms, const char * username, size_t username_len) {
    for (secretkey_perms_entry * ent = perms->entries; ent < perms->entries + MAXKEYS; ent++) {
        if (ent->occupied && strlen(ent->username) == username_len && strncmp(ent->username, username, username_len) == 0) return ent;
    }

    return secretkey_perms_newentry(perms, username, username_len);
}

// libghost generic permission handler setup (nice way to let libghost handle deserialization and serialization automatically)

static void * secretkey_perm_ctor(void * userdata) {
    secretkey_perms * perms = malloc(sizeof(secretkey_perms));
    memset(perms, 0, sizeof(secretkey_perms));
    perms->store = (secretkey_store *)userdata;
    return perms;
}

static gh_result secretkey_perm_dtor(void * instance, void * userdata) {
    (void)userdata;

    free(instance);
    return GHR_OK;
}

static gh_result secretkey_perm_match(void * instance, gh_permrequest_id group_id, gh_permrequest_id resource_id, void * userdata) {
    (void)instance;
    (void)userdata;

    if (strcmp(group_id, "secretkey") == 0 && strcmp(resource_id, "user") == 0) return GHR_OK;
    return GHR_PERMPARSER_NOMATCH;
}

static gh_result secretkey_perm_newentry(void * instance, const gh_permgeneric_key * key, void * userdata, void ** out_entry) {
    (void)userdata;
    secretkey_perms * perms = (secretkey_perms *)instance;

    *out_entry = secretkey_perms_newentry(perms, key->key.buffer, key->key.size);
    return GHR_OK;
}

static gh_result secretkey_perm_loadentry(void * instance, void * entryv, gh_permrequest_id field, gh_permparser * parser, void * userdata) {
    (void)instance;
    (void)userdata;

    secretkey_perms_entry * entry = (secretkey_perms_entry *)entryv;

    if (strcmp(field, "read") == 0) {
        gh_conststr username;
        gh_result result = gh_permparser_nextstring(parser, &username);
        if (ghr_iserr(result)) return result;

        entry->read_key = (username.size == strlen("true") && strncmp(username.buffer, "true", username.size) == 0) ? SECRETKEY_PERMS_TRUE : SECRETKEY_PERMS_FALSE;
        return GHR_OK;
    } else if (strcmp(field, "set") == 0) {
        gh_conststr username;
        gh_result result = gh_permparser_nextstring(parser, &username);
        if (ghr_iserr(result)) return result;

        entry->set_key = (username.size == strlen("true") && strncmp(username.buffer, "true", username.size) == 0) ? SECRETKEY_PERMS_TRUE : SECRETKEY_PERMS_FALSE;
        return GHR_OK;
    } else {
        return GHR_PERMPARSER_UNKNOWNFIELD;
    }
}

static gh_result secretkey_perm_save(void * instance, gh_permwriter * writer, void * userdata) {
    (void)userdata;

    secretkey_perms * perms = (secretkey_perms *)instance;

    gh_result res = gh_permwriter_beginresource(writer, "secretkey", "user");
    if (ghr_iserr(res)) return res;

    for (secretkey_perms_entry * ent = perms->entries; ent < perms->entries + MAXKEYS; ent++) {
        if (!ent->occupied) continue;

        res = gh_permwriter_beginentry(writer, ent->username, strlen(ent->username));
        if (ghr_iserr(res)) return res;

        res = gh_permwriter_field(writer, "read");
        if (ghr_iserr(res)) return res;

        res = gh_permwriter_fieldargstring(writer, (ent->read_key ? "true" : "false"), strlen((ent->read_key ? "true" : "false")));
        if (ghr_iserr(res)) return res;
        
        res = gh_permwriter_field(writer, "set");
        if (ghr_iserr(res)) return res;

        res = gh_permwriter_fieldargstring(writer, (ent->set_key ? "true" : "false"), strlen((ent->set_key ? "true" : "false")));
        if (ghr_iserr(res)) return res;

        res = gh_permwriter_endentry(writer);
        if (ghr_iserr(res)) return res;
    }

    return gh_permwriter_endresource(writer);
}

static const char * description_set = "Script '${source}' is requesting access to the secret key of user ${username}. If accepted, the script will be able to:$$- Change the secret key";
static const char * description_read = "Script '${source}' is requesting access to the secret key of user ${username}. If accepted, the script will be able to: $$ - Read the secret key";
static const char * description_readset = "Script '${source}' is requesting access to the secret key of user ${username}. If accepted, the script will be able to: $$ - Change the secret key $$ - Read the secret key";

// Custom "gate"/request function. The protocol is as follows:
//   - You call this function right before executing some kind of operation on 'entry'.
//   - If the function succeeds, you can execute an operation, but only THAT operation using data from the exact same 'entry', and only right then and there.
//   - If the function fails, you cannot execute any operation.
//
// Just like libghost's permfs, this function will call the prompter for you if the permision isn't "set in stone" yet.
//
// Since this is *not* a function that will go anywhere into libghost, there is no longer a need to expose libghost types everywhere in the interface (like
// with the gh_result return values of previous functions).
// As demonstrated, you can take in and return anything you want (in this case a lot of booleans). You will likely want to return a gh_result, though, so that
// you can report which error actually happened. You could also return your own error type, and translate a set of gh_result into your own error set.
//
// You will almost certainly want to take in a gh_thread though, since that structure contains both a name you can use as the 'source' in the request to the
// prompter, and it also contains the gh_perms object with a pointer to the actual prompter.
// Depending on your needs, you may choose to for example use a static prompter here (not determined by the gh_perms->prompter), in which case you could get
// away with just a 'const char * source' argument. This is discouraged unless you have a really good reason for it, as it breaks the "one size fits all"
// design of prompters.
static bool secretkey_gate(gh_thread * thread, secretkey_perms_entry * entry, bool flag_read, bool flag_set, bool future) {
    if (flag_read) {
        if (entry->read_key == SECRETKEY_PERMS_TRUE) flag_read = false;
        else if (entry->read_key == SECRETKEY_PERMS_FALSE) return false;
    }

    if (flag_set) {
        if (entry->set_key == SECRETKEY_PERMS_TRUE) flag_set = false;
        else if (entry->set_key == SECRETKEY_PERMS_FALSE) return false;
    }

    if (!flag_read && !flag_set) return true;

    gh_permrequest req = {
        .group = "secretkey",
        .resource = "user"
    };
    strncpy(req.source, thread->safe_id, GH_PERMREQUEST_SOURCEMAX);
    req.source[GH_PERMREQUEST_SOURCEMAX - 1] = '\0';

    gh_permrequest_setfield(&req, "key", gh_conststr_fromz(entry->username));
    gh_permrequest_setfield(&req, "username", gh_conststr_fromz(entry->username));

    if (flag_read && flag_set) {
        gh_permrequest_setfield(&req, "description", gh_conststr_fromz(description_readset));
    } else if (flag_read) {
        gh_permrequest_setfield(&req, "description", gh_conststr_fromz(description_read));
    } else if (flag_set) {
        gh_permrequest_setfield(&req, "description", gh_conststr_fromz(description_set));
    }

    if (future) gh_permrequest_setfield(&req, "hint", gh_conststr_fromlit("future"));
    if (flag_read) gh_permrequest_setfield(&req, "read_secret_key", gh_conststr_fromlit("yes"));
    if (flag_set) gh_permrequest_setfield(&req, "set_secret_key", gh_conststr_fromlit("yes"));

    gh_permresponse resp;
    gh_result result = gh_permprompter_request(&thread->perms.prompter, &req, &resp);
    if (ghr_iserr(result)) return false;

    switch(resp) {
    case GH_PERMRESPONSE_ACCEPT:
        return true;
    case GH_PERMRESPONSE_ACCEPTREMEMBER:
        if (flag_read) entry->read_key = SECRETKEY_PERMS_TRUE;
        if (flag_set) entry->set_key = SECRETKEY_PERMS_TRUE;
        return true;
    case GH_PERMRESPONSE_REJECT:
        return false;
    case GH_PERMRESPONSE_REJECTREMEMBER:
        if (flag_read) entry->read_key = SECRETKEY_PERMS_FALSE;
        if (flag_set) entry->set_key = SECRETKEY_PERMS_FALSE;
        return true;
    default: return false;
    }
}

// RPC functions that will be exposed to Lua

static void rpc_getsecretkey(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * username;
    size_t username_size;
    if (!gh_rpcframe_argbuf(frame, 0, &username, &username_size)) {
        gh_rpcframe_failhere(frame, ghr_errnoval(GHR_RPCF_ARG0, EINVAL));
    }
    username[username_size - 1] = '\0';

    secretkey_perms * perms = gh_perms_getgeneric(&frame->thread->perms, "secretkey");
    secretkey_perms_entry * entry = secretkey_perms_getentry(perms, username, username_size - 1);

    bool success = secretkey_gate(frame->thread, entry, true, false, false);
    if (!success) {
        gh_rpcframe_failhere(frame, GHR_UNKNOWN);
    }

    secretkey key = {0};
    strncpy(key.username, username, NAMELEN);
    key.username[NAMELEN - 1] = '\0';

    if (secretkey_store_get(perms->store, &key)) {
        gh_rpcframe_returnbuftypedhere(frame, key.key, strlen(key.key));
    } else {
        gh_rpcframe_failarghere(frame, 0);
    }
}

static void rpc_setsecretkey(gh_rpc * rpc, gh_rpcframe * frame) {
    (void)rpc;

    char * username;
    size_t username_size;
    if (!gh_rpcframe_argbuf(frame, 0, &username, &username_size)) {
        gh_rpcframe_failarghere(frame, 0);
    }
    username[username_size - 1] = '\0';

    secretkey_perms * perms = gh_perms_getgeneric(&frame->thread->perms, "secretkey");
    secretkey_perms_entry * entry = secretkey_perms_getentry(perms, username, username_size - 1);

    char * newkey;
    size_t newkey_size;
    if (!gh_rpcframe_argbuf(frame, 1, &newkey, &newkey_size)) {
        gh_rpcframe_failarghere(frame, 1);
    }
    newkey[newkey_size - 1] = '\0';

    bool success = secretkey_gate(frame->thread, entry, false, true, false);
    if (success) {
        secretkey key = {0};
        strncpy(key.username, username, NAMELEN);
        key.username[NAMELEN - 1] = '\0';
        strncpy(key.key, newkey, KEYLEN);
        key.key[KEYLEN - 1] = '\0';

        secretkey_store_set(perms->store, key);
    }

    gh_rpcframe_setresult(frame, success ? GHR_OK : GHR_UNKNOWN);
}

int main(void) {
    secretkey_store store = secretkey_store_new();
    secretkey_store_set(&store, (secretkey){ "root", "topsecretkey" });
    secretkey_store_set(&store, (secretkey){ "myuser", "secret!!!" });
    secretkey_store_set(&store, (secretkey){ "nobody", "helloworld123" });
    secretkey_store_printall(&store);

    gh_permgeneric pg = {
        .userdata = &store,

        .ctor = secretkey_perm_ctor,
        .dtor = secretkey_perm_dtor,
        .match = secretkey_perm_match,
        .newentry = secretkey_perm_newentry,
        .loadentry = secretkey_perm_loadentry,
        .save = secretkey_perm_save,
    };

    gh_sandbox sandbox;
    gh_sandboxoptions options = {
        .name = "my sandbox",
        .memory_limit_bytes = GH_SANDBOX_NOLIMIT,
        .functioncall_frame_limit_bytes = GH_SANDBOX_NOLIMIT
    };

    ghr_assert(gh_sandbox_ctor(&sandbox, options));

    gh_alloc alloc = gh_alloc_default();

    gh_rpc rpc;
    ghr_assert(gh_rpc_ctor(&rpc, &alloc));

    ghr_assert(gh_rpc_register(&rpc, "getsecretkey", rpc_getsecretkey, GH_RPCFUNCTION_THREADUNSAFEGLOBAL));
    ghr_assert(gh_rpc_register(&rpc, "setsecretkey", rpc_setsecretkey, GH_RPCFUNCTION_THREADUNSAFEGLOBAL));

    int pipefd[2];
    assert(pipe(pipefd) >= 0);

    gh_permprompter prompter = gh_permprompter_simpletui(pipefd[0]);

    gh_threadoptions thread_options = {
        .sandbox = &sandbox,
        .rpc = &rpc,
        .prompter = prompter,
        .default_timeout_ms = GH_IPC_NOTIMEOUT,
        .name = "my thread",
        .safe_id = "lua program"
    };

    gh_thread thread;
    ghr_assert(gh_thread_ctor(&thread, thread_options));

    ghr_assert(gh_perms_registergeneric(&thread.perms, "secretkey", &pg));

    int savedfd = open("generic.ghperm", O_RDONLY);
    assert(savedfd >= 0 || errno == ENOENT);
    if (savedfd >= 0) {
        ghr_assert(gh_perms_readfd(&thread.perms, savedfd, NULL));
        assert(close(savedfd) >= 0);
    }

    assert(write(pipefd[1], "y\ny\n", 4) == 4);
    int luafd = open("generic.lua", O_RDONLY);
    assert(luafd >= 0);

    gh_threadnotif_script script_result;
    ghr_assert(gh_thread_runfilesync(&thread, luafd, &script_result));

    if (ghr_iserr(script_result.result)) {
        fprintf(stderr, "Lua error: ");
        ghr_fputs(stderr, script_result.result);
        fprintf(stderr, "%s", script_result.error_msg);
        assert("Lua error!" && false);
    }

    assert(close(luafd) >= 0);

    savedfd = open("generic.ghperm", O_WRONLY | O_TRUNC | O_CREAT, 0600);
    assert(savedfd >= 0);
    ghr_assert(gh_perms_write(&thread.perms, savedfd));
    assert(close(savedfd) >= 0);

    ghr_assert(gh_thread_dtor(&thread, NULL));
    ghr_assert(gh_rpc_dtor(&rpc));
    ghr_assert(gh_sandbox_dtor(&sandbox, NULL));

    assert(close(pipefd[1]) >= 0);
    assert(close(pipefd[0]) >= 0);
}
