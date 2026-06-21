#include "lia.h"

static void print_install_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia install [--save-dev] [--production] [source]\n");
    fprintf(stream, "\n");
    fprintf(stream, "Without a source, installs packages from %s.\n", LIA_LOCK_FILE);
    fprintf(stream, "--save-dev records a direct package in devDependencies.\n");
    fprintf(stream, "--production skips lockfile packages marked as dev dependencies.\n");
    fprintf(stream, "\n");
    fprintf(stream, "Sources:\n");
    fprintf(stream, "  package-name[@version] from $LIA_REGISTRY_URL\n");
    fprintf(stream, "  github:owner/repo@ref\n");
    fprintf(stream, "  https://example.com/package.tar.gz\n");
    fprintf(stream, "  https://example.com/package.tgz\n");
    fprintf(stream, "  https://example.com/package.zip\n");
    fprintf(stream, "  ./local-package.tar.gz\n");
}

static void print_ci_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia ci [--production]\n");
    fprintf(stream, "\n");
    fprintf(stream, "Strictly restores %s after checking it matches %s.\n", LIA_LOCK_FILE, LIA_MANIFEST_FILE);
}

static void print_update_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia update [package]\n");
}

typedef enum {
    ARCHIVE_TAR_GZ,
    ARCHIVE_ZIP
} ArchiveType;

typedef struct {
    char *archive_source;
    ArchiveType archive_type;
    int is_local_path;
} ResolvedInstallSource;

int semver_satisfies(const char *version_text, const char *constraint);
int looks_like_semver_constraint(const char *value);
static int is_constraint_only_dependency(const char *source);

typedef struct {
    char **items;
    size_t count;
} StringList;

typedef struct {
    StringList active;
    StringList resolved;
    int workspace_counter;
    int install_dependencies;
    int dev_dependency;
} InstallContext;

static void free_resolved_install_source(ResolvedInstallSource *source) {
    free(source->archive_source);
    source->archive_source = NULL;
    source->is_local_path = 0;
}

void free_dependency_spec(DependencySpec *spec) {
    free(spec->source);
    free(spec->constraint);
    spec->source = NULL;
    spec->constraint = NULL;
}

static int string_list_contains(StringList *list, const char *value) {
    for (size_t i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], value) == 0) {
            return 1;
        }
    }

    return 0;
}

static int string_list_push(StringList *list, const char *value) {
    char **new_items = realloc(list->items, sizeof(char *) * (list->count + 1));
    if (new_items == NULL) {
        return 1;
    }

    list->items = new_items;
    list->items[list->count] = duplicate_string(value);
    if (list->items[list->count] == NULL) {
        return 1;
    }

    list->count++;
    return 0;
}

static void string_list_pop(StringList *list) {
    if (list->count == 0) {
        return;
    }

    free(list->items[list->count - 1]);
    list->count--;
}

