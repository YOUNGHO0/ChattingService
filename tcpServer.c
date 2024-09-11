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
#include <signal.h>

#define TCP_PORT 5100 /* ������ ��Ʈ ��ȣ */
#define MAX_CLIENTS 100 /* �ִ� Ŭ���̾�Ʈ �� */
#define MAX_USERS 100 /* �ִ� ����� �� */

typedef struct userinfo {
    char userName[10];
    char message[BUFSIZ];
    int csockId;
} userinfo;

// �α��� ������ �����ϴ� ����ü
typedef struct userLoginInfo {
    char userId[20];
    char password[20];
} userLoginInfo;

userLoginInfo users[MAX_USERS]; // ����� ������ ������ �迭
int user_count = 0; // ���� ��ϵ� ����� ��
int client_count = 0;
int  client_csock[MAX_USERS];


void setFdFlagAndListen(int ssock);

void broadcastToClients(int client_count, const int *client_csock, userinfo *user);

void setCsockFlag(int flags, int csock);
void setPipeWithNonblock( int pipe_fd[][2], int client_count);

void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user);

void handleUserLogin(int csock, userinfo *user);

void setSignalHandler();

int findUserIndex(char *userId, char *password) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].userId, userId) == 0 && strcmp(users[i].password, password) == 0) {
            return i; // ����� ã��
        }
    }
    return -1; // ����� ����
}

void handleClient(int csock, int client_index, int pipe_fd[][2], struct sockaddr_in cliaddr) {
    userinfo user;
    user.csockId = csock;

    close(pipe_fd[client_index][0]); // �ڽ��� �б� ��ũ���� �ݱ�
    setNetworkConnection(&cliaddr, &user);

    handleUserLogin(csock, &user);

    while (1) {
        int n = read(csock, user.message, BUFSIZ); // Ŭ���̾�Ʈ�κ��� ������ �б�
        if (n == 0) {
            printf("Client disconnected.\n");
            break;
        } else if (n < 0) {
            perror("read issue()");
            break;
        }

        user.message[n] = '\0';
        printf("Received data from %s: %s\n", user.userName, user.message);
        /* �θ� ���μ������� ���� �޽����� ���� */
        write(pipe_fd[client_index][1], &user, sizeof(user));

        if (strncmp(user.message, "...", 3) == 0) {
            close(csock);
            break;
        }
    }

    close(pipe_fd[client_index][1]); // �ڽ��� ���� ��ũ���� �ݱ�
    exit(0);
}

void handleUserLogin(int csock, userinfo *user) {// �α��� �Ǵ� ȸ������ ���� �����
    char choice[10];
    write(csock, "�α��� �Ǵ� ȸ�������� �����ϼ��� (login/signup): ", strlen("�α��� �Ǵ� ȸ�������� �����ϼ��� (login/signup): "));
    read(csock, choice, sizeof(choice));
    choice[strcspn(choice, "\n")] = '\0'; // �ٹٲ� ����

    if (strcmp(choice, "login") == 0) {
        // �α��� ó��
        char userId[20], password[20];
        write(csock, "���̵�: ", strlen("���̵�: "));
        read(csock, userId, sizeof(userId));
        userId[strcspn(userId, "\n")] = '\0'; // �ٹٲ� ����

        write(csock, "��й�ȣ: ", strlen("��й�ȣ: "));
        read(csock, password, sizeof(password));
        password[strcspn(password, "\n")] = '\0'; // �ٹٲ� ����

        int userIndex = findUserIndex(userId, password);
        if (userIndex != -1) {
            write(csock, "�α��� ����!\n", strlen("�α��� ����!\n"));
        } else {
            write(csock, "�α��� ����! �߸��� ���̵� �Ǵ� ��й�ȣ�Դϴ�.\n", strlen("�α��� ����! �߸��� ���̵� �Ǵ� ��й�ȣ�Դϴ�.\n"));
            close(csock);
            exit(0);
        }
    } else if (strcmp(choice, "signup") == 0) {
        // ȸ������ ó��
        if (user_count >= MAX_USERS) {
            write(csock, "�ִ� ����� ���� �����߽��ϴ�. ȸ�������� �Ұ����մϴ�.\n", strlen("�ִ� ����� ���� �����߽��ϴ�. ȸ�������� �Ұ����մϴ�.\n"));
            close(csock);
            exit(0);
        }

        char userId[20], password[20];
        write(csock, "�� ���̵�: ", strlen("�� ���̵�: "));
        read(csock, userId, sizeof(userId));
        userId[strcspn(userId, "\n")] = '\0'; // �ٹٲ� ����

        write(csock, "�� ��й�ȣ: ", strlen("�� ��й�ȣ: "));
        read(csock, password, sizeof(password));
        password[strcspn(password, "\n")] = '\0'; // �ٹٲ� ����

        // ����� �߰�
        strcpy(users[user_count].userId, userId);
        strcpy(users[user_count].password, password);
        strcpy((*user).userName, userId);
        user_count++;
        write(csock, "ȸ������ ����!\n", strlen("ȸ������ ����!\n"));
    } else {
        write(csock, "�߸��� �����Դϴ�. ������ �����մϴ�.\n", strlen("�߸��� �����Դϴ�. ������ �����մϴ�.\n"));
        close(csock);
        printf("���� �ݱ� : %d\n", csock);
        exit(0);
    }
}

