// reuseprobe: does Wine's winhttp survive an idle keep-alive connection?
// Hypothesis: server closes idle keep-alive sockets; Wine reuses them and reports 12152.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
static FILE *out;
static DWORD one(HINTERNET con, int n, DWORD *status)
{
    HINTERNET req = WinHttpOpenRequest(con, L"GET",
        L"/community/leaderboard/getAvailableLeaderboards?title=age4",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    DWORD err = 0, code = 0; char body[200] = {0}; DWORD got = 0;
    DWORD t = GetTickCount();
    if (!req) err = GetLastError();
    else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) err = GetLastError();
    else if (!WinHttpReceiveResponse(req, NULL)) err = GetLastError();
    else {
        DWORD len = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        WinHttpReadData(req, body, sizeof(body) - 1, &got);
    }
    fprintf(out, "  request #%d %-6s err=%-6lu http=%-4lu %lums  %.60s\n",
            n, err ? "FAILED" : "ok", err, code, GetTickCount() - t, err ? "" : body);
    fflush(out);
    if (status) *status = code;
    if (req) WinHttpCloseHandle(req);
    return err;
}
int main(void)
{
    out = fopen("D:\\aoe\\reuseprobe.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== keep-alive reuse probe (same connection handle) ===\n\n");

    HINTERNET ses = WinHttpOpen(L"reuseprobe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { fprintf(out, "WinHttpOpen err=%lu\n", GetLastError()); fclose(out); return 1; }
    WinHttpSetTimeouts(ses, 10000, 10000, 10000, 20000);
    HINTERNET con = WinHttpConnect(ses, L"aoe-api.worldsedgelink.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { fprintf(out, "connect err=%lu\n", GetLastError()); fclose(out); return 1; }

    DWORD st = 0;
    one(con, 1, &st);                 // establishes the connection
    int gaps[] = { 20, 45, 75, 110 };
    int n = 2;
    for (int i = 0; i < 4; i++) {
        fprintf(out, "\n  -- idle %ds --\n", gaps[i]); fflush(out);
        Sleep(gaps[i] * 1000);
        one(con, n++, &st);
    }
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