static void free_string_list(StringList *list) {
    for (size_t i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

static void init_install_context(InstallContext *context) {
    context->active.items = NULL;
    context->active.count = 0;
    context->resolved.items = NULL;
    context->resolved.count = 0;
    context->workspace_counter = 0;
    context->install_dependencies = 1;
    context->dev_dependency = 0;
}

static void free_install_context(InstallContext *context) {
    free_string_list(&context->active);
    free_string_list(&context->resolved);
    context->workspace_counter = 0;
    context->install_dependencies = 1;
    context->dev_dependency = 0;
}

int parse_dependency_spec(const char *raw_value, DependencySpec *spec) {
    spec->source = NULL;
    spec->constraint = NULL;

    const char *hash = strrchr(raw_value, '#');
    if (hash != NULL && hash[1] != '\0' && looks_like_semver_constraint(hash + 1)) {
        spec->source = copy_range(raw_value, (size_t)(hash - raw_value));
        spec->constraint = duplicate_string(hash + 1);
    } else {
        spec->source = duplicate_string(raw_value);
    }

    if (spec->source == NULL || (hash != NULL && spec->constraint == NULL && looks_like_semver_constraint(hash + 1))) {
        free_dependency_spec(spec);
        return 1;
    }

    if (spec->constraint != NULL) {
        int constraint_check = semver_satisfies("0.0.0", spec->constraint);
        if (constraint_check == -1) {
            fprintf(stderr, "lia: invalid semver constraint '%s'\n", spec->constraint);
            free_dependency_spec(spec);
            return 1;
        }
    }

    return 0;
}

typedef struct {
    int major;
    int minor;
    int patch;
} SemVer;

static int parse_semver(const char *value, SemVer *version) {
    const char *cursor = value;
    if (*cursor == 'v' || *cursor == 'V') {
        cursor++;
    }

    if (!isdigit((unsigned char)*cursor)) {
        return 0;
    }

    char *end = NULL;
    long major = strtol(cursor, &end, 10);
    if (end == cursor || *end != '.') {
        return 0;
    }

    cursor = end + 1;
    long minor = strtol(cursor, &end, 10);
    if (end == cursor || *end != '.') {
        return 0;
    }

    cursor = end + 1;
    long patch = strtol(cursor, &end, 10);
    if (end == cursor) {
        return 0;
    }

    if (*end != '\0' && *end != '-' && *end != '+') {
        return 0;
    }

    if (major < 0 || minor < 0 || patch < 0) {
        return 0;
    }

    version->major = (int)major;
    version->minor = (int)minor;
    version->patch = (int)patch;
    return 1;
}

static int compare_semver(SemVer left, SemVer right) {
    if (left.major != right.major) {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor) {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.patch != right.patch) {
        return left.patch < right.patch ? -1 : 1;
    }
    return 0;
}

static int semver_in_range(SemVer version, SemVer lower, SemVer upper_exclusive) {
    return compare_semver(version, lower) >= 0 && compare_semver(version, upper_exclusive) < 0;
}

static int semver_satisfies_token(SemVer version, const char *token) {
    if (strcmp(token, "*") == 0) {
        return 1;
    }

    if (token[0] == '^') {
        SemVer lower;
        if (!parse_semver(token + 1, &lower)) {
            return -1;
        }

        SemVer upper = lower;
        if (lower.major > 0) {
            upper.major++;
            upper.minor = 0;
            upper.patch = 0;
        } else if (lower.minor > 0) {
            upper.minor++;
            upper.patch = 0;
        } else {
            upper.patch++;
        }

        return semver_in_range(version, lower, upper);
    }

    if (token[0] == '~') {
        SemVer lower;
        if (!parse_semver(token + 1, &lower)) {
            return -1;
        }

        SemVer upper = lower;
        upper.minor++;
        upper.patch = 0;
        return semver_in_range(version, lower, upper);
    }

    const char *version_text = token;
    int comparison = 0;
    if (starts_with(token, ">=")) {
        comparison = 1;
        version_text = token + 2;
    } else if (starts_with(token, "<=")) {
        comparison = 2;
        version_text = token + 2;
    } else if (starts_with(token, ">")) {
        comparison = 3;
        version_text = token + 1;
    } else if (starts_with(token, "<")) {
        comparison = 4;
        version_text = token + 1;
    } else if (starts_with(token, "=")) {
        comparison = 0;
        version_text = token + 1;
    }

    SemVer required;
    if (!parse_semver(version_text, &required)) {
        return -1;
    }

    int cmp = compare_semver(version, required);
    switch (comparison) {
        case 1:
            return cmp >= 0;
        case 2:
            return cmp <= 0;
        case 3:
            return cmp > 0;
        case 4:
            return cmp < 0;
        default:
            return cmp == 0;
    }
}

int semver_satisfies(const char *version_text, const char *constraint) {
    if (constraint == NULL || constraint[0] == '\0' || strcmp(constraint, "*") == 0) {
        return 1;
    }

    SemVer version;
    if (!parse_semver(version_text, &version)) {
        return -1;
    }

    char *constraint_copy = duplicate_string(constraint);
    if (constraint_copy == NULL) {
        return -1;
    }

    int result = 1;
    char *token = strtok(constraint_copy, " \t\r\n");
    while (token != NULL) {
        int token_result = semver_satisfies_token(version, token);
        if (token_result <= 0) {
            result = token_result;
            break;
        }
        token = strtok(NULL, " \t\r\n");
    }

    free(constraint_copy);
    return result;
}

int looks_like_semver_constraint(const char *value) {
    if (value == NULL || value[0] == '\0') {
        return 0;
    }

    return value[0] == '*' ||
           value[0] == '^' ||
           value[0] == '~' ||
           value[0] == '<' ||
           value[0] == '>' ||
           value[0] == '=' ||
           value[0] == 'v' ||
           value[0] == 'V' ||
           isdigit((unsigned char)value[0]);
}

static int detect_archive_type(const char *source, ArchiveType *archive_type) {
    if (archive_suffix_matches(source, ".tar.gz") || archive_suffix_matches(source, ".tgz")) {
        *archive_type = ARCHIVE_TAR_GZ;
        return 1;
    }

    if (archive_suffix_matches(source, ".zip")) {
        *archive_type = ARCHIVE_ZIP;
        return 1;
    }

    return 0;
}

static int resolve_github_source(const char *source, ResolvedInstallSource *resolved) {
    const char *spec = source + strlen("github:");
    const char *at = strchr(spec, '@');
    size_t repo_spec_length = at == NULL ? strlen(spec) : (size_t)(at - spec);

    char *repo_spec = copy_range(spec, repo_spec_length);
    char *ref = at == NULL ? duplicate_string("main") : duplicate_string(at + 1);
    if (repo_spec == NULL || ref == NULL) {
        free(repo_spec);
        free(ref);
        return 1;
    }

    char *slash = strchr(repo_spec, '/');
    if (slash == NULL || slash == repo_spec || slash[1] == '\0' || strchr(slash + 1, '/') != NULL) {
        fprintf(stderr, "lia: GitHub source must look like github:owner/repo@ref\n");
        free(repo_spec);
        free(ref);
        return 1;
    }

    *slash = '\0';
    const char *owner = repo_spec;
    const char *repo = slash + 1;

    if (!is_valid_github_part(owner, 0) ||
        !is_valid_github_part(repo, 0) ||
        !is_valid_github_part(ref, 1)) {
        fprintf(stderr, "lia: invalid GitHub source: %s\n", source);
        free(repo_spec);
        free(ref);
        return 1;
    }

    resolved->archive_source = format_string("https://codeload.github.com/%s/%s/tar.gz/%s", owner, repo, ref);
    resolved->archive_type = ARCHIVE_TAR_GZ;
    resolved->is_local_path = 0;

    free(repo_spec);
    free(ref);

    if (resolved->archive_source == NULL) {
        return 1;
    }

    return 0;
}

static int parse_registry_source(const char *source, char **name, char **version) {
    const char *at = strchr(source, '@');
    if (at == source || (at != NULL && at[1] == '\0')) {
        return 0;
    }

    if (at == NULL) {
        *name = duplicate_string(source);
        *version = duplicate_string("latest");
    } else {
        *name = copy_range(source, (size_t)(at - source));
        *version = duplicate_string(at + 1);
    }

    if (*name == NULL || *version == NULL) {
        free(*name);
        free(*version);
        *name = NULL;
        *version = NULL;
        return 0;
    }

    if (!is_valid_package_name(*name) || !is_non_empty_text(*version)) {
        free(*name);
        free(*version);
        *name = NULL;
        *version = NULL;
        return 0;
    }

    for (const unsigned char *cursor = (const unsigned char *)*version; *cursor != '\0'; cursor++) {
        if (isspace(*cursor) ||
            *cursor == '/' ||
            *cursor == '\\' ||
            *cursor == '"' ||
            *cursor == '\'' ||
            *cursor == '#') {
            free(*name);
            free(*version);
            *name = NULL;
            *version = NULL;
            return 0;
        }
    }

    return 1;
}

static const char *skip_inline_ws(const char *cursor, const char *end) {
    while (cursor < end && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    return cursor;
}

static char *parse_json_array_string_item(const char **cursor, const char *end) {
    const char *start = skip_inline_ws(*cursor, end);
    if (start >= end || *start != '"') {
        return NULL;
    }

    const char *scan = start + 1;
    int escaped = 0;
    while (scan < end) {
        if (escaped) {
            escaped = 0;
        } else if (*scan == '\\') {
            escaped = 1;
        } else if (*scan == '"') {
            char *raw = copy_range(start, (size_t)(scan - start + 1));
            if (raw == NULL) {
                return NULL;
            }
            char *value = json_raw_to_string(raw);
            free(raw);
            *cursor = scan + 1;
            return value;
        }
        scan++;
    }

    return NULL;
}

static int choose_higher_semver(const char *candidate, const char *current) {
    SemVer candidate_version;
    SemVer current_version;

    if (!parse_semver(candidate, &candidate_version)) {
        return 0;
    }

    if (current == NULL || !parse_semver(current, &current_version)) {
        return 1;
    }

    return compare_semver(candidate_version, current_version) > 0;
}

static char *resolve_registry_version_from_index(const char *metadata, const char *requested_version) {
    if (strcmp(requested_version, "latest") == 0) {
        return json_get_string_value(metadata, "latest");
    }

    JsonEntries root;
    if (!parse_json_root_entries(metadata, &root)) {
        return NULL;
    }

    JsonEntry *versions_entry = find_json_entry(&root, "versions");
    if (versions_entry == NULL) {
        free_json_entries(&root);
        return NULL;
    }

    const char *raw = versions_entry->raw_value;
    const char *cursor = skip_inline_ws(raw, raw + strlen(raw));
    const char *end = raw + strlen(raw);
    if (cursor >= end || *cursor != '[') {
        free_json_entries(&root);
        return NULL;
    }
    cursor++;

    char *best = NULL;
    int valid = 1;
    while (cursor < end) {
        cursor = skip_inline_ws(cursor, end);
        if (cursor < end && *cursor == ']') {
            break;
        }

        char *candidate = parse_json_array_string_item(&cursor, end);
        if (candidate == NULL) {
            valid = 0;
            break;
        }

        int satisfies = semver_satisfies(candidate, requested_version);
        if (satisfies > 0 && choose_higher_semver(candidate, best)) {
            free(best);
            best = duplicate_string(candidate);
            if (best == NULL) {
                free(candidate);
                valid = 0;
                break;
            }
        }

        free(candidate);
        cursor = skip_inline_ws(cursor, end);
        if (cursor < end && *cursor == ',') {
            cursor++;
            continue;
        }
        if (cursor < end && *cursor == ']') {
            break;
        }
    }

    free_json_entries(&root);
    if (!valid) {
        free(best);
        return NULL;
    }

    return best;
}

static char *resolve_registry_version(const char *registry_url, const char *name, const char *requested_version) {
    if (strcmp(requested_version, "latest") != 0 &&
        semver_satisfies("0.0.0", requested_version) == -1) {
        return duplicate_string(requested_version);
    }

    char *index_url = format_string("%s/packages/%s", registry_url, name);
    if (index_url == NULL) {
        return NULL;
    }

    char *metadata = fetch_url_to_string(index_url);
    if (metadata == NULL) {
        fprintf(stderr, "lia: failed to fetch registry metadata: %s\n", index_url);
        free(index_url);
        return NULL;
    }

    char *resolved_version = resolve_registry_version_from_index(metadata, requested_version);
    if (resolved_version == NULL) {
        fprintf(stderr, "lia: no registry version of %s satisfies '%s'\n", name, requested_version);
    }

    free(metadata);
    free(index_url);
    return resolved_version;
}

static int is_registry_source(const char *source) {
    if (starts_with(source, "github:") ||
        starts_with(source, "http://") ||
        starts_with(source, "https://") ||
        starts_with(source, "file://") ||
        strchr(source, '/') != NULL ||
        strchr(source, '\\') != NULL) {
        return 0;
    }

    ArchiveType archive_type;
    if (detect_archive_type(source, &archive_type) || is_constraint_only_dependency(source)) {
        return 0;
    }

    char *name = NULL;
    char *version = NULL;
    int result = parse_registry_source(source, &name, &version);
    free(name);
    free(version);
    return result;
}

static int resolve_registry_source(const char *source, ResolvedInstallSource *resolved) {
    char *name = NULL;
    char *version = NULL;
    if (!parse_registry_source(source, &name, &version)) {
        fprintf(stderr, "lia: registry source must look like package-name or package-name@version\n");
        return 1;
    }

    const char *registry_env = getenv("LIA_REGISTRY_URL");
    const char *registry_value = registry_env == NULL || registry_env[0] == '\0'
                                     ? "http://127.0.0.1:7788"
                                     : registry_env;
    char *registry_url = trim_trailing_slashes(registry_value);
    if (registry_url == NULL) {
        free(name);
        free(version);
        return 1;
    }

    char *resolved_version = resolve_registry_version(registry_url, name, version);
    if (resolved_version == NULL) {
        free(registry_url);
        free(name);
        free(version);
        return 1;
    }

    char *metadata_url = format_string("%s/packages/%s/%s", registry_url, name, resolved_version);
    free(registry_url);
    free(resolved_version);
    if (metadata_url == NULL) {
        free(name);
        free(version);
        return 1;
    }

    char *metadata = fetch_url_to_string(metadata_url);
    if (metadata == NULL) {
        fprintf(stderr, "lia: failed to fetch registry metadata: %s\n", metadata_url);
        free(metadata_url);
        free(name);
        free(version);
        return 1;
    }

    char *tarball = json_get_string_value(metadata, "tarball");
    free(metadata);
    if (tarball == NULL) {
        fprintf(stderr, "lia: registry metadata missing tarball: %s\n", metadata_url);
        free(metadata_url);
        free(name);
        free(version);
        return 1;
    }

    ArchiveType archive_type;
    if (!detect_archive_type(tarball, &archive_type)) {
        fprintf(stderr, "lia: registry tarball must be .tar.gz, .tgz, or .zip: %s\n", tarball);
        free(tarball);
        free(metadata_url);
        free(name);
        free(version);
        return 1;
    }

    resolved->archive_source = tarball;
    resolved->archive_type = archive_type;
    resolved->is_local_path = !starts_with(tarball, "http://") &&
                              !starts_with(tarball, "https://") &&
                              !starts_with(tarball, "file://");

    free(metadata_url);
    free(name);
    free(version);
    return 0;
}

static int resolve_install_source(const char *source, ResolvedInstallSource *resolved) {
    resolved->archive_source = NULL;
    resolved->archive_type = ARCHIVE_TAR_GZ;
    resolved->is_local_path = 0;

    if (starts_with(source, "github:")) {
        return resolve_github_source(source, resolved);
    }

    if (is_registry_source(source)) {
        return resolve_registry_source(source, resolved);
    }

    ArchiveType archive_type;
    if (!detect_archive_type(source, &archive_type)) {
        fprintf(stderr, "lia: install source must be package[@version], github:owner/repo@ref, .tar.gz, .tgz, or .zip\n");
        return 1;
    }

    resolved->archive_source = duplicate_string(source);
    if (resolved->archive_source == NULL) {
        return 1;
    }

    resolved->archive_type = archive_type;
    resolved->is_local_path = !starts_with(source, "http://") &&
                              !starts_with(source, "https://") &&
                              !starts_with(source, "file://");

    return 0;
}

static int prepare_install_workspace(const char *workspace_dir) {
    if (remove_tree(workspace_dir) != 0) {
        return 1;
    }

    return make_directory_p(workspace_dir);
}

static int fetch_archive(const ResolvedInstallSource *source, const char *archive_path) {
    if (source->is_local_path) {
        return copy_file_path(source->archive_source, archive_path);
    }

    return run_shell_command_2("curl -fsSL %s -o %s", source->archive_source, archive_path);
}

static int extract_archive(const char *archive_path, const char *extract_dir, ArchiveType archive_type) {
    if (make_directory_p(extract_dir) != 0) {
        return 1;
    }

    if (archive_type == ARCHIVE_TAR_GZ) {
        return run_shell_command_2("tar -xzf %s -C %s", archive_path, extract_dir);
    }

    return run_shell_command_2("unzip -q %s -d %s", archive_path, extract_dir);
}

static const char *archive_type_extension(ArchiveType archive_type) {
    return archive_type == ARCHIVE_ZIP ? "zip" : "tar.gz";
}

static const char *archive_type_workspace_name(ArchiveType archive_type) {
    return archive_type == ARCHIVE_ZIP ? "archive.zip" : "archive.tar.gz";
}

static int integrity_hex_value(const char *integrity, const char **hex) {
    if (integrity == NULL || strncmp(integrity, "sha256:", 7) != 0) {
        return 0;
    }

    const char *value = integrity + 7;
    if (strlen(value) != 64) {
        return 0;
    }

    for (const unsigned char *cursor = (const unsigned char *)value; *cursor != '\0'; cursor++) {
        if (!isxdigit(*cursor)) {
            return 0;
        }
    }

    *hex = value;
    return 1;
}

static char *lia_cache_root(void) {
    const char *cache_env = getenv("LIA_CACHE_DIR");
    if (cache_env != NULL && cache_env[0] != '\0') {
        return duplicate_string(cache_env);
    }

    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        return format_string("%s/.lia/cache", home);
    }

    return duplicate_string(".lia/cache");
}

static char *cache_path_for_integrity(const char *integrity, ArchiveType archive_type, int ensure_parent) {
    const char *hex = NULL;
    if (!integrity_hex_value(integrity, &hex)) {
        return NULL;
    }

    char *root = lia_cache_root();
    if (root == NULL) {
        return NULL;
    }

    char *sha_dir = join_path(root, "sha256");
    free(root);
    if (sha_dir == NULL) {
        return NULL;
    }

    if (ensure_parent && make_directory_p(sha_dir) != 0) {
        free(sha_dir);
        return NULL;
    }

    char *filename = format_string("%s.%s", hex, archive_type_extension(archive_type));
    if (filename == NULL) {
        free(sha_dir);
        return NULL;
    }

    char *path = join_path(sha_dir, filename);
    free(filename);
    free(sha_dir);
    return path;
}

static int store_archive_in_cache(const char *archive_path, const char *integrity, ArchiveType archive_type) {
    char *cache_path = cache_path_for_integrity(integrity, archive_type, 1);
    if (cache_path == NULL) {
        return 1;
    }

    int result = 0;
    if (!file_exists(cache_path)) {
        result = copy_file_path(archive_path, cache_path);
    }

    free(cache_path);
    return result;
}

static int restore_archive_from_cache(
    const char *integrity,
    const char *workspace_dir,
    char **archive_path,
    ArchiveType *archive_type
) {
    ArchiveType types[] = {ARCHIVE_TAR_GZ, ARCHIVE_ZIP};
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        char *cache_path = cache_path_for_integrity(integrity, types[i], 0);
        if (cache_path == NULL) {
            continue;
        }

        if (!file_exists(cache_path)) {
            free(cache_path);
            continue;
        }

        char *destination = join_path(workspace_dir, archive_type_workspace_name(types[i]));
        if (destination == NULL) {
            free(cache_path);
            return 1;
        }

        if (copy_file_path(cache_path, destination) != 0) {
            free(destination);
            free(cache_path);
            return 1;
        }

        *archive_path = destination;
        *archive_type = types[i];
        free(cache_path);
        return 0;
    }

    return 1;
}

static char *find_package_root(const char *extract_dir) {
    char *root_manifest = join_path(extract_dir, LIA_MANIFEST_FILE);
    if (root_manifest == NULL) {
        return NULL;
    }

    if (file_exists(root_manifest)) {
        free(root_manifest);
        return duplicate_string(extract_dir);
    }
    free(root_manifest);

    DIR *directory = opendir(extract_dir);
    if (directory == NULL) {
        return NULL;
    }

    char *found_root = NULL;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char *candidate = join_path(extract_dir, entry->d_name);
        if (candidate == NULL) {
            closedir(directory);
            free(found_root);
            return NULL;
        }

        if (!directory_exists(candidate)) {
            free(candidate);
            continue;
        }

        char *candidate_manifest = join_path(candidate, LIA_MANIFEST_FILE);
        if (candidate_manifest == NULL) {
            free(candidate);
            closedir(directory);
            free(found_root);
            return NULL;
        }

        if (file_exists(candidate_manifest)) {
            if (found_root != NULL) {
                fprintf(stderr, "lia: archive contains multiple packages\n");
                free(candidate_manifest);
                free(candidate);
                closedir(directory);
                free(found_root);
                return NULL;
            }
            found_root = candidate;
        } else {
            free(candidate);
        }

        free(candidate_manifest);
    }

    closedir(directory);
    return found_root;
}

