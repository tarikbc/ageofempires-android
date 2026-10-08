// mclick: mouse and keyboard input inside the Wine session, for menus that touch input cannot reach (GameNative's
// touch screen moves a relative cursor). Tokens, run in order:
//   c:X,Y   move the cursor to X,Y (desktop pixels) and left-click
//   r:X,Y   move and right-click
//   m:X,Y   move only
//   k:VK    press and release a virtual key (hex, e.g. k:1b = Esc, k:0d = Enter)
//   w:MS    wait
// usage: mclick.exe TOKEN [TOKEN ...]  or  mclick.exe @FILE (tokens in a file)    log: D:\aoe2\mclick.txt
// Used to drive Age of Empires II: DE's menus in the tests (docs/guides/AOE2-DE.md).
// Build: x86_64-w64-mingw32-gcc -O1 -static -mwindows -o mclick.exe mclick.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static void mouse(DWORD down, DWORD up)
{
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = down;
    in[1].mi.dwFlags = up;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(60);
    SendInput(1, &in[1], sizeof(INPUT));
}

static void key(WORD vk)
{
    INPUT in[2] = {0};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[0].ki.wScan = in[1].ki.wScan = (WORD)MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in[0], sizeof(INPUT));
    Sleep(60);
    SendInput(1, &in[1], sizeof(INPUT));
}

int WINAPI WinMain(HINSTANCE a, HINSTANCE b, LPSTR cmd, int d)
{
    FILE *f = fopen("D:\\aoe2\\mclick.txt", "a");
    static char buf[8192];
    if (cmd[0] == '@') {  // @FILE: read the tokens from a file (winhandler passes at most 51 bytes)
        FILE *in = fopen(cmd + 1, "r");
        size_t n = in ? fread(buf, 1, sizeof(buf) - 1, in) : 0;
        if (in) fclose(in);
        buf[n] = 0;
        for (size_t i = 0; i < n; i++) if (buf[i] == '\r' || buf[i] == '\n') buf[i] = ' ';
        cmd = buf;
    }
    char *tok = strtok(cmd, " ");
    while (tok) {
        int x = 0, y = 0;
        if ((tok[0] == 'c' || tok[0] == 'r' || tok[0] == 'm') && sscanf(tok + 2, "%d,%d", &x, &y) == 2) {
            SetCursorPos(x, y);
            Sleep(80);
            if (tok[0] == 'c') mouse(MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
            if (tok[0] == 'r') mouse(MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
        } else if (tok[0] == 'k') {
            key((WORD)strtol(tok + 2, NULL, 16));
        } else if (tok[0] == 'w') {
            Sleep(atoi(tok + 2));
        }
        if (f) fprintf(f, "%s\n", tok);
        Sleep(120);
        tok = strtok(NULL, " ");
    }
    POINT p;
    GetCursorPos(&p);
    if (f) { fprintf(f, "cursor %ld,%ld\n", p.x, p.y); fclose(f); }
    return 0;
}
