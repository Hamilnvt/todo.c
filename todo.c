// TODO:
// - add tags to group todos
// - right now some todos are removed and I don't know why
// - remove/complete todo should be in a bigger loop
// - bound check complete/remove/modify index

#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

#include "/home/mathieu/Coding/C/libs/dynamic_arrays.h"

#define DEFAULT_TODO_PATH "/home/mathieu/todos/all.td"
static char *todo_path = DEFAULT_TODO_PATH;
#define TMP_TODO_FILENAME "/tmp/todo_tmp_s395n8a697w3b87v8r" 
#define PRIORITY_STR "priority: "
#define COMPLETED_STR "completed"
#define DIVIDER_STR "----------"
#define DIVIDER_STR_WITH_NL DIVIDER_STR"\n"
static bool flag_all = false;

static inline bool streq(const char *s1, const char *s2) { return strcmp(s1, s2) == 0; }

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} AoS;
static AoS args = {0};
static AoS flags = {0};

typedef struct
{
    char *body;
    size_t priority;
    bool completed;
} Todo;

void todo_print(Todo t, FILE *sink)
{
    if (t.completed) fprintf(sink, "completed");
    fprintf(sink, PRIORITY_STR "%zu\n", t.priority);
    fprintf(sink, DIVIDER_STR_WITH_NL);
    fprintf(sink, "%s", t.body);
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

void skip_line(char **text)
{
    while (**text && **text != '\n')
        *text += 1;
    if (**text) *text += 1;
}

bool parse_todo(char **content, Todo *todo)
{
    char *it = *content;
    if (!*it) return false;
    while (isspace(*it)) it++;
    if (!*it) return false;
    if (strncmp(it, COMPLETED_STR, strlen(COMPLETED_STR)) == 0) {
        *todo = (Todo){0};    
        todo->completed = true;
        it += strlen(COMPLETED_STR);
        while (*it != '\n') {
            if (!isspace(*it)) {
                printf("ERROR: unexpected character `%c` on the completed line, did you touch something in this file? Because you shouldn't\n", *it);
                printf("NOTE: Use the add command.\n");
                return false;
            }
            it++;
        }
    }
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return false;
    }
    if (strncmp(it, PRIORITY_STR, strlen(PRIORITY_STR)) == 0) {
        if (!todo->completed) *todo = (Todo){0};
        it += strlen(PRIORITY_STR);
        char *end = it;
        long priority = strtol(it, &end, 10);
        if (priority <= 0) {
            *end = '\0';
            printf("ERROR: priority should be a positive integer greater than zero, you wrote `%s`\n", it);
            return false;
        }
        todo->priority = priority;
        it = end;
        while (*it != '\n') {
            if (!isspace(*it)) {
                printf("ERROR: unexpected character `%c` on the priority line, did you touch something in this file? Because you shouldn't\n", *it);
                printf("NOTE: Insert a newline and start writing your todo\n");
                return false;
            }
            it++;
        }
        it++;
    }
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return false;
    }
    if (strncmp(it, DIVIDER_STR_WITH_NL, strlen(DIVIDER_STR_WITH_NL)) != 0) {
        printf("ERROR: todo content should begin with `" DIVIDER_STR "`\n");
        return false;
    } else it += strlen(DIVIDER_STR_WITH_NL);
    if (!*it) {
        printf("ERROR: reached end of file while parsing todo\n");
        return false;
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
        return false;
    }
    *content = it;
    todo->body = malloc(todolen+1);
    strncpy(todo->body, begin, todolen);
    todo->body[todolen] = '\0';
    return true;
}

bool parse_todos(char *content, Todos *todos)
{
    da_clear(todos);
    Todo todo;

    char *it = content;
    while (*it) {
        if (!parse_todo(&it, &todo)) return false;
        da_push(todos, todo);
    }
    return true;
}

static char *program_name = NULL;
void usage()
{
    // TODO
    printf("usage: %s\n", program_name);
}

void info()
{
    printf("todo is a cli tool to manage todos\n");
    usage();
}

bool is_valid_todo_path(char *path)
{
    FILE *f = fopen(path, "r");
    bool is_valid_path = false;
    if (f) {
        is_valid_path = true;
        fclose(f);
    }
    bool is_todo_path = false;
    char *point = NULL;
    if ((point = strrchr(path, '.'))) {
        if (streq(point, ".td")) {
            is_todo_path = true;
        }
    }
    return is_valid_path && is_todo_path;
}

bool get_all_todos(Todos *todos)
{
    da_clear(todos);
    if (!is_valid_todo_path(todo_path)) {
        printf("ERROR: `%s` is not a valid todo path\n", todo_path);
        printf("NOTE: a todo path is an existing file with extension .td\n");
        printf("NOTE: If it's your first time you can add a todo via this command: %s add\n", program_name);
        return false;
    }
    char *content = read_file(todo_path);
    if (!content) return false;
    if (!parse_todos(content, todos)) return false;
    if (todos->count == 0) {
        printf("INFO: No todos found at `%s`\n", todo_path);
        printf("NOTE: You can add one with this command: %s add\n", program_name);
        return false;
    }
    qsort(todos->items, todos->count, sizeof(todos->items[0]), compare_todos_descending_priority);
    return true;
}

bool command_show()
{
    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (!t.completed || flag_all)
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
    printf("DEBUG: content:\n`%s`\n", todo.body);
    return true;
}

