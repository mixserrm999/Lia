#include "lia.h"

static void print_init_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia init [--name <name>] [--main <path>] [--force]\n");
    fprintf(stream, "\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "  --name <name>  Project name for %s\n", LIA_MANIFEST_FILE);
    fprintf(stream, "  --main <path>  Main Lua entry file, default: src/main.lua\n");
    fprintf(stream, "  --force        Overwrite %s if it already exists\n", LIA_MANIFEST_FILE);
}

static void print_run_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia run <script> [args...]\n");
}

static void print_check_usage(FILE *stream) {
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia check\n");
}

static int write_manifest(const char *name, const char *main_file) {
    FILE *file = fopen(LIA_MANIFEST_FILE, "w");
    if (file == NULL) {
        fprintf(stderr, "lia: failed to create %s\n", LIA_MANIFEST_FILE);
        return 1;
    }

    size_t start_script_length = strlen("lia ") + strlen(main_file) + 1;
    char *start_script = malloc(start_script_length);
    if (start_script == NULL) {
        fclose(file);
        fprintf(stderr, "lia: failed to allocate memory for init\n");
        return 1;
    }
    snprintf(start_script, start_script_length, "lia %s", main_file);

    fputs("{\n", file);
    fputs("  \"name\": ", file);
    write_json_string(file, name);
    fputs(",\n", file);
    fputs("  \"version\": \"0.1.0\",\n", file);
    fputs("  \"main\": ", file);
    write_json_string(file, main_file);
    fputs(",\n", file);
    fputs("  \"scripts\": {\n", file);
    fputs("    \"start\": ", file);
    write_json_string(file, start_script);
    fputs("\n", file);
    fputs("  },\n", file);
    fputs("  \"dependencies\": {},\n", file);
    fputs("  \"devDependencies\": {}\n", file);
    fputs("}\n", file);

    free(start_script);

    if (fclose(file) != 0) {
        fprintf(stderr, "lia: failed to write %s\n", LIA_MANIFEST_FILE);
        return 1;
    }

    printf("Created %s\n", LIA_MANIFEST_FILE);
    return 0;
}

int run_init(int argc, char **argv) {
    const char *name = NULL;
    const char *main_file = "src/main.lua";
    int force = 0;

    for (int i = 2; i < argc; i++) {
        if (is_flag(argv[i], "-h", "--help")) {
            print_init_usage(stdout);
            return 0;
        }

        if (is_flag(argv[i], "-f", "--force")) {
            force = 1;
            continue;
        }

        if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --name requires a value\n");
                return 2;
            }
            name = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--name=", 7) == 0) {
            name = argv[i] + 7;
            continue;
        }

        if (strcmp(argv[i], "--main") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "lia: --main requires a value\n");
                return 2;
            }
            main_file = argv[++i];
            continue;
        }

        if (strncmp(argv[i], "--main=", 7) == 0) {
            main_file = argv[i] + 7;
            continue;
        }

        fprintf(stderr, "lia: unknown init option: %s\n", argv[i]);
        return 2;
    }

    if (file_exists(LIA_MANIFEST_FILE) && !force) {
        fprintf(stderr, "lia: %s already exists; use --force to overwrite\n", LIA_MANIFEST_FILE);
        return 1;
    }

    char *default_name = NULL;
    if (name == NULL || name[0] == '\0') {
        default_name = default_project_name();
        if (default_name == NULL) {
            fprintf(stderr, "lia: failed to allocate memory for project name\n");
            return 1;
        }
        name = default_name;
    }

    int result = write_manifest(name, main_file);
    free(default_name);
    return result;
}

void init_project_manifest(ProjectManifest *manifest) {
    manifest->name = NULL;
    manifest->version = NULL;
    manifest->main_file = NULL;
    manifest->scripts.items = NULL;
    manifest->scripts.count = 0;
    manifest->dependencies.items = NULL;
    manifest->dependencies.count = 0;
    manifest->dev_dependencies.items = NULL;
    manifest->dev_dependencies.count = 0;
    manifest->bin.items = NULL;
    manifest->bin.count = 0;
}

