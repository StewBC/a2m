#include "app_options.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        (char *)"s7d0=/tmp/does-not-need-to-exist.po",
        (char *)"--hd",
        (char *)"s5d0=/tmp/other.po",
        (char *)"--model",
        (char *)"plus",
        (char *)"--mb-slot",
        (char *)"5",
        (char *)"--turbo",
        (char *)"1,4,max",
    };
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
        const char *path = "/tmp/a2m-test-mockingboard-section.ini";
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
            &options, 2, 1, "/tmp/live.po"));
        app_options_smartport_clear_path(&options, 2, 1);
        app_options_destroy(&options);
    }

    printf("OK app_options_mounts\n");
    return 0;
}
