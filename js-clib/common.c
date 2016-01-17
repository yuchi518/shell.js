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
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/// log

void ___log(u32 level, const char *file_name, const char *function_name, int line_number, char* format, ...)
{
    char buffer[MAX_LOG_MSG];
    va_list args;
    va_start (args, format);
    vsnprintf (buffer, MAX_LOG_MSG, format, args);

    //do something with the error


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


/// thread

struct thread_struct* run_thread(struct thread_struct* thrd, thread_func func, void *param)
{
    if (!thrd) {
        thrd = mem_alloc(sizeof(*thrd));
    }

    return thrd;
}