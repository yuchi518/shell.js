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

char *read_file(const char *path, size_t *size);

int main(int argc, char *argv[]) {
    if (argc > 1)
    {
        int i;
        for (i=1; i<argc; i++)
        {
            v7_val_t exec_result = 0;
            struct v7 *v7 = v7_create();
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
                v7_exec(v7, js_code, &exec_result);
            }
            //v7_exec_file(v7, js_code, &exec_result);

            v7_destroy(v7);

            printf("%llx", (long long int)(enum v7_err)exec_result);

        }
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