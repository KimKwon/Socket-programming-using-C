#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>   
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#define BUFSIZE 1024 

void error(char *msg)
{
    perror(msg);
    exit(1);
}

char *getPath(char *req);
int getFileType(char *path); 
int resMaker(int client_fd, char *req); 

int main(int argc, char *argv[])
{
    int sockfd, newsockfd; 
    int portno; 
    socklen_t clilen;
    
    char buffer[BUFSIZE];
    
    struct sockaddr_in serv_addr, cli_addr;
    
    int n, pid;
    if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
    }
    
    sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0) 
       error("ERROR opening socket");
    
    bzero((char *) &serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]); 
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
    serv_addr.sin_port = htons(portno); 
    
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
             error("ERROR on binding");
    
    listen(sockfd,5); 
    
    while(1){
        clilen = sizeof(cli_addr);
        if((newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen))<0) error("ERROR on accept");
        if((pid = fork())==0){
            bzero(buffer,BUFSIZE);
            if((n = read(newsockfd,buffer,BUFSIZE))<0) error("ERROR reading from socket"); 

            if(n){ // 읽어들인 바이트가 0보다 클 때만 request 메시지를 처리한다. n==0 일때는, 클라이언트가 keep-alive에 대한 인증으로 payload가 없는 probe packet 보내는 경우라고 생각한다.
                buffer[n] = '\0';
                printf("\n%s\n",buffer);

                if(resMaker(newsockfd,buffer) < 0) error("ERROR sending response to client"); 
            }
            else{
                printf("connection keep alive test probe packet\n");
            }
            
            exit(1);
        }
        else if(pid == -1) error("fork error");
        else{
            // shutdown(newsockfd,SHUT_WR);
            close(newsockfd);   
        } 
    }
    
    close(sockfd);
    
    
    return 0; 
}


char *getPath(char *req){
    int i;
    char *makePath = strstr(req," "); // request meesage은 "GET <path> HTTP/1.1 . . . " 과 같은 형식이다. 첫번째 공백문자의 주소를 가르키는 포인터를 찾는다. 
    char *path = malloc(sizeof(char)*20);
    for(i=0;*(makePath+1+i)!=' ';i++){ // 첫번째 공백 다음 문자부터 path가 등장한다. makePath에 할당한 주소+1부터 시작해서 i를 1씩 증가시키면서 다음 공백이 나올 때까지 탐색하면 된다.
        *(path+i) = *(makePath+1+i); // 한칸씩 주소를 이동하면서 그 주소에 해당하는 문자를 path에 채워나간다.
    }
    // example) req-> GET /abc.html HTTP/1.1 .... | *(makePath+1+0)=/, *(makePath+1+1)=a, *(makePath+1+2)=b, *(makePath+1+3)=c ... 이런 식으로 동작한다.
    path[++i] = '\0'; // path에 맨 뒤에 NULL을 추가해준다.
    char *dot_path = calloc(strlen(path)+2,sizeof(char)); 
    if(strcmp(path,"/")==0) dot_path = "./index.html"; // request message의 path가 '/' 라면, path를 "./index.html" 로 설정. (포트번호로 접속시 뜨는 페이지를 index로 설정)
    else sprintf(dot_path,".%s",path);    // 그 외에는 path 앞에 .을 추가해준다.
    return dot_path; // 완성된 path를 반환.
}

int getFileType(char *path){ 
    if(strstr(path,".html")!=NULL) return 0;
    else if(strstr(path,".jpg")!=NULL) return 1;
    else if(strstr(path,".png")!=NULL) return 2;
    else if(strstr(path,".gif")!=NULL) return 3;
    else if(strstr(path,".mp3")!=NULL) return 4;
    else if(strstr(path,".pdf")!=NULL) return 5;
    else if(strstr(path,".ico")!=NULL) return 6;
    else return 7; // 파일 형식에 따라 정수를 반환해주는데, 이 반환 값을 fileType[index] index부분에 넣으면 파일 형식에 따른 헤더 문자열을 반환 받을 수 있다.
    // 지정하지 않은 파일형식을 요청받았을 경우 7을 반환하여 이 후 진행과정에서 7이라면, "./fail.html"로 이동시켜 잘못된 접근임을 클라이언트에게 알린다.
}