static int ensure_lock_file(void) {
    if (file_exists(LIA_LOCK_FILE)) {
        return 0;
    }

    FILE *file = fopen(LIA_LOCK_FILE, "w");
    if (file == NULL) {
        fprintf(stderr, "lia: failed to create %s\n", LIA_LOCK_FILE);
        return 1;
    }

    fputs("{\n", file);
    fputs("  \"lockfileVersion\": 1,\n", file);
    fputs("  \"packages\": {}\n", file);
    fputs("}\n", file);

    if (fclose(file) != 0) {
        fprintf(stderr, "lia: failed to write %s\n", LIA_LOCK_FILE);
        return 1;
    }

    return 0;
}

static int update_project_dependencies(const char *package_name, const char *source, int save_dev) {
    char *quoted_source = json_quote_string(source);
    if (quoted_source == NULL) {
        return 1;
    }

    const char *target_object = save_dev ? "devDependencies" : "dependencies";
    const char *other_object = save_dev ? "dependencies" : "devDependencies";
    if (save_dev && ensure_json_object_field(LIA_MANIFEST_FILE, "devDependencies", "  ") != 0) {
        free(quoted_source);
        return 1;
    }

    int result = upsert_json_object_entry(
        LIA_MANIFEST_FILE,
        target_object,
        package_name,
        quoted_source,
        "    ",
        "  "
    );

    if (result == 0 && json_file_has_object(LIA_MANIFEST_FILE, other_object)) {
        result = remove_json_object_entry(LIA_MANIFEST_FILE, other_object, package_name, "    ", "  ");
    }

    free(quoted_source);
    return result;
}

