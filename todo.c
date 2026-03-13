#include <stdio.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/wait.h>
#include <limits.h>

#include "dynamic_arrays.h"

#define return_defer(value) \
    do {                    \
        result = (value);   \
        goto defer;         \
    } while (0)

typedef struct
{
    char *name;
    char *path;
} Path;

typedef struct
{
    Path *items;
    size_t count;
    size_t capacity;
} Paths;

#define TODO_FILE_EXTENSION "td"
static char *home_path = NULL;
static char *todo_dir_path = NULL;
static char *default_todo_path = NULL;
static char *todo_path = NULL;
static char *paths_path = NULL;
static Paths paths = {0};
static bool todo_path_is_custom = false;
static char tmp_todo_filename[] = "/tmp/todo_XXXXXX";
#define DIVIDER_STR "----------"
#define DIVIDER_STR_WITH_NL DIVIDER_STR"\n"

bool set_home_path(void)
{
    home_path = getenv("HOME");
    if (!home_path) {
        printf("ERROR: could not get environment variable `HOME`\n");
        return false;
    }
    return true;
}

void set_todo_dir_path(void)
{
    char path_buffer[PATH_MAX + 1];
    snprintf(path_buffer, sizeof(path_buffer), "%s/.todo", home_path);
    size_t len = strlen(path_buffer);
    todo_dir_path = malloc(sizeof(char)*(len+1));
    strncpy(todo_dir_path, path_buffer, len);
    todo_dir_path[len] = '\0';
}

void set_default_todo_path(void)
{
    char path_buffer[PATH_MAX + 1];
    snprintf(path_buffer, sizeof(path_buffer), "%s/all.td", todo_dir_path);
    size_t len = strlen(path_buffer);
    default_todo_path = malloc(sizeof(char)*(len+1));
    strncpy(default_todo_path, path_buffer, len);
    default_todo_path[len] = '\0';

    todo_path = default_todo_path;
}

void set_paths_path(void)
{
    char paths_buffer[PATH_MAX + 1];
    snprintf(paths_buffer, sizeof(paths_buffer), "%s/paths", todo_dir_path);
    size_t len = strlen(paths_buffer);
    paths_path = malloc(sizeof(char)*(len+1));
    strncpy(paths_path, paths_buffer, len);
    paths_path[len] = '\0';
}

bool does_file_exist(char *path)
{
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool is_directory(char *path)
{
    if (!path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

bool can_get_paths(void)
{
    if (!does_file_exist(paths_path)) return false;
    if (is_directory(paths_path)) return false;
    FILE *f = fopen(paths_path, "r");
    if (!f) return false;
    else fclose(f);
    return true;
}

bool try_get_paths_and_report_error(void)
{
    if (!does_file_exist(paths_path)) {
        printf("ERROR: could not stat file at `%s`: %s\n", paths_path, strerror(errno));
        return false;
    }

    if (is_directory(paths_path)) {
        printf("ERROR: `%s` is a directory, not a file\n", paths_path);
        return false;
    }

    FILE *f = fopen(paths_path, "r");
    if (!f) {
        printf("ERROR: could not open file at `%s`: %s\n", paths_path, strerror(errno));
        return false;
    }

    char *_line = NULL;
    char *line;
    size_t len = 0;
    ssize_t nread;
    bool error = false;
    size_t n = 0;
    while ((nread = getline(&_line, &len, f)) != -1) {
        line = _line;
        n++;
        while (line && *line != '\n' && isblank(*line)) line++;
        if (!line || *line == '\n') continue;
        char *name = line;
        char *name_end = NULL;
        if (*line == '"') {
            name++;
            line++;
            while (line && *line != '\n' && *line != '"') line++;
            if (!line || *line == '\n') {
                printf("ERROR: unclosed string (paths line %zu)\n", n);
                error = true;
                continue;
            }
            name_end = line;
            line++;
        } else {
            while (line && !isspace(*line)) line++;
            name_end = line;
        }
        ptrdiff_t name_len = name_end - name;
        while (line && *line != '\n' && isblank(*line)) line++;
        if (!line || *line == '\n') {
            printf("ERROR: name `%.*s` without path (paths line %zu)\n", (int)name_len, name, n);
            error = true;
            continue;
        }
        char *path = line;
        while (line && *line && *line != '\n') line++;
        *line = '\0';

        char expanded_path[PATH_MAX + 1] = {0};
        if (path[0] == '~' && (path[1] == '/' || path[1] == '\0')) {
            snprintf(expanded_path, sizeof(expanded_path), "%s/%s", home_path, path+1);
            path = expanded_path;
        }

        char path_buf[PATH_MAX + 1] = {0};
        if (!realpath(path, path_buf)) {
            printf("ERROR: path `%s` does not exist (paths line %zu)\n", path, n);
            error = true;
            continue;
        }

        Path parsed_path = {
            .name = strndup(name, name_len),
            .path = strdup(path_buf)
        };
        da_push(&paths, parsed_path);
    }
    free(_line);
    fclose(f);
    return !error;
}

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

    char indent_fmt[4096] = {0};
    snprintf(indent_fmt, sizeof(indent_fmt), "%-*s%s", indent, "", fmt);

    char buffer[4096] = {0};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), indent_fmt, ap);
    va_end(ap);

    char *line = strtok(buffer, "\n");
    if (!line) return;

    printf("%s\n", line);
    line = strtok(NULL, "\n");
    printf_indent(indent, line);
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
    CMD_ADDPATH,
    CMD_HELP,
    CMD_PRINT,
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

void print_aos(AoS aos)
{
    for (size_t i = 0; i < aos.count; i++) {
        printf("'%s'", aos.items[i]);
        if (i != aos.count-1) printf(",");
        printf(" ");
    }
    printf("\n");
}

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
    printf("usage: %s command\n", program_name);
}

