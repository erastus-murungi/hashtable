//
// Created by Erastus Murungi on 5/24/20.
//

#include "dict.h"
#include <string.h>


#define AR_GROW(n) (n + (n >> 1))

#define AR_TRIM(n) (n << 1) / 3

#define SHOULD_GROW(arr, n) (arr->ar_allocated <= n)

#define AR_ALLOCATED_SIZE(arr) (arr->ar_allocated)

#define AR_NUM_ENTRIES(arr) (arr->ar_nentries)

#define AR_FREE_SIZE(arr) (arr->ar_free)

static inline void checkindex(list *arr, size_t index);

static inline int array_resize_helper(list *arr, ssize_t new_size)
{
        arr->ar_items = SAFEREALLOC(arr->ar_items, new_size * sizeof(dt_entry));
        if (arr->ar_items == NULL)
                return -1;
        arr->ar_allocated = new_size;
        arr->ar_free = arr->ar_allocated - arr->ar_used;
        return 0;
}

static int array_setitem(list *arr, ssize_t ix, dt_entry *en)
{
        checkindex(arr, ix);
        arr->ar_items[ix] = en;
        return 1;
}

dt_entry *array_getitem(list *arr, ssize_t ix)
{
        checkindex(arr, ix);
        return arr->ar_items[ix];
}


static inline int array_resize(list *arr)
{
        assert(arr);
        if (arr->ar_allocated <= arr->ar_used) {
                ssize_t new_size = AR_GROW(arr->ar_used);
                return array_resize_helper(arr, new_size) == -1;
        } else if (arr->ar_used > MINSIZE && arr->ar_used < (arr->ar_allocated >> 1)) {
                ssize_t new_size = AR_TRIM(arr->ar_used);
                return array_resize_helper(arr, new_size) == -1;
        }
        return 1;
}

int array_grow(list *arr, ssize_t n)
{
        if (SHOULD_GROW(arr, n)) {
                ssize_t m = AR_ALLOCATED_SIZE(arr);
                array_resize_helper(arr, AR_GROW(n));
                if (n >= AR_ALLOCATED_SIZE(arr))
                        return -1;
                if (m == AR_ALLOCATED_SIZE(arr))
                        return 1;
                return 0;
        }
        return 1;
}


list *array_create(ssize_t nentries)
{
        list *arr = SAFEMALLOC(sizeof(list));
        size_t m = AR_GROW(nentries);
        *arr = (list) {.ar_items=SAFEMALLOC(sizeof(dt_entry) * m),
                .ar_used=0,
                .ar_free=m,
                .ar_allocated=m,
                .ar_isfirst=1};
        return arr;
}

ssize_t array_lookup(list *arr, dt_entry *en)
{
        if (!arr) return -1;
        for (size_t i = 0; i < arr->ar_used; i++) {
                if (arr->ar_items[i]->et_key == en->et_key)
                        return i;
        }
        return -1;
}

int array_append(list *arr, dt_entry *item)
{
        assert(arr);
        if (array_resize(arr) == -1)
                return -1;
        arr->ar_items[arr->ar_used++] = item;
        arr->ar_free--;
        return 1;
}


static inline void checkindex(list *arr, size_t index)
{
        if ((size_t) index > (size_t) arr->ar_used) {
                fprintf(stderr, "index %zd out of bounds for ar_items of size %zd", arr->ar_used, index);
                exit(EXIT_FAILURE);
        }
}

void array_insert(list *arr, ssize_t index, dt_entry *item)
{
        assert(arr);
        checkindex(arr, index);
        array_resize(arr);
        for (ssize_t i = index; i < arr->ar_used; i++)
                arr[i + 1] = arr[i];
        arr->ar_items[index] = item;
        arr->ar_used++;
}

int array_extend(list *arr, dt_entry **ens, ssize_t nd)
{
        if (!arr || !ens)
                return -1;
        if (nd == 0)
                return 1;
        if (array_grow(arr, arr->ar_used + nd) == -1)
                return -1;

        ssize_t last = arr->ar_used;
        arr->ar_used += nd;
        arr->ar_free = arr->ar_allocated - arr->ar_used;
        memcpy(arr->ar_items + last, ens, nd * sizeof(dt_entry*));
        return 0;
}

void array_delete(list *arr, ssize_t index)
{
        assert(arr);
        assert(index >= 0 && index < arr->ar_used);
        for (ssize_t i = index + 1; i < arr->ar_used; i++)
                arr->ar_items[i - 1] = arr->ar_items[i];
        arr->ar_used--;
        array_resize(arr);
}

dt_entry array_pop(list *arr)
{
        assert(arr);
        arr->ar_used--;
        dt_entry *item = arr->ar_items[arr->ar_used];
        array_resize(arr);
        return *item;
}


void array_free(list *arr)
{
        dt_entry *entry = arr->ar_items[0];
        for (ssize_t i = 0; i < arr->ar_used; entry = arr->ar_items[++i]) {
                if (entry != NULL)
                        free(entry);
        }
        free(arr->ar_items);
        free(arr);
        list **loc = &arr;
        *loc = NULL;
}


int
arr_remove_entry(list *arr, ssize_t ix)
{
        if (!arr) {
                fprintf(stderr, "null array pointer\n");
                return -1;
        }
        assert(ix >= 0 && ix < arr->ar_used);
        dt_entry *en = array_getitem(arr, ix);
        free(en);
        array_setitem(arr, ix, NULL);
        return 1;
}

void array_free_items(list *arr)
{
        dt_entry *entry = arr->ar_items[0];
        if (arr->ar_isfirst == 1) {
                for (ssize_t i = 0; i < arr->ar_used; entry = arr->ar_items[++i]) {
                        if (entry != NULL)
                                free(entry);
                }
        }
        free(arr->ar_items);
        free(arr);
}

int array_copy(list *arr)
{
        list *arr_copy = SAFEMALLOC(sizeof(list));
        if (!arr_copy)
                return -1;
        memcpy(arr_copy, arr, sizeof(list));
        arr_copy->ar_items = SAFEMALLOC(sizeof(dt_entry *) * arr->ar_allocated);
        if (!arr_copy->ar_items)
                return -1;
        memcpy(arr_copy, arr, sizeof(dt_entry *) * arr->ar_allocated);
        arr_copy->ar_isfirst = ~arr->ar_isfirst;
        return 0;
}


int array_clear(list *arr)
{
        if (!arr)
                return -1;

        if (arr->ar_isfirst == 1) {
                dt_entry** entries = arr->ar_items;
                dt_entry *en = *entries;
                for (ssize_t i = 0; i < arr->ar_used; en = *(++entries), i++) {
                        if (en)
                                free(en);
                }
        }

        arr->ar_used = 0;
        arr->ar_free = MINSIZE;
        arr->ar_items = SAFEREALLOC(arr->ar_items, MINSIZE);

        if (!arr->ar_items)
                return -1;

        arr->ar_allocated = MINSIZE;
        return 0;
}

ssize_t array_size(list *arr) {
        if (!arr)
                return -1;
        return arr->ar_used;
}
