#include "fuzzy.h"

#include <ctype.h>
#include <limits.h>
#include <stddef.h>

int fuzzy_score(const char *query, const char *text) {
    if (!query || !text)
        return -1;
    while (*query && isspace((unsigned char)*query))
        query++;
    if (!*query)
        return 0;
    while (*text && isspace((unsigned char)*text))
        text++;
    int best = INT_MAX;
    for (size_t start = 0; text[start]; start++) {
        if (tolower((unsigned char)text[start]) != tolower((unsigned char)*query))
            continue;
        size_t position = start;
        int score = (int)start;
        if (start && isalnum((unsigned char)text[start - 1]))
            score += 20;
        const char *pattern = query;
        for (; *pattern; pattern++) {
            if (isspace((unsigned char)*pattern))
                continue;
            size_t before = position;
            while (text[position] &&
                   tolower((unsigned char)text[position]) != tolower((unsigned char)*pattern))
                position++;
            if (!text[position])
                break;
            score += (int)(position - before) * 3;
            position++;
        }
        if (!*pattern && score < best)
            best = score;
    }
    return best == INT_MAX ? -1 : best;
}
