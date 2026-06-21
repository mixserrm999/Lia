#include "lia.h"

int is_flag(const char *value, const char *short_name, const char *long_name) {
    return strcmp(value, short_name) == 0 || strcmp(value, long_name) == 0;
}

int file_exists(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    return 1;
}

char *duplicate_string(const char *value) {
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length + 1);
    return copy;
}

char *copy_range(const char *start, size_t length) {
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, start, length);
    copy[length] = '\0';
    return copy;
}

char *format_string(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int length = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (length < 0) {
        return NULL;
    }

    char *result = malloc((size_t)length + 1);
    if (result == NULL) {
        return NULL;
    }

    va_start(args, format);
    vsnprintf(result, (size_t)length + 1, format, args);
    va_end(args);

    return result;
}

int starts_with(const char *value, const char *prefix) {
    return strncmp(value, prefix, strlen(prefix)) == 0;
}

int archive_suffix_matches(const char *value, const char *suffix) {
    size_t value_length = strcspn(value, "?#");
    size_t suffix_length = strlen(suffix);

    if (value_length < suffix_length) {
        return 0;
    }

    return strncmp(value + value_length - suffix_length, suffix, suffix_length) == 0;
}

int directory_exists(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) {
        return 0;
    }

    return S_ISDIR(info.st_mode);
}

char *join_path(const char *left, const char *right) {
    size_t left_length = strlen(left);
    const char *separator = "/";

    if (left_length > 0 && (left[left_length - 1] == '/' || left[left_length - 1] == '\\')) {
        separator = "";
    }

    return format_string("%s%s%s", left, separator, right);
}

char *shell_quote(const char *value) {
    size_t length = 2;
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        length += (*cursor == '\'') ? 4 : 1;
    }

    char *quoted = malloc(length + 1);
    if (quoted == NULL) {
        return NULL;
    }

    char *out = quoted;
    *out++ = '\'';
    for (const char *cursor = value; *cursor != '\0'; cursor++) {
        if (*cursor == '\'') {
            memcpy(out, "'\\''", 4);
            out += 4;
        } else {
            *out++ = *cursor;
        }
    }
    *out++ = '\'';
    *out = '\0';

    return quoted;
}

int run_shell_command(const char *command) {
    int status = system(command);
    if (status != 0) {
        fprintf(stderr, "lia: command failed: %s\n", command);
        return 1;
    }

    return 0;
}

int run_shell_command_1(const char *format, const char *arg) {
    char *quoted_arg = shell_quote(arg);
    if (quoted_arg == NULL) {
        return 1;
    }

    char *command = format_string(format, quoted_arg);
    free(quoted_arg);
    if (command == NULL) {
        return 1;
    }

    int result = run_shell_command(command);
    free(command);
    return result;
}

int run_shell_command_2(const char *format, const char *first, const char *second) {
    char *quoted_first = shell_quote(first);
    char *quoted_second = shell_quote(second);
    if (quoted_first == NULL || quoted_second == NULL) {
        free(quoted_first);
        free(quoted_second);
        return 1;
    }

    char *command = format_string(format, quoted_first, quoted_second);
    free(quoted_first);
    free(quoted_second);
    if (command == NULL) {
        return 1;
    }

    int result = run_shell_command(command);
    free(command);
    return result;
}

int make_directory_p(const char *path) {
    return run_shell_command_1("mkdir -p %s", path);
}

int remove_tree(const char *path) {
    return run_shell_command_1("rm -rf %s", path);
}

int copy_file_path(const char *source, const char *destination) {
    return run_shell_command_2("cp %s %s", source, destination);
}

int copy_directory_contents(const char *source, const char *destination) {
    char *source_contents = join_path(source, ".");
    if (source_contents == NULL) {
        return 1;
    }

    int result = run_shell_command_2("cp -R %s %s", source_contents, destination);
    free(source_contents);
    return result;
}

char *trim_trailing_slashes(const char *value) {
    size_t length = strlen(value);
    while (length > 0 && value[length - 1] == '/') {
        length--;
    }

    if (length == 0) {
        return duplicate_string(value);
    }

    return copy_range(value, length);
}

char *fetch_url_to_string(const char *url) {
    char *quoted_url = shell_quote(url);
    if (quoted_url == NULL) {
        return NULL;
    }

    char *command = format_string("curl -fsSL %s", quoted_url);
    free(quoted_url);
    if (command == NULL) {
        return NULL;
    }

    FILE *pipe = popen(command, "r");
    free(command);
    if (pipe == NULL) {
        return NULL;
    }

    size_t capacity = 4096;
    size_t length = 0;
    char *content = malloc(capacity);
    if (content == NULL) {
        pclose(pipe);
        return NULL;
    }

    int ch;
    while ((ch = fgetc(pipe)) != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *new_content = realloc(content, capacity);
            if (new_content == NULL) {
                free(content);
                pclose(pipe);
                return NULL;
            }
            content = new_content;
        }
        content[length++] = (char)ch;
    }

    if (pclose(pipe) != 0) {
        free(content);
        return NULL;
    }

    content[length] = '\0';
    return content;
}

