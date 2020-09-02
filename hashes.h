//
// Created by Erastus Murungi on 5/26/20.
//

#ifndef HASHTABLE_HASHES_H
#define HASHTABLE_HASHES_H

#include <math.h>

hash_t hash_int(dkey_t key)
{
        return (key == -1) ? -2: key;

}

hash_t
hash_raw_pointer(const void *p)
{
        size_t y = (size_t) p;
        /* bottom 3 or 4 bits are likely to be 0; rotate y by 4 to avoid
           excessive hash collisions for dicts and sets */
        y = (y >> 4) | (y << (8 * SIZEOF_VOID_P - 4));
        return (hash_t) y;
}

hash_t dbj2(unsigned char *str)
{
        hash_t hash = 5381;
        int c;

        while ((c = *str++))
                hash = ((hash << (hash_t) 5) + hash) + c;

        return hash;
}

hash_t java_hash(unsigned char *str, unsigned int n)
{
        hash_t t = 0;
        unsigned int c, i = 1;
        while ((c = *str++))
                t += (c * 31 ^ (n - i++));
        return t;
}

hash_t
hash_pointer(const void *p)
{
        hash_t x = hash_raw_pointer(p);
        if (x == -1) {
                x = -2;
        }
        return x;
}


#if SIZEOF_VOID_P >= 8
#  define HASH_BITS 61
#else
#  define HASH_BITS 31
#endif

#define HASH_INF 314159
#define HASH_NAN 0

#define HASH_MODULUS (((size_t)1 << HASH_BITS) - 1)


#ifndef IS_INFINITY
        #define IS_INFINITY(X) isinf(X)
#endif


#ifndef IS_FINITE
        #define IS_FINITE(X) isfinite(X)
#endif

hash_t
hash_double(double v)
{
        int e, sign;
        double m;
        hash_t x, y;

        if (!IS_FINITE(v)) {
                if (IS_INFINITY(v))
                        return v > 0 ? HASH_INF : -HASH_INF;
                else
                        return HASH_NAN;
        }

        m = frexp(v, &e);

        sign = 1;
        if (m < 0) {
                sign = -1;
                m = -m;
        }

        /* process 28 bits at a time;  this should work well both for binary
           and hexadecimal floating point. */
        x = 0;
        while (m) {
                x = ((x << 28) & HASH_MODULUS) | x >> (HASH_BITS - 28);
                m *= 268435456.0;  /* 2**28 */
                e -= 28;
                y = (hash_t)m;  /* pull out integer part */
                m -= y;
                x += y;
                if (x >= HASH_MODULUS)
                        x -= HASH_MODULUS;
        }

        /* adjust for the exponent;  first reduce it modulo HASH_BITS */
        e = e >= 0 ? e % HASH_BITS : HASH_BITS-1-((-1-e) % HASH_BITS);
        x = ((x << e) & HASH_MODULUS) | x >> (HASH_BITS - e);

        x = x * sign;
        if (x == (hash_t)-1)
                x = (hash_t)-2;
        return (hash_t)x;
}

hash_t hash(dkey_t key) {
        return hash_double(key);
}


#endif //HASHTABLE_HASHES_H
