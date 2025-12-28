#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>

#include "/home/mathieu/Coding/C/libs/dynamic_arrays.h"

#define return_defer(value) \
    do {                    \
        result = (value);   \
        goto defer;         \
    } while (0)

#define FILE_EXTENSION "td"
#define DEFAULT_TODO_PATH "/home/mathieu/todos/all.td"
static char *todo_path = DEFAULT_TODO_PATH;
#define TMP_TODO_FILENAME "/tmp/todo_tmp_s395n8a697w3b87v8r" 
#define DIVIDER_STR "----------"
#define DIVIDER_STR_WITH_NL DIVIDER_STR"\n"

#define ANSI_CLEAR_SCREEN "\x1b[2J"
#define ANSI_GO_HOME_CURSOR "\x1B[H"
void clear_screen(void)
{
    printf(ANSI_CLEAR_SCREEN); 
    printf(ANSI_GO_HOME_CURSOR); 
    fflush(stdout);
}

void printf_indent(int indent, char *fmt, ...)
{
    if (fmt == NULL) return;
    va_list ap;
    va_start(ap, fmt);
    char indent_fmt[4096] = {0};
    snprintf(indent_fmt, sizeof(indent_fmt), "%-*s%s", indent, "", fmt);
    char buffer[4096] = {0};
    vsnprintf(buffer, sizeof(buffer), indent_fmt, ap);
    va_end(ap);
    char *line = strtok(buffer, "\n");
    if (line) {
        printf("%s\n", line);
        line = strtok(NULL, "\n");
        printf_indent(indent, line);
    }
}

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

typedef enum
{
    CMD_SHOW,
    CMD_ADD,
    CMD_COMPLETE,
    CMD_DELETE,
    CMD_MODIFY,
    CMD_HELP,
    CMD_UNKNOWN,
    CMDS_COUNT
} Command;

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} AoS;
static AoS args = {0};
static AoS flags = {0};
static AoS tags = {0};

typedef struct
{
    size_t *items;
    size_t count;
    size_t capacity;
} size_ts;

void shift_args(void)
{
    args.items++;
    args.count--;
}

typedef struct
{
    char *body;
    size_t priority;
    bool completed;
    char *tag;
} Todo;

static Todo template_todo = {0};

void todo_fprint(Todo t, FILE *sink)
{
    fprintf(sink, "[");
    if (t.completed) fprintf(sink, "x");
    fprintf(sink, "] ");
    if (t.priority) fprintf(sink, "%zu ", t.priority);
    if (t.tag) fprintf(sink, "@%s", t.tag);
    fprintf(sink, "\n");

    fprintf(sink, DIVIDER_STR_WITH_NL);
    fprintf(sink, "%s", t.body ? t.body : "\n");
    fprintf(sink, DIVIDER_STR_WITH_NL);
    fprintf(sink, "\n");
}

static inline void todo_print(Todo t) { todo_fprint(t, stdout); }

static int compare_todos_descending_priority(const void *p1, const void *p2)
{
    const Todo *t1 = (Todo *)p1;
    const Todo *t2 = (Todo *)p2;
    return t1->priority < t2->priority;
}

typedef struct
{
    Todo *items;
    size_t count;
    size_t capacity;
} Todos;

