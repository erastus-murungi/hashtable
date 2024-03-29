//
// Created by Erastus Murungi on 5/24/20.
//

#include "dict.h"

#include <string.h>

#include "hashes.h"

/* The total number of slots in the dictionary */
#define DT_SIZE(dt) (dt->dt_allocated_count)

#define DT_MASK(dt) ((dt)->dt_allocated_count - 1)

/* The total number of used entry slots */
#define DT_USED(dt) ((dt)->dt_entries.ar_used_count)

/* Append an entry to the entries array*/
#define DT_ADD_TO_ENTRIES(dt, entry) array_append (&dt->dt_entries, entry)

/* entries_array[ix].value = value */
#define DT_SET_VALUE(dt, ix, value)                                           \
  (array_getitem (&dt->dt_entries, ix)->et_value = value)

#define IS_POWER_OF_2(x) (((x) & (x - 1)) == 0)

#define DT_GET_ENTRY(dt, ix) ((dt)->dt_entries.ar_items[ix])

#define DT_LAST_ENTRY(dt) (dt->dt_entries.ar_items[dt->dt_entries.ar_used - 1])

#define DT_SET_ENTRY(dt, ix, entry)                                           \
  (array_setitem (&(dt->dt_entries), ix, entry))

#define PERTURB_SHIFT ((unsigned)5)

enum
{
  EMPTY = (-1),
  DUMMY = (-2),
  DICT_IS_NULL = (-3),
  KEY_IS_NULL = (-4),
} LookupStatus;

#define NONE (NULL)

#define USABLE_FRACTION(n) (((n) << 1) / 3)

#define ESTIMATE_SIZE(n) (((((n)*3) + 1)) >> 1)

#define GROW(d) ((d)->dt_active_entries_count * 3)

#define ACTUAL_SIZE(size)                                                     \
  (IS_POWER_OF_2 (size) ? size : (1 << (64 - __builtin_clzl (size))))

#define DT_ENTRIES(dt) (dt->dt_entries.ar_items)

#define NEEDS_RESIZING(dt) (dt->dt_free_count <= 0)

#define MIN_NUM_ENT (5)

static inline void dictkeys_set_index (dict *keys, ssize_t i, ssize_t ix);

static inline ssize_t dictkeys_get_index (const dict *dt, ssize_t i);

ssize_t dict_new_index (dict *dt, ssize_t minsize);

static void build_indices (dict *dt);

#ifdef PROBE

static int N = 0;

static double average = 0;

#endif

static inline void
assert_consistent (dict *dt)
{
  ssize_t usable = USABLE_FRACTION (dt->dt_allocated_count);

  assert (0 <= dt->dt_used_count && dt->dt_used_count <= usable);
  assert (IS_POWER_OF_2 (dt->dt_allocated_count));
  assert (0 <= dt->dt_free_count && dt->dt_free_count <= usable);
  assert (0 <= dt->dt_active_entries_count
          && dt->dt_active_entries_count <= usable);
  assert (dt->dt_free_count + dt->dt_active_entries_count <= usable);
}

static dt_entry *
zip_to_entries (dkey_t *keys, dval_t *values, ssize_t n)
{
  if (!keys || n <= 0)
    {
      printf ("Keys must be provided.\n");
      return NULL;
    }
  dt_entry *entries = SAFEMALLOC (sizeof (dt_entry) * n);
  if (values)
    {
      for (ssize_t i = 0; i < n; i++)
        {
          entries[i] = (dt_entry){ hash (keys[i]), keys[i], values[i] };
        }
    }
  else
    {
      for (ssize_t i = 0; i < n; i++)
        {
          entries[i] = (dt_entry){ hash (keys[i]), keys[i], NULL };
        }
    }
  return entries;
}

dict *
dict_new_presized (size_t nentries)
{
  dict *d = SAFEMALLOC (sizeof (dict));
  entry_list *arr = array_create (nentries);
  if (!arr)
    {
      fprintf (stderr, "array create failed\n");
      return NULL;
    }
  ssize_t estimate = ACTUAL_SIZE (ESTIMATE_SIZE (nentries));

  *d = (dict){ .dt_entries = *arr,
               .dt_free_count = USABLE_FRACTION (estimate),
               .dt_active_entries_count = 0,
               .dt_indices = NULL,
               .dt_used_count = 0,
               .dt_allocated_count = 0 };
  free (arr);
  if (dict_new_index (d, estimate) < 0)
    {
      fprintf (stderr, "dict new index error\n");
      return NULL;
    }
  return d;
}

