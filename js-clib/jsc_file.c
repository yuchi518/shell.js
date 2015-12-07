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
// Created by Yuchi on 12/7/15.
//

#include "jsc_file.h"

static double sum(double a, double b) {
    return a + b;
}

static v7_val_t js_sum(struct v7 *v7) {
    double arg0 = v7_to_number(v7_arg(v7, 0));
    double arg1 = v7_to_number(v7_arg(v7, 1));
    double result = sum(arg0, arg1);
    return v7_create_number(result);
}

void jsc_install_file_lib(struct v7 *v7)
{
    v7_set_method(v7, v7_get_global(v7), "sum", &js_sum);
}