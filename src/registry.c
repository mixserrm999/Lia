#include "lia.h"

static void print_login_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia login --token <token> [--registry <url>]\n");
}

static void print_publish_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia publish [--tag <tag>] [--registry <url>] [--token <token>]\n");
}

static void print_pack_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia pack\n");
}

static void print_search_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia search <term> [--registry <url>]\n");
}

static void print_deprecate_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia deprecate <package>@<version> <message> [--registry <url>] [--token <token>]\n");
}

static void print_config_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia config get <key>\n");
    fprintf(stream, "  lia config set <key> <value>\n");
    fprintf(stream, "\n");
    fprintf(stream, "Keys: registry, cache\n");
}

static void print_doctor_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia doctor\n");
}

typedef struct {
    char *registry;
    char *token;
} LiaCredentials;

static void free_lia_credentials(LiaCredentials *credentials) {
    free(credentials->registry);
    free(credentials->token);
    credentials->registry = NULL;
    credentials->token = NULL;
}

char *default_registry_url(void) {
    const char *registry_env = getenv("LIA_REGISTRY_URL");
    if (registry_env != NULL && registry_env[0] != '\0') {
        return trim_trailing_slashes(registry_env);
    }

    char *configured_registry = lia_config_get("registry");
    if (configured_registry != NULL && configured_registry[0] != '\0') {
        char *trimmed = trim_trailing_slashes(configured_registry);
        free(configured_registry);
        return trimmed;
    }
    free(configured_registry);

    return duplicate_string("http://127.0.0.1:7788");
}

static char *lia_credentials_dir(void) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        home = ".";
    }

    return format_string("%s/.lia", home);
}

static char *lia_credentials_path(void) {
    const char *override = getenv("LIA_CREDENTIALS");
    if (override != NULL && override[0] != '\0') {
        return duplicate_string(override);
    }

    char *dir = lia_credentials_dir();
    if (dir == NULL) {
        return NULL;
    }

    char *path = join_path(dir, "credentials.json");
    free(dir);
    return path;
}

static char *lia_config_path(void) {
    const char *override = getenv("LIA_CONFIG");
    if (override != NULL && override[0] != '\0') {
        return duplicate_string(override);
    }

    char *dir = lia_credentials_dir();
    if (dir == NULL) {
        return NULL;
    }

    char *path = join_path(dir, "config.json");
    free(dir);
    return path;
}

static int is_valid_config_key(const char *key) {
    return strcmp(key, "registry") == 0 || strcmp(key, "cache") == 0;
}

char *lia_config_get(const char *key) {
    if (!is_valid_config_key(key)) {
        return NULL;
    }

    char *path = lia_config_path();
    if (path == NULL) {
        return NULL;
    }

    char *content = read_file_to_string(path);
    free(path);
    if (content == NULL) {
        return NULL;
    }

    char *value = json_get_string_value(content, key);
    free(content);
    return value;
}

