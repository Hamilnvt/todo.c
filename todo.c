// TODO:
// - add tags to group todos
// - maybe remove command CMD_IS_PATH
// - completed can be just the x and uncompleted has nothing
// - what if I wanted to uncomplete a todo?
//   > introduce -all flag for complete/delete/modify commands
//     - "completing" a completed todo makes it uncompleted (maybe with a warning and an input confirmation)
// - idea for a command: comments parser to detect and create todos from a file

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

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

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }
static inline bool strneq(const char *s1, const char *s2, size_t n) { return strncmp(s1, s2, n) == 0; }

typedef enum
{
    CMD_SHOW,
    CMD_IS_PATH,
    CMD_ADD,
    CMD_COMPLETE,
    CMD_DELETE,
    CMD_MODIFY,
    CMD_HELP,
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

void shift_args()
{
    args.items++;
    args.count--;
}

typedef struct
{
    char *body;
    size_t priority;
    bool completed;
} Todo;

void todo_print(Todo t, FILE *sink)
{
    fprintf(sink, "[");
    if (t.completed) fprintf(sink, "x");
    fprintf(sink, "] ");
    if (t.priority) fprintf(sink, "%zu", t.priority);
    fprintf(sink, "\n");

    fprintf(sink, DIVIDER_STR_WITH_NL);
    fprintf(sink, "%s", t.body ? t.body : "\n");
    fprintf(sink, DIVIDER_STR_WITH_NL);
    fprintf(sink, "\n");
}

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

static inline void trim_left(char **str)
{
    if (!str || !*str) return;
    while (isblank(**str)) (*str)++;
}

bool parse_todo(char **content, Todo *todo)
{
    if (!content || !*content) return false;

    bool result = true;

    *todo = (Todo){0};
    char *it = *content;

    trim_left(&it);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return_defer(false);
    }
    if (*it != '[') {
        printf("ERROR: expecting '[' but got '%c'\n", *it);
        return_defer(false);
    } else it++;

    trim_left(&it);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return_defer(false);
    }
    if (*it == 'x') {
        it++;
        todo->completed = true;
        trim_left(&it);
        if (!*it) {
            printf("ERROR: reached end of file while parsing todo\n");
            return_defer(false);
        }
    }
    if (*it != ']') {
        printf("ERROR: expecting ']' but got '%c'\n", *it);
        return_defer(false);
    } else it++;

    trim_left(&it);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return_defer(false);
    }
    if (*it != '\n' && !isdigit(*it)) {
        printf("ERROR: expecting the priority number or nothing, but got '%c'\n", *it);
        return_defer(false);
    }
    if (isdigit(*it)) {
        char *end = it;
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

    trim_left(&it);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return_defer(false);
    }
    if (*it == '@') {
        printf("ERROR: tags are not yet supported\n");
        return_defer(false);
    }
    //printf("`%s`", it);
    if (*it != '\n') {
        printf("ERROR: expecting new line but got '%c'\n", *it);
        return_defer(false);
    } else it++;

    trim_left(&it);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return_defer(false);
    }
    if (strncmp(it, DIVIDER_STR_WITH_NL, strlen(DIVIDER_STR_WITH_NL)) != 0) {
        printf("ERROR: todo content should begin with `" DIVIDER_STR "`\n");
        return_defer(false);
    } else it += strlen(DIVIDER_STR_WITH_NL);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return_defer(false);
    }

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
void usage()
{
    // TODO: commands and flags
    printf("usage: %s\n", program_name);
}

void info()
{
    printf("todo is a cli tool to manage todos\n");
    usage();
}

void command_usage(Command cmd)
{
    static_assert(CMDS_COUNT == 7, "Switch all commands in command_usage");
    switch (cmd)
    {
        case CMD_SHOW:
        {
            printf("Command show:\n");
            printf("    info:\n        print pending todos in `path` to stdout\n");
            printf("    usage:\n        todo [show] [path]\n");
            printf("    flags:\n        -all                also print completed todos\n");
        } break;
        case CMD_IS_PATH:
        {
            printf("TODO: command_help for command that is a path\n");
        } break;
        case CMD_ADD:
        {
            printf("TODO: command_help for command add\n");
        } break;
        case CMD_COMPLETE:
        {
            printf("TODO: command_help for command complete\n");
        } break;
        case CMD_DELETE:
        {
            printf("TODO: command_help for command delete\n");
        } break;
        case CMD_MODIFY:
        {
            printf("TODO: command_help for command modify\n");
        } break;
        case CMD_HELP:
        {
            printf("TODO: command_help for command help\n");
        } break;
        default:
            printf("Unreachable switching command in command_help\n");
            abort();
    }
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
    else                                                  return CMD_IS_PATH;
}

