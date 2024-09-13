#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define BUFFER_SIZE 1024
#define MAX_MESSAGES 1000
#define TCP_PORT 5100

// 전역 변수: 부모와 자식 프로세스의 메시지를 저장
char parent_messages[MAX_MESSAGES][BUFFER_SIZE];
char child_messages[MAX_MESSAGES][BUFFER_SIZE];
int parent_msg_count = 0;
int child_msg_count = 0;

struct sockaddr_in setNetwork(char *const *argv, struct sockaddr_in *servaddr);

void print_error(const char *msg);

struct sockaddr_in setNetwork(char *const *argv, struct sockaddr_in *servaddr);

void print_matching_messages(const char *keyword, int is_parent);

int handleArgs(char *const *argv);

void makeChildPipe(int *to_child);

void makeParentPipe(int *to_parent);

int makeServerSocketPipe(int ssock);

void setNoneBlockSeverPipe(int ssock, int flags);

void setChildPipeToNoneblock(const int *to_child, int flags);

int setSocketPipeToNoneblock(int ssock);

void receveMessageFromServer(int ssock, char *mesg);

void handleSearchCommand(const int *to_child, ssize_t bytes_read);

bool isSearchCommand(const char *mesg);

void sendMessageToChild(const char *mesg, const int *to_child, ssize_t len);

void findKeyWordInData(const char *mesg);

void sendMessageToServer(int ssock, const char *mesg, ssize_t len);

bool getLoggable();

int main(int argc, char **argv) {
    printf("This is a fork-based client\n");
    int ssock;
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];

    if (argc < 2) {
        return handleArgs(argv);
    }

    /* 소켓을 생성 */
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }
    /* 소켓이 접속할 주소 지정 */
    servaddr = setNetwork(argv, &servaddr);
    /* 지정한 주소로 접속 */
    if (connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }

    int to_child[2];
    int to_parent[2];

    makeChildPipe(to_child);
    makeParentPipe(to_parent);
    int flags = setSocketPipeToNoneblock(ssock);
    setChildPipeToNoneblock(to_child, flags);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork()");
        exit(-1);
    }

    if (pid == 0) {
        close(to_child[1]);
        close(to_parent[0]);

        while (1) {
            // 자식 프로세스: 서버에서 메시지 수신 및 검색 요청 처리
            ssize_t bytes_read;
            receveMessageFromServer(ssock, mesg);
            // 검색 요청 읽기
            handleSearchCommand(to_child, bytes_read);
            usleep(100000); // 0.1초 지연
        }
    } else {
        // 부모 프로세스: 키보드 입력 받기 및 메시지 전송
        close(to_child[0]);
        close(to_parent[1]);
        while (1) {

            ssize_t len = fgets(mesg, sizeof(mesg), stdin) != NULL ? strlen(mesg) : 0;

            if (len > 0) {
                if (isSearchCommand(mesg)) {
                    // 검색 요청을 자식 프로세스에 보내기
                    sendMessageToChild(mesg, to_child, len);
                    printf("Chat Log\n");
                    printf("Scan chat log ... please wait\n");
                    findKeyWordInData(mesg);
                } else {
                    // 소켓으로 데이터를 비동기로 전송
                    sendMessageToServer(ssock, mesg, len);

                    // 부모 프로세스에서 수신된 메시지 저장
                    if (getLoggable()) {
                        strncpy(parent_messages[parent_msg_count++], mesg, BUFFER_SIZE);
                    }
                }

                // 종료 조건 확인
                if (strncmp(mesg, "...", 3) == 0) {
                    close(to_parent[1]);
                    close(to_parent[0]);
                    close(to_child[1]);
                    close(to_child[0]);
                    printf("exit.\n");
                    exit(0);
                }
            }
            usleep(100000); // 0.1초 지연
        }
    }

    return 0;
}

bool getLoggable() { return parent_msg_count < MAX_MESSAGES; }

void sendMessageToServer(int ssock, const char *mesg, ssize_t len) {
    if (send(ssock, mesg, len, MSG_DONTWAIT) <= 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("send()");
            close(ssock);
            exit(0);
        }
    }
}

void findKeyWordInData(const char *mesg) {
    char *keyword = mesg + 7;
    keyword[strcspn(keyword, "\n")] = '\0'; // 개행 문자 제거
    print_matching_messages(keyword, 1);
}

void sendMessageToChild(const char *mesg, const int *to_child, ssize_t len) {
    if (write(to_child[1], mesg, len) != len) {
        perror("writeerror®()");
        close(to_child[1]);
        exit(0);
    }
}