static int write_lia_config_value(const char *key, const char *value) {
    if (!is_valid_config_key(key) || !is_non_empty_text(value)) {
        fprintf(stderr, "lia: config key/value is invalid\n");
        return 1;
    }

    char *dir = lia_credentials_dir();
    char *path = lia_config_path();
    if (dir == NULL || path == NULL) {
        free(dir);
        free(path);
        return 1;
    }

    if (make_directory_p(dir) != 0) {
        free(dir);
        free(path);
        return 1;
    }

    JsonEntries entries;
    entries.items = NULL;
    entries.count = 0;

    char *content = read_file_to_string(path);
    if (content != NULL && !parse_json_root_entries(content, &entries)) {
        fprintf(stderr, "lia: config file must be a JSON object: %s\n", path);
        free(content);
        free(dir);
        free(path);
        return 1;
    }

    char *quoted_value = json_quote_string(value);
    if (quoted_value == NULL) {
        free_json_entries(&entries);
        free(content);
        free(dir);
        free(path);
        return 1;
    }

    int found = 0;
    for (size_t i = 0; i < entries.count; i++) {
        if (strcmp(entries.items[i].key, key) == 0) {
            char *copy = duplicate_string(quoted_value);
            if (copy == NULL) {
                free(quoted_value);
                free_json_entries(&entries);
                free(content);
                free(dir);
                free(path);
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
            free(quoted_value);
            free_json_entries(&entries);
            free(content);
            free(dir);
            free(path);
            return 1;
        }

        entries.items = new_items;
        entries.items[entries.count].key = duplicate_string(key);
        entries.items[entries.count].raw_value = duplicate_string(quoted_value);
        if (entries.items[entries.count].key == NULL || entries.items[entries.count].raw_value == NULL) {
            free(quoted_value);
            free_json_entries(&entries);
            free(content);
            free(dir);
            free(path);
            return 1;
        }
        entries.count++;
    }

    char *tmp_path = format_string("%s.tmp", path);
    if (tmp_path == NULL) {
        free(quoted_value);
        free_json_entries(&entries);
        free(content);
        free(dir);
        free(path);
        return 1;
    }

    FILE *file = fopen(tmp_path, "wb");
    if (file == NULL) {
        fprintf(stderr, "lia: failed to write config at %s\n", tmp_path);
        free(tmp_path);
        free(quoted_value);
        free_json_entries(&entries);
        free(content);
        free(dir);
        free(path);
        return 1;
    }

    fputs("{\n", file);
    for (size_t i = 0; i < entries.count; i++) {
        fputs("  ", file);
        write_json_string(file, entries.items[i].key);
        fputs(": ", file);
        fputs(entries.items[i].raw_value, file);
        if (i + 1 < entries.count) {
            fputc(',', file);
        }
        fputc('\n', file);
    }
    fputs("}\n", file);

    int result = fclose(file) == 0 ? 0 : 1;
    if (result == 0 && rename(tmp_path, path) != 0) {
        fprintf(stderr, "lia: failed to replace config at %s\n", path);
        result = 1;
    }

    if (result == 0) {
        printf("Set %s\n", key);
    }

    free(tmp_path);
    free(quoted_value);
    free_json_entries(&entries);
    free(content);
    free(dir);
    free(path);
    return result;
}

int run_config(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_config_usage(stdout);
        return 0;
    }

    if (argc < 4) {
        print_config_usage(stderr);
        return 2;
    }

    const char *action = argv[2];
    const char *key = argv[3];
    if (!is_valid_config_key(key)) {
        fprintf(stderr, "lia: unknown config key '%s'\n", key);
        return 2;
    }

    if (strcmp(action, "get") == 0) {
        if (argc != 4) {
            print_config_usage(stderr);
            return 2;
        }

        char *value = lia_config_get(key);
        if (value == NULL) {
            fprintf(stderr, "lia: config key '%s' is not set\n", key);
            return 1;
        }

        printf("%s\n", value);
        free(value);
        return 0;
    }

    if (strcmp(action, "set") == 0) {
        if (argc != 5) {
            print_config_usage(stderr);
            return 2;
        }

        return write_lia_config_value(key, argv[4]);
    }

    fprintf(stderr, "lia: unknown config action '%s'\n", action);
    return 2;
}

static int command_available(const char *command_name) {
    char *quoted = shell_quote(command_name);
    if (quoted == NULL) {
        return 0;
    }

    char *command = format_string("command -v %s >/dev/null 2>&1", quoted);
    free(quoted);
    if (command == NULL) {
        return 0;
    }

    int result = system(command) == 0;
    free(command);
    return result;
}

static void print_command_check(const char *command_name) {
    printf("%s: %s\n", command_name, command_available(command_name) ? "ok" : "missing");
}

static int doctor_lockfile(void) {
    if (!file_exists(LIA_LOCK_FILE)) {
        printf("lockfile: not found\n");
        return 0;
    }

    char *content = read_file_to_string(LIA_LOCK_FILE);
    if (content == NULL) {
        printf("lockfile: unreadable\n");
        return 1;
    }

    JsonEntries root;
    if (!parse_json_root_entries(content, &root)) {
        printf("lockfile: invalid JSON\n");
        free(content);
        return 1;
    }

    JsonEntry *version = find_json_entry(&root, "lockfileVersion");
    int result = 0;
    if (version == NULL) {
        printf("lockfile: upgrade available\n");
    } else if (strcmp(version->raw_value, "1") == 0) {
        printf("lockfile: v1\n");
    } else {
        printf("lockfile: unsupported version %s\n", version->raw_value);
        result = 1;
    }

    free_json_entries(&root);
    free(content);
    return result;
}