char *read_file(char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("ERROR: Could not open file at `%s`\n", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    char *content = malloc(len + 1);
    fread(content, len, 1, f);
    fclose(f);
    content[len] = '\0';
    return content;
}

void skip_line(char **str)
{
    if (!str || !*str) return;
    while (**str && **str != '\n')
        *str += 1;
    if (**str) *str += 1;
}

void trim_left(char **str)
{
    if (!str || !*str) return;
    while (isblank(**str)) (*str)++;
}

bool advance(char **str)
{
    trim_left(str);
    if (**str == '\0') {
        printf("ERROR: reached end of file while parsing todo\n");
        return false;
    }
    return true;
}

bool parse_todo(char **content, Todo *todo)
{
    if (!content || !*content) return false;

    bool result = true;

    *todo = (Todo){0};
    char *it = *content;

    if (!advance(&it)) return_defer(false);
    if (*it != '[') {
        printf("ERROR: expecting '[' but got '%c'\n", *it);
        return_defer(false);
    } else it++;

    if (!advance(&it)) return_defer(false);
    if (*it == 'x') {
        it++;
        todo->completed = true;
        if (!advance(&it)) return_defer(false);
    }
    if (*it != ']') {
        printf("ERROR: expecting ']' but got '%c'\n", *it);
        return_defer(false);
    } else it++;

    if (!advance(&it)) return_defer(false);
    if (*it != '\n' && !isdigit(*it) && *it != '@') {
        printf("ERROR: expecting the priority number, a tag or nothing, but got '%c'\n", *it);
        return_defer(false);
    }
    if (isdigit(*it)) {
        char *end;
        long priority = strtol(it, &end, 10);
        if (priority <= 0) {
            *end = '\0';
            printf("ERROR: priority should be a positive integer greater than zero\n");
            printf("NOTE: you inserted `%s`\n", it);
            return_defer(false);
        }
        todo->priority = priority;
        it = end;
    }

    if (!advance(&it)) return_defer(false);
    if (*it == '@') {
        it++;
        char *end_tag = it;
        // TODO: also check if *it
        while (!isspace(*end_tag)) end_tag++;
        if (end_tag == it) {
            printf("ERROR: empty tag\n");
            return_defer(false);
        }
        ptrdiff_t tag_len = end_tag - it;
        todo->tag = malloc(tag_len+1);
        strncpy(todo->tag, it, tag_len);
        todo->tag[tag_len] = '\0';
        it = end_tag;
    }

    if (!advance(&it)) return_defer(false);
    if (*it != '\n') {
        printf("ERROR: expecting new line but got '%c'\n", *it);
        return_defer(false);
    } else it++;

    if (!advance(&it)) return_defer(false);
    if (strncmp(it, DIVIDER_STR_WITH_NL, strlen(DIVIDER_STR_WITH_NL)) != 0) {
        printf("ERROR: todo content should begin with `" DIVIDER_STR "`\n");
        return_defer(false);
    } else it += strlen(DIVIDER_STR_WITH_NL);
    if (!advance(&it)) return_defer(false);

    char *begin = it;
    size_t todolen = 0;
    bool done = false;
    while (*it) {
        if (strncmp(it, DIVIDER_STR_WITH_NL, strlen(DIVIDER_STR_WITH_NL)) == 0) {
            it += strlen(DIVIDER_STR_WITH_NL);
            done = true;
            break;
        }
        todolen++;
        it++;
    }
    if (!done) {
        printf("ERROR: todo content should end with `" DIVIDER_STR "`\n");
        return_defer(false);
    }

    todo->body = malloc(todolen+1);
    strncpy(todo->body, begin, todolen);
    todo->body[todolen] = '\0';

defer:
    *content = it;
    return result;
}

bool parse_todos(char *content, Todos *todos)
{
    da_clear(todos);

    char *it = content;
    bool res;
    Todo todo;
    while (isspace(*it)) it++;
    while (*it) {
        res = parse_todo(&it, &todo);
        if (!res && *it) return false;
        da_push(todos, todo);
        while (isspace(*it)) it++;
    }
    return true;
}

static char *program_name = NULL;
void usage(void)
{
    // TODO: commands and flags
    printf("usage: %s\n", program_name);
}

void info(void)
{
    printf("todo is a cli tool to manage todos\n");
    usage();
}

static_assert(CMDS_COUNT == 7, "Get all commands to cstr in command_to_cstr");
char *command_to_cstr(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "show";
    case CMD_ADD:      return "add";
    case CMD_COMPLETE: return "complete";
    case CMD_DELETE:   return "delete";
    case CMD_MODIFY:   return "modify";
    case CMD_HELP:     return "help";
    case CMD_UNKNOWN:  return "unknown";
    case CMDS_COUNT:
    default:
        printf("Unreachable command in command_to_cstr\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 7, "Get all commands info in info_of");
const char *info_of(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "print pending todos in `path` to stdout";
    case CMD_ADD:      return "TODO: info add";
    case CMD_COMPLETE: return "TODO: info complete";
    case CMD_DELETE:   return "TODO: info delete";
    case CMD_MODIFY:   return "TODO: info modify";
    case CMD_HELP:     return "TODO: info help";
    case CMD_UNKNOWN:  return "TODO: info unknown";
    case CMDS_COUNT:
    default:
        printf("Unreachable command in info_of\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 7, "Get all commands info in usage_of");
const char *usage_of(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "todo [show] [path]";
    case CMD_ADD:      return "TODO: usage add";
    case CMD_COMPLETE: return "TODO: usage complete";
    case CMD_DELETE:   return "TODO: usage delete";
    case CMD_MODIFY:   return "TODO: usage modify";
    case CMD_HELP:     return "TODO: usage help";
    case CMD_UNKNOWN:  return "TODO: usage unknown";
    case CMDS_COUNT:
    default:
        printf("Unreachable command in usage_of\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 7, "Get all commands info in flags_of");
const char *flags_of(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "-all                also print completed todos\n";
    case CMD_ADD:      return "TODO: flags add";
    case CMD_COMPLETE: return "TODO: flags complete";
    case CMD_DELETE:   return "TODO: flags delete";
    case CMD_MODIFY:   return "TODO: flags modify";
    case CMD_HELP:     return "TODO: flags help";
    case CMD_UNKNOWN:  return "TODO: flags unknown";
    case CMDS_COUNT:
    default:
        printf("Unreachable command in flags_of\n");
        abort();
    }
}

void help_of(Command cmd)
{
    if (cmd == CMD_UNKNOWN) {
        printf("ERROR: unknown command\n");
        return;
    } else if (cmd < 0 || cmd >= CMDS_COUNT) {
        printf("Unreachable command in help_of\n");
        abort();
    }

    printf("Command %s:\n", command_to_cstr(cmd));

    printf_indent(4, "info:\n");
    printf_indent(8, "%s\n", info_of(cmd));

    printf_indent(4, "usage:\n");
    printf_indent(8, "%s\n", usage_of(cmd));

    printf_indent(4, "flags:\n");
    printf_indent(8, "%s\n", flags_of(cmd));
}

static_assert(CMDS_COUNT == 7, "Get all commands from string in get_command");
Command get_command(char *str)
{
         if (streq(str, "show"))                          return CMD_SHOW;
    else if (streq(str, "add"))                           return CMD_ADD;
    else if (streq(str, "complete") || streq(str, "com")) return CMD_COMPLETE;
    else if (streq(str, "delete")   || streq(str, "del")) return CMD_DELETE;
    else if (streq(str, "modify")   || streq(str, "mod")) return CMD_MODIFY;
    else if (streq(str, "help"))                          return CMD_HELP;
    else                                                  return CMD_UNKNOWN;
}

bool command_help(void)
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command help\n", flag);
        printf("TODO: print command usage\n");
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 1) {
        printf("ERROR: too many arguments for command help\n");
        help_of(CMD_HELP);
        return false;
    } else if (args.count == 0) {
        help_of(CMD_HELP);
    } else {
        Command cmd = get_command(args.items[0]);
        if (cmd == CMD_UNKNOWN) {
            printf("ERROR: unknown command  `%s`\n", args.items[0]);
            return false;
        }
        help_of(cmd);
    }

    return true;
}

bool check_valid_todo_path(char *path)
{
    if (!path) return false;

    FILE *f = fopen(path, "r");
    if (!f) {
        printf("ERROR: could not open file at `%s`\n", path);
        return false;
    } else fclose(f);

    char *point = strrchr(path, '.');
    bool got_extension = !!point;
    point++;
    got_extension |= streq(point, FILE_EXTENSION);
    if (!got_extension) {
        printf("ERROR: file `%s` has no extension `%s`\n", path, FILE_EXTENSION);
        return false; 
    }

    return true;
}

bool get_all_todos(Todos *todos)
{
    da_clear(todos);
    char *content = read_file(todo_path);
    if (!content) return false;
    if (!parse_todos(content, todos)) return false;
    if (todos->count == 0) {
        printf("INFO: no todos found at `%s`\n", todo_path);
        printf("NOTE: to add one use the command: %s add %s\n",
                program_name,
                streq(todo_path, DEFAULT_TODO_PATH) ? "" : todo_path);
        return false;
    }
    qsort(todos->items, todos->count, sizeof(todos->items[0]), compare_todos_descending_priority);
    return true;
}

bool todo_has_selected_tag(Todo t)
{
    if (tags.count == 0) return true;
    if (!t.tag) return false;
    for (size_t j = 0; j < tags.count; j++) {
        if (streq(t.tag, tags.items[j])) {
            return true;
        }
    }
    return false;
}

#define ALL true
size_ts get_todo_indexes(Todos todos, bool all)
{
    size_ts indexes = {0};
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if ((!t.completed || all) && todo_has_selected_tag(t))
            da_push(&indexes, i);
    }
    return indexes;
}

bool command_show(void)
{
    bool show_all = false;

    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "a") || streq(flag, "all")) show_all = true;
        else {
            printf("ERROR: unknown flag `%s` for command show\n", flag);
            printf("TODO: print command usage\n");
            flag_error = true;
        }
    }
    if (flag_error) return false;

    if (args.count > 0) {
        printf("ERROR: too many arguments for command show\n");
        help_of(CMD_SHOW);
        return false;
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    size_ts indexes = get_todo_indexes(todos, show_all);
    clear_screen();
    for (size_t i = 0; i < indexes.count; i++) {
        Todo t = todos.items[indexes.items[i]];
        todo_print(t);
    }
    return true;
}

bool add_todo_to_file(Todo todo)
{
    FILE *f = fopen(todo_path, "a");
    if (!f) {
        printf("ERROR: could not open todo file at `%s`\n", todo_path);
        return false;
    }
    todo_fprint(todo, f);
    fclose(f);
    printf("INFO: todo added at `%s`\n", todo_path);
    return true;
}

bool add_or_modify_todo(Todo *todo)
{
    bool modify = !!todo;
    FILE *f = fopen(TMP_TODO_FILENAME, "w");
    if (!f) {
        printf("ERROR: Could not create temporary file in /tmp\n"); 
        return false;
    } else {
        if (modify) {
            todo_fprint(*todo, f);
        } else {
            if (tags.count == 1) {
                template_todo.tag = tags.items[0];
            } else if (tags.count > 1) {
                printf("ERROR: at the moment only one tag at a time is supported\n");
                printf("NOTE: tags: ");
                for (size_t i = 0; i < tags.count; i++) {
                    printf("%s", tags.items[i]);
                    if (i != tags.count-1)
                        printf(",");
                    printf(" ");
                }
                printf("\n");
                return false;
            }
            todo_fprint(template_todo, f);
        }
        fprintf(f, "Any text inserted after the second line will be ignored\n");
        fclose(f);
    }
    char cmd[256] = {0};
    sprintf(cmd, "$EDITOR %s", TMP_TODO_FILENAME);
    system(cmd);
    char *content = read_file(TMP_TODO_FILENAME);
    remove(TMP_TODO_FILENAME);
    if (!content) {
        printf("ERROR: could not read temporary todo file at `%s`\n", TMP_TODO_FILENAME);
        return false;
    }

    Todo new_todo = {0};
    if (!parse_todo(&content, &new_todo)) return false;

    if (strlen(new_todo.body) == 0) {
        printf("INFO: empty todos won't be added\n");
        return true;
    }
    char *tmp = new_todo.body;
    while (isspace(*tmp)) tmp++;
    if (!*tmp) {
        printf("INFO: todos with empty lines or spaces only won't be added\n");
        return true;
    }

    if (!modify) return add_todo_to_file(new_todo);
    else {
        *todo = new_todo;
        return true;
    }
}

static inline bool add_todo(void) { return add_or_modify_todo(NULL); }
static inline bool modify_todo(Todo *todo) { return add_or_modify_todo(todo); }

bool command_add(void)
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command add\n", flag);
        printf("TODO: print command usage\n");
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 1) {
        printf("ERROR: too many arguments for command add\n");
        help_of(CMD_ADD);
        return false;
    } else if (args.count == 1) {
        char *arg = args.items[0];
        if (isdigit(*arg)) {
            char *end;
            long priority = strtol(arg, &end, 10);
            if (priority <= 0 || *end != '\0') {
                printf("ERROR: priority should be a positive integer greater than zero\n");
                printf("NOTE: you inserted `%s`\n", arg);
                return false;
            }
            template_todo.priority = priority;
        } else {
            printf("ERROR: priority should be a positive integer greater than zero\n");
            printf("NOTE: you inserted `%s`\n", arg);
            return false;
        }
    }

    return add_todo();
}

