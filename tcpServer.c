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

typedef struct userinfo{
    char * userName;
    char * password;
    char * message;
    int csockId;
};

int main(int argc, char **argv) {
    int ssock;                 /* 소켓 디스크립터 정의 */
    socklen_t clen;
    struct sockaddr_in servaddr, cliaddr; /* 주소 구조체 정의 */
    char mesg[BUFSIZ];
    int pipe_fd[MAX_CLIENTS][2]; /* 파이프 파일 디스크립터 배열 */

    /* 서버 소켓 생성 */
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    /* 주소 구조체에 주소 지정 */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(TCP_PORT); /* 사용할 포트 지정 */

    /* bind 함수를 사용하여 서버 소켓의 주소 설정 */
    if (bind(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind()");
        return -1;
    }

    /* 소켓을 non-blocking 모드로 설정 */
    int flags = fcntl(ssock, F_GETFL, 0);
    if (flags < 0) {
        perror("fcntl(F_GETFL)");
        return -1;
    }
    if (fcntl(ssock, F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl(F_SETFL)");
        return -1;
    }

    /* 동시에 접속하는 클라이언트의 처리를 위한 대기 큐를 설정 */
    if (listen(ssock, 8) < 0) {
        perror("listen()");
        return -1;
    }

    clen = sizeof(cliaddr);
    int client_count = 0;
    while (1) {
        /* 클라이언트가 접속하면 접속을 허용하고 클라이언트 소켓 생성 */
        int csock = accept(ssock, (struct sockaddr *)&cliaddr, &clen);

        if (csock < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 부모 프로세스는 읽기 디스크립터를 사용하여 자식의 데이터를 읽음
                for (int i = 0; i < client_count; i++) { // 수정된 부분
                    int n = read(pipe_fd[i][0], mesg, BUFSIZ);
                    if (n > 0) {
                        mesg[n] = '\0';
                        printf("Parent received from child: %s\n", mesg); // 자식 프로세스의 메시지를 출력
                    }
                }
                continue;
                // 클라이언트 연결이 없을 경우, 다른 작업 수행
            } else {
                perror("accept()");
                continue;
            }
        }

        printf("client count %d\n",client_count);


        flags = fcntl(csock, F_GETFL, 0);
        if (flags < 0) {
            perror("fcntl(F_GETFL)");
            return -1;
        }

// O_NONBLOCK 플래그를 제거하여 블로킹 모드로 설정
        if (fcntl(csock, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            perror("fcntl(F_SETFL)");
            return -1;
        }


        if (client_count >= MAX_CLIENTS) {
            printf("Max clients reached. Connection rejected.\n");
            close(csock);
            continue;
        }


        /* 새로운 파이프 생성 */
        if (pipe(pipe_fd[client_count]) == -1) {
            perror("pipe");
            return -1;
        }
        // pipe_fd[i][0]을 비동기 모드로 설정
        int flags = fcntl(pipe_fd[client_count][0], F_GETFL, 0);
        if (flags == -1) {
            perror("fcntl F_GETFL");
            exit(EXIT_FAILURE);
        }
        if (fcntl(pipe_fd[client_count][0], F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl F_SETFL");
            exit(EXIT_FAILURE);
        }

        if (fork() == 0) { // 자식 프로세스
            close(pipe_fd[client_count][0]); // 읽기 디스크립터 닫기 (자식은 쓰기 전용)

            close(ssock); // 자식 프로세스는 서버 소켓을 사용하지 않음

            /* 네트워크 주소를 문자열로 변경 */
            inet_ntop(AF_INET, &cliaddr.sin_addr, mesg, BUFSIZ);
            printf("Client is connected : %s\n", mesg);
            fflush(stdout); // 버퍼에 있는 데이터를 강제로 출력

            while (1) {
                int n = read(csock, mesg, BUFSIZ); // 클라이언트로부터 데이터 읽기
                if (n == 0) {
                    // 클라이언트가 연결을 종료함
                    printf("Client disconnected.\n");
                    break;
                } else if (n < 0) {
                    // 실제 오류 발생
                    perror("read issue()");
                    break;
                }

                mesg[n] = '\0';
                printf("Received data : %s\n", mesg);
                fflush(stdout); // 버퍼에 있는 데이터를 강제로 출력

                /* 부모 프로세스에게 받은 메시지를 전달 */
                write(pipe_fd[client_count][1], mesg, n);

                if (strncmp(mesg, "...", 3) == 0) {
                    close(csock);
                    break;
                }

                /* 클라이언트로 메시지 전송 */
//                if (write(csock, mesg, n) <= 0)
//                    perror("write()");
            }

            close(pipe_fd[client_count][1]); // 자식의 쓰기 디스크립터 닫기
            exit(0);
        }

        // 부모 프로세스
        close(pipe_fd[client_count][1]); // 부모는 쓰기 디스크립터 닫기
        client_count++;

        // 좀비 프로세스 제거
//        while (waitpid(-1, NULL, WNOHANG) > 0);
    }

    for (int i = 0; i < client_count; i++) {
        close(pipe_fd[i][0]);
        close(pipe_fd[i][1]);
    }
    close(ssock); // 서버 소켓을 닫음
    return 0;
}
