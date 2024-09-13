#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <limits.h>
#include <sys/fcntl.h>
#define TCP_PORT 5100 /* 서버의 포트 번호 */
#define MAX_CLIENTS 100 /* 최대 클라이언트 수 */
#define MAX_USERS 100 /* 최대 사용자 수 */

typedef struct userinfo {
    char userName[10];
    char message[BUFSIZ];
    int csockId;
    int room_number;
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
//int  client_csock[MAX_USERS];
char current_directory_path[PATH_MAX]; // 현재 디렉토리 경로를 저장할 버퍼

int pipe_fd[MAX_CLIENTS][2];

struct csock_info{
    int csock;
    int roomNumber;
};

struct csock_info client_csock_info[200];
void loadUsersFromFile();
void saveUsersToFile();
void setFdFlagAndListen(int ssock);
void broadcastToClients(int client_count, const int client_pos, userinfo *user);
void setCsockFlag(int flags, int csock);
void setPipeWithNonblock( int pipe_fd[][2], int client_count);
void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user);
void handleUserLogin(int csock, userinfo *user);
void setSignalHandler();
void broadCast(int client_count, const userinfo *user, const char *formatted_message);
int findUserIndex(char *userId, char *password);
void handleClient(int csock, int client_index, int pipe_fd[][2], struct sockaddr_in cliaddr);
void handleUserLogin(int csock, userinfo *user);
void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user);
void handle_sigchld(int sig);
void initDaemon();
FILE *open_file_in_saved_dir(const char *filename, const char *mode);
int main(int argc, char **argv) {


    // getcwd 함수를 사용하여 현재 디렉토리 경로를 가져옴
    if (getcwd(current_directory_path, sizeof(current_directory_path)) != NULL) {
        printf("currentDirectory: %s\n", current_directory_path);
    } else {
        perror("getcwd() error");
    }

    initDaemon();

    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    int opt = 1;

    FILE * file = open_file_in_saved_dir("chatRoom.txt", "a");
    if (file == NULL) {
        perror("Error creating file");
        return 1;
    }

    if (fprintf(file, "%s ", "default") < 0) {
        perror("fprintf");
    }

    // 빈 파일을 생성하고 나서 파일을 닫습니다
    fclose(file);


    file = open_file_in_saved_dir("data.txt", "a");
    if (file == NULL) {
        perror("Error creating file");
        return 1;
    }
    // 빈 파일을 생성하고 나서 파일을 닫습니다
    fclose(file);


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
                        broadcastToClients(client_count, i, &user);
                    }
                }
                continue;
            } else {
                perror("accept()");
            }
        }

        for(int k =0; k< client_count; k++){
            printf("socket %d pos val : %d\n", k,client_csock_info[k].csock);
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
        client_csock_info[client_count].csock = csock;
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

void broadcastToClients(int client_count, int client_pos, userinfo *user) {
    char formatted_message[BUFSIZ];
    snprintf(formatted_message, sizeof(formatted_message), "%s: %s", (*user).userName, (*user).message);


    printf("Parent received from child (User: %s): %s\n", user->userName, user->message);
    if (strncmp(user->message, "create:", 7) == 0){

        printf("start write\n");
        FILE *file = open_file_in_saved_dir("chatRoom.txt", "a");
        if (file == NULL) {
            perror("fopen");
            return;
        }

        // 파일 디스크립터 가져오기
        int fd = fileno(file);

        // 파일에 문자열 쓰기
        if (fprintf(file, "%s ", (user->message+7)) < 0) {
            perror("fprintf");
        }

        // fflush를 사용해 버퍼 비우기 (디스크에 즉시 반영)
        if (fflush(file) != 0) {
            perror("fflush");
        }

        // fsync를 사용해 디스크에 강제로 반영
        if (fsync(fd) < 0) {
            perror("fsync");
        }
        // 파일 닫기
        fclose(file);
        printf("finish write\n");

    }
    else if(strncmp(user->message, "select:", 7) == 0) {
        int number = atoi(user->message + 7);
        client_csock_info[client_pos].roomNumber = number;
        printf("select room number is %d\n",number);
        return;
    }
    broadCast(client_count, user, formatted_message);
}

void broadCast(int client_count, const userinfo *user, const char *formatted_message) {
    for (int i = 0; i < client_count; i++) {
        printf("user RoomNumber %d\n", user->room_number );
        printf("%d csock : %d\n", i,client_csock_info[i].csock);
        printf("%d roomNumber : %d\n",i, client_csock_info[i].roomNumber );
        if (client_csock_info[i].csock != (*user).csockId && client_csock_info[i].roomNumber == user->room_number) { // Exclude the sender
            printf("socket to write : %d\n",client_csock_info[i].csock);
            if (write(client_csock_info[i].csock, formatted_message, strlen(formatted_message)) < 0) {
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
                client_csock_info[i].csock = client_csock_info[client_count - 1].csock; // 마지막 소켓으로 교체
                client_csock_info[i].roomNumber = client_csock_info[client_count-1].roomNumber;
                pipe_fd[i][0] = pipe_fd[client_count-1][0];



                client_count--; // 클라이언트 수 감소

                break;
            }
        }
    }
}

void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user) {/* 네트워크 주소를 문자열로 변경 */
    inet_ntop(AF_INET, &(*cliaddr).sin_addr, (*user).message, BUFSIZ);
    printf("Client is connected : %s\n", (*user).message);
    fflush(stdout);
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
            printf("socket pos : %d  number  :%d\n", k,client_csock_info[k].csock);
        }
        close(csock);
        exit(0);
    }
}

