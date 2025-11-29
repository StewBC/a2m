The nuklear folder holds the implementation for a library that is the A2M UI (GUI) implemented using the nuklear immidiate mode "C" GUI library (and is called the UNK library in code).  I use Nuklear with an SDL2 backend.
Nuklear It can be found at https://github.com/Immediate-Mode-UI/Nuklear.

unk_layout does the sizing of the windows that the other classes (dasm, mem, etc) render into, controlled by unk_view, the main class.
unk_apl2 holds the rendering code to represent the apple 2 text, hgr, etc. "views" as an SDL texture that unk_view displays.