static int update_lock_file(
    const char *package_name,
    const char *package_version,
    const char *source,
    const char *constraint,
    const char *integrity,
    const char *install_path,
    int is_dev
) {
    if (ensure_lock_file() != 0) {
        return 1;
    }

    char *quoted_version = json_quote_string(package_version);
    char *quoted_source = json_quote_string(source);
    char *quoted_path = json_quote_string(install_path);
    char *quoted_integrity = integrity == NULL ? NULL : json_quote_string(integrity);
    char *quoted_constraint = constraint == NULL ? NULL : json_quote_string(constraint);
    if (quoted_version == NULL || quoted_source == NULL || quoted_path == NULL ||
        (integrity != NULL && quoted_integrity == NULL) ||
        (constraint != NULL && quoted_constraint == NULL)) {
        free(quoted_version);
        free(quoted_source);
        free(quoted_path);
        free(quoted_integrity);
        free(quoted_constraint);
        return 1;
    }

    char *raw_entry = NULL;
    if (constraint == NULL) {
        raw_entry = format_string(
            "{\"version\": %s, \"source\": %s, \"integrity\": %s, \"path\": %s%s}",
            quoted_version,
            quoted_source,
            quoted_integrity == NULL ? "\"\"" : quoted_integrity,
            quoted_path,
            is_dev ? ", \"dev\": true" : ""
        );
    } else {
        raw_entry = format_string(
            "{\"version\": %s, \"source\": %s, \"requirement\": %s, \"integrity\": %s, \"path\": %s%s}",
            quoted_version,
            quoted_source,
            quoted_constraint,
            quoted_integrity == NULL ? "\"\"" : quoted_integrity,
            quoted_path,
            is_dev ? ", \"dev\": true" : ""
        );
    }

    free(quoted_version);
    free(quoted_source);
    free(quoted_path);
    free(quoted_integrity);
    free(quoted_constraint);

    if (raw_entry == NULL) {
        return 1;
    }

    int result = upsert_json_object_entry(
        LIA_LOCK_FILE,
        "packages",
        package_name,
        raw_entry,
        "    ",
        "  "
    );

    free(raw_entry);
    return result;
}