dict *
dict_new_initialized (dkey_t *keys, dval_t *values, size_t n)
{
  if (!keys)
    {
      fprintf (stderr, "keys is null\n");
      exit (EXIT_FAILURE);
    }
  if (n == 0)
    {
      return dict_new_empty ();
    }
  else
    {
      dict *d = dict_new_presized (n);
      for (ssize_t i = 0; i < n; i++)
        {
          dval_t *val = (values == NULL) ? NULL : &values[i];
          if (dict_insert_with_hash (d, hash (keys[i]), &keys[i], val)
              > OK_REPLACED)
            {
              return NULL;
            }
        }
      assert_consistent (d);
      return d;
    }
}

dict *
dict_new_empty (void)
{
  dict *d = SAFEMALLOC (sizeof (dict));

  entry_list *arr = array_create (MINSIZE);
  *d = (dict){
    .dt_entries = *arr,
    .dt_free_count = MIN_NUM_ENT,
    .dt_active_entries_count = 0,
    .dt_indices = NULL,
    .dt_used_count = 0,
    .dt_allocated_count = MINSIZE,
  };
  free (arr);
  dict_new_index (d, MINSIZE);
  return d;
}

ssize_t
dict_new_index (dict *dt, ssize_t minsize)
{
  ssize_t s = ACTUAL_SIZE (minsize);
  int es;

  if (s <= 0xff)
    { // 255 | (2^8) - 1
      (dt->dt_indices) = SAFEMALLOC (sizeof (int8_t) * s);
      es = 1;
    }
  else if (s <= 0xffff)
    { // 65535 | (2^16) - 1
      (dt->dt_indices) = SAFEMALLOC (sizeof (int16_t) * s);
      es = 2;
    }
#if SIZEOF_VOID_P > 4
  else if (s > 0xffffffff)
    { // (2^32) - 1
      (dt->dt_indices) = SAFEMALLOC (sizeof (int64_t) * s);
      es = 8;
    }
#endif
  else
    {
      (dt->dt_indices) = SAFEMALLOC (sizeof (int32_t) * s);
      es = 4;
    }
  ssize_t ts = es * s;
  memset (dt->dt_indices, EMPTY, ts);
  dt->dt_allocated_count = s;
  if (!dt->dt_indices)
    {
      return -1;
    }
  return ts;
}

static inline ssize_t
dictkeys_get_index (const dict *dt, ssize_t i)
{
  ssize_t s = DT_SIZE (dt);
  ssize_t ix;

  if (s <= 0xff)
    { // 255 | (2^8) - 1
      const int8_t *indices = (const int8_t *)(dt->dt_indices);
      ix = indices[i];
    }
  else if (s <= 0xffff)
    { // 65535 | (2^16) - 1
      const int16_t *indices = (const int16_t *)(dt->dt_indices);
      ix = indices[i];
    }
#if SIZEOF_VOID_P > 4
  else if (s > 0xffffffff)
    { // 4294967295 | (2^32) - 1
      const int64_t *indices = (const int64_t *)(dt->dt_indices);
      ix = indices[i];
    }
#endif
  else
    {
      const int32_t *indices = (const int32_t *)(dt->dt_indices);
      ix = indices[i];
    }
  assert (ix >= DUMMY);
  return ix;
}

/* write to indices. */
static inline void
dictkeys_set_index (dict *keys, ssize_t i, ssize_t ix)
{
  ssize_t s = DT_SIZE (keys);

  assert (ix >= DUMMY);

  if (s <= 0xff)
    {
      int8_t *indices = (int8_t *)(keys->dt_indices);
      assert (ix <= 0x7f);
      indices[i] = (char)ix;
    }
  else if (s <= 0xffff)
    {
      int16_t *indices = (int16_t *)(keys->dt_indices);
      assert (ix <= 0x7fff);
      indices[i] = (int16_t)ix;
    }
#if SIZEOF_VOID_P > 4
  else if (s > 0xffffffff)
    {
      int64_t *indices = (int64_t *)(keys->dt_indices);
      indices[i] = ix;
    }
#endif
  else
    {
      int32_t *indices = (int32_t *)(keys->dt_indices);
      assert (ix <= 0x7fffffff);
      indices[i] = (int32_t)ix;
    }
}

