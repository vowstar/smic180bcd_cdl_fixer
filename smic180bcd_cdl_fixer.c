/**
 * @file smic180bcd_cdl_fixer.c
 * @author Huang Rui (vowstar@gmail.com)
 * @brief Fix smic180bcd cdl netlist for ic618 spiceIn
 * @version 0.1
 * @date 2024-03-28
 *
 */

#include <ctype.h>
#include <math.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "argparse.h"

#define MAX_LINE_LENGTH (4096)
#define MAX_NAME_LENGTH (128)

/* Structure for a node in the linked list */
struct line_node {
    char *line;              /* Pointer to the string */
    struct line_node *next;  /* Pointer to the next node */
};

/* Linked list structure for port information */
struct port_node {
    char *port_name;         /* Name of the port */
    char direction;          /* 'I': in, 'O': out, 'B': inout */
    struct port_node *next;  /* Pointer to the next node */
};

/* Linked list structure for module information */
struct module_node {
    char *module_name;       /* Name of the module */
    struct port_node *ports; /* Linked list of port information */
    struct module_node *next;/* Pointer to the next node */
};

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

/* Function to insert a new line at the beginning of the list */
void prepend_line(struct line_node **head, const char *new_line) {
    struct line_node *new_node = (struct line_node *)malloc(sizeof(struct line_node));
    if (!new_node) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1); /* Exit if memory allocation fails */
    }
    new_node->line = strdup(new_line);  /* Duplicate the string */
    new_node->next = *head;
    *head = new_node;
}

/* Function to check each line and prepend if pattern is not matched */
void check_and_prepend(struct line_node **head, const char *pattern, const char *prepend_str) {
    regex_t regex;
    int prepend_needed = 1;
    struct line_node *current = *head;

    regcomp(&regex, pattern, REG_EXTENDED | REG_NOSUB | REG_NEWLINE);

    /* Check each line for the pattern */
    while (current) {
        if (regexec(&regex, current->line, 0, NULL, 0) == 0) { /* Match found */
            prepend_needed = 0;
            break;
        }
        current = current->next;
    }

    /* Prepend if no lines match the pattern */
    if (prepend_needed) {
        prepend_line(head, prepend_str);
    }

    regfree(&regex);
}

/* Helper function to replace all occurrences of a pattern in a string */
char *str_replace(const char *str, const char *pattern, const char *replacement) {
    size_t str_len = strlen(str);
    size_t pattern_len = strlen(pattern);
    size_t replacement_len = strlen(replacement);

    /* Count the number of occurrences of the pattern */
    size_t count = 0;
    const char *tmp = str;
    while ((tmp = strstr(tmp, pattern))) {
        count++;
        tmp += pattern_len;
    }

    /* Calculate new string length */
    size_t new_len = str_len + count * (replacement_len - pattern_len);

    /* Allocate memory for the new string */
    char *result = malloc(new_len + 1);  /* +1 for the null terminator */
    if (!result) {
        return NULL;  /* Memory allocation failed */
    }

    /* Replace each occurrence of the pattern */
    const char *current = str;
    char *new_str = result;
    while ((tmp = strstr(current, pattern))) {
        size_t len = tmp - current;
        memcpy(new_str, current, len);  /* Copy characters before the pattern */
        memcpy(new_str + len, replacement, replacement_len);  /* Copy replacement */
        current = tmp + pattern_len;
        new_str += len + replacement_len;
    }
    strcpy(new_str, current);  /* Copy the rest of the string */

    return result;
}

/* Function to replace substrings in each line of the linked list */
void replace_substrings(struct line_node *head, const char **patterns, size_t pattern_count) {
    if (!head) {
        return; /* No operation if list is empty */
    }

    struct line_node *current = head;
    while (current) {
        for (size_t i = 0; i < pattern_count; i += 2) {
            const char *pattern = patterns[i];
            const char *replacement = patterns[i + 1];

            char *result = str_replace(current->line, pattern, replacement);
            if (result) {
                free(current->line);
                current->line = result;
            }
        }
        current = current->next;
    }
}

