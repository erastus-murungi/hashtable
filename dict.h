#ifndef HASHTABLE_DICT_H
#define HASHTABLE_DICT_H

#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdbool.h>


// For systems where SIZEOF_VOID_P is not defined, determine it
// based on __LP64__ (defined by gcc on 64-bit systems)
#if defined(__LP64__)
#define SIZEOF_VOID_P 8
#else
#define SIZEOF_VOID_P sizeof(void*)
#endif

#define MINSIZE (8)

void *safe_malloc(size_t n, unsigned long line);

void *safe_realloc(void *p, size_t n, unsigned long line);

#define SAFEMALLOC(n) safe_malloc(n, __LINE__)
#define SAFEREALLOC(p, n) safe_realloc(p, n, __LINE__)

/* The type of the dictionary key; Currently it is a double  */
typedef double dkey_t; 

/* The type of a dictionary value; Currently it is a "string" */
typedef char* dval_t; 

/* The type of a hash; Currently it is uint64_t */
typedef uint64_t hash_t;

/**
 * @brief This is an single item in the dictionary
 *      
 * An entry in the dictionary is composed of a key and a value
 * items are used when we need to iterate need to get (key, value) pair(s)
 * from the dictionary
 * 
 */
typedef struct _item
{
        dkey_t   key;
        dval_t   value;
} item;


/**
 * @brief A sized ordered set of items 
 * 
 */
typedef struct _itemobj {
        item*   items;
        ssize_t n_items;
} itemobj;

/**
 * @brief A sized ordered set of keys
 * 
 */
typedef struct _keyobj {
        dkey_t* key;
        ssize_t n_keys;
} keyobj;

/**
 * @brief A sized ordered set of values
 * 
 */
typedef struct _valobj {
        dval_t * vals;
        ssize_t n_vals;
} valobj;

/**
 * @brief A single entry in our dictionary
 * 
 * We internally represent an entry in our dictionary as a tuple of 
 *      (
 *              value,
 *              key,
 *              hash(key)
 *      )
 * It is important to store the hash of the key to avoid expensive recomputations
 * 
 * @note This is different of struct _item. 
 * struct _item is only used as a return type.
 * 
 */
struct _entry
{
        hash_t et_hashval;
        dkey_t et_key;
        dval_t et_value;
};
typedef struct _entry dt_entry;

/**
 * @brief A representation of the list of dt_entry values
 * 
 * See https://user-images.githubusercontent.com/21957448/186776267-1c46bbb2-4f2f-4b91-a3db-6d3f1bad8cbc.png
 * for an illustration
 * 
 * 
 * ar_items has `ar_allocated` total slots.
 * ar_items has `ar_free` free slots
 * 
 */
typedef struct _list_of_entries
{
        dt_entry**              ar_items;
        ssize_t                 ar_free;
        ssize_t                 ar_used;           // used = dummies + nentries
        ssize_t                 ar_allocated;
        unsigned short          ar_isfirst;
} list;


typedef struct _dict
{
        list         dt_entries;        // entries in order
        ssize_t      dt_free;           // frees
        ssize_t      dt_nentries;       // active entries
        void*        dt_indices;        // indices
        ssize_t      dt_allocated;      // all of it
        ssize_t      dt_used;           // active + dummies
} dict;

list *array_create(ssize_t initial_size);

ssize_t array_lookup(list *arr, dt_entry *en);

int array_append(list *arr, dt_entry *item);

void array_insert(list *arr, ssize_t index, dt_entry *item);

void array_delete(list *arr, ssize_t index);

dt_entry array_pop(list *arr);

int array_extend(list *arr, dt_entry **ens, ssize_t nd);

dt_entry *array_getitem(list *arr, ssize_t ix);

void array_free(list *arr);

static int array_setitem(list *arr, ssize_t ix, dt_entry *entry);

void array_free_items(list *arr);

int arr_remove_entry(list *arr, ssize_t ix);

int array_grow(list *arr, ssize_t n);

int array_clear(list *arr);

ssize_t array_size(list *arr);


// return hash(key)
hash_t hash(dkey_t key);

/**
 * @brief create a new empty dictionary with no entries and MINSIZE total slots
 * 
 * @return dict* 
 */
dict*
dict_new_empty();

dict*
dict_new_initialized(dkey_t *keys, dval_t *values, size_t n);

int
dict_contains(dict *dict, dkey_t key);

int dict_repr(dict *dt, FILE *stream);

static void dict_print(dict *dt) {
        if (!dt) {
                fprintf(stderr, "null ptr");
                return;
        }
        if (dict_repr(dt, stdout) == -1 )
                fprintf(stderr, "ERROR\n");
        printf("\n");

}

int
dict_delitem(dict *dt, dkey_t key);

int dict_insert_with_hash(dict *dt, hash_t hash, const dkey_t *key, const dval_t *value);

int dict_insert(dict *dt, dkey_t key, dval_t value);

ssize_t dict_lookup(dict *dt, hash_t h, dkey_t key, volatile dval_t *value);

void print_indices(dict *dt);

void dict_printitems(itemobj *it);

int dict_freeitems(itemobj *it);

int dict_free(dict *dt);

keyobj *dict_getkeys(dict *dt);

itemobj *dict_getitems(dict *dt);

int dict_freekeys(keyobj *);

void dict_printkeys(keyobj *);

valobj *dict_getvalues(dict *dt);

int dict_freevalues(valobj *v);

void dict_printvalues(valobj *v);

dict *dict_copy(dict *o);

int dict_update(dict *a, dict *b, int override);

dict *dict_merge(dict *a, dict *b, int override);

int dict_equal(dict *a, dict *b);

void dict_printinfo(dict *dt);

/**
 * @brief The total number of active entries in the dictionary
 * 
 * @note 
 * This does not include dummies
 * 
 * @param dt A pointer to a dictionary object
 * @return ssize_t 
 */
ssize_t dict_size(dict *dt);

int dict_is_empty(dict *dt);

int dict_clear(dict *dt);

dval_t dict_getvalue(dict *dt, dkey_t key);

dval_t dict_getvalue_knownhash(dict *dt, hash_t h, dkey_t key);

int dict_getitem(dict *dt, dkey_t key, item *it);

dict* dict_new_presized(ssize_t nentries);

void dict_printitem(item it);

#endif //HASHTABLE_DICT_H