void free_project_manifest(ProjectManifest *manifest) {
    free(manifest->name);
    free(manifest->version);
    free(manifest->main_file);
    free_json_entries(&manifest->scripts);
    free_json_entries(&manifest->dependencies);
    free_json_entries(&manifest->dev_dependencies);
    free_json_entries(&manifest->bin);
    init_project_manifest(manifest);
}

int validate_string_object_values(const char *object_name, JsonEntries *entries, int validate_keys_as_package_names) {
    for (size_t i = 0; i < entries->count; i++) {
        if (validate_keys_as_package_names && !is_valid_package_name(entries->items[i].key)) {
            fprintf(stderr, "lia: %s contains invalid package name '%s'\n", object_name, entries->items[i].key);
            return 1;
        }

        if (!validate_keys_as_package_names && !is_valid_script_name(entries->items[i].key)) {
            fprintf(stderr, "lia: %s contains invalid script name '%s'\n", object_name, entries->items[i].key);
            return 1;
        }

        char *value = json_raw_to_string(entries->items[i].raw_value);
        if (value == NULL || !is_non_empty_text(value)) {
            fprintf(stderr, "lia: %s.%s must be a non-empty string\n", object_name, entries->items[i].key);
            free(value);
            return 1;
        }

        if (validate_keys_as_package_names) {
            DependencySpec spec;
            if (parse_dependency_spec(value, &spec) != 0) {
                free(value);
                return 1;
            }

            if (spec.constraint == NULL &&
                looks_like_semver_constraint(spec.source) &&
                semver_satisfies("0.0.0", spec.source) == -1) {
                fprintf(stderr, "lia: %s.%s has invalid semver constraint '%s'\n",
                        object_name,
                        entries->items[i].key,
                        spec.source);
                free_dependency_spec(&spec);
                free(value);
                return 1;
            }

            free_dependency_spec(&spec);
        }

        free(value);
    }

    return 0;
}

int read_required_manifest_string(JsonEntries *root_entries, const char *field_name, char **out) {
    JsonEntry *entry = find_json_entry(root_entries, field_name);
    if (entry == NULL) {
        fprintf(stderr, "lia: %s is missing required field '%s'\n", LIA_MANIFEST_FILE, field_name);
        return 1;
    }

    char *value = json_raw_to_string(entry->raw_value);
    if (value == NULL || !is_non_empty_text(value)) {
        fprintf(stderr, "lia: %s field '%s' must be a non-empty string\n", LIA_MANIFEST_FILE, field_name);
        free(value);
        return 1;
    }

    *out = value;
    return 0;
}

int read_required_manifest_object(JsonEntries *root_entries, const char *field_name, JsonEntries *out) {
    JsonEntry *entry = find_json_entry(root_entries, field_name);
    if (entry == NULL) {
        fprintf(stderr, "lia: %s is missing required field '%s'\n", LIA_MANIFEST_FILE, field_name);
        return 1;
    }

    if (!json_raw_to_object_entries(entry->raw_value, out)) {
        fprintf(stderr, "lia: %s field '%s' must be an object\n", LIA_MANIFEST_FILE, field_name);
        return 1;
    }

    return 0;
}

int read_optional_manifest_object(JsonEntries *root_entries, const char *field_name, JsonEntries *out) {
    out->items = NULL;
    out->count = 0;

    JsonEntry *entry = find_json_entry(root_entries, field_name);
    if (entry == NULL) {
        return 0;
    }

    if (!json_raw_to_object_entries(entry->raw_value, out)) {
        fprintf(stderr, "lia: %s field '%s' must be an object\n", LIA_MANIFEST_FILE, field_name);
        return 1;
    }

    return 0;
}

static int validate_bin_entries(JsonEntries *entries) {
    for (size_t i = 0; i < entries->count; i++) {
        if (!is_valid_package_name(entries->items[i].key)) {
            fprintf(stderr, "lia: bin contains invalid command name '%s'\n", entries->items[i].key);
            return 1;
        }

        char *value = json_raw_to_string(entries->items[i].raw_value);
        if (value == NULL || !is_non_empty_text(value)) {
            fprintf(stderr, "lia: bin.%s must be a non-empty string\n", entries->items[i].key);
            free(value);
            return 1;
        }

        if (value[0] == '/' || strstr(value, "..") != NULL) {
            fprintf(stderr, "lia: bin.%s must be a relative package path\n", entries->items[i].key);
            free(value);
            return 1;
        }

        free(value);
    }

    return 0;
}

