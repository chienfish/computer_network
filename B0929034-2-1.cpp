#include <iostream>
#include <stdio.h>
#include <string>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <ctime>
using namespace std;

string Domain = "", Path = "", content = "";
int method = 0, fileNum = 0, fileSize = 0, step = 1;
double START = 0, END = 0;

void ParseUrl(string url) {  // 分解url -> 產生domain, path
	int init;
    if (url.substr(0, 7) == "http://") init = 7;
    else if (url.substr(0, 8) == "https://") init = 8;

    bool dash = false;
    for (int i = init; i < url.length(); i++) {
        if (url[i] == '/') dash = true;
        if (url[i] == '/' && dash == true) {
            Domain = url.substr(init, i-init);
            Path = url.substr(i, url.length());
            break;
        }
    }
}

string HostToIp(const string& host) {   // 找出IP address
    hostent* hostname = gethostbyname(host.c_str());
    if (hostname) return string(inet_ntoa(**(in_addr**)hostname->h_addr_list));
    else return "-1";
}

string NonPerRequest() {     // non-persistent使用的request
    char message[2048] = {};
    sprintf(message, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: Close\r\n"     // 發送完請求就會關閉連線
        "\r\n" ,Path.c_str(), Domain.c_str());

    return message;
}

string PerRequest() {    // persistent使用的request
    char message[2048] = {};
    sprintf(message, 
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: keep-alive\r\n"    // 連線會一直開著
        "\r\n" ,Path.c_str(), Domain.c_str());

    return message;
}

int ConnectHttp() { // 連線http建立TCP連線
    // 建立socket
    int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == -1){
        cout << "無效的socket!" << endl;
        exit(-1);
    }

    // socket的結構
    struct sockaddr_in info;
    string ip = HostToIp(Domain);
    bzero(&info,sizeof(info));	// 初始化 將struct涵蓋的bits設為0
    info.sin_family = AF_INET; // 通訊協定(Internet)
    info.sin_port = htons(80); // 連接的埠口位址
    if (inet_pton(info.sin_family, ip.c_str(), &info.sin_addr) == 0) {
		cout << "請確認url是否有效以及連線狀況!" << endl;
        exit(0);
    }

    // 連線
    if (connect(sock, (struct sockaddr *)&info, sizeof(info)) == -1){
		cout << "連線失敗!" << endl;
        exit(-1);
    }
    END = clock();
    printf("STEP%d | %f(s): Connecting to server...\n", step, (END-START)/CLOCKS_PER_SEC);
    step++;

    return sock;
}

void downloadIMG(string imgSRC, int imgNUM, int sd) {
    // 切割imgSRC -> 得到domain&path
    ParseUrl(imgSRC);

    // 發出請求並連線
    printf("-- 物件%d --\n", fileNum);
    string req = "";
    if (method == 1) req = PerRequest();
    else if (method == 2) {
        req = NonPerRequest();
        sd = ConnectHttp();
    }
    send(sd, req.c_str(), strlen(req.c_str()), 0);

    // 建立檔案
    START = clock();
    char buf[10000]={};
    string name = "pic" + to_string(imgNUM) + ".jpg";
    int pic = open(name.c_str(), O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR); 
    int recvSize = recv(sd, buf, sizeof(buf)-1, 0);

    // 取得img的content-length
    string contentLen = "Content-Length: ";
    char *sub = strstr(buf, contentLen.c_str()) + strlen(contentLen.c_str());
    string targetLen;
    while (isdigit(*sub)) {
        targetLen += *sub;
        sub++;
    }

    // 取得圖片內容並寫入檔案
    char *imgContent = strstr(buf, "\r\n\r\n") + 4; // 回傳的內容加上兩個CRLF
    int imgContentLen = recvSize-(imgContent-buf);
    write(pic, imgContent, imgContentLen);
    while (true) {
        recvSize = recv(sd, buf, sizeof(buf)-1, 0);
        write(pic, buf, recvSize);
        imgContentLen += recvSize;

        if (imgContentLen >= stoi(targetLen)) break; // 當讀取到的內容跟content length一樣 -> 全部讀完
    }
    fileSize += imgContentLen;
    close(pic);
    if (method == 2) shutdown(sd, SHUT_WR);

    END = clock();
    printf("STEP%d | %f(s): Dowdloading %s...\n", step, (END-START)/CLOCKS_PER_SEC, imgSRC.c_str());
    step++;
}

void downloadHREF(string href, int sd) {
    ParseUrl(href);
    printf("-- 物件%d --\n", fileNum);
    string req = "";
    char buf[10000]={};
    if (method == 1) req = PerRequest();
    else if (method == 2) {
        req = NonPerRequest();
        sd = ConnectHttp();
    }
    send(sd, req.c_str(), strlen(req.c_str()), 0);
    read(sd, buf,1024);

    string BUFFER = buf;
    string subContent = "";
    for (int i = 0; i < BUFFER.length(); i++) { 
        if (buf[i] != '<') continue;
        else {
            subContent = BUFFER.substr(i, BUFFER.length());
            break;
        }
    }
    
    START = clock();
    int fd = open(Path.substr(1, BUFFER.length()).c_str(), O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR); 
    write(fd, subContent.c_str(), strlen(subContent.c_str()));
    close(fd);
    fileSize += strlen(subContent.c_str());
    END = clock();
    printf("STEP%d | %f(s): Downloading the content of %s...\n", step, (END-START)/CLOCKS_PER_SEC, Path.substr(1, BUFFER.length()).c_str());
    step++;
}

