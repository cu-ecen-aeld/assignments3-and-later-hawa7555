
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

void* connection_thread(void *threadArgs)
{
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

    if(data_packet_complete == false)
    {
        syslog(LOG_ERR, "Didn't receive full packet, connection might also be closed by client\n");

        if(buffer_start != NULL)
        {
            free(buffer_start);
        }

        // freeaddrinfo(serv_info);
        // close(my_socket);
        close(client_socket);
        // closelog();
        // exit(1);
        return NULL;
    }

    if(buffer_start == NULL)
    {
        syslog(LOG_ERR, "No data to write, exiting \n");
        // freeaddrinfo(serv_info);
        // close(my_socket);
        close(client_socket);
        // closelog();
        // close(fd);
        // exit(1);
        return NULL;
    }

    //open/create file
    pthread_mutex_lock(&file_mutex);
    fd = open("/var/tmp/aesdsocketdata", O_RDWR|O_CREAT|O_APPEND, 0600);
    // pthread_mutex_unlock(&file_mutex);

    //Error creating/opening file
    if(fd == -1)
    {
        syslog(LOG_ERR, "Error creating/opening file \n");
        perror("Error creating/opening file");

        if(buffer_start != NULL) 
        {
            free(buffer_start);
        }
        
        // freeaddrinfo(serv_info);
        // close(my_socket);
        close(client_socket);
        // closelog();
        // exit(1);
        pthread_mutex_unlock(&file_mutex);
        return NULL;
    }

    // if(buffer_start == NULL)
    // {
    //     syslog(LOG_ERR, "No data to write, exiting \n");
    //     // freeaddrinfo(serv_info);
    //     // close(my_socket);
    //     //close(client_socket);
    //     // closelog();
    //     close(fd);
    //     // exit(1);
    // }

    //Writing to file
    ssize_t nr;
    // size_t str_length = strlen(buffer_start);
    // pthread_mutex_lock(&file_mutex);
    nr = write(fd, buffer_start, total_size);
    close(fd);
    pthread_mutex_unlock(&file_mutex);

    if(buffer_start != NULL) 
    {
        free(buffer_start);
    }

    //Error writing to file
    if(nr == -1)
    {
        syslog(LOG_ERR, "Error writing to file \n");
        perror("Error writing to file");
        
        // if(buffer_start != NULL) 
        // {
        //     free(buffer_start);
        // }

        // freeaddrinfo(serv_info);
        // close(my_socket);
        close(client_socket);
        // closelog();
        // close(fd);
        // exit(1);
        //pthread_mutex_unlock(&file_mutex);
        return NULL;
    }
    
    // close(fd);
    //pthread_mutex_unlock(&file_mutex);

    // if(buffer_start != NULL) 
    // {
    //     free(buffer_start);
    // }

    memset(rec_buffer, 0, sizeof(rec_buffer) );

    pthread_mutex_lock(&file_mutex);
    fd = open("/var/tmp/aesdsocketdata", O_RDONLY);
    if(fd == -1)
    {
        syslog(LOG_ERR, "Error opening file for reading\n");
        perror(NULL);
        // freeaddrinfo(serv_info);
        // close(my_socket);
        pthread_mutex_unlock(&file_mutex);
        close(client_socket);
        // closelog();
        // exit(1);
        return NULL;
    }
    //lseek(fd, 0, SEEK_SET);

    // pthread_mutex_lock(&file_mutex);
    do
    {
        // pthread_mutex_lock(&file_mutex);
        nr = read(fd, rec_buffer, sizeof(rec_buffer));
        // pthread_mutex_unlock(&file_mutex);

        if(nr == -1)
        {
            syslog(LOG_ERR, "Error reading data from file\n");
            perror("Error reading data from file");
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            // freeaddrinfo(serv_info);
            // close(my_socket);
            close(client_socket);
            // closelog();
            // exit(1);
            // pthread_mutex_lock(&file_mutex);
            return NULL;
        }

        rc = send(client_socket, rec_buffer, nr, 0);
        if(rc == -1)
        {
            syslog(LOG_ERR, "Error sending data to client\n");
            perror(NULL);
            close(fd);
            pthread_mutex_unlock(&file_mutex);
            // freeaddrinfo(serv_info);
            // close(my_socket);
            close(client_socket);
            // closelog();
            // exit(1);
            return NULL;
        }
    } while(nr > 0);

    pthread_mutex_unlock(&file_mutex);
    close(fd);
    // pthread_mutex_unlock(&file_mutex);

    rc = close(client_socket);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error closing connection from %s\n", ipstr);
        perror(NULL);
        // freeaddrinfo(serv_info);
        // // close(my_socket);
        // close(client_socket);
        // closelog();
        // exit(1);
    }
    
    syslog(LOG_DEBUG, "Closed connection from %s\n", ipstr);
    return NULL;
}

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
        // return;
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
        //exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&file_mutex);
    fd = open("/var/tmp/aesdsocketdata", O_RDWR|O_CREAT|O_APPEND, 0600);
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

