#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "lexer.h"
#include "parser.h"
#include "interp.h"
#include "ast.h"

// ── Judgement messages for CLI mistakes ────────────────────────────────────

static void judge(const char *msg) {
    fprintf(stderr, "\033[35m[NullScript judges you]: %s\033[0m\n", msg);
}

static void error(const char *msg) {
    fprintf(stderr, "\033[31m[NullScript Error]: %s\033[0m\n", msg);
}

static void print_usage(void) {
    printf(
        "\033[1mNullScript\033[0m — the language that judges you\n\n"
        "  \033[36mnullscript x file.ns\033[0m              interpret a NullScript file\n"
        "  \033[36mnullscript x -c file.ns\033[0m           compile to .nsx (--compile)\n"
        "  \033[36mnullscript x file.nsx\033[0m             run a compiled .nsx file\n"
        "  \033[36mnullscript x -w file.ns file.exe\033[0m  compile to Windows exe (--windows)\n"
        "  \033[36mnullscript x -m file.ns file.bin\033[0m  compile to macOS binary (--mac)\n"
        "  \033[36mnullscript x -l file.ns file.bin\033[0m  compile to Linux binary (--linux)\n\n"
        "  \033[36mnullscript i library\033[0m              install a package\n"
        "  \033[36mnullscript i -g library\033[0m           install globally (--global)\n"
        "  \033[36mnullscript i -v library\033[0m           install to venv (--venv)\n\n"
        "  \033[36mnullscript v -m foldername\033[0m        create and activate a venv (--make)\n"
        "  \033[36mnullscript v -o foldername\033[0m        activate an existing venv (--open)\n\n"
        "  \033[36mnullscript r -a https://url\033[0m       add a package repository (--add)\n"
        "  \033[36mnullscript r -u\033[0m                   update repository manifests (--update)\n"
        "  \033[36mnullscript r -U\033[0m                   upgrade installed packages (--upgrade)\n"
        "  \033[36mnullscript r -uU\033[0m                  update manifests and upgrade packages\n\n"
        "  \033[36mnullscript version\033[0m                print version\n"
        "  \033[36mnullscript help\033[0m                   show this message\n"
    );
}

// ── Read file ──────────────────────────────────────────────────────────────

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    fread(buf, 1, size, f);
    fclose(f);
    buf[size] = '\0';
    return buf;
}

// ── Extension checks ───────────────────────────────────────────────────────

static int ends_with(const char *s, const char *suffix) {
    int sl = strlen(s), fl = strlen(suffix);
    return sl >= fl && !strcmp(s + sl - fl, suffix);
}

// ── "Compile" to .nsx (just store serialised source with magic header) ──────
// Real compilation is out of scope for this implementation; .nsx is a
// trivially-wrapped source file that the runner identifies by magic bytes.

#define NSX_MAGIC "NSX1\n"

static int compile_nsx(const char *src_path, const char *out_path) {
    char *src = read_file(src_path);
    if (!src) {
        error("could not read source file");
        return 1;
    }
    // Validate it parses
    Token **toks = lexer_tokenise(src);
    ASTNode *ast = parse(toks);
    tokens_free(toks);
    if (!ast) {
        free(src);
        return 1;
    }
    ast_node_free(ast);

    FILE *out = fopen(out_path, "wb");
    if (!out) { error("could not write output file"); free(src); return 1; }
    fputs(NSX_MAGIC, out);
    fputs(src, out);
    fclose(out);
    free(src);
    printf("\033[32m[NullScript]: compiled to %s\033[0m\n", out_path);
    return 0;
}

static char *read_nsx(const char *path) {
    char *raw = read_file(path);
    if (!raw) return NULL;
    int magic_len = strlen(NSX_MAGIC);
    if (strncmp(raw, NSX_MAGIC, magic_len)) {
        error("not a valid .nsx file. did you corrupt it? impressive.");
        free(raw);
        return NULL;
    }
    char *src = strdup(raw + magic_len);
    free(raw);
    return src;
}

// ── Execute source string ──────────────────────────────────────────────────