typedef struct {
    char *name;
    char *version;
    JsonEntries dependencies;
    JsonEntries bin;
} PackageInfo;

static void init_package_info(PackageInfo *info) {
    info->name = NULL;
    info->version = NULL;
    info->dependencies.items = NULL;
    info->dependencies.count = 0;
    info->bin.items = NULL;
    info->bin.count = 0;
}

static void free_package_info(PackageInfo *info) {
    free(info->name);
    free(info->version);
    free_json_entries(&info->dependencies);
    free_json_entries(&info->bin);
    init_package_info(info);
}

static int read_package_info(const char *package_root, PackageInfo *info) {
    init_package_info(info);

    char *package_manifest_path = join_path(package_root, LIA_MANIFEST_FILE);
    if (package_manifest_path == NULL) {
        return 1;
    }

    char *package_manifest = read_file_to_string(package_manifest_path);
    free(package_manifest_path);
    if (package_manifest == NULL) {
        fprintf(stderr, "lia: package archive must contain %s\n", LIA_MANIFEST_FILE);
        return 1;
    }

    JsonEntries root_entries;
    if (!parse_json_root_entries(package_manifest, &root_entries)) {
        fprintf(stderr, "lia: package %s must be a JSON object\n", LIA_MANIFEST_FILE);
        free(package_manifest);
        return 1;
    }

    int result = 1;
    if (read_required_manifest_string(&root_entries, "name", &info->name) != 0 ||
        read_required_manifest_string(&root_entries, "version", &info->version) != 0 ||
        read_required_manifest_object(&root_entries, "dependencies", &info->dependencies) != 0) {
        goto done;
    }

    if (!is_valid_package_name(info->name)) {
        fprintf(stderr, "lia: package name '%s' is invalid\n", info->name);
        goto done;
    }

    if (validate_string_object_values("dependencies", &info->dependencies, 1) != 0 ||
        read_optional_manifest_bin(&root_entries, info->name, &info->bin) != 0) {
        goto done;
    }

    result = 0;

done:
    free_json_entries(&root_entries);
    free(package_manifest);
    if (result != 0) {
        free_package_info(info);
    }
    return result;
}

static int link_package_bins(const char *package_name, const char *install_path, JsonEntries *bin_entries) {
    if (bin_entries->count == 0) {
        return 0;
    }

    if (make_directory_p(LIA_PACKAGE_BIN_DIR) != 0) {
        return 1;
    }

    for (size_t i = 0; i < bin_entries->count; i++) {
        char *bin_path = json_raw_to_string(bin_entries->items[i].raw_value);
        if (bin_path == NULL || !is_non_empty_text(bin_path)) {
            fprintf(stderr, "lia: bin.%s must be a non-empty string\n", bin_entries->items[i].key);
            free(bin_path);
            return 1;
        }

        char *package_script = join_path(install_path, bin_path);
        char *command_path = join_path(LIA_PACKAGE_BIN_DIR, bin_entries->items[i].key);
        char *relative_script = format_string("../%s/%s", package_name, bin_path);
        free(bin_path);
        if (package_script == NULL || command_path == NULL || relative_script == NULL) {
            free(package_script);
            free(command_path);
            free(relative_script);
            return 1;
        }

        if (!file_exists(package_script)) {
            fprintf(stderr, "lia: bin target does not exist: %s\n", package_script);
            free(package_script);
            free(command_path);
            free(relative_script);
            return 1;
        }

        char *quoted_script = shell_quote(relative_script);
        free(package_script);
        free(relative_script);
        if (quoted_script == NULL) {
            free(command_path);
            return 1;
        }

        FILE *file = fopen(command_path, "w");
        if (file == NULL) {
            fprintf(stderr, "lia: failed to create package bin %s\n", command_path);
            free(quoted_script);
            free(command_path);
            return 1;
        }

        fputs("#!/bin/sh\n", file);
        fputs("script_dir=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n", file);
        fprintf(file, "exec lia \"$script_dir\"/%s \"$@\"\n", quoted_script);
        free(quoted_script);

        if (fclose(file) != 0) {
            fprintf(stderr, "lia: failed to write package bin %s\n", command_path);
            free(command_path);
            return 1;
        }

        int chmod_result = run_shell_command_1("chmod +x %s", command_path);
        free(command_path);
        if (chmod_result != 0) {
            return 1;
        }
    }

    return 0;
}

int unlink_package_bins(const char *package_name) {
    char *package_dir = join_path(LIA_PACKAGES_DIR, package_name);
    if (package_dir == NULL) {
        return 1;
    }

    if (!directory_exists(package_dir)) {
        free(package_dir);
        return 0;
    }

    PackageInfo package_info;
    if (read_package_info(package_dir, &package_info) != 0) {
        free(package_dir);
        return 1;
    }

    int result = 0;
    for (size_t i = 0; i < package_info.bin.count; i++) {
        char *command_path = join_path(LIA_PACKAGE_BIN_DIR, package_info.bin.items[i].key);
        if (command_path == NULL) {
            result = 1;
            break;
        }

        if (file_exists(command_path) && remove_tree(command_path) != 0) {
            result = 1;
            free(command_path);
            break;
        }
        free(command_path);
    }

    free_package_info(&package_info);
    free(package_dir);
    return result;
}

static char *read_installed_package_version(const char *package_name) {
    char *package_dir = join_path(LIA_PACKAGES_DIR, package_name);
    if (package_dir == NULL) {
        return NULL;
    }

    char *manifest_path = join_path(package_dir, LIA_MANIFEST_FILE);
    free(package_dir);
    if (manifest_path == NULL) {
        return NULL;
    }

    char *manifest = read_file_to_string(manifest_path);
    free(manifest_path);
    if (manifest == NULL) {
        return NULL;
    }

    char *version = json_get_string_value(manifest, "version");
    free(manifest);
    return version;
}

