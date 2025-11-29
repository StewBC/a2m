// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "runtime_lib.h"

static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if(!p) {
        fprintf(stderr, "Out of memory.\n");
        exit(2);
    }
    return p;
}

static char *xstrdup(const char *s) {
    if(!s) {
        return NULL;
    }
    size_t n = strlen(s) + 1;
    char *p = (char *)xmalloc(n);
    memcpy(p, s, n);
    return p;
}

static int xmodel_parse(const char *s) {
    if(0 == stricmp(s, "plus")) {
        return 0;
    }
    if(0 == stricmp(s, "enh")) {
        return 1;
    }
    fprintf(stderr, "Model must be plus or enh, not %s\n", s);
    exit(2);
}

static int xui_parse(const char *s) {
    if(0 == stricmp(s, "gui")) {
        return 0;
    }
    if(0 == stricmp(s, "text")) {
        return 0;
    }
    fprintf(stderr, "UI must be gui or text, not %s\n", s);
    exit(2);
}

static void print_help(const char *prog) {
    const char *name = strrchr(prog, '\\');
    if(!name) {
        name = strrchr(prog, '/');
    }
    if(!name) {
        name = prog;
    } else {
        name++;
    }

    printf("%s V2.00.\n", name);
    printf("This is free and unencumbered software released into the public domain\n");
    printf("Apple ][+ or //e Enhanced emulator\n");
    printf("By Stefan Wessels, 2025\n");
    printf("Usage: %s [options]\n", name);
    printf("Options:\n");
    printf("  --inifile <val>       -i <val>    path to an .ini file\n");
    printf("  --model <val>         -m <val>    plus (][+) | enh (//e Enhanced)\n");
    printf("  --ui <val>            -u <val>    gui or text\n");
    printf("  --createini           -c          create ini file if not found\n");
    printf("  --merge               -m          merge command line over ini file\n");
    printf("  --noini               -n          don't use an ini file\n");
    printf("  --saveini             -s          save to ini file at quit\n");
    printf("  --help                -h          Show this help\n");
}

static int match_long(const char *arg, const char *name, const char **out_val) {
    size_t n = strlen(name);
    if(strncmp(arg, name, n) != 0) {
        return 0;
    }
    if(arg[n] == '\0') {
        if(out_val) {
            *out_val = NULL;
        }
        return 1;
    }
    if(arg[n] == '=')  {
        if(out_val) {
            *out_val = arg + n + 1;
        }
        return 1;
    }
    return 0;
}

int parse_args(Opts *o, int argc, char **argv) {
    // Default Opts
    memset(o, 0, sizeof(Opts));
    o->inifile = xstrdup("./a2m.ini");
    o->model = 1;

    // Opt parsing
    int i = 1;
    while(i < argc) {
        const char *arg = argv[i];
        if(arg[0] != '-') {
            break;
        }
        if(!strcmp(arg, "--")) {
            i++;
            break;
        }

        const char *val = NULL;
        if(arg[1] == '-') {
            if(match_long(arg, "--help", &val)) {
                print_help(argv[0]);
                return A2_ERR;
            }
            if(match_long(arg, "--inifile", &val)) {
                if(!val) {
                    if(++i >= argc) {
                        fprintf(stderr, "--inifile requires a value\n");
                        return A2_ERR;
                    }
                    val = argv[i];
                }
                free(o->inifile);
                o->inifile = xstrdup(val);
                i++;
                continue;
            }
            if(match_long(arg, "--model", &val)) {
                if(!val) {
                    if(++i >= argc) {
                        fprintf(stderr, "--prefix-hex requires a value\n");
                        return A2_ERR;
                    }
                    val = argv[i];
                }
                o->model = xmodel_parse(val);
                i++;
                continue;
            }
            if(match_long(arg, "--ui", &val)) {
                if(!val) {
                    if(++i >= argc) {
                        fprintf(stderr, "--ui requires a value\n");
                        return A2_ERR;
                    }
                    val = argv[i];
                }
                o->ui = xui_parse(val);
                i++;
                continue;
            }
            if(match_long(arg, "--createini", &val)) {
                o->createini = 1;
                i++;
                continue;
            }
            if(match_long(arg, "--merge", &val)) {
                o->merge = 1;
                i++;
                continue;
            }
            if(match_long(arg, "--noini", &val)) {
                o->noini = 1;
                i++;
                continue;
            }
            if(match_long(arg, "--saveini", &val)) {
                o->saveini = 1;
                i++;
                continue;
            }
        }

        /* short options */
        if(!strcmp(arg, "-h")) {
            print_help(argv[0]);
            return A2_ERR;
        } else if(!strcmp(arg, "-i")) {
            if(++i >= argc) {
                fprintf(stderr, "-mi requires value\n");
                return A2_ERR;
            }
            free(o->inifile);
            o->inifile = xstrdup(argv[i]);
            i++;
            continue;
        } else if(!strcmp(arg, "-m")) {
            if(++i >= argc) {
                fprintf(stderr, "-m requires value\n");
                return A2_ERR;
            }
            o->model = xmodel_parse(argv[i]);
            i++;
            continue;
        } else if(!strcmp(arg, "-u")) {
            if(++i >= argc) {
                fprintf(stderr, "-u requires value\n");
                return A2_ERR;
            }
            o->ui = xui_parse(argv[i]);
            i++;
            continue;
        } else if(!strcmp(arg, "-c")) {
            o->createini = 1;
            i++;
            continue;
        } else if(!strcmp(arg, "-m")) {
            o->merge = 1;
            i++;
            continue;
        } else if(!strcmp(arg, "-n")) {
            o->noini = 1;
            i++;
            continue;
        } else if(!strcmp(arg, "-s")) {
            o->saveini = 1;
            i++;
            continue;
        } else {
            fprintf(stderr, "unrecognised parameter %s\n", argv[i]);
            return A2_ERR;
        }
    }

    return A2_OK;
}