bool command_help()
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
        // TODO report error too many arguments 
        return false;
    } else if (args.count == 0) {
        command_usage(CMD_HELP);
    } else {
        Command cmd = get_command(args.items[0]);
        if (cmd == CMD_IS_PATH) {
            printf("ERROR: unknown command  `%s`\n", args.items[0]);
            return false;
        }
        command_usage(cmd);
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
        printf("NOTE: you can add one with this command: %s add\n", program_name);
        return false;
    }
    qsort(todos->items, todos->count, sizeof(todos->items[0]), compare_todos_descending_priority);
    return true;
}

bool command_show()
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

    if (args.count > 1) {
        // TODO report error too many arguments 
        return false;
    } else if (args.count == 1) {
        if (!check_valid_todo_path(args.items[0])) return false;
        else {
            todo_path = args.items[0];
            shift_args();
        }
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (!t.completed || show_all)
            todo_print(t, stdout);
    }
    return true;
}

// TODO: make the user choose the file
bool add_todo_to_file(Todo todo)
{
    FILE *f = fopen(todo_path, "a");
    if (!f) {
        printf("ERROR: could not open todo file at `%s`\n", todo_path);
        return false;
    }
    todo_print(todo, f);
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
            todo_print(*todo, f);
        } else {
            Todo template_todo = {0};
            todo_print(template_todo, f);
        }
        fprintf(f, "Any text inserted after the second line will be ignored\n");
        // TODO: right now if you modify a completed todo it will be marked as not completed
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
        // TODO report error too many arguments
        return false;
    } else if (args.count == 1) {
        if (!check_valid_todo_path(args.items[0])) return false;
        else todo_path = args.items[0];
    }

    return add_or_modify_todo(NULL);
}
static inline bool modify_todo(Todo *todo) { return add_or_modify_todo(todo); }

bool command_modify()
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command modify\n", flag);
        printf("TODO: print command usage\n");
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 1) {
        // TODO report error too many arguments
        return false;
    } else if (args.count == 1) {
        if (!check_valid_todo_path(args.items[0])) return false;
        else todo_path = args.items[0];
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    int n = 1;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (t.completed) continue;
        printf("%d.\n", n);
        todo_print(t, stdout);
        n++;
    }

    int index = 0;
    printf("Insert the number of the todo that you want to modify (or type `quit`/'q')\n");
    while (index <= 0 || (size_t)index >= todos.count) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (strneq(buffer, "quit", 4) || strneq(buffer, "q", 1))
            return false;
        index = atoi(buffer);
        printf("index: %d\n", index);
        if (index > 0) break;
        else printf("ERROR: `%s` is not a valid number, check again.\n", buffer);
    }

    n = 1;
    for (size_t i = 0; i < todos.count; i++) {
        Todo *t = &todos.items[i];
        if (t->completed) continue;
        if (n == index) {
            if (!modify_todo(t)) return false;
            break;
        } else n++;
    }
    FILE *f = fopen(todo_path, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", todo_path);
        return false;
    }
    for (size_t i = 0; i < todos.count; i++) {
        todo_print(todos.items[i], f);
    }
    fclose(f);
    printf("INFO: todo %d has been modified\n", n);
    return true;
}

