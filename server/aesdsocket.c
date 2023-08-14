#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>


#define BACKLOG 10
#define BUFFER_SIZE 1024

int setup_signal_handling(void);
static void signal_handler(int signo);
static void write_package(const char *package_buffer, size_t packet_size);
static void write_response(int clientfd);
static void handle_client(int clientfd, struct sockaddr_in client_addr);

static bool server_active = true;
static bool accepting_connection = false;

static int socketfd;
char *packet_buffer=NULL;
size_t packet_buffer_size = BUFFER_SIZE;

int main(int argc, char **argv)
{
    if(argc > 1)
    {
        for(int i = 1; i < argc; i++)
        {
            if(strcmp(argv[i], "-d") == 0)
            {
                printf("Daemon mode on\n");
                daemon(0,0);
            }
        }
    }

    if(setup_signal_handling() != 0)
    {
        return -1;
    }

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    int option = 1;
    setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if(socketfd == -1)
    {
        return -1;
    }

    struct sockaddr_in self_addr = {0};
    self_addr.sin_family = AF_INET;
    self_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    self_addr.sin_port = htons(9000);

    if(bind(socketfd, (struct sockaddr *)&self_addr, sizeof(self_addr)) < 0)
    {
        perror("Error binding to port.\n");
        return -1;
    }

    packet_buffer = malloc(packet_buffer_size + sizeof(char));
    if(packet_buffer == NULL)
    {
        return -1;
    }

    while(server_active)
    {
        struct sockaddr_in client_addr;

        if(listen(socketfd, BACKLOG) != 0)
        {
            perror("Error while listening on connection.\n");
            return -1;
        }

        socklen_t client_len = sizeof(client_addr);

        accepting_connection = true;
        int clientfd = accept(socketfd, (struct sockaddr *)&client_addr, &client_len);
        accepting_connection = false;

        if(clientfd < 0)
        {
            perror("Error accepting client.");
            return -1;
        }

        handle_client(clientfd, client_addr);
    }

    if(unlink("/var/tmp/aesdsocketdata") != 0)
        perror("Error deleting file");
    free(packet_buffer);
    close(socketfd);
    return EXIT_SUCCESS;
}

static void handle_client(int clientfd, struct sockaddr_in client_addr)
{
    char recv_buffer[BUFFER_SIZE];
    char client_str[INET_ADDRSTRLEN];
    inet_ntop( AF_INET, &(client_addr.sin_addr), client_str, INET_ADDRSTRLEN );
    syslog(LOG_INFO, "Accepted connection from %s", client_str);

    size_t packet_size = 0;
    bool packet_finished = false;

    while(true)
    {
        int received_bytes = 0;
        received_bytes = recv(clientfd, recv_buffer, sizeof(recv_buffer), 0);
        
        if(received_bytes < 0)
        {
            //ERROR
            perror("Error after receive");
            close(clientfd);
            break;
        }

        if(received_bytes == 0 )
        {
            packet_size = 0;
            syslog(LOG_INFO, "Closed connection from %s", client_str);
            close(clientfd);
            return;
        }

        while(packet_buffer_size <= (packet_size + received_bytes))
        {
            packet_buffer_size += BUFFER_SIZE;
            if((packet_buffer=realloc(packet_buffer, packet_buffer_size)) == 0)
            {
                perror("Failed to allocate memory");
                exit(-1);
            }                
        }

        for(int i = 0; i < BUFFER_SIZE; i++)
        {
            packet_buffer[packet_size] = recv_buffer[i];
            packet_size++;
            if(recv_buffer[i] == '\n')
            {
                packet_finished = true;
                break;
            }
        }

        if(packet_finished)
        {
            write_package(packet_buffer, packet_size);
            packet_finished = false;
            write_response(clientfd);
        }

    }
}

static void write_package(const char *package_buffer, size_t packet_size)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    int out_fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_WRONLY | O_APPEND, mode);
    int written_bytes = 0;
    while(written_bytes < packet_size){
        written_bytes += write(out_fd, packet_buffer, packet_size - written_bytes);
    }
    close(out_fd);            
    packet_size = 0;
}

static void write_response(int clientfd)
{
    int out_fd = open("/var/tmp/aesdsocketdata", O_RDONLY);
    char send_buffer[BUFFER_SIZE];
    int read_bytes;
    
    while((read_bytes=read(out_fd, send_buffer, BUFFER_SIZE)))
    {
        int sent_bytes = 0;
        while((read_bytes - sent_bytes) > 0){
            sent_bytes += send(clientfd, send_buffer, read_bytes, 0);
        }
    }
}
    
int setup_signal_handling(void)
{
    struct sigaction sig_action;// = {0};
    sig_action.sa_handler = signal_handler;
    sigemptyset (&sig_action.sa_mask);
    sig_action.sa_flags = 0;
    
    if(sigaction(SIGINT, &sig_action, NULL) != 0)
    {
        //ERROR
        return -1;
    }
    if(sigaction(SIGTERM, &sig_action, NULL) != 0)
    {
        //ERROR
        return -1;
    }
    return 0;
}

static void signal_handler(int signo)
{
    switch(signo)
    {
        case SIGINT:
        case SIGTERM:
            syslog(LOG_INFO, "Caught signal, exiting\n");
            if(accepting_connection)
            {
                if(unlink("/var/tmp/aesdsocketdata") != 0)
                    perror("Error deleting file");
                close(socketfd);
                free(packet_buffer);
                exit(EXIT_SUCCESS);
            }
            server_active = false;
        default:
            //ERROR;
            break;
    }
}