void info(void)
{
    printf("todo is a cli tool to manage todos\n");
    usage();
}

static_assert(CMDS_COUNT == 9, "Get all commands to cstr in command_to_cstr");
char *command_to_cstr(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "show";
    case CMD_ADD:      return "add";
    case CMD_COMPLETE: return "complete";
    case CMD_DELETE:   return "delete";
    case CMD_MODIFY:   return "modify";
    case CMD_ADDPATH:  return "addpath";
    case CMD_HELP:     return "help";
    case CMD_PRINT:    return "print";
    case CMD_UNKNOWN:  return "unknown";
    case CMDS_COUNT:
    default:
        printf("Unreachable command in command_to_cstr\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 9, "Get all commands info in info_of");
const char *info_of(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "print todos to stdout";
    case CMD_ADD:      return "add todo";
    case CMD_COMPLETE: return "complete todo";
    case CMD_DELETE:   return "delete todo";
    case CMD_MODIFY:   return "modify todo";
    case CMD_ADDPATH:  return "add path with a name to paths file";
    case CMD_HELP:     return "show help for `command`";
    case CMD_PRINT:    return "print `info`";
    case CMD_UNKNOWN:
    case CMDS_COUNT:
    default:
        printf("Unreachable command in info_of\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 9, "Get all commands info in usage_of");
char *usage_of(Command cmd)
{
    char *usage_msg = malloc(sizeof(char)*1024);
    usage_msg = memset(usage_msg, 0, sizeof(char)*1024);

    strcat(usage_msg, program_name);
    strcat(usage_msg, " ");
    switch (cmd)
    {
    case CMD_SHOW:     return strcat(usage_msg, "[show] [tags..] [flags..]");
    case CMD_ADD:      return strcat(usage_msg, "add [tags..] [flags..]");
    case CMD_COMPLETE: return strcat(usage_msg, "(com)plete [tags..] [flags..]");
    case CMD_DELETE:   return strcat(usage_msg, "(del)ete [tags..] [flags..]");
    case CMD_MODIFY:   return strcat(usage_msg, "(mod)ify [tags..] [flags..]");
    case CMD_ADDPATH:  return strcat(usage_msg, "addpath <name> <path>");
    case CMD_HELP:     return strcat(usage_msg, "(h)elp <command>");
    case CMD_PRINT:    return strcat(usage_msg, "print <info> [-path <path>]");
    case CMD_UNKNOWN:
    case CMDS_COUNT:
    default:
        printf("Unreachable command in usage_msg_of\n");
        abort();
    }
    return usage_msg;
}

static_assert(CMDS_COUNT == 9, "Get all commands info in flags_of");
const char *flags_of(Command cmd)
{
    char *flags_msg = malloc(sizeof(char)*1024);
    memset(flags_msg, 0, sizeof(char)*1024);

    switch (cmd)
    {
    case CMD_SHOW:     strcat(flags_msg, "-(a)ll                       also print completed todos\n"\
                                         "-(p)ath <path>               print todos in `path`. ");
                       strcat(flags_msg, "Default is ");
                       strcat(flags_msg, default_todo_path);
                       break;
    case CMD_ADD:      return "no flags for command 'add'";
    case CMD_COMPLETE: return "-(a)ll                also choose among completed todos\n";

    case CMD_DELETE:   return "-(a)ll                      also choose among completed todos\n"           \
                              "-(A)LL                      delete ALL todos (with specified `tags`)\n"    \
                              "-(c)ompleted                delete completed todos (with specified `tags`)";

    case CMD_MODIFY:   return "-(a)ll                also choose among completed todos\n";
    case CMD_ADDPATH:  return "no flags for command addpath";
    case CMD_HELP:     return "no flags for command 'help'";
    case CMD_PRINT:    return "-(p)ath <path>               print `info` based on `path`";
    case CMD_UNKNOWN:
    case CMDS_COUNT:
    default:
        printf("Unreachable command in flags_of\n");
        abort();
    }

    return flags_msg;
}

void help_of(Command cmd)
{
    printf("Command %s:\n", command_to_cstr(cmd));

    printf_indent(4, "info:\n");
    printf_indent(8, "%s\n", info_of(cmd));

    printf_indent(4, "usage:\n");
    printf_indent(8, "%s\n", usage_of(cmd));

    printf_indent(4, "flags:\n");
    printf_indent(8, "%s\n", flags_of(cmd));
}

static_assert(CMDS_COUNT == 9, "Get all commands from string in get_command");
Command get_command(char *str)
{
         if (streq(str, "show"))                          return CMD_SHOW;
    else if (streq(str, "add"))                           return CMD_ADD;
    else if (streq(str, "complete") || streq(str, "com")) return CMD_COMPLETE;
    else if (streq(str, "delete")   || streq(str, "del")) return CMD_DELETE;
    else if (streq(str, "modify")   || streq(str, "mod")) return CMD_MODIFY;
    else if (streq(str, "addpath"))                       return CMD_ADDPATH;
    else if (streq(str, "help")     || streq(str, "h"))   return CMD_HELP;
    else if (streq(str, "print"))                         return CMD_PRINT;
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
        for (Command cmd = 0; cmd < CMDS_COUNT; cmd++) {
            if (cmd == CMD_UNKNOWN) continue;
            help_of(cmd);
            printf("\n");
        }
    } else {
        Command cmd = get_command(args.items[0]);
        if (cmd == CMD_UNKNOWN) {
            printf("ERROR: unknown command `%s`\n", args.items[0]);
            return false;
        }
        help_of(cmd);
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
                todo_path_is_custom ? todo_path : "");
        return false;
    }
    da_sort(todos, compare_todos_descending_priority);
    return true;
}

bool todo_has_selected_tag(Todo t)
{
    if (tags.count == 0) return true;
    if (!t.tag) return false;
    for (size_t i = 0; i < tags.count; i++) {
        if (streq(t.tag, tags.items[i])) {
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

void todo_free(Todo *t) {
    if (t->body) free(t->body);
    t->body = NULL; 
    if (t->tag) free(t->tag);
    t->tag = NULL;
}

bool get_all_tags(AoS *tags)
{
    Todos todos = {0};
    if (!get_all_todos(&todos)) return false; 
    da_clear(tags);
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (t.tag == NULL) continue;
        bool found = false;
        for (size_t j = 0; j < tags->count; j++) {
            if (streq(t.tag, tags->items[j])) {
                found = true;
                break;
            }
        }
        if (found) continue;
        da_push(tags, strdup(t.tag));
        todo_free(&t);
    }
    da_free(&todos);
    return true;
}

bool print_all_tags_from_todo_path(void)
{
    AoS tags = {0};
    if (!get_all_tags(&tags)) return false;
    if (da_is_empty(&tags)) {
        printf("No tags found at `%s`\n", todo_path);
    } else {
        da_foreach (tags, tag) {
            printf("%s\n", *tag);
            free(*tag);
        }
    }
    return true;
}

bool command_print(void)
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command print\n", flag);
        flag_error = true;
    }
    if (flag_error) {
        printf("FLAGS: %s\n", flags_of(CMD_PRINT));
        return false;
    }

    if (args.count != 1) {
        printf("ERROR: too many arguments for command print\n");
        help_of(CMD_PRINT);
        return false;
    }

    char *info = args.items[0];
    if (streq(info, "tags")) {
        if(!print_all_tags_from_todo_path()) return false;
    } else if (streq(info, "paths")) {
        if (da_is_empty(&paths)) printf("No paths found at `%s`\n", paths_path);
        else da_foreach (paths, p) printf("%s -> %s\n", p->name, p->path);
    } else {
        printf("ERROR: unknown <info> '%s' for command print\n", info);
        printf("NOTE: Available options:\n");
        printf("NOTE: - tags         found in file %s\n", todo_path);
        printf("NOTE: - paths        found in file %s\n", paths_path);
        return false;
    }

    return true;
}

bool does_file_have_todo_extension(char *path)
{
    char *point = strrchr(path, '.');
    return (point++ && streq(point, TODO_FILE_EXTENSION));
}

bool is_valid_todo_path(char *path)
{
    if (!path) return false;
    if (!does_file_exist(path)) return false;
    if (is_directory(path)) return false;
    if (!does_file_have_todo_extension(path)) return false; 
    return true;
}

bool check_is_valid_todo_path_and_report_error(char *path)
{
    if (!path) return false;

    if (!does_file_exist(path)) {
        printf("ERROR: could not stat file at `%s`: %s\n", path, strerror(errno));
        return false;
    }

    if (is_directory(path)) {
        printf("ERROR: `%s` is a directory, not a file\n", path);
        return false;
    }

    if (!does_file_have_todo_extension(path)) {
        printf("ERROR: file `%s` has not extension `%s`\n", path, TODO_FILE_EXTENSION);
        return false; 
    }

    return true;
}

bool ask_user_confirmation(void)
{
    while (true) {
        char buffer[64] = {0};
        ssize_t nread = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (nread == -1) {
            printf("ERROR: could not read confirmation from stdin: %s\n", strerror(errno));
            exit(1);
        }
        if (nread == 0) return false;
        nread--;
        buffer[nread] = '\0';
        for (ssize_t i = 0; i <= nread; i++) {
            buffer[i] = tolower(buffer[i]);
        }
        if ((nread == 3 && streq(buffer, "yes")) || (nread == 1 && *buffer == 'y'))
            return true;
        else if ((nread == 2 && streq(buffer, "no")) || (nread == 1 && *buffer == 'n'))
            return false;

        printf("(y)es or (n)o?\n");
    }
    return false;
}

void print_no_todos_found(void)
{
    printf("INFO: There are no todos at `%s` ", todo_path);
    if (tags.count > 0) {
        printf("with tag%s ", tags.count == 1 ? "" : "s");
        print_aos(tags);
        printf("INFO: But there are:\n");
        print_all_tags_from_todo_path();
    }
}

bool command_addpath(void)
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command addpath\n", flag);
        flag_error = true;
    }
    if (flag_error) {
        puts(flags_of(CMD_ADDPATH));
        return false;
    }

    if (args.count != 2) {
        printf("ERROR: incorrect number of arguments for command addpath, expecting 2 but got %zu\n", args.count);
        printf("USAGE: %s\n", usage_of(CMD_ADDPATH));
        return false;
    }

    if (!does_file_exist(paths_path)) {
        if (errno == ENOENT) {
            FILE *f = fopen(paths_path, "w");
            if (!f) {
                printf("ERROR: could not create paths file at `%s`: %s\n", paths_path, strerror(errno));
                return false;
            } else {
                printf("INFO: paths file created at `%s`\n", paths_path);
                fclose(f);
            }
        } else {
            printf("ERROR: could not stat file at `%s`: %s\n", paths_path, strerror(errno));
            return false;
        }
    }

    if (is_directory(paths_path)) {
        printf("ERROR: `%s` should be a file but is a directory\n", paths_path);
        return false;
    }

    FILE *f = fopen(paths_path, "a");
    if (!f) {
        printf("ERROR: could not open file at `%s`: %s\n", paths_path, strerror(errno));
        return false;
    }

    char *name = args.items[0];
    char *path = args.items[1];

    bool path_error = !check_is_valid_todo_path_and_report_error(path);
    da_foreach (paths, p) {
        if (streq(p->name, name)) {
            printf("ERROR: redefinition of path `%s`\n", name);
            printf("NOTE: bound to `%s`\n", p->path);
            path_error = true;
            break;
        }
    }
    if (path_error) return false;

    bool has_spaces = strpbrk(name, " \t") != NULL;
    if (has_spaces) fprintf(f, "\"");
    fprintf(f, "%s", name);
    if (has_spaces) fprintf(f, "\"");
    fprintf(f, " %s\n", path);
    fclose(f);

    printf("INFO: Added path: `%s` -> %s\n", name, path);

    return true;
}

