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

volatile int exit_flag = 0;

void signal_handler(int signal_number)
{
    syslog(LOG_INFO, "Caught signal, exiting");
    exit_flag = 1;
}

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
    char ipstr[INET_ADDRSTRLEN];

    int fd = -1;

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
        freeaddrinfo(serv_info);
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

    while(!exit_flag)
    {
        client_socket = accept(my_socket, &client_addr, &client_addr_size);
        if(client_socket == -1)
        {
            if(exit_flag) break;
            syslog(LOG_ERR, "Error accepting connection \n");
            perror(NULL);
            freeaddrinfo(serv_info);
            close(my_socket);
            closelog();
            exit(1);
        }

        struct sockaddr_in *client_addr_in = (struct sockaddr_in *)&client_addr;

        inet_ntop(client_addr.sa_family, &client_addr_in->sin_addr, ipstr, sizeof(ipstr) );
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

        if(!data_packet_complete)
        {
            syslog(LOG_ERR, "Error in receving complete data\n");
            if(buffer_start != NULL)
            {
                free(buffer_start);
            }

            freeaddrinfo(serv_info);
            close(my_socket);
            close(client_socket);
            closelog();
            exit(1);
        }

        //open/create file
        fd = open("/var/tmp/aesdsocketdata", O_RDWR|O_CREAT|O_APPEND, 0600);

        //Error creating file
        if(fd == -1)
        {
            syslog(LOG_ERR, "Error creating/opening file \n");
            perror(NULL);

            if(buffer_start != NULL) 
            {
                free(buffer_start);
            }
            
            freeaddrinfo(serv_info);
            close(my_socket);
            close(client_socket);
            closelog();
            exit(1);
        }

        if(buffer_start == NULL)
        {
            syslog(LOG_ERR, "No data to write, exiting \n");
            freeaddrinfo(serv_info);
            close(my_socket);
            close(client_socket);
            closelog();
            close(fd);
            exit(1);
        }

        //Writing to file
        ssize_t nr;
        // size_t str_length = strlen(buffer_start);
        nr = write(fd, buffer_start, total_size);

        //Error writing to file
        if(nr == -1)
        {
            syslog(LOG_ERR, "Error writing to file \n");
            perror(NULL);
            
            if(buffer_start != NULL) 
            {
                free(buffer_start);
            }

            freeaddrinfo(serv_info);
            close(my_socket);
            close(client_socket);
            closelog();
            close(fd);
            exit(1);
        }
        
        close(fd);

        if(buffer_start != NULL) 
        {
            free(buffer_start);
        }

        memset(rec_buffer, 0, sizeof(rec_buffer) );

        fd = open("/var/tmp/aesdsocketdata", O_RDONLY);
        if(fd == -1)
        {
            syslog(LOG_ERR, "Error opening file for reading\n");
            perror(NULL);
            freeaddrinfo(serv_info);
            close(my_socket);
            close(client_socket);
            closelog();
            exit(1);
        }
        //lseek(fd, 0, SEEK_SET);

        do
        {
            nr = read(fd, rec_buffer, sizeof(rec_buffer));

            if(nr == -1)
            {
                syslog(LOG_ERR, "Error reading data from file\n");
                perror(NULL);
                close(fd);
                freeaddrinfo(serv_info);
                close(my_socket);
                close(client_socket);
                closelog();
                exit(1);
            }

            rc = send(client_socket, rec_buffer, nr, 0);
            if(rc == -1)
            {
                syslog(LOG_ERR, "Error sending data to client\n");
                perror(NULL);
                close(fd);
                freeaddrinfo(serv_info);
                close(my_socket);
                close(client_socket);
                closelog();
                exit(1);
            }
        }while(nr > 0);
        
        close(fd);
        rc = close(client_socket);
        if(rc != 0)
        {
            syslog(LOG_ERR, "Error closing connection from %s\n", ipstr);
            perror(NULL);
            freeaddrinfo(serv_info);
            close(my_socket);
            // close(client_socket);
            closelog();
            exit(1);
        }
        syslog(LOG_DEBUG, "Closed connection from %s\n", ipstr);
    }

    close(my_socket);
    freeaddrinfo(serv_info);

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





