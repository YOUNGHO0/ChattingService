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
#define TCP_PORT 5100 /* 서버의 포트 번호 */
#define MAX_CLIENTS 100 /* 최대 클라이언트 수 */
#define MAX_USERS 100 /* 최대 사용자 수 */

typedef struct userinfo {
    char userName[10];
    char message[BUFSIZ];
    int csockId;
} userinfo;

// 로그인 정보를 저장하는 구조체
typedef struct userLoginInfo {
    char userId[20];
    char password[20];
} userLoginInfo;

userLoginInfo users[MAX_USERS]; // 사용자 정보를 저장할 배열
int user_count = 0; // 현재 등록된 사용자 수

int findUserIndex(char *userId, char *password) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].userId, userId) == 0 && strcmp(users[i].password, password) == 0) {
            return i; // 사용자 찾음
        }
    }
    return -1; // 사용자 없음
}

void handleClient(int csock, int client_index, int pipe_fd[][2], struct sockaddr_in cliaddr) {
    userinfo user;
    snprintf(user.userName, sizeof(user.userName), "%d%c", client_index + 1, 'a' + client_index);
    user.csockId = csock;
    close(pipe_fd[client_index][0]); // 자식은 읽기 디스크립터 닫기

    /* 네트워크 주소를 문자열로 변경 */
    inet_ntop(AF_INET, &cliaddr.sin_addr, user.message, BUFSIZ);
    printf("Client is connected : %s\n", user.message);
    fflush(stdout);

    // 로그인 또는 회원가입 여부 물어보기
    char choice[10];
    write(csock, "로그인 또는 회원가입을 선택하세요 (login/signup): ", strlen("로그인 또는 회원가입을 선택하세요 (login/signup): "));
    read(csock, choice, sizeof(choice));
    choice[strcspn(choice, "\n")] = '\0'; // 줄바꿈 제거

    if (strcmp(choice, "login") == 0) {
        // 로그인 처리
        char userId[20], password[20];
        write(csock, "아이디: ", strlen("아이디: "));
        read(csock, userId, sizeof(userId));
        userId[strcspn(userId, "\n")] = '\0'; // 줄바꿈 제거

        write(csock, "비밀번호: ", strlen("비밀번호: "));
        read(csock, password, sizeof(password));
        password[strcspn(password, "\n")] = '\0'; // 줄바꿈 제거

        int userIndex = findUserIndex(userId, password);
        if (userIndex != -1) {
            write(csock, "로그인 성공!\n", strlen("로그인 성공!\n"));
        } else {
            write(csock, "로그인 실패! 잘못된 아이디 또는 비밀번호입니다.\n", strlen("로그인 실패! 잘못된 아이디 또는 비밀번호입니다.\n"));
            close(csock);
            exit(0);
        }
    } else if (strcmp(choice, "signup") == 0) {
        // 회원가입 처리
        if (user_count >= MAX_USERS) {
            write(csock, "최대 사용자 수에 도달했습니다. 회원가입이 불가능합니다.\n", strlen("최대 사용자 수에 도달했습니다. 회원가입이 불가능합니다.\n"));
            close(csock);
            exit(0);
        }

        char userId[20], password[20];
        write(csock, "새 아이디: ", strlen("새 아이디: "));
        read(csock, userId, sizeof(userId));
        userId[strcspn(userId, "\n")] = '\0'; // 줄바꿈 제거

        write(csock, "새 비밀번호: ", strlen("새 비밀번호: "));
        read(csock, password, sizeof(password));
        password[strcspn(password, "\n")] = '\0'; // 줄바꿈 제거

        // 사용자 추가
        strcpy(users[user_count].userId, userId);
        strcpy(users[user_count].password, password);
        user_count++;
        write(csock, "회원가입 성공!\n", strlen("회원가입 성공!\n"));
    } else {
        write(csock, "잘못된 선택입니다. 연결을 종료합니다.\n", strlen("잘못된 선택입니다. 연결을 종료합니다.\n"));
        close(csock);
        exit(0);
    }

    while (1) {
        int n = read(csock, user.message, BUFSIZ); // 클라이언트로부터 데이터 읽기
        if (n == 0) {
            printf("Client disconnected.\n");
            break;
        } else if (n < 0) {
            perror("read issue()");
            break;
        }

        user.message[n] = '\0';
        printf("Received data from %s: %s\n", user.userName, user.message);
        fflush(stdout);

        /* 부모 프로세스에게 받은 메시지를 전달 */
        write(pipe_fd[client_index][1], &user, sizeof(user));

        if (strncmp(user.message, "...", 3) == 0) {
            close(csock);
            break;
        }
    }

    close(pipe_fd[client_index][1]); // 자식의 쓰기 디스크립터 닫기
    exit(0);
}

int main(int argc, char **argv) {
    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    int pipe_fd[MAX_CLIENTS][2];

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

    int flags = fcntl(ssock, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(ssock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        return -1;
    }

    if (listen(ssock, 8) < 0) {
        perror("listen()");
        return -1;
    }

    clen = sizeof(cliaddr);
    int client_count = 0;
    int  client_csock[MAX_USERS];
    while (1) {
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);

        if (csock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                for (int i = 0; i < client_count; i++) {
                    userinfo user;
                    int n = read(pipe_fd[i][0], &user, sizeof(user));
                    if (n > 0) {
                        char formatted_message[BUFSIZ];
                        snprintf(formatted_message, sizeof(formatted_message), "%s: %s", user.userName, user.message);
//                        printf("Parent received from child (User: %s): %s\n", user.userName, user.message);
                        for (int i = 0; i < client_count; i++) {
                            if (client_csock[i] != user.csockId) { // Exclude the sender
                                if (write(client_csock[i], formatted_message, strlen(formatted_message)) < 0) {
                                    perror("broadcast write");
                                }
                            }
                        }

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

        if (fcntl(csock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            perror("fcntl(F_SETFL)");
            return -1;
        }

        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected.\n");
            close(csock);
            continue;
        }

        if (pipe(pipe_fd[client_count]) == -1) {
            perror("pipe");
            return -1;
        }

        flags = fcntl(pipe_fd[client_count][0], F_GETFL, 0);
        if (flags == -1) {
            perror("fcntl F_GETFL");
            exit(EXIT_FAILURE);
        }
        if (fcntl(pipe_fd[client_count][0], F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl F_SETFL");
            exit(EXIT_FAILURE);
        }

        if (fork() == 0) { // 자식 프로세스
            close(ssock);
            handleClient(csock, client_count, pipe_fd, cliaddr);
        }
        client_csock[client_count] = csock;
        close(pipe_fd[client_count][1]); // 부모는 쓰기 디스크립터 닫기
        client_count++;
    }

    for (int i = 0; i < client_count; i++) {
        close(pipe_fd[i][0]);
        close(pipe_fd[i][1]);
    }
    close(ssock);
    return 0;
}