bool get_todo_index_from_user(int *index, size_t count, char *action)
{
    printf("Insert the number of the todo that you want to %s (or type `quit`/'q')\n", action);
    while (true) {
        printf("> ");
        char buffer[16] = {0};
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strlen(buffer)-1] = '\0';
        if (strneq(buffer, "quit", 4) || strneq(buffer, "q", 1))
            return false;
        char *end;
        *index = strtol(buffer, &end, 10);
        if (buffer != end
            && *end == '\0'
            && *index >= 0
            && (size_t)*index < count) break;
        else {
            printf("ERROR: `%s` is not a valid number.\n", buffer);
        }
    }
    return true;
}

bool save_todos_to_file(Todos todos)
{
    FILE *f = fopen(todo_path, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", todo_path);
        return false;
    }
    for (size_t i = 0; i < todos.count; i++) {
        todo_fprint(todos.items[i], f);
    }
    fclose(f);
    return true;
}

bool command_modify(void)
{
    bool modify_all = false;

    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "all") || streq(flag, "a")) {
            modify_all = true;
        } else {
            printf("ERROR: unknown flag `%s` for command modify\n", flag);
            printf("TODO: print command usage\n");
            flag_error = true;
        }
    }
    if (flag_error) return false;

    if (args.count > 0) {
        printf("ERROR: too many arguments for command modify\n");
        help_of(CMD_MODIFY);
        return false;
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    size_ts indexes = get_todo_indexes(todos, modify_all);
    if (indexes.count == 0) {
        printf("There are no todos to modify\n");
        return true;
    }
    clear_screen();
    for (size_t i = 0; i < indexes.count; i++) {
        Todo t = todos.items[indexes.items[i]];
        printf("%zu.\n", i);
        todo_print(t);
    }

    int index;
    if (!get_todo_index_from_user(&index, indexes.count, "modify")) return false;
    if (!modify_todo(&todos.items[indexes.items[index]])) return false;
    if (!save_todos_to_file(todos)) return false;

    printf("INFO: todo %d has been modified\n", index);
    return true;
}

