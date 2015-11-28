//
// Created by 玉奇 陳 on 2015/11/28.
//

#include "v7.h"

int main(int argc, char *argv[]) {
    const char *js_code = argc > 1 ? argv[1] : "";
    v7_val_t exec_result;
    struct v7 *v7 = v7_create();

    v7_exec(v7, js_code, &exec_result);

    v7_destroy(v7);

    return 0;
}