static void
build_indices (dict *dt)
{
  dt_entry **entries = dt->dt_entries.ar_items;
  size_t mask = (size_t)DT_SIZE (dt) - 1; // mask

  ssize_t m = dt->dt_entries.ar_used_count;
  assert (m == dt->dt_used_count);
  dt_entry *entry = *(entries);
  for (ssize_t ix = 0; ix != m; entry = *(++entries), ++ix)
    {
      if (entry != NULL)
        {
          hash_t hash = entry->et_hashval;
          size_t i = hash & mask;
          for (size_t perturb = hash; dictkeys_get_index (dt, i) != EMPTY;)
            {
              perturb >>= PERTURB_SHIFT;
              i = mask & (i * 5 + perturb + 1);
            }
          dictkeys_set_index (dt, i, ix);
        }
    }
}

/**
 * if entry exists in hashtable return that entry(pos), else return empty
 * Search index of hash table from offset of entry table
 */
static ssize_t
lookdict_index (dict *dt, hash_t hash, ssize_t index)
{
  size_t mask = DT_MASK (dt);
  size_t perturb = (size_t)hash;
  size_t i = (size_t)hash & mask;

  for (;;)
    {
      ssize_t ix = dictkeys_get_index (dt, i);
      if (ix == index)
        {
          return i;
        }
      if (ix == EMPTY)
        {
          return EMPTY;
        }
      perturb >>= PERTURB_SHIFT;
      i = mask & (i * 5 + perturb + 1);
    }
}

static inline ssize_t
get_initial_probe_index (dict *dt, hash_t h)
{
  return h & DT_MASK (dt);
}

/**
 * @brief Lookup function based on algorithm D of Knuth
 *
 * @param dt a dictionary object
 * @param key_hash the hash of the key
 * @param key the key
 * @param value the value
 * @return ssize_t
 */
ssize_t
dict_lookup (dict *dt, hash_t key_hash, dkey_t key, volatile dval_t *value)
{
  if (!dt)
    {
      return DICT_IS_NULL;
    }
  // The initial probe index is computed as hash mod the table size.
  ssize_t i = get_initial_probe_index (dt, key_hash);
  int x = 0;

  ssize_t mask, perturb;
  mask = DT_MASK (dt);
  perturb = key_hash;

  for (;;)
    {
      x++;
      ssize_t ix = dictkeys_get_index (dt, i);
      if (ix == EMPTY)
        {
#ifdef PROBES
          average = ((average * (N)) + x) / (N + 1);
          N++;
#endif
          *value = NONE;
          return ix;
        }
      if (ix >= 0)
        {
          dt_entry *maybe = DT_GET_ENTRY (dt, ix);
          if ((&key == &maybe->et_key)
              || (key_hash == maybe->et_hashval && maybe->et_key == key))
            {
#ifdef PROBES
              average = ((average * (N)) + x) / (N + 1);
              N++;
#endif
              *value = maybe->et_value;
              return ix;
            }
        }
      perturb >>= PERTURB_SHIFT;
      i = mask & ((i * 5) + perturb + 1);
    }
}

dval_t
dict_getvalue_knownhash (dict *dt, hash_t h, dkey_t key)
{
  dval_t value;
  if (dict_lookup (dt, h, key, &value) < 0 || value == NONE)
    {
      return NONE;
    }
  else
    return value;
}

/**
 * @brief find slot for an item from its hash
          when it is known that the key is not present in the dict
 *
 * @param dt a dictionary
 * @param hash hash of the index of an item
 * @return ssize_t an index
 */
static ssize_t
find_empty_slot (dict *dt, hash_t hash)
{
  assert (dt != NULL);

  size_t i = get_initial_probe_index (dt, hash);
  ssize_t ix = dictkeys_get_index (dt, i);

  const size_t mask = DT_MASK (dt);

  for (size_t perturb = hash; ix >= 0;)
    {
      perturb >>= PERTURB_SHIFT;
      i = (i * 5 + perturb + 1) & mask;
      ix = dictkeys_get_index (dt, i);
    }
  return i;
}

