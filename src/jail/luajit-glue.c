/* Code in this file has been adapted from the file 'src/lib_io.c' in LuaJIT.
 *
 * The license terms of LuaJIT have been inserted below in this comment verbatim.
 *
 * 
 * ===============================================================================
 * LuaJIT -- a Just-In-Time Compiler for Lua. https://luajit.org/
 * 
 * Copyright (C) 2005-2023 Mike Pall. All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * [ MIT license: https://www.opensource.org/licenses/mit-license.php ]
 * 
 * ===============================================================================
 * [ LuaJIT includes code from Lua 5.1/5.2, which has this license statement: ]
 * 
 * Copyright (C) 1994-2012 Lua.org, PUC-Rio.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * 
 * ===============================================================================
 * [ LuaJIT includes code from dlmalloc, which has this license statement: ]
 * 
 * This is a version (aka dlmalloc) of malloc/free/realloc written by
 * Doug Lea and released to the public domain, as explained at
 * https://creativecommons.org/licenses/publicdomain
 * 
 * ===============================================================================
 */

#include <jail/lua.h>
#include <jail/luajit-glue.h>

#pragma GCC diagnostic push
#pragma clang diagnostic ignored "-Wreserved-macro-identifier"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#pragma GCC diagnostic ignored "-Wundef"
#pragma GCC diagnostic ignored "-Wunused-parameter"
// RATIONALE: LuaJIT was not written to compile under such a restrictive warning/error
// config. Since we use LuaJIT internals here (lj_obj.h), we have to disable the
// diagnostics that cause problems.

#include "luajit/src/lj_obj.h"

struct gh_luajit_file {
  FILE *fp;		/* File handle. */
  uint32_t type;	/* File type. */
};

#define IOFILE_TYPE_FILE 0

/* NOTE:
 * This is somewhat hacky, and it *may* break if LuaJIT ever updates how files are stored.
 * If anything breaks, the worst thing that will happen is that subjail will segfault when
 * working on files. This does not consistute a security issue in the host process.
 *
 * To make this a bit safer, we don't assign the metatable here - that's the responsibility
 * of a Lua wrapper.
 */

void gh_luajit_pushfile(lua_State *L, FILE * fp)
{
    gh_luajit_file *iof = (gh_luajit_file *)lua_newuserdata(L, sizeof(gh_luajit_file));

    GCudata *ud = udataV(L->top-1);
    ud->udtype = UDTYPE_IO_FILE;

    iof->fp = fp;
    iof->type = IOFILE_TYPE_FILE;
}

#pragma GCC diagnostic pop
