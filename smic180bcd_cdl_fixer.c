/**
 * @file smic180bcd_cdl_fixer.c
 * @author Huang Rui (vowstar@gmail.com)
 * @brief Fix smic180bcd cdl netlist for ic618 spiceIn
 * @version 0.1
 * @date 2024-03-28
 *
 */

#include <math.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Converts an SI unit string to a double */
double si_to_double(const char *si_str) {
    double value;
    char unit[3]; /* Buffer to hold SI unit, e.g., "n", "k" */

    /* Scan the string for a double followed by a string */
    if (sscanf(si_str, "%lf%s", &value, unit) < 1) {
        return NAN; /* Return Not-a-Number if conversion fails */
    }

    /* Map SI units to their multiplier */
    const struct { char *unit; double multiplier; } units[] = {
        {"y", 1e-24}, {"z", 1e-21}, {"a", 1e-18}, {"f", 1e-15}, {"p", 1e-12},
        {"n", 1e-9}, {"u", 1e-6}, {"m", 1e-3}, {"c", 1e-2}, {"d", 1e-1},
        {"da", 1e1}, {"h", 1e2}, {"k", 1e3}, {"M", 1e6}, {"G", 1e9},
        {"T", 1e12}, {"P", 1e15}, {"E", 1e18}, {"Z", 1e21}, {"Y", 1e24}
    };

    size_t num_units = sizeof(units) / sizeof(units[0]);
    for (size_t i = 0; i < num_units; i++) {
        if (strcmp(units[i].unit, unit) == 0) {
            return value * units[i].multiplier;
        }
    }

    return value; /* Return the value as is if no unit is found */
}

/* Converts a double to an SI unit string */
void double_to_si(double value, char *si_str, size_t max_len) {
    const struct { char *unit; double divisor; } units[] = {
        {"Y", 1e24}, {"Z", 1e21}, {"E", 1e18}, {"P", 1e15}, {"T", 1e12},
        {"G", 1e9}, {"M", 1e6}, {"k", 1e3}, {"h", 1e2}, {"da", 1e1},
        {"d", 1e-1}, {"c", 1e-2}, {"m", 1e-3}, {"u", 1e-6}, {"n", 1e-9},
        {"p", 1e-12}, {"f", 1e-15}, {"a", 1e-18}, {"z", 1e-21}, {"y", 1e-24}
    };

    size_t num_units = sizeof(units) / sizeof(units[0]);
    for (size_t i = 0; i < num_units; i++) {
        /* Check if the value fits into one of the SI unit ranges */
        if (fabs(value) >= units[i].divisor && units[i].divisor != 1) {
            snprintf(si_str, max_len, "%g%s", value / units[i].divisor, units[i].unit);
            return;
        }
    }

    /* Fallback to simple decimal representation if no unit fits */
    snprintf(si_str, max_len, "%g", value);
}

/* Function to check and prepend string if pattern is not matched */
int check_and_prepend(char **buffer, long *length, const char *pattern, const char *prepend_str) {
    regex_t regex;
    regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB | REG_NEWLINE);
    if (regexec(&regex, (const char *)*buffer, 0, NULL, 0) != 0) { /* No match */
        size_t prepend_length = strlen(prepend_str);
        size_t new_length = prepend_length + *length;
        char *new_buffer = (char *)malloc(new_length);
        if (!new_buffer) {
            regfree(&regex);
            return 0; /* Memory allocation failed */
        }
        memcpy(new_buffer, prepend_str, prepend_length);
        memcpy(new_buffer + prepend_length, *buffer, *length);
        free(*buffer);
        *buffer = new_buffer;
        *length = new_length;
    }
    regfree(&regex);
    return 1; /* Success */
}

