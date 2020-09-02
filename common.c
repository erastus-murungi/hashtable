
#include "dict.h"


void *safe_malloc(size_t n, unsigned long line)
{
        void *p = malloc(n);
        if (!p) {
                fprintf(stderr, "[%s:%lu]Out of memory(%ld bytes)\n",
                        __FILE__, line, n);
        }
        return p;
}

void *safe_realloc(void *p, size_t n, unsigned long line)
{
        void *P = realloc(p, n);
        if (!P) {
                fprintf(stderr, "[%s:%lu]Out of memory(%ld bytes)\n",
                        __FILE__, line, n);
        }
        return P;
}
