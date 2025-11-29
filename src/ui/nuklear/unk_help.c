// Apple ][+ and //e Enhanced emulator with assembler
// Stefan Wessels, 2025
// This is free and unencumbered software released into the public domain.

#include "unk_lib.h"

// These are defined in unk_view and used in nuklrsdl.h, hence globals
extern float sdl_x_scale, sdl_y_scale;

void unk_show_help(UNK *v) {
    struct nk_context *ctx = v->ctx;
    SDL_Rect r = v->sdl_os_rect;
    r.w /= sdl_x_scale;
    r.h /= sdl_y_scale;
    if(nk_begin(ctx, "Help", nk_rect(r.x, r.y, r.w, r.h), NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_label_colored(ctx, "Apple ][+ emulator by Stefan Wessels, 2025.", NK_TEXT_CENTERED,
                         color_help_master);
        nk_layout_row_dynamic(ctx, r.h - 55, 1);
        if(nk_group_begin(ctx, "Help Pages", 0)) {
            if(v->help_page == 0) {
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "While emulation is running:", NK_TEXT_ALIGN_LEFT, color_help_notice);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "All keys go to the emulated machine (The Apple ][+), except for the function keys.  Function keys always go to the emulator.", NK_TEXT_ALIGN_LEFT, color_help_key_heading);
                nk_layout_row_dynamic(ctx, 13, 2);
                nk_label(ctx, "F1  - Help.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F9  - Set a breakpoint at the cursor PC.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F2  - Show / Hide debugger windows.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F10 - Step over - Single step but not into a JSR call.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F3  - Toggle emulation speed between 1 MHZ and as fast as possible.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F11 - Step into - Single step, even into a JSR call.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F5  - Run (Go) when emulation is stopped.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F11 + Shift - Step out - Step past RTS at this calling level.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F6  - Set Program Counter (PC) to cursor PC.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F12 - Switch between color/mono and also RGB mode if double hires.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + SCRLK/PAUSE - Reset the machine.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "F12 + Shift - Switch to Franklin 80 col, if available, on ][+.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "While emulation is stopped:", NK_TEXT_ALIGN_LEFT, color_help_notice);
                nk_label_colored(ctx, "If the debug view is visible, all keys go to the debug window over which the mouse is hovered.", NK_TEXT_ALIGN_LEFT, color_help_key_heading);
                nk_label_colored(ctx, "CPU Window", NK_TEXT_ALIGN_LEFT, color_help_heading);
                nk_label(ctx,
                         "Click into a box to edit, i.e. PC, SP, a register or flag and change the value.  Press ENTER to make the change effective.",
                         NK_TEXT_ALIGN_LEFT);
                nk_label_colored(ctx, "Disassembly window", NK_TEXT_ALIGN_LEFT, color_help_heading);
                nk_layout_row_dynamic(ctx, 13, 2);
                nk_label(ctx, "CTRL + g - Set cursor PC to address.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CURSOR UP/DOWN   - Move the cursor PC by a line.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + p - Set Apple ][+ PC to the cursor PC .", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "PAGE UP/DOWN     - Move the cursor PC by a page.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CRTL + s - Search symbols.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "HOME/END         - Move top/bottom visible line to cursor PC.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CRTL + a - Assemble the assembler root file.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CURSOR LEFT/RIGHT- Reset cursor PC to cpu PC.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CRTL + SHIFT + a - Set assembler root file.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "TAB              - Toggle symbol display (4 possible states).", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "Memory window", NK_TEXT_ALIGN_LEFT, color_help_heading);
                nk_layout_row_dynamic(ctx, 27, 1);
                nk_label_wrap(ctx,
                              "Type HEX digits to edit the memory in HEX edit mode, or type any key when editing in ASCII mode.  The address that will be edited is shown at the bottom of the window.");
                nk_layout_row_dynamic(ctx, 13, 2);
                nk_label(ctx, "CRTL + g - Set view start to address.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CRTL + s - Search symbols.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + v - Split the view (up to 16 times).", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + f - Find by ASCII or HEX.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + j - Join a split window with the one below.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + n - Find next (forward).", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + Shift + j - Join a split window with the one above.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + Shift + n - Find previous (backwards).", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "ALT-0 through ALT-f - Select the memory view (made with CTRL+V).", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CURSOR UP/DOWN - Move the cursor a line up or down.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "CTRL + t - Toggle editing HEX or ASCII at the cursor location.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "PAGE   UP/DOWN - Move the cursor a page up or down.", NK_TEXT_ALIGN_LEFT);
                nk_spacer(ctx);
                nk_label(ctx, "HOME/END       - Move the cursor to the start/end of the line.", NK_TEXT_ALIGN_LEFT);
                nk_spacer(ctx);
                nk_label(ctx, "HOME/END +CTRL - Move the cursor to the start/end of the view.", NK_TEXT_ALIGN_LEFT);
                nk_label_colored(ctx, "Miscellaneous window", NK_TEXT_ALIGN_LEFT, color_help_heading);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label(ctx, "Note that this window updates while running, but changes can only be made while the emulation is stopped.",
                         NK_TEXT_ALIGN_LEFT);
                nk_label_colored(ctx, "Miscellaneous SmartPort", NK_TEXT_ALIGN_LEFT, color_help_sub_heading);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label(ctx, "Use the Slot.0 button to boot that disk, when stopped.  Use Eject to eject the disk and Insert will bring up a file chooser to select a new disk.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "Miscellaneous Debug", NK_TEXT_ALIGN_LEFT, color_help_sub_heading);
                nk_layout_row_dynamic(ctx, 27, 1);
                nk_label_wrap(ctx,
                              "The status shows when a Step Over or Step out is actively running.  The Step Cycles show how many cycles the last step took (for profiling) and Total Cycles show all cycles since start (will wrap).");
                nk_layout_row_dynamic(ctx, 40, 1);
                nk_label_wrap(ctx,
                              "Breakpoints come in 2 forms.  PC or address (or range).  PC shows up as 4 HEX digits.  Address shows up as R(ead) and or W(rite) access with the address or range in ['s.  With both types, there's also an optional access count (current count/trigger count).  Breakpoints can be edited, enabled/disabled, View jumps to PC of breakpoint (disabled for address) and cleared.  If multiple breakpoints, Clear All removes all breakpoints.");
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label(ctx, "The call stack shows where the JSR was called and the destination address (and label if available). Click either to set the cursor to that address.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "Miscellaneous Display", NK_TEXT_ALIGN_LEFT, color_help_sub_heading);
                nk_layout_row_dynamic(ctx, 27, 1);
                nk_label_wrap(ctx,
                              "Shows the status of the display soft-switches.  Can be overridden to, for example, see the off-screen page where the application or game may be making changes if page flipping is used.  Turning Override off will reset back to the actual machine status.");
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "Miscellaneous Language Card", NK_TEXT_ALIGN_LEFT, color_help_sub_heading);
                nk_layout_row_dynamic(ctx, 14, 1);
                nk_label_wrap(ctx,
                              "Shows the status of the language card soft-switches.  Apart from Read ROM / RAM, this is read-only information.");
                nk_label_colored(ctx, "Configuration", NK_TEXT_ALIGN_LEFT, color_help_notice);
                nk_layout_row_dynamic(ctx, 60, 1);
                nk_label_wrap(ctx,
                              "An optional apple2.ini file in the launch folder can configure option.  The sections are [display] with scale=<scale> for a uniform scaling of the emulator window/display (1.0 default).  [smartoprt] with slot=<1..7>, drive0=<path>, drive1=<path> and boot=<0|anything>, 0 is No.  Note the path does not contain \"'s and all entries are optional.  Slot 7 shows up even without an ini file but the Slot= must be set for other smartport drives to show up.  The [Video] section has Slot=<1..7> with 3 being the likely slot, and device = Franklin Ace Display the only accepted setting.");
            } else {
                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label_colored(ctx, "The built-in assembler supports these constructs:", NK_TEXT_ALIGN_LEFT, color_help_notice);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "6502 Mnemonics", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "All standard opcodes and addressing modes.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "Labels", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "Labels start with [a-z|_] and can contain those symbols, and also numbers, and end with a ':'.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "Variables", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "Values can be assigned to variables, and variables can be used in expressions.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".commands", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "Dot commands are special assembler commands, described below.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "Comments", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "The comment character is ';' and everything after ';' on a line is ignored.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "Address", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "The address character is '*' and it can be assigned and read.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 2);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "Expressions", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.80f);
                nk_label(ctx, "The assembler handles complex expressions, rules and precedence as described below.", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_dynamic(ctx, 26, 2);
                nk_label_colored(ctx, "Dot Commands:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, color_help_heading);
                nk_label_colored(ctx, "Expressions:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, color_help_heading);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".align value", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Aligns data to the next value boundary", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'*' ':' Number variables '('", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Addr, anon labels, num, variables and brackets", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".byte value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 8 bits", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'+' '-' '<' '>' '!' '~'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Unary +, -, lo & hi byte, not and binary not", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".word value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 16 bits, lo to hi", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'**'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Exponentiation (Raise to the power of)", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".dword value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 32 bits, lo to hi", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'*' '/' '%'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Multiply, divide and modulus", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".qword value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 64 bits, lo to hi", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'+' '-'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Additive (plus and minus)", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".drow value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 16 bits, hi to lo", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'<<' '>>'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Shift left and shift right", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".drowd value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 32 bits, hi to lo", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".lt .le .gt .ge", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "for relational <, <=, >, >=", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".drowq value[,value]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Outputs value as 64 bits, hi to lo", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".ne .eq", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "for != (not equal) and == (equal)", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".for <init>,<cond>,<iter>", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Starts a for loop", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'&'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Bitwise and", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".endfor", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Ends a for loop", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'^'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Exclusive or", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".if <condition>", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Start & true part of a conditional code path", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'|'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Bitwise or", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".else", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "False part of a conditional .if code path", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'&&' '||'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Logical 'and' and 'or'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".endif", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Close conditional .if / .else code path", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "'?' ':'", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Ternary Conditional", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".macro \"name\" [.*[, .*]*]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Start a macro with optional arguments", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_spacer(ctx);
                nk_layout_row_push(ctx, 0.30f);
                nk_spacer(ctx);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".endmacro", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Ends a macro", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.50f);
                nk_label(ctx, "Operator precedence is top down in the table above", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".string string[,string]*", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "string is a value or characters in \"'s", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.50f);
                nk_label(ctx, "Variables also take = <expression>, ++ and --", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".strcode", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Sets a string character transform expression", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.50f);
                nk_label(ctx, "++ and -- are pre operations, even though they have post syntax", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".include \"filename\"", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Includes the file, filename, at this address", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_spacer(ctx);
                nk_layout_row_push(ctx, 0.30f);
                nk_spacer(ctx);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".incbin \"filename\"", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Include a binary file at this address", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_spacer(ctx);
                nk_layout_row_push(ctx, 0.30f);
                nk_spacer(ctx);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, ".org value", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Another way to set the current address", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_spacer(ctx);
                nk_layout_row_push(ctx, 0.30f);
                nk_spacer(ctx);
                nk_layout_row_end(ctx);

                nk_layout_row_dynamic(ctx, 26, 2);
                nk_label_colored(ctx, "Number formats:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, color_help_heading);
                nk_label_colored(ctx, "String quoted number formats:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, color_help_heading);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "$[0-9a-f]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Hexadecimal numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "\\x[0-9a-f]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Hexadecimal numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "0[0-7]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Octal numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "\\0[0-7]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Octal numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "%[0-1]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Binary numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "\\%[0-1]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Binary numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_begin(ctx, NK_DYNAMIC, 13, 4);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "[0-9]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Decimal numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.20f);
                nk_label(ctx, "\\[0-9]+", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_push(ctx, 0.30f);
                nk_label(ctx, "Decimal numbers", NK_TEXT_ALIGN_LEFT);
                nk_layout_row_end(ctx);

                nk_layout_row_dynamic(ctx, 26, 1);
                nk_label_colored(ctx, "Notes:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_BOTTOM, color_help_heading);

                nk_layout_row_dynamic(ctx, 13, 1);
                nk_label(ctx, "The address symbol, '*', can be assigned to, or read.  When read, returns address + 1, so that something like 'lda *' will load the correct address.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "Anonymous labels are unnamed labels, for example ': lda :- ; load from the address where lda resides' or ': beq :- ; branch back to self on eq'", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "  Anonymous labels can be read forwards and backwards using + or -.  Examples: 'bra :--' to branch back two ':'s or 'beq :+' to branch to next ':'.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "Ternary conditional '<eval> ? <evaluate if true> : <evaluate if false>' takes expressions in all 3 clauses, so it can be nested.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "Variables all live in a common global scope, so beware of name collisions, especially with .for, .macro and .strcode parameters.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "Since macros take variables, a macro cannot be called with '#5', for example.  The '#' will have to be in the code.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "strcode assigns each non-quoted character in the string to a variable named _ and evaluates the expression given in .strcode and emits the result.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "  example: .strcode _ .ge 65 ? _-65 : _ .ge 41 ? _-41 : _     To resume normal pass-through processing use '.strcode _'.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "Characters can be quoted in strings to bypass .strcode processing, for example '.string \"\\A\"' will always emit 'A'.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "A for loop example: '.for _row=0, _row .lt 192, _row++ ; do for block code, up to .endfor, 192 times'.", NK_TEXT_ALIGN_LEFT);
                nk_label(ctx, "Expressions are signed (int64_t) hence .qword or .drowq may emit bad values for numbers greater than $7FFFFFFFFFFFFFFF. An assembler warning will be given.", NK_TEXT_ALIGN_LEFT);
            }
            nk_group_end(ctx);
            nk_layout_row_static(ctx, 13, 40, 3);
            nk_label(ctx, "Page:", NK_TEXT_ALIGN_LEFT | NK_TEXT_ALIGN_TOP);
            const struct nk_color active = {0xff, 0xff, 0x00, 0xff};
            struct nk_style_button style = ctx->style.button;
            if(!v->help_page) {
                style.text_active = style.text_hover = style.text_normal = active;
            }
            if(nk_button_label_styled(ctx, &style, "1")) {
                v->help_page = 0;
            }
            if(v->help_page) {
                style.text_active = style.text_hover = style.text_normal = active;
            } else {
                style = ctx->style.button;
            }
            if(nk_button_label_styled(ctx, &style, "2")) {
                v->help_page = 1;
            }
        }
    }
    nk_end(ctx);
}