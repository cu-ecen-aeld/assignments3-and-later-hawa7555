
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <syslog.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <sys/queue.h>
#include <time.h>

#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define DEVICE_PATH "/dev/aesdchar"
#else
#define DEVICE_PATH "/var/tmp/aesdsocketdata"
#endif

volatile int exit_flag = 0;

void signal_handler(int signal_number)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
}

struct ThreadNode {
    pthread_t thread;
    int client_socket;
    struct sockaddr client_addr;
    SLIST_ENTRY(ThreadNode) entries;
};
pthread_mutex_t file_mutex;

//Client connection threads
void* connection_thread(void *threadArgs)
{
    //get data passed from thread args
    struct ThreadNode* thread_node = (struct ThreadNode*) threadArgs;
    int client_socket = thread_node->client_socket;
    struct sockaddr_in *client_addr_in = (struct sockaddr_in *)&thread_node->client_addr;
    
    char ipstr[INET_ADDRSTRLEN];
    int fd, rc;
    inet_ntop(thread_node->client_addr.sa_family, &client_addr_in->sin_addr, ipstr, sizeof(ipstr) );
    syslog(LOG_DEBUG, "Accepted connection from %s\n", ipstr);

    char rec_buffer[2048];
    int curr_num_of_bytes;
    char* buffer_start = NULL;
    int total_size = 0;
    char* new_buffer_start;
    bool data_packet_complete = false;

    //Receive data
    do
    {
        curr_num_of_bytes = recv(client_socket, rec_buffer, sizeof(rec_buffer) - 1, 0);
        if(curr_num_of_bytes <= 0) break;
        rec_buffer[curr_num_of_bytes] = '\0';
        new_buffer_start = realloc(buffer_start, total_size + curr_num_of_bytes + 1);

        if(new_buffer_start == NULL)
        {
            syslog(LOG_ERR, "Failed to allocate new memory for packet\n");
            break;
        }

        buffer_start = new_buffer_start;
        memcpy(buffer_start + total_size, rec_buffer, curr_num_of_bytes);
        total_size += curr_num_of_bytes;
        buffer_start[total_size] = '\0';

        if(strchr(rec_buffer, '\n') != NULL)
        {
            data_packet_complete = true;
            break;
        }
    } while(1);

    //if /n was not received or recv returned less than zero or zero bytes
    if(data_packet_complete == false)
    {
        syslog(LOG_ERR, "Didn't receive full packet, connection might also be closed by client\n");

        if(buffer_start != NULL)
        {
            free(buffer_start);
        }

        close(client_socket);
        return NULL;
    }

    //no data from client
    if(buffer_start == NULL)
    {
        syslog(LOG_ERR, "No data to write, exiting \n");
        close(client_socket);
        return NULL;
    }

    //open/create file
    pthread_mutex_lock(&file_mutex);                                                   //lock mutex before accessing/creating file
    fd = open(DEVICE_PATH, O_RDWR|O_CREAT|O_APPEND, 0600);

    //Error creating/opening file, close the client, unlock mutex and return from this thread
    if(fd == -1)
    {
        syslog(LOG_ERR, "Error creating/opening file \n");
        perror("Error creating/opening file");

        if(buffer_start != NULL) 
        {
            free(buffer_start);
        }
        
        close(client_socket);
        pthread_mutex_unlock(&file_mutex);
        return NULL;
    }

    //Writing to file
    ssize_t nr;
    nr = write(fd, buffer_start, total_size);
    close(fd);
    pthread_mutex_unlock(&file_mutex);                  //unlock mutex after writing to file

    if(buffer_start != NULL) 
    {
        free(buffer_start);
    }

    //Error writing to file
    if(nr == -1)
    {
        syslog(LOG_ERR, "Error writing to file \n");
        perror("Error writing to file");
        close(client_socket);
        return NULL;
    }

    //re-using rec buffer to read data from file
    memset(rec_buffer, 0, sizeof(rec_buffer) );

    //lock mutex to read from file now
    pthread_mutex_lock(&file_mutex);
    fd = open(DEVICE_PATH, O_RDONLY);
    if(fd == -1)
    {
        syslog(LOG_ERR, "Error opening file for reading\n");
        perror("Error opening file for reading");
        pthread_mutex_unlock(&file_mutex);
        close(client_socket);
        return NULL;
    }

    //reading from file
    do
    {
        nr = read(fd, rec_buffer, sizeof(rec_buffer));

        if(nr == -1)
        {
            syslog(LOG_ERR, "Error reading data from file\n");
            perror("Error reading data from file");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            close(client_socket);
            return NULL;
        }

        rc = send(client_socket, rec_buffer, nr, 0);
        if(rc == -1)
        {
            syslog(LOG_ERR, "Error sending data to client\n");
            perror("Error sending data to client");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            close(client_socket);
            return NULL;
        }
    } while(nr > 0);

    pthread_mutex_unlock(&file_mutex);
    close(fd);

    rc = close(client_socket);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error closing connection from %s\n", ipstr);
        perror("Error closing connection");
    }
    
    syslog(LOG_DEBUG, "Closed connection from %s\n", ipstr);
    return NULL;
}