void handleClient(int csock, int client_index, int (*pipe_fd)[2], struct sockaddr_in cliaddr) {
    userinfo user;
    user.csockId = csock;
    close(pipe_fd[client_index][0]); // 자식은 읽기 디스크립터 닫기
    setNetworkConnection(&cliaddr, &user);

    handleUserLogin(csock, &user);
    write(csock, "Input ChatRoom Number", strlen("Input ChatRoom Number"));
    FILE *file = open_file_in_saved_dir("chatRoom.txt", "r");
    if (file == NULL) {
        perror("fopen");
        return;
    }

    char result[2000] = {'\0'};
    char chatRoom[2000];
    int i =0;
    while (fscanf(file, "%19s ", chatRoom ) == 1) {
        char temp[1000];  // 형식에 맞는 문자열을 저장할 임시 배열
        snprintf(temp, sizeof(temp), "%s:%d ", chatRoom, i);
        strcat(result, temp);
        i++;
    }
    strcat(result,"\n");
    fclose(file);

    write(csock, result, strlen(result));

    result[0] = '\0';

    char chatRoomNumber[2000];
    int room =read(csock, chatRoomNumber, sizeof(chatRoomNumber));//
    if (room < 0) {
        perror("read");
        return;
    }

    // Null 종료 문자 추가
    chatRoomNumber[room] = '\0';

    strcpy(user.message,chatRoomNumber);
    write(pipe_fd[client_index][1], &user, sizeof(user));



    if (strncmp(chatRoomNumber, "create:", 7) == 0) {

        int count = 0;
        printf("start\n");
        FILE *file = open_file_in_saved_dir("chatRoom.txt", "r");
        if (file == NULL) {
            perror("fopen");
            return;
        }
        printf("start1\n");

        char chatRoom[2000];
        while (fscanf(file, "%19s ", chatRoom ) == 1) {
            count++;
        }
        printf("start2\n");

        fclose(file);
        sleep(2);
        printf("start4\n");
        int updated_room_count;
        do{
            updated_room_count =0;

            FILE *file = open_file_in_saved_dir("chatRoom.txt", "r");
            if (file == NULL) {
                perror("fopen");
                return;
            }
            char chatRoom[2000];
            while (fscanf(file, "%19s ", chatRoom ) == 1) {
                updated_room_count++;
            }
            fclose(file);
            printf("start5\n");
        }while(updated_room_count <= count);

        printf("updsated room_number : %d\n",updated_room_count);

        user.room_number=updated_room_count-1;
        printf("start6\n");
        write(csock, "Updated Room List\n", strlen("Updated Room List\n"));

        file = open_file_in_saved_dir("chatRoom.txt", "r");
        if (file == NULL) {
            perror("fopen");
            return;
        }
        printf("start7\n");
        while (fscanf(file, "%19s ", chatRoom ) == 1) {
            strcat(result, chatRoom);
            strcat(result, " ");
        }
        strcat(result,"\n");
        fclose(file);


        write(csock, result, strlen(result));

        result[0] = '\0';
        char select[2000];  // 충분히 큰 버퍼를 준비
        // "select:"와 원래 문자열을 결합
        snprintf(select, sizeof(select), "select:%d",user.room_number );
        strcpy(user.message,select);
        write(pipe_fd[client_index][1], &user, sizeof(user));

    } else {
        char select[2000];  // 충분히 큰 버퍼를 준비
        int num =atoi(chatRoomNumber);
        user.room_number = num;
        // "select:"와 원래 문자열을 결합
        snprintf(select, sizeof(select), "select:%s", chatRoomNumber);
        strcpy(user.message,select);
        write(pipe_fd[client_index][1], &user, sizeof(user));
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

int findUserIndex(char *userId, char *password) {
    for (int i = 0; i < user_count; i++) {
        if (strcmp(users[i].userId, userId) == 0 && strcmp(users[i].password, password) == 0) {
            return i; // 사용자 찾음
        }
    }
    return -1; // 사용자 없음
}

void saveUsersToFile() {
    FILE *file = open_file_in_saved_dir( "data.txt", "w");
    if (file == NULL) {
        perror("fopen");
        return;
    }

    for (int i = 0; i < user_count; i++) {
        fprintf(file, "%s %s\n", users[i].userId, users[i].password);
    }

    fclose(file);
}

void loadUsersFromFile() {
    FILE *file = open_file_in_saved_dir("data.txt", "r");
    if (file == NULL) {
        perror("fopen");
        return;
    }

    while (fscanf(file, "%19s %19s", users[user_count].userId, users[user_count].password) == 2) {
        user_count++;
    }

    fclose(file);
}
void initDaemon() {
    pid_t pid;

    // 첫 번째 fork
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        // 부모 프로세스 종료
        exit(EXIT_SUCCESS);
    }

    // 새로운 세션 생성
    if (setsid() < 0) {
        perror("setsid");
        exit(EXIT_FAILURE);
    }

    // 두 번째 fork (세션 리더를 탈출)
    pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // 데몬의 파일 권한 마스크 설정
    umask(0);
    // 루트 디렉토리로 변경
    chdir("/");

    // 표준 입출력 파일 디스크립터를 /dev/null로 리다이렉트
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY); // stdin
    open("/dev/null", O_WRONLY); // stdout
    open("/dev/null", O_RDWR);   // stderr
}
FILE *open_file_in_saved_dir(const char *filename, const char *mode) {
    char full_path[PATH_MAX];
    snprintf(full_path, sizeof(full_path), "%s/%s", current_directory_path, filename); // 전체 경로 생성
    return fopen(full_path, mode); // 파일 열기
}

