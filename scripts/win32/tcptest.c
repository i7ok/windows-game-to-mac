/* tcptest: does GetExtendedTcpTable report owning PIDs under this Wine?
 * Opens a loopback connection to itself, then prints every IPv4 TCP row with its owner PID.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    WSADATA wsa;
    SOCKET ls, cs, as;
    struct sockaddr_in a = {0};
    int alen = sizeof(a);
    DWORD size = 0, i, ret;
    MIB_TCPTABLE_OWNER_PID *t;

    WSAStartup(MAKEWORD(2, 2), &wsa);
    ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(ls, (struct sockaddr *)&a, sizeof(a));
    listen(ls, 1);
    getsockname(ls, (struct sockaddr *)&a, &alen);
    cs = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    connect(cs, (struct sockaddr *)&a, sizeof(a));
    as = accept(ls, NULL, NULL);
    {
        char b = 'x';
        send(cs, &b, 1, 0);          /* make Wine hand the unix fds to this process */
        recv(as, &b, 1, 0);
    }
    printf("my pid=%lu (0x%lx), test listen port=%u\n", GetCurrentProcessId(), GetCurrentProcessId(), ntohs(a.sin_port));

    GetExtendedTcpTable(NULL, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    t = malloc(size);
    ret = GetExtendedTcpTable(t, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    printf("GetExtendedTcpTable ret=%lu rows=%lu\n", ret, ret ? 0 : t->dwNumEntries);
    for (i = 0; !ret && i < t->dwNumEntries; i++)
    {
        MIB_TCPROW_OWNER_PID *r = &t->table[i];
        struct in_addr la, ra;
        char ls_[32], rs_[32];
        la.s_addr = r->dwLocalAddr;
        ra.s_addr = r->dwRemoteAddr;
        strcpy(ls_, inet_ntoa(la));
        strcpy(rs_, inet_ntoa(ra));
        if (strcmp(ls_, "127.0.0.1") && strcmp(ls_, "0.0.0.0")) continue;
        printf("state=%2lu %s:%u -> %s:%u pid=%lu (0x%lx)\n", r->dwState, ls_, ntohs((u_short)r->dwLocalPort),
               rs_, ntohs((u_short)r->dwRemotePort), r->dwOwningPid, r->dwOwningPid);
    }
    closesocket(as);
    closesocket(cs);
    closesocket(ls);
    return 0;
}
