#include "specific.h"
#include "../assoc.h"


#define DJB2HASHINIT 5381
#define DJB2HASHFACT 33
#define ZKTHASHINIT  331
#define REHASHMARK 0.6
#define RESIZEFACT 2

entry *_createEntry(void *key, void *data);
unsigned long _zktHash(assoc *a, void *key);
unsigned long _djb2Hash(assoc *a, void *key);
unsigned long _findNextProbe(assoc *a, unsigned long hash, unsigned long probe);
bool _rehash(assoc **a);
bool _rehashTable(assoc *old, assoc *new);
bool _shouldRehash(assoc *a);
int _nextPrime(const int n);
bool _isPrime(const int n);
bool _isOdd(const int n);
bool _keysMatch(assoc *a, void *x, void *y);

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
   a->tableSize = INITSIZE;
   a->table = ncalloc(a->tableSize, sizeof(entry *));
   a->cuckooSize = INITSIZE;
   a->cuckoo = ncalloc(a->cuckooSize, sizeof(entry *));
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
   entry *e, *tmp;
   int hash, cHash, count;
   e = _createEntry(key, data);
   tmp = e;
   do{
      hash = _djb2Hash(*a, e->key);
      if((*a)->table[hash] == NULL){
         (*a)->table[hash] = e;
         (*a)->count += 1;
         return;
      }
      else{
         if (_keysMatch(*a, (*a)->table[hash]->key, e->key) == true){
            (*a)->table[hash]->data = e->data;
            free(e);
            return;
         }
         tmp = (*a)->table[hash];
         (*a)->table[hash] = e;
         e = tmp;
         cHash = _zktHash(*a, e->key);
         if ((*a)->cTable[cHash] == NULL){
            (*a)->cTable[hash] = e;
            (*a)->cCount += 1;
            return;
         }
         else{
            if(_keysMatch(*a, (*a)->cTable[cHash]->key, e->key) == true){
               (*a)->cTable[cHash]->data = e->data;
               free(e);
               return;
            }
            tmp = (*a)->cTable[cHash];
            (*a)->cTable[cHash] = e;
            e = tmp;
            count++;
         }
      }
   } while (count < 4); /*count < some number*/
   if (_shouldRehash(*a) == true){
      _rehash(a);
   }
}

unsigned long _findNextProbe(assoc *a, unsigned long hash, unsigned long probe){
   unsigned long nextHash;
   nextHash = hash;
   if (probe > hash){
      nextHash = a->tableSize - (probe - nextHash);
   }
   else{
      nextHash = hash - probe;
   }
   return nextHash;
}

/*
   Returns the number of key/data pairs
   currently stored in the table
*/
unsigned int assoc_count(assoc* a){
   return a->count + a->cCount;
}

/*
   Returns a pointer to the data, given a key
   NULL => not found
*/
void* assoc_lookup(assoc* a, void* key){
   unsigned long hash, nextHash, probe;
   hash = _djb2Hash(a, key);
   nextHash = hash;
   probe = _zktHash(a, key);
   do{
      if (a->table[nextHash] == NULL){
         return NULL;
      }
      if (_keysMatch(a, a->table[nextHash]->key, key) == true){
            return a->table[nextHash]->data;
      }
      nextHash = _findNextProbe(a, nextHash, probe);
   } while (nextHash != hash);
   return NULL;
}

void assoc_todot(assoc* a);

