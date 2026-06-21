#include "lia.h"

char *json_quote_string(const char *value) {
    size_t length = 2;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (*cursor == '"' || *cursor == '\\' || *cursor == '\b' || *cursor == '\f' ||
            *cursor == '\n' || *cursor == '\r' || *cursor == '\t') {
            length += 2;
        } else if (*cursor < 0x20) {
            length += 6;
        } else {
            length += 1;
        }
    }

    char *quoted = malloc(length + 1);
    if (quoted == NULL) {
        return NULL;
    }

    char *out = quoted;
    *out++ = '"';
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        switch (*cursor) {
            case '"':
                *out++ = '\\';
                *out++ = '"';
                break;
            case '\\':
                *out++ = '\\';
                *out++ = '\\';
                break;
            case '\b':
                *out++ = '\\';
                *out++ = 'b';
                break;
            case '\f':
                *out++ = '\\';
                *out++ = 'f';
                break;
            case '\n':
                *out++ = '\\';
                *out++ = 'n';
                break;
            case '\r':
                *out++ = '\\';
                *out++ = 'r';
                break;
            case '\t':
                *out++ = '\\';
                *out++ = 't';
                break;
            default:
                if (*cursor < 0x20) {
                    snprintf(out, 7, "\\u%04x", *cursor);
                    out += 6;
                } else {
                    *out++ = (char)*cursor;
                }
                break;
        }
    }
    *out++ = '"';
    *out = '\0';

    return quoted;
}

