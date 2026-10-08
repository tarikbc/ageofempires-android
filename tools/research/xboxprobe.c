// xboxprobe: fetch the endpoints the game actually calls through winhttp.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
static FILE *out;
static void get(const wchar_t *host, const wchar_t *path, const char *label)
{
    HINTERNET ses = WinHttpOpen(L"xboxprobe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { fprintf(out, "%-42s WinHttpOpen err=%lu\n", label, GetLastError()); return; }
    WinHttpSetTimeouts(ses, 10000, 10000, 10000, 25000);
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { fprintf(out, "%-42s connect err=%lu\n", label, GetLastError()); WinHttpCloseHandle(ses); return; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    DWORD err = 0, code = 0, t = GetTickCount(); char body[256] = {0}; DWORD got = 0;
    if (!req) err = GetLastError();
    else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) err = GetLastError();
    else if (!WinHttpReceiveResponse(req, NULL)) err = GetLastError();
    else {
        DWORD len = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        WinHttpReadData(req, body, sizeof(body) - 1, &got);
    }
    fprintf(out, "%-42s %-6s err=%-6lu http=%-4lu %lums  %.90s\n",
            label, err ? "FAILED" : "ok", err, code, GetTickCount() - t, err ? "" : body);
    fflush(out);
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
}
int main(void)
{
    out = fopen("D:\\aoe\\xboxprobe.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== endpoints the game calls through winhttp ===\n\n");
    get(L"title.mgt.xboxlive.com", L"/titles/default/endpoints?type=1", "title.mgt.xboxlive.com (XAL)");
    get(L"self.events.data.microsoft.com", L"/OneCollector/1.0/", "self.events.data.microsoft.com");
    get(L"aoe-api.worldsedgelink.com",
        L"/community/leaderboard/getAvailableLeaderboards?title=age4", "control: AoE backend");
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