/* Function to replace substrings in a dynamic buffer */
void replace_substrings(char **buffer, const char *patterns[], size_t num_patterns, size_t *buffer_size) {
    size_t original_len = strlen(*buffer);
    size_t tmp_buffer_size = original_len + 1; /* Include space for null terminator */
    char *result = malloc(tmp_buffer_size);
    if (!result) {
        fprintf(stderr, "Memory allocation failed\n");
        return;
    }
    memset(result, 0, tmp_buffer_size);

    char *original = *buffer;
    char *temp = result;
    size_t used = 0;

    while (*original) {
        int replaced = 0;
        for (size_t i = 0; i < num_patterns; i += 2) {
            size_t len = strlen(patterns[i]);
            if (strncmp(original, patterns[i], len) == 0) {
                size_t replacement_len = strlen(patterns[i + 1]);
                if (used + replacement_len >= tmp_buffer_size) {
                    /* Resize buffer if needed */
                    tmp_buffer_size = tmp_buffer_size + replacement_len + original_len;
                    char *new_result = realloc(result, tmp_buffer_size);
                    if (!new_result) {
                        fprintf(stderr, "Memory allocation failed\n");
                        free(result);
                        return;
                    }
                    result = new_result;
                    temp = result + used; /* Reset temp to new position */
                }
                strcpy(temp, patterns[i + 1]);
                original += len;
                temp += replacement_len;
                used += replacement_len;
                replaced = 1;
                break;
            }
        }
        if (!replaced) {
            if (used + 1 >= tmp_buffer_size) {
                /* Resize buffer if needed */
                tmp_buffer_size = tmp_buffer_size + 1 + original_len;
                char *new_result = realloc(result, tmp_buffer_size);
                if (!new_result) {
                    fprintf(stderr, "Memory allocation failed\n");
                    free(result);
                    return;
                }
                result = new_result;
                temp = result + used; /* Reset temp to new position */
            }
            *temp++ = *original++;
            used++;
        }
    }
    *temp = '\0';
    free(*buffer);
    result[used] = '\0'; /* Null-terminate the result */
    *buffer = result;
    *buffer_size = used;
}

/* Structure for a node in the linked list */
struct line_node {
    char *line;              /* Pointer to the string */
    struct line_node *next;  /* Pointer to the next node */
};

/* Function to free the memory allocated for the linked list */
void free_lines(struct line_node *head) {
    while (head) {
        struct line_node *temp = head;
        head = head->next;
        free(temp->line);  /* Free the string */
        free(temp);        /* Free the node */
    }
}

/* Function to split buffer into a linked list of strings */
struct line_node *split_buffer(const char *buffer, size_t *line_count) {
    *line_count = 0;  /* Initialize line count */
    if (!buffer || !*buffer) {
        return NULL;  /* Return NULL for empty buffer */
    }

    struct line_node *head = NULL, *current = NULL;

    /* Iterate over the buffer to split it into lines */
    const char *start = buffer;
    while (1) {
        const char *end = strchr(start, '\n');  /* Find the end of the current line */
        size_t len = (end ? end - start : strlen(start));  /* Compute line length */

        if (len > 0) {  /* Check if line has content */
            /* Allocate memory for the new node */
            struct line_node *new_node = malloc(sizeof(struct line_node));
            if (!new_node) {
                free_lines(head);  /* Free previously allocated nodes */
                fprintf(stderr, "Memory allocation failed\n");
                return NULL;
            }

            /* Allocate memory for the line */
            new_node->line = malloc(len + 1);
            if (!new_node->line) {
                free(new_node);
                free_lines(head);
                fprintf(stderr, "Memory allocation failed\n");
                return NULL;
            }

            /* Copy the line into the new node */
            strncpy(new_node->line, start, len);
            new_node->line[len] = '\0';  /* Null-terminate the string */
            new_node->next = NULL;

            /* Link the new node into the list */
            if (current) {
                current->next = new_node;
            } else {
                head = new_node;
            }
            current = new_node;
            (*line_count)++;  /* Increment line count */
        }
        if (!end) break;  /* Exit loop if no more lines */
        start = end + 1;  /* Move to the start of the next line */
    }
    return head;
}

/* Function to join a linked list of strings into a single buffer with newline separators */
char *join_lines(struct line_node *head, size_t *buffer_size) {
    *buffer_size = 0;
    if (!head) {
        return NULL;  /* Return NULL for empty list */
    }

    /* Calculate the total length of the joined string */
    size_t total_length = 0;
    for (struct line_node *current = head; current; current = current->next) {
        total_length += strlen(current->line) + 1;  /* Include space for newline */
    }

    /* Allocate memory for the combined buffer */
    char *buffer = malloc(total_length + 1);  /* Add space for null terminator */
    if (!buffer) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }

    /* Copy lines into the buffer and add newline characters */
    char *ptr = buffer;
    for (struct line_node *current = head; current; current = current->next) {
        ptr = stpcpy(ptr, current->line);  /* Copy line to buffer */
        *ptr++ = '\n';  /* Add newline character */
    }
    *ptr = '\0';  /* Null-terminate the buffer */
    *buffer_size = total_length;  /* Update buffer size */

    return buffer;
}

