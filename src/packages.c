#include "lia.h"

static void print_list_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia list\n");
}

static void print_remove_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia remove <package>\n");
}

static void print_info_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia info <package>\n");
}

static void print_outdated_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia outdated\n");
}

static int read_lock_packages_for_command(JsonEntries *packages, int missing_ok) {
    packages->items = NULL;
    packages->count = 0;

    if (!file_exists(LIA_LOCK_FILE)) {
        if (missing_ok) {
            return 0;
        }
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

static char *json_object_string(JsonEntries *entries, const char *field_name, int required) {
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

static JsonEntry *find_lock_package(JsonEntries *packages, const char *package_name) {
    for (size_t i = 0; i < packages->count; i++) {
        if (strcmp(packages->items[i].key, package_name) == 0) {
            return &packages->items[i];
        }
    }

    return NULL;
}

static char *registry_name_from_source(const char *source) {
    if (source == NULL ||
        starts_with(source, "github:") ||
        starts_with(source, "http://") ||
        starts_with(source, "https://") ||
        starts_with(source, "./") ||
        starts_with(source, "../") ||
        archive_suffix_matches(source, ".tar.gz") ||
        archive_suffix_matches(source, ".tgz") ||
        archive_suffix_matches(source, ".zip")) {
        return NULL;
    }

    const char *at = strchr(source, '@');
    size_t length = at == NULL ? strlen(source) : (size_t)(at - source);
    char *name = copy_range(source, length);
    if (name == NULL) {
        return NULL;
    }

    if (!is_valid_package_name(name)) {
        free(name);
        return NULL;
    }

    return name;
}

int run_list(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_list_usage(stdout);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "lia: list accepts no arguments\n");
        return 2;
    }

    JsonEntries packages;
    if (read_lock_packages_for_command(&packages, 1) != 0) {
        return 1;
    }

    if (packages.count == 0) {
        printf("No packages installed\n");
        free_json_entries(&packages);
        return 0;
    }

    for (size_t i = 0; i < packages.count; i++) {
        JsonEntries fields;
        if (!json_raw_to_object_entries(packages.items[i].raw_value, &fields)) {
            fprintf(stderr, "lia: lockfile package '%s' must be an object\n", packages.items[i].key);
            free_json_entries(&packages);
            return 1;
        }

        char *version = json_object_string(&fields, "version", 0);
        char *source = json_object_string(&fields, "source", 0);
        printf("%s@%s", packages.items[i].key, version == NULL ? "unknown" : version);
        if (source != NULL) {
            printf(" %s", source);
        }
        printf("\n");

        free(version);
        free(source);
        free_json_entries(&fields);
    }

    free_json_entries(&packages);
    return 0;
}

int run_info(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_info_usage(stdout);
        return 0;
    }

    if (argc != 3) {
        print_info_usage(stderr);
        return 2;
    }

    const char *package_name = argv[2];
    if (!is_valid_package_name(package_name)) {
        fprintf(stderr, "lia: invalid package name '%s'\n", package_name);
        return 2;
    }

    JsonEntries packages;
    if (read_lock_packages_for_command(&packages, 0) != 0) {
        return 1;
    }

    JsonEntry *package = find_lock_package(&packages, package_name);
    if (package == NULL) {
        fprintf(stderr, "lia: package '%s' not found in %s\n", package_name, LIA_LOCK_FILE);
        free_json_entries(&packages);
        return 1;
    }

    JsonEntries fields;
    if (!json_raw_to_object_entries(package->raw_value, &fields)) {
        fprintf(stderr, "lia: lockfile package '%s' must be an object\n", package_name);
        free_json_entries(&packages);
        return 1;
    }

    char *version = json_object_string(&fields, "version", 0);
    char *source = json_object_string(&fields, "source", 0);
    char *requirement = json_object_string(&fields, "requirement", 0);
    char *integrity = json_object_string(&fields, "integrity", 0);
    char *path = json_object_string(&fields, "path", 0);

    printf("name: %s\n", package_name);
    if (version != NULL) {
        printf("version: %s\n", version);
    }
    if (source != NULL) {
        printf("source: %s\n", source);
    }
    if (requirement != NULL) {
        printf("requirement: %s\n", requirement);
    }
    if (integrity != NULL) {
        printf("integrity: %s\n", integrity);
    }
    if (path != NULL) {
        printf("path: %s\n", path);
    }

    free(version);
    free(source);
    free(requirement);
    free(integrity);
    free(path);
    free_json_entries(&fields);
    free_json_entries(&packages);
    return 0;
}