static inline int
dict_resize (dict *dt, ssize_t minsize)
{
  assert (dt && minsize >= MINSIZE);
  free (dt->dt_indices);
  dt->dt_indices = NULL;
  if (dict_new_index (dt, minsize) == -1)
    {
      fprintf (stderr, "Memory Error\n");
      return -1;
    }
  build_indices (dt);
  dt->dt_free_count
      = USABLE_FRACTION (dt->dt_allocated_count) - dt->dt_active_entries_count;
  return 0;
}

int
dict_insert (dict *dt, dkey_t key, dval_t value)
{
  hash_t h = hash (key);
  int ret = dict_insert_with_hash (dt, h, &key, &value);
  assert_consistent (dt);
  return ret;
}

/**
 * @brief Insert a 3-tuple of (key, value, hash(key) into a dictionary)
 *
 * @param dt the dictionary object
 * @param hash
 * @param key
 * @param value
 * @return int (-1) if any input value is invalid
 *             (0)  if an entirely new
 *             (1)  if key was already in the dictionary
 */
int
dict_insert_with_hash (dict *dt, hash_t hash, const dkey_t *key,
                       const dval_t *value)
{
  if (!value || !key || !dt)
    return INVALID_INPUT;

  dval_t oldvalue;
  // lookup the key, while simulatneously retrieving the value if the key
  // exists
  ssize_t ix = dict_lookup (dt, hash, *key, &oldvalue);

  if (ix == EMPTY)
    // key was not found, so we insert it
    {
      if (NEEDS_RESIZING (dt))
        {
          dict_resize (dt, GROW (dt));
        }
      // we allocate space for a new entry
      dt_entry *new_entry = SAFEMALLOC (sizeof (dt_entry));
      *new_entry = (dt_entry){ hash, *key, *value };
      // we add the entry to the entry_list of entrys
      DT_ADD_TO_ENTRIES (dt, new_entry);
      ssize_t hashpos = find_empty_slot (dt, hash);
      dictkeys_set_index (dt, hashpos, DT_USED (dt) - 1);
      dt->dt_used_count++;
      dt->dt_free_count--;
      dt->dt_active_entries_count++;
      return OK;
    }
  else if (oldvalue != NONE && (oldvalue != *value))
    // key was found, so we overwrite the value at `ix`
    {
      DT_SET_VALUE (dt, ix, *value);
      return OK_REPLACED;
    }
  return INTERNAL_ERROR;
}

dval_t
dict_getvalue (dict *dt, dkey_t key)
{
  if (!dt)
    {
      fprintf (stderr, "null pointer\n");
    }
  hash_t h = hash (key);
  return dict_getvalue_knownhash (dt, h, key);
}

int
dict_getitem (dict *dt, dkey_t key, item *it)
{
  if (!dt)
    {
      return -1;
    }
  dval_t val = dict_getvalue (dt, key);
  if (!val)
    {
      return 1;
    }
  else
    {
      *it = (item){ key, val };
      return 0;
    }
}

valset *
dict_getvalues (dict *dt)
{
  if (!dt)
    {
      fprintf (stderr, "Null pointer\n");
      return NULL;
    }
  if (dt->dt_active_entries_count == 0)
    {
      valset *v = SAFEMALLOC (sizeof (valset));
      v->n_vals = 0;
      v->vals = NULL;
      return v;
    }
  else
    {
      valset *v = SAFEMALLOC (sizeof (valset));
      dval_t *values
          = SAFEMALLOC (sizeof (dval_t) * dt->dt_active_entries_count);
      v->vals = values;
      v->n_vals = dt->dt_active_entries_count;
      dt_entry **entries = DT_ENTRIES (dt);

      for (ssize_t i = 0, j = 0, m = dt->dt_used_count; i < m; i++, entries++)
        if (entries)
          values[j++] = (*entries)->et_value;

      return v;
    }
}

int
dict_freevalues (valset *v)
{
  if (!v)
    return -1;
  if (v->n_vals == 0)
    {
      free (v);
    }
  else
    {
      free (v->vals);
      free (v);
    }
  return 0;
}

static void repr_val (dval_t x, FILE *stream);