int run_doctor(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_doctor_usage(stdout);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "lia: doctor accepts no arguments\n");
        return 2;
    }

    int result = 0;
    printf("lia: %s\n", LIA_VERSION);
    print_command_check("curl");
    print_command_check("tar");
    print_command_check("unzip");
    print_command_check("sha256sum");

    if (file_exists(LIA_MANIFEST_FILE)) {
        ProjectManifest manifest;
        if (load_project_manifest(&manifest) == 0) {
            printf("manifest: ok (%s@%s)\n", manifest.name, manifest.version);
            free_project_manifest(&manifest);
        } else {
            printf("manifest: invalid\n");
            result = 1;
        }
    } else {
        printf("manifest: not found\n");
    }

    if (doctor_lockfile() != 0) {
        result = 1;
    }

    char *registry = default_registry_url();
    if (registry != NULL) {
        char *health_url = format_string("%s/health", registry);
        char *quoted_url = health_url == NULL ? NULL : shell_quote(health_url);
        char *command = quoted_url == NULL ? NULL : format_string("curl -fsSL %s >/dev/null 2>&1", quoted_url);
        int registry_ok = command != NULL && system(command) == 0;
        printf("registry: %s (%s)\n", registry_ok ? "ok" : "unreachable", registry);
        free(command);
        free(quoted_url);
        free(health_url);
        free(registry);
    }

    return result;
}

static int read_lia_credentials(LiaCredentials *credentials) {
    credentials->registry = NULL;
    credentials->token = NULL;

    char *path = lia_credentials_path();
    if (path == NULL) {
        return 1;
    }

    char *content = read_file_to_string(path);
    free(path);
    if (content == NULL) {
        return 0;
    }

    credentials->registry = json_get_string_value(content, "registry");
    credentials->token = json_get_string_value(content, "token");
    free(content);
    return 0;
}

static int write_lia_credentials(const char *registry, const char *token) {
    char *dir = lia_credentials_dir();
    char *path = lia_credentials_path();
    if (dir == NULL || path == NULL) {
        free(dir);
        free(path);
        return 1;
    }

    if (make_directory_p(dir) != 0) {
        free(dir);
        free(path);
        return 1;
    }

    FILE *file = fopen(path, "w");
    if (file == NULL) {
        fprintf(stderr, "lia: failed to write credentials at %s\n", path);
        free(dir);
        free(path);
        return 1;
    }

    fputs("{\n", file);
    fputs("  \"registry\": ", file);
    write_json_string(file, registry);
    fputs(",\n", file);
    fputs("  \"token\": ", file);
    write_json_string(file, token);
    fputs("\n", file);
    fputs("}\n", file);

    int result = fclose(file) == 0 ? 0 : 1;
    if (result != 0) {
        fprintf(stderr, "lia: failed to save credentials at %s\n", path);
    }

    free(dir);
    free(path);
    return result;
}

static int registry_auth_check(const char *registry, const char *token) {
    char *url = format_string("%s/auth/check", registry);
    char *header = format_string("Authorization: Bearer %s", token);
    if (url == NULL || header == NULL) {
        free(url);
        free(header);
        return 1;
    }

    char *quoted_url = shell_quote(url);
    char *quoted_header = shell_quote(header);
    free(url);
    free(header);
    if (quoted_url == NULL || quoted_header == NULL) {
        free(quoted_url);
        free(quoted_header);
        return 1;
    }

    char *command = format_string("curl -fsSL -H %s %s >/dev/null", quoted_header, quoted_url);
    free(quoted_url);
    free(quoted_header);
    if (command == NULL) {
        return 1;
    }

    int status = system(command);
    free(command);
    if (status != 0) {
        fprintf(stderr, "lia: registry authentication failed\n");
        return 1;
    }

    return 0;
}

int run_login(int argc, char **argv) {
    const char *token = NULL;
    char *registry = NULL;

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_login_usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--token") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --token requires a value\n");
                free(registry);
                return 2;
            }
            token = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--token=", 8) == 0) {
            token = argv[i] + 8;
            continue;
        }

        if (strcmp(argv[i], "--registry") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --registry requires a value\n");
                free(registry);
                return 2;
            }
            free(registry);
            registry = trim_trailing_slashes(argv[++i]);
            continue;
        }

        if (strncmp(argv[i], "--registry=", 11) == 0) {
            free(registry);
            registry = trim_trailing_slashes(argv[i] + 11);
            continue;
        }

        fprintf(stderr, "lia: unknown login option: %s\n", argv[i]);
        free(registry);
        return 2;
    }

    if (token == NULL || token[0] == '\0') {
        print_login_usage(stderr);
        free(registry);
        return 2;
    }

    if (registry == NULL) {
        registry = default_registry_url();
    }
    if (registry == NULL) {
        return 1;
    }

    if (registry_auth_check(registry, token) != 0 ||
        write_lia_credentials(registry, token) != 0) {
        free(registry);
        return 1;
    }

    printf("Logged in to %s\n", registry);
    free(registry);
    return 0;
}