bool command_complete()
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command complete\n", flag);
        printf("TODO: print command usage\n");
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 1) {
        // TODO report error too many arguments
        return false;
    } else if (args.count == 1) {
        if (!check_valid_todo_path(args.items[0])) return false;
        else todo_path = args.items[0];
    }

    char buffer[64] = {0};
    Todos todos = {0};
    while (true) {
        if (!get_all_todos(&todos)) return false;
        int n = 0;
        for (size_t i = 0; i < todos.count; i++) {
            Todo t = todos.items[i];
            if (t.completed) continue;
            n++;
            printf("%d.\n", n);
            todo_print(t, stdout);
        }

        if (n == 0) {
            printf("All todos are completed\n");
            return true;
        }

        int index = 0;
        printf("Insert the number of the todo that you want to complete (or type `quit`/'q')\n");
        while (true) {
            ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));; 
            buffer[nread-1] = '\0';
            if (strneq(buffer, "quit", 4) || tolower(*buffer) == 'q')
                return true;
            index = atoi(buffer);
            if (index <= 0 || index > n)
                printf("ERROR: `%s` is not a valid number, check again.\n", buffer);
            else break;
        }

        n = 1;
        for (size_t i = 0; i < todos.count; i++) {
            Todo *t = &todos.items[i];
            if (t->completed) continue;
            if (n == index) {
                t->completed = true;
                break;
            } else n++;
        }

        FILE *f = fopen(todo_path, "w");
        if (!f) {
            printf("ERROR: could not open `%s`\n", todo_path);
            return false;
        }

        for (size_t i = 0; i < todos.count; i++) {
            todo_print(todos.items[i], f);
        }
        fclose(f);
        printf("INFO: todo %d has been marked as completed\n", index);

        printf("\nContinue completing? y/N\n");
        ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[nread-1] = '\0';
        if (tolower(*buffer) != 'y' && !strneq(buffer, "yes", 3))
            return true;
    }
    printf("Unreachable in command_complete\n");
    abort();
}

bool delete_completed_todos()
{
    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    size_t i = 0;
    Todo t;
    while (i < todos.count) {
        t = todos.items[i];
        if (t.completed) da_remove(&todos, i, t);
        else i++;
    }

    FILE *f = fopen(todo_path, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", todo_path);
        return false;
    }
    for (size_t i = 0; i < todos.count; i++) {
        todo_print(todos.items[i], f);
    }
    fclose(f);

    return true;
}

bool command_delete()
{
    bool deleted_completed = false;

    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "c") || streq(flag, "completed")) deleted_completed = true;    
        else {
            printf("ERROR: unknown flag `%s` for command delete\n", flag);
            printf("TODO: print command usage\n");
            flag_error = true;
        }
    }
    if (flag_error) return false;

    if (args.count > 1) {
        // TODO report error too many arguments
        return false;
    } else if (args.count == 1) {
        if (!check_valid_todo_path(args.items[0])) return false;
        else todo_path = args.items[0];
    }

    if (deleted_completed) return delete_completed_todos();

    char buffer[64] = {0};
    Todos todos = {0};
    while (true) {
        if (!get_all_todos(&todos)) return false;
        for (size_t i = 0; i < todos.count; i++) {
            Todo t = todos.items[i];
            printf("%zu.\n", i+1);
            todo_print(t, stdout);
        }

        int index = 0;
        printf("Insert the number of the todo that you want to delete (or type `quit`/'q')\n");
        while (true) {
            ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));; 
            buffer[nread-1] = '\0';
            if (strneq(buffer, "quit", 4) || tolower(*buffer) == 'q')
                return true;
            index = atoi(buffer);
            if (index <= 0 || (size_t)index > todos.count)
                printf("ERROR: `%s` is not a valid number, check again.\n", buffer);
            else break;
        }

        Todo t; (void)t;
        da_remove(&todos, index-1, t);
        FILE *f = fopen(todo_path, "w");
        if (!f) {
            printf("ERROR: could not open `%s`\n", todo_path);
            return false;
        }
        for (size_t i = 0; i < todos.count; i++) {
            todo_print(todos.items[i], f);
        }
        fclose(f);
        printf("INFO: todo %d has been deleted\n", index);

        printf("\nContinue deleting? y/N\n");
        ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[nread-1] = '\0';
        if (tolower(*buffer) != 'y' && !strneq(buffer, "yes", 3))
            return true;
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
            if (!*arg) {
                printf("ERROR: flag without name (lonely dash)\n");
                return 1;
            }
            da_push(&flags, arg);
        } else {
            da_push(&args, arg);
        }
    }

    Command command = args.count == 0 ? CMD_SHOW : get_command(args.items[0]);
    if (command != CMD_SHOW && command != CMD_IS_PATH) {
        shift_args();
    }

    bool result = true;
    static_assert(CMDS_COUNT == 7, "Switch all commands in main");
    switch (command)
    {
        case CMD_SHOW:
        case CMD_IS_PATH:  result = command_show();     break;
        case CMD_ADD:      result = command_add();      break; 
        case CMD_COMPLETE: result = command_complete(); break;
        case CMD_DELETE:   result = command_delete();   break;
        case CMD_MODIFY:   result = command_modify();   break;
        case CMD_HELP:     result = command_help();     break;
        default:
            printf("Unreachable switching command in main\n");
            abort();
    }

    if (result) return 0;
    else        return 1;
}
