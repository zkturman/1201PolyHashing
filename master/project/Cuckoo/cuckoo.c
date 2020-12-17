#include "specific.h"
#include "../assoc.h"

#define INITSIZE 16
#define STRINGTYPE 0
#define DJB2HASHINIT 5381
#define DJB2HASHFACT 33
#define ZKTHASHINIT  1000
#define ZKTHASHFACT 33
#define REHASHMARK 0.6
#define RESIZEFACT 2.0
#define LOGBASE 2
#define EVENMOD 2

/*Creates table structures for an array of key/data pairs. Contains the count
of values and total size.*/
table *_createTable();

/*Creates a structure to contain a key/data pair*/
entry *_createEntry(void *key, void *data);

/*Responsible for adding a new entry to assoc a. Returns false if it could not
find a place for the entry within log base 2 of the table sizes.*/
bool _doCuckoo(assoc **a, entry **e);

/*Determines whether the base or cuckoo table should be updated*/
table *_specifyTable(assoc *a, void *key, unsigned long *hash, bool cuckoo);

/*Generates a DJB2 hash based on code found:
 http://www.cse.yorku.ca/~oz/hash.html. This hash is used for the base table.*/
unsigned long _djb2Hash(assoc *a, void *key);

/*Generates an SDBM hash from http://www.cse.yorku.ca/~oz/hash.html. This is
the hash used for the cuckoo table.*/
unsigned long _sdbmHash(assoc *a, void *key);

/*Returns true if an entry could be added to a hash table. Updates the count of
 values in the table.*/
bool _addEntry(table *t, int index, entry *e);

/*Returns true if an entry could be updated. Frees e after updating.*/
bool _updateEntry(table *t, int index, entry *e);

/*Returns true if the memory of a give key matches another. If assoc is set
to use strings, this will be based on strlen. If not, it will be based on the
the assoc's specified key size.*/
bool _keysMatch(assoc *a, void *x, void *y);

/*Swaps two entries*/
void _swapEntry(entry **a, entry **b);

/*Returns true if a number is odd*/
bool _isOdd(const int n);

/*Returns true if the number of values in a table exceed the rehash capacity.*/
bool _shouldRehash(table *t);

/*Returns true if the fuction could resizes and rehashes the data within an
assoc a. This creates a new assoc and copies over the data.*/
bool _rehash(assoc **a);

/*Returns true both the base and cuckoo table could be rehashed successfully*/
bool _rehashBothTables(assoc *oldA, assoc **newA);

/*Updates the size of a new table based on fact and the size of an old table*/
void _resizeTable(table *oldT, table *newT, int fact);

/*Returns true if a table could be rehashed successfully. Only one table, the
base OR the cuckoo is rehashed.*/
bool _rehashSingleTable(assoc **newA, table *oldT);

/*Returns true if a value could be inserted into an assoc a. This function is
used only for rehashing. If we couldn't add the key, we try resizing and
rehashing again.*/
bool _rehashInsert(assoc** a, void* key, void* data);

/*Returns the log base two of n approximately. May not work perfectly for odd
numbers.*/
int _log2(int n);

void _test();

/*
   Initialise the Associative array
   keysize : number of bytes (or 0 => string)
   This is important when comparing keys since
   we'll need to use either memcmp() or strcmp()
*/
assoc* assoc_init(int keysize){
   assoc *a;
   if (keysize < 0){
      on_error("Negative keysize? Exiting.");
   }
   a = (assoc *)ncalloc(1, sizeof(assoc));
   a->base = _createTable();
   a->cuckoo = _createTable();
   a->keySize = keysize;
   if(keysize == STRINGTYPE){
      a->useStrings = true;
   }
   return a;
}

/*
   Insert key/data pair
   - may cause resize, therefore 'a' might
   be changed due to a realloc() etc.
*/
void assoc_insert(assoc** a, void* key, void* data){
   entry *e;
   bool found;
   e = _createEntry(key, data);
   found = _doCuckoo(a, &e);
   if (found == false || _shouldRehash((*a)->base)
      || _shouldRehash((*a)->cuckoo)){
      _rehash(a);
      if (found == false){
         assoc_insert(a, e->key, e->data);
         free(e);
      }
   }
}

/*
   Returns the number of key/data pairs
   currently stored in the table
*/
unsigned int assoc_count(assoc* a){
   return a->base->count + a->cuckoo->count;
}

