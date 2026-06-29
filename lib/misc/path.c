#include "tlibc_everything.h"

/* Normalizes a path relative to cwd into an absolute path, resolving ./ and ../ components.
   Writes at most max_len bytes into absolute_path (including null terminator). */
void tlibc_cal_absolute_path(const char *path, const char *cwd, char *absolute_path, size_t max_len) {
    if (max_len == 0) return;

    // Empty path -> use cwd
    if (path == NULL || path[0] == '\0')
    {
        snprintf(absolute_path, max_len, "%s", cwd ? cwd : "");
    }
    // Absolute path -> use as-is
    else if (path[0] == '/')
    {
        snprintf(absolute_path, max_len, "%s", path);
    }
    // Relative path -> prepend cwd
    else
    {
        snprintf(absolute_path, max_len, "%s/%s", cwd ? cwd : "", path);
    }

    // Resolve ./ and ../ components in-place
    char *p = absolute_path;
    while (*p != '\0')
    {
        /* ./ -> remove the "./" */
        if (*p == '.' && *(p + 1) == '/')
        {
            strcpy(p, p + 2);
        }
        /* ../ -> remove the previous segment and the "../" */
        else if (*p == '.' && *(p + 1) == '.' && *(p + 2) == '/')
        {
            char *current_dotdot_ptr = p;
            char *q = p - 2;

            if (q < absolute_path) { // "../" at start of path
                if (absolute_path[0] == '/') { // e.g. "/../foo" -> "/foo"
                    memmove(absolute_path + 1, current_dotdot_ptr + 3, strlen(current_dotdot_ptr + 3) + 1);
                    p = absolute_path;
                } else { // e.g. "../foo" -> "foo"
                    strcpy(absolute_path, current_dotdot_ptr + 3);
                    p = absolute_path - 1;
                }
            }
            else // Normal "segment/../" case
            {
                // Find the start of the segment before "../"
                while (q > absolute_path && *(q - 1) != '/') {
                    q--;
                }
                char *parent_dir_start = p - 1;
                if (parent_dir_start == absolute_path && *parent_dir_start == '/') { // "/../..."
                     memmove(absolute_path + 1, current_dotdot_ptr + 3, strlen(current_dotdot_ptr + 3) + 1);
                     p = absolute_path;
                     continue;
                } else if (parent_dir_start < absolute_path) { // "../..." (relative)
                     p = current_dotdot_ptr + 2;
                } else {
                    // Remove the segment before ".." and the "../"
                    char *segment_to_remove_start = parent_dir_start;
                    while(segment_to_remove_start > absolute_path && *(segment_to_remove_start - 1) != '/') {
                        segment_to_remove_start--;
                    }
                    strcpy(segment_to_remove_start, current_dotdot_ptr + 3);
                    p = segment_to_remove_start - 1;
                }
            }
        }
        else
        {
            p++;
        }
    }

    // Trailing "/." and "/.." cleanup
    // NOTE: The main loop above handles "foo/./bar" but NOT "foo/." (trailing).
    // This block catches trailing cases. Known to be fragile with short paths.
    size_t len_before_trailing_handle = strlen(absolute_path);
    if (len_before_trailing_handle >= 2) {
        char *pt = absolute_path + len_before_trailing_handle - 1;

        // path/. -> path
        if (*pt == '.' && *(pt - 1) == '/') {
            *(pt - 1) = '\0';
        }
        // path/.. -> path (pop last segment)
        else if (*pt == '.' && len_before_trailing_handle >= 3 && *(pt - 1) == '.' && *(pt - 2) == '/') {
            char *slash_before_dotdot = pt - 2;
            if (slash_before_dotdot == absolute_path) {
                absolute_path[1] = '\0'; // "/.." -> "/"
            } else {
                char *q = slash_before_dotdot - 1;
                while (q > absolute_path && *(q - 1) != '/') {
                    q--;
                }
                *q = '\0';
            }
        }
    } else if (len_before_trailing_handle == 1 && absolute_path[0] == '.') {
        absolute_path[0] = '\0';
    }

    // Collapse multiple leading slashes: "///foo" -> "/foo"
    if (absolute_path[0] == '/') {
        char *first_char = absolute_path;
        while (*(first_char) == '/' && *(first_char + 1) == '/') {
            strcpy(first_char, first_char + 1);
        }
    }

    // Remove trailing slash, but keep root "/"
    size_t len = strlen(absolute_path);
    if (len > 1 && absolute_path[len - 1] == '/')
    {
        absolute_path[len - 1] = '\0';
    }

    // Empty result -> root
    if (strlen(absolute_path) == 0)
    {
        strcpy(absolute_path, "/");
    }
    // Result must be absolute; prepend "/" if missing
    else if (absolute_path[0] != '/')
    {
        /* TODO: this path is suspicious - if cwd is absolute, result should always be absolute */
        size_t len2 = strlen(absolute_path);
        char *x = absolute_path + len2;
        *(x + 1) = '\0';
        while (x > absolute_path)
        {
            *x = *(x - 1);
            x--;
        }
        *x = '/';
    }
}