// TODO:
// - add tags to group todos
// - maybe remove support for # priority
// - replace DEFAULT_TODO_PATH everywhere with the more user specific todo_path
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
#define PRIORITY_STR "Priority: "

typedef struct
{
    char **items;
    size_t count;
    size_t capacity;
} AoS;

typedef struct
{
    char *body;
    size_t priority;
    bool completed;
} Todo;

void todo_print(Todo t, FILE *sink)
{
    if (t.completed) fprintf(sink, "~");
    else for (size_t i = 0; i < t.priority; i++) fprintf(sink, "#");
    fprintf(sink, "\n");
    fprintf(sink, "%s\n", t.body);
    fprintf(sink, "%%%%\n\n\n");
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

static bool flag_all = false;
bool parse_todos(char *content, Todos *todos)
{
    da_clear(todos);
    Todo todo;

    char *begin = content;
    char *it    = content;
    bool in_todo;
    while (*it != '\0') {
        if (!in_todo) {
            if (isspace(*it)) {
                it++;
                continue;
            } else if (*it == '#' || *it == '~') {
                in_todo = true;
                todo = (Todo){0};
                if (*it == '#') {
                    todo.priority = 1;
                    it++;
                    while (*it == '#') {
                        todo.priority++;
                        it++;
                    }
                } else {
                    todo.completed = true;
                    it++;
                }
                while (*it != '\n') {
                    if (!isspace(*it)) {
                        printf("ERROR: unexpected character `%c` on the priority line, did you touch something in this file? Because you shouldn't\n", *it);
                        printf("NOTE: Insert a newline and start writing your todo\n");
                        return false;
                    }
                    it++;
                }
                it++;
                begin = it;
            }
        } else {
            size_t todolen = 0;
            while (*it) {
                if (*it == '%') {
                    if (!*(it+1)) {
                        printf("ERROR: unexpected %% at the end of file, need %%%%\n");
                        return false;
                    }
                    if (*(it+1) == '%') {
                        in_todo = false;
                        it += 2;
                        break;
                    }
                }
                todolen++;
                it++;
            }
            todo.body = malloc(todolen+1);
            strncpy(todo.body, begin, todolen);
            todo.body[todolen] = '\0';
            da_push(todos, todo);
        }

        it++;
    }
    da_fit(todos);
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
        if (strcmp(point, ".td") == 0) {
            is_todo_path = true;
        }
    }
    return is_valid_path && is_todo_path;
}

bool get_all_todos_at(char *todo_path, Todos *todos)
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

bool print_ordered_todos()
{
    Todos todos = {0};
    if (!get_all_todos_at(todo_path, &todos)) return false;
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
    FILE *f = fopen(DEFAULT_TODO_PATH, "a");
    if (!f) {
        printf("ERROR: could not open todo file at `%s`\n", DEFAULT_TODO_PATH);
        return false;
    }
    fprintf(f, "\n");
    for (size_t i = 0; i < todo.priority; i++)
        fprintf(f, "#");
    fprintf(f, "\n");
    fprintf(f, "%s", todo.body);
    fprintf(f, "\n%%%%");
    fclose(f);
    printf("INFO: todo added at `%s`\n", DEFAULT_TODO_PATH);
    return true;
}

