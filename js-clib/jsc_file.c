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
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <sys/syslimits.h>

static double sum(double a, double b) {
    return a + b;
}

static v7_val_t jsc_sum(struct v7 *v7) {
    double arg0 = v7_to_number(v7_arg(v7, 0));
    double arg1 = v7_to_number(v7_arg(v7, 1));
    double result = sum(arg0, arg1);
    return v7_create_number(result);
}


/*static v7_val_t exec(struct v7 *v7)
{


}*/

static v7_val_t jsc_pwd(struct v7 *v7)
{
    char *path = getcwd(NULL, 0);
    if (path)
    {
        v7_val_t result = v7_create_string(v7, path, ~0, 1);
        free(path);
        return result;
    }

    return v7_create_undefined();
}

static v7_val_t jsc_cd(struct v7 *v7)
{
    size_t argc = v7_argc(v7);
    if (argc == 1)
    {
        v7_val_t str = v7_arg(v7, 0);
        if (v7_is_string(str))
        {
            const char *cstr = v7_to_cstring(v7, &str);
            if (cstr != NULL)
            {
                chdir(cstr);
                return jsc_pwd(v7);
            }
        }
    }
    return v7_create_undefined();
}

static int _globerr(const char *path, int eerrno)
{
    fprintf(stderr, "%s: %s: %s\n", "jsc_file", path, strerror(eerrno));
    return 0;	/* let glob() keep going */
}

static v7_val_t jsc_ls(struct v7 *v7)
{

    int i, c = 0;
    int flags = 0;
    glob_t results;
    int ret;
    size_t argc = v7_argc(v7);

    if (argc == 0)
    {
        ret = glob(".", flags, _globerr, &results);
        if (ret != 0) {
            fprintf(stderr, "%s: problem with %s (%s), stopping early\n",
                    "jsc_file", ".",
                    /* ugly: */	(ret == GLOB_ABORTED ? "filesystem problem" :
                                    ret == GLOB_NOMATCH ? "no match of pattern" :
                                    ret == GLOB_NOSPACE ? "no dynamic memory" :
                                    "unknown problem"));
        } else {
            c++;
        }
    }
    else
    {
        for (i = 0; i < argc; i++) {

            v7_val_t str = v7_arg(v7, i);
            if (!v7_is_string(str)) continue;
            const char *cstr = v7_to_cstring(v7, &str);
            if (cstr == NULL) continue;

            flags |= (c > 0 ? GLOB_APPEND : 0);
            ret = glob(cstr, flags, _globerr, & results);
            if (ret != 0) {
                fprintf(stderr, "%s: problem with %s (%s), stopping early\n",
                        "jsc_file", cstr,
                        /* ugly: */	(ret == GLOB_ABORTED ? "filesystem problem" :
                                        ret == GLOB_NOMATCH ? "no match of pattern" :
                                        ret == GLOB_NOSPACE ? "no dynamic memory" :
                                        "unknown problem"));
                break;
            }

            c++;
        }
    }

    if (c == 0) return v7_create_undefined();

    v7_val_t array = v7_create_array(v7);

    for (i = 0; i < results.gl_pathc; i++) {
        //printf("%s\n", results.gl_pathv[i]);
        v7_array_push(v7, array, v7_create_string(v7, results.gl_pathv[i], ~0, 1));
    }

    globfree(& results);
    /*
    //if (v7_argc(v7)==0)
    DIR *mydir;
    struct dirent *myfile;
    struct stat mystat;


    char buf[512];
    mydir = opendir("./");
    while((myfile = readdir(mydir)) != NULL)
    {
        if (strcmp(myfile->d_name, ".") == 0 || strcmp(myfile->d_name, "..") == 0) continue;
        v7_array_push(v7, array, v7_create_string(v7, myfile->d_name, ~0, 1));
        //sprintf(buf, "%s", myfile->d_name);
        //stat(buf, &mystat);
        //printf("%zu",mystat.st_size);
        //printf(" %s\n", myfile->d_name);
    }
    closedir(mydir);
     */

    return array;
}

v7_val_t jsc_realpath(struct v7 *v7)
{
    char path[PATH_MAX];
    int c = 0, i;
    int argc = v7_argc(v7);
    v7_val_t array = v7_create_array(v7);

    for (i=0; i<argc; i++)
    {
        v7_val_t obj = v7_arg(v7, i);
        if (v7_is_array(v7, obj))
        {
            const size_t len = v7_array_length(v7, obj);
            int j;
            v7_val_t item;
            for (j=0; j<len;j ++)
            {
                item = v7_array_get(v7, obj, j);
                if (!v7_is_string(item)) continue;
                const char *cstr = v7_to_cstring(v7, &item);
                if (cstr == NULL) continue;
                if (realpath(cstr, path) != NULL)
                {
                    v7_array_push(v7, array, v7_create_string(v7, path, ~0, 1));
                    c++;
                }
            }
            continue;
        }

        if (!v7_is_string(obj)) continue;
        const char *cstr = v7_to_cstring(v7, &obj);
        if (cstr == NULL) continue;
        if (realpath(cstr, path) != NULL)
        {
            v7_array_push(v7, array, v7_create_string(v7, path, ~0, 1));
            c++;
        }
    }

    return c==0?v7_create_undefined():array;
}


void jsc_install_file_lib(struct v7 *v7)
{
    v7_set_method(v7, v7_get_global(v7), "sum", &jsc_sum);
    //v7_set_method(v7, v7_get_global(v7), "exec", &jsc_exec);
    v7_set_method(v7, v7_get_global(v7), "pwd", &jsc_pwd);
    v7_set_method(v7, v7_get_global(v7), "cd", &jsc_cd);
    v7_set_method(v7, v7_get_global(v7), "ls", &jsc_ls);
    v7_set_method(v7, v7_get_global(v7), "realpath", &jsc_realpath);
}