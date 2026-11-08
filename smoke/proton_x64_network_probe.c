#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windns.h>
#include <winhttp.h>
#include <wincrypt.h>
#include <stdio.h>
#include <string.h>

static FILE *logfile;
#define LOG(...) do { fprintf(logfile, __VA_ARGS__); fputc('\n', logfile); fflush(logfile); } while (0)

static void CALLBACK status_callback(HINTERNET handle, DWORD_PTR context, DWORD status,
                                      LPVOID data, DWORD length) {
    (void)handle; (void)context;
    if (status == WINHTTP_CALLBACK_STATUS_SECURE_FAILURE && data && length == sizeof(DWORD))
        LOG("TLS secure_failure_flags=0x%08lx", *(DWORD *)data);
}

static void probe(const char *host, const wchar_t *wide) {
    struct addrinfo hints = {0}, *addresses = NULL;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    int result = getaddrinfo(host, "443", &hints, &addresses);
    LOG("HOST %s WSA_getaddrinfo=%d", host, result);
    PDNS_RECORD records = NULL;
    DNS_STATUS dns = DnsQuery_A(host, DNS_TYPE_A, DNS_QUERY_STANDARD, NULL, &records, NULL);
    LOG("HOST %s DnsQuery_A=%ld", host, dns);
    if (records) DnsRecordListFree(records, DnsFreeRecordList);
    if (!result && addresses) {
        SOCKET s = socket(addresses->ai_family, SOCK_STREAM, IPPROTO_TCP);
        if (s != INVALID_SOCKET) {
            u_long nonblocking = 1; ioctlsocket(s, FIONBIO, &nonblocking);
            int connected = connect(s, addresses->ai_addr, (int)addresses->ai_addrlen);
            int error = connected == 0 ? 0 : WSAGetLastError();
            if (connected != 0 && (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS)) {
                fd_set ready, failed; FD_ZERO(&ready); FD_ZERO(&failed);
                FD_SET(s, &ready); FD_SET(s, &failed);
                struct timeval timeout = {8, 0};
                int selected = select(0, NULL, &ready, &failed, &timeout);
                if (selected > 0) {
                    int size = sizeof(error);
                    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&error, &size)) error = WSAGetLastError();
                } else error = selected == 0 ? WSAETIMEDOUT : WSAGetLastError();
            }
            LOG("HOST %s TCP443_error=%d", host, error);
            closesocket(s);
        } else LOG("HOST %s socket_error=%d", host, WSAGetLastError());
    }
    if (addresses) freeaddrinfo(addresses);
    for (int secure = 0; secure <= 1; ++secure) {
        HINTERNET session = WinHttpOpen(L"HMDeck-network-diagnostic/1", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                       WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) { LOG("HOST %s WinHttpOpen_error=%lu", host, GetLastError()); continue; }
        WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
        WinHttpSetStatusCallback(session, status_callback, WINHTTP_CALLBACK_FLAG_SECURE_FAILURE, 0);
        HINTERNET connection = WinHttpConnect(session, wide, secure ? 443 : 80, 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", L"/", NULL,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0) : NULL;
        if (!request) LOG("HOST %s HTTPS=%d open_error=%lu", host, secure, GetLastError());
        else {
            DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
            DWORD started = GetTickCount();
            if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
                LOG("HOST %s HTTPS=%d send_error=%lu elapsed_ms=%lu", host, secure, GetLastError(), GetTickCount()-started);
            else if (!WinHttpReceiveResponse(request, NULL))
                LOG("HOST %s HTTPS=%d receive_error=%lu elapsed_ms=%lu", host, secure, GetLastError(), GetTickCount()-started);
            else {
                DWORD code = 0, size = sizeof(code);
                if (WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX))
                    LOG("HOST %s HTTPS=%d HTTP_status=%lu elapsed_ms=%lu", host, secure, code, GetTickCount()-started);
                else LOG("HOST %s HTTPS=%d status_error=%lu", host, secure, GetLastError());
            }
            WinHttpCloseHandle(request);
        }
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
    }
}

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--trace-child")) {
        char path[MAX_PATH], command[MAX_PATH+32];
        if (!GetModuleFileNameA(NULL, path, sizeof(path))) return 3;
        snprintf(command, sizeof(command), "\"%s\" --trace-child", path);
        FILE *ca = fopen("C:\\activation-ca-path.txt", "r");
        if (ca) {
            char location[4096];
            if (fgets(location, sizeof(location), ca)) {
                location[strcspn(location, "\r\n")] = 0;
                if (location[0] == '/') SetEnvironmentVariableA("SSL_CERT_FILE", location);
            }
            fclose(ca);
        }
        /* Only public GET probes run in this child: no activation requests or tokens. */
        SetEnvironmentVariableA("WINEDEBUG", "-all,err+all,warn+winhttp,warn+secur32,trace+secur32,trace+winhttp");
        STARTUPINFOA startup = {0}; PROCESS_INFORMATION child = {0}; startup.cb = sizeof(startup);
        if (!CreateProcessA(NULL, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &child)) return 4;
        DWORD status = WaitForSingleObject(child.hProcess, 35000), code = 5;
        if (status == WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess, &code);
        else TerminateProcess(child.hProcess, 5);
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
        return (int)code;
    }
    logfile = fopen("C:\\activation-network.log", "w");
    if (!logfile) return 2;
    SYSTEMTIME utc; GetSystemTime(&utc);
    LOG("BEGIN pid=%lu UTC=%04u-%02u-%02uT%02u:%02u:%02uZ certificate_validation=enabled redirects=disabled proxy=direct",
        GetCurrentProcessId(), utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);
    char ca_path[4096]; LOG("SSL_CERT_FILE_present=%d", GetEnvironmentVariableA("SSL_CERT_FILE", ca_path, sizeof(ca_path)) > 0);
    HCRYPTPROV provider = 0;
    BOOL acquired = CryptAcquireContextW(&provider, NULL, MS_ENH_RSA_AES_PROV_W, PROV_RSA_AES, CRYPT_VERIFYCONTEXT);
    LOG("RSA_AES_provider_ok=%d error=0x%08lx", acquired, acquired ? 0UL : GetLastError());
    if (acquired) CryptReleaseContext(provider, 0);
    WSADATA wsa; int status = WSAStartup(MAKEWORD(2,2), &wsa);
    LOG("WSAStartup=%d", status);
    if (!status) {
        probe("support.codefusion.technology", L"support.codefusion.technology");
        probe("store.steampowered.com", L"store.steampowered.com");
        WSACleanup();
    }
    LOG("END"); fclose(logfile);
    return status != 0;
}
