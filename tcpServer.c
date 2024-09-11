#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#define TCP_PORT 5100 /* ������ ��Ʈ ��ȣ */

int main(int argc, char **argv) {
    int ssock;                 /* ���� ��ũ���� ���� */
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr; /* �ּ� ����ü ���� */
    char mesg[BUFSIZ];
    int pipe_fd[2];            /* ������ ���� ��ũ����: pipe_fd[0]�� �б�, pipe_fd[1]�� ���� */

    /* ������ ���� */
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        return -1;
    }

    /* ���� ���� ���� */
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    /* �ּ� ����ü�� �ּ� ���� */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT); /* ����� ��Ʈ ���� */

    /* bind �Լ��� ����Ͽ� ���� ������ �ּ� ���� */
    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind()");
        return -1;
    }

    /* ���ÿ� �����ϴ� Ŭ���̾�Ʈ�� ó���� ���� ��� ť�� ���� */
    if (listen(ssock, 8) < 0) {
        perror("listen()");
        return -1;
    }

    clen = sizeof(cliaddr);
    while (1) {
        /* Ŭ���̾�Ʈ�� �����ϸ� ������ ����ϰ� Ŭ���̾�Ʈ ���� ���� */
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);
        if (csock < 0) {
            perror("accept()");
            continue;
        }

        if (fork() == 0) { // �ڽ� ���μ���
            close(pipe_fd[0]); // �б� ��ũ���� �ݱ� (�ڽ��� ���� ����)

            /* ��Ʈ��ũ �ּҸ� ���ڿ��� ���� */
            inet_ntop(AF_INET, &cliaddr.sin_addr, mesg, BUFSIZ);
            printf("Client is connected : %s\n", mesg);
            fflush(stdout); // ���ۿ� �ִ� �����͸� ������ ���
            while (1) {
                int n = read(csock, mesg, BUFSIZ); // Ŭ���̾�Ʈ�κ��� ������ �б�
                if (n <= 0) {
                    perror("read()");
                    break;
                }

                mesg[n] = '\0';
                printf("Received data : %s\n", mesg);
                fflush(stdout); // ���ۿ� �ִ� �����͸� ������ ���
                /* �θ� ���μ������� ���� �޽����� ���� */
                write(pipe_fd[1], mesg, n);

                if (strncmp(mesg, "...", 3) == 0) {
                    close(csock);
                    break;
                }

                /* Ŭ���̾�Ʈ�� �޽��� ���� */
//                if (write(csock, mesg, n) <= 0)
//                    perror("write()");
            }

            close(pipe_fd[1]); // �ڽ��� ���� ��ũ���� �ݱ�
            exit(0);
        }

        // �θ� ���μ����� �б� ��ũ���͸� ����Ͽ� �ڽ��� �����͸� ����
        close(pipe_fd[1]); // �θ�� ���� ��ũ���� �ݱ�
        while (1) {
            int n = read(pipe_fd[0], mesg, BUFSIZ);
            if (n > 0) {
                mesg[n] = '\0';
                printf("Parent received from child: %s\n", mesg); // �ڽ� ���μ����� �޽����� ���
            } else if (n == 0) {
                // �������� �����ٴ� ���� �ڽ� ���μ����� ����Ǿ����� �ǹ�
                break;
            } else {
                perror("read()");
                break;
            }
        }
        while (waitpid(-1, NULL, WNOHANG) > 0); // ���� ���μ��� ����
    }

    close(pipe_fd[0]); // �θ��� �б� ��ũ���� �ݱ�
    close(ssock);      // ���� ������ ����
    return 0;
}