static char *resolve_publish_registry(const char *override_registry, LiaCredentials *credentials) {
    if (override_registry != NULL) {
        return trim_trailing_slashes(override_registry);
    }

    const char *registry_env = getenv("LIA_REGISTRY_URL");
    if (registry_env != NULL && registry_env[0] != '\0') {
        return trim_trailing_slashes(registry_env);
    }

    if (credentials->registry != NULL && credentials->registry[0] != '\0') {
        return trim_trailing_slashes(credentials->registry);
    }

    return default_registry_url();
}

static char *resolve_publish_token(const char *override_token, LiaCredentials *credentials) {
    if (override_token != NULL) {
        return duplicate_string(override_token);
    }

    const char *token_env = getenv("LIA_REGISTRY_TOKEN");
    if (token_env != NULL && token_env[0] != '\0') {
        return duplicate_string(token_env);
    }

    if (credentials->token != NULL && credentials->token[0] != '\0') {
        return duplicate_string(credentials->token);
    }

    return NULL;
}

static int create_package_archive(ProjectManifest *manifest, const char *work_dir, char **archive_path) {
    if (remove_tree(work_dir) != 0 ||
        make_directory_p(work_dir) != 0) {
        return 1;
    }

    *archive_path = format_string("%s/%s-%s.tar.gz", work_dir, manifest->name, manifest->version);
    if (*archive_path == NULL) {
        return 1;
    }

    return run_shell_command_1(
        "tar --exclude='./.git' --exclude='./.lia' --exclude='./packages' --exclude='./build' --exclude='./third_party' -czf %s .",
        *archive_path
    );
}

static char *url_encode_component(const char *value) {
    size_t length = 0;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (isalnum(*cursor) || *cursor == '-' || *cursor == '_' || *cursor == '.' || *cursor == '~') {
            length += 1;
        } else {
            length += 3;
        }
    }

    char *encoded = malloc(length + 1);
    if (encoded == NULL) {
        return NULL;
    }

    char *out = encoded;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (isalnum(*cursor) || *cursor == '-' || *cursor == '_' || *cursor == '.' || *cursor == '~') {
            *out++ = (char)*cursor;
        } else {
            snprintf(out, 4, "%%%02X", *cursor);
            out += 3;
        }
    }
    *out = '\0';
    return encoded;
}

int run_search(int argc, char **argv) {
    const char *term = NULL;
    const char *registry_override = NULL;

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_search_usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--registry") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --registry requires a value\n");
                return 2;
            }
            registry_override = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--registry=", 11) == 0) {
            registry_override = argv[i] + 11;
            continue;
        }

        if (term != NULL) {
            print_search_usage(stderr);
            return 2;
        }
        term = argv[i];
    }

    if (term == NULL || term[0] == '\0') {
        print_search_usage(stderr);
        return 2;
    }

    char *registry = registry_override == NULL ? default_registry_url() : trim_trailing_slashes(registry_override);
    char *encoded_term = url_encode_component(term);
    if (registry == NULL || encoded_term == NULL) {
        free(registry);
        free(encoded_term);
        return 1;
    }

    char *url = format_string("%s/search?q=%s&format=text", registry, encoded_term);
    free(registry);
    free(encoded_term);
    if (url == NULL) {
        return 1;
    }

    char *results = fetch_url_to_string(url);
    if (results == NULL) {
        fprintf(stderr, "lia: failed to search registry: %s\n", url);
        free(url);
        return 1;
    }

    if (results[0] == '\0') {
        printf("No packages found\n");
    } else {
        fputs(results, stdout);
        if (results[strlen(results) - 1] != '\n') {
            fputc('\n', stdout);
        }
    }

    free(results);
    free(url);
    return 0;
}

