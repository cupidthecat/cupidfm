#ifndef FUZZY_H
#define FUZZY_H

/* Case-insensitive subsequence match; smaller scores rank first, -1 means no match. */
int fuzzy_score(const char *query, const char *text);

#endif
