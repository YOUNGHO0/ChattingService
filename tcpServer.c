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

//자식 프로세스에서 부모 프로세스로 전달하는 메세지 구조체
typedef struct userinfo {
    char userName[10];
    char message[BUFSIZ];
    int csockId; // 클라이언트 소켓 아이디
    int room_number; //방 번호
} userinfo;

// 로그인 정보를 저장하는 구조체
// 로그인시 사용
typedef struct userLoginInfo {
    char userId[20];
    char password[20];
} userLoginInfo;

// 클라이언트 소켓이 어떤 pid를 가지고 있는 지 확인하는 구조체
typedef struct {
    pid_t pid;
    int csock;
} ProcessInfo;

ProcessInfo process_info[1000];
userLoginInfo users[MAX_USERS]; // 사용자 정보를 저장할 배열
int user_count = 0; // 현재 등록된 사용자 수
int client_count = 0;
char current_directory_path[PATH_MAX]; // 현재 디렉토리 경로를 저장할 버퍼

// 각 클라이언트마다 파이프 생성
int pipe_fd[MAX_CLIENTS][2];

//클라이언트가 어떤 방에 있는지 확인하는 구조체
struct csock_info{
    int csock;
    int roomNumber;
};

struct csock_info client_csock_info[200];
// 파일에서 사용자 로드
void loadUsersFromFile();
//사용자를 파일에 저장
void saveUsersToFile();
// 서버 소켓에 FD플레그를 지정하고 listen 함
void setFdFlagAndListen(int ssock);
// 현재 접속한 클라이언트중 방번호가 같은 클라이언트에게 메세지 전송
void broadcastToClients(int client_count, const int client_pos, userinfo *user);
//클라이언트 소켓 플레그 생성
void setCsockFlag(int flags, int csock);
// 파이프를 논블로킹으로 설정
void setPipeWithNonblock( int pipe_fd[][2], int client_count);
//네트워크 연결 설정
void setNetworkConnection(struct sockaddr_in *cliaddr, userinfo *user);
// 유저 로그인 처리
void handleUserLogin(int csock, userinfo *user);
//시그널 핸들러 설정 => 자식 프로세스가 죽은경우 처리 필요한 경우 처리
void setSignalHandler();
//broadcastToClients 에서 실제 전송을 담당하는 함수
void broadCast(int client_count, const userinfo *user, const char *formatted_message);
//해당하는 사용자 찾기
int findUserIndex(char *userId, char *password);
// 클라이언트 접속 후 fork이후 클라이언트 핸들링하는 함수
void handleClient(int csock, int client_index, int pipe_fd[][2], struct sockaddr_in cliaddr);
// sigchld 시그널 처리하는 함수
void handle_sigchld(int sig);
// 데몬으로 실행하는 함수
void initDaemon();
// 데몬으로 실행되면 디렉토리가 변경되므로 디렉토리 변경전 위치를 기준으로 읽는 함수
FILE *open_file_in_saved_dir(const char *filename, const char *mode);
// 파일 존재 유무
int file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}