int read_optional_manifest_bin(JsonEntries *root_entries, const char *package_name, JsonEntries *out) {
    out->items = NULL;
    out->count = 0;

    JsonEntry *entry = find_json_entry(root_entries, "bin");
    if (entry == NULL) {
        return 0;
    }

    char *single_bin = json_raw_to_string(entry->raw_value);
    if (single_bin != NULL) {
        if (!is_non_empty_text(single_bin) || single_bin[0] == '/' || strstr(single_bin, "..") != NULL) {
            fprintf(stderr, "lia: bin must be a relative package path\n");
            free(single_bin);
            return 1;
        }

        out->items = malloc(sizeof(JsonEntry));
        if (out->items == NULL) {
            free(single_bin);
            return 1;
        }

        out->items[0].key = duplicate_string(package_name);
        out->items[0].raw_value = json_quote_string(single_bin);
        out->count = 1;
        free(single_bin);

        if (out->items[0].key == NULL || out->items[0].raw_value == NULL) {
            free_json_entries(out);
            return 1;
        }

        return 0;
    }

    if (!json_raw_to_object_entries(entry->raw_value, out)) {
        fprintf(stderr, "lia: %s field 'bin' must be a string or object\n", LIA_MANIFEST_FILE);
        return 1;
    }

    if (validate_bin_entries(out) != 0) {
        free_json_entries(out);
        return 1;
    }

    return 0;
}

int load_project_manifest(ProjectManifest *manifest) {
    init_project_manifest(manifest);

    if (!file_exists(LIA_MANIFEST_FILE)) {
        fprintf(stderr, "lia: %s not found; run `lia init` first\n", LIA_MANIFEST_FILE);
        return 1;
    }

    char *content = read_file_to_string(LIA_MANIFEST_FILE);
    if (content == NULL) {
        fprintf(stderr, "lia: failed to read %s\n", LIA_MANIFEST_FILE);
        return 1;
    }

    JsonEntries root_entries;
    if (!parse_json_root_entries(content, &root_entries)) {
        fprintf(stderr, "lia: %s must be a JSON object\n", LIA_MANIFEST_FILE);
        free(content);
        return 1;
    }

    int result = 1;
    if (read_required_manifest_string(&root_entries, "name", &manifest->name) != 0 ||
        read_required_manifest_string(&root_entries, "version", &manifest->version) != 0 ||
        read_required_manifest_string(&root_entries, "main", &manifest->main_file) != 0 ||
        read_required_manifest_object(&root_entries, "scripts", &manifest->scripts) != 0 ||
        read_required_manifest_object(&root_entries, "dependencies", &manifest->dependencies) != 0 ||
        read_optional_manifest_object(&root_entries, "devDependencies", &manifest->dev_dependencies) != 0) {
        goto done;
    }

    if (!is_valid_package_name(manifest->name)) {
        fprintf(stderr, "lia: %s field 'name' must use letters, numbers, dots, underscores, or hyphens\n", LIA_MANIFEST_FILE);
        goto done;
    }

    if (validate_string_object_values("scripts", &manifest->scripts, 0) != 0 ||
        validate_string_object_values("dependencies", &manifest->dependencies, 1) != 0 ||
        validate_string_object_values("devDependencies", &manifest->dev_dependencies, 1) != 0 ||
        read_optional_manifest_bin(&root_entries, manifest->name, &manifest->bin) != 0) {
        goto done;
    }

    result = 0;

done:
    free_json_entries(&root_entries);
    free(content);
    if (result != 0) {
        free_project_manifest(manifest);
    }
    return result;
}

int run_check(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_check_usage(stdout);
        return 0;
    }

    if (argc > 2) {
        fprintf(stderr, "lia: check accepts no arguments\n");
        return 2;
    }

    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    printf("%s is valid\n", LIA_MANIFEST_FILE);
    free_project_manifest(&manifest);
    return 0;
}