void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user) {/* ��Ʈ��ũ �ּҸ� ���ڿ��� ���� */
    inet_ntop(AF_INET, &(*cliaddr).sin_addr, (*user).message, BUFSIZ);
    printf("Client is connected : %s\n", (*user).message);
    fflush(stdout);
}


// SIGCHLD ��ȣ ó����
void handle_sigchld(int sig) {
    int status;
    pid_t pid;

    // ����� �ڽ� ���μ����� ��ٸ�
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("Child process %d terminated.\n", pid);
        // �ڽ��� ����Ǹ� �ش��ϴ� ������ ����
        for (int i = 0; i < client_count; i++) {
            if (client_csock[i] == -1) continue; // �̹� ���� ������ ����
            printf("Closing socket: %d\n", client_csock[i]);
            close(client_csock[i]);
            client_csock[i] = -1; // ���� ������ -1�� ǥ��
            client_count--;
        }
    }
}

int main(int argc, char **argv) {
    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    int pipe_fd[MAX_CLIENTS][2];


    setSignalHandler();

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind()");
        return -1;
    }

    setFdFlagAndListen(ssock);

    clen = sizeof(cliaddr);

    while (1) {
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);

        if (csock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                for (int i = 0; i < client_count; i++) {
                    userinfo user;
                    int n = read(pipe_fd[i][0], &user, sizeof(user));
                    if (n > 0) {
                        broadcastToClients(client_count, client_csock, &user);
                    }
                }
                continue;
            } else {
                perror("accept()");
            }
        }

        int flags = fcntl(csock, F_GETFL, 0);
        setCsockFlag(flags, csock);

        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected.\n");
            close(csock);
            continue;
        }

        setPipeWithNonblock(pipe_fd, client_count);

        if (fork() == 0) { // �ڽ� ���μ���
            close(ssock);
            handleClient(csock, client_count, pipe_fd, cliaddr);
        }
        client_csock[client_count] = csock;
        close(pipe_fd[client_count][1]); // �θ�� ���� ��ũ���� �ݱ�
        client_count++;
    }

    for (int i = 0; i < client_count; i++) {
        close(pipe_fd[i][0]);
        close(pipe_fd[i][1]);
    }
    close(ssock);
    return 0;
}

void setSignalHandler() {
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // ��ȣ ó�� �� �ڵ� �����
    sigaction(SIGCHLD, &sa, NULL);
}

void setPipeWithNonblock( int pipe_fd[][2], int client_count) {
    if (pipe(pipe_fd[client_count]) == -1) {
        perror("pipe");
    }
    int flags = fcntl((pipe_fd)[client_count][0], F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(pipe_fd[client_count][0], F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        exit(EXIT_FAILURE);
    }
}

void setCsockFlag(int flags, int csock) {
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
    }
    if (fcntl(csock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
    }
}

void broadcastToClients(int client_count, const int *client_csock, userinfo *user) {
    char formatted_message[BUFSIZ];
    snprintf(formatted_message, sizeof(formatted_message), "%s: %s", (*user).userName, (*user).message);
//                        printf("Parent received from child (User: %s): %s\n", user.userName, user.message);
    for (int i = 0; i < client_count; i++) {
        if (client_csock[i] != (*user).csockId) { // Exclude the sender
            printf("���� ���� write : %d\n",client_csock[i]);
            if (write(client_csock[i], formatted_message, strlen(formatted_message)) < 0) {
                perror("broadcast write");
            }
        }
    }
}

void setFdFlagAndListen(int ssock) {
    int flags = fcntl(ssock, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
    }
    if (fcntl(ssock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
    }

    if (listen(ssock, 8) < 0) {
        perror("listen()");
    }
}
