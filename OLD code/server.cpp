

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

#define SOCKET_ERROR        -1
#define BUFFER_SIZE         100
#define MESSAGE             "This is the message I'm sending back and forth"
#define QUEUE_SIZE          5
#define MAX_MSG_SZ          1024

using namespace std;

queue<int> workload; // I don't feel good about thiiiisssss..... :S
sem_t work_mutex;


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










void *serve(void *in_data)      // tid = thread id, dir = html/
{
    //long tid = (long)threadid;
    char contentType[MAX_MSG_SZ];
    vector<char *> headerLines;
    
/*
     get a task from the queue
     the task is a socket
     once the task (socket) is grabbed from the queue i read in the request from that socket
     if there is no task to grab, sleep for a little bit then check back later
     take the read in request and parse the headers searching for the requested resource (URI) in the request
     check to see if the requested resource exists by using stat
     the program needs to know where to look for the local resource, this must be set by you
     you’ll want to check if it is a file or a directory
     if it is a file, using stat will give you the size of the file and can be used to populate the Content-Length HTTP header
     also check the requested resources extension to make sure it is one of the supported file formats, use this to populate the Content-Type header
     if the resource exists, i create a HTTP/1.x 200 OK response and send it to the requester
     the response needs to include the content length
     repeat until the program is closed -- while(1){}
*/
    
    char pBuffer[BUFFER_SIZE];
    char method[MAX_MSG_SZ];
    char requestedFile[MAX_MSG_SZ];
    char httpVersion[MAX_MSG_SZ];
    int socketid;
    
    
    
    struct thread_info* t_info = ( struct thread_info* ) in_data;
    int tid = t_info->thread_id;
    
    while( 1 )
    {
        sem_wait( &work_mutex );
        
        //printf("I am thread: %d\n", tid);
        
        if(workload.size() <= 0)        // If there is NO task...
        {
            usleep(30);
        }
        else
        {
            //printf("\nGetting socket ID: %d, size: %d\n", workload.front(), (int)workload.size());
            
            socketid = workload.front();
            workload.pop();
            printf("new size: %d\n", (int)workload.size());
        }
        
        sem_post( &work_mutex );
    }
    
    
    
    
    /*
    
    
    // First get a task from the queue
    while(1)
    {
        sem_wait( &work_mutex );
        
        if(workload.size() <= 0)        // If there is NO task...
        {
            usleep(30);
        }
        else     // else if there IS a task, then process it and jump back to the top!
        {
            printf("\nGetting socket ID: %d, size: %d\n", workload.front(), (int)workload.size());

            socketid = workload.front();
            workload.pop();             // ----- I don't feel like this is working -----  -2?? That's not even a thing! :(
            printf("new size: %d\n", (int)workload.size());
            
            
            
            printf("\nServing...\n");
            
            // Read the request line
            char *request = getLine(socketid);
            printf("Request line %s\n\n",request);
            
            // Parse through the request...
            if(strstr(request, "GET"))
            {
                sscanf(request, "%s%s%s", method, requestedFile, httpVersion);
            }
            
            printf("method: %s\nrequestedFile: %s\nhttpVersion: %s\n", method, requestedFile, httpVersion);
            
            // Read the header lines
            getHeaderLines(headerLines, socketid , false);
            
            
            // Now print them out
            for (int i = 0; i < headerLines.size(); i++) {
                printf("[%d] %s\n",i,headerLines[i]);
                if(strstr(headerLines[i], "Content-Type")) {
                    sscanf(headerLines[i], "Content-Type: %s", contentType);
                }
            }
            
            
            
            struct stat filestat;
            
            if(stat(requestedFile, &filestat)) {
                printf("ERROR in stat\n");
            }
            if(S_ISREG(filestat.st_mode)) {
                printf("%s is a regular file \n", requestedFile);
                printf("file size = %d\n", (int)filestat.st_size);
                
                write(socketid, "HTTP/1.0 200 OK", strlen("HTTP/1.0 200 OK"));
                write(socketid, "<head></head><body><p>This is a pretty weird test</p></body>", strlen("<head></head><body><p>This is a pretty weird test</p></body>"));
            }
            if(S_ISDIR(filestat.st_mode)) {
                printf("%s is a directory\n", requestedFile);
            }
            
            printf("\nClosing the socket (In Thread)");
            // close socket
            if(close(socketid) == SOCKET_ERROR)
            {
                printf("\nCould not close socket\n");
                return 0;
            }
        }
        
        sem_post( &work_mutex );
    }
    */
    
    //pthread_exit(NULL);
}




























