#include "app_options.h"
#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir _rmdir
#define A2M_TEST_TMP "a2m-test-tmp"
#else
#include <unistd.h>
#define A2M_TEST_TMP "/tmp"
#endif

static void expect_true(const char *name, int v)
{
    if (!v) {
        fprintf(stderr, "FAIL: %s\n", name);
        exit(1);
    }
}

static void set_ini_path(app_options *options, const char *path)
{
    size_t length = strlen(path);
    char *copy = (char *)malloc(length + 1u);
    expect_true("allocate INI path", copy != NULL);
    memcpy(copy, path, length + 1u);
    free(options->ini_path);
    options->ini_path = copy;
}

int main(void)
{
    app_options options;
    int slot = 0;
    int unit = 0;
    int model = -1;
    char *argv1[] = {
        (char *)"a2m",
        (char *)"--noini",
        (char *)"--disk",
        (char *)"s6d0=tests/fixtures/Apple DOS 3.3 January 1983.nib",
        (char *)"--disk",
        (char *)"s6d1=tests/fixtures/Apple DOS 3.3 January 1983.nib",
        (char *)"--hd",
        (char *)"s7d0=" A2M_TEST_TMP "/does-not-need-to-exist.po",
        (char *)"--hd",
        (char *)"s5d0=" A2M_TEST_TMP "/other.po",
        (char *)"--model",
        (char *)"plus",
        (char *)"--mb-slot",
        (char *)"5",
        (char *)"--turbo",
        (char *)"1,4,max",
    };

    expect_true("mkdir test tmp", mkdir(A2M_TEST_TMP, 0755) == 0 || errno == EEXIST);
    char *argv2[] = {
        (char *)"a2m",
        (char *)"--noini",
        (char *)"-d",
        (char *)"tests/fixtures/Apple DOS 3.3 January 1983.nib",
    };
    char *argv3[] = {
        (char *)"a2m",
        (char *)"--noini",
        (char *)"--model",
        (char *)"enh",
        (char *)"--mb-slot",
        (char *)"0",
    };

    expect_true("parse key s6d0", app_options_parse_slot_unit_key("s6d0", &slot, &unit));
    expect_true("slot 6", slot == 6 && unit == 0);
    expect_true("bad key", !app_options_parse_slot_unit_key("x6d0", &slot, &unit));
    expect_true("model plus", app_model_from_string("plus", &model) && model == 1);
    expect_true("model enh", app_model_from_string("enh", &model) && model == 0);
    expect_true("label plus", strcmp(app_model_label(1), "][+") == 0);
    expect_true("label enh", strcmp(app_model_label(0), "//e Enhanced") == 0);

    expect_true(
        "parse multi",
        app_options_load_startup(&options, (int)(sizeof(argv1) / sizeof(argv1[0])), argv1));
    expect_true("two diskii", options.diskii_count == 2);
    expect_true("disk0 s6d0", options.diskii[0].slot == 6 && options.diskii[0].drive == 0);
    expect_true("disk1 s6d1", options.diskii[1].slot == 6 && options.diskii[1].drive == 1);
    expect_true("two sp", options.smartport_count == 2);
    expect_true("sp s7d0", options.smartport[0].slot == 7 && options.smartport[0].unit == 0);
    expect_true("sp s5d0", options.smartport[1].slot == 5 && options.smartport[1].unit == 0);
    expect_true("model plus flag", options.apple_model == 1);
    expect_true("mb slot 5", options.mb_slot == 5);
    expect_true("slot 5 Mockingboard", options.slot_cards[5] == APP_SLOT_CARD_MOCKINGBOARD);
    expect_true("slot 6 Disk II", options.slot_cards[6] == APP_SLOT_CARD_DISKII);
    expect_true("slot 7 SmartPort", options.slot_cards[7] == APP_SLOT_CARD_SMARTPORT);
    expect_true(
        "convenience s6d0",
        options.disk_s6d0 != NULL && strstr(options.disk_s6d0, "Apple DOS") != NULL);
    app_options_destroy(&options);

    expect_true(
        "parse bare disk",
        app_options_load_startup(&options, (int)(sizeof(argv2) / sizeof(argv2[0])), argv2));
    expect_true("one diskii", options.diskii_count == 1);
    expect_true("default s6d0", options.diskii[0].slot == 6 && options.diskii[0].drive == 0);
    app_options_destroy(&options);

    expect_true(
        "parse model enh mb off",
        app_options_load_startup(&options, (int)(sizeof(argv3) / sizeof(argv3[0])), argv3));
    expect_true("model enh default", options.apple_model == 0);
    expect_true("mb off", options.mb_slot == 0);
    expect_true("slot 4 empty", options.slot_cards[4] == APP_SLOT_CARD_EMPTY);
    app_options_destroy(&options);

    /* Slot selection is authoritative and selecting a second Mockingboard moves it. */
    app_options_init(&options);
    expect_true("default MB slot 4", options.slot_cards[4] == APP_SLOT_CARD_MOCKINGBOARD);
    expect_true("select MB slot 2",
        app_options_set_slot_card(&options, 2, APP_SLOT_CARD_MOCKINGBOARD));
    expect_true("old MB cleared", options.slot_cards[4] == APP_SLOT_CARD_EMPTY);
    expect_true("new MB selected",
        options.slot_cards[2] == APP_SLOT_CARD_MOCKINGBOARD && options.mb_slot == 2);
    expect_true("select empty", app_options_set_slot_card(&options, 2, APP_SLOT_CARD_EMPTY));
    expect_true("MB disabled", options.mb_slot == 0);
    app_options_destroy(&options);

    /* Explicit [Slots] keys retain installed controllers even when they have
       no mounted media. */
    app_options_init(&options);
    expect_true("load slot-card INI",
        app_options_apply_ini_file(&options, "../tests/fixtures/slot_cards.ini"));
    expect_true("slot INI model", options.apple_model == 1);
    expect_true("slot INI MB",
        options.slot_cards[2] == APP_SLOT_CARD_MOCKINGBOARD && options.mb_slot == 2);
    expect_true("slot INI SmartPort 3", options.slot_cards[3] == APP_SLOT_CARD_SMARTPORT);
    expect_true("slot INI SmartPort boot", options.smartport_boot_slot == 3);
    expect_true("slot INI empty 4", options.slot_cards[4] == APP_SLOT_CARD_EMPTY);
    expect_true("slot INI Disk II 6", options.slot_cards[6] == APP_SLOT_CARD_DISKII);
    expect_true("slot INI SmartPort 7", options.slot_cards[7] == APP_SLOT_CARD_SMARTPORT);
    expect_true("unmounted controllers have no paths",
        options.diskii_count == 0 && options.smartport_count == 0);
    app_options_destroy(&options);

    /* Mockingboard has no standalone INI section: only [Slots] selects it,
       and saving removes obsolete section entries from an existing file. */
    {
        const char *path = A2M_TEST_TMP "/a2m-test-mockingboard-section.ini";
        FILE *file = fopen(path, "w");
        config *saved;
        expect_true("create obsolete Mockingboard INI", file != NULL);
        fputs("[Mockingboard]\nslot=2\ns2d0=\n", file);
        expect_true("close obsolete Mockingboard INI", fclose(file) == 0);

        app_options_init(&options);
        expect_true("load obsolete Mockingboard INI",
            app_options_apply_ini_file(&options, path));
        expect_true("obsolete Mockingboard section ignored",
            options.slot_cards[2] == APP_SLOT_CARD_EMPTY &&
            options.slot_cards[4] == APP_SLOT_CARD_MOCKINGBOARD);
        expect_true("move Mockingboard through slot model",
            app_options_set_slot_card(&options, 2, APP_SLOT_CARD_MOCKINGBOARD));
        options.original_del = true;
        options.smartport_boot_slot = 7;
        set_ini_path(&options, path);
        expect_true("save Slots-only INI", app_options_save_shutdown(&options));

        saved = config_load(path);
        expect_true("reload Slots-only INI", saved != NULL);
        expect_true("Mockingboard section removed",
            config_get(saved, "Mockingboard", "slot") == NULL &&
            config_get(saved, "Mockingboard", "s2d0") == NULL);
        expect_true("Mockingboard persisted in Slots",
            config_get(saved, "Slots", "slot2") != NULL &&
            strcmp(config_get(saved, "Slots", "slot2"), "mockingboard") == 0);
        expect_true("original DEL persisted",
            config_get(saved, "config", "original_del") != NULL &&
            strcmp(config_get(saved, "config", "original_del"), "true") == 0);
        expect_true("SmartPort boot slot persisted",
            config_get(saved, "SmartPort", "boot_slot") != NULL &&
            strcmp(config_get(saved, "SmartPort", "boot_slot"), "7") == 0);
        config_destroy(saved);
        app_options_destroy(&options);
        expect_true("remove temporary INI", remove(path) == 0);
    }

    /* The six browse-folder keys load in the same order consumed by frontend. */
    app_options_init(&options);
    expect_true("load browse paths INI",
        app_options_apply_ini_file(&options, "../tests/fixtures/browse_paths.ini"));
    expect_true("browse assembler", strcmp(options.browse_dirs[0], "asm") == 0);
    expect_true("browse floppy", strcmp(options.browse_dirs[1], "floppy") == 0);
    expect_true("browse smartport", strcmp(options.browse_dirs[2], "smartport") == 0);
    expect_true("browse binary", strcmp(options.browse_dirs[3], "binary") == 0);
    expect_true("browse basic", strcmp(options.browse_dirs[4], "basic") == 0);
    expect_true("browse snapshot", strcmp(options.browse_dirs[5], "snapshot") == 0);
    expect_true(
        "assembler file",
        options.assembler_file != NULL &&
            strstr(options.assembler_file, "sources/example.s") != NULL);
    app_options_destroy(&options);

    /* Multi-image queue: same drive twice appends. */
    {
        char *argv_q[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--disk",
            (char *)"s6d0=tests/fixtures/a.nib",
            (char *)"--disk",
            (char *)"s6d0=tests/fixtures/b.nib",
            (char *)"--disk",
            (char *)"s6d1=tests/fixtures/c.nib",
        };
        expect_true(
            "parse queue",
            app_options_load_startup(
                &options, (int)(sizeof(argv_q) / sizeof(argv_q[0])), argv_q));
        expect_true("three diskii entries", options.diskii_count == 3);
        expect_true("queue d0 count 2",
            app_options_diskii_queue(&options, 6, 0)->count == 2);
        expect_true("queue d1 count 1",
            app_options_diskii_queue(&options, 6, 1)->count == 1);
        expect_true(
            "queue d0[0]",
            app_options_diskii_queue(&options, 6, 0)->paths[0] != NULL &&
                strstr(app_options_diskii_queue(&options, 6, 0)->paths[0], "a.nib") != NULL);
        expect_true(
            "queue d0[1]",
            app_options_diskii_queue(&options, 6, 0)->paths[1] != NULL &&
                strstr(app_options_diskii_queue(&options, 6, 0)->paths[1], "b.nib") != NULL);
        app_options_destroy(&options);
    }

    /* Slot 1 is a normal peripheral slot; queues remain distinct by slot. */
    {
        char *argv_q[] = {
            (char *)"a2m", (char *)"--noini",
            (char *)"--disk", (char *)"s1d0=tests/fixtures/a.nib",
            (char *)"--disk", (char *)"s5d0=tests/fixtures/b.nib",
        };
        expect_true("parse arbitrary slots", app_options_load_startup(
            &options, (int)(sizeof(argv_q) / sizeof(argv_q[0])), argv_q));
        expect_true("s1 queue", app_options_diskii_queue(&options, 1, 0)->count == 1);
        expect_true("s5 queue", app_options_diskii_queue(&options, 5, 0)->count == 1);
        expect_true("live append s1", app_options_diskii_append_path(
            &options, 1, 0, "tests/fixtures/c.nib"));
        expect_true("live append reflected", app_options_diskii_queue(
            &options, 1, 0)->count == 2);
        expect_true("live eject s1", app_options_diskii_eject_current(&options, 1, 0));
        expect_true("live eject reflected", app_options_diskii_queue(
            &options, 1, 0)->count == 1);
        expect_true("SmartPort live set", app_options_smartport_set_path(
            &options, 2, 1, A2M_TEST_TMP "/live.po"));
        app_options_smartport_clear_path(&options, 2, 1);
        app_options_destroy(&options);
    }

    /* INI [DiskII] comma-separated (and quoted) lists build a multi-image queue.
       Relative paths resolve against the INI file's directory, not cwd.
       apply_convenience_paths must not collapse that queue to the first path. */
    {
        const char *dir = A2M_TEST_TMP "/a2m-test-ini-rel";
        const char *media_dir = A2M_TEST_TMP "/media";
        const char *path = A2M_TEST_TMP "/a2m-test-ini-rel/game.ini";
        FILE *file;
        expect_true("mkdir INI-relative dir", mkdir(dir, 0755) == 0 || errno == EEXIST);
        expect_true("mkdir media dir", mkdir(media_dir, 0755) == 0 || errno == EEXIST);
        file = fopen(A2M_TEST_TMP "/media/side A.nib", "wb");
        expect_true("touch side A", file != NULL && fclose(file) == 0);
        file = fopen(A2M_TEST_TMP "/media/side B.nib", "wb");
        expect_true("touch side B", file != NULL && fclose(file) == 0);
        file = fopen(path, "w");
        expect_true("create DiskII queue INI", file != NULL);
        fputs(
            "[DiskII]\n"
            "s6d0 = \"../media/side A.nib\",\"../media/side B.nib\"\n"
            "s6d1 = " A2M_TEST_TMP "/util.dsk\n",
            file);
        expect_true("close DiskII queue INI", fclose(file) == 0);

        app_options_init(&options);
        expect_true("load DiskII queue INI", app_options_apply_ini_file(&options, path));
        expect_true("queue INI three mounts", options.diskii_count == 3);
        expect_true("queue INI d0 count",
            app_options_diskii_queue(&options, 6, 0)->count == 2);
        expect_true("queue INI d0[0] resolved vs INI",
            options.diskii[0].path != NULL &&
                strstr(options.diskii[0].path, "/media/side A.nib") != NULL &&
                strstr(options.diskii[0].path, "../media/") == NULL);
        expect_true("queue INI d0[1] resolved vs INI",
            options.diskii[1].path != NULL &&
                strstr(options.diskii[1].path, "/media/side B.nib") != NULL);
        expect_true("queue INI d1 stays absolute",
            options.diskii[2].path != NULL &&
                strstr(options.diskii[2].path, A2M_TEST_TMP "/util.dsk") != NULL);

        app_options_sync_convenience_paths(&options);
        expect_true(
            "convenience joined",
            options.disk_s6d0 != NULL &&
                strstr(options.disk_s6d0, "side A.nib") != NULL &&
                strstr(options.disk_s6d0, "side B.nib") != NULL);
        expect_true("apply convenience keeps queue",
            app_options_apply_convenience_paths(&options));
        expect_true("queue still two after convenience",
            app_options_diskii_queue(&options, 6, 0)->count == 2);

        set_ini_path(&options, path);
        expect_true("save DiskII queue INI", app_options_save_shutdown(&options));
        app_options_destroy(&options);

        app_options_init(&options);
        expect_true("reload saved DiskII queue INI",
            app_options_apply_ini_file(&options, path));
        expect_true("reload queue count 2",
            app_options_diskii_queue(&options, 6, 0)->count == 2);
        expect_true("reload still has A",
            options.diskii[0].path != NULL &&
                strstr(options.diskii[0].path, "side A.nib") != NULL);
        expect_true("reload still has B",
            options.diskii[1].path != NULL &&
                strstr(options.diskii[1].path, "side B.nib") != NULL);
        app_options_destroy(&options);
        expect_true("remove DiskII queue INI", remove(path) == 0);
        (void)remove(A2M_TEST_TMP "/media/side A.nib");
        (void)remove(A2M_TEST_TMP "/media/side B.nib");
        (void)rmdir(media_dir);
        (void)rmdir(dir);
    }

    /* Configure Apply/Save must keep live media mounts: eject clears live options
       while the dialog snapshot can still hold the old SmartPort paths. */
    {
        const char *path = A2M_TEST_TMP "/a2m-test-sp-eject-save.ini";
        app_options live;
        app_options dialog;
        config *saved;
        FILE *file = fopen(path, "w");
        expect_true("create SP eject INI", file != NULL);
        fputs(
            "[Slots]\n"
            "slot6=diskii\n"
            "slot7=smartport\n"
            "[SmartPort]\n"
            "s7d0=" A2M_TEST_TMP "/a.po\n"
            "s7d1=" A2M_TEST_TMP "/b.po\n",
            file);
        expect_true("close SP eject INI", fclose(file) == 0);

        app_options_init(&live);
        expect_true("load SP eject INI", app_options_apply_ini_file(&live, path));
        expect_true("two SP before eject", live.smartport_count == 2);
        app_options_smartport_clear_path(&live, 7, 0);
        app_options_smartport_clear_path(&live, 7, 1);
        expect_true("live cleared after eject", live.smartport_count == 0);

        /* Stale Configure snapshot still has the pre-eject mounts. */
        app_options_init(&dialog);
        expect_true("load stale dialog snapshot",
            app_options_apply_ini_file(&dialog, path));
        expect_true("dialog still mounted", dialog.smartport_count == 2);

        /* Simulate CONFIG_APPLY / SAVE_INI_NOW: take dialog settings, then put
           live media back. */
        expect_true(
            "replace keeps live empty mounts",
            app_options_replace_media_mounts(&dialog, &live));
        expect_true("dialog media now empty", dialog.smartport_count == 0);
        expect_true("slot card still SmartPort",
            dialog.slot_cards[7] == APP_SLOT_CARD_SMARTPORT);

        set_ini_path(&dialog, path);
        expect_true("save after eject", app_options_save_shutdown(&dialog));
        saved = config_load(path);
        expect_true("reload after eject save", saved != NULL);
        expect_true(
            "s7d0 empty after eject save",
            config_get(saved, "SmartPort", "s7d0") != NULL &&
                config_get(saved, "SmartPort", "s7d0")[0] == '\0');
        expect_true(
            "s7d1 empty after eject save",
            config_get(saved, "SmartPort", "s7d1") != NULL &&
                config_get(saved, "SmartPort", "s7d1")[0] == '\0');
        config_destroy(saved);
        app_options_destroy(&dialog);
        app_options_destroy(&live);
        expect_true("remove SP eject INI", remove(path) == 0);
    }

    /* samples/golf.ini: ../disks/... is relative to the INI, not the process cwd.
       disks/ is gitignored — path resolution is always checked; media existence
       is optional and skipped when the local images are absent. */
    {
        const char *golf_paths[] = {
            "../samples/golf.ini",
            "samples/golf.ini",
            NULL,
        };
        const char *golf = NULL;
        int g;
        for (g = 0; golf_paths[g] != NULL; ++g) {
            FILE *probe = fopen(golf_paths[g], "r");
            if (probe != NULL) {
                fclose(probe);
                golf = golf_paths[g];
                break;
            }
        }
        if (golf != NULL) {
            app_options_init(&options);
            expect_true("load samples/golf.ini", app_options_apply_ini_file(&options, golf));
            expect_true("golf two Disk II mounts", options.diskii_count == 2);
            expect_true("golf side A resolved",
                options.diskii[0].path != NULL &&
                    strstr(options.diskii[0].path,
                           "World Class Leader Board (1987)(Access)(Side A).do") != NULL &&
                    strstr(options.diskii[0].path, "../disks/") == NULL);
            expect_true("golf side B resolved",
                options.diskii[1].path != NULL &&
                    strstr(options.diskii[1].path,
                           "World Class Leader Board (1987)(Access)(Side B).do") != NULL &&
                    strstr(options.diskii[1].path, "../disks/") == NULL);
            {
                FILE *media_a = fopen(options.diskii[0].path, "rb");
                FILE *media_b = fopen(options.diskii[1].path, "rb");
                if (media_a != NULL && media_b != NULL) {
                    expect_true("close golf side A", fclose(media_a) == 0);
                    expect_true("close golf side B", fclose(media_b) == 0);
                } else {
                    if (media_a != NULL) {
                        fclose(media_a);
                    }
                    if (media_b != NULL) {
                        fclose(media_b);
                    }
                }
            }
            app_options_destroy(&options);
        }
    }

    /* Inspector config: default off, INI/CLI, 0 honoured, garbage → default. */
    {
        char *argv_inspector[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--inspector",
            (char *)"--inspector-memory",
            (char *)"64",
        };
        char *argv_no[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--no-inspector",
        };
        char *argv_zero[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--inspector-memory",
            (char *)"0",
        };
        char *argv_bad[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--inspector-memory",
            (char *)"5",
        };
        const char *path = A2M_TEST_TMP "/a2m-test-inspector.ini";
        FILE *file;
        config *saved;

        app_options_init(&options);
        expect_true("inspector default off", !options.inspector);
        expect_true("inspector budget default 128", options.inspector_memory_mb == 128);
        app_options_destroy(&options);

        expect_true(
            "CLI --inspector",
            app_options_load_startup(
                &options, (int)(sizeof(argv_inspector) / sizeof(argv_inspector[0])), argv_inspector));
        expect_true("CLI inspector on", options.inspector);
        expect_true("CLI inspector budget 64", options.inspector_memory_mb == 64);
        app_options_destroy(&options);

        expect_true(
            "CLI --no-inspector",
            app_options_load_startup(
                &options, (int)(sizeof(argv_no) / sizeof(argv_no[0])), argv_no));
        expect_true("CLI inspector off", !options.inspector);
        app_options_destroy(&options);

        expect_true(
            "CLI inspector budget 0",
            app_options_load_startup(
                &options,
                (int)(sizeof(argv_zero) / sizeof(argv_zero[0])),
                argv_zero));
        expect_true("CLI 0 honoured", options.inspector_memory_mb == 0);
        app_options_destroy(&options);

        expect_true(
            "CLI inspector budget 5 rejected",
            !app_options_load_startup(
                &options, (int)(sizeof(argv_bad) / sizeof(argv_bad[0])), argv_bad));
        app_options_destroy(&options);

        file = fopen(path, "w");
        expect_true("create inspector INI", file != NULL);
        fputs(
            "[debug]\n"
            "inspector = 1\n"
            "inspector_memory_mb = 0\n"
            "history_memory_mb = 0\n"
            "frame_ring_memory_mb = 32\n",
            file);
        expect_true("close inspector INI", fclose(file) == 0);
        app_options_init(&options);
        expect_true("load inspector INI", app_options_apply_ini_file(&options, path));
        expect_true("INI inspector on", options.inspector);
        expect_true("INI inspector budget 0", options.inspector_memory_mb == 0);
        expect_true("INI history 0", options.history_memory_mb == 0);
        expect_true("INI frame 32", options.frame_ring_memory_mb == 32);
        set_ini_path(&options, path);
        expect_true("save inspector INI", app_options_save_shutdown(&options));
        app_options_destroy(&options);

        saved = config_load(path);
        expect_true("reload inspector INI", saved != NULL);
        expect_true(
            "saved inspector",
            config_get(saved, "debug", "inspector") != NULL &&
                strcmp(config_get(saved, "debug", "inspector"), "true") == 0);
        expect_true(
            "saved inspector budget 0",
            config_get(saved, "debug", "inspector_memory_mb") != NULL &&
                strcmp(config_get(saved, "debug", "inspector_memory_mb"), "0") ==
                    0);
        config_destroy(saved);

        file = fopen(path, "w");
        expect_true("create garbage inspector INI", file != NULL);
        fputs("[debug]\ninspector_memory_mb = no-thanks\n", file);
        expect_true("close garbage inspector INI", fclose(file) == 0);
        app_options_init(&options);
        expect_true("load garbage inspector INI", app_options_apply_ini_file(&options, path));
        expect_true("garbage inspector budget → default", options.inspector_memory_mb == 128);
        app_options_destroy(&options);

        file = fopen(path, "w");
        expect_true("create legacy TM INI", file != NULL);
        fputs(
            "[debug]\n"
            "timemachine = 1\n"
            "timemachine_memory_mb = 64\n",
            file);
        expect_true("close legacy TM INI", fclose(file) == 0);
        app_options_init(&options);
        expect_true("load legacy TM INI", app_options_apply_ini_file(&options, path));
        expect_true("legacy timemachine ignored", !options.inspector);
        expect_true("legacy TM budget ignored", options.inspector_memory_mb == 128);
        set_ini_path(&options, path);
        options.inspector = true;
        expect_true("save drops legacy TM keys", app_options_save_shutdown(&options));
        app_options_destroy(&options);
        saved = config_load(path);
        expect_true("reload after drop", saved != NULL);
        expect_true(
            "legacy timemachine key gone",
            config_get(saved, "debug", "timemachine") == NULL);
        expect_true(
            "legacy TM budget key gone",
            config_get(saved, "debug", "timemachine_memory_mb") == NULL);
        expect_true(
            "inspector written",
            config_get(saved, "debug", "inspector") != NULL &&
                strcmp(config_get(saved, "debug", "inspector"), "true") == 0);
        config_destroy(saved);
        expect_true("remove inspector INI", remove(path) == 0);
    }

    {
        char *argv_green[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--video-display",
            (char *)"green",
        };
        char *argv_colour_amber[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--video-display",
            (char *)"colour,amber",
        };
        char *argv_color[] = {
            (char *)"a2m",
            (char *)"--noini",
            (char *)"--video-display",
            (char *)"color",
        };
        app_options scratch;

        app_options_init(&scratch);
        expect_true("default colour", scratch.colour_display);
        expect_true("default white", scratch.mono_mode == APP_MONO_WHITE);
        expect_true(
            "apply green token",
            app_options_apply_video_display_arg(&scratch, "green"));
        expect_true("green is mono", !scratch.colour_display);
        expect_true("green phosphor", scratch.mono_mode == APP_MONO_GREEN);
        expect_true(
            "colour leaves phosphor",
            app_options_apply_video_display_arg(&scratch, "colour"));
        expect_true("back to colour", scratch.colour_display);
        expect_true("phosphor still green", scratch.mono_mode == APP_MONO_GREEN);
        expect_true(
            "compound colour,white",
            app_options_apply_video_display_arg(&scratch, "color, white"));
        expect_true("compound stays colour", scratch.colour_display);
        expect_true("compound sets white", scratch.mono_mode == APP_MONO_WHITE);
        expect_true(
            "bad token rejected",
            !app_options_apply_video_display_arg(&scratch, "blue"));
        app_options_destroy(&scratch);

        expect_true(
            "cli green",
            app_options_load_startup(
                &options,
                (int)(sizeof(argv_green) / sizeof(argv_green[0])),
                argv_green));
        expect_true("cli green mono", !options.colour_display);
        expect_true("cli green mode", options.mono_mode == APP_MONO_GREEN);
        app_options_destroy(&options);

        expect_true(
            "cli colour,amber",
            app_options_load_startup(
                &options,
                (int)(sizeof(argv_colour_amber) / sizeof(argv_colour_amber[0])),
                argv_colour_amber));
        expect_true("cli compound colour", options.colour_display);
        expect_true("cli compound amber", options.mono_mode == APP_MONO_AMBER);
        app_options_destroy(&options);

        expect_true(
            "cli color spelling",
            app_options_load_startup(
                &options,
                (int)(sizeof(argv_color) / sizeof(argv_color[0])),
                argv_color));
        expect_true("cli color is colour", options.colour_display);
        app_options_destroy(&options);
    }

    printf("OK app_options_mounts\n");
    return 0;
}
