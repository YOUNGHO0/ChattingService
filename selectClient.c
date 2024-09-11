#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define TCP_PORT 5100

int main(int argc, char **argv)
{
    printf("this is select client\n");
    int ssock;
    struct sockaddr_in servaddr;
    char mesg[BUFSIZ];

    if (argc < 2) {
        printf("Usage : %s IP_ADRESS\n", argv[0]);
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

    while (1) {
        fd_set readFd;
        FD_ZERO(&readFd);              // fd_set 초기화
        FD_SET(STDIN_FILENO, &readFd); // 표준 입력 감시
        FD_SET(ssock, &readFd);        // 소켓 입력 감시

        // 감시할 최대 파일 디스크립터 설정
        int max_fd = (STDIN_FILENO > ssock) ? STDIN_FILENO : ssock;

        // select 호출: 키보드와 소켓 입력을 감시
        int ret = select(max_fd + 1, &readFd, NULL, NULL, NULL);
        if (ret < 0) {
            perror("select()");
            return -1;
        }

        // 소켓에서 데이터 수신 감지
        if (FD_ISSET(ssock, &readFd)) {
            int n = recv(ssock, mesg, sizeof(mesg) - 1, 0);
            if (n <= 0) {
                if (n == 0) {
                    printf("서버가 연결을 종료했습니다.\n");
                } else {
                    perror("recv()");
                }
                break;
            }
            mesg[n] = '\0'; // 받은 메시지 null 종료
            printf("%s", mesg); // 서버에서 받은 메시지 출력
            fflush(stdout); // 출력 버퍼를 즉시 비우기
        }

        // 키보드 입력 감지
        if (FD_ISSET(STDIN_FILENO, &readFd)) {
            if (fgets(mesg, sizeof(mesg), stdin) != NULL) {
                // 소켓으로 데이터를 비동기로 전송
                if (send(ssock, mesg, strlen(mesg), MSG_DONTWAIT) <= 0) {
                    perror("send()");
                    return -1;
                }
            }
        }
    }
    close(ssock); // 소켓을 닫는다.

    return 0;
}
