//
//  BRSet.c
//
//  Created by Aaron Voisine on 9/11/15.
//  Copyright (c) 2015 breadwallet LLC
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "BRSet.h"
#include <stdlib.h>
#include <string.h>

// linear probed hashtable for good cache performance, maximum load factor is 2/3

static const size_t tableSizes[] = { // starting with 1, multiply by 3/2, round up, then find next largest prime
    1, 3, 7, 13, 23, 37, 59, 97, 149, 227, 347, 523, 787, 1187, 1783, 2677, 4019, 6037, 9059, 13591,
    20389, 30593, 45887, 68863, 103307, 154981, 232487, 348739, 523129, 784697, 1177067, 1765609,
    2648419, 3972643, 5958971, 8938469, 13407707, 20111563, 30167359, 45251077, 67876637, 101814991,
    152722489, 229083739, 343625629, 515438447, 773157683, 1159736527, 1739604799, 2609407319, 3914111041
};

#define TABLE_SIZES_LEN (sizeof(tableSizes)/sizeof(*tableSizes))

struct _BRSet {
    void **table; // hashtable
    size_t size; // number of buckets in table
    size_t itemCount; // number of items in set
    size_t (*hash)(const void *); // hash function
    int (*eq)(const void *, const void *); // equality function
};

static void BRSetInit(BRSet *set, size_t (*hash)(const void *), int (*eq)(const void *, const void *), size_t capacity)
{
    size_t i = 0;
    
    while (i < TABLE_SIZES_LEN && tableSizes[i] < capacity) i++;

    if (i + 1 < TABLE_SIZES_LEN) { // use next larger table size to keep load factor below 2/3 at capacity
        set->table = calloc(tableSizes[i + 1], sizeof(void *));
        set->size = tableSizes[i + 1];
    }
    
    set->itemCount = 0;
    set->hash = hash;
    set->eq = eq;
}

// retruns a newly allocated empty set that must be freed by calling BRSetFree(), hash is a function that returns a hash
// value for a given set item, eq is a function that tests if two set items are equal, capacity is the maximum estimated
// number of items the set will need to hold
BRSet *BRSetNew(size_t (*hash)(const void *), int (*eq)(const void *, const void *), size_t capacity)
{
    BRSet *set = malloc(sizeof(BRSet));
    
    BRSetInit(set, hash, eq, capacity);
    return set;
}

// rebuilds hashtable to hold up to capacity items
static void BRSetGrow(BRSet *set, size_t capacity)
{
    BRSet newSet;
    
    BRSetInit(&newSet, set->hash, set->eq, capacity);
    BRSetUnion(&newSet, set);
    free(set->table);
    set->table = newSet.table;
    set->size = newSet.size;
    set->itemCount = newSet.itemCount;
}

// adds given item to set or replaces an equivalent existing item, returns item replaced if any
void *BRSetAdd(BRSet *set, void *item)
{
    size_t size = set->size;
    size_t i = set->hash(item) % size;
    void *t = set->table[i];

    while (t && t != item && ! set->eq(t, item)) { // probe for empty bucket
        i = (i + 1) % size;
        t = set->table[i];
    }

    if (! t) set->itemCount++;
    set->table[i] = item;
    if (set->itemCount > ((size + 2)/3)*2) BRSetGrow(set, size); // limit load factor to 2/3
    return t;
}

// removes item equivalent to given item from set, returns item removed if any
void *BRSetRemove(BRSet *set, const void *item)
{
    size_t size = set->size;
    size_t i = set->hash(item) % size;
    void *r = set->table[i], *t;

    while (r != item && r && ! set->eq(r, item)) { // probe for item
        i = (i + 1) % size;
        r = set->table[i];
    }
    
    if (r) {
        set->itemCount--;
        set->table[i] = NULL;
        i = (i + 1) % size;
        t = set->table[i];
        
        while (t) { // hashtable cleanup
            set->itemCount--;
            set->table[i] = NULL;
            BRSetAdd(set, t);
            i = (i + 1) % size;
            t = set->table[i];
        }
    }
    
    return r;
}