void
dict_printvalues (valset *v)
{
  if (!v)
    {
      fprintf (stderr, "Null pointer\n");
    }
  else if (v->n_vals == 0)
    {
      printf ("dict_values([])\n");
    }
  else
    {
      printf ("dict_values([");
      repr_val (v->vals[0], stdout);
      printf (",\n");
      ssize_t m = v->n_vals - 1;
      for (ssize_t i = 1; i < m; i++)
        {
          printf ("             ");
          repr_val (v->vals[i], stdout);
          printf (",\n");
        }
      printf ("             ");
      repr_val (v->vals[m], stdout);
      printf ("])\n");
    }
}

keyset *
dict_getkeys (dict *dt)
{
  if (!dt)
    {
      return NULL;
    }
  keyset *ko = SAFEMALLOC (sizeof (*ko));
  if (dt->dt_active_entries_count == 0)
    {
      ko->key = NULL;
      ko->n_keys = 0;
    }
  else
    {
      dkey_t *keys = SAFEMALLOC (sizeof (*keys) * dt->dt_active_entries_count);
      ko->key = keys;
      ko->n_keys = dt->dt_active_entries_count;
      dt_entry **entries = DT_ENTRIES (dt);
      for (ssize_t i = 0, j = 0, m = dt->dt_entries.ar_used_count; i < m; i++)
        if (entries[i])
          keys[j++] = entries[i]->et_key;
    }
  return ko;
}

int
dict_freekeys (keyset *k)
{
  if (!k)
    return -1;
  if (k->n_keys == 0)
    {
      free (k);
    }
  else
    {
      free (k->key);
      free (k);
    }
  return 0;
}

static void repr_key (dkey_t x, FILE *stream);

void
dict_printkeys (keyset *keyobj)
{
  if (!keyobj)
    {
      fprintf (stderr, "Null pointer\n");
    }
  else if (keyobj->n_keys == 0)
    {
      printf ("dict_keys([])\n");
    }
  else
    {
      printf ("dict_keys([");
      repr_key (keyobj->key[0], stdout);
      printf (",\n");
      ssize_t m = keyobj->n_keys - 1;
      for (ssize_t i = 1; i <= m; i++)
        {
          printf ("           ");
          repr_key (keyobj->key[i], stdout);
          printf (",\n");
        }
      printf ("           ");
      repr_key (keyobj->key[m], stdout);
      printf ("])\n");
    }
}

itemset *
dict_getitems (dict *dt)
{
  if (!dt)
    {
      return NULL;
    }
  if (dt->dt_active_entries_count == 0)
    {
      itemset *it = SAFEMALLOC (sizeof (itemset));
      it->n_items = 0;
      it->items = NULL;
      return it;
    }
  itemset *it = SAFEMALLOC (sizeof (itemset));
  item *items = SAFEMALLOC (sizeof (item) * dt->dt_active_entries_count);
  dt_entry **entries = DT_ENTRIES (dt);

  item t;
  for (ssize_t i = 0, j = 0; i < dt->dt_entries.ar_used_count; i++)
    {
      if (entries[i] != NULL)
        {
          t = (item){ .key = entries[i]->et_key,
                      .value = entries[i]->et_value };
          items[j++] = t;
        }
    }
  it->items = items;
  it->n_items = dt->dt_entries.ar_used_count;
  return it;
}

int
dict_contains (dict *dict, dkey_t key)
{
  if (!dict)
    return -1;
  ssize_t ix;
  dval_t value;
  hash_t h = hash (key);
  ix = dict_lookup (dict, h, key, &value);
  if (ix == DICT_IS_NULL)
    return -1;
  return (ix != EMPTY && value != NONE);
}

static void
repr_val (dval_t x, FILE *stream)
{
  fprintf (stream, "%s", x);
}

static void
repr_key (dkey_t x, FILE *stream)
{
  fprintf (stream, "%10.3f", x);
}

static void
repr_item (item *t, FILE *stream)
{
  fprintf (stream, "(");
  repr_key (t->key, stream);
  fprintf (stream, ", ");
  repr_val (t->value, stream);
  fprintf (stream, ")");
}