const char *skip_json_ws(const char *cursor, const char *end) {
    while (cursor < end && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    return cursor;
}

int is_hex_digit(char ch) {
    return isdigit((unsigned char)ch) ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

int parse_json_string_at(const char *cursor, const char *end, char **out, const char **after) {
    if (cursor >= end || *cursor != '"') {
        return 0;
    }

    char *value = malloc((size_t)(end - cursor));
    if (value == NULL) {
        return 0;
    }

    const char *input = cursor + 1;
    char *output = value;

    while (input < end) {
        unsigned char ch = (unsigned char)*input++;
        if (ch == '"') {
            *output = '\0';
            *out = value;
            *after = input;
            return 1;
        }

        if (ch != '\\') {
            *output++ = (char)ch;
            continue;
        }

        if (input >= end) {
            free(value);
            return 0;
        }

        ch = (unsigned char)*input++;
        switch (ch) {
            case '"':
            case '\\':
            case '/':
                *output++ = (char)ch;
                break;
            case 'b':
                *output++ = '\b';
                break;
            case 'f':
                *output++ = '\f';
                break;
            case 'n':
                *output++ = '\n';
                break;
            case 'r':
                *output++ = '\r';
                break;
            case 't':
                *output++ = '\t';
                break;
            case 'u':
                if (end - input < 4 ||
                    !is_hex_digit(input[0]) ||
                    !is_hex_digit(input[1]) ||
                    !is_hex_digit(input[2]) ||
                    !is_hex_digit(input[3])) {
                    free(value);
                    return 0;
                }
                *output++ = '?';
                input += 4;
                break;
            default:
                free(value);
                return 0;
        }
    }

    free(value);
    return 0;
}

const char *find_matching_brace(const char *open_brace, const char *end) {
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    for (const char *cursor = open_brace; cursor < end; cursor++) {
        char ch = *cursor;

        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (ch == '\\') {
                escaped = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
            continue;
        }

        if (ch == '"') {
            in_string = 1;
            continue;
        }

        if (ch == '{') {
            depth++;
        } else if (ch == '}') {
            depth--;
            if (depth == 0) {
                return cursor;
            }
        }
    }

    return NULL;
}

int find_json_object_bounds(
    const char *json,
    const char *object_key,
    const char **content_start,
    const char **content_end
) {
    const char *end = json + strlen(json);

    for (const char *cursor = json; cursor < end; cursor++) {
        if (*cursor != '"') {
            continue;
        }

        char *key = NULL;
        const char *after_key = NULL;
        if (!parse_json_string_at(cursor, end, &key, &after_key)) {
            continue;
        }

        int matches = strcmp(key, object_key) == 0;
        free(key);

        if (!matches) {
            cursor = after_key - 1;
            continue;
        }

        const char *value = skip_json_ws(after_key, end);
        if (value >= end || *value != ':') {
            cursor = after_key - 1;
            continue;
        }

        value = skip_json_ws(value + 1, end);
        if (value >= end || *value != '{') {
            cursor = after_key - 1;
            continue;
        }

        const char *close_brace = find_matching_brace(value, end);
        if (close_brace == NULL) {
            return 0;
        }

        *content_start = value + 1;
        *content_end = close_brace;
        return 1;
    }

    return 0;
}

char *json_get_string_value(const char *json, const char *target_key) {
    const char *end = json + strlen(json);

    for (const char *cursor = json; cursor < end; cursor++) {
        if (*cursor != '"') {
            continue;
        }

        char *key = NULL;
        const char *after_key = NULL;
        if (!parse_json_string_at(cursor, end, &key, &after_key)) {
            continue;
        }

        int matches = strcmp(key, target_key) == 0;
        free(key);

        if (!matches) {
            cursor = after_key - 1;
            continue;
        }

        const char *value = skip_json_ws(after_key, end);
        if (value >= end || *value != ':') {
            cursor = after_key - 1;
            continue;
        }

        value = skip_json_ws(value + 1, end);
        if (value >= end || *value != '"') {
            cursor = after_key - 1;
            continue;
        }

        char *result = NULL;
        const char *after_value = NULL;
        if (!parse_json_string_at(value, end, &result, &after_value)) {
            return NULL;
        }

        return result;
    }

    return NULL;
}

void free_json_entries(JsonEntries *entries) {
    for (size_t i = 0; i < entries->count; i++) {
        free(entries->items[i].key);
        free(entries->items[i].raw_value);
    }

    free(entries->items);
    entries->items = NULL;
    entries->count = 0;
}

const char *find_json_value_end(const char *value_start, const char *end) {
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    for (const char *cursor = value_start; cursor < end; cursor++) {
        char ch = *cursor;

        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (ch == '\\') {
                escaped = 1;
            } else if (ch == '"') {
                in_string = 0;
            }
            continue;
        }

        if (ch == '"') {
            in_string = 1;
            continue;
        }

        if (ch == '{' || ch == '[') {
            depth++;
            continue;
        }

        if (ch == '}' || ch == ']') {
            if (depth > 0) {
                depth--;
            }
            continue;
        }

        if (ch == ',' && depth == 0) {
            return cursor;
        }
    }

    return end;
}

int parse_json_object_entries(const char *content_start, const char *content_end, JsonEntries *entries) {
    entries->items = NULL;
    entries->count = 0;

    const char *cursor = content_start;
    while (cursor < content_end) {
        cursor = skip_json_ws(cursor, content_end);
        if (cursor >= content_end) {
            break;
        }

        if (*cursor == ',') {
            cursor++;
            continue;
        }

        char *key = NULL;
        const char *after_key = NULL;
        if (!parse_json_string_at(cursor, content_end, &key, &after_key)) {
            free_json_entries(entries);
            return 0;
        }

        cursor = skip_json_ws(after_key, content_end);
        if (cursor >= content_end || *cursor != ':') {
            free(key);
            free_json_entries(entries);
            return 0;
        }

        const char *value_start = skip_json_ws(cursor + 1, content_end);
        const char *value_end = find_json_value_end(value_start, content_end);
        const char *raw_end = value_end;
        while (raw_end > value_start && isspace((unsigned char)*(raw_end - 1))) {
            raw_end--;
        }

        char *raw_value = copy_range(value_start, (size_t)(raw_end - value_start));
        if (raw_value == NULL) {
            free(key);
            free_json_entries(entries);
            return 0;
        }

        JsonEntry *new_items = realloc(entries->items, sizeof(JsonEntry) * (entries->count + 1));
        if (new_items == NULL) {
            free(key);
            free(raw_value);
            free_json_entries(entries);
            return 0;
        }

        entries->items = new_items;
        entries->items[entries->count].key = key;
        entries->items[entries->count].raw_value = raw_value;
        entries->count++;

        cursor = value_end;
        if (cursor < content_end && *cursor == ',') {
            cursor++;
        }
    }

    return 1;
}

JsonEntry *find_json_entry(JsonEntries *entries, const char *key) {
    for (size_t i = 0; i < entries->count; i++) {
        if (strcmp(entries->items[i].key, key) == 0) {
            return &entries->items[i];
        }
    }

    return NULL;
}

int parse_json_root_entries(const char *json, JsonEntries *entries) {
    entries->items = NULL;
    entries->count = 0;

    const char *end = json + strlen(json);
    const char *open_brace = skip_json_ws(json, end);
    if (open_brace >= end || *open_brace != '{') {
        return 0;
    }

    const char *close_brace = find_matching_brace(open_brace, end);
    if (close_brace == NULL) {
        return 0;
    }

    const char *after_root = skip_json_ws(close_brace + 1, end);
    if (after_root != end) {
        return 0;
    }

    return parse_json_object_entries(open_brace + 1, close_brace, entries);
}

char *json_raw_to_string(const char *raw_value) {
    const char *end = raw_value + strlen(raw_value);
    const char *value = skip_json_ws(raw_value, end);
    if (value >= end || *value != '"') {
        return NULL;
    }

    char *parsed = NULL;
    const char *after_value = NULL;
    if (!parse_json_string_at(value, end, &parsed, &after_value)) {
        return NULL;
    }

    after_value = skip_json_ws(after_value, end);
    if (after_value != end) {
        free(parsed);
        return NULL;
    }

    return parsed;
}

int json_raw_to_object_entries(const char *raw_value, JsonEntries *entries) {
    entries->items = NULL;
    entries->count = 0;

    const char *end = raw_value + strlen(raw_value);
    const char *open_brace = skip_json_ws(raw_value, end);
    if (open_brace >= end || *open_brace != '{') {
        return 0;
    }

    const char *close_brace = find_matching_brace(open_brace, end);
    if (close_brace == NULL) {
        return 0;
    }

    const char *after_value = skip_json_ws(close_brace + 1, end);
    if (after_value != end) {
        return 0;
    }

    return parse_json_object_entries(open_brace + 1, close_brace, entries);
}

int upsert_json_object_entry(
    const char *path,
    const char *object_key,
    const char *entry_key,
    const char *raw_value,
    const char *entry_indent,
    const char *close_indent
) {
    char *json = read_file_to_string(path);
    if (json == NULL) {
        fprintf(stderr, "lia: failed to read %s\n", path);
        return 1;
    }

    const char *content_start = NULL;
    const char *content_end = NULL;
    if (!find_json_object_bounds(json, object_key, &content_start, &content_end)) {
        fprintf(stderr, "lia: failed to find object '%s' in %s\n", object_key, path);
        free(json);
        return 1;
    }

    JsonEntries entries;
    if (!parse_json_object_entries(content_start, content_end, &entries)) {
        fprintf(stderr, "lia: failed to parse object '%s' in %s\n", object_key, path);
        free(json);
        return 1;
    }

    int found = 0;
    for (size_t i = 0; i < entries.count; i++) {
        if (strcmp(entries.items[i].key, entry_key) == 0) {
            char *copy = duplicate_string(raw_value);
            if (copy == NULL) {
                free_json_entries(&entries);
                free(json);
                return 1;
            }
            free(entries.items[i].raw_value);
            entries.items[i].raw_value = copy;
            found = 1;
            break;
        }
    }

    if (!found) {
        JsonEntry *new_items = realloc(entries.items, sizeof(JsonEntry) * (entries.count + 1));
        if (new_items == NULL) {
            free_json_entries(&entries);
            free(json);
            return 1;
        }

        entries.items = new_items;
        entries.items[entries.count].key = duplicate_string(entry_key);
        entries.items[entries.count].raw_value = duplicate_string(raw_value);
        if (entries.items[entries.count].key == NULL || entries.items[entries.count].raw_value == NULL) {
            free_json_entries(&entries);
            free(json);
            return 1;
        }
        entries.count++;
    }

    char *tmp_path = format_string("%s.tmp", path);
    if (tmp_path == NULL) {
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    FILE *file = fopen(tmp_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "lia: failed to open %s for writing\n", tmp_path);
        free(tmp_path);
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    fwrite(json, 1, (size_t)(content_start - json), file);
    fputc('\n', file);
    for (size_t i = 0; i < entries.count; i++) {
        fputs(entry_indent, file);
        write_json_string(file, entries.items[i].key);
        fputs(": ", file);
        fputs(entries.items[i].raw_value, file);
        if (i + 1 < entries.count) {
            fputc(',', file);
        }
        fputc('\n', file);
    }
    fputs(close_indent, file);
    fputs(content_end, file);

    if (fclose(file) != 0) {
        fprintf(stderr, "lia: failed to write %s\n", tmp_path);
        free(tmp_path);
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "lia: failed to replace %s\n", path);
        free(tmp_path);
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    free(tmp_path);
    free_json_entries(&entries);
    free(json);
    return 0;
}

int remove_json_object_entry(
    const char *path,
    const char *object_key,
    const char *entry_key,
    const char *entry_indent,
    const char *close_indent
) {
    char *json = read_file_to_string(path);
    if (json == NULL) {
        fprintf(stderr, "lia: failed to read %s\n", path);
        return 1;
    }

    const char *content_start = NULL;
    const char *content_end = NULL;
    if (!find_json_object_bounds(json, object_key, &content_start, &content_end)) {
        fprintf(stderr, "lia: failed to find object '%s' in %s\n", object_key, path);
        free(json);
        return 1;
    }

    JsonEntries entries;
    if (!parse_json_object_entries(content_start, content_end, &entries)) {
        fprintf(stderr, "lia: failed to parse object '%s' in %s\n", object_key, path);
        free(json);
        return 1;
    }

    size_t write_index = 0;
    for (size_t read_index = 0; read_index < entries.count; read_index++) {
        if (strcmp(entries.items[read_index].key, entry_key) == 0) {
            free(entries.items[read_index].key);
            free(entries.items[read_index].raw_value);
            continue;
        }

        if (write_index != read_index) {
            entries.items[write_index] = entries.items[read_index];
        }
        write_index++;
    }
    entries.count = write_index;

    char *tmp_path = format_string("%s.tmp", path);
    if (tmp_path == NULL) {
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    FILE *file = fopen(tmp_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "lia: failed to open %s for writing\n", tmp_path);
        free(tmp_path);
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    fwrite(json, 1, (size_t)(content_start - json), file);
    fputc('\n', file);
    for (size_t i = 0; i < entries.count; i++) {
        fputs(entry_indent, file);
        write_json_string(file, entries.items[i].key);
        fputs(": ", file);
        fputs(entries.items[i].raw_value, file);
        if (i + 1 < entries.count) {
            fputc(',', file);
        }
        fputc('\n', file);
    }
    fputs(close_indent, file);
    fputs(content_end, file);

    if (fclose(file) != 0) {
        fprintf(stderr, "lia: failed to write %s\n", tmp_path);
        free(tmp_path);
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    if (rename(tmp_path, path) != 0) {
        fprintf(stderr, "lia: failed to replace %s\n", path);
        free(tmp_path);
        free_json_entries(&entries);
        free(json);
        return 1;
    }

    free(tmp_path);
    free_json_entries(&entries);
    free(json);
    return 0;
}