int run_remove(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_remove_usage(stdout);
        return 0;
    }

    if (argc != 3) {
        print_remove_usage(stderr);
        return 2;
    }

    const char *package_name = argv[2];
    if (!is_valid_package_name(package_name)) {
        fprintf(stderr, "lia: invalid package name '%s'\n", package_name);
        return 2;
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    if (find_json_entry(&manifest.dependencies, package_name) == NULL) {
        fprintf(stderr, "lia: dependency '%s' not found in %s\n", package_name, LIA_MANIFEST_FILE);
        free_project_manifest(&manifest);
        return 1;
    }
    free_project_manifest(&manifest);

    if (remove_json_object_entry(LIA_MANIFEST_FILE, "dependencies", package_name, "    ", "  ") != 0) {
        return 1;
    }

    if (file_exists(LIA_LOCK_FILE) &&
        remove_json_object_entry(LIA_LOCK_FILE, "packages", package_name, "    ", "  ") != 0) {
        return 1;
    }

    char *package_dir = format_string("%s/%s", LIA_PACKAGES_DIR, package_name);
    if (package_dir == NULL) {
        return 1;
    }

    int result = 0;
    if (directory_exists(package_dir) && remove_tree(package_dir) != 0) {
        result = 1;
    }
    free(package_dir);

    if (result == 0) {
        printf("Removed %s\n", package_name);
    }
    return result;
}

int run_outdated(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_outdated_usage(stdout);
        return 0;
    }

    if (argc != 2) {
        fprintf(stderr, "lia: outdated accepts no arguments\n");
        return 2;
    }

    JsonEntries packages;
    if (read_lock_packages_for_command(&packages, 1) != 0) {
        return 1;
    }

    if (packages.count == 0) {
        printf("No packages installed\n");
        free_json_entries(&packages);
        return 0;
    }

    char *registry = default_registry_url();
    if (registry == NULL) {
        free_json_entries(&packages);
        return 1;
    }

    int checked = 0;
    int outdated = 0;
    int result = 0;
    for (size_t i = 0; i < packages.count; i++) {
        JsonEntries fields;
        if (!json_raw_to_object_entries(packages.items[i].raw_value, &fields)) {
            fprintf(stderr, "lia: lockfile package '%s' must be an object\n", packages.items[i].key);
            result = 1;
            break;
        }

        char *version = json_object_string(&fields, "version", 0);
        char *source = json_object_string(&fields, "source", 0);
        char *registry_name = registry_name_from_source(source);
        if (version == NULL || registry_name == NULL) {
            free(version);
            free(source);
            free(registry_name);
            free_json_entries(&fields);
            continue;
        }

        checked++;
        char *url = format_string("%s/packages/%s/latest", registry, registry_name);
        char *metadata = url == NULL ? NULL : fetch_url_to_string(url);
        char *latest = metadata == NULL ? NULL : json_get_string_value(metadata, "version");
        if (latest == NULL) {
            fprintf(stderr, "lia: failed to read latest version for %s\n", registry_name);
            result = 1;
        } else if (strcmp(version, latest) != 0) {
            printf("%s current=%s latest=%s\n", registry_name, version, latest);
            outdated++;
        }

        free(latest);
        free(metadata);
        free(url);
        free(version);
        free(source);
        free(registry_name);
        free_json_entries(&fields);

        if (result != 0) {
            break;
        }
    }

    if (result == 0 && checked == 0) {
        printf("No registry packages in %s\n", LIA_LOCK_FILE);
    } else if (result == 0 && outdated == 0) {
        printf("All registry packages are current\n");
    }

    free(registry);
    free_json_entries(&packages);
    return result;
}
