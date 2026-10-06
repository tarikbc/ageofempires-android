// netprobe: test the Wine network and crypto paths that AoE IV touches at NetworkGlobal.
// Each test runs in its own thread with a time limit, so a hang is reported instead of blocking.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <stdio.h>

FILE *out;
#define LOG(...) do { printf(__VA_ARGS__); fprintf(out, __VA_ARGS__); fflush(out); fflush(stdout); } while (0)

static const char *HOST = "worldsedge_cardinal.bugsplat.com";
const wchar_t *WHOST = L"worldsedge_cardinal.bugsplat.com";
const wchar_t *WPATH = L"/api/fullDumpFlag?database=worldsedge_cardinal";

DWORD tick0;
DWORD ms(void) { return GetTickCount() - tick0; }

static DWORD WINAPI t_dns(void *p)
{
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    int r = getaddrinfo(HOST, "443", &hints, &res);
    LOG("  getaddrinfo=%d\n", r);
    if (!r) {
        char buf[64]; inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, buf, sizeof(buf));
        LOG("  addr=%s\n", buf);
        freeaddrinfo(res);
    }
    return 0;
}

static int connect_nb(const char *ip, int port)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    u_long nb = 1;
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons(port); inet_pton(AF_INET, ip, &sa.sin_addr);
    ioctlsocket(s, FIONBIO, &nb);
    int r = connect(s, (struct sockaddr *)&sa, sizeof(sa));
    int err = WSAGetLastError();
    LOG("  connect=%d wsaerr=%d at %lu ms\n", r, r ? err : 0, ms());
    fd_set wr, ex; FD_ZERO(&wr); FD_ZERO(&ex); FD_SET(s, &wr); FD_SET(s, &ex);
    TIMEVAL tv = {5, 0};
    r = select(0, NULL, &wr, &ex, &tv);
    LOG("  select=%d writable=%d except=%d at %lu ms\n", r, FD_ISSET(s, &wr), FD_ISSET(s, &ex), ms());
    int so = 0, len = sizeof(so);
    getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&so, &len);
    LOG("  SO_ERROR=%d\n", so);
    closesocket(s);
    return 0;
}

static char resolved[64] = "52.70.252.81";
static DWORD WINAPI t_connect_remote(void *p) { return connect_nb(resolved, 443); }
static DWORD WINAPI t_connect_refused(void *p) { return connect_nb("127.0.0.1", 9); }

static DWORD WINAPI t_connect_blocking(void *p)
{
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET; sa.sin_port = htons(443); inet_pton(AF_INET, resolved, &sa.sin_addr);
    int r = connect(s, (struct sockaddr *)&sa, sizeof(sa));
    LOG("  blocking connect=%d wsaerr=%d at %lu ms\n", r, r ? WSAGetLastError() : 0, ms());
    closesocket(s);
    return 0;
}

static DWORD WINAPI t_adapters(void *p)
{
    ULONG size = 0;
    ULONG r = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &size);
    LOG("  GetAdaptersAddresses(size query)=%lu size=%lu\n", r, size);
    if (size) {
        IP_ADAPTER_ADDRESSES *a = malloc(size);
        r = GetAdaptersAddresses(AF_UNSPEC, 0, NULL, a, &size);
        LOG("  GetAdaptersAddresses=%lu\n", r);
        for (IP_ADAPTER_ADDRESSES *i = (r == 0 ? a : NULL); i; i = i->Next)
            LOG("    %ls type=%lu oper=%d maclen=%lu\n", i->FriendlyName, i->IfType, i->OperStatus, i->PhysicalAddressLength);
        free(a);
    }
    size = 0;
    r = GetAdaptersInfo(NULL, &size);
    LOG("  GetAdaptersInfo(size query)=%lu size=%lu\n", r, size);
    if (size) {
        IP_ADAPTER_INFO *a = malloc(size);
        r = GetAdaptersInfo(a, &size);
        LOG("  GetAdaptersInfo=%lu\n", r);
        free(a);
    }
    char name[256]; DWORD n = sizeof(name);
    BOOL ok = GetComputerNameExA(ComputerNameDnsFullyQualified, name, &n);
    LOG("  GetComputerNameEx=%d '%s'\n", ok, ok ? name : "");
    return 0;
}

static void alg(const wchar_t *id)
{
    BCRYPT_ALG_HANDLE h = NULL;
    NTSTATUS s = BCryptOpenAlgorithmProvider(&h, id, NULL, 0);
    LOG("  open %ls = 0x%08lx\n", id, (unsigned long)s);
    if (h) BCryptCloseAlgorithmProvider(h, 0);
}

