// xinputprobe: report which XInput pads are connected inside the Wine prefix and sample their state for a few
// seconds (buttons and left stick), to see whether GameNative's virtual gamepad reaches Windows programs.
// usage: xinputprobe.exe [outfile] [seconds]    defaults: D:\aoe\xinput.txt 10
#include <windows.h>
#include <xinput.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *out = fopen(argc > 1 ? argv[1] : "D:\\aoe\\xinput.txt", "w");
    if (!out) return 1;
    int secs = argc > 2 ? atoi(argv[2]) : 10;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
        XINPUT_CAPABILITIES caps;
        DWORD r = XInputGetCapabilities(i, 0, &caps);
        fprintf(out, "pad %lu: %s", i, r == ERROR_SUCCESS ? "connected" : "not connected");
        if (r == ERROR_SUCCESS) fprintf(out, " type=%u subtype=%u flags=%#x", caps.Type, caps.SubType, caps.Flags);
        fprintf(out, "\n");
    }
    fflush(out);
    DWORD last[XUSER_MAX_COUNT] = { 0 };
    for (int t = 0; t < secs * 20; t++) {
        for (DWORD i = 0; i < XUSER_MAX_COUNT; i++) {
            XINPUT_STATE s;
            if (XInputGetState(i, &s) != ERROR_SUCCESS) continue;
            if (s.dwPacketNumber != last[i]) {
                last[i] = s.dwPacketNumber;
                fprintf(out, "%5d ms pad %lu packet %lu buttons=%#06x lx=%6d ly=%6d rx=%6d ry=%6d lt=%3u rt=%3u\n", t * 50, i,
                        s.dwPacketNumber, s.Gamepad.wButtons, s.Gamepad.sThumbLX, s.Gamepad.sThumbLY, s.Gamepad.sThumbRX,
                        s.Gamepad.sThumbRY, s.Gamepad.bLeftTrigger, s.Gamepad.bRightTrigger);
                fflush(out);
            }
        }
        Sleep(50);
    }
    fprintf(out, "END\n");
    fclose(out);
    return 0;
}
