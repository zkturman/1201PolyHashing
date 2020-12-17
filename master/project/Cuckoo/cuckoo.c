#include "specific.h"
#include "../assoc.h"

#define INITSIZE 17
#define STRINGTYPE 0
#define DJB2HASHINIT 5381
#define DJB2HASHFACT 33
#define ZKTHASHINIT  3371
#define ZKTHASHFACT 29
#define REHASHMARK 0.6
#define RESIZEFACT 1.5

table *_createTable();
table *_specifyTable(assoc *a, void *key, unsigned long *hash, bool cuckoo);
entry *_createEntry(void *key, void *data);
unsigned long _zktHash(assoc *a, void *key);
unsigned long _djb2Hash(assoc *a, void *key);
unsigned long _findNextProbe(assoc *a, unsigned long hash, unsigned long probe);
bool _rehash(assoc **a);
bool _rehashBothTables(assoc *old, assoc *new);
bool _rehashSingleTable(assoc *newA, table *oldT);
bool _shouldRehash(table *t);
int _nextPrime(const int n);
bool _isPrime(const int n);
bool _isOdd(const int n);
bool _keysMatch(assoc *a, void *x, void *y);
bool _doCuckoo(assoc **a, entry *e);
bool _addEntry(table *t, int index, entry *e);
bool _updateEntry(table *t, int index, entry *e);
void _swapEntry(entry **a, entry **b);
bool _rehashInsert(assoc** a, void* key, void* data);
void _resizeTable(table *old, table **new, int fact);
int _log2(int n);

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
   found = _doCuckoo(a, e);
   _rehash(a);
   if (found == false){
      assoc_insert(a, e->key, e->data);
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
   cHash = _zktHash(a, key);
   if (_keysMatch(a, a->base->ary[hash]->key, key) == true){
      return a->base->ary[hash]->data;
   }
   if (_keysMatch(a, a->cuckoo->ary[cHash]->key, key) == true){
      return a->base->ary[cHash]->data;
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

entry *_createEntry(void *key, void *data){
   entry *e;
   e = ncalloc(1, sizeof(entry));
   e->key = key;
   e->data = data;
   return e;
}

/*modified djb2 hash function from http://www.cse.yorku.ca/~oz/hash.html*/
unsigned long _zktHash(assoc *a, void *key){
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
      hash = (hash * ZKTHASHFACT) + c;
      count++;
   }
   return hash % a->cuckoo->size;
}

/*adapted djb2 hash function from http://www.cse.yorku.ca/~oz/hash.html*/
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

table *_createTable(){
   table *t;
   t = ncalloc(1, sizeof(table));
   t->size = INITSIZE;
   t->ary = ncalloc(t->size, sizeof(entry *));
   return t;
}

bool _rehash(assoc **a){
   assoc *newA;
   if (a == NULL || *a == NULL){
      return false;
   }
   newA = assoc_init((*a)->keySize);
   newA->useStrings = (*a)->useStrings;
   _rehashBothTables(*a, newA);
   assoc_free(*a);
   *a = newA;
   return true;
}

bool _rehashBothTables(assoc *old, assoc *new){
   int fact = 1;
   bool stuck = false;
   if (new == NULL || old == NULL){
      return false;
   }
   do{
      printf("stuck here? %d\n", stuck);
      fact = (int)(fact * RESIZEFACT);
      _resizeTable(old->base, &(new->base), fact);
      _resizeTable(old->cuckoo, &(new->cuckoo), fact);
      if (_rehashSingleTable(new, old->base) == true){
         stuck = true;
      }
      else if (_rehashSingleTable(new, old->cuckoo) == true){
         stuck = true;
      }
      else{
         stuck = false;
      }
   } while (stuck == true);
   return true;
}

bool _rehashSingleTable(assoc *newA, table *oldT){
   int i;
   bool stuck;
   for (i = 0; i < oldT->size; i++){
      if (oldT->ary[i] != NULL){
         stuck = !_rehashInsert(&newA, oldT->ary[i]->key,
            oldT->ary[i]->data);
      }
   }
   return stuck;
}

void _resizeTable(table *old, table **new, int fact){
   (*new)->size = old->size * fact;
   free((*new)->ary);
   (*new)->ary = ncalloc((*new)->size, sizeof(entry *));
}

bool _rehashInsert(assoc** a, void* key, void* data){
   entry *e;
   bool found;
   e = _createEntry(key, data);
   found = _doCuckoo(a, e);
   return found;
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

bool _doCuckoo(assoc **a, entry *e){
   table *t;
   unsigned long hash;
   int rounds = 0;
   bool shouldCuckoo, found = false;
   do{
      shouldCuckoo = _isOdd(rounds);
      t = _specifyTable(*a, e->key, &hash, shouldCuckoo);
      if(t->ary[hash] == NULL){
         found = _addEntry(t, hash, e);
      }
      else{
         if (_keysMatch(*a, t->ary[hash]->key, e->key) == true){
            printf("we hit this\n");
            found = _updateEntry(t, hash, e);
         }
         else{
            _swapEntry(&(t->ary[hash]), &e);
            rounds++;
         }
      }
      printf("stuck here? %d - %d\n",found, rounds);
   } while ((found == false) && (rounds < _log2((*a)->base->size)));
   printf("stuck here?\n");
   return found;
}

int _log2(int n){
   int count = 0, val;
   val = n;
   while (val > 0){
      val /= 2;
      count ++;
   }
   return count;
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

void _swapEntry(entry **a, entry **b){
   entry *tmp;
   tmp = *a;
   *a = *b;
   *b = tmp;
}

table *_specifyTable(assoc *a, void *key, unsigned long *hash, bool cuckoo){
   if (!cuckoo){
      *hash = _djb2Hash(a, key);
      return a->base;
   }
   else{
      *hash = _zktHash(a, key);
      return a->cuckoo;
   }
}

int _nextPrime(const int n){
   int i, next = 0;
   i = n;
   while (next == 0){
      i++;
      if (_isOdd(i) || i == 2){
         if (_isPrime(i) == true){
            next = i;
         }
      }
   }
   return next;
}

bool _isPrime(const int n){
   int i;
   if (n < 2){
      return false;
   }
   if (n == 2){
      return true;
   }
   for (i = 2; i < n / 2; i++){
      if (n % i == 0){
         return false;
      }
   }
   return true;
}

bool _isOdd(const int n){
   return n % 2;
}

void _test(){
   assoc *test1, *test2, *test3, *test4;
   entry *ent1, *ent2, *ent3;
   table *t1;
   int i, count, v1, v4, v5, v10, data;
   long v2, v6;
   unsigned long placeholder, hash1, hash2;
   char v3[20], v7[20], v8[20], v9[20];
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

   /*probe hashing with _zktHash*/
   v1 = 1024;
   placeholder = ((((((long)ZKTHASHINIT * ZKTHASHFACT + 0) * ZKTHASHFACT + 4)
      * ZKTHASHFACT + 0) * ZKTHASHFACT + 0));
   assert(_zktHash(test1, &v1) == placeholder % test1->base->size);
   v1 = 223;
   placeholder = ((((((long)ZKTHASHINIT * ZKTHASHFACT + 223)
      * ZKTHASHFACT + 0) * ZKTHASHFACT + 0) * ZKTHASHFACT + 0));
   assert(_zktHash(test1, &v1) == placeholder % test1->base->size);
   v2 = 1024;
   placeholder = (((((((((long)ZKTHASHINIT * ZKTHASHFACT + 0)
      * ZKTHASHFACT + 4) * ZKTHASHFACT + 0) * ZKTHASHFACT + 0)
      * ZKTHASHFACT + 0) * ZKTHASHFACT + 0) * ZKTHASHFACT + 0)
      * ZKTHASHFACT + 0);
   assert(_zktHash(test2, &v2) == placeholder % test2->base->size);
   strcpy(v3, "cab");
   placeholder = ((((long)ZKTHASHINIT * ZKTHASHFACT + 'c')
      * ZKTHASHFACT + 'a') * ZKTHASHFACT + 'b');
   assert(_zktHash(test3, (void *)v3) == (placeholder % test3->base->size));

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
   count = 0;
   for (v1 = 0; v1 < 100; v1++){
      v4 = v1;
      hash1 = _zktHash(test1, &v1);
      hash2 = _zktHash(test1, &v4);
      assert(hash1 == hash2);
      assert(hash1 < (unsigned long)test1->cuckoo->size);
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
   assert(_doCuckoo(&test1, ent1));
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
   strcpy(v9, "avo"); /*collision with "cab"*/
   assert(_djb2Hash(test3, v9) == _djb2Hash(test3, v3));
   assoc_insert(&test3, (void *)v9, NULL);
   assert(test3->base->count == 2);
   assert(test3->cuckoo->count == 1);

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
   _resizeTable(test1->base, &test4->base, 2);
   assert(test4->base->size == test1->base->size * 2);
   assert(!_rehashSingleTable(test4, test1->base)); /*shouldn't be stuck*/
   assert(!_rehashSingleTable(test4, test1->cuckoo));
   assoc_free(test4);
   test4 = NULL;
   assert(_rehashBothTables(test1, test4) == false);
   test4 = assoc_init(sizeof(int));
   assert(_rehashBothTables(test1, test4) == true);
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
   assert(_rehashBothTables(NULL, test4) == false);
   assert(_rehashBothTables(test3, NULL) == false);
   assert(_rehashBothTables(NULL, NULL) == false);
   assert(_rehashBothTables(test3, test4) == true);
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
   assert(_isPrime(0) == false);
   assert(_isPrime(1) == false);
   assert(_isPrime(2) == true);
   assert(_isPrime(3) == true);
   assert(_isPrime(9) == false);
   assert(_isPrime(23) == true);
   assert(_nextPrime(1) == 2);
   assert(_nextPrime(2) == 3);
   assert(_nextPrime(3) == 5);
   assert(_nextPrime(4) == 5);
   assert(_nextPrime(9) == 11);
   assoc_free(test1);
   assoc_free(test2);
   assoc_free(test3);
}
