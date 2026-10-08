// apiprobe: fetch a real Age of Empires backend endpoint through Wine's winhttp.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
static FILE *out;
static void get(const wchar_t *host, const wchar_t *path, const char *label)
{
    HINTERNET ses = WinHttpOpen(L"apiprobe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { fprintf(out, "%-34s WinHttpOpen err=%lu\n", label, GetLastError()); return; }
    WinHttpSetTimeouts(ses, 10000, 10000, 10000, 25000);
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { fprintf(out, "%-34s connect err=%lu\n", label, GetLastError()); WinHttpCloseHandle(ses); return; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    DWORD err = 0, code = 0; char body[400] = {0}; DWORD got = 0;
    if (!req) err = GetLastError();
    else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) err = GetLastError();
    else if (!WinHttpReceiveResponse(req, NULL)) err = GetLastError();
    else {
        DWORD len = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        WinHttpReadData(req, body, sizeof(body) - 1, &got);
    }
    fprintf(out, "%-34s %s err=%lu http=%lu bytes=%lu\n     %.120s\n",
            label, err ? "FAILED" : "ok", err, code, got, body);
    fflush(out);
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
}
int main(void)
{
    out = fopen("D:\\aoe\\apiprobe.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== real AoE backend endpoints through Wine winhttp ===\n\n");
    get(L"aoe-api.worldsedgelink.com",
        L"/community/leaderboard/getAvailableLeaderboards?title=age4", "leaderboards title=age4");
    get(L"aoe-api.worldsedgelink.com",
        L"/community/leaderboard/getAvailableLeaderboards?title=age2", "leaderboards title=age2");
    get(L"dr-activerelease1-api.worldsedgelink.com", L"/wss/", "game host /wss/ (expect 101/400)");
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