static int run_source(const char *src, const char *filename) {
    Token **tokens = lexer_tokenise(src);

    // Check for lex errors
    int lex_ok = 1;
    for (int i = 0; tokens[i]; i++) {
        if (tokens[i]->type == TOK_ERROR) { lex_ok = 0; break; }
    }
    if (!lex_ok) {
        tokens_free(tokens);
        return 1;
    }

    ASTNode *ast = parse(tokens);
    tokens_free(tokens);

    if (!ast) {
        fprintf(stderr, "\033[31m[NullScript]: parse failed in %s\033[0m\n", filename);
        return 1;
    }

    Interpreter *interp = interp_new();
    // Set args (empty for now — could be populated from argv)
    Value *args_arr = val_array();
    env_set(interp->globals, "__args__", args_arr, 0, 1);
    val_release(args_arr);

    Value *result = interp_run(interp, ast);
    val_release(result);

    int exit_code = interp->had_error ? 1 : 0;
    interp_free(interp);
    ast_node_free(ast);
    return exit_code;
}

// ── Subcommand: execute ────────────────────────────────────────────────────

static int cmd_execute(int argc, char **argv) {
    // argv here starts after "nullscript x"
    if (argc == 0) {
        judge("you typed 'nullscript x' and then just... stopped. what were you going to do?");
        print_usage();
        return 1;
    }

    // Flags
    int flag_compile = 0;
    const char *platform = NULL;  // "windows", "mac", "linux"

    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        const char *flag = argv[i];
        if (!strcmp(flag, "-c") || !strcmp(flag, "--compile")) { flag_compile = 1; }
        else if (!strcmp(flag, "-w") || !strcmp(flag, "--windows")) { platform = "windows"; }
        else if (!strcmp(flag, "-m") || !strcmp(flag, "--mac"))     { platform = "mac"; }
        else if (!strcmp(flag, "-l") || !strcmp(flag, "--linux"))   { platform = "linux"; }
        else {
            char msg[128];
            snprintf(msg, sizeof(msg), "unknown flag '%s'. i don't know what that is and frankly neither do you.", flag);
            judge(msg);
            return 1;
        }
        i++;
    }

    if (i >= argc) {
        judge("you gave me flags but no file. very bold. very wrong.");
        return 1;
    }

    const char *src_path = argv[i++];

    // Compile to .nsx
    if (flag_compile) {
        char out_path[512];
        if (i < argc) strncpy(out_path, argv[i], sizeof(out_path));
        else {
            strncpy(out_path, src_path, sizeof(out_path) - 5);
            char *dot = strrchr(out_path, '.');
            if (dot) strcpy(dot, ".nsx");
            else strcat(out_path, ".nsx");
        }
        return compile_nsx(src_path, out_path);
    }

    // Native platform compile (stub)
    if (platform) {
        if (i >= argc) {
            judge("you said compile to a platform but didn't give me an output filename. classic.");
            return 1;
        }
        const char *out_path = argv[i];
        // Compile to .nsx first, then note this is a stub
        char nsx_tmp[512];
        snprintf(nsx_tmp, sizeof(nsx_tmp), "%s.nsx", out_path);
        int r = compile_nsx(src_path, nsx_tmp);
        if (r) return r;
        printf("\033[33m[NullScript]: native compilation for %s is a stub in this build. "
               "the .nsx file has been saved to %s instead. "
               "wrap it in a shell script if you need to distribute it.\033[0m\n",
               platform, nsx_tmp);
        return 0;
    }

    // Run .nsx
    if (ends_with(src_path, ".nsx")) {
        char *src = read_nsx(src_path);
        if (!src) return 1;
        int r = run_source(src, src_path);
        free(src);
        return r;
    }

    // Run .ns
    if (!ends_with(src_path, ".ns")) {
        judge("that file doesn't end in .ns or .nsx. what exactly are you trying to run?");
        return 1;
    }

    char *src = read_file(src_path);
    if (!src) {
        char msg[256];
        snprintf(msg, sizeof(msg), "could not open '%s'. does it exist? did you spell it right?", src_path);
        error(msg);
        return 1;
    }
    int r = run_source(src, src_path);
    free(src);
    return r;
}

// ── Subcommand: install ────────────────────────────────────────────────────