#if !USE_AESD_CHAR_DEVICE
static void timer_thread (union sigval sigval)
{
    struct timespec time_val;
    struct tm *tmp;
    char outstr[200];
    int fd, rc;

    if (clock_gettime(CLOCK_REALTIME, &time_val) != 0)
    {
        perror("clock_gettime");
        return;
    }

    tmp = localtime(&time_val.tv_sec);
    if(tmp == NULL)
    {
        syslog(LOG_ERR, "Couldn't get local time");
        return;
    }
    rc = strftime(outstr, sizeof(outstr), "timestamp: %a, %d %b %Y %T %z", tmp);
    outstr[rc]='\n';
    outstr[rc+1] = '\0';

    if (rc == 0)
    {
        syslog(LOG_ERR, "strftime returned 0");
        return;
    }

    pthread_mutex_lock(&file_mutex);
    fd = open(DEVICE_PATH, O_RDWR|O_CREAT|O_APPEND, 0600);
    if(fd == -1)
    {
        perror("file error");
        pthread_mutex_unlock(&file_mutex);
        return;
    }

    ssize_t nr;
    nr = write(fd, outstr, rc+1);
    close(fd);
    pthread_mutex_unlock(&file_mutex);
    if(nr == -1)
    {
        syslog(LOG_ERR, "Error writing to file \n");
        perror("Error writing to file");
        return;
    }        
}
#endif

SLIST_HEAD(slisthead, ThreadNode) head_node;

