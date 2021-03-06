/*
 * hash.c -- Hash table module.
 *
 * **********************************************
 * CMU ARPA Speech Project
 *
 * Copyright (c) 1999 Carnegie Mellon University.
 * ALL RIGHTS RESERVED.
 * **********************************************
 * 
 * HISTORY
 * 
 * 05-May-1999	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon
 * 		Removed hash_key2hash().  Added hash_enter_bkey() and hash_lookup_bkey(),
 * 		and len attribute to hash_entry_t.
 * 
 * 30-Apr-1999	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon
 * 		Added hash_key2hash().
 * 
 * 18-Jun-97	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon
 * 		Included case sensitive/insensitive option.  Removed local, static
 * 		maintenance of all hash tables.
 * 
 * 31-Jul-95	M K Ravishankar (rkm@cs.cmu.edu) at Carnegie Mellon
 * 		Created.
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "hash.h"
#include "err.h"
#include "ckd_alloc.h"
#include "case.h"


#if 0
static void prime_sieve (int32 max)
{
    char *notprime;
    int32 p, pp;
    
    notprime = (char *) ckd_calloc (max+1, 1);
    p = 2;
    for (;;) {
	printf ("%d\n", p);
	for (pp = p+p; pp <= max; pp += p)
	    notprime[pp] = 1;
	for (++p; (p <= max) && notprime[p]; p++);
	if (p > max)
	    break;
    }
}
#endif


/*
 * HACK!!  Initial hash table size is restricted by this set of primes.  (Of course,
 * collision resolution by chaining will accommodate more entries indefinitely, but
 * efficiency will drop.)
 */
static int32 prime[] = {
    101, 211, 307, 401, 503, 601, 701, 809, 907,
    1009, 1201, 1601, 2003, 2411, 3001, 4001, 5003, 6007, 7001, 8009, 9001,
    10007, 12007, 16001, 20011, 24001, 30011, 40009, 50021, 60013, 70001, 80021, 90001,
    100003, 120011, 160001, 200003, 240007, 300007, 400009, 500009, 600011, 700001, 800011, 900001,
    -1
};


static int32 prime_size (int32 size)
{
    int32 i;
    
    for (i = 0; (prime[i] > 0) && (prime[i] < size); i++);
    if (prime[i] <= 0) {
	E_WARN("Very large hash table requested (%d entries)\n", size);
	--i;
    }
    return (prime[i]);
}


hash_table_t *hash_new (int32 size, int32 casearg)
{
    hash_table_t *h;
    
    h = (hash_table_t *) ckd_calloc (1, sizeof(hash_table_t));
    h->size = prime_size (size+(size>>1));
    h->nocase = (casearg == HASH_CASE_NO);
    h->table = (hash_entry_t *) ckd_calloc (h->size, sizeof(hash_entry_t));
    /* The above calloc clears h->table[*].key and .next to NULL, i.e. an empty table */

    return h;
}


/*
 * Compute hash value for given key string.
 * Somewhat tuned for English text word strings.
 */
static uint32 key2hash (hash_table_t *h, const char *key)
{
    register const char *cp;
    register char c;
    register int32 s;
    register uint32 hash;
    
    hash = 0;
    s = 0;
    
    if (h->nocase) {
	for (cp = key; *cp; cp++) {
	    c = *cp;
	    c = UPPER_CASE(c);
	    hash += c << s;
	    s += 5;
	    if (s >= 25)
		s -= 24;
	}
    } else {
	for (cp = key; *cp; cp++) {
	    hash += (*cp) << s;
	    s += 5;
	    if (s >= 25)
		s -= 24;
	}
    }
    
    return (hash % h->size);
}


static char *makekey (uint8 *data, int32 len, char *key)
{
    int32 i, j;
    
    if (! key)
	key = (char *) ckd_calloc (len*2 + 1, sizeof(char));
    
    for (i = 0, j = 0; i < len; i++, j += 2) {
	key[j] = 'A' + (data[i] & 0x000f);
	key[j+1] = 'J' + ((data[i] >> 4) & 0x000f);
    }
    key[j] = '\0';
    
    return key;
}


