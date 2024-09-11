#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#define TCP_PORT 5100

int main(int argc, char **argv)
{
    printf("This is a fork-based client\n");
    int ssock;
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];

    if (argc < 2) {
        printf("Usage: %s IP_ADDRESS\n", argv[0]);
        return -1;
    }

    /* ������ ���� */
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    /* ������ ������ �ּ� ���� */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    /* ���ڿ��� ��Ʈ��ũ �ּҷ� ���� */
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(TCP_PORT);

    /* ������ �ּҷ� ���� */
    if (connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork()");
        return -1;
    }

    if (pid == 0) {
        // �ڽ� ���μ���: �����κ��� ������ �ޱ�
        while (1) {
            int n = recv(ssock, mesg, sizeof(mesg) - 1, 0);
            if (n <= 0) {
                if (n == 0) {
                    printf("������ ������ �����߽��ϴ�.\n");
                } else {
                    perror("recv()");
                }
                close(ssock);
                exit(0);
            }
            mesg[n] = '\0'; // ���� �޽��� null ����
            printf("%s", mesg); // �������� ���� �޽��� ���
            fflush(stdout); // ��� ���۸� ��� ����
        }
    } else {
        // �θ� ���μ���: Ű���� �Է� �ޱ�
        // ǥ�� �Է��� ����ŷ ���� ����
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags < 0) {
            perror("fcntl(F_GETFL)");
            return -1;
        }
        if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("fcntl(F_SETFL)");
            return -1;
        }

        while (1) {
            ssize_t len = fgets(mesg, sizeof(mesg), stdin) != NULL ? strlen(mesg) : 0;
            if (len > 0) {
                // �������� �����͸� �񵿱�� ����
                if (send(ssock, mesg, len, MSG_DONTWAIT) <= 0) {
                    perror("send()");
                    close(ssock);
                    exit(0);
                }
            }
            usleep(100000); // 0.1�� ����
        }
    }

    return 0;
}
