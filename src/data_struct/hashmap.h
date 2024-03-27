#ifndef HASHMAP_H
#define HASHMAP_H

#include "hashmap.h"
#include "../dir_size_calc.h"

typedef struct Node {
    char* key;
    long value;
    struct Node* next;
} Node;

typedef struct HashMap {
    Node* table[100];
} HashMap;

HashMap* create_hash_map();
void insert(HashMap* hash_map, char* key, long value);
long get(HashMap* hash_map, const char* key);
void free_hash_map(HashMap* hash_map);
bool HashMap_containsKey(const HashMap *map, const char *key);
long HashMap_getValue(const HashMap *map, const char *key);
void HashMap_put(HashMap *map, const char *key, long value);

#endif // HASHMAP_H