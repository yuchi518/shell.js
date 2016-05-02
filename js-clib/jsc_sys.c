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
// Created by Yuchi Chen on 2016/5/2.
//

#include "jsc_sys.h"
#include "plat_mem.h"

static enum v7_err jsc_exec(struct v7 *v7, v7_val_t* result)
{
    size_t argc = v7_argc(v7);
    if (argc >= 1)
    {
        char* path = v7_stringify(v7, v7_arg(v7, 0), NULL, 0, V7_STRINGIFY_DEFAULT);
        char** args;



        plat_mem_release(path);
    }

    *result = v7_mk_undefined();
    return V7_INVALID_ARG;
}

void jsc_install_sys_lib(struct v7 *v7)
{
    v7_set_method(v7, v7_get_global(v7), "exec", &jsc_exec);
}

void jsc_uninstall_sys_lib(struct v7 *v7)
{

}