int run_pack(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_pack_usage(stdout);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "lia: pack accepts no arguments\n");
        return 2;
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    if (!file_exists(manifest.main_file)) {
        fprintf(stderr, "lia: pack main file does not exist: %s\n", manifest.main_file);
        free_project_manifest(&manifest);
        return 1;
    }

    char *temp_archive = NULL;
    char *archive_name = format_string("%s-%s.tar.gz", manifest.name, manifest.version);
    int result = 1;
    if (archive_name == NULL) {
        goto done;
    }

    if (create_package_archive(&manifest, ".lia/tmp/pack", &temp_archive) != 0 ||
        copy_file_path(temp_archive, archive_name) != 0) {
        goto done;
    }

    printf("Packed %s@%s to %s\n", manifest.name, manifest.version, archive_name);
    result = 0;

done:
    free(temp_archive);
    free(archive_name);
    free_project_manifest(&manifest);
    return result;
}

static int upload_publish_archive(const char *registry, const char *token, const char *archive_path, const char *tag) {
    char *encoded_tag = url_encode_component(tag == NULL || tag[0] == '\0' ? "latest" : tag);
    char *url = encoded_tag == NULL ? NULL : format_string("%s/publish?tag=%s", registry, encoded_tag);
    free(encoded_tag);
    char *header = format_string("Authorization: Bearer %s", token);
    char *content_type = duplicate_string("Content-Type: application/gzip");
    if (url == NULL || header == NULL || content_type == NULL) {
        free(url);
        free(header);
        free(content_type);
        return 1;
    }

    char *quoted_url = shell_quote(url);
    char *quoted_header = shell_quote(header);
    char *quoted_content_type = shell_quote(content_type);
    char *quoted_archive_path = shell_quote(archive_path);
    free(url);
    free(header);
    free(content_type);
    if (quoted_url == NULL || quoted_header == NULL || quoted_content_type == NULL || quoted_archive_path == NULL) {
        free(quoted_url);
        free(quoted_header);
        free(quoted_content_type);
        free(quoted_archive_path);
        return 1;
    }

    char *command = format_string(
        "curl -fsSL -X PUT -H %s -H %s -T %s %s >/dev/null",
        quoted_header,
        quoted_content_type,
        quoted_archive_path,
        quoted_url
    );

    free(quoted_url);
    free(quoted_header);
    free(quoted_content_type);
    free(quoted_archive_path);
    if (command == NULL) {
        return 1;
    }

    int status = system(command);
    free(command);
    if (status != 0) {
        fprintf(stderr, "lia: publish failed\n");
        return 1;
    }

    return 0;
}

static int parse_package_version_spec(const char *spec, char **name, char **version) {
    *name = NULL;
    *version = NULL;

    const char *at = strrchr(spec, '@');
    if (at == NULL || at == spec || at[1] == '\0') {
        return 0;
    }

    *name = copy_range(spec, (size_t)(at - spec));
    *version = duplicate_string(at + 1);
    if (*name == NULL || *version == NULL) {
        free(*name);
        free(*version);
        *name = NULL;
        *version = NULL;
        return 0;
    }

    if (!is_valid_package_name(*name) || !is_non_empty_text(*version) ||
        strchr(*version, '/') != NULL || strchr(*version, '\\') != NULL) {
        free(*name);
        free(*version);
        *name = NULL;
        *version = NULL;
        return 0;
    }

    return 1;
}

