#include "../../../ADTs/General/bool.h"

#define INITSIZE 16
#define STRINGTYPE 0

struct entry{
   void *key;
   void *data;
};
typedef struct entry entry;

struct table{
   entry **ary;
   int size;
   int count;
};
typedef struct table table;

struct assoc{
   table *base;
   table *cuckoo;
   int mod;
   int keySize;
   int useStrings;
};
typedef struct assoc assoc;

void _test();