bool command_show(void)
{
    bool flag_show_all = false;

    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "a") || streq(flag, "all")) flag_show_all = true;
        else {
            printf("ERROR: unknown flag `%s` for command show\n", flag);
            flag_error = true;
        }
    }
    if (flag_error) {
        puts(flags_of(CMD_SHOW));
        return false;
    }

    if (args.count > 0) {
        printf("ERROR: too many arguments for command show\n");
        help_of(CMD_SHOW);
        return false;
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    size_ts indexes = get_todo_indexes(todos, flag_show_all);
    if (indexes.count == 0) {
        print_no_todos_found();
        return true;
    }

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
    int fd = mkstemp(tmp_todo_filename);
    if (fd == -1) {
        printf("ERROR: Could not create temporary file\n");
        return false;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        printf("ERROR: Could not open temporary file\n"); 
        close(fd);
        return false;
    }

    if (modify) {
        todo_fprint(*todo, f);
    } else {
        if (tags.count == 1) {
            template_todo.tag = tags.items[0];
        } else if (tags.count > 1) {
            printf("ERROR: at the moment only one tag at a time can be assigned to a todo\n");
            printf("NOTE: you inserted: ");
            print_aos(tags);
            fclose(f);
            return false;
        }
        todo_fprint(template_todo, f);
    }
    fprintf(f, "Any text inserted after the second line will be ignored\n");
    fclose(f);

    char *cmd[3] = {0};
    char *editor = getenv("EDITOR");
    if (!editor) editor = "vi";
    cmd[0] = editor;
    cmd[1] = tmp_todo_filename;

    pid_t child = fork();
    switch (child)
    {
    case -1:
        printf("ERROR: could not create child process to open editor\n");
        return false;
    case 0:
        execvp(editor, cmd);
        exit(0);
    default:
        waitpid(child, NULL, 0);
    }

    char *content = read_file(tmp_todo_filename);
    remove(tmp_todo_filename);
    if (!content) {
        printf("ERROR: could not read temporary todo file at `%s`\n", tmp_todo_filename);
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
        if (tags.count == 0) printf("INFO: There are no todos to modify at `%s`", todo_path);
        else print_no_todos_found();
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
            if (tags.count == 0) printf("INFO: All todos are completed at `%s`", todo_path);
            else print_no_todos_found();
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
            printf("\nINFO: There are no todos left to complete\n");
            return true;
        }

        printf("\nContinue completing? yes/no\n");
        if (!ask_user_confirmation()) return true;
    }
    printf("Unreachable in command_complete\n");
    abort();
}