int main(int argc, char* argv[])
{
    int hSocket = -1, hServerSocket = -1;  /* handle to socket */
    struct hostent* pHostInfo;   /* holds info about a machine */
    struct sockaddr_in Address; /* Internet socket address struct */
    int nAddressSize = sizeof(struct sockaddr_in);
    char pBuffer[BUFFER_SIZE];
    int nHostPort = -1, numThreads;
    string str = "html/";
    char* dir = (char*)str.c_str();
    int optval = 1;
    int rc;
    //mutex mutex;
    //queue<int> workload;
    
    
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
    
    
    
    
    
    /*
     
     
     
     std::cout << "This is an example of pthreads!" << std::endl;
     
     sem_init( &work_mutex, 0, 1 );
     
     pthread_t threads[ THREADS ];
     
     struct thread_info all_thread_info[ THREADS ];
     
     for( int i = 0; i < THREADS; i++ )
     {
     sem_wait( &work_mutex );
     
     std::cout << "creating thread: " << i << "\t" << std::endl;
     all_thread_info[ i ].thread_id = i;
     pthread_create( &threads[ i ], NULL, serve, ( void* ) &all_thread_info[ i ] );
     
     sem_post( &work_mutex );
     }
     
     while( 1 )
     {
     // spin your wheels
     }
     
     return 0;
     
     */
    
    
    // First set up the signal handler
    struct sigaction sigold, signew;
    signew.sa_handler = signalHandler;
    sigemptyset(&signew.sa_mask);
    sigaddset(&signew.sa_mask, SIGINT);
    signew.sa_flags = SA_RESTART;
    sigaction(SIGINT, &signew, &sigold);
    
    sem_init( &work_mutex, 0, 1 );
    pthread_t threads[numThreads];
    struct thread_info all_thread_info[numThreads];

    printf("\nStarting server...");
    printf("\nMaking socket");
    hServerSocket = setUpSocket(hServerSocket, Address, nHostPort, nAddressSize);
    
    if(hServerSocket < 0)
    {
        printf("Exiting...\n");
        return 0;
    }

    printf("Making a thread queue of %d elements\n", numThreads);
    
    for(int i=0; i<numThreads; i++)
    {
        /* Create independent threads each of which will execute serve() */
        
        sem_wait( &work_mutex );
        
        printf("creating thread: %d\n", i);
        all_thread_info[ i ].thread_id = i;
        
        pthread_create( &threads[ i ], NULL, serve, ( void* ) &all_thread_info[ i ] );
        //pthread_join(threads[ i ], NULL);
        
        sem_post( &work_mutex );
    }
    
    /* establish listen queue */        // -- queue..?
    if(listen(hServerSocket,numThreads) == SOCKET_ERROR)
    {
        printf("\nCould not listen\n");
        return 0;
    }

    while(1)
    {
        printf("\n\t---------------------------\n");
        printf("\n\tWaiting for a connection...\n");
        printf("\n\t---------------------------\n\n");
        /* get the connected socket */
        hSocket=accept(hServerSocket,(struct sockaddr*)&Address,(socklen_t *)&nAddressSize);
        
        setsockopt (hSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)); // To keep use of the port I guess..
        
        if(hSocket < 0)
        {
            printf("ERROR: Could not accept connection!");
        }
        else
        {
            printf("Adding new task: %d", hSocket);
            // Add a new task to the queue
            workload.push(hSocket);
        }
        
        printf("\nGot a connection from %X (%d)\n"
               , Address.sin_addr.s_addr
               , ntohs(Address.sin_port));
        
        
        
        //strcpy(pBuffer,MESSAGE);
        //printf("\nSending \"%s\" to client",pBuffer);
        /* number returned by read() and write() is the number of bytes
        ** read or written, with -1 being that an error occured
        ** write what we received back to the server */
        //write(hSocket,pBuffer,strlen(pBuffer)+1);
        /* read from socket into buffer */
        //memset(pBuffer,0,sizeof(pBuffer));
        //read(hSocket,pBuffer,BUFFER_SIZE);
        
        
        // -- I think we just need to close the socket in serve(), right after the THREAD does the work --
        
        //printf("\nClosing the socket");
        /* close socket */
        //if(close(hSocket) == SOCKET_ERROR)
        //{
        //    printf("\nCould not close socket\n");
        //    return 0;
        //}
    }
}


















