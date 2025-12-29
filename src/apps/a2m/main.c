// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "rt_lib.h"

#define MAX_CLI_PARAMS  1

typedef struct OPTS OPTS;
typedef void (*cb_arg_handler)(int num_params, const char **params, OPTS *opts);

typedef struct ARGCTX {
    int   argc;
    char **argv;
    int   index;   // current index in argv
    int   done;    // set on end or fatal error
    int   error;   // non-zero on error
} ARGCTX;

typedef struct ARGSPEC {
    const char *long_name;
    const char *short_name;
    int         param_count;   // -1 optional, 0 none, N exact
    cb_arg_handler  handler;
} ARGSPEC;

typedef struct OPTS {
    ARGCTX ctx;
    const char *ini_file_name;
    INI_STORE ini_store;
    uint32_t defaults: 1;
    uint32_t help: 1;
    uint32_t noini: 1;
    uint32_t nosaveini: 1;
    uint32_t remember: 1;
    uint32_t saveini: 1;
    uint32_t pad: 26;
} OPTS;

void argctx_init(ARGCTX *ctx, int argc, char **argv) {
    ctx->argc  = argc;
    ctx->argv  = argv;
    ctx->index = 1;  // skip program name
    ctx->done  = (argc <= 1);
    ctx->error = 0;
}

void handle_break(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    UNUSED(opts);
}

void handle_defaults(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    opts->defaults = 1;
}

void handle_disk(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    const char *val = strchr(params[0], '=');
    if(!val) {
        return;
    }
    int64_t l = val - params[0];
    val++; // skip the '='
    if(l != 4) {
        // must be sNdX
        return;
    }
    char *key = (char *)malloc(l + 1);
    if(!key) {
        return;
    }
    memcpy(key, params[0], l);
    key[l] = '\0';
    ini_set(&opts->ini_store, "DiskII", key, val);
    free(key);
}

void handle_franklin80(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    int slot;
    if((sscanf(params[0], "%d", &slot) == 1) && slot >= 1 && slot <= 7) {
        char key[] = "s3dev";
        key[1] = (char)('0' + slot);
        ini_set(&opts->ini_store, "Video", key, "Franklin Ace Display");
    }
}

void handle_inifile(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    opts->ini_file_name = params[0];
}

void handle_leds(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    ini_set(&opts->ini_store, "Config", "disk_leds", params[0]);
}

void handle_model(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    ini_set(&opts->ini_store, "Machine", "Model", params[0]);
}

void handle_noini(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    opts->noini = 1;
}

void handle_nosaveini(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    opts->nosaveini = 1;
}

void handle_remember(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    opts->remember = 1;
    ini_set(&opts->ini_store, "Config", "Save", "yes");
}

void handle_saveini(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    opts->saveini = 1;
}

void handle_smart(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    const char *val = strchr(params[0], '=');
    if(!val) {
        return;
    }
    int64_t l = val - params[0];
    val++;
    if(l != 4) {
        // must be sNdX
        return;
    }
    char *key = (char *)malloc(l + 1);
    if(!key) {
        return;
    }
    memcpy(key, params[0], l);
    key[l] = '\0';
    ini_set(&opts->ini_store, "SmartPort", key, val);
    free(key);
}

void handle_turbo(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    ini_set(&opts->ini_store, "Machine", "Turbo", params[0]);
}

void handle_ui(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    ini_set(&opts->ini_store, "Config", "ui", params[0]);
}

