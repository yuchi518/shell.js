/*
 * Shell.js (jssh), JavaScript shell
 * Copyright (C) 2015 Yuchi (yuchi518@gmail.com)

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation. For the terms of this
 * license, see <http://www.gnu.org/licenses>.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
//
// Created by Yuchi on 2015/12/27.
//

#include "common.h"
#include "uthash.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>

/// log

void ___log(u32 level, const char *file_name, const char *function_name, int line_number, char* format, ...)
{
    char buffer[MAX_LOG_MSG];
    va_list args;
    va_start (args, format);
    vsnprintf (buffer, MAX_LOG_MSG, format, args);

    //do something with the error
    if (level & LOG_ERR)
    {
        fprintf(stderr, "%s", buffer);
    }
    else
    {
        fprintf(stdout, "%s", buffer);

    }


    va_end (args);
}

/// memory

void* mem_alloc(size_t size)
{
    void* mem = malloc(size);
    if (mem) memset(mem, 0, size);

    return mem;
}

void mem_free(void *memory)
{
    if (memory) free(memory);
}

void mem_copy(void *mem_dest, void *mem_src, size_t size)
{
    memcpy(mem_dest, mem_src, size);
}

void mem_clean(void *memory, size_t size)
{
    memset(memory, 0, size);
}


/// thread

thread* run_thread(thread* thrd, thread_func func, void *param)
{
    if (!thrd) {
        thrd = mem_alloc(sizeof(*thrd));
        thrd->_should_free = true;
    } else {
        mem_clean(thrd, sizeof(*thrd));
    }

    thrd->inst = mem_alloc(sizeof(pthread_t));

    pthread_create((pthread_t*)thrd->inst, NULL , func , param);

    return thrd;
}

void* wait_thread(thread* thrd)
{
    void* ret;
    pthread_join(*(pthread_t*)thrd->inst, &ret);
    return ret;
}

void destroy_thread(thread* thrd)
{
    pthread_t* p = thrd->inst;
    thrd->inst = nil;
    if (p)
    {
        pthread_cancel(*p);
        mem_free(p);
    }

    if (thrd->_should_free)
    {
        thrd->_should_free = false;     // redundant code
        mem_free(thrd);
    }
}


/// resource

struct res_
{
    int id;
    UT_hash_handle hh;
    //u32 __align;
    u8 data[0];
};

struct res_mgn_
{
    int last_id;
    struct res_ *head;
};


resource_management res_create_management(void)
{
    struct res_mgn_* mgn = mem_alloc(sizeof(struct res_mgn_));

    return mgn;
}

int res_create(resource_management _mgn, size_t size, void** resource)
{
    struct res_ *res;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;
    int id;

    while(true)
    {
        id = mgn->last_id++;
        HASH_FIND_INT(mgn->head, &id, res);

        if (!res)
        {
            res = mem_alloc(sizeof(struct res_)+size);
            res->id = id;
            HASH_ADD_INT(mgn->head, id, res);
            if (resource) *resource = res->data;
            return id;
        }
    }
}

void* res_get(resource_management _mgn, int id)
{
    struct res_ *res;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;
    HASH_FIND_INT(mgn->head, &id, res);

    return res?res->data:nil;
}

void res_release(resource_management _mgn, int id)
{
    struct res_ *res;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;
    HASH_FIND_INT(mgn->head, &id, res);
    if (res)
    {
        HASH_DEL(mgn->head, res);
        mem_free(res);
    }
}

void res_release_all(resource_management _mgn, void* (callback)(int id, void* resource))
{
    struct res_ *res, *tmp;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;

    HASH_ITER(hh, mgn->head, res, tmp)
    {
        HASH_DEL(mgn->head, res);
        callback(res->id, res->data);
        mem_free(res);
    }
}

void res_release_management(resource_management _mgn)
{
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;

    mem_free(mgn);
}