int main(int argc, char **argv) {


    // getcwd 함수를 사용하여 현재 디렉토리 경로를 가져옴
    if (getcwd(current_directory_path, sizeof(current_directory_path)) != NULL) {
        printf("currentDirectory: %s\n", current_directory_path);
    } else {
        perror("getcwd() error");
    }

//    initDaemon();

    int ssock;
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr;
    int opt = 1;
    // 파일이 존재하는지 확인
    int fileIsNew = !file_exists("chatRoom.txt");
    FILE * file = open_file_in_saved_dir("chatRoom.txt", "a");
    if (file == NULL) {
        perror("Error creating file");
        return 1;
    }

    //

    // 파일이 없었던 경우에만 "default"를 추가합니다.
    if (fileIsNew) {
        // 파일이 존재하지 않으면 "default"를 추가하기 위해 "w" 모드로 파일을 열고 작성
        FILE *file = open_file_in_saved_dir("chatRoom.txt", "w");
        if (file == NULL) {
            perror("Error creating file");
            return 1;
        }

        // "default"를 파일에 추가
        if (fprintf(file, "%s ", "default") < 0) {
            perror("fprintf");
            fclose(file);
            return 1;
        }

        fclose(file);
    }

    // 빈 파일을 생성하고 나서 파일을 닫습니다
    fclose(file);
    file = open_file_in_saved_dir("data.txt", "a");
    if (file == NULL) {
        perror("Error creating file");
        return 1;
    }
    fclose(file);


    // 시그널 핸들러설정
    setSignalHandler();

    //소켓 할당
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    if (setsockopt(ssock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt(SO_REUSEADDR)");
        close(ssock);
        return -1;
    }

    //서버 연결 설정
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT);

    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind()");
        return -1;
    }
    //서버 연결 설정 끝


    // FDflag 설정 후 listen
    setFdFlagAndListen(ssock);


    clen = sizeof(cliaddr);

    while (1) {
        // 클라이언트와 연결 수립
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);

        // 비동기로 연결이 되지 않은경우에
        if (csock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                for (int i = 0; i < client_count; i++) {
                    userinfo user;
                    //메세지가 있으면 해당 메세지 broadCast
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

        //클라이언트 소켓 플래그 생성
        int flags = fcntl(csock, F_GETFL, 0);
        setCsockFlag(flags, csock);

        // 최대 클라이언트 갯수 확인
        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected.\n");
            close(csock);
            continue;
        }
        // 논블로킹으로 설정
        setPipeWithNonblock(pipe_fd, client_count);


        pid_t  pid = fork();
        if ( pid == 0) { // 자식 프로세스
            close(ssock);
            handleClient(csock, client_count, pipe_fd, cliaddr);
        }
        //부모에서 사용
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
        printf("Select room number is %d\n",number);
        char buffer[BUFSIZ];
        int len;

//         포맷된 문자열을 버퍼에 저장합니다.
        len = snprintf(buffer, sizeof(buffer), "\nYou Selected channel:%d\nStart chat\n", number);
        if (len < 0) {
            perror("snprintf");
            return;
        }

        // 포맷된 문자열을 클라이언트 소켓에 씁니다.
        if (write(client_csock_info[client_pos].csock, buffer, len) < 0) {
            perror("broadcast write");
        }

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

    // 로그인 과정 로그인 과정에 맞게 서버에 메세지 전송 읽기 작업
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
        // 회원가입의 경우 처리
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
        //아예 다른 메뉴인 경우 처리
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
    //채팅방 생성
    write(csock, "Input ChatRoom Number\nIf you want to create use 'create:{channelName} to create channel\n", strlen("Input ChatRoom Number\nIf you want to create use 'create:{channelName} to create channel\n"));
    FILE *file = open_file_in_saved_dir("chatRoom.txt", "r");
    if (file == NULL) {
        perror("fopen");
        return;
    }

    //채팅방 보여주기
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
    // 채팅방 목록 클라이언트로 전송
    write(csock, result, sizeof(result));

    result[0] = '\0';
    // 채팅방 번호 받아오기
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


    // 채팅방 번호를 생성하는 경우
    if (strncmp(chatRoomNumber, "create:", 7) == 0) {

        // 파일에 채팅방 생성후 파일 닫기
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

        // 사용자 방번호 처리
        user.room_number=updated_room_count-1;
        printf("start6\n");
        write(csock, "Channel Added\n", strlen("Channel Added\n"));

        file = open_file_in_saved_dir("chatRoom.txt", "r");
        if (file == NULL) {
            perror("fopen");
            return;
        }
        printf("start7\n");

        i = 0;
        result[0] = '\0';  // 결과 문자열 초기화

        while (fscanf(file, "%19s ", chatRoom ) == 1) {
            char temp[1000];  // 형식에 맞는 문자열을 저장할 임시 배열
            snprintf(temp, sizeof(temp), "%s:%d ", chatRoom, i);
            strcat(result, temp);
            i++;
        }
        strcat(result,"\n");
        fclose(file);

        printf("result : %s\n", result);

        // 결과 문자열의 실제 길이를 사용하여 write 호출
        write(csock, result, strlen(result));

        char select[2000];  // 충분히 큰 버퍼를 준비
        // "select:"와 원래 문자열을 결합
        // 부모 프로세스로 사용자가 선택한 방번호 전송
        snprintf(select, sizeof(select), "select:%d",user.room_number );
        strcpy(user.message,select);
        write(pipe_fd[client_index][1], &user, sizeof(user));

    } else {
        // 사용자가 방번호를 선택한 경우
        // 이상한 응답이 들어오면 가장 앞의 방 0번이 선택됨
        char select[2000];  // 충분히 큰 버퍼를 준비
        int num =atoi(chatRoomNumber);
        user.room_number = num;
        // "select:"와 원래 문자열을 결합
        snprintf(select, sizeof(select), "select:%s", chatRoomNumber);
        strcpy(user.message,select);
        write(pipe_fd[client_index][1], &user, sizeof(user));
        sleep(1);
    }


    //채팅 과정
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

        // 로그아웃 처리
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

