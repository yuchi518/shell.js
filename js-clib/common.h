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

#ifndef SHELL_JS_COMMON_H
#define SHELL_JS_COMMON_H

#include <stddef.h>

#ifndef nil
#define nil        (0)
#endif

typedef char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;
typedef long long s64;
typedef unsigned long long u64;
typedef u8 bool;
#define true        (1)
#define false       (0)

/// log
#define MAX_LOG_MSG         1024

#define LOG_ERR             0x01000000
#define LOG_DBG             0x02000000
#define LOG_INFO            0x04000000

void ___log(u32 level, const char *file_name, const char *function_name, int line_number, char* format, ...);
#define log(level, format, args...)             ___log(level, __FILE__, __func__, __LINE__, format, ##args)
#define log_err(level, format, args...)         log((level | LOG_ERR), format, ##args)
#define log_dbg(level, format, args...)         log((level | LOG_DBG), format, ##args)
#define log_info(level, format, args...)        log((level | LOG_INFO), format, ##args)

/// memory
void* mem_alloc(size_t size);
void mem_free(void *memory);
void mem_copy(void *mem_dest, void *mem_src, size_t size);
void mem_clean(void *memory, size_t size);

/// thread
typedef void* (*thread_func)(void *param);

struct thread_struct {
    void* inst;
    bool _should_free;
    union {
        void *_param[1];
    };
};

typedef struct thread_struct thread;

thread* run_thread(thread* thrd, thread_func func, void* param);
void* wait_thread(thread* thrd);
void destroy_thread(thread* thrd);

/// run, management thread
typedef int runid;
runid run(thread_func func, size_t param_size, void* param);
void run_cancel(runid rid);
void run_done(void);            // only call before program exit

/// resource management
typedef void* resource_management;
resource_management res_create_management(void);
int res_create(resource_management mgn, size_t size, void** resource);
void* res_get(resource_management mgn, int id);
void res_release(resource_management mgn, int id);
void res_release_all(resource_management mgn, void* (callback)(int id, void* resource));
int res_any(resource_management mgn);
void res_release_management(resource_management mgn);

#endif //SHELL_JS_THREADS_H