bool clear_todo_file(void)
{
    FILE *f = fopen(todo_path, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", todo_path);
        return false;
    }
    fclose(f);
    printf("INFO: Todo file `%s` has been cleared\n", todo_path);
    return true;
}

void todo_remove(Todos *todos, size_t index)
{
    Todo *t = &todos->items[index];
    todo_free(t); 
    da_remove(todos, index);
}

bool command_delete(void)
{
    bool delete_all = false;
    bool select_from_all = false;
    bool delete_completed = false;

    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        if (streq(flag, "A") || streq(flag, "ALL")) delete_all = true;
        else if (streq(flag, "c") || streq(flag, "completed")) delete_completed = true;
        else if (streq(flag, "a") || streq(flag, "all")) select_from_all = true;
        else {
            printf("ERROR: unknown flag `%s` for command delete\n", flag);
            printf("TODO: print command usage\n");
            flag_error = true;
        }
    }
    if (delete_all && delete_completed) {
        printf("ERROR: conflicting flags -all and -completed\n");
        help_of(CMD_DELETE);
        flag_error = true;
    }
    if (flag_error) return false;

    if (args.count > 0) {
        printf("ERROR: too many arguments for command delete\n");
        help_of(CMD_DELETE);
        return false;
    }

    if (delete_all || delete_completed) {
        if (delete_all)
            printf("Delete ALL todos? yes/no\n");
        else
            printf("Delete all completed todos? yes/no\n");
        printf("path: `%s`\n", todo_path);
        if (tags.count > 0) {
            printf("tags: ");
            print_aos(tags);
        }

        bool confirmation = ask_user_confirmation();
        if (!confirmation) return true;
        if (delete_all && tags.count == 0) return clear_todo_file();

        Todos todos = {0};
        if (!get_all_todos(&todos)) return false;
        size_t i = 0;
        Todo t;
        size_t count_before = todos.count;
        while (i < todos.count) {
            t = todos.items[i];
            if (todo_has_selected_tag(t)) {
                if (delete_completed && !t.completed) {
                    i++;
                    continue;
                }
                todo_remove(&todos, i);
            } else i++;
        }
        size_t count_after = todos.count;

        if (!save_todos_to_file(todos)) return false;

        printf("INFO: Deleted %zu todos\n", count_before - count_after + 1);
        return true;
    }

    Todos todos = {0};
    if (!get_all_todos(&todos)) return false;
    if (todos.count == 0) {
        printf("INFO: There are no todos to delete\n");
        return true;
    }

    while (true) {
        size_ts indexes = get_todo_indexes(todos, select_from_all);
        if (indexes.count == 0) {
            if (tags.count == 0) printf("INFO: There are no todos at `%s`", todo_path);
            else print_no_todos_found();
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

        todo_remove(&todos, indexes.items[index]);

        if (!save_todos_to_file(todos)) return false;

        printf("INFO: todo %d has been deleted\n", index);

        if (indexes.count-1 == 0) {
            printf("\nINFO: There are no todos left to delete\n");
            return true;
        }

        printf("\nContinue deleting? yes/no\n");
        if (!ask_user_confirmation()) return true;
    }
    printf("Unreachable in command_delete\n");
    abort();
}