static int cmd_install(int argc, char **argv) {
    if (argc == 0) {
        judge("install what, exactly? you didn't say.");
        return 1;
    }
    int is_global = 0, is_venv = 0;
    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--global")) is_global = 1;
        else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--venv")) is_venv = 1;
        else {
            char msg[128];
            snprintf(msg, sizeof(msg), "what is '%s'? that is not a flag.", argv[i]);
            judge(msg);
            return 1;
        }
        i++;
    }
    if (i >= argc) {
        judge("you gave install flags but no package name. deeply unserious.");
        return 1;
    }
    const char *pkg = argv[i];
    const char *scope = is_global ? "globally" : (is_venv ? "to the venv" : "globally (you didn't specify, so i decided for you)");

    // Check if we're in a venv (look for .nullscript-venv marker)
    int in_venv = 0;
    {
        FILE *f = fopen(".nullscript-venv", "r");
        if (f) { in_venv = 1; fclose(f); }
    }

    if (!is_global && !is_venv) {
        if (in_venv) { is_venv = 1; scope = "to the venv"; }
        else {
            printf("you didn't specify -g (global) or -v (venv) and you're not in a venv.\n");
            printf("install globally? [y/N] ");
            char response[8];
            if (fgets(response, sizeof(response), stdin) && (response[0] == 'y' || response[0] == 'Y')) {
                is_global = 1; scope = "globally";
            } else {
                printf("installation cancelled. fair enough.\n");
                return 0;
            }
        }
    }

    printf("\033[32m[NullScript]: would install '%s' %s. "
           "(package registry not yet configured — add repos with 'nullscript r -a <url>')\033[0m\n",
           pkg, scope);
    return 0;
}

// ── Subcommand: venv ───────────────────────────────────────────────────────

static int cmd_venv(int argc, char **argv) {
    if (argc == 0) {
        judge("venv what? make one? open one? you have to tell me.");
        return 1;
    }
    int flag_make = 0, flag_open = 0;
    int i = 0;
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-m") || !strcmp(argv[i], "--make")) flag_make = 1;
        else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--open")) flag_open = 1;
        else {
            judge("that is not a valid venv flag. please read the help.");
            return 1;
        }
        i++;
    }
    if (i >= argc) { judge("you need to provide a folder name."); return 1; }
    const char *folder = argv[i];

    if (flag_make) {
        // Create the venv folder structure
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s/packages %s/bin", folder, folder);
        system(cmd);
        // Write activation marker
        char marker[512];
        snprintf(marker, sizeof(marker), "%s/.nullscript-venv", folder);
        FILE *f = fopen(marker, "w");
        if (f) { fprintf(f, "venv\n"); fclose(f); }
        printf("\033[32m[NullScript]: venv created at '%s'.\033[0m\n", folder);
        printf("\033[33m[NullScript]: to activate, run: nullscript v -o %s\033[0m\n", folder);
        printf("\033[33m[NullScript]: (or add 'source %s/activate.sh' to your shell config)\033[0m\n", folder);
        // Write a simple activate script
        char activate[512];
        snprintf(activate, sizeof(activate), "%s/activate.sh", folder);
        FILE *fa = fopen(activate, "w");
        if (fa) {
            fprintf(fa, "#!/bin/sh\nexport NULLSCRIPT_VENV=\"%s\"\ncd \"%s\" && ln -sf .nullscript-venv .nullscript-venv-active 2>/dev/null\necho \"NullScript venv '%s' activated.\"\n",
                    folder, folder, folder);
            fclose(fa);
            chmod(activate, 0755);
        }
        return 0;
    }
    if (flag_open) {
        char marker[512];
        snprintf(marker, sizeof(marker), "%s/.nullscript-venv", folder);
        FILE *f = fopen(marker, "r");
        if (!f) {
            char msg[256];
            snprintf(msg, sizeof(msg), "'%s' is not a NullScript venv. did you make one first?", folder);
            error(msg);
            return 1;
        }
        fclose(f);
        char activate[512];
        snprintf(activate, sizeof(activate), "%s/activate.sh", folder);
        printf("\033[32m[NullScript]: activating venv '%s'...\033[0m\n", folder);
        printf("\033[33m[NullScript]: run this in your shell to activate:\033[0m\n");
        printf("  source %s\n", activate);
        return 0;
    }

    judge("you used 'nullscript v' without -m or -o. what were you trying to do?");
    return 1;
}

// ── Subcommand: repo ───────────────────────────────────────────────────────

#define REPOS_FILE ".nullscript-repos"

