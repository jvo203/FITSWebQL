#include <arpa/inet.h>
#include <stdio.h>
int main(int argc, char **argv) {
    char *host_ip = argc > 1 ? argv[1] : "127.0.0.1";
    char *netmask = argc > 2 ? argv[2] : "255.255.255.255";
    struct in_addr host, mask, broadcast;
    char broadcast_address[INET_ADDRSTRLEN];
    if (inet_pton(AF_INET, host_ip, &host) == 1 &&
        inet_pton(AF_INET, netmask, &mask) == 1)
        broadcast.s_addr = host.s_addr | ~mask.s_addr;
    else {
        fprintf(stderr, "Failed converting strings to numbers\n");
        return 1;
    }
    if (inet_ntop(AF_INET, &broadcast, broadcast_address, INET_ADDRSTRLEN) != NULL)
        printf("Broadcast address of %s with netmask %s is %s\n",
            host_ip, netmask, broadcast_address);
    else {
        fprintf(stderr, "Failed converting number to string\n");
        return 1;
    }
    return 0;
}
