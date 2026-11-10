#ifndef SELECTION_H
#define SELECTION_H

#include "vector.h"

typedef struct PathSelection PathSelection;

/* Paths are absolute logical paths. Ancestor/descendant selections conflict. */
bool selection_set(PathSelection **selection, const char *path, bool directory, bool selected);
bool selection_has(const PathSelection *selection, const char *path);
size_t selection_count(const PathSelection *selection);
/* Exports owned strings in selection order. Release even an empty result with Vector_bye. */
bool selection_export(const PathSelection *selection, Vector *paths);
void selection_clear(PathSelection *selection);
void selection_destroy(PathSelection *selection);
void selection_prune(PathSelection *selection);

#endif