static int installed_package_satisfies(const char *package_name, const char *constraint, int *is_satisfied) {
    *is_satisfied = 0;

    char *installed_version = read_installed_package_version(package_name);
    if (installed_version == NULL) {
        return 0;
    }

    if (constraint != NULL) {
        int semver_result = semver_satisfies(installed_version, constraint);
        if (semver_result == -1) {
            fprintf(stderr, "lia: installed %s version '%s' cannot be checked against '%s'\n",
                    package_name,
                    installed_version,
                    constraint);
            free(installed_version);
            return 1;
        }

        *is_satisfied = semver_result == 1;
    } else {
        *is_satisfied = 1;
    }

    if (*is_satisfied) {
        printf("Using %s@%s\n", package_name, installed_version);
    }

    free(installed_version);
    return 0;
}

static int install_source_recursive(
    InstallContext *context,
    const char *raw_source,
    const char *expected_name,
    const char *expected_integrity,
    int record_direct_dependency
);

static int install_package_dependencies(InstallContext *context, PackageInfo *package_info) {
    for (size_t i = 0; i < package_info->dependencies.count; i++) {
        JsonEntry *dependency = &package_info->dependencies.items[i];
        char *dependency_source = json_raw_to_string(dependency->raw_value);
        if (dependency_source == NULL) {
            fprintf(stderr, "lia: dependencies.%s must be a string\n", dependency->key);
            return 1;
        }

        int result = install_source_recursive(context, dependency_source, dependency->key, NULL, 0);
        free(dependency_source);

        if (result != 0) {
            return result;
        }
    }

    return 0;
}

static int install_package_from_root(
    InstallContext *context,
    const char *package_root,
    const char *source,
    const char *constraint,
    const char *integrity,
    const char *expected_name,
    int record_direct_dependency
) {
    PackageInfo package_info;
    if (read_package_info(package_root, &package_info) != 0) {
        return 1;
    }

    int result = 1;
    int active_pushed = 0;

    if (expected_name != NULL && strcmp(expected_name, package_info.name) != 0) {
        fprintf(stderr,
                "lia: dependency '%s' resolved to package '%s'\n",
                expected_name,
                package_info.name);
        goto done;
    }

    if (constraint != NULL) {
        int semver_result = semver_satisfies(package_info.version, constraint);
        if (semver_result == -1) {
            fprintf(stderr,
                    "lia: package %s version '%s' cannot be checked against '%s'\n",
                    package_info.name,
                    package_info.version,
                    constraint);
            goto done;
        }
        if (semver_result == 0) {
            fprintf(stderr,
                    "lia: package %s@%s does not satisfy '%s'\n",
                    package_info.name,
                    package_info.version,
                    constraint);
            goto done;
        }
    }

    if (string_list_contains(&context->active, package_info.name)) {
        fprintf(stderr, "lia: circular dependency detected at '%s'\n", package_info.name);
        goto done;
    }

    if (string_list_push(&context->active, package_info.name) != 0) {
        goto done;
    }
    active_pushed = 1;

    if (make_directory_p(LIA_PACKAGES_DIR) != 0) {
        goto done;
    }

    char *install_path = format_string("%s/%s", LIA_PACKAGES_DIR, package_info.name);
    if (install_path == NULL) {
        goto done;
    }

    if (unlink_package_bins(package_info.name) != 0 ||
        remove_tree(install_path) != 0 ||
        make_directory_p(install_path) != 0 ||
        copy_directory_contents(package_root, install_path) != 0) {
        free(install_path);
        goto done;
    }

    if (link_package_bins(package_info.name, install_path, &package_info.bin) != 0) {
        free(install_path);
        goto done;
    }

    if (record_direct_dependency && update_project_dependencies(package_info.name, source, context->dev_dependency) != 0) {
        free(install_path);
        goto done;
    }

    if (update_lock_file(package_info.name, package_info.version, source, constraint, integrity, install_path, context->dev_dependency) != 0) {
        free(install_path);
        goto done;
    }

    printf("Installed %s@%s\n", package_info.name, package_info.version);

    if (context->install_dependencies &&
        install_package_dependencies(context, &package_info) != 0) {
        free(install_path);
        goto done;
    }

    if (!string_list_contains(&context->resolved, package_info.name) &&
        string_list_push(&context->resolved, package_info.name) != 0) {
        free(install_path);
        goto done;
    }

    free(install_path);
    result = 0;

done:
    if (active_pushed) {
        string_list_pop(&context->active);
    }
    free_package_info(&package_info);
    return result;
}

static int is_constraint_only_dependency(const char *source) {
    ArchiveType archive_type;
    if (detect_archive_type(source, &archive_type) ||
        starts_with(source, "github:") ||
        starts_with(source, "http://") ||
        starts_with(source, "https://") ||
        starts_with(source, "file://") ||
        strchr(source, '/') != NULL ||
        strchr(source, '\\') != NULL) {
        return 0;
    }

    return looks_like_semver_constraint(source) && semver_satisfies("0.0.0", source) != -1;
}

static int require_installed_package(const char *package_name, const char *constraint) {
    int is_satisfied = 0;
    if (installed_package_satisfies(package_name, constraint, &is_satisfied) != 0) {
        return 1;
    }

    if (!is_satisfied) {
        fprintf(stderr,
                "lia: dependency '%s' requires '%s' but no installed package satisfies it\n",
                package_name,
                constraint == NULL ? "*" : constraint);
        return 1;
    }

    return 0;
}

