// TODO
// - copy all the todos in ~/all.td into the folder with their respective cathegory

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
#include <dirent.h>

#include "dynamic_arrays.h"

#define return_defer(value) \
    do {                    \
        result = (value);   \
        goto defer;         \
    } while (0)

static char *home_path = NULL;
static char *todo_path = NULL;
#define DIVIDER_STR "---"
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

char *join_path(char *p1, char *p2)
{
    char buffer[PATH_MAX + 1];
    size_t len = strlen(p1) + 1 + strlen(p2) + 1;
    snprintf(buffer, len, "%s/%s", p1, p2);
    char *result = malloc(sizeof(char)*(len+1));
    strncpy(result, buffer, len);
    result[len] = '\0';
    return result;
}

static inline void set_todo_path(void) { todo_path = join_path(home_path, ".todo"); }

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

char *next_todo_filename_in_dir(char *dirpath)
{
    size_t i = 0;
    char *filepath_format = join_path(dirpath, "%ctodo%zu");
    char buffer1[PATH_MAX + 2];
    char buffer2[PATH_MAX + 2];
    while (true) {
        snprintf(buffer1, sizeof(buffer1), filepath_format, '-', i);
        snprintf(buffer2, sizeof(buffer2), filepath_format, '+', i);
        if (!does_file_exist(buffer1) && !does_file_exist(buffer2)) {
            free(filepath_format);
            size_t len = strlen(buffer1);
            char *filepath = malloc(sizeof(char)*(len+1));
            strncpy(filepath, buffer1, len);
            filepath[len] = '\0';
            return filepath;
        }
        i++;
    }
    return NULL;
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
#define NO_CATHEGORY "_no_cathegory"
static char *global_cathegory = NO_CATHEGORY;
static size_t global_priority = 0;

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
    char *path;
    char *cathegory;
    char *title;
    bool completed;
    size_t priority;
    AoS tags;
    char *body;
} Todo;