/* Free up all allocated space from 'a' */
void assoc_free(assoc* a){
   int i = 0;
   for (i = 0; i < a->tableSize; i++){
      if (a->table[i] != NULL){
         free(a->table[i]);
      }
   }
   for (i = 0; i < a->cuckooSize; i++){
      if (a->cuckoo[i] != NULL){
         free(a->cuckoo[i]);
      }
   }
   free(a->table);
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

/*custom hashing equation, though partially inspired by djb2*/
unsigned long _zktHash(assoc *a, void *key){
   int length, position, count = 0;
   unsigned long hash = ZKTHASHINIT;
   unsigned char c;
   if (a->useStrings == true){
      length = strlen((char *)key);
   }
   else{
      length = a->keySize;
   }
   while(count < length){
      c = *((unsigned char *)key + count);
      position = count + 1;
      hash = (c + (position)) * position * hash + ZKTHASHINIT;
      count++;
   }
   return hash % (a->tableSize - 1) + 1;
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
   return hash % a->tableSize;
}

bool _rehash(assoc **a){
   assoc *newA;
   if (a == NULL || *a == NULL){
      return false;
   }
   newA = assoc_init((*a)->keySize);
   newA->useStrings = (*a)->useStrings;
   if (_rehashTable(*a, newA) == false){
      return false;
   }
   assoc_free(*a);
   *a = newA;
   return true;
}

bool _rehashTable(assoc *old, assoc *new){
   int i;
   if (old == NULL || new == NULL){
      return false;
   }
   new->tableSize = _nextPrime(old->tableSize * RESIZEFACT);
   free(new->table);
   new->table = ncalloc(new->tableSize, sizeof(entry *));
   for (i = 0; i < old->tableSize; i++){
      if (old->table[i] != NULL){
         assoc_insert(&new, old->table[i]->key, old->table[i]->data);
      }
   }
   return true;
}

bool _shouldRehash(assoc *a){
   int capacity;
   if (a == NULL){
      return false;
   }
   capacity = (int)(a->tableSize * REHASHMARK);
   if (a->count > capacity){
      return true;
   }
   return false;
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
   entry *ent1, *ent2;
   int i, count, v1, v4, v5, v10, data;
   long v2, v6;
   unsigned long placeholder, hash1, hash2, probe;
   char v3[20], v7[20], v8[20], v9[20];
   /*assoc_init(-1);*/
   test1 = assoc_init(sizeof(int));
   test2 = assoc_init(sizeof(long));
   test3 = assoc_init(0);
   assert(test1 != NULL);
   assert(test1->tableSize == INITSIZE);
   assert(test1->count == 0);
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
   assert(_djb2Hash(test1, &v1) == placeholder % test1->tableSize);
   v1 = 223;
   placeholder = ((((((long)DJB2HASHINIT * DJB2HASHFACT + 223)
      * DJB2HASHFACT + 0) * DJB2HASHFACT + 0) * DJB2HASHFACT + 0));
   assert(_djb2Hash(test1, &v1) == placeholder % test1->tableSize);
   v2 = 1024;
   placeholder = (((((((((long)DJB2HASHINIT * DJB2HASHFACT + 0)
      * DJB2HASHFACT + 4) * DJB2HASHFACT + 0) * DJB2HASHFACT + 0)
      * DJB2HASHFACT + 0) * DJB2HASHFACT + 0) * DJB2HASHFACT + 0)
      * DJB2HASHFACT + 0);
   assert(_djb2Hash(test2, &v2) == placeholder % test2->tableSize);
   strcpy(v3, "cab");
   placeholder = ((((long)DJB2HASHINIT * DJB2HASHFACT + 'c')
      * DJB2HASHFACT + 'a') * DJB2HASHFACT + 'b');
   assert(_djb2Hash(test3, (void *)v3) == (placeholder % test3->tableSize));

   /*probe hashing with _zktHash*/
   v1 = 1024;
   placeholder = (((((long)ZKTHASHINIT * (0 + 1) * 1 + ZKTHASHINIT)
      * (4 + 2) * 2 + ZKTHASHINIT) * (0 + 3) * 3 + ZKTHASHINIT)
      * (0 + 4) * 4 + ZKTHASHINIT);
   assert(_zktHash(test1, &v1) == placeholder % (test1->tableSize - 1) + 1);
   v1 = 212;
   placeholder = (((((long)ZKTHASHINIT * (212 + 1) * 1 + ZKTHASHINIT)
      * (0 + 2) * 2 + ZKTHASHINIT) * (0 + 3) * 3 + ZKTHASHINIT)
      * (0 + 4) * 4 + ZKTHASHINIT);
   assert(_zktHash(test1, &v1) == placeholder % (test1->tableSize - 1) + 1);
   v2 = 1024;
   placeholder = (((((((((long)ZKTHASHINIT * (0 + 1) * 1 + ZKTHASHINIT)
      * (4 + 2) * 2 + ZKTHASHINIT) * (0 + 3) * 3 + ZKTHASHINIT)
      * (0 + 4) * 4 + ZKTHASHINIT) * (0 + 5) * 5 + ZKTHASHINIT)
      * (0 + 6) * 6 + ZKTHASHINIT) * (0 + 7) * 7 + ZKTHASHINIT)
      * (0 + 8) * 8 + ZKTHASHINIT);
   assert(_zktHash(test2, &v2) == placeholder % (test1->tableSize - 1) + 1);
   strcpy(v3, "bob");
   placeholder = ((((long)ZKTHASHINIT * ('b' + 1) * 1 + ZKTHASHINIT)
      * ('o' + 2) * 2 + ZKTHASHINIT) * ('b' + 3) * 3 + ZKTHASHINIT);
   assert(_zktHash(test3, (void *)v3) == placeholder % (test1->tableSize - 1) + 1);

   /*make sure things with different addresses hash the same if same value*/
   count = 0;
   for (v1 = 0; v1 < 10; v1++){
      v4 = v1;
      hash1 = _djb2Hash(test1, &v1);
      hash2 = _djb2Hash(test1, &v4);
      assert(hash1 == hash2);
      assert(hash1 < (unsigned long)test1->tableSize);
      if (hash1 == 0){
         count++;
      }
   }
   assert(count > 0);

   /*basic inserting and lookup, no probing yet*/
   v1 = 1;
   assoc_insert(&test1, &v1, NULL);
   assert(*(int *)(test1->table[_djb2Hash(test1, &v1)]->key) == v1);
   assert(test1->count == 1);
   assert(assoc_lookup(test1, &v1) == NULL);
   v4 = 3;
   v5 = 4;
   assoc_insert(&test1, &v4, &v5);
   assoc_insert(&test1, &v5, NULL);
   assert(test1->count == 3);
   assert(assoc_lookup(test1, &v4) == &v5);
   v2 = 100;
   v6 = 10010101000000;
   assoc_insert(&test2, &v2, NULL);
   assoc_insert(&test2, &v6, &v2);
   assert(test2->count == 2);
   assert(assoc_lookup(test2, &v6) == &v2);
   strcpy(v7, "avocado");
   assoc_insert(&test3, (void *)v3, NULL);
   assoc_insert(&test3, (void *)v7, (void *)v3);
   assert(strcmp((char *)assoc_lookup(test3, (void *)v7), "bob") == 0);
   strcpy(v8, "avocado");
   assoc_insert(&test3, (void *)v8, NULL);

   v10 = 3;
   assert(_keysMatch(test1, &v1, &v4) == false);
   assert(_keysMatch(test1, &v4, &v10) == true);
   assert(_keysMatch(test3, (void *)v7, NULL) == false);
   assert(_keysMatch(test3, (void *)v7, (void *)v8) == true);

   /*test inserting + overwriting*/
   assert(test3->count == 2);
   assoc_insert(&test3, (void *)v8, &v1);
   assert(test3->count == 2);
   assert(*(int *)(assoc_lookup(test3, (void *)v8)) == v1);

   /*test probing and collisions*/
   assert(_findNextProbe(test3, (unsigned long)3, (unsigned long) 5) == 15);
   assert(_findNextProbe(test3, (unsigned long)3, (unsigned long) 2) == 1);
   assert(_findNextProbe(test3, (unsigned long)3, (unsigned long) 3) == 0);
   strcpy(v9, "avocada"); /*luckily a collision with "bob"*/
   assert(_djb2Hash(test3, v9) == _djb2Hash(test3, v3));
   probe = _findNextProbe(test3, _djb2Hash(test3, v9), _zktHash(test3, v9));
   assoc_insert(&test3, (void *)v9, NULL);
   assert(strcmp(v9, (char *)test3->table[probe]->key) ==0);
   assert(test3->count == 3);
   assoc_insert(&test3, (void *)v9, NULL);
   assert(test3->count == 3);

   /*test, isOdd, isPrime, _nextPrime rehashing*/
   assert(_shouldRehash(test1) == false);
   assert(_shouldRehash(test2) == false);
   assert(_shouldRehash(test3) == false);
   test4 = NULL;
   assert(_shouldRehash(test4) == false);
   test1->count = test1->tableSize * REHASHMARK + 1;
   assert(_shouldRehash(test1) == true);
   test1->count = 3;
   assert(_isOdd(1) == true);
   assert(_isOdd(2) == false);
   assert(_isOdd(0) == false);
   assert(_isOdd(23) == true);
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
   assert(_rehash(NULL) == false);
   assert(_rehashTable(test1, test4) == false);
   test4 = assoc_init(sizeof(int));
   assert(_rehashTable(test1, test4) == true);
   assert(test4->tableSize == _nextPrime(test1->tableSize * RESIZEFACT));
   count = 0;
   for (i = 0; i < test4->tableSize; i++){
      if (test4->table[i] != NULL){
         count++;
      }
   }
   assert(count == test1->count);
   assoc_free(test4);

   /*test again with strings*/
   test4 = assoc_init(0);
   assert(_rehashTable(NULL, test4) == false);
   assert(_rehashTable(test3, NULL) == false);
   assert(_rehashTable(NULL, NULL) == false);
   assert(_rehashTable(test3, test4) == true);
   assert(test4->count == test3->count);
   assert(assoc_count(test4) == assoc_count(test3));
   assert(test4->tableSize == _nextPrime(test3->tableSize * RESIZEFACT));
   count = 0;
   for (i = 0; i < test4->tableSize; i++){
      if (test4->table[i] != NULL){
         count++;
      }
   }
   assert(count == test4->count);
   assoc_free(test4);
   test4 = NULL;
   assert(_rehash(&test4) == false);
   assert(_rehash(&test1) == true);
   assert(_rehash(&test2) == true);
   assert(_rehash(&test3) == true);
   assert(test1->tableSize == _nextPrime(INITSIZE * RESIZEFACT));
   assert(test1->count == 3);
   assert(test1->useStrings == false);
   assert(test1->table[36] != NULL);
   count = 0;
   for (i = 0; i < test1->tableSize; i++){
      if (test1->table[i] != NULL){
         count++;
      }
   }
   assert(count == test1->count);

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
