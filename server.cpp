

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <signal.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>             // stl vector
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <queue>
#include <semaphore.h>
#include <cstring>
#include <dirent.h>

#define SOCKET_ERROR        -1
#define BUFFER_SIZE         100
#define MESSAGE             "This is the message I'm sending back and forth"
#define QUEUE_SIZE          5
#define MAX_MSG_SZ          1024

using namespace std;


struct thread_info
{
    int thread_id;
    int another_number;
};



// Determine if the character is whitespace
bool isWhitespace(char c)
{
    switch (c)
    {
        case '\r':
        case '\n':
        case ' ':
        case '\0':
            return true;
        default:
            return false;
    }
}

// Strip off whitespace characters from the end of the line
void chomp(char *line)
{
    int len = (int)strlen(line);
    while (isWhitespace(line[len]))
    {
        line[len--] = '\0';
    }
}

// Read the line one character at a time, looking for the CR
// You dont want to read too far, or you will mess up the content
char * getLine(int fds)
{
    char buffer[MAX_MSG_SZ];
    char *line;
    
    int messagesize = 0;
    int amtread = 0;
    
    while((amtread = (int)read(fds, buffer + messagesize, 1)) < MAX_MSG_SZ)
    {
        if (amtread >= 0)
        {
            messagesize += amtread;
        }
        else
        {
            perror("Socket Error is:");
            fprintf(stderr, "Read Failed on file descriptor %d messagesize = %d\n", fds, messagesize);
            exit(2);
        }
        //fprintf(stderr,"%d[%c]", messagesize,message[messagesize-1]);
        if (buffer[messagesize - 1] == '\n')
            break;
    }
    buffer[messagesize] = '\0';
    chomp(buffer);
    line = (char *)malloc((strlen(buffer) + 1) * sizeof(char));
    strcpy(line, buffer);
    //fprintf(stderr, "GetLine: [%s]\n", line);
    return line;
}

// Change to upper case and replace with underlines for CGI scripts
void upcaseAndReplaceDashWithUnderline(char *str)
{
    int i;
    char *s;
    
    s = str;
    for (i = 0; s[i] != ':'; i++)
    {
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] = 'A' + (s[i] - 'a');
        
        if (s[i] == '-')
            s[i] = '_';
    }
    
}

// When calling CGI scripts, you will have to convert header strings
// before inserting them into the environment.  This routine does most
// of the conversion
char *formatHeader(char *str, char *prefix)
{
    char *result = (char *)malloc(strlen(str) + strlen(prefix));
    char* value = strchr(str,':') + 2;
    upcaseAndReplaceDashWithUnderline(str);
    *(strchr(str,':')) = '\0';
    sprintf(result, "%s%s=%s", prefix, str, value);
    return result;
}

// Get the header lines from a socket
//   envformat = true when getting a request from a web client
//   envformat = false when getting lines from a CGI program

//void getHeaderLines(vector<char*> &headerLines int skt, bool envformat)
void getHeaderLines(vector<char *> &headerLines, int skt, bool envformat)
{
    // Read the headers, look for specific ones that may change our responseCode
    char *line;
    char *tline;
    
    //cout << "\nIn getHeaderLines()...\n" << endl;
    
    tline = getLine(skt);
    while(strlen(tline) != 0)
    {
        //cout << "strlen(tline): " << strlen(tline) << endl;
        
        if (strstr(tline, "Content-Length") || strstr(tline, "Content-Type"))
        {
            if (envformat)
            {
                line = formatHeader(tline, (char*)"");
            }
            else
            {
                line = strdup(tline);
            }
        }
        else
        {
            if (envformat)
            {
                line = formatHeader(tline, (char*)"HTTP_");
            }
            else
            {
                line = (char *)malloc((strlen(tline) + 10) * sizeof(char));
                sprintf(line, "HTTP_%s", tline);
            }
        }
        //fprintf(stderr, "Header --> [%s]\n", line);
        
        headerLines.push_back(line);
        free(tline);
        tline = getLine(skt);
    }
    free(tline);
}

void signalHandler(int status)
{
    printf("\nRecieved signal %d\n", status);
}