// removes all items from set
void BRSetClear(BRSet *set)
{
    memset(set->table, 0, set->size);
    set->itemCount = 0;
}

// returns the number of items in set
size_t BRSetCount(BRSet *set)
{
    return set->itemCount;
}

// true if item is contained in set
int BRSetContains(BRSet *set, const void *item)
{
    return (BRSetGet(set, item) != NULL);
}

// true if any items in otherSet are contained in set
int BRSetIntersects(BRSet *set, const BRSet *otherSet)
{
    size_t i = 0, size = otherSet->size;
    void *t;
    
    while (i < size) {
        t = otherSet->table[i++];
        if (t && BRSetGet(set, t) != NULL) return 1;
    }
    
    return 0;
}

// returns member item from set equivalent to given item
void *BRSetGet(BRSet *set, const void *item)
{
    size_t size = set->size;
    size_t i = set->hash(item) % size;
    void *t = set->table[i];

    while (t != item && t && ! set->eq(t, item)) { // probe for item
        i = (i + 1) % size;
        t = set->table[i];
    }
    
    return t;
}

// returns an initial random item from set for use when iterating, or NULL if set is empty
void *BRSetFirst(BRSet *set)
{
    size_t i = 0, size = set->size;
    void *r = NULL;

    while (! r && i < size) r = set->table[i++];
    return r;
}

// returns the next item after given item when iterating, or NULL if no more items are available
void *BRSetNext(BRSet *set, const void *item)
{
    size_t size = set->size;
    size_t i = set->hash(item) % size;
    void *t = set->table[i], *r = NULL;
    
    while (t != item && t && ! set->eq(t, item)) { // probe for item
        i = (i + 1) % size;
        t = set->table[i];
    }
    
    i++;
    while (! r && i < size) r = set->table[i++];
    return r;
}

// writes up to count items from set to allItems, allItems must be large enough to hold itemSize*count bytes
void BRSetAll(BRSet *set, void *allItems, size_t itemSize, size_t count)
{
    size_t i = 0, j = 0, size = set->size;
    void *t;
    
    while (i < size && j < count) {
        t = set->table[i++];
        if (t) memcpy((uint8_t *)allItems + itemSize*j++, t, itemSize);
    }
}

// calls map() with each item in set
void BRSetMap(BRSet *set, void *info, void (*map)(void *info, void *item))
{
    size_t i = 0, size = set->size;
    void *t;
    
    while (i < size) {
        t = set->table[i++];
        if (t) map(info, t);
    }
}

// adds or replaces items from otherSet into set
void BRSetUnion(BRSet *set, const BRSet *otherSet)
{
    size_t i = 0, size = otherSet->size;
    void *t;
    
    if (otherSet->itemCount > ((set->size + 2)/3)*2) BRSetGrow(set, otherSet->itemCount);
    
    while (i < size) {
        t = otherSet->table[i++];
        if (t) BRSetAdd(set, t);
    }
}

// removes items contained in otherSet from set
void BRSetMinus(BRSet *set, const BRSet *otherSet)
{
    size_t i = 0, size = otherSet->size;
    void *t;
    
    while (i < size) {
        t = otherSet->table[i++];
        if (t) BRSetRemove(set, t);
    }
}

// removes items not contained in otherSet from set
void BRSetIntersect(BRSet *set, const BRSet *otherSet)
{
    size_t i = 0, size = set->size;
    void *t;
    BRSet oset = *otherSet;
    
    oset.hash = set->hash;
    oset.eq = set->eq;
    
    while (i < size) {
        t = set->table[i];

        if (t && ! BRSetContains(&oset, t)) {
            BRSetRemove(set, t);
        }
        else i++;
    }
}

// frees memory allocated for set
void BRSetFree(BRSet *set)
{
    free(set->table);
    free(set);
}