int
dict_repr (dict *dt, FILE *stream)
{
  if (!dt)
    return -1;
  if (dt->dt_active_entries_count == 0)
    {
      fprintf (stream, "dict([])");
      return 1;
    }
  else
    {
      fprintf (stream, "dict([");
      ssize_t m = dt->dt_active_entries_count;
      bool first = true;
      dt_entry *entry = dt->dt_entries.ar_items[0];
      for (ssize_t i = 0; i < dt->dt_entries.ar_used_count;
           entry = dt->dt_entries.ar_items[++i])
        {
          if (entry)
            {
              if (first)
                first = false;
              else
                fprintf (stream, "     ");

              repr_key (entry->et_key, stream);
              fprintf (stream, " : ");
              repr_val (entry->et_value, stream);

              if (--m)
                fprintf (stream, ",\n ");
            }
        }
    }
  printf ("])\n");
  return 0;
}

int
dict_delitem (dict *dt, dkey_t key)
{
  hash_t h = hash (key);
  dval_t oldvalue;

  ssize_t index = dict_lookup (dt, h, key, &oldvalue);
  if (index < 0 || oldvalue == NONE)
    return -1; // key not found
  ssize_t i = lookdict_index (dt, h, index);

  dictkeys_set_index (dt, i, DUMMY);

  if (arr_remove_entry (&dt->dt_entries, index) == -1)
    {
      return -1;
    }
  dt->dt_active_entries_count -= 1;
  return 0;
}

void
print_indices (dict *dt)
{
  if (!dt)
    {
      fprintf (stderr, "ERROR");
      return;
    }
  ssize_t s = dt->dt_allocated_count;
  ssize_t m = s;
  if (s <= 0xff)
    { // 255 | (2^8) - 1
      printf ("[");
      const int8_t *indices = (const int8_t *)dt->dt_indices;
      int8_t t = indices[0];
      for (ssize_t i = 0; i < dt->dt_allocated_count; t = indices[++i])
        {
          switch (t)
            {
            case (DUMMY):
              printf ("DUMMY");
              break;
            case (EMPTY):
              printf ("EMPTY");
              break;
            default:
              printf ("%d", t);
            }
          if (--m)
            printf (",");
        }
      printf ("]\n");
    }
  else if (s <= 0xffff)
    { // 65535 | (2^16) - 1
      printf ("[");
      const int16_t *indices = (const int16_t *)dt->dt_indices;
      int16_t t = indices[0];
      for (ssize_t i = 0; i < dt->dt_allocated_count; t = indices[++i])
        {
          switch (t)
            {
            case (DUMMY):
              printf ("DUMMY");
              break;
            case (EMPTY):
              printf ("EMPTY");
              break;
            default:
              printf ("%d", t);
            }
          if (--m)
            printf (",");
        }
      printf ("]\n");
#if SIZEOF_VOID_P > 4
    }
  else if (s > 0xffffffff)
    { // (2^32) - 1
      printf ("[");
      const int64_t *indices = (const int64_t *)dt->dt_indices;
      int64_t t = indices[0];
      for (ssize_t i = 0; i < dt->dt_allocated_count; t = indices[++i])
        {
          switch (t)
            {
            case (DUMMY):
              printf ("DUMMY");
              break;
            case (EMPTY):
              printf ("EMPTY");
              break;
            default:
              printf ("%lld", t);
            }
          if (--m)
            printf (",");
        }
      printf ("]\n");
    }
#endif
  else
    {
      printf ("[");
      const int32_t *indices = (const int32_t *)dt->dt_indices;
      int32_t t = indices[0];
      for (ssize_t i = 0; i < dt->dt_allocated_count; t = indices[++i])
        {
          switch (t)
            {
            case (DUMMY):
              printf ("DUMMY");
              break;
            case (EMPTY):
              printf ("EMPTY");
              break;
            default:
              printf ("%d", t);
            }
          if (--m)
            printf (",");
        }
      printf ("]\n");
    }
}

void
dict_printitem (item it)
{
  repr_item (&it, stdout);
  printf ("\n");
}

void
dict_printitems (itemset *it)
{
  ssize_t n = it->n_items;
  item *item = it->items;
  if (n == 0)
    {
      printf ("dict_items([])\n");
      return;
    }

  printf ("dict_items([");
  repr_item (&item[0], stdout);
  printf (",\n");

  for (ssize_t i = 1; i < n - 1; i++)
    {
      printf ("            ");
      repr_item (&item[i], stdout);
      printf (",\n");
    }
  printf ("            ");
  repr_item (&item[n - 1], stdout);
  printf ("])\n");
}