int setUpSocket(int hServerSocket, struct sockaddr_in Address, int nHostPort, int nAddressSize)
{
    /* make a socket */
    hServerSocket=socket(AF_INET,SOCK_STREAM,0);
    
    if(hServerSocket == SOCKET_ERROR)
    {
        printf("\nCould not make a socket\n");
        return -1;
    }
    
    /* fill address struct */
    Address.sin_addr.s_addr=INADDR_ANY;
    Address.sin_port=htons(nHostPort);
    Address.sin_family=AF_INET;
    
    printf("\nBinding to port %d\n",nHostPort);
    
    /* bind to a port */
    if(::bind(hServerSocket,(struct sockaddr*)&Address,sizeof(Address)) == SOCKET_ERROR)
    {
        printf("\nCould not connect to host\n");
        return -1;
    }
    /*  get port number */
    getsockname( hServerSocket, (struct sockaddr *) &Address,(socklen_t *)&nAddressSize);
    printf("opened socket as fd (%d) on port (%d) for stream i/o\n",hServerSocket, ntohs(Address.sin_port) );
    
    printf("Server\n\
           sin_family        = %d\n\
           sin_addr.s_addr   = %d\n\
           sin_port          = %d\n"
           , Address.sin_family
           , Address.sin_addr.s_addr
           , ntohs(Address.sin_port)
           );
    
    return hServerSocket;
}

void setUpSignalHandlers(struct sigaction sigold, struct sigaction signew)
{
    signew.sa_handler = signalHandler;
    sigemptyset(&signew.sa_mask);
    sigaddset(&signew.sa_mask, SIGINT);
    signew.sa_flags = SA_RESTART;
    sigaction(SIGINT, &signew, &sigold);
}



int GetFileSize(std::string filename)
{
    struct stat stat_buf;
    int rc = stat(filename.c_str(), &stat_buf);
    return (int)(rc == 0 ? stat_buf.st_size : -1);
}





//void *serve(void *in_data)      // tid = thread id, dir = html/
void serve(int socketid, char* request, char* dir)
{
    vector<char *> headerLines;
    string typeHtml = "text/html";
    string typeText = "text/plain";
    string typeGif = "image/gif";
    string typeJpg = "image/jpg";
    string type;
    char* pBuffer[BUFFER_SIZE];
    char* method;
    char* filename;
    char* file;
    char* httpVersion;
    char* contentType;
    
    
            
    char* s = strtok (request ," ");

    method = s;
    printf("method: %s\n", method);
    s = strtok (NULL, " ");
    file = s;

    string str1(dir);
    string str2(file);
    string path = str1 + str2;

    printf("file: %s\n", path.c_str());
    s = strtok (NULL, " ");
    httpVersion = s;
    printf("http: %s\n", httpVersion);

    // Read the header lines
    getHeaderLines(headerLines, socketid , false); 
   
    printf("\nServing %s...\n", path.c_str());

    struct stat filestat;

    if(stat(path.c_str(), &filestat)) {
        string errorPage = "<html><head><title>Page Not Found</title></head><body><h1>Error! 404, Page Not Found.</h1></body></html>";
        string response = "HTTP/1.0 404 error\r\n";
        response += "Content-Length: "+to_string(errorPage.length())+"\r\n";
        response += "Content-Type: text/html\r\n\r\n";
        
        response += errorPage;
        
        write(socketid, response.c_str(), response.length());
    }
    else if(S_ISREG(filestat.st_mode)) {
        printf("%s is a regular file \n", path.c_str());
        printf("file size = %d\n", (int)filestat.st_size);
        
        if(path.substr(path.find_last_of(".") + 1) == "html") {
            type = typeHtml;
        }
        else if(path.substr(path.find_last_of(".") + 1) == "txt") {
            type = typeText;
        }
        else if(path.substr(path.find_last_of(".") + 1) == "gif") {
            type = typeGif;
        }
        else if(path.substr(path.find_last_of(".") + 1) == "jpg") {
            type = typeJpg;
        }
        else {
            type = typeText;
        }
        
        string response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: "+type+"\r\n";
        response += "Content-Length: "+to_string((int)filestat.st_size)+"\r\n\r\n";
        
        write(socketid, response.c_str(), response.length());
        
        int fd = open(path.c_str(),O_RDONLY);
        if(fd < 0) {
            printf("open of %s failed", path.c_str());
            return;
        }
        
        int rval;
        while((rval = (int)read(fd, pBuffer, BUFFER_SIZE)) > 0)
        {
            write(socketid,pBuffer,rval);
        }
    }
    else if(S_ISDIR(filestat.st_mode)) {
        int len;
        DIR *dirp;
        struct dirent *dp;
        bool isIndex = false;
        
        string response = "HTTP/1.1 200 OK\r\n";
        
        dirp = opendir(path.c_str());
        while ((dp = readdir(dirp)) != NULL) {
            if(strcmp(dp->d_name, "index.html") == 0) {
                isIndex = true;
                break;
            }
        }
        rewinddir(dirp);
        //(void)closedir(dirp);
        
        if(isIndex) {
            string filename = str2.substr(1, str2.size()-1)+"/index.html";
            
            response += "Content-Type: text/html\r\n";
            response += "Content-Length: "+to_string(GetFileSize(filename))+"\r\n\r\n";
            
            printf("\n\nResponse:\n%s\n\n", response.c_str());
            
            write(socketid, response.c_str(), response.length());
        
            int fd = open(("html/"+filename).c_str(),O_RDONLY);
            if(fd < 0) {
                printf("open of %s failed", filename.c_str());
                return;
            }
            
            int rval;
            while((rval = (int)read(fd, pBuffer, BUFFER_SIZE)) > 0)
            {
                write(socketid,pBuffer,rval);
            }
            
            if(close(socketid) == SOCKET_ERROR)
            {
                printf("\nCould not close socket\n");
                return;
            }
        }
        else {
            response += "Content-Type: "+type+"\r\n";
            response += "Content-Length: "+to_string((int)filestat.st_size)+"\r\n\r\n";

            response += "<html><head><title>Directory Listing</title></head><body>";//<ul>";

            //dirp = opendir(path.c_str());
            while ((dp = readdir(dirp)) != NULL) {
                string filename = dp->d_name;
                if(filename.back() == '/'){
                    filename = filename.substr(0, filename.size()-2);
                }
                string link = "<a href='"+str2.substr(1, str2.size()-1)+"/"+filename+"'>"+filename+"</a><br>";
                printf("Appending: %s", link.c_str());
                response += link;
            }
            (void)closedir(dirp);

            response += "</body></html>";

            write(socketid,response.c_str(),response.length());
        }
    }

    if(close(socketid) == SOCKET_ERROR)
    {
        printf("\nCould not close socket\n");
        return;
    }
}



