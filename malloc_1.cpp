#include <cstdio>
#include <cmath>
#include <unistd.h>

void* smalloc(size_t size) {
    if (size == 0 or size > pow(10,8)) {
        return NULL;
    }
    void* prev_prog_break = sbrk(size);
    if (prev_prog_break == (void*)(-1)) {
        return NULL;
    }
    return prev_prog_break;
}