bool isSearchCommand(const char *mesg) { return strncmp(mesg, "search:", 7) == 0; }

void handleSearchCommand(const int *to_child, ssize_t bytes_read) {
    char res[BUFFER_SIZE];
    bytes_read = read(to_child[0], res, sizeof(res));
    if (bytes_read > 0) {
        res[bytes_read] = '\0';
        if (strncmp(res, "search:", 7) == 0) {
            char *keyword = res + 7;
            keyword[strcspn(keyword, "\n")] = '\0';
            print_matching_messages(keyword, 1);
        }
    } else if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            print_error("read() from pipefd1");
        }
    }

    printf("Search Finished\n");
}


// 서버로부터 메시지 수신 및 저장하는 함수
void receveMessageFromServer(int ssock, char *mesg) {
    ssize_t bytes_read = read(ssock, mesg, BUFFER_SIZE - 1);
    if (bytes_read > 0) {
        mesg[bytes_read] = '\0'; // 받은 메시지를 null로 종료

        // 부모 프로세스에서 수신된 메시지 저장
        if (getLoggable()) {
            strncpy(parent_messages[parent_msg_count++], mesg, BUFFER_SIZE);
        }
        // 메시지 출력
        printf("%s", mesg);
    } else if (bytes_read < 0) {
        // 읽기 오류 처리
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            print_error("read() from socket");
        }
    }
}




int setSocketPipeToNoneblock(int ssock) {
    int flags = fcntl(ssock, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        exit(-1);
    }
    if (fcntl(ssock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        exit(-1);
    }
    return flags;
}

void setChildPipeToNoneblock(const int *to_child, int flags) {
    flags = fcntl(to_child[0], F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        exit(-1);
    }
    if (fcntl(to_child[0], F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        exit(-1);
    }
}



void makeParentPipe(int *to_parent) {
    if (pipe(to_parent) == -1) {
        perror("pipe");
        exit(-1);
    }
}

void makeChildPipe(int *to_child) {
    if (pipe(to_child) == -1) {
        perror("pipe");
        exit(-1);
    }
}

int handleArgs(char *const *argv) {
    printf("Usage: %s IP_ADDRESS\n", argv[0]);
    return -1;
}


void print_error(const char *msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

// KMP 알고리즘의 LPS 배열 계산 함수
void compute_lps(const char *pattern, int m, int *lps) {
    int length = 0;
    lps[0] = 0; // 첫 번째 LPS 값은 0
    int i = 1;

    // 패턴에 대해 LPS 배열 계산
    while (i < m) {
        if (pattern[i] == pattern[length]) {
            length++;
            lps[i] = length;
            i++;
        } else {
            if (length != 0) {
                length = lps[length - 1];
            } else {
                lps[i] = 0;
                i++;
            }
        }
    }
}

// KMP 알고리즘으로 문자열 검색
int KMP_search(const char *text, const char *pattern) {
    int n = strlen(text);
    int m = strlen(pattern);
    int lps[m];

    // 패턴에 대한 LPS 배열 계산
    compute_lps(pattern, m, lps);

    int i = 0; // 텍스트의 인덱스
    int j = 0; // 패턴의 인덱스

    // 텍스트에서 패턴을 검색
    while (i < n) {
        if (pattern[j] == text[i]) {
            i++;
            j++;
        }

        // 패턴이 텍스트 내에서 완전히 일치한 경우
        if (j == m) {
            return 1; // 매칭 발견
            j = lps[j - 1];
        } else if (i < n && pattern[j] != text[i]) {
            // 패턴이 일치하지 않는 경우
            if (j != 0) {
                j = lps[j - 1];
            } else {
                i++;
            }
        }
    }
    return 0; // 매칭 발견되지 않음
}

// KMP 알고리즘을 사용하는 메시지 검색 함수
void print_matching_messages(const char *keyword, int is_parent) {
    int count = is_parent ? parent_msg_count : child_msg_count;
    char (*messages)[BUFFER_SIZE] = is_parent ? parent_messages : child_messages;

    for (int i = 0; i < count; ++i) {
        if (KMP_search(messages[i], keyword)) {
            printf("%s\n", messages[i]);
        }
    }
}
struct sockaddr_in setNetwork(char *const *argv, struct sockaddr_in *servaddr) {
    memset(servaddr, 0, sizeof(*servaddr));
    (*servaddr).sin_family = AF_INET;
    /* 문자열을 네트워크 주소로 변경 */
    inet_pton(AF_INET, argv[1], &((*servaddr).sin_addr.s_addr));
    (*servaddr).sin_port = htons(TCP_PORT);
    return (*servaddr);
}