bool ask_user_confirmation(void)
{
    char buffer[16] = {0};
    ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));
    buffer[nread-1] = '\0';
    if (tolower(*buffer) != 'y' && !strneq(buffer, "yes", 3))
        return false;
    else return true;
}

bool command_complete(void)
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command complete\n", flag);
        printf("TODO: print command usage\n");
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 0) {
        printf("ERROR: too many arguments for command complete\n");
        help_of(CMD_COMPLETE);
        return false;
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    while (true) {
        size_ts indexes = get_todo_indexes(todos, !ALL);
        if (indexes.count == 0) {
            printf("All todos are completed\n");
            return true;
        }
        clear_screen();
        for (size_t i = 0; i < indexes.count; i++) {
            Todo t = todos.items[indexes.items[i]];
            printf("%zu.\n", i);
            todo_print(t);
        }

        int index;
        if (!get_todo_index_from_user(&index, indexes.count, "complete")) return false;
        todos.items[indexes.items[index]].completed = true;
        if (!save_todos_to_file(todos)) return false;

        printf("INFO: todo %d has been marked as completed\n", index);

        if (indexes.count-1 == 0) {
            printf("\nThere are no todo left to complete\n");
            return true;
        }

        printf("\nContinue completing? y/N\n");
        if (!ask_user_confirmation()) return true;
    }
    printf("Unreachable in command_complete\n");
    abort();
}

