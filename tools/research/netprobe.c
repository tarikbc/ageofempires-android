// netprobe: isolate where the network path breaks inside the Wine session.
// DNS -> TCP connect -> plain HTTP -> HTTPS (WinHTTP), each with a timeout.
#include <windows.h>
#include <winhttp.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

static FILE *out;

static DWORD now_ms(void) { return GetTickCount(); }

static void test_dns(const char *host)
{
    ADDRINFOA hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    DWORD t = now_ms();
    int rc = getaddrinfo(host, "443", &hints, &res);
    fprintf(out, "DNS  %-28s rc=%d (%lu ms)", host, rc, now_ms() - t);
    if (rc == 0 && res) {
        struct sockaddr_in *sa = (struct sockaddr_in *)res->ai_addr;
        char ip[32]; inet_ntop(AF_INET, &sa->sin_addr, ip, sizeof(ip));
        fprintf(out, "  -> %s\n", ip);
        freeaddrinfo(res);
    } else {
        fprintf(out, "  FAILED (%d)\n", WSAGetLastError());
    }
}

static void test_tcp(const char *host, int port)
{
    ADDRINFOA hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) { fprintf(out, "TCP  %s  dns failed\n", host); return; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in sa = *(struct sockaddr_in *)res->ai_addr;
    sa.sin_port = htons((u_short)port);
    freeaddrinfo(res);
    DWORD t = now_ms();
    int rc = connect(s, (struct sockaddr *)&sa, sizeof(sa));
    fprintf(out, "TCP  %-28s rc=%d err=%d (%lu ms)\n", host, rc, rc ? WSAGetLastError() : 0, now_ms() - t);
    closesocket(s);
}

static void test_winhttp(const wchar_t *host, const wchar_t *path)
{
    HINTERNET ses = WinHttpOpen(L"netprobe/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { fprintf(out, "HTTPS %-26ls WinHttpOpen err=%lu\n", host, GetLastError()); return; }
    WinHttpSetTimeouts(ses, 8000, 8000, 8000, 15000);
    HINTERNET con = WinHttpConnect(ses, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!con) { fprintf(out, "HTTPS %-26ls connect err=%lu\n", host, GetLastError()); WinHttpCloseHandle(ses); return; }
    HINTERNET req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    DWORD t = now_ms();
    if (!req) { fprintf(out, "HTTPS %-26ls openreq err=%lu\n", host, GetLastError()); }
    else if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        fprintf(out, "HTTPS %-26ls send err=%lu (%lu ms)\n", host, GetLastError(), now_ms() - t);
    else if (!WinHttpReceiveResponse(req, NULL))
        fprintf(out, "HTTPS %-26ls recv err=%lu (%lu ms)\n", host, GetLastError(), now_ms() - t);
    else {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        fprintf(out, "HTTPS %-26ls OK status=%lu (%lu ms)\n", host, code, now_ms() - t);
    }
    if (req) WinHttpCloseHandle(req);
    WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
}

int main(void)
{
    out = fopen("D:\\aoe\\netprobe.txt", "w");
    if (!out) return 1;
    WSADATA wsa;
    int wr = WSAStartup(MAKEWORD(2, 2), &wsa);
    fprintf(out, "=== network probe ===\nWSAStartup rc=%d\n\n", wr);

    test_dns("www.ageofempires.com");
    test_dns("api.ageofempires.com");
    test_dns("www.microsoft.com");
    fprintf(out, "\n");
    test_tcp("www.ageofempires.com", 443);
    test_tcp("www.microsoft.com", 443);
    fprintf(out, "\n");
    test_winhttp(L"www.ageofempires.com", L"/");
    test_winhttp(L"www.microsoft.com", L"/");
    fprintf(out, "\nEND\n");
    fclose(out);
    WSACleanup();
    return 0;
}
