// timingtest: is the multi-second TLS delay on the AoE backend caused by revocation checking?
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#ifndef SECURITY_FLAG_IGNORE_REVOCATION
#define SECURITY_FLAG_IGNORE_REVOCATION 0x00000080   /* wininet value; mingw omits it */
#endif
static FILE *out;
static void timed(const wchar_t *host, const wchar_t *path, const char *label, DWORD secflags)
{
    HINTERNET ses = WinHttpOpen(L"timingtest/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { fprintf(out, "%-40s WinHttpOpen err=%lu\n", label, GetLastError()); return; }
    WinHttpSetTimeouts(ses, 15000, 15000, 15000, 30000);
    DWORD t0 = GetTickCount();
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    DWORD t_conn = GetTickCount();
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : NULL;
    DWORD err = 0, code = 0;
    DWORD t_send = 0, t_recv = 0;
    if (req && secflags) {
        DWORD f = secflags;
        WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &f, sizeof(f));
    }
    if (!con) err = GetLastError();
    else if (!req) err = GetLastError();
    else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) err = GetLastError();
    else {
        t_send = GetTickCount();
        if (!WinHttpReceiveResponse(req, NULL)) err = GetLastError();
        else {
            DWORD len = sizeof(code);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        }
    }
    t_recv = GetTickCount();
    fprintf(out, "%-40s %s err=%-5lu http=%-4lu  connect=%-6lums  tls+send+recv=%-6lums  total=%lums\n",
            label, err ? "FAILED" : "ok", err, code, t_conn - t0, t_recv - t_send, t_recv - t0);
    fflush(out);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    WinHttpCloseHandle(ses);
}
int main(void)
{
    out = fopen("D:\\aoe\\timingtest.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== TLS timing: is it revocation checking? ===\n\n");
    const wchar_t *P = L"/community/leaderboard/getAvailableLeaderboards?title=age4";
    timed(L"aoe-api.worldsedgelink.com", P, "aoe-api default", 0);
    timed(L"aoe-api.worldsedgelink.com", P, "aoe-api IGNORE_REVOCATION",
          SECURITY_FLAG_IGNORE_REVOCATION);
    timed(L"aoe-api.worldsedgelink.com", P, "aoe-api IGNORE_ALL (rev+ca+cn+wrongusage+date)",
          SECURITY_FLAG_IGNORE_REVOCATION | SECURITY_FLAG_IGNORE_UNKNOWN_CA |
          SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
          SECURITY_FLAG_IGNORE_CERT_DATE_INVALID);
    timed(L"www.microsoft.com", L"/", "microsoft default (control)", 0);
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