int main(int argc, char **argv)
{
    bool daemon_mode = false;

    if (argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = true;
    }

    openlog(NULL, 0, LOG_USER);               //open log with LOG_USER facility

    #if !USE_AESD_CHAR_DEVICE
    remove(DEVICE_PATH);
    #endif

    struct addrinfo addr_info;
    struct addrinfo* serv_info;
    int rc;

    struct sockaddr client_addr;
    socklen_t client_addr_size = sizeof(client_addr);

    int my_socket, client_socket;
    my_socket = socket(PF_INET, SOCK_STREAM, 0);
    if(my_socket == -1)
    {
        syslog(LOG_ERR, "Error creating socket desc \n");
        perror("Error creating socket desc");
        closelog();
        exit(1);
    }

    int opt = 1;
    if(setsockopt(my_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        syslog(LOG_ERR, "Error setting socket options\n");
        perror("Error setting socket options");
        close(my_socket);
        closelog();
        exit(1);
    }

    memset(&addr_info, 0, sizeof(addr_info) );
    addr_info.ai_family = PF_INET;
    addr_info.ai_socktype = SOCK_STREAM;
    addr_info.ai_flags = AI_PASSIVE;

    rc = getaddrinfo(NULL, "9000", &addr_info, &serv_info);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error with getaddrinfo \n");
        perror("Error with getaddrinfo");
        close(my_socket);
        closelog();
        exit(1);
    }

    rc = bind(my_socket, serv_info->ai_addr, serv_info->ai_addrlen);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error with bind \n");
        perror("Error with bind");
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    rc = listen(my_socket, 5);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error with listen \n");
        perror("Error with listen");
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    if (daemon_mode)
    {
        pid_t pid = fork();
    
        if (pid < 0)
        {
            perror("Error with forking");
            exit(1);
        }
        
        if (pid > 0)
        {
            exit(0);
        }
        
        setsid();
        chdir("/");

        //close all file desc
        for (int  i = 0; i < sysconf(_SC_OPEN_MAX); i++)
        {
            if(i != my_socket)
            {
                close(i);
            }
        }
        
        //re-direct stdin, stdout, stderr to /dev/null
        open("/dev/null", O_RDWR);
        dup(0);
        dup(0);
        openlog(NULL, 0, LOG_USER);
    }

    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action) );
    new_action.sa_handler = signal_handler;

    if( sigaction(SIGINT, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error registering for SIGINT\n");
        perror("Error registering for SIGINT");
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    if( sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error registering for SIGTERM\n");
        perror("Error registering for SIGTERM");
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    SLIST_INIT(&head_node);
    pthread_mutex_init(&file_mutex, NULL);

    #if !USE_AESD_CHAR_DEVICE
    //creating timer thread to post timestamps in file
    timer_t timerid;
    struct sigevent sev;
    memset(&sev, 0, sizeof(struct sigevent));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_value.sival_ptr = NULL;
    sev.sigev_notify_function = timer_thread;

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) != 0)
    {
        perror("timer_create");
        exit(1);
    }
    
    //timer alarm to go off after 10 seconds
    struct itimerspec timer_value;

    timer_value.it_value.tv_sec = 10;
    timer_value.it_value.tv_nsec = 0;
    
    timer_value.it_interval.tv_sec = 10;
    timer_value.it_interval.tv_nsec = 0;

    //start timer, before blocking on accept()
    if (timer_settime(timerid, 0, &timer_value, NULL) != 0)
    {
        perror("timer_settime");
        exit(1);
    }
    #endif

    while(!exit_flag)
    {
        client_socket = accept(my_socket, &client_addr, &client_addr_size);         //accept connections from client, blocking call

        if(client_socket == -1)
        {
            if(exit_flag) break;
            syslog(LOG_ERR, "Error accepting connection \n");
            perror("Error accepting one connection");
            continue;
        }

        struct ThreadNode* thread_node_ptr = (struct ThreadNode*) malloc( sizeof(struct ThreadNode) );

        if(thread_node_ptr != NULL)
        {
            //pass client values to thread
            thread_node_ptr->client_socket = client_socket;
            thread_node_ptr->client_addr = client_addr;
            rc = pthread_create(&thread_node_ptr->thread, NULL, connection_thread, thread_node_ptr);
            if(rc != 0)
            {
                syslog(LOG_ERR, "Thread creating error");
                free(thread_node_ptr);
                close(client_socket);
                break;
            }
            SLIST_INSERT_HEAD(&head_node, thread_node_ptr, entries);
            
            struct ThreadNode* thread_node_ = SLIST_FIRST(&head_node);
            struct ThreadNode *next = NULL;

            //try to clean completed threads after adding a new thread and before waiting for new connection
            int thread_completed;
            while(thread_node_ != NULL)
            {
                next = SLIST_NEXT(thread_node_, entries);

                thread_completed = pthread_tryjoin_np(thread_node_->thread, NULL);         //non blocking call, as accept() will block, can't wait for join()
                if(thread_completed == 0)
                {
                    SLIST_REMOVE(&head_node, thread_node_, ThreadNode, entries);
                    free(thread_node_);
                }
                thread_node_ = next;
            }
        }

        else
        {
            syslog(LOG_ERR, "Malloc failed\n");
            perror("Malloc failed");
            close(client_socket);
            break;
        }
    }

    #if !USE_AESD_CHAR_DEVICE
    timer_delete(timerid);
    #endif

    struct ThreadNode* thread_node_ = SLIST_FIRST(&head_node);
    struct ThreadNode *next = NULL;

    //wait for threads to complete
    while(thread_node_ != NULL)
    {
        next = SLIST_NEXT(thread_node_, entries);
        pthread_join(thread_node_->thread, NULL);
        SLIST_REMOVE(&head_node, thread_node_, ThreadNode, entries);
        free(thread_node_);
        thread_node_ = next;
    }

    close(my_socket);
    freeaddrinfo(serv_info);
    pthread_mutex_destroy(&file_mutex);

    #if !USE_AESD_CHAR_DEVICE
    //remove file after getting signal
    rc = remove(DEVICE_PATH);
    if(rc != 0 && errno != ENOENT)
    {
        syslog(LOG_ERR, "Error deleting file\n");
        perror("Error deleting file");
        closelog();
        exit(1);
    }
    #endif

    closelog();
    return 0;
}