/* Function to process each line of the linked list */
void process_list(struct line_node *head) {
    if (!head) {
        return; /* No operation if list is empty */
    }
    /* Compile regular expressions for matching */
    regex_t regex_w, regex_l, regex_fingers, regex_area, regex_pj;
    regcomp(&regex_w, "w=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);
    regcomp(&regex_l, "l=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);
    regcomp(&regex_fingers, "fingers=([0-9]+\\.?[0-9]*[a-zA-Z]*)", REG_EXTENDED);
    regcomp(&regex_area, "area=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);
    regcomp(&regex_pj, "pj=([0-9]+\\.?[0-9]*[a-zA-Z]+)", REG_EXTENDED);

    struct line_node *current = head;

    while (current) {
        /* Initialize variables for processing */
        double w = 0.0, l = 0.0, fw, fingers = 1.0;
        double area = 0.0, pj = 0.0;
        char fw_str[MAX_NAME_LENGTH], l_str[MAX_NAME_LENGTH], w_str[MAX_NAME_LENGTH];
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

        /* Move to the next line in the list */
        current = current->next;
    }

    /* Free the regex structures */
    regfree(&regex_w);
    regfree(&regex_l);
    regfree(&regex_fingers);
    regfree(&regex_area);
    regfree(&regex_pj);
}

/**
 * Function to free a linked list of port nodes.
 * This function will iterate through the list of port_node and free each node.
 * @param ports Pointer to the head of the port_node list.
 */
void free_ports(struct port_node *ports) {
    struct port_node *current_port = ports;
    while (current_port) {
        struct port_node *next_port = current_port->next;
        free(current_port->port_name);  // Free the dynamically allocated port name
        free(current_port);             // Free the port node itself
        current_port = next_port;       // Move to the next port node
    }
}

/*
 * Function to parse the input.soc_mod file and create a linked list of module information.
 * This function dynamically allocates memory for module_node and port_node structures.
 * It returns the head of the module linked list.
 */
struct module_node* parse_soc_mod_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filename);
        return NULL;
    }

    char line[MAX_LINE_LENGTH];
    struct module_node *modules_head = NULL;
    struct module_node *current_module = NULL;
    struct module_node *new_module = NULL;
    struct port_node *last_port = NULL;
    const int module_indent_level = 0;
    const int port_indent_level = 4;
    const int direction_indent_level = 6;

    while (fgets(line, sizeof(line), file)) {
        /* Remove newline character */
        line[strcspn(line, "\n")] = 0;

        /* Determine the indentation level */
        int current_indent = 0;
        while (isspace((unsigned char)line[current_indent])) current_indent++;

        /* Skip empty lines and comments */
        if (line[current_indent] == '\0' || (line[current_indent] == '#')) continue;

        /* Check if the line represents a module name */
        if (current_indent == module_indent_level) {
            new_module = malloc(sizeof(struct module_node));
            new_module->module_name = strdup(line + current_indent);
            char *colon_pos = strchr(new_module->module_name, ':');
            if (colon_pos) *colon_pos = '\0'; /* Remove the colon */

            new_module->ports = NULL;
            new_module->next = NULL;

            if (current_module) {
                current_module->next = new_module;
            } else {
                modules_head = new_module;
            }
            current_module = new_module;
            last_port = NULL;
        }
        /* Check if the line represents a port name */
        else if (current_indent == port_indent_level) {
            char port_name[MAX_NAME_LENGTH];
            char format_string[20];
            snprintf(format_string, sizeof(format_string), "%%%ds", MAX_NAME_LENGTH - 1);
            sscanf(line + current_indent, format_string, port_name);
            char *colon_pos = strchr(port_name, ':');
            if (colon_pos) *colon_pos = '\0'; /* Remove the colon */

            struct port_node *new_port = malloc(sizeof(struct port_node));
            new_port->port_name = strdup(port_name);
            new_port->direction = 'B'; /* Default direction to 'B' */
            new_port->next = NULL;

            if (last_port) {
                last_port->next = new_port;
            } else if (current_module) {
                current_module->ports = new_port;
            }
            last_port = new_port;
        }
        /* Check if the line represents a port direction */
        else if (current_indent == direction_indent_level && strstr(line, "direction:")) {
            char *direction = strstr(line, "direction:") + strlen("direction:");
            while (isspace((unsigned char)*direction)) direction++;

            /* Set the direction of the last port node */
            if (last_port) {
                last_port->direction = (direction[0] == 'i' ? 'I' : (direction[0] == 'o' ? 'O' : 'B'));
            }
        }
    }

    fclose(file);
    return modules_head;
}

