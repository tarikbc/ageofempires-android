// postprobe: libHttpClient-style requests through Wine's winhttp.
// The game's calls fail with 12152 (INVALID_SERVER_RESPONSE) while a plain GET works.
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
static FILE *out;
static void req(const wchar_t *host, const wchar_t *path, const wchar_t *verb,
                const char *body, const wchar_t *ctype, const wchar_t *extra,
                const char *label)
{
    HINTERNET ses = WinHttpOpen(L"postprobe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { fprintf(out, "%-38s WinHttpOpen err=%lu\n", label, GetLastError()); return; }
    WinHttpSetTimeouts(ses, 10000, 10000, 10000, 20000);
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { fprintf(out, "%-38s connect err=%lu\n", label, GetLastError()); WinHttpCloseHandle(ses); return; }
    HINTERNET r = WinHttpOpenRequest(con, verb, path, NULL, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    DWORD err = 0, code = 0, t = GetTickCount();
    if (!r) err = GetLastError();
    else {
        if (ctype) {
            wchar_t hdr[256];
            _snwprintf(hdr, 256, L"Content-Type: %ls%ls", ctype, extra ? extra : L"");
            WinHttpAddRequestHeaders(r, hdr, -1, WINHTTP_ADDREQ_FLAG_ADD);
        }
        DWORD blen = body ? (DWORD)strlen(body) : 0;
        if (!WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                (LPVOID)body, blen, blen, 0)) err = GetLastError();
        else if (!WinHttpReceiveResponse(r, NULL)) err = GetLastError();
        else {
            DWORD len = sizeof(code);
            WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        }
    }
    fprintf(out, "%-38s %-6s err=%-6lu http=%-4lu %lums\n",
            label, err ? "FAILED" : "ok", err, code, GetTickCount() - t);
    fflush(out);
    if (r) WinHttpCloseHandle(r);
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
}
int main(void)
{
    out = fopen("D:\\aoe\\postprobe.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== libHttpClient-style requests ===\n\n");
    const wchar_t *H = L"dr-activerelease1-api.worldsedgelink.com";
    const char *J = "{\"clientLibVersion\":191,\"operation\":0,\"sessionToken\":\"probe\"}";

    req(H, L"/wss/", L"GET", NULL, NULL, NULL, "GET /wss/ (baseline)");
    req(H, L"/game/login/readSession", L"POST", J, L"application/json", NULL, "POST readSession (json body)");
    req(H, L"/game/login/readSession", L"POST", J, L"application/json",
        L"\r\nExpect:", "POST readSession (Expect suppressed)");
    req(H, L"/game/Challenge/getChallenges", L"POST", J, L"application/json", NULL, "POST getChallenges");
    req(H, L"/game/account/getProfileProperty", L"POST", J, L"application/json; charset=utf-8", NULL,
        "POST getProfileProperty (charset)");
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