SLIST_HEAD(slisthead, ThreadNode) head_node;

int main(int argc, char **argv)
{
    openlog(NULL, 0, LOG_USER);               //open log with LOG_USER facility
    remove("/var/tmp/aesdsocketdata");

    bool daemon_mode = false;

    if (argc == 2 && strcmp(argv[1], "-d") == 0)
    {
        daemon_mode = true;
    }

    struct addrinfo addr_info;
    struct addrinfo* serv_info;
    int rc;

    struct sockaddr client_addr;
    socklen_t client_addr_size = sizeof(client_addr);
    // char ipstr[INET_ADDRSTRLEN];

    //int fd = -1;

    int my_socket, client_socket;
    my_socket = socket(PF_INET, SOCK_STREAM, 0);
    if(my_socket == -1)
    {
        syslog(LOG_ERR, "Error creating socket desc \n");
        perror(NULL);
        closelog();
        exit(1);
    }

    int opt = 1;
    if(setsockopt(my_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        syslog(LOG_ERR, "Error setting socket options\n");
        perror(NULL);
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
        perror(NULL);
        //freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    rc = bind(my_socket, serv_info->ai_addr, serv_info->ai_addrlen);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error with bind \n");
        perror(NULL);
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
            syslog(LOG_ERR, "Error with forking \n");
            freeaddrinfo(serv_info);
            close(my_socket);
            closelog();
            exit(1);
        }
        
        if (pid > 0)
        {
            exit(0);
        }
        
        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        open("/dev/null", O_RDWR);
        dup(0);
        dup(0);
    }

    rc = listen(my_socket, 5);
    if(rc != 0)
    {
        syslog(LOG_ERR, "Error with listen \n");
        perror(NULL);
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action) );
    new_action.sa_handler = signal_handler;

    if( sigaction(SIGINT, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error registering for SIGINT\n");
        perror(NULL);
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    if( sigaction(SIGTERM, &new_action, NULL) != 0)
    {
        syslog(LOG_ERR, "Error registering for SIGTERM\n");
        perror(NULL);
        freeaddrinfo(serv_info);
        close(my_socket);
        closelog();
        exit(1);
    }

    SLIST_INIT(&head_node);
    pthread_mutex_init(&file_mutex, NULL);

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
    
    struct itimerspec timer_value;

    timer_value.it_value.tv_sec = 10;
    timer_value.it_value.tv_nsec = 0;
    
    timer_value.it_interval.tv_sec = 10;
    timer_value.it_interval.tv_nsec = 0;

    if (timer_settime(timerid, 0, &timer_value, NULL) != 0)
    {
        perror("timer_settime");
        exit(1);
    }

    while(!exit_flag)
    {
        client_socket = accept(my_socket, &client_addr, &client_addr_size);

        if(client_socket == -1)
        {
            if(exit_flag) break;
            syslog(LOG_ERR, "Error accepting connection \n");
            perror("Error accepting one connection");
            // freeaddrinfo(serv_info);
            // close(my_socket);
            // closelog();
            // exit(1);
            continue;
        }

        struct ThreadNode* thread_node_ptr = (struct ThreadNode*) malloc( sizeof(struct ThreadNode) );

        if(thread_node_ptr != NULL)
        {
            thread_node_ptr->client_socket = client_socket;
            thread_node_ptr->client_addr = client_addr;
            rc = pthread_create(&thread_node_ptr->thread, NULL, connection_thread, thread_node_ptr);
            if(rc != 0)
            {
                syslog(LOG_ERR, "Thread creating error");
                free(thread_node_ptr);
                close(client_socket);
                // exit(1);
                break;
            }
            SLIST_INSERT_HEAD(&head_node, thread_node_ptr, entries);
            
            struct ThreadNode* thread_node_ = SLIST_FIRST(&head_node);
            struct ThreadNode *next = NULL;

            int thread_completed;
            while(thread_node_ != NULL)
            {
                next = SLIST_NEXT(thread_node_, entries);

                thread_completed = pthread_tryjoin_np(thread_node_->thread, NULL);
                if(thread_completed == 0)
                {
                    // close(thread_node_->client_socket);
                    SLIST_REMOVE(&head_node, thread_node_, ThreadNode, entries);
                    free(thread_node_);
                }
                thread_node_ = next;
            }
        }

        else
        {
            perror("Malloc failed");
            close(client_socket);
            break;
        }
    }

    timer_delete(timerid);

    struct ThreadNode* thread_node_ = SLIST_FIRST(&head_node);
    struct ThreadNode *next = NULL;

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

    rc = remove("/var/tmp/aesdsocketdata");
    if(rc != 0 && errno != ENOENT)
    {
        syslog(LOG_ERR, "Error deleting file\n");
        perror(NULL);
        closelog();
        exit(1);
    }

    closelog();
    return 0;
}






