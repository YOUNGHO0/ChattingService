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
#include <sys/mman.h>

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


typedef struct {
    pid_t pid;
    int csock;
} ProcessInfo;

ProcessInfo process_info[1000];


userLoginInfo users[MAX_USERS]; // 사용자 정보를 저장할 배열
int user_count = 0; // 현재 등록된 사용자 수
int client_count = 0;
int  client_csock[MAX_USERS];
int pipe_fd[MAX_CLIENTS][2];

void loadUsersFromFile() {
    FILE *file = fopen("data.txt", "r");
    if (file == NULL) {
        perror("fopen");
        return;
    }

    while (fscanf(file, "%19s %19s", users[user_count].userId, users[user_count].password) == 2) {
        user_count++;
    }

    fclose(file);
}

void saveUsersToFile() {
    FILE *file = fopen( "data.txt", "w");
    if (file == NULL) {
        perror("fopen");
        return;
    }

    for (int i = 0; i < user_count; i++) {
        fprintf(file, "%s %s\n", users[i].userId, users[i].password);
    }

    fclose(file);
}


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
            return i; // 사용자 찾음
        }
    }
    return -1; // 사용자 없음
}

void handleClient(int csock, int client_index, int pipe_fd[][2], struct sockaddr_in cliaddr) {
    userinfo user;
    user.csockId = csock;
    close(pipe_fd[client_index][0]); // 자식은 읽기 디스크립터 닫기
    setNetworkConnection(&cliaddr, &user);

    handleUserLogin(csock, &user);

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

        if (strncmp(user.message, "...", 3) == 0) {
            printf("exit client thread\n");
            close(pipe_fd[client_index][1]); // 자식의 쓰기 디스크립터 닫기
            close(csock);
            exit(0);
        }

        printf("Received data from %s: %s\n", user.userName, user.message);
        /* 부모 프로세스에게 받은 메시지를 전달 */
        write(pipe_fd[client_index][1], &user, sizeof(user));


    }
    close(csock);
    close(pipe_fd[client_index][1]); // 자식의 쓰기 디스크립터 닫기
    exit(0);
}

void handleUserLogin(int csock, userinfo *user) {
    char choice[10];
    write(csock, "input type (login/signup): ", strlen("input type (login/signup):"));
    read(csock, choice, sizeof(choice));
    choice[strcspn(choice, "\n")] = '\0'; // 줄바꿈 제거

    if (strcmp(choice, "login") == 0) {
        char userId[20], password[20];
        write(csock, "ID: ", strlen("ID: "));
        read(csock, userId, sizeof(userId));
        userId[strcspn(userId, "\n")] = '\0'; // 줄바꿈 제거

        write(csock, "PW: ", strlen("PW: "));
        read(csock, password, sizeof(password));
        password[strcspn(password, "\n")] = '\0'; // 줄바꿈 제거

        loadUsersFromFile();
        int userIndex = findUserIndex(userId, password);
        if (userIndex != -1) {
            write(csock, "SUCCESS LOGIN\n", strlen("SUCCESS LOGIN\n"));
            strcpy(user->userName, userId); // 유저 이름 저장
        } else {
            write(csock, "INCORRECT UserName PassWord\n", strlen("INCORRECT UserName PassWord\n"));
            close(csock);
            exit(0);
        }
    } else if (strcmp(choice, "signup") == 0) {
        if (user_count >= MAX_USERS) {
            write(csock, "MAX User \n", strlen("MAX User \n"));
            close(csock);
            exit(0);
        }

        char userId[20], password[20];
        write(csock, "ID: ", strlen("ID: "));
        read(csock, userId, sizeof(userId));
        userId[strcspn(userId, "\n")] = '\0'; // 줄바꿈 제거

        write(csock, "PW: ", strlen("PW: "));
        read(csock, password, sizeof(password));
        password[strcspn(password, "\n")] = '\0'; // 줄바꿈 제거

        loadUsersFromFile();
        // 중복 확인
        for (int i = 0; i < user_count; i++) {
            if (strcmp(users[i].userId, userId) == 0) {
                write(csock, "ID ALREADY EXIST.\n", strlen("ID ALREADY EXIST.\n"));
                close(csock);
                exit(0);
            }
        }

        // 사용자 추가
        strcpy(users[user_count].userId, userId);
        strcpy(users[user_count].password, password);
        user_count++;
        saveUsersToFile();
        strcpy(user->userName, userId);
        write(csock, "SUCCESS\n", strlen("SUCCESS\n"));
    } else {
        write(csock, "WRONG OPTION QUIT CONNECTION \n", strlen("WRONG OPTION QUIT CONNECTION \n"));
        for(int k =0; k< client_count; k++){
            printf("socket pos : %d  number  :%d\n", k,client_csock[k]);
        }
        close(csock);
        exit(0);
    }
}

void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user) {/* 네트워크 주소를 문자열로 변경 */
    inet_ntop(AF_INET, &(*cliaddr).sin_addr, (*user).message, BUFSIZ);
    printf("Client is connected : %s\n", (*user).message);
    fflush(stdout);
}


// SIGCHLD 신호 처리기
void handle_sigchld(int sig) {
    printf("시그널 발생\n");
    int status;
    pid_t pid;

    // 종료된 자식 프로세스를 기다림
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        // 종료된 자식 프로세스의 소켓 찾기 및 닫기
        for (int i = 0; i < client_count; i++) {
            if (process_info[i].pid == pid) {

                int csock = process_info[i].csock;
                close(csock);
                printf("pid handling finished %d %d\n", pid, csock );
                process_info[i].pid = process_info[client_count-1].pid;
                process_info[i].csock = process_info[client_count-1].csock; // 닫힌 소켓을 -1로 표시

                // 클라이언트 소켓 배열에서 소켓 제거
                client_csock[i] = client_csock[client_count - 1]; // 마지막 소켓으로 교체
                pipe_fd[i][0] = pipe_fd[client_count-1][0];

                //pid 인포도 바꿔줘야되네


                client_count--; // 클라이언트 수 감소

                break;
            }
        }
    }
}

int main(int argc, char **argv) {
    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    int opt = 1;


    setSignalHandler();

    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    if (setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(ssock);
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

        for(int k =0; k< client_count; k++){
            printf("socket %d pos val : %d\n", k,client_csock[k]);
        }
        int flags = fcntl(csock, F_GETFL, 0);
        setCsockFlag(flags, csock);

        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected.\n");
            close(csock);
            continue;
        }

        setPipeWithNonblock(pipe_fd, client_count);
        pid_t  pid = fork();
        if ( pid == 0) { // 자식 프로세스
            close(ssock);
            handleClient(csock, client_count, pipe_fd, cliaddr);
        }
        process_info[client_count].pid = pid;
        process_info[client_count].csock = csock;
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

void setSignalHandler() {
    struct sigaction sa;
    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
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
        printf("client count : %d\n", client_count);
        if (client_csock[i] != (*user).csockId) { // Exclude the sender
            printf("socket to write : %d\n",client_csock[i]);
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
