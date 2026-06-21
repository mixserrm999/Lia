#ifndef LIA_H
#define LIA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define mkdir(path, mode) _mkdir(path)
#define popen _popen
#define pclose _pclose
#else
#include <unistd.h>
#endif

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef LIA_VERSION
#define LIA_VERSION "0.1.0"
#endif

#define LIA_MANIFEST_FILE "lia.json"
#define LIA_LOCK_FILE "lia-lock.json"
#define LIA_PACKAGES_DIR "packages"
#define LIA_TMP_DIR ".lia/tmp/install"

typedef struct {
    char *key;
    char *raw_value;
} JsonEntry;

typedef struct {
    JsonEntry *items;
    size_t count;
} JsonEntries;

typedef struct {
    char *source;
    char *constraint;
} DependencySpec;

typedef struct {
    char *name;
    char *version;
    char *main_file;
    JsonEntries scripts;
    JsonEntries dependencies;
} ProjectManifest;

int is_flag(const char *value, const char *short_name, const char *long_name);
int file_exists(const char *path);
char *duplicate_string(const char *value);
char *copy_range(const char *start, size_t length);
char *format_string(const char *format, ...);
int starts_with(const char *value, const char *prefix);
int archive_suffix_matches(const char *value, const char *suffix);
int directory_exists(const char *path);
char *join_path(const char *left, const char *right);
char *shell_quote(const char *value);
int run_shell_command(const char *command);
int run_shell_command_1(const char *format, const char *arg);
int run_shell_command_2(const char *format, const char *first, const char *second);
int make_directory_p(const char *path);
int remove_tree(const char *path);
int copy_file_path(const char *source, const char *destination);
int copy_directory_contents(const char *source, const char *destination);
char *trim_trailing_slashes(const char *value);
char *fetch_url_to_string(const char *url);
char *compute_file_integrity(const char *path);
int verify_file_integrity(const char *path, const char *expected_integrity, char **actual_integrity);
char *read_file_to_string(const char *path);
char *default_project_name(void);
void write_json_string(FILE *file, const char *value);
char *json_quote_string(const char *value);
int is_valid_github_part(const char *value, int allow_slash);
int is_valid_package_name(const char *value);
int is_non_empty_text(const char *value);
int is_valid_script_name(const char *value);
void free_dependency_spec(DependencySpec *spec);
int parse_dependency_spec(const char *raw_value, DependencySpec *spec);
int semver_satisfies(const char *version_text, const char *constraint);
int looks_like_semver_constraint(const char *value);

void free_json_entries(JsonEntries *entries);
JsonEntry *find_json_entry(JsonEntries *entries, const char *key);
int parse_json_root_entries(const char *json, JsonEntries *entries);
char *json_get_string_value(const char *json, const char *target_key);
char *json_raw_to_string(const char *raw_value);
int json_raw_to_object_entries(const char *raw_value, JsonEntries *entries);
int upsert_json_object_entry(
    const char *path,
    const char *object_key,
    const char *entry_key,
    const char *entry_value,
    const char *entry_indent,
    const char *close_indent
);
int remove_json_object_entry(
    const char *path,
    const char *object_key,
    const char *entry_key,
    const char *entry_indent,
    const char *close_indent
);

void init_project_manifest(ProjectManifest *manifest);
void free_project_manifest(ProjectManifest *manifest);
int validate_string_object_values(const char *object_name, JsonEntries *entries, int validate_keys_as_package_names);
int read_required_manifest_string(JsonEntries *root_entries, const char *field_name, char **out);
int read_required_manifest_object(JsonEntries *root_entries, const char *field_name, JsonEntries *out);
int load_project_manifest(ProjectManifest *manifest);

char *default_registry_url(void);
int run_init(int argc, char **argv);
int run_check(int argc, char **argv);
int run_manifest_script(int argc, char **argv);
int run_login(int argc, char **argv);
int run_publish(int argc, char **argv);
int run_install(int argc, char **argv);
int run_update(int argc, char **argv);
int run_list(int argc, char **argv);
int run_remove(int argc, char **argv);
int run_info(int argc, char **argv);
int run_outdated(int argc, char **argv);
int run_lia_script(int argc, char **argv, int script_index);

#endif