void renameSrc(int left, int right, int imgNUM) {
    string name = "pic"+ to_string(imgNUM) + ".jpg";
    content.replace(left, right-left, name);
}

int main(int argc, char **argv) {
    cout << "連線方式有兩種:\t" << "1. persistent\t" << "2. non-persistent" << endl;
    cout << "請輸入欲選方式之數值: ";
    cin >> method;

    // 判斷connection
    if (method != 1 && method != 2) {
        cout << "\n輸入值不在選項中!" << endl;
        return 0;
    }

    double FirstStart = 0;
    FirstStart = clock();

    // 判斷在執行檔後是否有加入參數
    string url, dir;
    if (argc == 1) { // 在終端機沒有輸入任何字串
        cout << "請輸入url: ";
        cin >> url;
        cout << "請輸入目錄: ";
        cin >> dir;
    } else if (argc == 3) { // 在終端機輸入url, dir
        url = argv[1];
        dir = argv[2];
    } else {
        cout << "輸入錯誤!" << endl;
        return 0;
    }

    // 切割url
    puts("\n>>>>>>進度與步驟<<<<<<");
    START = clock();
    ParseUrl(url);
    END = clock();
    printf("STEP%d | %f(s): Parsing url...\n", step, (END-START)/CLOCKS_PER_SEC);
    step++;

    // 建立資料夾
    char pastPath[1024], nowPath[1024];
    string createDir = "mkdir -p " + dir;  
    START = clock();
    system(createDir.c_str());
    getcwd(pastPath, 1024);
    sprintf(nowPath, "%s/%s",pastPath, dir.c_str());
    chdir(nowPath);
    END = clock();
    printf("STEP%d | %f(s): Creating/Opening %s file...\n", step, (END-START)/CLOCKS_PER_SEC, dir.c_str());
    step++;

    // 得到request訊息(start-line & header) -> 建立TCP連線 -> 取得response
    string request = "";
    if (method == 1) request = PerRequest();
    else if (method == 2) request = NonPerRequest();
    
	int sock = ConnectHttp();
	char buffer[1024] = {};
    printf("-- 物件%d --\n", fileNum+1);
	send(sock, request.c_str(), strlen(request.c_str()), 0);
    read(sock, buffer,1024);
    
    // 將傳回的內容刪掉request -> 剩下html(response)
    string BUFFER = buffer;
    for (int i = 0; i < BUFFER.length(); i++) { 
        if (buffer[i] != '<') continue;
        else {
            content = BUFFER.substr(i, BUFFER.length());
            break;
        }
    }
    
    // 抓所有照片的src
    vector<string> allIMG;
    int left = 0, right = 0;
    string substr = "src=";
    while ((left = content.find(substr, left)) != string::npos) {
        for (int j = left+5; j < content.length(); j++){
            if (content[j] == '"') {
                right = j;
                break;
            }   
        }
        string imgSRC = content.substr(left+5, right-left-5);
        renameSrc(left+5, right, allIMG.size());
        allIMG.push_back(imgSRC);
        left += substr.length();
    }

    // 抓所有<a>的href
    vector<string> allHREF;
    left = 0, right = 0;
    substr = "href=";
    while ((left = content.find(substr, left)) != string::npos) {
        for (int j = left+6; j < content.length(); j++){
            if (content[j] == '"') {
                right = j;
                break;
            }   
        }
        string aHREF = content.substr(left+6, right-left-6);
        allHREF.push_back(aHREF);
        left += substr.length();
    }


    //建立html檔案
    START = clock();
    int fd = open(Path.substr(1, BUFFER.length()).c_str(), O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR); 
    write(fd, content.c_str(), strlen(content.c_str()));
    close(fd);
    fileSize += strlen(content.c_str());
    fileNum++;
    END = clock();
    printf("STEP%d | %f(s): Downloading the content of %s...\n", step, (END-START)/CLOCKS_PER_SEC, Path.substr(1, BUFFER.length()).c_str());
    step++;

    // 下載圖片
    for (int i = 0; i < allIMG.size(); i++) {
        string src = "";
        if (allIMG[i][0] == '/') allIMG[i].erase(0, 1);

        if (allIMG[i].find("http") == -1)   src = "http://"+ Domain + "/"+ allIMG[i];
        else   src = allIMG[i];
        fileNum++;
        downloadIMG(src, i, sock);
    }

    // 下載href的htm
    for (int i = 0; i < allHREF.size(); i++) {
        string src = "";
        if (allHREF[i][0] == '/') allHREF[i].erase(0, 1);

        if (allHREF[i].find("http") == -1)   src = "http://"+ Domain + "/"+ allHREF[i];
        else   src = allHREF[i];
        fileNum++;
        downloadHREF(src, sock);
    }

    // END = clock();
    // puts("\n>>>>>>狀態與統計資訊<<<<<<");
    // printf("%s為有效的url\n", url.c_str());
    // printf("檔案數量: %d\n", fileNum);
    // printf("下載總容量: %d KB\n", fileSize/1024);
    // printf("下載花費時間: %f (s)\n", (END-FirstStart)/CLOCKS_PER_SEC);

    // puts("\n>>>>>>連線資訊<<<<<<");
    // printf("URL: %s\n", url.c_str());
    // printf("DOMAIN: %s\n", Domain.c_str());
    // printf("IP: %s\n", HostToIp(Domain).c_str());
    // printf("PORT: %d\n", 80); 
    // shutdown(sock, SHUT_WR);

    return 0;
}