/**
 * Function to free a linked list of module nodes.
 * This function will iterate through the list of module_node and free each node,
 * along with its associated ports by calling free_ports.
 * @param modules Pointer to the head of the module_node list.
 */
void free_modules(struct module_node *modules) {
    struct module_node *current_module = modules;
    while (current_module) {
        struct module_node *next_module = current_module->next;
        free(current_module->module_name);  // Free the dynamically allocated module name
        free_ports(current_module->ports);   // Free the linked list of ports
        free(current_module);               // Free the module node itself
        current_module = next_module;       // Move to the next module node
    }
}

/**
 * Function to insert or update *.PININFO line after .SUBCKT line in the linked list.
 * It searches for .SUBCKT lines, extracts the module name, and then adds or updates
 * the *.PININFO line based on the module information in the module_node linked list.
 */
void insert_pininfo(struct line_node *head, struct module_node *modules) {
    while (head && head->next) {
        if (strncmp(head->line, ".SUBCKT", strlen(".SUBCKT")) == 0) {
            char module_name[MAX_NAME_LENGTH];  /* Assuming a max module name length of MAX_NAME_LENGTH */
            char format_string[20];
            snprintf(format_string, sizeof(format_string), "%%%ds", MAX_NAME_LENGTH - 1);
            sscanf(head->line + strlen(".SUBCKT"), format_string, module_name);  /* Extract module name */

            /* Find corresponding module information */
            struct module_node *current_module = modules;
            while (current_module) {
                if (strcmp(current_module->module_name, module_name) == 0) {
                    /* Check if the module has ports */
                    if (current_module->ports) {
                        /* Match found, build the *.PININFO line */
                        char pininfo_line[MAX_LINE_LENGTH] = "*.PININFO";  /* Start building PININFO line */
                        struct port_node *current_port = current_module->ports;
                        while (current_port) {
                            char port_info[MAX_NAME_LENGTH];
                            sprintf(port_info, " %s:%c", current_port->port_name, current_port->direction);
                            strcat(pininfo_line, port_info);
                            current_port = current_port->next;
                        }

                        /* Check if next line is already a PININFO line */
                        if (head->next && strncmp(head->next->line, "*.PININFO", strlen("*.PININFO")) == 0) {
                            free(head->next->line);  /* Free the existing line */
                            head->next->line = strdup(pininfo_line);  /* Replace with new line */
                        } else {
                            /* Insert new PININFO line */
                            struct line_node *new_node = malloc(sizeof(struct line_node));
                            new_node->line = strdup(pininfo_line);
                            new_node->next = head->next;
                            head->next = new_node;
                        }
                    }
                    break;  /* Exit loop after processing */
                }
                current_module = current_module->next;
            }
        }
        head = head->next;
    }
}