static DWORD WINAPI t_bcrypt(void *p)
{
    unsigned char rnd[16];
    LOG("  BCryptGenRandom=0x%08lx\n", (unsigned long)BCryptGenRandom(NULL, rnd, sizeof(rnd), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    alg(BCRYPT_RNG_ALGORITHM); alg(BCRYPT_SHA1_ALGORITHM); alg(BCRYPT_SHA256_ALGORITHM); alg(BCRYPT_SHA512_ALGORITHM);
    alg(BCRYPT_MD5_ALGORITHM); alg(BCRYPT_AES_ALGORITHM); alg(BCRYPT_RSA_ALGORITHM);
    alg(BCRYPT_ECDH_P256_ALGORITHM); alg(BCRYPT_ECDH_P384_ALGORITHM); alg(BCRYPT_ECDSA_P256_ALGORITHM);
    alg(L"ECDH"); alg(BCRYPT_PBKDF2_ALGORITHM);

    BCRYPT_ALG_HANDLE h = NULL; BCRYPT_KEY_HANDLE k1 = NULL, k2 = NULL; BCRYPT_SECRET_HANDLE sec = NULL;
    NTSTATUS s = BCryptOpenAlgorithmProvider(&h, BCRYPT_ECDH_P256_ALGORITHM, NULL, 0);
    if (!s) {
        s = BCryptGenerateKeyPair(h, &k1, 256, 0); LOG("  ecdh genkey1=0x%08lx\n", (unsigned long)s);
        if (!s) { s = BCryptFinalizeKeyPair(k1, 0); LOG("  ecdh finalize1=0x%08lx\n", (unsigned long)s); }
        s = BCryptGenerateKeyPair(h, &k2, 256, 0);
        if (!s) s = BCryptFinalizeKeyPair(k2, 0);
        if (!s) { s = BCryptSecretAgreement(k1, k2, &sec, 0); LOG("  ecdh agreement=0x%08lx\n", (unsigned long)s); }
        if (sec) {
            ULONG got = 0; unsigned char key[64];
            s = BCryptDeriveKey(sec, BCRYPT_KDF_RAW_SECRET, NULL, key, sizeof(key), &got, 0);
            LOG("  ecdh derive raw=0x%08lx len=%lu\n", (unsigned long)s, got);
        }
    }
    h = NULL;
    s = BCryptOpenAlgorithmProvider(&h, BCRYPT_RSA_ALGORITHM, NULL, 0);
    if (!s) {
        BCRYPT_KEY_HANDLE k = NULL;
        s = BCryptGenerateKeyPair(h, &k, 2048, 0);
        if (!s) s = BCryptFinalizeKeyPair(k, 0);
        LOG("  rsa 2048 genkey=0x%08lx\n", (unsigned long)s);
    }
    return 0;
}

static DWORD WINAPI t_winhttp(void *p)
{
    HINTERNET ses = WinHttpOpen(L"netprobe/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    LOG("  WinHttpOpen=%p err=%lu\n", ses, ses ? 0 : GetLastError());
    if (!ses) return 0;
    WinHttpSetTimeouts(ses, 5000, 5000, 5000, 5000);
    HINTERNET con = WinHttpConnect(ses, WHOST, 443, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", WPATH, NULL, NULL, NULL, WINHTTP_FLAG_SECURE) : NULL;
    BOOL ok = req && WinHttpSendRequest(req, NULL, 0, NULL, 0, 0, 0);
    LOG("  WinHttpSendRequest=%d err=%lu at %lu ms\n", ok, ok ? 0 : GetLastError(), ms());
    if (ok) {
        ok = WinHttpReceiveResponse(req, NULL);
        DWORD code = 0, sz = sizeof(code);
        if (ok) WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        LOG("  WinHttpReceiveResponse=%d err=%lu status=%lu at %lu ms\n", ok, ok ? 0 : GetLastError(), code, ms());
    }
    return 0;
}

static void run(const char *name, LPTHREAD_START_ROUTINE fn, DWORD limit)
{
    LOG("[%s] start at %lu ms\n", name, ms());
    HANDLE t = CreateThread(NULL, 0, fn, NULL, 0, NULL);
    DWORD w = WaitForSingleObject(t, limit);
    LOG("[%s] %s at %lu ms\n", name, w == WAIT_OBJECT_0 ? "done" : "HANG (time limit)", ms());
}

int main(int argc, char **argv)
{
    out = fopen(argc > 1 ? argv[1] : "D:\\netprobe.txt", "w");
    tick0 = GetTickCount();
    WSADATA wd;
    LOG("WSAStartup=%d\n", WSAStartup(MAKEWORD(2, 2), &wd));
    run("dns", t_dns, 15000);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    if (!getaddrinfo(HOST, NULL, &hints, &res)) {
        inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, resolved, sizeof(resolved));
        freeaddrinfo(res);
    }
    run("adapters", t_adapters, 15000);
    run("bcrypt", t_bcrypt, 30000);
    run("connect_refused_nonblocking", t_connect_refused, 15000);
    run("connect_remote_nonblocking", t_connect_remote, 15000);
    run("connect_remote_blocking", t_connect_blocking, 15000);
    run("winhttp_https", t_winhttp, 30000);
    LOG("END at %lu ms\n", ms());
    fclose(out);
    ExitProcess(0);
}