static char *append_shell_arguments(const char *command, int argc, char **argv, int first_arg_index) {
    size_t length = strlen(command);

    for (int i = first_arg_index; i < argc; i++) {
        char *quoted = shell_quote(argv[i]);
        if (quoted == NULL) {
            return NULL;
        }
        length += 1 + strlen(quoted);
        free(quoted);
    }

    char *full_command = malloc(length + 1);
    if (full_command == NULL) {
        return NULL;
    }

    strcpy(full_command, command);
    for (int i = first_arg_index; i < argc; i++) {
        char *quoted = shell_quote(argv[i]);
        if (quoted == NULL) {
            free(full_command);
            return NULL;
        }

        strcat(full_command, " ");
        strcat(full_command, quoted);
        free(quoted);
    }

    return full_command;
}

static void print_available_scripts(ProjectManifest *manifest) {
    if (manifest->scripts.count == 0) {
        return;
    }

    fprintf(stderr, "Available scripts:\n");
    for (size_t i = 0; i < manifest->scripts.count; i++) {
        fprintf(stderr, "  %s\n", manifest->scripts.items[i].key);
    }
}

static char *with_package_bin_environment(const char *command) {
    char *quoted_bin_dir = shell_quote(LIA_PACKAGE_BIN_DIR);
    if (quoted_bin_dir == NULL) {
        return NULL;
    }

    char *full_command = format_string(
        "PATH=%s:$PATH LIA_PACKAGE_BIN_DIR=%s %s",
        quoted_bin_dir,
        quoted_bin_dir,
        command
    );
    free(quoted_bin_dir);
    return full_command;
}

static int run_named_manifest_script(
    ProjectManifest *manifest,
    const char *script_name,
    int argc,
    char **argv,
    int first_arg_index,
    int pass_arguments,
    int required
) {
    JsonEntry *script = find_json_entry(&manifest->scripts, script_name);
    if (script == NULL) {
        if (required) {
            fprintf(stderr, "lia: script '%s' not found in %s\n", script_name, LIA_MANIFEST_FILE);
            print_available_scripts(manifest);
            return 1;
        }
        return 0;
    }

    char *script_command = json_raw_to_string(script->raw_value);
    if (script_command == NULL || !is_non_empty_text(script_command)) {
        fprintf(stderr, "lia: scripts.%s must be a non-empty string\n", script_name);
        free(script_command);
        return 1;
    }

    char *command_with_args = NULL;
    if (pass_arguments) {
        command_with_args = append_shell_arguments(script_command, argc, argv, first_arg_index);
    } else {
        command_with_args = duplicate_string(script_command);
    }
    free(script_command);
    if (command_with_args == NULL) {
        fprintf(stderr, "lia: failed to prepare script command\n");
        return 1;
    }

    char *full_command = with_package_bin_environment(command_with_args);
    free(command_with_args);
    if (full_command == NULL) {
        fprintf(stderr, "lia: failed to prepare script environment\n");
        return 1;
    }

    printf("> %s\n", full_command);
    fflush(stdout);
    int result = run_shell_command(full_command);
    free(full_command);
    return result;
}

int run_manifest_script(int argc, char **argv) {
    if (argc == 3 && is_flag(argv[2], "-h", "--help")) {
        print_run_usage(stdout);
        return 0;
    }

    if (argc < 3) {
        print_run_usage(stderr);
        return 2;
    }

    const char *script_name = argv[2];
    ProjectManifest manifest;
    if (load_project_manifest(&manifest) != 0) {
        return 1;
    }

    int result = 0;
    int run_lifecycle = !starts_with(script_name, "pre") && !starts_with(script_name, "post");
    char *pre_script = NULL;
    char *post_script = NULL;

    if (run_lifecycle) {
        pre_script = format_string("pre%s", script_name);
        post_script = format_string("post%s", script_name);
        if (pre_script == NULL || post_script == NULL) {
            result = 1;
            goto done;
        }

        result = run_named_manifest_script(&manifest, pre_script, argc, argv, 3, 0, 0);
        if (result != 0) {
            goto done;
        }
    }

    result = run_named_manifest_script(&manifest, script_name, argc, argv, 3, 1, 1);
    if (result != 0) {
        goto done;
    }

    if (run_lifecycle) {
        result = run_named_manifest_script(&manifest, post_script, argc, argv, 3, 0, 0);
    }

done:
    free(pre_script);
    free(post_script);
    free_project_manifest(&manifest);
    return result;
}
