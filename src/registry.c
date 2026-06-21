#include "lia.h"

static void print_login_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia login --token <token> [--registry <url>]\n");
}

static void print_publish_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia publish [--registry <url>] [--token <token>]\n");
}

static void print_pack_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia pack\n");
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
    const char *registry_value = registry_env == NULL || registry_env[0] == '\0'
                                     ? "http://127.0.0.1:7788"
                                     : registry_env;
    return trim_trailing_slashes(registry_value);
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

static int upload_publish_archive(const char *registry, const char *token, const char *archive_path) {
    char *url = format_string("%s/publish", registry);
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

int run_publish(int argc, char **argv) {
    const char *registry_override = NULL;
    const char *token_override = NULL;

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

        fprintf(stderr, "lia: unknown publish option: %s\n", argv[i]);
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

    if (upload_publish_archive(registry, token, archive_path) != 0) {
        goto done;
    }

    printf("Published %s@%s to %s\n", manifest.name, manifest.version, registry);
    result = 0;

done:
    free(archive_path);
    free(registry);
    free(token);
    free_lia_credentials(&credentials);
    free_project_manifest(&manifest);
    return result;
}