static int install_source_recursive(
    InstallContext *context,
    const char *raw_source,
    const char *expected_name,
    const char *expected_integrity,
    int record_direct_dependency
) {
    DependencySpec spec;
    if (parse_dependency_spec(raw_source, &spec) != 0) {
        return 1;
    }

    if (expected_name != NULL && string_list_contains(&context->active, expected_name)) {
        fprintf(stderr, "lia: circular dependency detected at '%s'\n", expected_name);
        free_dependency_spec(&spec);
        return 1;
    }

    if (is_constraint_only_dependency(spec.source)) {
        if (expected_name == NULL) {
            fprintf(stderr, "lia: version constraint '%s' needs a dependency name\n", spec.source);
            free_dependency_spec(&spec);
            return 1;
        }

        int result = require_installed_package(expected_name, spec.source);
        free_dependency_spec(&spec);
        return result;
    }

    if (expected_integrity == NULL && !record_direct_dependency && expected_name != NULL && context->dev_dependency) {
        int is_satisfied = 0;
        if (installed_package_satisfies(expected_name, spec.constraint, &is_satisfied) != 0) {
            free_dependency_spec(&spec);
            return 1;
        }
        if (is_satisfied) {
            free_dependency_spec(&spec);
            return 0;
        }
    }

    ResolvedInstallSource resolved;
    resolved.archive_source = NULL;
    resolved.archive_type = ARCHIVE_TAR_GZ;
    resolved.is_local_path = 0;

    int result = 1;
    char *workspace_dir = format_string("%s-%d", LIA_TMP_DIR, context->workspace_counter++);
    char *archive_path = NULL;
    char *extract_dir = NULL;
    char *package_root = NULL;
    char *actual_integrity = NULL;
    ArchiveType archive_type = ARCHIVE_TAR_GZ;
    int archive_ready = 0;

    if (workspace_dir == NULL) {
        goto done;
    }

    if (prepare_install_workspace(workspace_dir) != 0) {
        goto done;
    }

    if (expected_integrity != NULL &&
        restore_archive_from_cache(expected_integrity, workspace_dir, &archive_path, &archive_type) == 0) {
        archive_ready = 1;
    }

    extract_dir = join_path(workspace_dir, "extract");
    if (extract_dir == NULL) {
        goto done;
    }

    if (!archive_ready) {
        if (resolve_install_source(spec.source, &resolved) != 0) {
            goto done;
        }

        archive_type = resolved.archive_type;
        archive_path = join_path(workspace_dir, archive_type_workspace_name(archive_type));
        if (archive_path == NULL) {
            goto done;
        }

        if (fetch_archive(&resolved, archive_path) != 0) {
            goto done;
        }
    }

    if (verify_file_integrity(archive_path, expected_integrity, &actual_integrity) != 0 ||
        store_archive_in_cache(archive_path, actual_integrity, archive_type) != 0 ||
        extract_archive(archive_path, extract_dir, archive_type) != 0) {
        goto done;
    }

    package_root = find_package_root(extract_dir);
    if (package_root == NULL) {
        fprintf(stderr, "lia: archive must contain a package with %s at its root\n", LIA_MANIFEST_FILE);
        goto done;
    }

    result = install_package_from_root(
        context,
        package_root,
        raw_source,
        spec.constraint,
        actual_integrity,
        expected_name,
        record_direct_dependency
    );

done:
    free(actual_integrity);
    free(package_root);
    free(extract_dir);
    free(archive_path);
    if (workspace_dir != NULL) {
        remove_tree(workspace_dir);
    }
    free(workspace_dir);
    free_resolved_install_source(&resolved);
    free_dependency_spec(&spec);
    return result;
}

static char *json_object_get_string(JsonEntries *entries, const char *field_name, int required) {
    JsonEntry *entry = find_json_entry(entries, field_name);
    if (entry == NULL) {
        if (required) {
            fprintf(stderr, "lia: lockfile package entry is missing '%s'\n", field_name);
        }
        return NULL;
    }

    char *value = json_raw_to_string(entry->raw_value);
    if (value == NULL || !is_non_empty_text(value)) {
        if (required) {
            fprintf(stderr, "lia: lockfile package field '%s' must be a non-empty string\n", field_name);
        }
        free(value);
        return NULL;
    }

    return value;
}

static int json_object_get_true(JsonEntries *entries, const char *field_name) {
    JsonEntry *entry = find_json_entry(entries, field_name);
    if (entry == NULL) {
        return 0;
    }

    const char *raw = entry->raw_value;
    while (isspace((unsigned char)*raw)) {
        raw++;
    }

    return strncmp(raw, "true", 4) == 0 &&
           (raw[4] == '\0' || isspace((unsigned char)raw[4]));
}

static int read_lock_packages(JsonEntries *packages) {
    packages->items = NULL;
    packages->count = 0;

    if (!file_exists(LIA_LOCK_FILE)) {
        fprintf(stderr, "lia: %s not found; install a package first\n", LIA_LOCK_FILE);
        return 1;
    }

    char *content = read_file_to_string(LIA_LOCK_FILE);
    if (content == NULL) {
        fprintf(stderr, "lia: failed to read %s\n", LIA_LOCK_FILE);
        return 1;
    }

    JsonEntries root_entries;
    if (!parse_json_root_entries(content, &root_entries)) {
        fprintf(stderr, "lia: %s must be a JSON object\n", LIA_LOCK_FILE);
        free(content);
        return 1;
    }

    int result = 1;
    JsonEntry *packages_entry = find_json_entry(&root_entries, "packages");
    if (packages_entry == NULL) {
        fprintf(stderr, "lia: %s is missing required field 'packages'\n", LIA_LOCK_FILE);
        goto done;
    }

    if (!json_raw_to_object_entries(packages_entry->raw_value, packages)) {
        fprintf(stderr, "lia: %s field 'packages' must be an object\n", LIA_LOCK_FILE);
        goto done;
    }

    result = 0;

done:
    free_json_entries(&root_entries);
    free(content);
    return result;
}

static int install_from_lockfile(int production_only) {
    JsonEntries packages;
    if (read_lock_packages(&packages) != 0) {
        return 1;
    }

    if (packages.count == 0) {
        printf("%s has no packages\n", LIA_LOCK_FILE);
        free_json_entries(&packages);
        return 0;
    }

    InstallContext context;
    init_install_context(&context);
    context.install_dependencies = 0;

    int result = 0;
    for (size_t i = 0; i < packages.count; i++) {
        JsonEntry *package_entry = &packages.items[i];
        if (!is_valid_package_name(package_entry->key)) {
            fprintf(stderr, "lia: lockfile contains invalid package name '%s'\n", package_entry->key);
            result = 1;
            break;
        }

        JsonEntries fields;
        if (!json_raw_to_object_entries(package_entry->raw_value, &fields)) {
            fprintf(stderr, "lia: lockfile package '%s' must be an object\n", package_entry->key);
            result = 1;
            break;
        }

        char *source = json_object_get_string(&fields, "source", 1);
        char *integrity = json_object_get_string(&fields, "integrity", 1);
        char *version = json_object_get_string(&fields, "version", 1);
        int is_dev = json_object_get_true(&fields, "dev");

        if (source == NULL || integrity == NULL || version == NULL) {
            free(source);
            free(integrity);
            free(version);
            free_json_entries(&fields);
            result = 1;
            break;
        }

        if (production_only && is_dev) {
            free(source);
            free(integrity);
            free(version);
            free_json_entries(&fields);
            continue;
        }

        char *exact_source = NULL;
        const char *install_source = source;
        if (is_registry_source(source)) {
            exact_source = format_string("%s@%s", package_entry->key, version);
            if (exact_source == NULL) {
                free(source);
                free(integrity);
                free(version);
                free_json_entries(&fields);
                result = 1;
                break;
            }
            install_source = exact_source;
        }

        int previous_dev_dependency = context.dev_dependency;
        context.dev_dependency = is_dev;
        int install_result = install_source_recursive(&context, install_source, package_entry->key, integrity, 0);
        context.dev_dependency = previous_dev_dependency;
        free(exact_source);
        free(source);
        free(integrity);
        free(version);
        free_json_entries(&fields);

        if (install_result != 0) {
            result = install_result;
            break;
        }
    }

    remove_tree(".lia/tmp");
    free_install_context(&context);
    free_json_entries(&packages);

    if (result == 0) {
        printf("Installed packages from %s\n", LIA_LOCK_FILE);
    }

    return result;
}