bool add_or_modify_todo(Todo *todo)
{
    bool modify = !!todo;
    printf("Modify? %s\n", modify ? "true" : "false");
    FILE *f = fopen(TMP_TODO_FILENAME, "w");
    if (!f) {
        printf("ERROR: Could not create temporary file in /tmp\n"); 
        return false;
    } else {
        fprintf(f, PRIORITY_STR);
        if (modify) fprintf(f, "%zu", todo->priority);
        fprintf(f, "\n");
        fprintf(f, "--------------------------------------------------\n");
        if (modify) fprintf(f, todo->body);
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
        printf("NOTE: priority is a positive integer or a sequence of '#'s.\n");
        printf("NOTE: here's the text you inserted\n");
        content = end+1;
        while (*content != '\n') content++;
        content++;
        printf("~~~\n%s\n~~~\n", content);
        return false;
    }
    size_t priority = 0;
    if (isdigit(*content)) {
        priority = atoi(content);
    } else if (*content == '#') {
        while (*content == '#') {
            priority++;
            content++;
        }
        //TODO: check if line is not empty
    } else {
        printf("ERROR: unknown priority format `%s`.\n", content);
        printf("NOTE: priority is a positive integer or a sequence of '#'s.\n");
        printf("NOTE: here's the text you inserted\n");
        content = end+1;
        skip_line(&content);
        printf("~~~\n%s\n~~~\n", content);
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

static inline bool command_add_todo(void) { return add_or_modify_todo(NULL); }
static inline bool modify_todo(Todo *todo) { return add_or_modify_todo(todo); }

bool command_modify_todo()
{
    Todos todos = {0};
    if (!get_all_todos_at(DEFAULT_TODO_PATH, &todos)) return false;
    int n = 1;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (t.completed) continue;
        printf("%d.\n", n);
        todo_print(t, stdout);
        n++;
    }

    int index = 0;
    printf("Insert the number of the todo that you'd want to modify (or insert `quit`)\n");
    while (index <= 0) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (strcmp(buffer, "quit") == 0)
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
    FILE *f = fopen(DEFAULT_TODO_PATH, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", DEFAULT_TODO_PATH);
        return false;
    }
    for (size_t i = 0; i < todos.count; i++) {
        todo_print(todos.items[i], f);
    }
    fclose(f);
    printf("INFO: todo %d has been modified\n", n);
    return true;
}

bool command_complete_todo()
{
    Todos todos = {0};
    if (!get_all_todos_at(DEFAULT_TODO_PATH, &todos)) return false;
    int n = 1;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (t.completed) continue;
        printf("%d.\n", n);
        todo_print(t, stdout);
        n++;
    }

    int index = 0;
    printf("Insert the number of the todo that you'd want to complete (or insert `quit`)\n");
    while (index <= 0) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (strcmp(buffer, "quit") == 0)
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
    FILE *f = fopen(DEFAULT_TODO_PATH, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", DEFAULT_TODO_PATH);
        return false;
    }
    for (size_t i = 0; i < todos.count; i++) {
        todo_print(todos.items[i], f);
    }
    fclose(f);
    printf("INFO: todo %d has been marked as completed\n", n);
    return true;
}

bool command_remove_todo()
{
    Todos todos = {0};
    if (!get_all_todos_at(DEFAULT_TODO_PATH, &todos)) return false;
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        printf("%zu.\n", i+1);
        todo_print(t, stdout);
    }

    int index = 0;
    printf("Insert the number of the todo that you'd want to remove (or insert `quit`)\n");
    while (index <= 0) {
        char buffer[16] = {0};
        read(STDIN_FILENO, buffer, sizeof(buffer));; 
        buffer[strlen(buffer)-1] = '\0';
        if (strcmp(buffer, "quit") == 0)
            return false;
        index = atoi(buffer);
        printf("index: %d\n", index);
        if (index > 0) break;
        else printf("ERROR: `%s` is not a valid number, check again.\n", buffer);
    }

    Todo t; (void)t;
    da_remove(&todos, index-1, t);
    FILE *f = fopen(DEFAULT_TODO_PATH, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", DEFAULT_TODO_PATH);
        return false;
    }
    for (size_t i = 0; i < todos.count; i++) {
        todo_print(todos.items[i], f);
    }
    fclose(f);
    printf("INFO: todo %d has been removed\n", index);
    return true;
}

int main(int argc, char **argv)
{
    program_name = argv[0];
    AoS args = {0};
    AoS flags = {0};

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
        if (strcmp(flag, "all") == 0) flag_all = true;    
        else if (strcmp(flag, "help") == 0 || strcmp(flag, "h") == 0) {
            info();
            return 0;
        } else {
            printf("ERROR: unknown flag `%s`\n", flag);
            usage();
            return 1;
        }
    }

    char *first_arg = NULL;
    char *command = NULL;
    if (args.count == 0) command = "show";
    else {
        first_arg = args.items[0];
        if (is_valid_todo_path(first_arg)) {
            command = "show";
            todo_path = first_arg;
        } else command = first_arg;
    }

    if (strcmp(command, "show") == 0) {
        if (!print_ordered_todos()) return 1;
    } else if (strcmp(command, "add") == 0) {
        if (!command_add_todo()) return 1;
    } else if (strcmp(command, "complete") == 0) {
        if (!command_complete_todo()) return 1;
    } else if (strcmp(command, "remove") == 0) {
        if (!command_remove_todo()) return 1;
    } else if (strcmp(command, "modify") == 0) {
        if (!command_modify_todo()) return 1;
    } else {
        printf("ERROR: `%s` is not a todo path nor a valid command \n", first_arg);
        return 1;
    }
    return 0;
}