bool delete_all_todos(void)
{
    FILE *f = fopen(todo_path, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", todo_path);
        return false;
    }
    fclose(f);
    printf("Deleted all todos\n");
    return true;
}

bool delete_completed_todos(void)
{
    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    size_t i = 0;
    Todo t;
    size_t before = todos.count;
    while (i < todos.count) {
        t = todos.items[i];
        if (t.completed && todo_has_selected_tag(t))
            da_remove(&todos, i);
        else i++;
    }
    size_t after = todos.count;

    if (!save_todos_to_file(todos)) return false;

    printf("Deleted %zu todos\n", before - after + 1);
    return true;
}

bool command_delete(void)
{
    bool deleted_all = false;
    bool select_from_all = false;
    bool deleted_completed = false;

    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "A") || streq(flag, "ALL")) deleted_all = true;
        else if (streq(flag, "c") || streq(flag, "completed")) deleted_completed = true;
        else if (streq(flag, "a") || streq(flag, "all")) select_from_all = true;
        else {
            printf("ERROR: unknown flag `%s` for command delete\n", flag);
            printf("TODO: print command usage\n");
            flag_error = true;
        }
    }
    if (deleted_all && deleted_completed) {
        printf("ERROR: conflicting flags -all and -completed\n");
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 0) {
        printf("ERROR: too many arguments for command delete\n");
        help_of(CMD_DELETE);
        return false;
    }

    if (deleted_all) {
        printf("Are you sure you want to delete ALL todos at `%s`? y/N\n", todo_path);
        if (ask_user_confirmation())
            return delete_all_todos();
        else return true;
    }

    if (deleted_completed) {
        printf("Are you sure you want to delete the completed todos at `%s`? y/N\n", todo_path);
        if (ask_user_confirmation())
            return delete_completed_todos();
        else return true;
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    if (todos.count == 0) {
        printf("There are no todos to delete\n");
        return true;
    }

    while (true) {
        size_ts indexes = get_todo_indexes(todos, select_from_all);
        if (indexes.count == 0) {
            printf("There are no todos to delete\n");
            return true;
        }
        clear_screen();
        for (size_t i = 0; i < indexes.count; i++) {
            Todo t = todos.items[indexes.items[i]];
            printf("%zu.\n", i);
            todo_print(t);
        }

        int index;
        if (!get_todo_index_from_user(&index, indexes.count, "delete")) return false;

        da_remove(&todos, indexes.items[index]);

        if (!save_todos_to_file(todos)) return false;

        printf("INFO: todo %d has been deleted\n", index);

        if (indexes.count-1 == 0) {
            printf("\nThere are no todo left to delete\n");
            return true;
        }

        printf("\nContinue deleting? y/N\n");
        if (!ask_user_confirmation()) return true;
    }
    printf("Unreachable in command_delete\n");
    abort();
}

