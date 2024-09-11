#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#define TCP_PORT 5100 /* ������ ��Ʈ ��ȣ */
#define MAX_CLIENTS 100 /* �ִ� Ŭ���̾�Ʈ �� */

typedef struct userinfo {
    char userName[10];
    char message[BUFSIZ];
    int csockId;
} userinfo;

int main(int argc, char **argv) {
    int ssock;                 /* ���� ��ũ���� ���� */
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr; /* �ּ� ����ü ���� */
    int pipe_fd[MAX_CLIENTS][2]; /* ������ ���� ��ũ���� �迭 */

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

    /* ������ non-blocking ���� ���� */
    int flags = fcntl(ssock, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(ssock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        return -1;
    }

    /* ���ÿ� �����ϴ� Ŭ���̾�Ʈ�� ó���� ���� ��� ť�� ���� */
    if (listen(ssock, 8) < 0) {
        perror("listen()");
        return -1;
    }

    clen = sizeof(cliaddr);
    int client_count = 0;

    while (1) {
        /* Ŭ���̾�Ʈ�� �����ϸ� ������ ����ϰ� Ŭ���̾�Ʈ ���� ���� */
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);

        if (csock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // �θ� ���μ����� �б� ��ũ���͸� ����Ͽ� �ڽ��� �����͸� ����
                for (int i = 0; i < client_count; i++) {
                    userinfo user;
                    int n = read(pipe_fd[i][0], &user, sizeof(user));
                    if (n > 0) {
                        // �ڽ� ���μ����� ���� �̸��� �޽����� ���
                        printf("Parent received from child (User: %s): %s\n", user.userName, user.message);
                    }
                }
                continue;
            } else {
                perror("accept()");
                continue;
            }
        }

        printf("client count %d\n", client_count);

        flags = fcntl(csock, F_GETFL, 0);
        if (flags < 0) {
            perror("fcntl(F_GETFL)");
            return -1;
        }

        // O_NONBLOCK �÷��׸� �����Ͽ� ���ŷ ���� ����
        if (fcntl(csock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            perror("fcntl(F_SETFL)");
            return -1;
        }

        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected.\n");
            close(csock);
            continue;
        }

        /* ���ο� ������ ���� */
        if (pipe(pipe_fd[client_count]) == -1) {
            perror("pipe");
            return -1;
        }

        // pipe_fd[i][0]�� �񵿱� ���� ����
        flags = fcntl(pipe_fd[client_count][0], F_GETFL, 0);
        if (flags == -1) {
            perror("fcntl F_GETFL");
            exit(EXIT_FAILURE);
        }
        if (fcntl(pipe_fd[client_count][0], F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl F_SETFL");
            exit(EXIT_FAILURE);
        }

        // Ŭ���̾�Ʈ ������ ���� ����ü ���� �� �ʱ�ȭ
        userinfo user;
        snprintf(user.userName, sizeof(user.userName), "%d%c", client_count + 1, 'a' + client_count);
        user.csockId = csock;

        if (fork() == 0) { // �ڽ� ���μ���
            close(pipe_fd[client_count][0]); // �б� ��ũ���� �ݱ� (�ڽ��� ���� ����)
            close(ssock); // �ڽ� ���μ����� ���� ������ ������� ����

            /* ��Ʈ��ũ �ּҸ� ���ڿ��� ���� */
            inet_ntop(AF_INET, &cliaddr.sin_addr, user.message, BUFSIZ);
            printf("Client is connected : %s\n", user.message);
            fflush(stdout); // ���ۿ� �ִ� �����͸� ������ ���

            while (1) {
                int n = read(csock, user.message, BUFSIZ); // Ŭ���̾�Ʈ�κ��� ������ �б�
                if (n == 0) {
                    // Ŭ���̾�Ʈ�� ������ ������
                    printf("Client disconnected.\n");
                    break;
                } else if (n < 0) {
                    // ���� ���� �߻�
                    perror("read issue()");
                    break;
                }

                user.message[n] = '\0';
                printf("Received data from %s: %s\n", user.userName, user.message);
                fflush(stdout); // ���ۿ� �ִ� �����͸� ������ ���

                /* �θ� ���μ������� ���� �޽����� ���� */
                write(pipe_fd[client_count][1], &user, sizeof(user));

                if (strncmp(user.message, "...", 3) == 0) {
                    close(csock);
                    break;
                }
            }

            close(pipe_fd[client_count][1]); // �ڽ��� ���� ��ũ���� �ݱ�
            exit(0);
        }

        // �θ� ���μ���
        close(pipe_fd[client_count][1]); // �θ�� ���� ��ũ���� �ݱ�
        client_count++;

        // ���� ���μ��� ����
        // while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    for (int i = 0; i < client_count; i++) {
        close(pipe_fd[i][0]);
        close(pipe_fd[i][1]);
    }
    close(ssock); // ���� ������ ����
    return 0;
}