/*
   Returns a pointer to the data, given a key
   NULL => not found
*/
void* assoc_lookup(assoc* a, void* key){
   unsigned long hash, cHash;
   hash = _djb2Hash(a, key);
   cHash = _sdbmHash(a, key);
   if ((a->base->ary[hash] != NULL)
      && (_keysMatch(a, a->base->ary[hash]->key, key) == true)){
      return a->base->ary[hash]->data;
   }
   if ((a->cuckoo->ary[cHash] != NULL)
      && (_keysMatch(a, a->cuckoo->ary[cHash]->key, key) == true)){
      return a->cuckoo->ary[cHash]->data;
   }
   return NULL;
}

void assoc_todot(assoc* a);

/* Free up all allocated space from 'a' */
void assoc_free(assoc* a){
   int i = 0;
   for (i = 0; i < a->base->size; i++){
      if (a->base->ary[i] != NULL){
         free(a->base->ary[i]);
      }
   }
   for (i = 0; i < a->cuckoo->size; i++){
      if (a->cuckoo->ary[i] != NULL){
         free(a->cuckoo->ary[i]);
      }
   }
   free(a->base->ary);
   free(a->base);
   free(a->cuckoo->ary);
   free(a->cuckoo);
   free(a);
}

table *_createTable(){
   table *t;
   t = ncalloc(1, sizeof(table));
   t->size = INITSIZE;
   t->ary = ncalloc(t->size, sizeof(entry *));
   return t;
}

entry *_createEntry(void *key, void *data){
   entry *e;
   e = ncalloc(1, sizeof(entry));
   e->key = key;
   e->data = data;
   return e;
}

bool _doCuckoo(assoc **a, entry **e){
   table *t;
   unsigned long hash;
   int rounds = 0;
   bool shouldCuckoo, found = false;
   do{
      shouldCuckoo = _isOdd(rounds);
      t = _specifyTable(*a, (*e)->key, &hash, shouldCuckoo);
      if(t->ary[hash] == NULL){
         found = _addEntry(t, hash, *e);
      }
      else{
         if (_keysMatch(*a, t->ary[hash]->key, (*e)->key) == true){
            found = _updateEntry(t, hash, *e);
         }
         else{
            _swapEntry(&(t->ary[hash]), e);
            rounds++;
         }
      }
   } while ((found == false) && (rounds < _log2((*a)->base->size)));
   return found;
}

table *_specifyTable(assoc *a, void *key, unsigned long *hash, bool cuckoo){
   if (!cuckoo){
      *hash = _djb2Hash(a, key);
      return a->base;
   }
   else{
      *hash = _sdbmHash(a, key);
      return a->cuckoo;
   }
}

unsigned long _djb2Hash(assoc *a, void *key){
   unsigned long hash = DJB2HASHINIT;
   unsigned char c;
   int length, count = 0;
   if (a->useStrings == true){
      length = strlen((char *)key);
   }
   else{
      length = a->keySize;
   }
   while (count < length){
      c = *((unsigned char *)key + count);
      hash = (hash * DJB2HASHFACT) + c;
      count++;
   }
   return hash % a->base->size;
}

unsigned long _sdbmHash(assoc *a, void *key){
   unsigned long hash = ZKTHASHINIT;
   unsigned char c;
   int length, count = 0;
   if (a->useStrings == true){
      length = strlen((char *)key);
   }
   else{
      length = a->keySize;
   }
   while (count < length){
      c = *((unsigned char *)key + count);
      hash = c + (hash << 6) + (hash << 16) - hash;
      count++;
   }
   return hash % a->cuckoo->size;
}

bool _addEntry(table *t, int index, entry *e){
   if (t == NULL || e == NULL){
      return false;
   }
   if (index < 0 || index >= t->size){
      return false;
   }
   t->ary[index] = e;
   t->count += 1;
   return true;
}

bool _updateEntry(table *t, int index, entry *e){
   if (t == NULL || e == NULL){
      return false;
   }
   if (index < 0 || index >= t->size){
      return false;
   }
   t->ary[index]->data = e->data;
   free(e);
   return true;
}

bool _keysMatch(assoc *a, void *x, void *y){
   if (a == NULL || x == NULL || y == NULL){
      return false;
   }
   if (a->useStrings == true){
      if (strcmp((char *)x, (char *)y) == 0){
         return true;
      }
   }
   else{
      if (memcmp(x, y, a->keySize) == 0){
         return true;
      }
   }
   return false;
}

void _swapEntry(entry **a, entry **b){
   entry *tmp;
   tmp = *a;
   *a = *b;
   *b = tmp;
}