void handle_help(int num_params, const char **params, OPTS *opts) {
    UNUSED(num_params);
    UNUSED(params);
    const char *prog = opts->ctx.argv[0];
    const char *name = util_strrtok(prog, "\\/");
    name = name ? (name + 1) : prog;
    opts->help = 1;

    printf("%s V2.00.\n", name);
    printf("This is free and unencumbered software released into the public domain\n");
    printf("Apple ][+ or //e Enhanced emulator\n");
    printf("By Stefan Wessels, 2025\n");
    printf("Usage: %s [options]\n", name);
    printf("Options [and optional values]:\n");
    printf(" Long <parameters>             Short <same parameters as long>\n");
    printf(" --break <break>               -b  Install a breakpoint\n");
    printf(" --defaults                    -f  Use default settings\n");
    printf(" --disk <slot><drive>=<image>  -d  Disk II in slot & drive contains image\n");
    printf(" --franklin80 <slot>           -8  Franklin Ace 80 col card in slot 3, plus only\n");
    printf(" --inifile <name>              -i  Path to an .ini file\n");
    printf(" --leds <on|off>               -l  Show disk activity LEDs in window based ui\n");
    printf(" --model <plus|enh>            -m  Select Apple ][+ or //e Enhanced\n");
    printf(" --noini                       -n  Don't use an ini file\n");
    printf(" --nosaveini                   -!  Don't save the ini no matter what\n");
    printf(" --remember                    -r  Add save at quit to ini file (implies -s)\n");
    printf(" --saveini                     -v  Save to ini file at quit\n");
    printf(" --smart <slot><drive>=<image> -s  Smartport in slot & device contains image\n");
    printf(" --turbo <csv>                 -t  Comma separated set of turbo multipliers\n");
    printf(" --ui <gui|text>               -u  Window based or text based user interface\n");
    printf(" --help                        -h  Show this help\n");
    printf("Where:\n");
    printf("  <break> is <ad[-ad]>[,pc|read|write|access[,reset|fast|slow][,count[,reset]]]\n");
    printf("    ad is a hex, decimal or octal 16-bit address\n");
    printf("  <csv> is a list of 1MHz multiplier numbers, or max\n");
    printf("  <drive> is Dn where n is a number in the range 0..1\n");
    printf("  <image> is a disk image (.po|hdv|2img for SmartPort and .nib for disk II)\n");
    printf("    if <image> contains spaces it needs to be in quotations (\"image file\")\n");
    printf("  <on|off> is either on, or 1 for on, or off or 0 for off\n");
    printf("  <slot> is Sn where n is a number in the range 1..7\n");
    printf("  Option values are not separated by spaces, example --turbo takes 1,8,16,max\n");
    printf("Command line options override (and potentially overwrite) any ini loaded options\n");
}

static const ARGSPEC g_specs[] = {
    { "--break", "-b", 1, handle_break      },
    { "--defaults", "-f", 0, handle_defaults   },
    { "--disk", "-d", 1, handle_disk       },
    { "--franklin80", "-8", 1, handle_franklin80 },
    { "--inifile", "-i", 1, handle_inifile    },
    { "--leds", "-l", 1, handle_leds       },
    { "--model", "-m", 1, handle_model      },
    { "--noini", "-n", 0, handle_noini      },
    { "--nosaveini", "-!", 0, handle_nosaveini  },
    { "--remember", "-r", 0, handle_remember   },
    { "--saveini", "-v", 0, handle_saveini    },
    { "--smart", "-s", 1, handle_smart      },
    { "--turbo", "-t", 1, handle_turbo      },
    { "--ui", "-u", 1, handle_ui         },
    { "--help", "-h", 0, handle_help       },
    { NULL, NULL, 0, NULL              }           // terminator
};

int parse_arg(ARGCTX *ctx, const char *long_name, const char *short_name, int param_count, const char **params, int *num_params) {
    if(ctx->done || ctx->error) {
        return 0;
    }

    if(ctx->index >= ctx->argc) {
        ctx->done = 1;
        return 0;
    }

    const char *arg = ctx->argv[ctx->index];

    // Not this option
    if(stricmp(arg, long_name) != 0 && (!short_name || stricmp(arg, short_name) != 0)) {
        return 0;
    }

    // How many args remain after this option?
    int remain_after = ctx->argc - ctx->index - 1;

    int actual_params = 0;

    if(param_count == -1) {
        // Optional: consume 1 param if there is one and it doesn't look like another option.
        if(remain_after > 0 && ctx->argv[ctx->index + 1][0] != '-') {
            actual_params = 1;
        } else {
            actual_params = 0;
        }
    } else {
        // Fixed count.
        if(remain_after < param_count) {
            ctx->error = 1;   // "missing parameters"
            ctx->done  = 1;
            if(num_params) {
                *num_params = 0;
            }
            return 0;
        }
        actual_params = param_count;
    }

    if(params && actual_params > 0) {
        for(int i = 0; i < actual_params; ++i) {
            params[i] = ctx->argv[ctx->index + 1 + i];
        }
    }

    if(num_params) {
        *num_params = actual_params;
    }

    ctx->index += 1 + actual_params;
    if(ctx->index >= ctx->argc) {
        ctx->done = 1;
    }

    return 1;
}

// Treat the current arg as positional (non-option).  Returns 1 if consumed, 0 if no more args.
int parse_positional(ARGCTX *ctx, const char **out_arg) {
    if(ctx->done || ctx->error) {
        return 0;
    }
    if(ctx->index >= ctx->argc) {
        ctx->done = 1;
        return 0;
    }

    const char *arg = ctx->argv[ctx->index];
    if(arg[0] == '-') {
        ctx->error = 2;  // unknown option = error
        ctx->done  = 1;
        return 0;
    }

    if(out_arg) {
        *out_arg = arg;
    }
    ctx->index++;
    if(ctx->index >= ctx->argc) {
        ctx->done = 1;
    }
    return 1;
}