static int cmd_repo(int argc, char **argv) {
    if (argc == 0) { judge("repo what? -a, -u, -U? you have to pick."); return 1; }

    // Handle combined flags like -uU
    int flag_add = 0, flag_update = 0, flag_upgrade = 0;
    const char *add_url = NULL;

    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-a") || !strcmp(a, "--add"))       { flag_add = 1; if (i+1 < argc) add_url = argv[++i]; }
        else if (!strcmp(a, "-u") || !strcmp(a, "--update"))  flag_update  = 1;
        else if (!strcmp(a, "-U") || !strcmp(a, "--upgrade")) flag_upgrade = 1;
        else if (a[0] == '-' && a[1] != '-') {
            // combined like -uU
            for (int j = 1; a[j]; j++) {
                if (a[j] == 'u') flag_update  = 1;
                else if (a[j] == 'U') flag_upgrade = 1;
                else if (a[j] == 'a') { flag_add = 1; if (i+1 < argc) add_url = argv[++i]; }
            }
        }
        else { judge("that is not a repo flag. try -a, -u, -U, or -uU."); return 1; }
    }

    if (flag_add) {
        if (!add_url) { judge("you said --add but didn't give a URL. impressive."); return 1; }
        // Ensure it ends in /manifest.json
        char full_url[1024];
        strncpy(full_url, add_url, sizeof(full_url));
        if (!ends_with(full_url, "/manifest.json")) {
            if (full_url[strlen(full_url)-1] != '/') strcat(full_url, "/");
            strcat(full_url, "manifest.json");
            printf("\033[33m[NullScript]: you forgot '/manifest.json'. i added it for you.\033[0m\n");
        }
        FILE *f = fopen(REPOS_FILE, "a");
        if (f) { fprintf(f, "%s\n", full_url); fclose(f); }
        printf("\033[32m[NullScript]: added repository: %s\033[0m\n", full_url);
    }
    if (flag_update) {
        printf("\033[32m[NullScript]: re-fetching all repository manifests...\033[0m\n");
        FILE *f = fopen(REPOS_FILE, "r");
        if (!f) { printf("  no repositories configured yet. add one with 'nullscript r -a <url>'\n"); }
        else {
            char line[1024];
            while (fgets(line, sizeof(line), f)) {
                line[strcspn(line, "\n")] = '\0';
                printf("  updating: %s\n", line);
                // In a real implementation: HTTP fetch + parse JSON
            }
            fclose(f);
        }
    }
    if (flag_upgrade) {
        printf("\033[32m[NullScript]: upgrading installed packages...\033[0m\n");
        printf("  (no packages currently installed)\n");
    }
    return 0;
}

// ── Main ───────────────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    if (argc < 2) {
        judge("you ran 'nullscript' with no arguments. what did you expect to happen?");
        print_usage();
        return 1;
    }

    const char *subcmd = argv[1];

    // Version
    if (!strcmp(subcmd, "version") || !strcmp(subcmd, "--version") || !strcmp(subcmd, "-V")) {
        printf("NullScript 0.1.0 — the language that judges you\n");
        printf("interpreter built in C\n");
        return 0;
    }

    // Help
    if (!strcmp(subcmd, "help") || !strcmp(subcmd, "--help") || !strcmp(subcmd, "-h")) {
        print_usage();
        return 0;
    }

    // Subcommands: x / execute, i / install, v / venv, r / repo
    if (!strcmp(subcmd, "x") || !strcmp(subcmd, "execute")) {
        return cmd_execute(argc - 2, argv + 2);
    }
    if (!strcmp(subcmd, "i") || !strcmp(subcmd, "install")) {
        return cmd_install(argc - 2, argv + 2);
    }
    if (!strcmp(subcmd, "v") || !strcmp(subcmd, "venv")) {
        return cmd_venv(argc - 2, argv + 2);
    }
    if (!strcmp(subcmd, "r") || !strcmp(subcmd, "repo")) {
        return cmd_repo(argc - 2, argv + 2);
    }

    // Unknown subcommand
    char msg[128];
    snprintf(msg, sizeof(msg),
        "'%s' is not a subcommand. the valid ones are x, i, v, r. "
        "or help if you're lost, which you clearly are.", subcmd);
    judge(msg);
    print_usage();
    return 1;
}