int
dict_freeitems (itemset *it)
{
  if (!it)
    {
      return -1;
    }
  if (it->n_items == 0)
    {
      free (it);
    }
  else
    {
      free (it->items);
      free (it);
    }
  return -1;
}

int
dict_free (dict *dt)
{
  if (!dt)
    {
      fprintf (stderr, "NULL POINTER\n");
      return -1;
    }
  assert (IS_POWER_OF_2 ((dt->dt_allocated_count)));
  array_free_items (&dt->dt_entries);
  free (dt->dt_indices);
  return 1;
}

int
dict_clear (dict *dt)
{
  if (!dt)
    {
      return -1; /* null_pointer*/
    }
  free (dt->dt_indices);
  dict_new_index (dt, MINSIZE);
  if (!dt->dt_indices)
    return -1;
  dt->dt_used_count = 0;
  dt->dt_active_entries_count = 0;
  dt->dt_free_count = MIN_NUM_ENT;
  if (array_clear (&dt->dt_entries) != 0)
    {
      return -1;
    }
  assert_consistent (dt);
  return 0;
}

dict *
dict_copy (dict *o)
{
  if (!o)
    return NULL;
  if (o->dt_active_entries_count == 0)
    return dict_new_empty ();

  assert (o);
  dict *new = SAFEMALLOC (sizeof (dict));
  memcpy (new, o, sizeof (dict));
  if (!new)
    {
      return NULL;
    }

  ssize_t keys_size = DT_SIZE (o);
  ssize_t d = dict_new_index (new, keys_size);
  if (d == -1)
    return NULL;
  memcpy (new->dt_indices, o->dt_indices, d);
  /* After copying key/value pairs, we need to incref all
     keysobj and valset and they are about to be co-owned by a
     new dict object. */

  dt_entry **entries = DT_ENTRIES (o);
  entry_list *entries_copy = array_create (o->dt_active_entries_count);
  if (!entries_copy)
    return NULL;
  entries_copy->ar_isfirst = ~entries_copy->ar_isfirst;

  ssize_t n = o->dt_used_count;
  for (ssize_t i = 0; i < n; i++)
    {
      dt_entry *entry = entries[i];
      dval_t value = entry->et_value;
      if (value != NULL)
        {
          array_append (entries_copy, entry);
        }
    }
  new->dt_entries = *entries_copy;
  assert_consistent (new);
  return new;
}

ssize_t
dict_size (dict *dt)
{
  if (dt == NULL)
    {
      fprintf (stderr, "Error\n");
      return -1;
    }
  return (dt)->dt_active_entries_count;
}

static int
val_eq (dval_t *self, dval_t *other)
{
  return strcmp (*self, *other) == 0;
}

static int
key_eq (const dkey_t *self, const dkey_t *other)
{
  return *self == *other;
}

int
dict_update (dict *a, dict *b, int override)
{
  ssize_t i, n;
  dt_entry **ep0;
  dt_entry *entry;

  if (override != 1 && override != 0)
    {
      return -1;
    }
  if (a == NULL || b == NULL)
    {
      return -1;
    }
  if (a == b || b->dt_used_count == 0)
    {
      /* a.update(a) or a.update({}); nothing to do */
      return 0;
    }
  // resize index
  if (USABLE_FRACTION (a->dt_allocated_count)
      < b->dt_active_entries_count + a->dt_used_count)
    {
      if (dict_resize (a, ESTIMATE_SIZE (a->dt_used_count + b->dt_used_count))
          != 0)
        {
          return -1;
        }
    }
  // might need to resize the entries array too
  if (array_grow (&a->dt_entries,
                  a->dt_entries.ar_used_count + b->dt_active_entries_count)
      == -1)
    {
      fprintf (stderr, "Memory full\n");
      return -1;
    }
  ep0 = DT_ENTRIES (b);
  for (i = 0, n = b->dt_active_entries_count; i < n; i++)
    {
      dkey_t key;
      dval_t value;
      hash_t hash;

      entry = ep0[i];
      key = entry->et_key;
      hash = entry->et_hashval;
      value = entry->et_value;

      if (value != NULL)
        {
          int err;
          if (override == 1)
            {
              err = dict_insert_with_hash (a, hash, &key, &value);
            }
          else
            {
              if (dict_getvalue_knownhash (a, hash, key) == NULL)
                err = dict_insert_with_hash (a, hash, &key, &value);
              else
                {
                  err = 0;
                }
            }
          if (err != 0)
            return -1;

          if (n != b->dt_active_entries_count)
            {
              fprintf (stderr, "dict mutated during update");
              return -1;
            }
        }
    }
  assert_consistent (a);
  return 0;
}