// TODO: use parse_todo here
bool add_or_modify_todo(Todo *todo)
{
    bool modify = !!todo;
    FILE *f = fopen(TMP_TODO_FILENAME, "w");
    if (!f) {
        printf("ERROR: Could not create temporary file in /tmp\n"); 
        return false;
    } else {
        fprintf(f, PRIORITY_STR);
        if (modify) fprintf(f, "%zu", todo->priority);
        fprintf(f, "\n");
        fprintf(f, DIVIDER_STR_WITH_NL);
        if (modify) fprintf(f, todo->body);
        fprintf(f, DIVIDER_STR_WITH_NL);
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
    if (strlen(content) == 0) {
        printf("INFO: empty todos won't be added\n");
        return true;
    }
    if (strncmp(content, PRIORITY_STR, strlen(PRIORITY_STR)) != 0) {
        printf("ERROR: you may have modified the structure of the file, do it again.\n");
        printf("NOTE: here's the text you inserted\n");
        printf("%s\n", content);
        return false;
    }
    content += strlen(PRIORITY_STR);

    char *end = content;
    while (*end != '\n') end++;
    *end = '\0';
    if (strlen(content) == 0) {
        printf("ERROR: you didn't specify the priority.\n");
        printf("NOTE: priority is a positive integer greater than zero.\n");
        printf("NOTE: here's the text you inserted\n");
        content = end+1;
        while (*content != '\n') content++;
        content++;
        printf(DIVIDER_STR_WITH_NL "%s\n" DIVIDER_STR_WITH_NL, content);
        return false;
    }
    size_t priority = 0;
    if (isdigit(*content)) {
        priority = atoi(content);
        //TODO: check if line is not empty
    } else {
        printf("ERROR: unknown priority format `%s`.\n", content);
        printf("NOTE: priority is a positive integer.\n");
        printf("NOTE: here's the text you inserted\n");
        content = end+1;
        skip_line(&content);
        printf(DIVIDER_STR_WITH_NL "%s\n" DIVIDER_STR_WITH_NL, content);
        return false;
    }

    content = end + 1;
    skip_line(&content);

    Todo new_todo = {0};
    new_todo.priority = priority;
    new_todo.body = content;
    if (!modify) return add_todo_to_file(new_todo);
    else {
        *todo = new_todo;
        return true;
    }
}

static inline bool command_add(void) { return add_or_modify_todo(NULL); }
static inline bool modify_todo(Todo *todo) { return add_or_modify_todo(todo); }

bool command_modify()
{
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
    printf("Insert the number of the todo that you want to modify (or insert `quit`)\n");
    while (index <= 0) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (streq(buffer, "quit"))
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
    printf("Insert the number of the todo that you want to complete (or insert `quit`)\n");
    while (index <= 0) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (streq(buffer, "quit"))
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
    printf("INFO: todo %d has been marked as completed\n", n);
    return true;
}

bool command_remove()
{
    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        printf("%zu.\n", i+1);
        todo_print(t, stdout);
    }

    int index = 0;
    printf("Insert the number of the todo that you want to remove (or insert `quit`)\n");
    while (index <= 0) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (streq(buffer, "quit"))
            return false;
        index = atoi(buffer);
        printf("index: %d\n", index);
        if (index > 0) break;
        else printf("ERROR: `%s` is not a valid number, check again.\n", buffer);
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
    printf("INFO: todo %d has been removed\n", index);
    return true;
}

typedef enum
{
    CMD_SHOW,
    CMD_ADD,
    CMD_COMPLETE,
    CMD_REMOVE,
    CMD_MODIFY,
    CMD_UNKNOWN,
    CMDS_COUNT
} Command;

Command get_command(char *str)
{
         if (streq(str, "show"))     return CMD_SHOW;
    else if (streq(str, "add"))      return CMD_ADD;
    else if (streq(str, "complete")) return CMD_COMPLETE;
    else if (streq(str, "remove"))   return CMD_REMOVE;
    else if (streq(str, "modify"))   return CMD_MODIFY;
    else                             return CMD_UNKNOWN;
}

int main(int argc, char **argv)
{
    program_name = argv[0];

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (*arg == '-') {
            arg++;
            da_push(&flags, arg);
        } else {
            da_push(&args, arg);
        }
    }

    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "all")) flag_all = true;    
        else if (streq(flag, "help") || streq(flag, "h")) {
            info();
            return 0;
        } else {
            printf("ERROR: unknown flag `%s`\n", flag);
            usage();
            return 1;
        }
    }

    char *first_arg = NULL;
    Command command = CMD_UNKNOWN;
    if (args.count == 0) command = CMD_SHOW;
    else {
        first_arg = args.items[0];
        if (is_valid_todo_path(first_arg)) {
            command = CMD_SHOW;
            todo_path = first_arg;
        } else command = get_command(first_arg);
    }

    static_assert(CMDS_COUNT == 6, "Switch all commands");
    switch (command)
    {
        case CMD_SHOW:     if (!command_show())   return 1; break;
        case CMD_ADD:      if (!command_add())      return 1; break; 
        case CMD_COMPLETE: if (!command_complete()) return 1; break;
        case CMD_REMOVE:   if (!command_remove())   return 1; break;
        case CMD_MODIFY:   if (!command_modify())   return 1; break;
        case CMD_UNKNOWN:
        default:
            printf("ERROR: `%s` is not a todo path nor a valid command \n", first_arg);
            return 1;
    }

    return 0;
}
