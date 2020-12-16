#include "../../../ADTs/General/bool.h"

#define INITSIZE 17
#define STRINGTYPE 0

struct entry{
   void *key;
   void *data;
};
typedef struct entry entry;

struct assoc{
   entry** table;
   int count;
   int tableSize;
   int dataSize;
   int useStrings;
   int hash; /*probably don't need that*/
};
typedef struct assoc assoc;

void _test();