bool setup(int argc, char **argv)
{
    if (!set_home_path()) return false;
    set_todo_dir_path();

    set_paths_path();
    if (can_get_paths()) try_get_paths_and_report_error();

    program_name = argv[0];

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (*arg == '-') {
            arg++;
            char *flag = arg;
            if (!*flag) {
                printf("ERROR: flag without name (lonely dash)\n");
                return false;
            }
            if (streq(flag, "path") || streq(flag, "p")) {
                free(todo_path);
                todo_path_is_custom = true;
                if (i+1 >= argc) {
                    printf("ERROR: expected path after flag -path, but got nothing\n");
                    return false;
                }
                i++;
                char *path = argv[i];
                if (is_valid_todo_path(path)) {
                    todo_path = path;
                    continue;
                }
                if (!can_get_paths()) {
                    printf("ERROR: path `%s` is not a todo path\n", path);
                    check_is_valid_todo_path_and_report_error(path);
                    printf("ERROR: Moreover, could not reach paths file at `%s`\n", paths_path);
                    try_get_paths_and_report_error();
                    printf("NOTE: you can create the file and add a path with command `addpath`\n");
                    return false;
                }
                bool found = false;
                da_foreach (paths, p) {
                    if (streq(path, p->name)) {
                        found = true;
                        todo_path = p->path;
                        break;
                    }
                }
                if (!found) {
                    printf("ERROR: Could not find an associated path to `%s`\n", path);
                    if (da_is_empty(&paths)) {
                        printf("NOTE: there aren't any paths registered at `%s`:\n", paths_path);
                    } else {
                        printf("NOTE: Here are the registered paths at `%s`:\n", paths_path);
                        da_foreach (paths, p) printf("- %s -> %s\n", p->name, p->path);
                    }
                    return false;
                }
            } else {
                da_push(&flags, flag);
            }
        } else if (*arg == '@') {
            arg++;
            if (!*arg) {
                printf("ERROR: tag without name (lonely at)\n");
                return false;
            }
            da_push(&tags, arg);
        } else {
            da_push(&args, arg);
        }
    }

    if (todo_path == NULL) set_default_todo_path();

    return true;
}

int main(int argc, char **argv)
{
    if (!setup(argc, argv)) return 1;

    Command command = CMD_SHOW;
    if (args.count > 0) {
        command = get_command(args.items[0]);
        if (command != CMD_UNKNOWN) shift_args();
    }

    bool result = true;

    static_assert(CMDS_COUNT == 9, "Switch all commands in main");
    switch (command)
    {
        case CMD_SHOW:     result = command_show();     break;
        case CMD_ADD:      result = command_add();      break; 
        case CMD_COMPLETE: result = command_complete(); break;
        case CMD_DELETE:   result = command_delete();   break;
        case CMD_MODIFY:   result = command_modify();   break;
        case CMD_ADDPATH:  result = command_addpath();  break;
        case CMD_HELP:     result = command_help();     break;
        case CMD_PRINT:    result = command_print();    break;
        case CMD_UNKNOWN: {
            printf("ERROR: unknown command `%s`\n", args.items[0]);
        } break;
        default:
            printf("Unreachable switching command in main\n");
            abort();
    }

    return result ? 0 : 1;
}
