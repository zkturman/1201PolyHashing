#include "../../../ADTs/General/bool.h"

struct entry{
   void *key;
   void *data;
};
typedef struct entry entry;

struct assoc{
   entry **table;
   int count;
   int tableSize;
   int keySize;
   int useStrings;
};
typedef struct assoc assoc;
