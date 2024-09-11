#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#define TCP_PORT 5100

int main(int argc, char **argv)
{
    printf("This is a fork-based client\n");
    int ssock;
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];

    if (argc < 2) {
        printf("Usage: %s IP_ADDRESS\n", argv[0]);
        return -1;
    }

    /* 소켓을 생성 */
    if ((ssock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket()");
        return -1;
    }

    /* 소켓이 접속할 주소 지정 */
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    /* 문자열을 네트워크 주소로 변경 */
    inet_pton(AF_INET, argv[1], &(servaddr.sin_addr.s_addr));
    servaddr.sin_port = htons(TCP_PORT);

    /* 지정한 주소로 접속 */
    if (connect(ssock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect()");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork()");
        return -1;
    }

    if (pid == 0) {
        // 자식 프로세스: 서버로부터 데이터 받기
        while (1) {
            int n = recv(ssock, mesg, sizeof(mesg) - 1, 0);
            if (n <= 0) {
                if (n == 0) {
                    printf("서버가 연결을 종료했습니다.\n");
                } else {
                    perror("recv()");
                }
                close(ssock);
                exit(0);
            }
            mesg[n] = '\0'; // 받은 메시지 null 종료
            printf("%s", mesg); // 서버에서 받은 메시지 출력
            fflush(stdout); // 출력 버퍼를 즉시 비우기
        }
    } else {
        // 부모 프로세스: 키보드 입력 받기
        // 표준 입력을 논블로킹 모드로 설정
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags < 0) {
            perror("fcntl(F_GETFL)");
            return -1;
        }
        if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
            perror("fcntl(F_SETFL)");
            return -1;
        }

        while (1) {
            ssize_t len = fgets(mesg, sizeof(mesg), stdin) != NULL ? strlen(mesg) : 0;
            if (len > 0) {
                // 소켓으로 데이터를 비동기로 전송
                if (send(ssock, mesg, len, MSG_DONTWAIT) <= 0) {
                    perror("send()");
                    close(ssock);
                    exit(0);
                }
            }
            usleep(100000); // 0.1초 지연
        }
    }

    return 0;
}