dict *
dict_merge (dict *a, dict *b, int override)
{
  dict *a_copy = dict_copy (a);
  if (!a_copy)
    {
      return NULL;
    }
  if ((dict_update (a_copy, b, override) != 0))
    {
      return NULL;
    }
  assert_consistent (a_copy);
  return a_copy;
}

int
dict_equal (dict *a, dict *b)
{
  ssize_t i;

  if (a->dt_active_entries_count != b->dt_active_entries_count)
    return 0;
  for (i = 0; i < a->dt_used_count; i++)
    {
      dt_entry *ep = DT_ENTRIES (a)[i];
      dval_t a_val = ep->et_value;
      if (a_val != NULL)
        {
          int cmp;
          dval_t b_val;
          dkey_t key = ep->et_key;

          ssize_t er = dict_lookup (b, ep->et_hashval, key, &b_val);
          if (b_val == NULL || er == DICT_IS_NULL)
            {
              return 0;
            }

          cmp = val_eq (&a_val, &b_val);
          if (cmp <= 0)
            return cmp;
        }
    }
  return 1;
}

ssize_t
dict_sizeof (dict *dt)
{
  if (!dt)
    {
      fprintf (stderr, "null pointer\n");
      return -1;
    }

  size_t t = 0;
  /* size of entries */
  t += sizeof (dt->dt_entries);
  t += (sizeof (dt_entry) * dt->dt_active_entries_count)
       + (sizeof (dt_entry *) * dt->dt_entries.ar_allocated_count);
  t += sizeof (dict);

  /* sizeof indices*/
  ssize_t s = DT_SIZE (dt);
  assert (IS_POWER_OF_2 (s));
  int es;
  if (s <= 0xff)
    es = 1;
  else if (s <= 0xffff)
    es = 2;
#if SIZEOF_VOID_P > 4
  else if (s > 0xffffffff)
    es = 8;
#endif
  else
    es = 4;
  t += dt->dt_allocated_count * es;
  return (ssize_t)t;
}

void
dict_printinfo (dict *dt)
{
  if (!dt)
    fprintf (stderr, "null pointer\n");
  else
    {
      printf ("\033[1m\033[32m--Dictionary Attributes--:\033[0m\n");
      printf ("< size in bytes   : \033[0m\033[33m%zd bytes\033[0m\n",
              dict_sizeof (dt));
#ifdef PROBES
      printf ("  avg no. probes  : \033[0m\033[33m%.2f\033[0m\n", average);
#endif
      printf ("  allocated       : \033[0m\033[33m%zd\033[0m\n",
              dt->dt_allocated_count);
      printf ("  used            : \033[0m\033[34m%zd\033[0m\n",
              dt->dt_used_count);
      printf ("  nentries        : \033[0m\033[34m%zd\033[0m\n",
              dt->dt_active_entries_count);
      printf ("  free            : \033[0m\033[32m%zd\033[0m\n",
              dt->dt_free_count);
      printf ("  load factor     : \033[1m\033[35m%.3f\033[0m />\n",
              ((double)dt->dt_used_count / (double)dt->dt_allocated_count));
    }
}

static void
build_indices_open_addressing_linear (dict *dt, ssize_t n)
{
  dt_entry **entries = DT_ENTRIES (dt);
  size_t mask = DT_MASK (dt);
  for (size_t ix = 0, i = 0; ix < n; ix++)
    {
      i = entries[i]->et_hashval & mask;
      while (dictkeys_get_index (dt, i) != EMPTY)
        i = (i + 1) & mask;
      dictkeys_set_index (dt, i, ix);
    }
}

int
dict_is_empty (dict *dt)
{
  if (!dt)
    {
      return -1;
    }
  return dt->dt_active_entries_count == 0;
}