bool _isOdd(const int n){
   return n % EVENMOD;
}

bool _shouldRehash(table *t){
   int capacity;
   if (t == NULL){
      return false;
   }
   capacity = (int)(t->size * REHASHMARK);
   if (t->count > capacity){
      return true;
   }
   return false;
}

bool _rehash(assoc **a){
   assoc *newA;
   if (a == NULL || *a == NULL){
      return false;
   }
   newA = assoc_init((*a)->keySize);
   newA->useStrings = (*a)->useStrings;
   _rehashBothTables(*a, &newA);
   assoc_free(*a);
   *a = newA;
   return true;
}

bool _rehashBothTables(assoc *oldA, assoc **newA){
   int fact = 1;
   bool stuck = false;
   if (newA == NULL || oldA == NULL){
      return false;
   }
   do{
      fact = (int)(fact * RESIZEFACT);
      _resizeTable(oldA->base, (*newA)->base, fact);
      _resizeTable(oldA->cuckoo, (*newA)->cuckoo, fact);
      if (_rehashSingleTable(newA, oldA->base) == true){
         stuck = true;
      }
      else if (_rehashSingleTable(newA, oldA->cuckoo) == true){
         stuck = true;
      }
      else{
         stuck = false;
      }
   } while (stuck == true);
   return true;
}

void _resizeTable(table *oldT, table *newT, int fact){
   int i;
   /*if we need to resize multiple times, we need to clear the array*/
   for (i = 0; i < newT->size; i++){
      if (newT->ary[i] != NULL){
         free(newT->ary[i]);
      }
   }
   newT->count = 0;
   newT->size = oldT->size * fact;
   free(newT->ary);
   newT->ary = ncalloc(newT->size, sizeof(entry *));
}

bool _rehashSingleTable(assoc **newA, table *oldT){
   int i;
   bool stuck = false;
   for (i = 0; i < oldT->size; i++){
      if (oldT->ary[i] != NULL){
         stuck = !(_rehashInsert(newA, oldT->ary[i]->key,
            oldT->ary[i]->data));
      }
   }
   return stuck;
}

bool _rehashInsert(assoc** a, void* key, void* data){
   entry *e;
   bool found;
   e = _createEntry(key, data);
   found = _doCuckoo(a, &e);
   if (found == false){
      free(e);
   }
   return found;
}

int _log2(int n){
   int count = 0, val;
   val = n;
   while (val > 0){
      val /= LOGBASE;
      count ++;
   }
   return count;
}