int run_install(int argc, char **argv) {
    const char *source = NULL;
    int save_dev = 0;
    int production_only = 0;

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_install_usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--save-dev") == 0 || strcmp(argv[i], "-D") == 0) {
            save_dev = 1;
            continue;
        }

        if (strcmp(argv[i], "--production") == 0) {
            production_only = 1;
            continue;
        }

        if (source != NULL) {
            fprintf(stderr, "lia: install accepts zero or one source\n");
            return 2;
        }
        source = argv[i];
    }

    if (save_dev && source == NULL) {
        fprintf(stderr, "lia: --save-dev requires a package source\n");
        return 2;
    }

    if (save_dev && production_only) {
        fprintf(stderr, "lia: --save-dev cannot be combined with --production\n");
        return 2;
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }
    free_project_manifest(&manifest);

    if (source == NULL) {
        return install_from_lockfile(production_only);
    }

    InstallContext context;
    init_install_context(&context);
    context.dev_dependency = save_dev;

    int result = install_source_recursive(&context, source, NULL, NULL, 1);
    remove_tree(".lia/tmp");
    free_install_context(&context);
    return result;
}

static JsonEntry *find_lock_package_entry(JsonEntries *packages, const char *package_name) {
    for (size_t i = 0; i < packages->count; i++) {
        if (strcmp(packages->items[i].key, package_name) == 0) {
            return &packages->items[i];
        }
    }

    return NULL;
}

static int check_locked_dependency(JsonEntries *packages, JsonEntry *dependency, int expected_dev) {
    char *manifest_source = json_raw_to_string(dependency->raw_value);
    if (manifest_source == NULL || !is_non_empty_text(manifest_source)) {
        fprintf(stderr, "lia: dependency '%s' must be a non-empty string\n", dependency->key);
        free(manifest_source);
        return 1;
    }

    JsonEntry *lock_entry = find_lock_package_entry(packages, dependency->key);
    if (lock_entry == NULL) {
        fprintf(stderr, "lia: %s is missing locked package '%s'\n", LIA_LOCK_FILE, dependency->key);
        free(manifest_source);
        return 1;
    }

    JsonEntries fields;
    if (!json_raw_to_object_entries(lock_entry->raw_value, &fields)) {
        fprintf(stderr, "lia: lockfile package '%s' must be an object\n", dependency->key);
        free(manifest_source);
        return 1;
    }

    char *locked_source = json_object_get_string(&fields, "source", 1);
    int locked_dev = json_object_get_true(&fields, "dev");
    int result = 0;
    if (locked_source == NULL) {
        result = 1;
    } else if (strcmp(manifest_source, locked_source) != 0) {
        fprintf(stderr,
                "lia: %s is out of sync for '%s' (manifest: %s, lockfile: %s)\n",
                LIA_LOCK_FILE,
                dependency->key,
                manifest_source,
                locked_source);
        result = 1;
    } else if (expected_dev != locked_dev) {
        fprintf(stderr,
                "lia: %s has wrong dependency type for '%s'\n",
                LIA_LOCK_FILE,
                dependency->key);
        result = 1;
    }

    free(locked_source);
    free_json_entries(&fields);
    free(manifest_source);
    return result;
}

static int check_locked_dependency_group(JsonEntries *packages, JsonEntries *dependencies, int expected_dev) {
    for (size_t i = 0; i < dependencies->count; i++) {
        if (check_locked_dependency(packages, &dependencies->items[i], expected_dev) != 0) {
            return 1;
        }
    }

    return 0;
}

static int check_lockfile_matches_manifest(ProjectManifest *manifest) {
    JsonEntries packages;
    if (read_lock_packages(&packages) != 0) {
        return 1;
    }

    int result = 0;
    if (check_locked_dependency_group(&packages, &manifest->dependencies, 0) != 0 ||
        check_locked_dependency_group(&packages, &manifest->dev_dependencies, 1) != 0) {
        result = 1;
    }

    free_json_entries(&packages);
    return result;
}

int run_ci(int argc, char **argv) {
    int production_only = 0;

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_ci_usage(stdout);
            return 0;
        }

        if (strcmp(argv[i], "--production") == 0) {
            production_only = 1;
            continue;
        }

        fprintf(stderr, "lia: unknown ci option: %s\n", argv[i]);
        return 2;
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    int result = 1;
    if (check_lockfile_matches_manifest(&manifest) != 0) {
        goto done;
    }

    if (remove_tree(LIA_PACKAGES_DIR) != 0) {
        goto done;
    }

    result = install_from_lockfile(production_only);
    if (result == 0) {
        printf("Clean install complete\n");
    }

done:
    free_project_manifest(&manifest);
    return result;
}

int run_update(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_update_usage(stdout);
        return 0;
    }

    if (argc > 3) {
        fprintf(stderr, "lia: update accepts zero or one package name\n");
        return 2;
    }

    const char *only_package = NULL;
    if (argc == 3) {
        only_package = argv[2];
        if (!is_valid_package_name(only_package)) {
            fprintf(stderr, "lia: invalid package name '%s'\n", only_package);
            return 2;
        }
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    InstallContext context;
    init_install_context(&context);

    int result = 0;
    int updated = 0;
    context.dev_dependency = 0;
    for (size_t i = 0; i < manifest.dependencies.count; i++) {
        JsonEntry *dependency = &manifest.dependencies.items[i];
        if (only_package != NULL && strcmp(dependency->key, only_package) != 0) {
            continue;
        }

        char *source = json_raw_to_string(dependency->raw_value);
        if (source == NULL || !is_non_empty_text(source)) {
            fprintf(stderr, "lia: dependencies.%s must be a non-empty string\n", dependency->key);
            free(source);
            result = 1;
            break;
        }

        int install_result = install_source_recursive(&context, source, dependency->key, NULL, 1);
        free(source);
        if (install_result != 0) {
            result = install_result;
            break;
        }
        updated++;
    }

    context.dev_dependency = 1;
    for (size_t i = 0; result == 0 && i < manifest.dev_dependencies.count; i++) {
        JsonEntry *dependency = &manifest.dev_dependencies.items[i];
        if (only_package != NULL && strcmp(dependency->key, only_package) != 0) {
            continue;
        }

        char *source = json_raw_to_string(dependency->raw_value);
        if (source == NULL || !is_non_empty_text(source)) {
            fprintf(stderr, "lia: devDependencies.%s must be a non-empty string\n", dependency->key);
            free(source);
            result = 1;
            break;
        }

        int install_result = install_source_recursive(&context, source, dependency->key, NULL, 1);
        free(source);
        if (install_result != 0) {
            result = install_result;
            break;
        }
        updated++;
    }

    if (result == 0 && only_package != NULL && updated == 0) {
        fprintf(stderr, "lia: dependency '%s' not found in %s\n", only_package, LIA_MANIFEST_FILE);
        result = 1;
    } else if (result == 0 && updated == 0) {
        printf("No dependencies to update\n");
    } else if (result == 0) {
        printf("Updated %d package%s\n", updated, updated == 1 ? "" : "s");
    }

    remove_tree(".lia/tmp");
    free_install_context(&context);
    free_project_manifest(&manifest);
    return result;
}
