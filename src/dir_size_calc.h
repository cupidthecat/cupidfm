#ifndef DIR_SIZE_H
#define DIR_SIZE_H

// Forward declaration of HashMap
typedef struct HashMap HashMap;

long get_directory_size(const char *dir_path, int max_depth);
void display_directory_size(const char *dir_path, HashMap *hash_map);
long *calculate_directory_size(void *arg);
#endif // DIR_SIZE_H