/* Function to process each line of the buffer */
void process_buffer(char **buffer_ptr, size_t *buffer_size) {
    regex_t regex_w, regex_l, regex_fingers, regex_area, regex_pj;
    regcomp(&regex_w, "w=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);
    regcomp(&regex_l, "l=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);
    regcomp(&regex_fingers, "fingers=([0-9]+\\.?[0-9]*[a-zA-Z]*)", REG_EXTENDED);
    regcomp(&regex_area, "area=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);
    regcomp(&regex_pj, "pj=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);

    size_t line_count = 0;
    struct line_node *head = split_buffer(*buffer_ptr, &line_count);
    struct line_node *current = head;
#if 1
    while (current) {
        double w = 0.0, l = 0.0, fw, fingers = 1.0;
        double area = 0.0, pj = 0.0;
        char fw_str[100], l_str[100], w_str[100];
        bool w_found = false, l_found = false, fingers_found = false;
        bool area_found = false, pj_found = false;

        regmatch_t matches[2];

        /* Check for 'w' value and convert to double */
        if (regexec(&regex_w, current->line, 2, matches, 0) == 0) {
            w = si_to_double(current->line + matches[1].rm_so);
            w_found = true;
        }
        /* Check for 'l' value and convert to double */
        if (regexec(&regex_l, current->line, 2, matches, 0) == 0) {
            l = si_to_double(current->line + matches[1].rm_so);
            l_found = true;
        }

        /* Check for 'fingers' value and convert to double */
        if (regexec(&regex_fingers, current->line, 2, matches, 0) == 0) {
            fingers = si_to_double(current->line + matches[1].rm_so);
            fingers_found = true;
        }

        /* Calculate fw and append it to the line */
        if (w_found && l_found) {
            fw = fingers_found ? (w / fingers) : w;
            double_to_si(fw, fw_str, sizeof(fw_str));
            char *new_line = (char *)malloc((strlen(current->line) + sizeof(fw_str) + 10) * sizeof(char));
            if (!new_line) {
                fprintf(stderr, "Memory allocation failed\n");
                exit(1);
            }
            sprintf(new_line, "%s fw=%s", current->line, fw_str);
            free(current->line);
            current->line = new_line;
        }

        /* Check for 'area' value and convert to double */
        if (regexec(&regex_area, current->line, 2, matches, 0) == 0) {
            area = si_to_double(current->line + matches[1].rm_so);
            area_found = true;
        }

        /* Check for 'pj' value and convert to double */
        if (regexec(&regex_pj, current->line, 2, matches, 0) == 0) {
            pj = si_to_double(current->line + matches[1].rm_so);
            pj_found = true;
        }

        if (area_found && pj_found) {
            double delta = pj * pj - 4 * area;
            if (delta < 0) {
                /* If delta is negative, continue to next line */
                current = current->next;
                continue;
            }
            double delta_sqrt = sqrt(pj * pj / 4 - 4 * area);
            double l1 = (pj / 2 + delta_sqrt) / 2;
            double l2 = (pj / 2 - delta_sqrt) / 2;
            double w1, w2;

            /* Check if both l1 and l2 are less than or equal to 0 */
            if (l1 <= 0 && l2 <= 0) {
                /* If both are <= 0, continue to next line */
                current = current->next;
                continue;
            }

            /* Calculate w1 and w2 */
            w1 = area / l1;
            w2 = area / l2;

            /* Compare l1 and w1 */
            if (l1 >= w1) {
                /* If l1 is less than or equal to w1, select l1 and w1 */
                double_to_si(l1, l_str, sizeof(l_str));
                double_to_si(w1, w_str, sizeof(w_str));
            } else {
                /* Otherwise, select l2 and w2 */
                double_to_si(l2, l_str, sizeof(l_str));
                double_to_si(w2, w_str, sizeof(w_str));
            }

            /* Append l and w to the line */
            char *new_line = (char *)malloc((strlen(current->line) + strlen(w_str) + strlen(l_str) + 10) * sizeof(char));
            if (!new_line) {
                fprintf(stderr, "Memory allocation failed\n");
                exit(1);
            }
            sprintf(new_line, "%s w=%s l=%s", current->line, w_str, l_str);
            free(current->line);
            current->line = new_line;
        }

        current = current->next;
    }
#endif
    /* Join the lines into a buffer */
    char *result_buffer = join_lines(head, buffer_size);
    free_lines(head);
    *buffer_ptr = result_buffer;

    /* Free the regex structures */
    regfree(&regex_w);
    regfree(&regex_l);
    regfree(&regex_fingers);
    regfree(&regex_area);
    regfree(&regex_pj);
}

int main() {
    char *buffer;
    long length;
    FILE *f = stdin;

    /* Seek to the end of the file to get length */
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Allocate memory for the buffer */
    buffer = (char *)malloc(length);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }
    memset(buffer, 0, length);

    /* Read the file into the buffer */
    fread(buffer, 1, length, f);

    /* Prepend param information */
    do {
        const char *header =
            "\n"
            "************************************************************************\n"
            "* CDL netlist\n"
            "************************************************************************\n"
            "\n";
        size_t header_length = strlen(header);
        size_t new_length = header_length + length;
        char *new_buffer = (char *)malloc((new_length+1)*sizeof(char));
        if (!new_buffer) {
            fprintf(stderr, "Failed to allocate memory for new buffer\n");
            free(buffer);
            return 1;
        }
        memset(new_buffer, 0, new_length);
        memcpy(new_buffer, header, header_length);
        memcpy(new_buffer + header_length, buffer, length);
        free(buffer);
        buffer = new_buffer;
        length = new_length;
        buffer[length] = '\0'; /* Null-terminate the buffer */
    }
    while (0);

    /* Define patterns and corresponding strings to prepend, in reverse order */
    const char *patterns[] = {
        "^\\.PARAM.*\n", ".PARAM\n",
        "^\\*\\.MEGA.*\n", "*.MEGA\n",
        "^\\*\\.EQUATION.*\n", "*.EQUATION\n",
        "^\\*\\.DIOAREA.*\n", "*.DIOAREA\n",
        "^\\*\\.DIOPERI.*\n", "*.DIOPERI\n",
        "^\\*\\.CAPVAL.*\n", "*.CAPVAL\n",
        "^\\*\\.RESVAL.*\n", "*.RESVAL\n",
        "^\\*\\.BIPOLAR.*\n", "*.BIPOLAR\n"
    };
    size_t num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    /* Check and prepend strings if necessary */
    for (size_t i = 0; i < num_patterns; i += 2) {
        if (!check_and_prepend(&buffer, &length, patterns[i], patterns[i + 1])) {
            fprintf(stderr, "Failed to allocate memory for new buffer\n");
            free(buffer);
            return 1;
        }
    }

    /* Prepend header information */
    do {
        const char *header =
            "************************************************************************\n"
            "* Generated by by smic180bcd_cdl_fixer\n"
            "* Author: Huang Rui <vowstar@gmail.com>\n"
            "\n"
            "* CDL parameter\n"
            "************************************************************************\n"
            "\n";
        size_t header_length = strlen(header);
        size_t new_length = header_length + length;
        char *new_buffer = (char *)malloc(new_length);
        if (!new_buffer) {
            fprintf(stderr, "Failed to allocate memory for new buffer\n");
            free(buffer);
            return 1;
        }
        memset(new_buffer, 0, new_length);
        memcpy(new_buffer, header, header_length);
        memcpy(new_buffer + header_length, buffer, length);
        free(buffer);
        buffer = new_buffer;
        length = new_length;
        buffer[length] = '\0'; /* Null-terminate the buffer */
    }
    while (0);

    /* Define CDL patterns and their replacements */
    const char *cdl_patterns[] = {
        " W=", " w=",
        " L=", " l=",
        " AREA=", " area=",
        " PJ=", " pj=",
        " M=", " m=",
        " FW=", " fw=",
        " C=", " c=",
        " R=", " r=",
        " FINGERS=", " fingers=",
    };

    /* Replace substrings in the buffer */
    replace_substrings(&buffer, cdl_patterns, sizeof(patterns) / sizeof(patterns[0]), &length);

    /* Process the buffer to calculate fw */
    process_buffer(&buffer, &length);

    /* Output buffer to stdout */
    fwrite(buffer, 1, length, stdout);

    /* Free buffer */
    free(buffer);
    return 0;
}
