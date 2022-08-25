#include "dict.h"
#include <stdio.h>
#include <sys/time.h>

/*
 * instructions change from NONE {NULL, EMPTY}
 * change repr_key, repr_val
 * change dval_t, dkey_t
 * change val_eq & key_eq
 */

#define _diffsec(start, end) (end.tv_sec - start.tv_sec)

#define _diffnsec(start, end) ((end.tv_nsec - start.tv_nsec))

#define diffmilli(start, end)                                                 \
  (_diffsec (start, end) * 1e3 + _diffnsec (start, end) / 1e6)

#define diffmicro(start, end)                                                 \
  (_diffsec (start, end) * 1e6 + _diffnsec (start, end) / 1e3)

#define diffnano(start, end)                                                  \
  (_diffsec (start, end) * 1e9 + _diffnsec (start, end))

void test_dict_insert (ssize_t maxlen);

void test_dict_initialized (ssize_t maxlen);

int
main ()
{
  test_dict_insert (4000000);
  return EXIT_SUCCESS;
}

struct timeval tv;
static char *
randstring (uint max_length)
{
  gettimeofday (&tv, NULL);
  srandom (tv.tv_usec);
  uint rand_length = 1 + ((unsigned long)random () % max_length);
  char *string = SAFEMALLOC (sizeof (*string) * (rand_length + 1));

  for (uint i = 0; i < rand_length; i++)
    {
      string[i] = 50 + (char)(random () % (25));
    }
  string[rand_length] = '\0';
  return string;
}

static inline void
free_strings (dval_t *strings, size_t n)
{
  for (size_t i = 0; i < n; i++)
    {
      free (strings[i]);
    }
  free (strings);
}

/* generate a random floating point number from min to max */
double
randfrom (double min, double max)
{
  double range = (max - min);
  double div = RAND_MAX / range;
  return min + (random () / div);
}

void
test_dict_insert (ssize_t maxlen)
{
  dval_t *values = SAFEMALLOC (sizeof (*values) * maxlen);
  dkey_t *keys = SAFEMALLOC (sizeof (*keys) * maxlen);

  for (ssize_t i = 0; i < maxlen; i++)
    {
      keys[i] = randfrom (0, RAND_MAX);
      values[i] = randstring (20);
    }
  dict *mp = dict_new_empty ();

  struct timespec start, end;
  clock_gettime (CLOCK_MONOTONIC, &start);
  int n_owrites = 0;
  for (ssize_t i = 0; i < maxlen; i++)
    {
      if ((n_owrites = n_owrites + dict_insert (mp, keys[i], values[i])) == -1)
        {
          fprintf (stderr, "insertion failed\n");
          goto Fail;
        }
      if ((mp->dt_nentries - 1) != i && n_owrites < 1)
        {
          fprintf (stderr, "%d\n", n_owrites);
          goto Fail;
        }
      if (!dict_contains (mp, keys[i]))
        {
          fprintf (stderr, "Failure\n");
          goto Fail;
        }
    }
  assert (n_owrites + mp->dt_nentries == maxlen);

  clock_gettime (CLOCK_MONOTONIC, &end);

  printf ("Number of overwrites: %d\n", n_owrites);
  double time = diffmilli (start, end);
  printf ("time taken for %zd `insert`s and `dict_contains`: %.5f ms\n",
          maxlen, time);

  free (keys);
  dict_printinfo (mp);
  dict_free (mp);
  free_strings (values, maxlen);
  return;

Fail:
  free (keys);
  dict_printinfo (mp);
  dict_free (mp);
  free_strings (values, maxlen);
  exit (EXIT_FAILURE);
}

void
test_dict_initialized (ssize_t maxlen)
{
  dval_t *values = SAFEMALLOC (sizeof (*values) * maxlen);
  dkey_t *keys = SAFEMALLOC (sizeof (*keys) * maxlen);

  for (ssize_t i = 0; i < maxlen; i++)
    {
      keys[i] = randfrom (0, 1000000);
      values[i] = randstring (20);
    }
  struct timespec start, end;
  clock_gettime (CLOCK_MONOTONIC, &start);
  dict *mp = dict_new_initialized (keys, values, maxlen);
  for (ssize_t i = 0; i < maxlen; i++)
    {
      if (!(dict_contains (mp, keys[i])))
        {
          fprintf (stderr, "not contained error\n");
          goto Fail;
        }
    }
  clock_gettime (CLOCK_MONOTONIC, &end);

  double time = diffmilli (start, end);
  printf ("time taken for %zd `insert`s and `dict_contains`: %.5f ms\n",
          maxlen, time);
  if (!mp)
    {
      fprintf (stderr, "failed\n");
      goto Fail;
    }

  item it = { .key = 0.00, NULL };
  dict_getitem (mp, *keys, &it);
  dict_printitem (it);

  dict_printinfo (mp);
  dict_free (mp);
Fail:
  free (keys);
  free_strings (values, maxlen);
}