int resMaker(int client_fd, char *req){
    int fd,n,contSize,check; 
    struct stat fileStat; // 전송할 파일의 정보를 저장할 stat 구조체.
    char cont[BUFSIZE], *path, res[BUFSIZE]; // cont-> 전송할 파일을 읽고 보내기 위한 버퍼. || path-> request에서 GET을 요청한 경로 || res-> header를 저장할 공간.
    char *resState = "HTTP/1.1 200 OK\r\n"; // Response 헤더 중 state에 해당하는 문자열. 
    char *contLength = "Content-Length: ",*r_contLength =malloc(strlen(contLength)+50); // contLength 뒤에 content의 크기를 붙여 만들 r_contLength 선언.
    char *contType = "Content-Type: ",*r_contType = malloc(strlen(contType)+50); // contType 뒤에 content의 type을 붙여 만들 r_contType 선언.
    char *contAcptRange = "Accept-Ranges: bytes\r\n";
    char fileType[8][30] = {"text/html\r\n",
                    "image/jpg\r\n",
                    "image/png\r\n",
                    "image/gif\r\n",
                    "audio/mpeg\r\n",
                    "application/pdf\r\n",
                    "image/x-icon\r\n",
                    "text/html\r\n"};
    int idfType = getFileType((path = getPath(req))); // req에 대한 path를 getFileType의 인자로 넘겨 파일형식을 나타내는 정수를 반환 받아 idfType에 저장.
    /* idfType 
    | [0] - text/html
    | [1] - image/jpg
    | [2] - image/png
    | [3] - image/gif
    | [4] - audio/mp3
    | [5] - application/pdf
    | [6] - image/x-icon
    | [7] - text/html
    */
    sprintf(r_contType,"%s%s",contType,fileType[idfType]);  // idfType에 해당하는 fileType을 "Content-Type: <fileType[idfType]>" 형식으로 r_contType에 저장.
    if(idfType == 7) path = "./fail.html"; // 알 수 없는 파일 형식을 요청받았을 경우 ./fail.html로 이동하도록 path를 임의로 설정.
    if((fd = open(path,O_RDONLY))<0){ // reqest message에서 추출해낸 path를 open하여 그 file descriptor를 fd에 저장.
        if((fd = open("./fail.html",O_RDONLY))<0) error(path); // 존재하지 않는 path를 요청받았을 경우 "./fail.html"로 이동하도록 path를 임의로 설정
        path = "./fail.html"; 
    }
    stat(path,&fileStat); // path의 stat 생성.
    contSize = fileStat.st_size; // contSize에 path에 해당하는 file의 size 할당.
    
    sprintf(r_contLength,"%s%d\r\n",contLength,contSize); // "Content-Length: <contSize>" 형식으로 r_contLength에 저장.
    sprintf(res,"%s%s%s%s\r\n",resState,r_contLength,r_contType,contAcptRange); // 위에서 생성한 header들을 합하여 Res에 저장.
    
    if((check = write(client_fd,res,strlen(res)))<0) error("header"); // header를 클라이언트에게 전송. (클라이언트 소켓에 write)
    
    
    while((n = read(fd,cont,BUFSIZE))>0){ // fd에 있는 Content를 BUFSIZE만큼 읽는다. 읽어온 바이트의 수가 n에 저장됨. | 실패시 n에 -1 저장 -> while loop 중지.
        if((check = write(client_fd,cont,n))<0){
            perror("write");
            break; // 읽어온 바이트의 수만큼 클라이언트 소켓에 write. | 만약 write 실패시 while문 break.
        } 
        bzero(cont,BUFSIZE);
    }
    free(r_contLength);
    free(r_contType); // 할당한 메모리 free.
    close(fd); // 연 파일 닫기.

    return ( (check < 0 || n < 0) ? -1: 0 ); // check와 n중 하나라도 음수라면 -1을 반환 , 그렇지 않다면 0 반환. == 실패시 -1 반환, 성공시 0 반환.
    
}