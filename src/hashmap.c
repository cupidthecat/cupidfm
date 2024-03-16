#include <stdlib.h>
#include <string.h>
#include "hashmap.h"

#define TABLE_SIZE 100

unsigned long hash(const char* str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % TABLE_SIZE;
}

unsigned int hash_function(const char *key) {
    unsigned int hash = 0;
    while (*key) {
        hash = (hash << 5) + *key++;
    }
    return hash;
}

HashMap* create_hash_map() {
    HashMap* hash_map = malloc(sizeof(HashMap));
    memset(hash_map, 0, sizeof(HashMap));
    return hash_map;
}

void insert(HashMap* hash_map, char* key, long value) {
    unsigned long index = hash(key);
    Node* node = malloc(sizeof(Node));
    if (node == NULL) {
        // Handle memory allocation failure
        return;
    }
    node->key = strdup(key);
    if (node->key == NULL) {
        // Handle memory allocation failure
        free(node);
        return;
    }
    node->value = value;
    node->next = hash_map->table[index];
    hash_map->table[index] = node;
}

long get(HashMap* hash_map, const char* key) {
    unsigned long index = hash(key);
    Node* node = hash_map->table[index];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return -1; // Return -1 if key is not found
}

void free_hash_map(HashMap* hash_map) {
    for (int i = 0; i < TABLE_SIZE; i++) {
        Node* node = hash_map->table[i];
        while (node != NULL) {
            Node* temp = node;
            node = node->next;
            free(temp->key);
            temp->key = NULL;
            free(temp);
            temp = NULL;
        }
    }
    free(hash_map);
    hash_map = NULL;
}

// Function to check if a key exists in the HashMap
bool HashMap_containsKey(const HashMap *map, const char *key) {
    // Check if map is NULL
    if (map == NULL) {
        return false;
    }

    // Calculate the hash of the key
    int hash = hash_function(key);

    // Check if hash is within the bounds of the table
    if (hash < 0 || hash >= TABLE_SIZE) {
        return false;
    }

    // Get the linked list for this hash
    Node *list = map->table[hash];

    // Iterate over the linked list
    while (list != NULL) {
        // If the key of the current node matches the given key, return true
        if (strcmp(list->key, key) == 0) {
            return true;
        }
        // Move to the next node
        list = list->next;
    }
    // If no matching key was found, return false
    return false;
}

// Function to get the value associated with a key in the HashMap
long HashMap_getValue(const HashMap *map, const char *key) {
    // Calculate the hash of the key
    int hash = hash_function(key);
    // Get the linked list for this hash
    Node *list = map->table[hash];
    // Iterate over the linked list
    while (list != NULL) {
        // If the key of the current node matches the given key, return its value
        if (strcmp(list->key, key) == 0) {
            return list->value;
        }
        // Move to the next node
        list = list->next;
    }
    // If no matching key was found, return a default value (e.g., -1)
    return -1;
}

// Function to put a key-value pair into the HashMap
void HashMap_put(HashMap *map, const char *key, long value) {
    // Calculate the hash of the key
    int hash = hash_function(key);
    // Create a new node
    Node *new_node = malloc(sizeof(Node));
    if (new_node == NULL) {
        // Handle memory allocation failure
        return;
    }
    new_node->key = strdup(key);
    if (new_node->key == NULL) {
        // Handle memory allocation failure
        free(new_node);
        return;
    }
    new_node->value = value;
    new_node->next = NULL;
    // If the linked list for this hash is empty, insert the new node at the beginning
    if (map->table[hash] == NULL) {
        map->table[hash] = new_node;
    } else {
        // Otherwise, append the new node to the end of the linked list
        Node *current = map->table[hash];
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }
}