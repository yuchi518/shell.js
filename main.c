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
// Created by Yuchi on 2015/11/28.
//

#include <stdlib.h>
#include "v7.h"
#include "jsc_file.h"

char *read_file(const char *path, size_t *size);
void print_err_and_res(enum v7_err err, v7_val_t result);
const char *errs_string[6] = {
        [V7_OK] = "OK",
        [V7_SYNTAX_ERROR] = "Syntax error",
        [V7_EXEC_EXCEPTION] = "Exec exception",
        [V7_STACK_OVERFLOW] = "Stack overflow",
        [V7_AST_TOO_LARGE] = "AST to large",
        [V7_INVALID_ARG] = "Invalid arguments",
};

void install_all_js_clibs(struct v7 *v7)
{
    jsc_install_file_lib(v7);
}

int main(int argc, char *argv[]) {
    enum v7_err err;
    v7_val_t exec_result;
    struct v7 *v7;

    if (argc > 1)
    {
        int i;
        for (i=1; i<argc; i++)
        {
            v7 = v7_create();
            install_all_js_clibs(v7);

            const char *js_path = argv[i];
            size_t js_size = 0;
            char *js_code = read_file(js_path, &js_size);

            if (js_size > 2)
            {
                if (js_code[0] == '#' && js_code[1] == '!') {
                    js_code += 2;

                    // fine line tail
                    while (*js_code != '\0' && *js_code != '\n' && *js_code != '\r') js_code ++;
                    // find line head
                    while (*js_code != '\0' && (*js_code == '\n' || *js_code == '\r')) js_code ++;
                }
                err = v7_exec(v7, js_code, &exec_result);
                if (err != V7_OK) print_err_and_res(err, exec_result);
            }

            v7_destroy(v7);
        }
    }
    else
    {
        char *js_string = NULL;
        size_t js_string_len = 0;
        v7 = v7_create();
        install_all_js_clibs(v7);

        printf("Shell.js 0.1\n>>> ");
        while ((getline(&js_string, &js_string_len, stdin)) != -1) {
            err = v7_exec(v7, js_string, &exec_result);
            if (err != V7_OK) print_err_and_res(err, exec_result);
            printf(">>> ");
        }

        v7_destroy(v7);
        free(js_string);
    }

    return 0;
}


char *read_file(const char *path, size_t *size) {
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

void print_err_and_res(enum v7_err err, v7_val_t result)
{
    printf("err: %s, result: %llx\n", errs_string[err], (long long int)result);
}