static int32 keycmp_nocase (hash_entry_t *entry, const char *key)
{
    char c1, c2;
    int32 i;
    const char *str;
    
    str = entry->key;
    for (i = 0; i < entry->len; i++) {
	c1 = *(str++);
	c1 = UPPER_CASE(c1);
	c2 = *(key++);
	c2 = UPPER_CASE(c2);
	if (c1 != c2)
	    return (c1-c2);
    }
    
    return 0;
}


static int32 keycmp_case (hash_entry_t *entry, const char *key)
{
    char c1, c2;
    int32 i;
    const char *str;
    
    str = entry->key;
    for (i = 0; i < entry->len; i++) {
	c1 = *(str++);
	c2 = *(key++);
	if (c1 != c2)
	    return (c1-c2);
    }
    
    return 0;
}


/*
 * Lookup chained entries with hash-value hash in table h for given key and return
 * associated value in *val.
 * Return value: 0 if key found in hash table, else -1.
 */
static int32 lookup (hash_table_t *h, uint32 hash, const char *key, int32 len, int32 *val)
{
    hash_entry_t *entry;
    
    entry = &(h->table[hash]);
    if (entry->key == NULL)
	return -1;
    
    if (h->nocase) {
	while (entry && ((entry->len != len) || (keycmp_nocase (entry, key) != 0)))
	    entry = entry->next;
    } else {
	while (entry && ((entry->len != len) || (keycmp_case (entry, key) != 0)))
	    entry = entry->next;
    }
    
    if (entry) {
	*val = entry->val;
	return 0;
    } else
	return -1;
}


int32 hash_lookup (hash_table_t *h, const char *key, int32 *val)
{
    uint32 hash;
    int32 len;
    
    hash = key2hash (h, key);
    len = strlen(key);
    
    return (lookup (h, hash, key, len, val));
}


int32 hash_lookup_bkey (hash_table_t *h, const char *key, int32 len, int32 *val)
{
    uint32 hash;
    char *str;
    
    str = makekey ((uint8 *)key, len, NULL);
    hash = key2hash (h, str);
    ckd_free (str);
    
    return (lookup (h, hash, key, len, val));
}


static int32 enter (hash_table_t *h, uint32 hash, const char *key, int32 len, int32 val)
{
    int32 old;
    hash_entry_t *cur, *new;
    
    if (lookup (h, hash, key, len, &old) == 0) {
	/* Key already exists */
	return old;
    }
    
    cur = &(h->table[hash]);
    if (cur->key == NULL) {
	/* Empty slot at hashed location; add this entry */
	cur->key = key;
	cur->len = len;
	cur->val = val;
    } else {
	/* Key collision; create new entry and link to hashed location */
	new = (hash_entry_t *) ckd_calloc (1, sizeof(hash_entry_t));
	new->key = key;
	new->len = len;
	new->val = val;
	new->next = cur->next;
	cur->next = new;
    }

    return val;
}


int32 hash_enter (hash_table_t *h, const char *key, int32 val)
{
    uint32 hash;
    int32 len;
    
    hash = key2hash (h, key);
    len = strlen(key);
    return (enter (h, hash, key, len, val));
}


int32 hash_enter_bkey (hash_table_t *h, const char *key, int32 len, int32 val)
{
    uint32 hash;
    char *str;
    
    str = makekey ((uint8 *)key, len, NULL);
    hash = key2hash (h, str);
    ckd_free (str);
    
    return (enter (h, hash, key, len, val));
}


glist_t hash_tolist (hash_table_t *h, int32 *count)
{
    glist_t g;
    hash_entry_t *e;
    int32 i, j;
    
    g = NULL;
    
    j = 0;
    for (i = 0; i < h->size; i++) {
	e = &(h->table[i]);
	
	if (e->key != NULL) {
	    g = glist_add_ptr (g, (void *)e);
	    j++;
	    
	    for (e = e->next; e; e = e->next) {
		g = glist_add_ptr (g, (void *)e);
		j++;
	    }
	}
    }
    
    *count = j;
    
    return g;
}


void hash_free (hash_table_t *h)
{
    hash_entry_t *e, *e2;
    int32 i;
    
    /* Free additional entries created for key collision cases */
    for (i = 0; i < h->size; i++) {
	for (e = h->table[i].next; e; e = e2) {
	    e2 = e->next;
	    ckd_free ((void *) e);
	}
    }
    
    ckd_free ((void *) h->table);
    ckd_free ((void *) h);
}
