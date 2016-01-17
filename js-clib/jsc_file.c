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
#include "common.h"
#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>

static double sum(double a, double b) {
    return a + b;
}

static enum v7_err jsc_sum(struct v7 *v7, v7_val_t* result) {
    double arg0 = v7_to_number(v7_arg(v7, 0));
    double arg1 = v7_to_number(v7_arg(v7, 1));
    *result = v7_mk_number(sum(arg0, arg1));
    return V7_OK;
}


/*static v7_val_t exec(struct v7 *v7)
{


}*/

static enum v7_err jsc_pwd(struct v7 *v7, v7_val_t* result)
{
    char *path = getcwd(NULL, 0);
    if (path)
    {
        *result = v7_mk_string(v7, path, ~0, 1);
        free(path);
        return V7_OK;
    }
    else
    {
        *result = v7_mk_undefined();
        return V7_INTERNAL_ERROR;
    }
}

static enum v7_err jsc_cd(struct v7 *v7, v7_val_t* result)
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
                return jsc_pwd(v7, result);
            }
        }
    }
    *result = v7_mk_undefined();
    return V7_INVALID_ARG;
}

static int _globerr(const char *path, int eerrno)
{

    log_err(0, "%s: %s\n", path, strerror(eerrno));

    return 0;    /* let glob() keep going */
}

static enum v7_err jsc_ls(struct v7 *v7, v7_val_t* result)
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
            log_err(0, "problem with %s (%s), stopping early\n",
                    ".",
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
                log_err(0, "problem with %s (%s), stopping early\n",
                        cstr,
                        /* ugly: */	(ret == GLOB_ABORTED ? "filesystem problem" :
                                        ret == GLOB_NOMATCH ? "no match of pattern" :
                                        ret == GLOB_NOSPACE ? "no dynamic memory" :
                                        "unknown problem"));
                break;
            }

            c++;
        }
    }

    if (c == 0) {
        *result = v7_mk_undefined();
        return V7_OK;
    }

    v7_val_t array = v7_mk_array(v7);
    c = 0;

    for (i = 0; i < results.gl_pathc; i++) {
        //log_info(0, "%s\n", results.gl_pathv[i]);
        v7_array_push(v7, array, v7_mk_string(v7, results.gl_pathv[i], ~0, 1));
        c++;
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
        v7_array_push(v7, array, v7_mk_string(v7, myfile->d_name, ~0, 1));
        //sprintf(buf, "%s", myfile->d_name);
        //stat(buf, &mystat);
        //printf("%zu",mystat.st_size);
        //printf(" %s\n", myfile->d_name);
    }
    closedir(mydir);
     */

    if (c==0) *result = v7_mk_undefined();
    else if (c==1) *result = v7_array_get(v7, array, 0);
    else *result = array;
    return V7_OK;
}

static enum v7_err jsc_realpath(struct v7 *v7, v7_val_t* result)
{
    char path[PATH_MAX];
    int c = 0, i;
    int argc = v7_argc(v7);
    v7_val_t array = v7_mk_array(v7);

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
                if (realpath(cstr, path))
                {
                    v7_array_push(v7, array, v7_mk_string(v7, path, ~0, 1));
                    c++;
                }
            }
            continue;
        }

        if (!v7_is_string(obj)) continue;
        const char *cstr = v7_to_cstring(v7, &obj);
        if (cstr == NULL) continue;
        if (realpath(cstr, path))
        {
            v7_array_push(v7, array, v7_mk_string(v7, path, ~0, 1));
            c++;
        }
    }

    if (c==0) *result = v7_mk_undefined();
    else if (c==1) *result = v7_array_get(v7, array, 0);
    else *result = array;
    return V7_OK;
}


static char *_read_file(const char *path, size_t *size) {
    FILE *fp;
    char *data = NULL;
    if ((fp = fopen(path, "rb")) == NULL) {

    } else if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
    } else {
        *size = ftell(fp);
        data = (char *) malloc(*size + 1);
        if (data != NULL) {
            fseek(fp, 0, SEEK_SET); /* Some platforms might not have rewind(), Oo */
            if (fread(data, 1, *size, fp) != *size) {
                free(data);
                return NULL;
            }
            data[*size] = '\0';
        }
        fclose(fp);
    }
    return data;
}

static enum v7_err jsc_cat(struct v7 *v7, v7_val_t* result)
{
    size_t file_size;
    int c = 0, i;
    int argc = v7_argc(v7);
    v7_val_t array = v7_mk_array(v7);
    char *buff;

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
                buff = _read_file(cstr, &file_size);
                if (buff != NULL)
                {
                    v7_array_push(v7, array, v7_mk_string(v7, buff, file_size, 1));
                    free(buff);
                }
                c++;
            }
            continue;
        }

        if (!v7_is_string(obj)) continue;
        const char *cstr = v7_to_cstring(v7, &obj);
        if (cstr == NULL) continue;
        buff = _read_file(cstr, &file_size);
        if (buff != NULL)
        {
            v7_array_push(v7, array, v7_mk_string(v7, buff, file_size, 1));
            free(buff);
        }
        c++;
    }

    if (c==0) *result = v7_mk_undefined();
    else if (c==1) *result = v7_array_get(v7, array, 0);
    else *result = array;
    return V7_OK;
}

static enum v7_err jsc_echo(struct v7 *v7, v7_val_t* result)
{
    int argc = v7_argc(v7);
    if (argc == 2)
    {
        v7_val_t obj0 = v7_arg(v7, 0);
        v7_val_t obj1 = v7_arg(v7, 1);

        if (v7_is_string(obj0) && v7_is_string(obj1))
        {
            const char *cstr0 = v7_to_cstring(v7, &obj0);
            const char *cstr1 = v7_to_cstring(v7, &obj1);

            if (cstr0!=NULL && cstr1!=NULL)
            {
                FILE *fp;
                if ((fp = fopen(cstr1, "wb")) != NULL)
                {
                    fwrite(cstr0, strlen(cstr0), 1, fp);
                    fclose(fp);
                }
            }
        }
    }

    *result = v7_mk_undefined();
    return V7_OK;
}


void jsc_install_file_lib(struct v7 *v7)
{
    v7_set_method(v7, v7_get_global(v7), "sum", &jsc_sum);
    //v7_set_method(v7, v7_get_global(v7), "exec", &jsc_exec);

    v7_set_method(v7, v7_get_global(v7), "cd", &jsc_cd);
    v7_set_method(v7, v7_get_global(v7), "pwd", &jsc_pwd);

    v7_set_method(v7, v7_get_global(v7), "ls", &jsc_ls);
    v7_set_method(v7, v7_get_global(v7), "realpath", &jsc_realpath);

    v7_set_method(v7, v7_get_global(v7), "echo", &jsc_echo);
    v7_set_method(v7, v7_get_global(v7), "cat", &jsc_cat);
}