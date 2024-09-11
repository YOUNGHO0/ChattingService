#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#define TCP_PORT 5100
#define PIPE_READ 0
#define PIPE_WRITE 1

int main(int argc, char **argv) {
    int ssock;

    struct sockaddr_in servaddr;
    int pipefd[2];  // ������ ���� ��ũ����
    char mesg[BUFSIZ];
    ssize_t n;

    if (argc < 2) {
        printf("Usage : %s IP_ADDRESS\n", argv[0]);
        return -1;
    }

    // ������ ����
    if (pipe(pipefd) < 0) {
        perror("pipe()");
        return -1;
    }

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(TCP_PORT);

    if (connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        close(ssock);
        return -1;
    }

    // �Է� ��Ʈ���� �������� ���� ������ ���𷺼�
    dup2(pipefd[PIPE_WRITE], STDOUT_FILENO);
    close(pipefd[PIPE_WRITE]);

    // ������ ������ ó���ϴ� �ڽ� ���μ��� ����
    if (fork() == 0) {
        close(pipefd[PIPE_READ]); // �ڽ� ���μ����� �б� ���� ������� ����
        while (1) {
            memset(mesg, 0, BUFSIZ);
            n = recv(ssock, mesg, BUFSIZ, 0);
            if (n > 0) {
                printf("Received data: %s", mesg);
            } else if (n == 0) {
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recv()");
                break;
            }
            usleep(100000);  // Sleep for 100 milliseconds
        }
        close(ssock);
        _exit(0);
    }

    // �θ� ���μ����� �������� �б� ���� ����Ͽ� �۽��� ó��
    close(pipefd[PIPE_WRITE]); // �θ� ���μ����� ���� ���� ������� ����
    while (1) {
        if (fgets(mesg, BUFSIZ, stdin) != NULL) {
            if (write(pipefd[PIPE_READ], mesg, strlen(mesg)) < 0) {
                perror("write()");
                break;
            }
            if (strncmp(mesg, "...", 3) == 0) {
                break;
            }
        }
    }

    close(pipefd[PIPE_READ]);
    close(ssock);

    return 0;
}
