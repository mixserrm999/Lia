#include "lia.h"

static void print_usage(FILE *stream) {
    fprintf(stream, "lia %s\n", LIA_VERSION);
    fprintf(stream, "\n");
    fprintf(stream, "Usage:\n");
    fprintf(stream, "  lia init [--name <name>] [--main <path>] [--force]\n");
    fprintf(stream, "  lia login --token <token> [--registry <url>]\n");
    fprintf(stream, "  lia pack\n");
    fprintf(stream, "  lia publish [--tag <tag>] [--registry <url>] [--token <token>]\n");
    fprintf(stream, "  lia search <term> [--registry <url>]\n");
    fprintf(stream, "  lia deprecate <package>@<version> <message> [--registry <url>] [--token <token>]\n");
    fprintf(stream, "  lia config get <key>\n");
    fprintf(stream, "  lia config set <key> <value>\n");
    fprintf(stream, "  lia doctor\n");
    fprintf(stream, "  lia install [--save-dev] [--production] [source]\n");
    fprintf(stream, "  lia ci [--production]\n");
    fprintf(stream, "  lia update [package]\n");
    fprintf(stream, "  lia list\n");
    fprintf(stream, "  lia remove <package>\n");
    fprintf(stream, "  lia info <package>\n");
    fprintf(stream, "  lia outdated\n");
    fprintf(stream, "  lia run <script> [args...]\n");
    fprintf(stream, "  lia check\n");
    fprintf(stream, "  lia <script.lua> [args...]\n");
    fprintf(stream, "  lia --version\n");
    fprintf(stream, "  lia --help\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(stderr);
        return 2;
    }

    if (is_flag(argv[1], "-h", "--help")) {
        print_usage(stdout);
        return 0;
    }

    if (is_flag(argv[1], "-v", "--version")) {
        printf("lia %s (%s)\n", LIA_VERSION, LUA_VERSION);
        return 0;
    }

    if (strcmp(argv[1], "init") == 0) {
        return run_init(argc, argv);
    }

    if (strcmp(argv[1], "login") == 0) {
        return run_login(argc, argv);
    }

    if (strcmp(argv[1], "publish") == 0) {
        return run_publish(argc, argv);
    }

    if (strcmp(argv[1], "pack") == 0) {
        return run_pack(argc, argv);
    }

    if (strcmp(argv[1], "search") == 0) {
        return run_search(argc, argv);
    }

    if (strcmp(argv[1], "deprecate") == 0) {
        return run_deprecate(argc, argv);
    }

    if (strcmp(argv[1], "config") == 0) {
        return run_config(argc, argv);
    }

    if (strcmp(argv[1], "doctor") == 0) {
        return run_doctor(argc, argv);
    }

    if (strcmp(argv[1], "install") == 0) {
        return run_install(argc, argv);
    }

    if (strcmp(argv[1], "ci") == 0) {
        return run_ci(argc, argv);
    }

    if (strcmp(argv[1], "update") == 0) {
        return run_update(argc, argv);
    }

    if (strcmp(argv[1], "list") == 0) {
        return run_list(argc, argv);
    }

    if (strcmp(argv[1], "remove") == 0) {
        return run_remove(argc, argv);
    }

    if (strcmp(argv[1], "info") == 0) {
        return run_info(argc, argv);
    }

    if (strcmp(argv[1], "outdated") == 0) {
        return run_outdated(argc, argv);
    }

    if (strcmp(argv[1], "run") == 0) {
        return run_manifest_script(argc, argv);
    }

    if (strcmp(argv[1], "check") == 0) {
        return run_check(argc, argv);
    }

    return run_lia_script(argc, argv, 1);
}