void todo_fprint(Todo t, FILE *sink)
{
    fprintf(sink, "TITLE: %s\n", t.title ? t.title : "");
    fprintf(sink, "COMPLETED: %s\n", t.completed ? "TRUE" : "FALSE");
    fprintf(sink, "PRIORITY: %zu\n", t.priority);
    fprintf(sink, "TAGS: ");
    da_enumerate (t.tags, i, tag) {
        fprintf(sink, "%s", *tag);
        if (i != t.tags.count-1) fprintf(sink, ", ");
    }
    fprintf(sink, "\n");
    fprintf(sink, DIVIDER_STR_WITH_NL);
    fprintf(sink, "%s\n", t.body ? t.body : "");
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

char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("ERROR: Could not open file at `%s`: %s\n", path, strerror(errno));
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

    char *it = *content;

    if (!advance(&it)) return_defer(false);
    if (strncmp(it, "TITLE: ", strlen("TITLE: ")) != 0) {
        printf("ERROR: expected `TITLE: [title]`\n");
        return_defer(false);
    } else it += strlen("TITLE: ");
    if (!advance(&it)) return_defer(false);
    char *title_begin = it;
    while (*it && *it != '\n') it++;
    if (!*it) return_defer(false); // TODO
    char *title_end = it-1;
    while (isblank(*title_end)) title_end--;
    ptrdiff_t title_len = title_end - title_begin + 1;
    if (title_len > 0) {
        todo->title = malloc(sizeof(char)*(title_len+1));
        strncpy(todo->title, title_begin, title_len);
        todo->title[title_len] = '\0';
    }
    //printf("Title: %s\n", todo->title ? todo->title : "");
    it++;

    if (!advance(&it)) return_defer(false);
    if (strncmp(it, "COMPLETED: ", strlen("COMPLETED: ")) != 0) {
        printf("ERROR: expected `COMPLETED: TRUE|FALSE`\n");
        return_defer(false);
    } else it += strlen("COMPLETED: ");
    if (!advance(&it)) return_defer(false);
    if (strncmp(it, "TRUE", strlen("TRUE")) == 0) {
        it += strlen("TRUE");
        todo->completed = true;
    } else if (strncmp(it, "FALSE", strlen("FALSE")) == 0) {
        it += strlen("FALSE");
        todo->completed = false;
    } else {
        printf("ERROR: expected `TRUE` or `FALSE`\n");
        return_defer(false);
    }
    if (!advance(&it)) return_defer(false);
    while (isblank(*it)) it++;
    if (*it != '\n') {
        printf("ERROR: unexpected end of COMPLETED line\n");
        return_defer(false);
    }
    //printf("Completed: %s\n", todo->completed ? "true" : "false");
    it++;

    if (!advance(&it)) return_defer(false);
    if (strncmp(it, "PRIORITY: ", strlen("PRIORITY: ")) != 0) {
        printf("ERROR: expected `PRIORITY: <priority>`\n");
        return_defer(false);
    } else it += strlen("PRIORITY: ");
    if (!advance(&it)) return_defer(false);
    if (!isdigit(*it)) {
        printf("ERROR: expected priority number\n");
        return_defer(false);
    }
    char *end;
    long priority = strtol(it, &end, 10);
    if (priority < 0) {
        *end = '\0';
        printf("ERROR: priority should be a positive integer greater or equal to zero\n");
        printf("NOTE: you inserted `%s`\n", it);
        return_defer(false);
    }
    todo->priority = priority;
    it = end;
    if (!advance(&it)) return_defer(false);
    while (isblank(*it)) it++;
    if (*it != '\n') {
        printf("ERROR: unexpected end of PRIORITY line\n");
        return_defer(false);
    }
    //printf("Priority: %zu\n", todo->priority);
    it++;

    if (!advance(&it)) return_defer(false);
    if (strncmp(it, "TAGS: ", strlen("TAGS: ")) != 0) {
        printf("ERROR: expected `TAGS: [tags...]`\n");
        return_defer(false);
    } else it += strlen("TAGS: ");
    
    while (true) {
        if (!advance(&it)) return_defer(false);
        if (*it == '\n') {
            it++;
            break;
        }
        char *tag_begin = it;
        while (*it && !isspace(*it) && *it != ',') it++;
        if (!*it) return_defer(false); // TODO
        ptrdiff_t tag_len = it - tag_begin;
        if (tag_len == 0) {
            printf("ERROR: tags must be separated by commas\n");
            return_defer(false);
        }
        char *tag = malloc(sizeof(char)*(tag_len+1));
        strncpy(tag, tag_begin, tag_len);
        tag[tag_len] = '\0';
        da_push(&todo->tags, tag);
        if (*it == ',') it++;
        else if (*it == '\n') {
            it++;
            break;
        }
    }
    //printf("Tags: ");
    //da_foreach (todo->tags, tag) {
    //    printf("%s ", *tag);
    //}
    //printf("\n");

    if (!advance(&it)) return_defer(false);
    if (strncmp(it, DIVIDER_STR_WITH_NL, strlen(DIVIDER_STR_WITH_NL)) != 0) {
        printf("ERROR: todo content should begin with `"DIVIDER_STR"`\n");
        return_defer(false);
    } else it += strlen(DIVIDER_STR_WITH_NL);

    if (!advance(&it)) return_defer(false);
    char *body_begin = it;
    size_t body_len = 0;
    while (*it) {
        body_len++;
        it++;
    }
    todo->body = malloc(sizeof(char)*(body_len+1));
    strncpy(todo->body, body_begin, body_len);
    todo->body[body_len] = '\0';
    //printf("Body:\n%s", todo->body);

defer:
    *content = it;
    return result;
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

static_assert(CMDS_COUNT == 8, "Get all commands to cstr in command_to_cstr");
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
    case CMD_PRINT:    return "print";
    case CMD_UNKNOWN:  return "unknown";
    case CMDS_COUNT:
    default:
        printf("Unreachable command in command_to_cstr\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 8, "Get all commands info in info_of");
const char *info_of(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "print todos to stdout";
    case CMD_ADD:      return "add todo";
    case CMD_COMPLETE: return "complete todo";
    case CMD_DELETE:   return "delete todo";
    case CMD_MODIFY:   return "modify todo";
    case CMD_HELP:     return "show help for `command`";
    case CMD_PRINT:    return "print `info`";
    case CMD_UNKNOWN:
    case CMDS_COUNT:
    default:
        printf("Unreachable command in info_of\n");
        abort();
    }
}

static_assert(CMDS_COUNT == 8, "Get all commands info in usage_of");
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

static_assert(CMDS_COUNT == 8, "Get all commands info in flags_of");
const char *flags_of(Command cmd)
{
    switch (cmd)
    {
    case CMD_SHOW:     return "-(a)ll                       also print completed todos\n";
    case CMD_ADD:      return "no flags for command 'add'";
    case CMD_COMPLETE: return "-(a)ll                also choose among completed todos\n";

    case CMD_DELETE:   return "-(a)ll                      also choose among completed todos\n"           \
                              "-(A)LL                      delete ALL todos (with specified `tags`)\n"    \
                              "-(c)ompleted                delete completed todos (with specified `tags`)";

    case CMD_MODIFY:   return "-(a)ll                also choose among completed todos\n";
    case CMD_HELP:     return "no flags for command 'help'";
    case CMD_PRINT:    return "no flags for command 'print'";

    case CMD_UNKNOWN:
    case CMDS_COUNT:
    default:
        printf("Unreachable command in flags_of\n");
        abort();
    }
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

static_assert(CMDS_COUNT == 8, "Get all commands from string in get_command");
Command get_command(char *str)
{
         if (streq(str, "show"))                          return CMD_SHOW;
    else if (streq(str, "add"))                           return CMD_ADD;
    else if (streq(str, "complete") || streq(str, "com")) return CMD_COMPLETE;
    else if (streq(str, "delete")   || streq(str, "del")) return CMD_DELETE;
    else if (streq(str, "modify")   || streq(str, "mod")) return CMD_MODIFY;
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

bool parse_todo_from_file(char *todo_filename, Todo *todo)
{
    char *content = read_file(todo_filename);
    if (!content) return false;
    return parse_todo(&content, todo);
}

bool get_todos_in_cathegory(Todos *todos, char *cathegory_name)
{
    char *cathegory_dir = join_path(todo_path, cathegory_name);

    DIR *d = opendir(cathegory_dir);
    if (!d) {
        printf("ERROR: Could not open todo cathegory directory at `%s`: %s\n", cathegory_dir, strerror(errno));
        return false;
    }
    struct dirent *todo_file;
    while ((todo_file = readdir(d))) {
        if (todo_file->d_name[0] == '.') continue;
        char *todo_filepath = join_path(cathegory_dir, todo_file->d_name);
        Todo todo = {
            .path = strdup(todo_filepath),
            .cathegory = strdup(cathegory_name)
        };
        if (!parse_todo_from_file(todo_filepath, &todo)) return false;
        da_push(todos, todo);
    }
    return true;
}

bool get_all_todos(Todos *todos)
{
    da_clear(todos);
    DIR *d = opendir(todo_path);
    if (!d) {
        printf("ERROR: Could not open todo directory at `%s`\n", todo_path);
        return false;
    }
    struct dirent *cathegory_dir;
    while ((cathegory_dir = readdir(d))) {
        if (cathegory_dir->d_name[0] == '.') continue;
        char *cathegory_path = join_path(todo_path, cathegory_dir->d_name);
        if (!is_directory(cathegory_path)) {
            printf("ERROR: %s is not a directory\n", cathegory_path);
            printf("NOTE: `%s` should contain cathegories directories only\n", todo_path);
        }
        if (!get_todos_in_cathegory(todos, cathegory_dir->d_name)) return false;
    }

    if (todos->count == 0) {
        printf("INFO: no todos found at `%s`\n", todo_path);
        printf("NOTE: to add one use the command: %s add\n", program_name);
        return false;
    }
    da_sort(todos, compare_todos_descending_priority);
    return true;
}

bool todo_has_selected_tag(Todo t)
{
    if (tags.count == 0) return true;
    if (da_is_empty(&t.tags)) return false;
    for (size_t i = 0; i < tags.count; i++) {
        da_foreach (t.tags, tag) {
            if (streq(*tag, tags.items[i])) {
                return true;
            }
        }
    }
    return false;
}

#define ALL true
size_ts get_todo_indices(Todos todos, bool all)
{
    size_ts indices = {0};
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if ((!t.completed || all) && todo_has_selected_tag(t))
            da_push(&indices, i);
    }
    return indices;
}

void todo_free(Todo *t) {
    if (t->title) free(t->title);
    t->title = NULL; 
    if (t->body) free(t->body);
    t->body = NULL; 
    if (t->cathegory) free(t->cathegory);
    t->cathegory = NULL; 
    if (!da_is_empty(&t->tags)) {
        da_foreach (t->tags, tag) free(*tag);
        da_free(&t->tags);
    }
}

bool get_all_tags_in_cathegory(AoS *tags)
{
    Todos todos = {0};
    if (!get_todos_in_cathegory(&todos)) return false; 
    da_clear(tags);
    for (size_t i = 0; i < todos.count; i++) {
        Todo t = todos.items[i];
        if (da_is_empty(&t.tags)) continue;
        da_foreach (t.tags, tag) {
            bool found = false;
            for (size_t j = 0; j < tags->count; j++) {
                if (streq(*tag, tags->items[i])) {
                    found = false;
                    break;
                }
            }
            if (!found) da_push(tags, strdup(*tag));
        }
        todo_free(&t);
    }
    da_free(&todos);
    return true;
}

bool print_all_tags_in_cathegory(void)
{
    AoS tags = {0};
    if (!get_all_tags_in_cathegory(&tags)) return false;
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
        if(!print_all_tags_in_cathegory()) return false;
    } else {
        printf("ERROR: unknown <info> '%s' for command print\n", info);
        printf("NOTE: Available options:\n");
        printf("NOTE: - tags         found in file %s\n", todo_path);
        return false;
    }

    return true;
}

bool is_valid_todo_path(char *path)
{
    if (!path) return false;
    if (!does_file_exist(path)) return false;
    if (is_directory(path)) return false;
    // TODO: match file name
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

    // TODO: match file name

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
    printf("INFO: There are no todos @%s ", global_cathegory);
    da_foreach (tags, tag) {
        printf("#%s ", *tag); 
    }
    printf("\n");
    printf("INFO: But there are:\n");
    print_all_tags_in_cathegory();
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
    if (!get_todos_in_cathegory(&todos)) return false;
    size_ts indices = get_todo_indices(todos, flag_show_all);
    if (indices.count == 0) {
        print_no_todos_found();
        return true;
    }

    clear_screen();
    for (size_t i = 0; i < indices.count; i++) {
        Todo t = todos.items[indices.items[i]];
        todo_print(t);
    }
    return true;
}

// TODO: delete file if some error occurs
bool command_add(void)
{
    bool flag_error = false;
    for (size_t i = 0; i < flags.count; i++) {
        char *flag = flags.items[i];
        printf("ERROR: unknown flag `%s` for command add\n", flag);
        flag_error = true;
    }
    if (flag_error) {
        printf("TODO: print command usage\n");
        return false;
    }

    if (args.count > 0) {
        printf("ERROR: too many arguments for command add\n");
        help_of(CMD_ADD);
        return false;
    }

    char *cathegory_dir = join_path(todo_path, global_cathegory);
    char *todo_filepath = next_todo_filename_in_dir(cathegory_dir); 
    FILE *f = fopen(todo_filepath, "w");
    if (!f) {
        printf("ERROR: Could not open file %s: %s\n", todo_filepath, strerror(errno));
        return false;
    }

    Todo template_todo = {
        .priority = global_priority,
        .cathegory = global_cathegory,
        .path = strdup(todo_filepath)
    };
    da_foreach (tags, tag)
        da_push(&template_todo.tags, *tag);
    todo_fprint(template_todo, f);
    fclose(f);

    char *cmd[3] = {0};
    char *editor = getenv("EDITOR");
    if (!editor) editor = "vi";
    cmd[0] = editor;
    cmd[1] = todo_filepath;

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

    char *content = read_file(todo_filepath);
    if (!content) {
        printf("ERROR: could not read todo file `%s`\n", todo_filepath);
        return false;
    }

    Todo new_todo = {0};
    if (!parse_todo(&content, &new_todo)) return false;

    if (strlen(new_todo.body) == 0 && strlen(new_todo.title) == 0) {
        printf("INFO: empty todos won't be added\n");
        return true;
    }
    char *empty_body = new_todo.body;
    while (isspace(*empty_body)) empty_body++;
    if (!*empty_body) {
        printf("INFO: todos with empty lines or spaces only won't be added\n");
        // TODO: maybe keep it if title is present
        return true;
    }

    printf("INFO: todo has been added to %s\n", todo_filepath);
    return true;
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

bool save_todo(Todo todo)
{
    FILE *f = fopen(todo.path, "w");
    if (!f) {
        printf("ERROR: could not open `%s`\n", todo.path);
        return false;
    }
    todo_fprint(todo, f);
    fclose(f);
    return true;
}

// TODO: delete file if some error occurs
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
            flag_error = true;
        }
    }
    if (flag_error) {
        printf("TODO: print command usage\n");
        return false;
    }

    if (args.count > 0) {
        printf("ERROR: too many arguments for command modify\n");
        help_of(CMD_MODIFY);
        return false;
    }

    // TODO
    Todos todos = {0};
    if (global_cathegory) if (!get_todos_in_cathegory(&todos, global_cathegory)) return false;
    else if (!get_all_todos(&todos)) return false;
    size_ts indices = get_todo_indices(todos, modify_all);
    if (indices.count == 0) {
        if (tags.count == 0) printf("INFO: There are no todos to modify at `%s`", todo_path);
        else print_no_todos_found();
        return true;
    }
    clear_screen();
    for (size_t i = 0; i < indices.count; i++) {
        Todo t = todos.items[indices.items[i]];
        printf("%zu.\n", i);
        todo_print(t);
    }

    int index;
    if (!get_todo_index_from_user(&index, indices.count, "modify")) return false;

    Todo *todo = &todos.items[indices.items[index]];
    FILE *f = fopen(todo->path, "w");
    if (!f) {
        printf("ERROR: Could not open file %s: %s\n", todo->path, strerror(errno));
        return false;
    }

    todo_fprint(*todo, f);
    fclose(f);

    char *cmd[3] = {0};
    char *editor = getenv("EDITOR");
    if (!editor) editor = "vi";
    cmd[0] = editor;
    cmd[1] = todo->path;

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

    char *content = read_file(todo->path);
    if (!content) {
        printf("ERROR: could not read todo file `%s`\n", todo->path);
        return false;
    }

    Todo new_todo = {0};
    if (!parse_todo(&content, &new_todo)) return false;

    if (strlen(new_todo.body) == 0 && strlen(new_todo.title) == 0) {
        printf("INFO: empty todos won't be added\n");
        return true;
    }
    char *empty_body = new_todo.body;
    while (isspace(*empty_body)) empty_body++;
    if (!*empty_body) {
        printf("INFO: todos with empty lines or spaces only won't be added\n");
        // TODO: maybe keep it if title is present
        return true;
    }

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
    if (!get_todos_in_cathegory(&todos)) return false;
    while (true) {
        size_ts indices = get_todo_indices(todos, !ALL);
        if (indices.count == 0) {
            if (tags.count == 0) printf("INFO: All todos are completed at `%s`", todo_path);
            else print_no_todos_found();
            return true;
        }
        clear_screen();
        for (size_t i = 0; i < indices.count; i++) {
            Todo t = todos.items[indices.items[i]];
            printf("%zu.\n", i);
            todo_print(t);
        }

        int index;
        if (!get_todo_index_from_user(&index, indices.count, "complete")) return false;
        Todo *todo = &todos.items[indices.items[index]];
        todo->completed = true;
        if (!save_todo(*todo)) return false;

        printf("INFO: todo %d has been marked as completed\n", index);

        if (indices.count-1 == 0) {
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

bool todo_delete(Todos *todos, size_t index)
{
    Todo *t = &todos->items[index];
    if (remove(t->path) != 0) {
        printf("ERROR: could not delete todo file `%s`: %s\n", t->path, strerror(errno));
        return false;
    }
    todo_free(t); 
    da_remove(todos, index);
    return true;
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
        if (!get_todos_in_cathegory(&todos)) return false;
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
                if (!todo_delete(&todos, i)) return false;
            } else i++;
        }
        size_t count_after = todos.count;

        printf("INFO: Deleted %zu todos\n", count_before - count_after + 1);
        return true;
    }

    Todos todos = {0};
    if (!get_todos_in_cathegory(&todos)) return false;
    if (todos.count == 0) {
        printf("INFO: There are no todos to delete\n");
        return true;
    }

    while (true) {
        size_ts indices = get_todo_indices(todos, select_from_all);
        if (indices.count == 0) {
            print_no_todos_found();
            return true;
        }
        clear_screen();
        for (size_t i = 0; i < indices.count; i++) {
            Todo t = todos.items[indices.items[i]];
            printf("%zu.\n", i);
            todo_print(t);
        }

        int index;
        if (!get_todo_index_from_user(&index, indices.count, "delete")) return false;

        if (!todo_delete(&todos, indices.items[index])) return false;

        printf("INFO: todo %d has been deleted\n", index);

        if (indices.count-1 == 0) {
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
    set_todo_path();

    program_name = argv[0];

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (*arg == '-') {
            arg++;
            char *flag = arg;
            if (!*flag) {
                printf("ERROR: flag without name (lonely '-')\n");
                return false;
            }
            da_push(&flags, flag);
        } else if (*arg == '@') {
            arg++;
            if (!*arg) {
                printf("ERROR: cathegory without name (lonely '@')\n");
                return false;
            }
            if (global_cathegory) {
                printf("ERROR: cathegory already set to `%s`\n", global_cathegory);
                return false;
            }
            global_cathegory = strdup(arg);
        } else if (*arg == '#') {
            arg++;
            if (!*arg) {
                printf("ERROR: tag without name (lonely '#')\n");
                return false;
            }
            da_push(&tags, arg);
        } else if (*arg == 'P') {
            arg++;
            if (!*arg) {
                printf("ERROR: priority without value (lonely 'P')\n");
                return false;
            }
            long priority = strtol(arg, NULL, 10);
            if (priority < 0) {
                printf("ERROR: priority should be a positive integer greater or equal to zero\n");
                printf("NOTE: you inserted `%s`\n", arg);
                return false;
            }
            global_priority = priority;
        } else {
            da_push(&args, arg);
        }
    }

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

    static_assert(CMDS_COUNT == 8, "Switch all commands in main");
    switch (command)
    {
        case CMD_SHOW:     result = command_show();     break;
        case CMD_ADD:      result = command_add();      break; 
        case CMD_COMPLETE: result = command_complete(); break;
        case CMD_DELETE:   result = command_delete();   break;
        case CMD_MODIFY:   result = command_modify();   break;
        case CMD_HELP:     result = command_help();     break;
        case CMD_PRINT:    result = command_print();    break;
        case CMD_UNKNOWN: {
            printf("ERROR: unknown command `%s`\n", args.items[0]);
            result = false;
        } break;
        default:
            printf("Unreachable switching command in main\n");
            abort();
    }

    return result ? 0 : 1;
}