int main(int argc, char* argv[])
{
    int hSocket = -1
        , hServerSocket = -1
        , nHostPort = -1
        , numThreads
        , nAddressSize = sizeof(struct sockaddr_in);
    struct hostent* pHostInfo;
    struct sockaddr_in Address;
    //struct sigaction sigold, signew;
    char* dir;
    
    
    
    
    
    if(argc < 4)
    {
        printf("\nUsage: server <port number> <number of threads> <dir>\n");
        return 0;
    }
    else
    {
        nHostPort = atoi(argv[1]);
        numThreads = atoi(argv[2]);
        dir = argv[3];
    }
    
    
    
    // Prepare thread info
    //sem_init(&work_mutex, 0, 1);
    pthread_t threads[numThreads];
    struct thread_info all_thread_info[numThreads];
    
    // Create threads
    printf("Making a list of %d threads.\n", numThreads);
    /*for(int i=0; i<numThreads; i++)
    {
        //sem_wait(&work_mutex);
        //&work_mutex.lock();
        
        printf("creating thread %d\n", i);
        all_thread_info[i].thread_id = i;
        pthread_create(&threads[i], NULL, serve, (void*) &all_thread_info[i]);
        pthread_join(threads[i], NULL);     // I don't know anything about this...
        
        //sem_post(&work_mutex);
    }
    //sem_close(&work_mutex);
    */
    
    
    printf("\nStarting server...\n");
    
    // Set up
    //setUpSignalHandlers(sigold, signew);
    hServerSocket = setUpSocket(hServerSocket, Address, nHostPort, nAddressSize);
    
    if(hServerSocket < 0)
    {
        printf("Exiting...\n");
        return 0;
    }
    
    
    
    // Establish listen queue
    if(listen(hServerSocket,numThreads) == SOCKET_ERROR)
    {
        printf("\nCould not listen!\t-> %d\n", listen(hServerSocket, numThreads));
        return 0;
    }
    
    while(1)
    {
        printf("\n\t---------------------------\n");
        printf("\n\tWaiting for a connection...\n");
        printf("\n\t---------------------------\n\n");
        
        // Get the connected socket
        hSocket = accept(hServerSocket, (struct sockaddr*)&Address, (socklen_t*)&nAddressSize);
        
        if(hSocket < 0)
        {
            printf("\nERROR: Could not accept connection!\n");
        }
        else
        {
            printf("\nGot a connection from %X (%d)\n"
               , Address.sin_addr.s_addr
               , ntohs(Address.sin_port));
            
            
              // First read the status line
                char *startline = getLine(hSocket);
              printf("Start line %s\n\n",startline);
            
            serve(hSocket, startline, dir);
        }
    }
}














