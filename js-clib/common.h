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

/// log
#define MAX_LOG_MSG         (1024)

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


/// thread
typedef void* (*thread_func)(void *param);

struct thread_struct {

};

struct thread_struct* run_thread(struct thread_struct* thrd, thread_func func, void *param);



#endif //SHELL_JS_THREADS_H