int main(int argc, const char *argv[]) {
    char *buffer;
    long length;
    FILE *file_in = stdin;
    FILE *file_out = stdout;

    /* Defile argparse variables */
    int no_param = 0;
    int no_case_conversion = 0;
    int no_calc_data = 0;
    const char *soc_module = NULL;
    const char *input = NULL;
    const char *output = NULL;

    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Basic options"),
        OPT_STRING('i', "input", &input, "input file", NULL, 0, 0),
        OPT_STRING('o', "output", &output, "output file", NULL, 0, 0),
        OPT_GROUP("Additional options"),
        OPT_BOOLEAN(0, "no-param", &no_param, "disable param", NULL, 0, 0),
        OPT_BOOLEAN(0, "no-case-conversion", &no_case_conversion, "disable case conversion", NULL, 0, 0),
        OPT_BOOLEAN(0, "no-calc-data", &no_calc_data, "disable data calculation", NULL, 0, 0),
        OPT_STRING('m', "soc-module", &soc_module, "specify SOC module", NULL, 0, 0),
        OPT_END(),
    };

    static const char *const usages[] = {
        "smic180bcd_cdl_fixer < input.cdl > output.cdl",
        "smic180bcd_cdl_fixer --input input.cdl --output output.cdl",
        "smic180bcd_cdl_fixer --input input.cdl --output output.cdl --soc-module example.soc_mod",
        NULL,
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usages, 0);
    argparse_describe(&argparse, "Fix smic180bcd cdl netlist for ic618 spiceIn", NULL);
    argc = argparse_parse(&argparse, argc, argv);

    /* Process input file path */
    if (input != NULL) {
        file_in = fopen(input, "r");
        if (!file_in) {
            fprintf(stderr, "Failed to open file: %s\n", input);
            return 1;
        }
    }

    /* Process output file path */
    if (output != NULL) {
        file_out = fopen(output, "w");
        if (!file_out) {
            fprintf(stderr, "Failed to open file: %s\n", output);
            return 1;
        }
    }

    /* Seek to the end of the file to get length */
    fseek(file_in, 0, SEEK_END);
    length = ftell(file_in);
    fseek(file_in, 0, SEEK_SET);

    /* Allocate memory for the buffer */
    buffer = (char *)malloc(length);
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory\n");
        return 1;
    }
    memset(buffer, 0, length);

    /* Read the file into the buffer */
    fread(buffer, 1, length, file_in);
    /* Split the buffer into a linked list of lines */
    size_t line_count;
    struct line_node *head = split_buffer(buffer, &line_count);
    /* Free the buffer */
    free(buffer);

    /* Prepend param information */
    do {
        const char *header =
            "\n"
            "************************************************************************\n"
            "* CDL netlist\n"
            "************************************************************************\n";

        prepend_line(&head, header);
    }
    while (0);

    if (!no_param) {
        /* Define cdl_parameter_patterns and corresponding strings to prepend, in reverse order */
        const char *cdl_param_patterns[] = {
            "^\\.PARAM.*\n", ".PARAM",
            "^\\*\\.MEGA.*\n", "*.MEGA",
            "^\\*\\.EQUATION.*\n", "*.EQUATION",
            "^\\*\\.DIOAREA.*\n", "*.DIOAREA",
            "^\\*\\.DIOPERI.*\n", "*.DIOPERI",
            "^\\*\\.CAPVAL.*\n", "*.CAPVAL",
            "^\\*\\.RESVAL.*\n", "*.RESVAL",
            "^\\*\\.BIPOLAR.*\n", "*.BIPOLA",
        };

        /* Check and prepend strings if necessary */
        for (size_t i = 0; i < sizeof(cdl_param_patterns) / sizeof(cdl_param_patterns[0]); i += 2) {
            check_and_prepend(&head, cdl_param_patterns[i], cdl_param_patterns[i + 1]);
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
            "************************************************************************\n";
        prepend_line(&head, header);
    }
    while (0);

    if (!no_case_conversion) {
        /* Define cdl_case_patterns and their replacements */
        const char *cdl_case_patterns[] = {
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

        /* Replace substrings in the linked list */
        replace_substrings(head, cdl_case_patterns, sizeof(cdl_case_patterns) / sizeof(cdl_case_patterns[0]));
    }

    if (!no_calc_data) {
        /* Process the buffer to calculate cdl parameters */
        process_list(head);
    }

    if (soc_module) {
        /* Parse the SOC module file */
        struct module_node *modules_head = parse_soc_mod_file(soc_module);
        if (modules_head) {
            /* Insert or update PININFO lines */
            insert_pininfo(head, modules_head);
            /* Free module information */
            free_modules(modules_head);
        }
    }

    /* Join the lines into a buffer */
    buffer = join_lines(head, &length);
    /* Output buffer to file_out */
    fwrite(buffer, 1, length, file_out);
    /* Free the linked list */
    free_lines(head);
    /* Free buffer */
    free(buffer);
    /* Close input file */
    if (file_in != stdin) {
        fclose(file_in);
    }
    /* Close output file */
    if (file_out != stdout) {
        fclose(file_out);
    }

    return 0;
}
