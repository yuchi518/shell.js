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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>

#include "common.h"
#include "uthash.h"

/// log

void ___log(uint32 level, const char *file_name, const char *function_name, int line_number, char* format, ...)
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


/// thread

thread* run_thread(thread* thrd, thread_func func, void *param)
{
    if (!thrd) {
        thrd = plat_mem_allocate(sizeof(*thrd));
        thrd->_should_free = true;
    } else {
        plat_mem_set(thrd, 0, sizeof(*thrd));
    }

    thrd->inst = plat_mem_allocate(sizeof(pthread_t));

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
        plat_mem_release(p);
    }

    if (thrd->_should_free)
    {
        thrd->_should_free = false;     // redundant code
        plat_mem_release(thrd);
    }
}

/// run
static void* _run_res_mgn = nil;
runid run(thread_func func, size_t param_size, void* param)
{
    if (!_run_res_mgn) {
        _run_res_mgn = res_create_management();
    }

    thread* thrd;
    runid rid;
    rid = res_create(_run_res_mgn, sizeof(*thrd) + param_size>sizeof(void*)?param_size:sizeof(void*), (void**)&thrd);

    if (param>0)
    {
        plat_mem_copy(thrd->_param, param, param_size);
    }
    else
    {
        thrd->_param[0] = nil;
    }

    run_thread(thrd, func, thrd->_param);

    return rid;
}

void run_cancel(runid rid)
{
    thread* thrd;
    if (_run_res_mgn)
    {
        thrd = res_get(_run_res_mgn, rid);
        pthread_t* p = thrd->inst;
        if (p) pthread_cancel(*p);
    }
}

void run_done(void)
{
    runid rid;
    thread* thrd;
    if (_run_res_mgn)
    {
        while ((rid = res_any(_run_res_mgn)) >= 0)
        {
            thrd = res_get(_run_res_mgn, rid);
            wait_thread(thrd);
            destroy_thread(thrd);
            res_release(_run_res_mgn, rid);
        }
        res_release_management(_run_res_mgn);
        _run_res_mgn = nil;
    }
}

/// resource

struct res_
{
    int id;
    UT_hash_handle hh;
    //u32 __align;
    uint8 data[0];
};

struct res_mgn_
{
    int last_id;
    struct res_ *head;
    pthread_mutex_t mutex;
};


resource_management_t res_create_management(void)
{
    struct res_mgn_* mgn = plat_mem_allocate(sizeof(struct res_mgn_));

    pthread_mutex_init(&mgn->mutex, nil);

    return mgn;
}

int res_create(resource_management_t _mgn, size_t size, resource_t* resource)
{
    struct res_ *res;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;
    int id;

    pthread_mutex_lock(&mgn->mutex);

    while(true)
    {
        id = mgn->last_id++;
        HASH_FIND_INT(mgn->head, &id, res);

        if (!res)
        {
            res = plat_mem_allocate(sizeof(struct res_) + size);
            res->id = id;
            HASH_ADD_INT(mgn->head, id, res);
            if (resource) *resource = res->data;

            pthread_mutex_unlock(&mgn->mutex);

            return id;
        }
    }
}

int res_create_and_clone(resource_management_t mgn, size_t size, resource_t resource_for_clone)
{
    resource_t resource;
    int id;

    id = res_create(mgn, size, &resource);
    if (id>=0)
    {
        plat_mem_copy(resource, resource_for_clone, size);
    }
    return id;
}

resource_t res_get(resource_management_t _mgn, int id)
{
    struct res_ *res;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;

    pthread_mutex_lock(&mgn->mutex);
    HASH_FIND_INT(mgn->head, &id, res);
    pthread_mutex_unlock(&mgn->mutex);

    return res?res->data:nil;
}

void res_release(resource_management_t _mgn, int id)
{
    struct res_ *res;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;

    pthread_mutex_lock(&mgn->mutex);
    HASH_FIND_INT(mgn->head, &id, res);
    if (res)
    {
        HASH_DEL(mgn->head, res);
        plat_mem_release(res);
    }
    pthread_mutex_unlock(&mgn->mutex);
}

void res_release_all(resource_management_t _mgn, void (callback)(int id, resource_t resource, void* user_data), void* user_data)
{
    struct res_ *res, *tmp;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;

    pthread_mutex_lock(&mgn->mutex);
    HASH_ITER(hh, mgn->head, res, tmp)
    {
        HASH_DEL(mgn->head, res);
        callback(res->id, res->data, user_data);
        plat_mem_release(res);
    }
    pthread_mutex_unlock(&mgn->mutex);
}

int res_any(resource_management_t _mgn)
{
    struct res_ *res, *tmp;
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;
    int id = -1;

    pthread_mutex_lock(&mgn->mutex);
    HASH_ITER(hh, mgn->head, res, tmp)
    {
        id = res->id;
    }
    pthread_mutex_unlock(&mgn->mutex);

    return id;
}

void res_release_management(resource_management_t _mgn)
{
    struct res_mgn_* mgn = (struct res_mgn_*)_mgn;

    pthread_mutex_destroy(&mgn->mutex);

    plat_mem_release(mgn);
}