int parse_args_all(const ARGSPEC *specs, OPTS *opts) {
    ARGCTX *ctx = &opts->ctx;
    while(!ctx->done && !ctx->error) {
        int matched = 0;

        for(const ARGSPEC *s = specs; !ctx->error && s->long_name; ++s) {
            const char *params[MAX_CLI_PARAMS];
            int n = 0;

            if(parse_arg(ctx, s->long_name, s->short_name, s->param_count, params, &n) > 0) {
                if(s->handler) {
                    s->handler(n, params, opts);
                }
                matched = 1;
                break;
            }
        }

        if(matched) {
            continue;
        }

        // No spec matched: treat as positional or error
        const char *pos = NULL;
        if(!parse_positional(ctx, &pos)) {
            break; // ctx.error or ctx.done
        }
        // handle positional here, or via a special handler in the table
    }

    return ctx->error ? A2_ERR : A2_OK;
}

int main_ini_merge_to(INI_STORE *source, INI_STORE *target) {
    int rv = A2_OK;
    for(int is = 0; is < source->sections.items; is++) {
        INI_SECTION *s = ARRAY_GET(&source->sections, INI_SECTION, is);
        for(int ik = 0; ik < s->kv.items; ik++) {
            INI_KV *kv = ARRAY_GET(&s->kv, INI_KV, ik);
            if(0 == stricmp(kv->key, "debug") && 0 == stricmp(kv->val, "break")) {
                if(A2_OK != ini_add(target, s->name, kv->key, kv->val)) {
                    rv = A2_ERR;
                }
            } else {
                if(A2_OK != ini_set(target, s->name, kv->key, kv->val)) {
                    rv = A2_ERR;
                }
            }
        }
    }
    return rv;
}

int main(int argc, char **argv) {
    int ini_load_status = A2_ERR;
    OPTS opts;
    INI_STORE ini_store;
    RUNTIME rt;
    APPLE2 m;
    UI ui;

    // Clear the command line options and init the store
    memset(&opts, 0, sizeof(OPTS));
    ini_init(&opts.ini_store);
    // Set a default ini file name
    opts.ini_file_name = "./a2m.ini";
    // Init the argumet context
    argctx_init(&opts.ctx, argc, argv);

    // Init the load ini config storage
    ini_init(&ini_store);

    // Deal with command line
    if(A2_OK != parse_args_all(g_specs, &opts)) {
        printf("Command line parameter parsing failed at parameter %d\n", opts.ctx.index);
        return A2_ERR;
    }

    if(opts.help) {
        return A2_OK;
    }

    if(!opts.noini) {
        // Load config from ini file
        ini_load_status = util_ini_load_file(opts.ini_file_name, ini_add, (void *)&ini_store);
    }

    if(opts.defaults || A2_ERR == ini_load_status) {
        ini_set(&ini_store, "Machine", "Model", "enh ; enh or plus");
        ini_set(&ini_store, "Machine", "Turbo", "1, 8, 16, max ");
        ini_set(&ini_store, "DiskII", "s6d0", "; path to .nib floppy image");
        ini_set(&ini_store, "DiskII", "s6d1", "");
        ini_set(&ini_store, "SmartPort", "s5d0", "; path to hd image (.po, .2img, .hdv)");
        ini_set(&ini_store, "SmartPort", "s5d1", "");
        ini_set(&ini_store, "SmartPort", "s7d0", "");
        ini_set(&ini_store, "SmartPort", "s7d1", "");
        ini_set(&ini_store, "Video", "s3dev", "Franklin Ace Display ; only for model = plus");
        ini_set(&ini_store, "Config", "disk_leds", "on");
    }

    main_ini_merge_to(&opts.ini_store, &ini_store);

    // This avoids reading ui.reconfig which might give a possible use of uninit var warning
    int reconfig = 0;
    do {
        // Config selves
        if(A2_OK != rt_init(&rt, &ini_store)) {
            goto rt_err;
        }
        if(A2_OK != apple2_init(&m, &ini_store)) {
            goto a2_err;
        }
        if(A2_OK != ui_init(&ui, m.model, &ini_store)) {
            goto ui_err;
        }

        // Set bindings (callbacks) for appe2 <-> rt <-> ui
        rt_bind(&rt, &m, &ui);

        // Enter the main loop, till the user quits or makes a reconfig that
        // requires everything to be re-configured
        rt_run(&rt, &m, &ui);

        reconfig = ui.reconfig;

        // Shut everything down
ui_err:
        ui_shutdown(&ui);
a2_err:
        apple2_shutdown(&m);
rt_err:
        rt_shutdown(&rt);

        // If a re-config is called for, go back and do it all over
    } while(reconfig);

    // If the ini file needs to be saved, save it
    if(!opts.nosaveini) {
        if(opts.saveini || ini_get(&ini_store, "Config", "Save")) {
            util_ini_save_file(opts.ini_file_name, &ini_store);
        }
    }

    // Shut the ini file down and then exit
    ini_shutdown(&ini_store);

    return A2_OK;
}
