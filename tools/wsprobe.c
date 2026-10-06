// wsprobe: reproduce the game's TLS breakdown outside the game.
// 1. plain HTTPS GETs to the backend host   -> expect 200
// 2. a real WebSocket to the same host, held open, reporting how long it survives
// 3. plain HTTPS GETs again                 -> do they now fail with 12157?
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>

static FILE *out;
static const wchar_t *HOST = L"dr-activerelease1-api.worldsedgelink.com";

static HINTERNET open_session(void)
{
    HINTERNET s = WinHttpOpen(L"wsprobe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) WinHttpSetTimeouts(s, 10000, 10000, 10000, 20000);
    return s;
}

// returns the WinHTTP error (0 = ok) and writes the status code
static DWORD https_get(const char *label, DWORD *status)
{
    HINTERNET ses = open_session();
    if (!ses) { fprintf(out, "  %-22s WinHttpOpen err=%lu\n", label, GetLastError()); return GetLastError(); }
    HINTERNET con = WinHttpConnect(ses, HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { DWORD e = GetLastError(); fprintf(out, "  %-22s connect err=%lu\n", label, e); WinHttpCloseHandle(ses); return e; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", L"/", NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    DWORD err = 0;
    if (!req) err = GetLastError();
    else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) err = GetLastError();
    else if (!WinHttpReceiveResponse(req, NULL)) err = GetLastError();
    else {
        DWORD len = sizeof(*status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, status, &len, WINHTTP_NO_HEADER_INDEX);
    }
    fprintf(out, "  %-22s %s (err=%lu)\n", label, err ? "FAILED" : "ok", err);
    fflush(out);
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return err;
}

static void ws_test(int max_seconds)
{
    HINTERNET ses = open_session();
    if (!ses) { fprintf(out, "  WinHttpOpen err=%lu\n", GetLastError()); return; }
    HINTERNET con = WinHttpConnect(ses, HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { fprintf(out, "  connect err=%lu\n", GetLastError()); WinHttpCloseHandle(ses); return; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", L"/", NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { fprintf(out, "  openreq err=%lu\n", GetLastError()); goto done; }

    if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0))
        { fprintf(out, "  set upgrade option err=%lu\n", GetLastError()); goto done; }
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        { fprintf(out, "  send err=%lu\n", GetLastError()); goto done; }
    if (!WinHttpReceiveResponse(req, NULL))
        { fprintf(out, "  recv err=%lu\n", GetLastError()); goto done; }

    HINTERNET ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    if (!ws) { fprintf(out, "  CompleteUpgrade err=%lu\n", GetLastError()); goto done; }
    fprintf(out, "  upgrade OK -- holding the socket open\n"); fflush(out);

    DWORD t0 = GetTickCount();
    DWORD last_send = t0;
    for (;;) {
        // keepalive: send a small text frame every 10 s
        if (GetTickCount() - last_send >= 10000) {
            DWORD se = WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                            (PVOID)"{\"k\":1}", 7);
            fprintf(out, "  [+%lus] keepalive send err=%lu\n", (GetTickCount() - t0) / 1000, se);
            fflush(out);
            last_send = GetTickCount();
            if (se != ERROR_SUCCESS) break;
        }
        BYTE buf[4096]; DWORD got = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        DWORD e = WinHttpWebSocketReceive(ws, buf, sizeof(buf), &got, &type);
        DWORD el = (GetTickCount() - t0) / 1000;
        if (e != ERROR_SUCCESS) {
            fprintf(out, "  WebSocket receive failed after %lus: err=%lu\n", el, e);
            break;
        }
        fprintf(out, "  [+%lus] frame type=%d bytes=%lu\n", el, (int)type, got); fflush(out);
        if (el >= (DWORD)max_seconds) { fprintf(out, "  held %lus, stopping (still alive)\n", el); break; }
    }
    WinHttpCloseHandle(ws);
done:
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
}

int main(void)
{
    out = fopen("D:\\aoe\\wsprobe.txt", "w");
    if (!out) return 1;
    fprintf(out, "=== TLS / WebSocket probe against the AoE backend ===\n\n");

    fprintf(out, "--- phase 1: plain HTTPS before ---\n");
    DWORD st = 0, e1 = https_get("GET / (before)", &st);

    fprintf(out, "\n--- phase 2: WebSocket, held open ---\n");
    ws_test(120);

    fprintf(out, "\n--- phase 3: plain HTTPS after ---\n");
    for (int i = 1; i <= 4; i++) {
        char lbl[64]; snprintf(lbl, sizeof(lbl), "GET / (after #%d)", i);
        st = 0;
        DWORD e = https_get(lbl, &st);
        if (i == 1) fprintf(out, "  [phase1 err=%lu  phase3-first err=%lu]\n", e1, e);
    }
    fprintf(out, "\nEND\n");
    fclose(out);
    return 0;
}