int main(int argc, char **argv) {
    Opts o;
    int ini_load_status = A2_ERR;
    INI_STORE ini_store;
    RUNTIME rt;
    APPLE2 m;
    UI ui;

    // Init the ini config storage
    ini_init(&ini_store);

    // Deal with command line
    if(A2_OK != parse_args(&o, argc, argv)) {
        return A2_ERR;
    }

    if(!o.noini) {
        // Load config from ini file
        ini_load_status = util_ini_load_file(o.inifile, ini_add, (void *)&ini_store);
    }

    // if an ini wasn't loaded, or wasn't wanted, make one anyway
    if(o.noini || A2_ERR == ini_load_status) {
        ini_add(&ini_store, "Machine", "Model", "enhanced ; plus | enh (//e Enhanced)");
        ini_add(&ini_store, "Machine", "Turbo", "1, 8, 16, max ; Float.  F3 cycles - max is flat out");
        ini_add(&ini_store, "Display", "scale", "1.0 ; Uniformly scale Application Window");
        ini_add(&ini_store, "Display", "disk_leds", "1 ; Show ini_store activity LEDs");
        ini_add(&ini_store, "Video", ";slot", "3 ; Slot where an ][+ 80 col card is inserted ");
        ini_add(&ini_store, "Video", ";device", "Franklin Ace Display ; 80 Column Videx like card");
        ini_add(&ini_store, "DiskII", "slot", "6 ; This says a slot contains a ini_store II controller");
        ini_add(&ini_store, "DiskII", "disk0", "; file name of a NIB floppy image, NOT in quotes");
        ini_add(&ini_store, "DiskII", "disk1", "; ./disks/Apple DOS 3.3 January 1983.nib ; example usage");
        ini_add(&ini_store, "SmartPort", "slot", "5 ; This says a slot contains a smartport");
        ini_add(&ini_store, "SmartPort", "disk0", " ; file name of an image, NOT in quotes");
        ini_add(&ini_store, "SmartPort", "disk1", "");
        ini_add(&ini_store, "SmartPort", "boot", "0 ; any value other than 0 will cause a boot of disk0");
        ini_add(&ini_store, "SmartPort", "slot", "7 ; There can be multiple slots");
        ini_add(&ini_store, "SmartPort", "disk0", "");
        ini_add(&ini_store, "SmartPort", "disk1", "");
        ini_add(&ini_store, "SmartPort", "boot", "0 ; last listed non-zero boot devices' disk0 will boot");
        ini_add(&ini_store, "Debug", ";break", "pc, restore, 0, 0 are the defaults for break =");
        ini_add(&ini_store, "Debug", ";break", "<address[-address]>[,pc|read|write|access][,restore | fast | slow | tron | trona | troff][, count[, reset]]]");
    }

    // If the command line arguments need to replace the ini args, permanently
    if(o.merge) {
        // runtime_merge(ini_store, o);
    }

    // if the ini needs to be be written to ini_store now
    if(o.createini) {
        util_ini_save_file(o.inifile, &ini_store);
    }

    do {
        // Config selves
        runtime_init(&rt, &ini_store);
        apple2_init(&m, &ini_store);
        ui_init(&ui, m.model, &ini_store);

        // Set bindings (callbacks) for appe2 <-> runtime <-> ui
        runtime_bind(&rt, &m, &ui);

        // Enter the main loop, till the user quits or makes a reconfig that
        // requires everything to be re-configured
        runtime_run(&rt, &m, &ui);

        // Shut everything down
        ui_shutdown(&ui);
        apple2_shutdown(&m);
        runtime_shutdown(&rt);

        // If a re-config is called for, go back and do it all over
    } while(ui.reconfig);

    // If the ini file needs to be saved, save it
    if(ini_get(&ini_store, "State", "save")) {
        util_ini_save_file(o.inifile, &ini_store);
    }

    // Shut the ini file down and then exit
    ini_shutdown(&ini_store);

    return A2_OK;
}