void _test(){
   assoc *test1, *test2, *test3, *test4;
   entry *ent1, *ent2, *ent3;
   table *t1;
   int i, count, v1, v4, v5, v10, data;
   long v2, v6;
   unsigned long placeholder, hash1, hash2;
   char v3[20], v7[20], v8[20];/*, v9[20];*/
   /*assoc_init(-1);*/
   test1 = assoc_init(sizeof(int));
   test2 = assoc_init(sizeof(long));
   test3 = assoc_init(0);
   assert(test1 != NULL);
   assert(test1->base->size == INITSIZE);
   assert(test1->cuckoo->size == INITSIZE);
   assert(test1->base->count == 0);
   assert(test1->cuckoo->count == 0);
   assert(test1->keySize == sizeof(int));
   assert(test1->useStrings == false);
   assert(test2 != NULL);
   assert(test2->keySize == sizeof(long));
   assert(test2->useStrings == false);
   assert(test3 != NULL);
   assert(test3->keySize == 0);
   assert(test3->useStrings == true);

   /*making entries*/
   data = 100;
   v1 = 23;
   ent1 = _createEntry(&v1, NULL);
   assert(*(int *)ent1->key == 23);
   assert(ent1->data == NULL);
   v2 = 123;
   ent2 = _createEntry(&v2, &data);
   assert(*(long *)ent2->key == 123);
   assert(*(int *)ent2->data = 100);
   free(ent1);
   free(ent2);

   /*hashing with _djb2Hash - gross, but needed for sanity*/
   v1 = 1024;
   placeholder = ((((((long)DJB2HASHINIT * DJB2HASHFACT + 0) * DJB2HASHFACT + 4)
      * DJB2HASHFACT + 0) * DJB2HASHFACT + 0));
   assert(_djb2Hash(test1, &v1) == placeholder % test1->base->size);
   v1 = 223;
   placeholder = ((((((long)DJB2HASHINIT * DJB2HASHFACT + 223)
      * DJB2HASHFACT + 0) * DJB2HASHFACT + 0) * DJB2HASHFACT + 0));
   assert(_djb2Hash(test1, &v1) == placeholder % test1->base->size);
   v2 = 1024;
   placeholder = (((((((((long)DJB2HASHINIT * DJB2HASHFACT + 0)
      * DJB2HASHFACT + 4) * DJB2HASHFACT + 0) * DJB2HASHFACT + 0)
      * DJB2HASHFACT + 0) * DJB2HASHFACT + 0) * DJB2HASHFACT + 0)
      * DJB2HASHFACT + 0);
   assert(_djb2Hash(test2, &v2) == placeholder % test2->base->size);
   strcpy(v3, "cab");
   placeholder = ((((long)DJB2HASHINIT * DJB2HASHFACT + 'c')
      * DJB2HASHFACT + 'a') * DJB2HASHFACT + 'b');
   assert(_djb2Hash(test3, (void *)v3) == (placeholder % test3->base->size));

   /*make sure things with different addresses hash the same if same value*/
   count = 0;
   for (v1 = 0; v1 < 100; v1++){
      v4 = v1;
      hash1 = _djb2Hash(test1, &v1);
      hash2 = _djb2Hash(test1, &v4);
      assert(hash1 == hash2);
      assert(hash1 < (unsigned long)test1->base->size);
      if (hash1 == 0){
         count++;
      }
   }
   assert(count > 0);

   /*inserting helper functions*/
   assert(_isOdd(1) == true);
   assert(_isOdd(2) == false);
   assert(_isOdd(0) == false);
   assert(_isOdd(23) == true);
   ent1 = _createEntry(&v1, NULL);
   t1 = _specifyTable(test1, ent1->key, &hash1, false);
   assert(memcmp(t1, test1->base, sizeof(table)) == 0);
   assert(memcmp(t1, test1->cuckoo, sizeof(table)) != 0);
   t1 = _specifyTable(test1, ent1->key, &hash1, true);
   assert(memcmp(t1, test1->base, sizeof(table)) != 0);
   assert(memcmp(t1, test1->cuckoo, sizeof(table)) == 0);
   ent2 = _createEntry(&v2, NULL);
   ent3 = ent1;
   _swapEntry(&ent1, &ent2);
   assert(memcmp(ent3, ent2, sizeof(entry)) == 0);
   assert(!_addEntry(NULL, 0, ent1));
   assert(!_addEntry(t1, 0, NULL));
   assert(!_addEntry(t1, -1, ent1));
   assert(!_addEntry(t1, INITSIZE, ent1));
   assert(_addEntry(t1, 0, ent1));
   assert(memcmp(t1->ary[0], ent1, sizeof(entry)) == 0);
   assert(!_updateEntry(NULL, 0, ent1));
   assert(!_updateEntry(t1, 0, NULL));
   assert(!_updateEntry(t1, -1, ent1));
   assert(!_updateEntry(t1, INITSIZE + 1, ent1));
   ent3 = _createEntry(&v1, &v1);
   assert(_updateEntry(t1, 0, ent3)); /*frees ent3*/
   assert(memcmp((int *)t1->ary[0]->data, &v1, sizeof(int)) == 0);
   ent1 = _createEntry(&v1, NULL);
   assert(_doCuckoo(&test1, &ent1));
   free(ent2);
   assoc_free(test1);


   /*basic inserting and lookup*/
   test1 = assoc_init(sizeof(int));
   v1 = 1;
   assoc_insert(&test1, &v1, NULL);
   assert(*(int *)(test1->base->ary[_djb2Hash(test1, &v1)]->key) == v1);
   assert(test1->base->count == 1);
   assert(assoc_lookup(test1, &v1) == NULL);
   v4 = 3;
   v5 = 4;
   assoc_insert(&test1, &v4, &v5);
   assoc_insert(&test1, &v5, NULL);
   assert(test1->base->count == 3);
   assert(assoc_lookup(test1, &v4) == &v5);
   v2 = 100;
   v6 = 10010101000000;
   assoc_insert(&test2, &v2, NULL);
   assoc_insert(&test2, &v6, &v2);
   assert(test2->base->count == 2);
   assert(assoc_lookup(test2, &v6) == &v2);
   strcpy(v7, "avocado");
   assoc_insert(&test3, (void *)v3, NULL);
   assoc_insert(&test3, (void *)v7, (void *)v3);
   assert(strcmp((char *)assoc_lookup(test3, (void *)v7), "cab") == 0);
   strcpy(v8, "avocado");
   assoc_insert(&test3, (void *)v8, NULL);

   v10 = 3;
   assert(_keysMatch(test1, &v1, &v4) == false);
   assert(_keysMatch(test1, &v4, &v10) == true);
   assert(_keysMatch(test3, (void *)v7, NULL) == false);
   assert(_keysMatch(test3, (void *)v7, (void *)v8) == true);

   /*test inserting + overwriting*/
   assert(test3->base->count == 2);
   assoc_insert(&test3, (void *)v8, &v1);
   assert(test3->base->count == 2);
   assert(*(int *)(assoc_lookup(test3, (void *)v8)) == v1);

   /*test probing and collisions*/
   /*strcpy(v9, "avo");
   assert(_djb2Hash(test3, v9) == _djb2Hash(test3, v3));
   assoc_insert(&test3, (void *)v9, NULL);
   assert(test3->base->count == 2);
   assert(test3->cuckoo->count == 1);*/

   /*test rehashing helper functions*/
   assert(_shouldRehash(test1->base) == false);
   assert(_shouldRehash(test2->base) == false);
   assert(_shouldRehash(test3->base) == false);
   assert(_shouldRehash(test1->cuckoo) == false);
   assert(_shouldRehash(test2->cuckoo) == false);
   assert(_shouldRehash(test3->cuckoo) == false);
   t1 = NULL;
   assert(_shouldRehash(t1) == false);
   test1->base->count = test1->base->size * REHASHMARK + 1;
   assert(_shouldRehash(test1->base) == true);
   test1->base->count = 3;
   test4 = NULL;
   test4 = assoc_init(sizeof(int));
   assert(_rehashInsert(&test4, &v1, NULL));
   assoc_free(test4);
   test4 = assoc_init(sizeof(int));
   _resizeTable(test1->base, test4->base, 2);
   assert(test4->base->size == test1->base->size * 2);
   assert(!_rehashSingleTable(&test4, test1->base)); /*shouldn't be stuck*/
   assert(!_rehashSingleTable(&test4, test1->cuckoo));
   assoc_free(test4);
   test4 = NULL;
   assert(_rehashBothTables(test1, &test4) == false);
   test4 = assoc_init(sizeof(int));
   assert(_rehashBothTables(test1, &test4) == true);
   assert(test4->base->size == (int) (test1->base->size * RESIZEFACT));
   count = 0;
   for (i = 0; i < test4->base->size; i++){
      if (test4->base->ary[i] != NULL){
         count++;
      }
   }
   assert(count == test1->base->count);
   assoc_free(test4);

   /*test again with strings*/
   test4 = assoc_init(0);
   assert(_rehashBothTables(NULL, &test4) == false);
   assert(_rehashBothTables(test3, NULL) == false);
   assert(_rehashBothTables(NULL, NULL) == false);
   assert(_rehashBothTables(test3, &test4) == true);
   assert(test4->base->count == test3->base->count);
   assert(assoc_count(test4) == assoc_count(test3));
   assert(test4->base->size == (int) (test3->base->size * RESIZEFACT));
   count = 0;
   for (i = 0; i < test4->base->size; i++){
      if (test4->base->ary[i] != NULL){
         count++;
      }
   }
   assert(count == test4->base->count);
   assoc_free(test4);

   /*rehash testing*/
   test4 = NULL;
   assert(_rehash(&test4) == false);
   assert(_rehash(&test1) == true);
   assert(_rehash(&test2) == true);
   assert(_rehash(&test3) == true);
   assert(test1->base->size == (int) (INITSIZE * RESIZEFACT));
   assert(test1->base->count == 3);
   assert(test1->useStrings == false);
   count = 0;
   for (i = 0; i < test1->base->size; i++){
      if (test1->base->ary[i] != NULL){
         count++;
      }
   }
   assert(count == test1->base->count);

   /*larger scale testing*/
   /*assoc_free(test1);
   test1 = assoc_init(sizeof(int));
   for (i = 0; i < 5000; i++){
      assoc_insert(&test1, &i, &i);
      assert((assoc_lookup(test1, &i)) != NULL);
   }
   assert(test1->tableSize > 100);
   printf("test1->count: %d\n", test1->count);
   printf("test1->tableSize: %d\n", test1->tableSize);
   assert(test1->count == 1000);
   i = i - 1;
   assert(assoc_lookup(test1, &i) != NULL);*/
   assoc_free(test1);
   assoc_free(test2);
   assoc_free(test3);
}
