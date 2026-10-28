#include "port/credits_text.h"

size_t CreditsText_Parse(char *buffer, size_t size, char **entries, size_t capacity)
{
    char *end;
    char *p;
    size_t count;

    if (buffer == NULL || entries == NULL || size == 0)
        return 0;

    end = buffer + size;
    p = buffer;
    count = 0;
    while (count < capacity) {
        char *open;
        char *role;
        char *roleEnd;
        char *name;
        char *nameEnd;

        open = p;
        while (open < end && *open != '"')
            open++;
        if (open == end)
            break;
        role = open + 1;

        roleEnd = role;
        while (roleEnd < end && *roleEnd != '"')
            roleEnd++;
        if (roleEnd == end)
            break;
        *roleEnd = '\0';

        open = roleEnd + 1;
        while (open < end && *open != '"')
            open++;
        if (open == end)
            break;
        name = open + 1;

        nameEnd = name;
        while (nameEnd < end && *nameEnd != '"')
            nameEnd++;
        if (nameEnd == end)
            break;
        *nameEnd = '\0';

        entries[count * 2] = role;
        entries[count * 2 + 1] = name;
        count++;
        p = nameEnd + 1;
    }
    return count;
}