int main(int argc, char **argv)
{
    program_name = argv[0];

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (*arg == '-') {
            arg++;
            char *flag = arg;
            if (!*flag) {
                printf("ERROR: flag without name (lonely dash)\n");
                return 1;
            }
            if (streq(flag, "path") || streq(flag, "p")) {
                if (i+1 >= argc) {
                    printf("ERROR: expected path after flag -path, but got nothing\n");
                    return 1;
                }
                char *path = argv[i+i];
                if (!check_valid_todo_path(path)) return 1;
                else todo_path = path;
            } else {
                da_push(&flags, flag);
            }
        } else if (*arg == '@') {
            arg++;
            if (!*arg) {
                printf("ERROR: tag without name (lonely at)\n");
                return 1;
            }
            da_push(&tags, arg);
        } else {
            da_push(&args, arg);
        }
    }

    Command command = CMD_SHOW;
    if (args.count > 0) {
        command = get_command(args.items[0]);
        if (command != CMD_UNKNOWN) shift_args();
    }

    bool result = true;

    static_assert(CMDS_COUNT == 7, "Switch all commands in main");
    switch (command)
    {
        case CMD_SHOW:     result = command_show();     break;
        case CMD_ADD:      result = command_add();      break; 
        case CMD_COMPLETE: result = command_complete(); break;
        case CMD_DELETE:   result = command_delete();   break;
        case CMD_MODIFY:   result = command_modify();   break;
        case CMD_HELP:     result = command_help();     break;
        case CMD_UNKNOWN: {
            printf("ERROR: unknown command `%s`\n", args.items[0]);
        } break;
        default:
            printf("Unreachable switching command in main\n");
            abort();
    }

    return result ? 0 : 1;
}
