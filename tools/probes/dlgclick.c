// dlgclick: wait for a dialog with a given title, then click its button whose text contains a given string.
//
// AoE IV shows a modal "We were unable to determine your video card's installed driver version" dialog at
// `Loading step: [Graphics driver check]` once its "Don't show this message for [ 1 ] days" choice has expired,
// and loading waits for it. Touch input moves a relative cursor, so a tap cannot reach the button; this does.
// usage: dlgclick.exe <window title> <button text part> [wait_seconds] [outfile]
// defaults: wait 600 s, log to D:\aoe\dlgclick.txt. Exits after one click.
// Build: x86_64-w64-mingw32-gcc -O1 -static -o dlgclick.exe dlgclick.c
#include <windows.h>
#include <stdio.h>
#include <string.h>

static const char *want;
static HWND found;

static BOOL CALLBACK child(HWND h, LPARAM lp)
{
    char cls[64], text[256];
    GetClassNameA(h, cls, sizeof(cls));
    GetWindowTextA(h, text, sizeof(text));
    if (!lstrcmpiA(cls, "Button") && strstr(text, want)) { found = h; return FALSE; }
    return TRUE;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 1;
    want = argv[2];
    int wait = argc > 3 ? atoi(argv[3]) : 600;
    FILE *f = fopen(argc > 4 ? argv[4] : "D:\\aoe\\dlgclick.txt", "a");
    if (!f) return 1;
    SYSTEMTIME t;
    for (int i = 0; i < wait * 4; i++) {
        HWND dlg = FindWindowA("#32770", argv[1]);
        if (dlg) {
            found = NULL;
            EnumChildWindows(dlg, child, 0);
            GetLocalTime(&t);
            if (found) {
                SendMessageA(found, BM_CLICK, 0, 0);
                fprintf(f, "%02d:%02d:%02d clicked \"%s\" in \"%s\"\n", t.wHour, t.wMinute, t.wSecond, want, argv[1]);
            } else {
                fprintf(f, "%02d:%02d:%02d dialog found, no button containing \"%s\"\n", t.wHour, t.wMinute, t.wSecond, want);
            }
            fclose(f);
            return found ? 0 : 3;
        }
        Sleep(250);
    }
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d no dialog \"%s\" within %d s\n", t.wHour, t.wMinute, t.wSecond, argv[1], wait);
    fclose(f);
    return 2;
}