int is_hex_string(const char *value, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (!isxdigit((unsigned char)value[i])) {
            return 0;
        }
    }

    return 1;
}

char *compute_file_integrity(const char *path) {
    char *quoted_path = shell_quote(path);
    if (quoted_path == NULL) {
        return NULL;
    }

    char *command = format_string("sha256sum %s", quoted_path);
    free(quoted_path);
    if (command == NULL) {
        return NULL;
    }

    FILE *pipe = popen(command, "r");
    free(command);
    if (pipe == NULL) {
        return NULL;
    }

    char line[256];
    if (fgets(line, sizeof(line), pipe) == NULL) {
        pclose(pipe);
        return NULL;
    }

    if (pclose(pipe) != 0) {
        return NULL;
    }

    if (strlen(line) < 64 || !is_hex_string(line, 64)) {
        return NULL;
    }

    return format_string("sha256:%.*s", 64, line);
}

int verify_file_integrity(const char *path, const char *expected_integrity, char **actual_integrity) {
    *actual_integrity = compute_file_integrity(path);
    if (*actual_integrity == NULL) {
        fprintf(stderr, "lia: failed to compute archive integrity for %s\n", path);
        return 1;
    }

    if (expected_integrity != NULL && strcmp(*actual_integrity, expected_integrity) != 0) {
        fprintf(stderr,
                "lia: integrity mismatch for %s\nexpected: %s\nactual:   %s\n",
                path,
                expected_integrity,
                *actual_integrity);
        return 1;
    }

    return 0;
}

char *read_file_to_string(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *content = malloc((size_t)length + 1);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }

    size_t read_count = fread(content, 1, (size_t)length, file);
    fclose(file);

    if (read_count != (size_t)length) {
        free(content);
        return NULL;
    }

    content[length] = '\0';
    return content;
}

char *sanitize_project_name(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return duplicate_string("lia-app");
    }

    size_t length = strlen(value);
    char *sanitized = malloc(length + 1);
    if (sanitized == NULL) {
        return NULL;
    }

    size_t output = 0;
    int previous_separator = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)value[i];
        if (isalnum(ch)) {
            sanitized[output++] = (char)tolower(ch);
            previous_separator = 0;
        } else if (ch == '-' || ch == '_' || ch == '.') {
            if (output > 0 && !previous_separator) {
                sanitized[output++] = '-';
                previous_separator = 1;
            }
        }
    }

    while (output > 0 && sanitized[output - 1] == '-') {
        output--;
    }

    if (output == 0) {
        free(sanitized);
        return duplicate_string("lia-app");
    }

    sanitized[output] = '\0';
    return sanitized;
}

char *default_project_name(void) {
    char cwd[4096];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return duplicate_string("lia-app");
    }

    const char *name = cwd;
    for (const char *cursor = cwd; *cursor != '\0'; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            name = cursor + 1;
        }
    }

    return sanitize_project_name(name);
}

void write_json_string(FILE *file, const char *value) {
    fputc('"', file);

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        switch (*cursor) {
            case '"':
                fputs("\\\"", file);
                break;
            case '\\':
                fputs("\\\\", file);
                break;
            case '\b':
                fputs("\\b", file);
                break;
            case '\f':
                fputs("\\f", file);
                break;
            case '\n':
                fputs("\\n", file);
                break;
            case '\r':
                fputs("\\r", file);
                break;
            case '\t':
                fputs("\\t", file);
                break;
            default:
                if (*cursor < 0x20) {
                    fprintf(file, "\\u%04x", *cursor);
                } else {
                    fputc(*cursor, file);
                }
                break;
        }
    }

    fputc('"', file);
}

int is_valid_github_part(const char *value, int allow_slash) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (isalnum(*cursor) || *cursor == '-' || *cursor == '_' || *cursor == '.') {
            continue;
        }

        if (allow_slash && *cursor == '/') {
            continue;
        }

        return 0;
    }

    return 1;
}

int is_valid_package_name(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (isalnum(*cursor) || *cursor == '-' || *cursor == '_' || *cursor == '.') {
            continue;
        }

        return 0;
    }

    return 1;
}

int is_non_empty_text(const char *value) {
    if (value == NULL) {
        return 0;
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (!isspace(*cursor)) {
            return 1;
        }
    }

    return 0;
}

int is_valid_script_name(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (isalnum(*cursor) || *cursor == '-' || *cursor == '_' || *cursor == ':' || *cursor == '.') {
            continue;
        }

        return 0;
    }

    return 1;
}