int run_deprecate(int argc, char **argv) {
    const char *registry_override = NULL;
    const char *token_override = NULL;
    const char *spec = NULL;
    const char *message = NULL;

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_deprecate_usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--registry") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --registry requires a value\n");
                return 2;
            }
            registry_override = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--registry=", 11) == 0) {
            registry_override = argv[i] + 11;
            continue;
        }

        if (strcmp(argv[i], "--token") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --token requires a value\n");
                return 2;
            }
            token_override = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--token=", 8) == 0) {
            token_override = argv[i] + 8;
            continue;
        }

        if (spec == NULL) {
            spec = argv[i];
            continue;
        }

        if (message == NULL) {
            message = argv[i];
            continue;
        }

        print_deprecate_usage(stderr);
        return 2;
    }

    if (spec == NULL || message == NULL || message[0] == '\0') {
        print_deprecate_usage(stderr);
        return 2;
    }

    char *name = NULL;
    char *version = NULL;
    if (!parse_package_version_spec(spec, &name, &version)) {
        fprintf(stderr, "lia: package spec must look like package@version\n");
        return 2;
    }

    LiaCredentials credentials;
    if (read_lia_credentials(&credentials) != 0) {
        free(name);
        free(version);
        return 1;
    }

    char *registry = resolve_publish_registry(registry_override, &credentials);
    char *token = resolve_publish_token(token_override, &credentials);
    if (registry == NULL || token == NULL) {
        fprintf(stderr, "lia: login required; run `lia login --token <token>` or set LIA_REGISTRY_TOKEN\n");
        free(registry);
        free(token);
        free_lia_credentials(&credentials);
        free(name);
        free(version);
        return 1;
    }

    char *encoded_name = url_encode_component(name);
    char *encoded_version = url_encode_component(version);
    char *url = encoded_name == NULL || encoded_version == NULL
                    ? NULL
                    : format_string("%s/deprecate/%s/%s", registry, encoded_name, encoded_version);
    char *header = format_string("Authorization: Bearer %s", token);
    char *content_type = duplicate_string("Content-Type: text/plain");
    char *quoted_url = url == NULL ? NULL : shell_quote(url);
    char *quoted_header = header == NULL ? NULL : shell_quote(header);
    char *quoted_content_type = content_type == NULL ? NULL : shell_quote(content_type);
    char *quoted_message = shell_quote(message);
    char *command = NULL;

    int result = 1;
    if (quoted_url != NULL && quoted_header != NULL && quoted_content_type != NULL && quoted_message != NULL) {
        command = format_string(
            "curl -fsSL -X PUT -H %s -H %s --data-binary %s %s >/dev/null",
            quoted_header,
            quoted_content_type,
            quoted_message,
            quoted_url
        );
    }

    if (command == NULL || system(command) != 0) {
        fprintf(stderr, "lia: deprecate failed\n");
    } else {
        printf("Deprecated %s@%s\n", name, version);
        result = 0;
    }

    free(command);
    free(quoted_message);
    free(quoted_content_type);
    free(quoted_header);
    free(quoted_url);
    free(content_type);
    free(header);
    free(url);
    free(encoded_version);
    free(encoded_name);
    free(registry);
    free(token);
    free_lia_credentials(&credentials);
    free(name);
    free(version);
    return result;
}

int run_publish(int argc, char **argv) {
    const char *registry_override = NULL;
    const char *token_override = NULL;
    const char *tag = "latest";

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_publish_usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--registry") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --registry requires a value\n");
                return 2;
            }
            registry_override = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--registry=", 11) == 0) {
            registry_override = argv[i] + 11;
            continue;
        }

        if (strcmp(argv[i], "--token") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --token requires a value\n");
                return 2;
            }
            token_override = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--token=", 8) == 0) {
            token_override = argv[i] + 8;
            continue;
        }

        if (strcmp(argv[i], "--tag") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --tag requires a value\n");
                return 2;
            }
            tag = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--tag=", 6) == 0) {
            tag = argv[i] + 6;
            continue;
        }

        fprintf(stderr, "lia: unknown publish option: %s\n", argv[i]);
        return 2;
    }

    if (!is_valid_package_name(tag)) {
        fprintf(stderr, "lia: invalid publish tag '%s'\n", tag);
        return 2;
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    if (!file_exists(manifest.main_file)) {
        fprintf(stderr, "lia: publish main file does not exist: %s\n", manifest.main_file);
        free_project_manifest(&manifest);
        return 1;
    }

    LiaCredentials credentials;
    if (read_lia_credentials(&credentials) != 0) {
        free_project_manifest(&manifest);
        return 1;
    }

    char *registry = resolve_publish_registry(registry_override, &credentials);
    char *token = resolve_publish_token(token_override, &credentials);
    if (registry == NULL || token == NULL) {
        fprintf(stderr, "lia: login required; run `lia login --token <token>` or set LIA_REGISTRY_TOKEN\n");
        free(registry);
        free(token);
        free_lia_credentials(&credentials);
        free_project_manifest(&manifest);
        return 1;
    }

    char *archive_path = NULL;
    int result = 1;
    if (create_package_archive(&manifest, ".lia/tmp/publish", &archive_path) != 0) {
        goto done;
    }

    if (upload_publish_archive(registry, token, archive_path, tag) != 0) {
        goto done;
    }

    printf("Published %s@%s to %s with tag %s\n", manifest.name, manifest.version, registry, tag);
    result = 0;

done:
    free(archive_path);
    free(registry);
    free(token);
    free_lia_credentials(&credentials);
    free_project_manifest(&manifest);
    return result;
}
