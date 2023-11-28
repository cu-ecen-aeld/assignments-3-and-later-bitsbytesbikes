#include <arpa/inet.h>
#include <errno.h>
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
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include "queue.h"


/* If the following define is set to, one the kernel char driver 
 * module is used for storing the strings */
#define USE_AESD_CHAR_DEVICE        (1)

#define BACKLOG 10
#define BUFFER_SIZE 1024

SLIST_HEAD(slisthead,client_thread_args) client_thread_list = SLIST_HEAD_INITIALIZER(client_thread_list);

static int setup_signal_handling(void);
static int setup_timer_handling();
static void signal_handler(int signo);
static void write_package(const char *package_buffer, size_t packet_size);
static void write_response(int clientfd);
static void *handle_client(void *params);
static void timer_expired(union sigval timer_data);

static bool server_active = true;
static bool accepting_connection = false;

static int socketfd;

struct client_thread_args
{
    pthread_t          thread_id;
    int                clientfd;
    struct sockaddr_in client_addr;
    bool               thread_finish_requested;
    bool               thread_finished;
    pthread_mutex_t    *output_lock;
    char               *packet_buffer;
    size_t             packet_buffer_size;
    SLIST_ENTRY(client_thread_args) entries;
};

struct timer_data {
    pthread_mutex_t    *output_lock;	
};
    

static pthread_mutex_t output_lock;

int main(int argc, char **argv)
{
    SLIST_INIT(&client_thread_list);

    pthread_mutex_init(&output_lock, NULL);

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

    if(setup_timer_handling(&output_lock) != 0)
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

	    struct client_thread_args *thread_args = malloc(sizeof(struct client_thread_args));

        if(thread_args == NULL)
        {
                perror("Error allocating memory for thread arguments.");
                return -1;
        }
	
        SLIST_INSERT_HEAD(&client_thread_list, thread_args, entries);
        thread_args->thread_finished = false;
        thread_args->clientfd = clientfd;
        thread_args->client_addr = client_addr;
        thread_args->thread_finish_requested = false;
        thread_args->output_lock = &output_lock;
        thread_args->packet_buffer = NULL;
        thread_args->packet_buffer_size = BUFFER_SIZE;

        pthread_create(&(thread_args->thread_id), NULL, handle_client, thread_args);

        struct client_thread_args *current;
        struct client_thread_args *np_tmp;
        SLIST_FOREACH_SAFE(current, &client_thread_list, entries, np_tmp) {
            if(current->thread_finished)
	    {
	        SLIST_REMOVE(&client_thread_list, current, client_thread_args, entries);
	        pthread_join(current->thread_id, NULL);
	        free(current);
	    }
	}
    }

    #if (USE_AESD_CHAR_DEVICE == 0)
        if(unlink("/var/tmp/aesdsocketdata") != 0)
            perror("Error deleting file");
    #endif

    close(socketfd);
    return EXIT_SUCCESS;
}

static void *handle_client(void *params)
{

    
    struct client_thread_args *client = (struct client_thread_args *)params;
    int clientfd = client->clientfd;
    struct sockaddr_in client_addr = client->client_addr;
    
    client->packet_buffer = malloc(client->packet_buffer_size + sizeof(char));
    if(client->packet_buffer == NULL)
    {
        perror("Error allocating packet buffer");
        return NULL;
    }

    char recv_buffer[BUFFER_SIZE];
    char client_str[INET_ADDRSTRLEN];
    inet_ntop( AF_INET, &(client_addr.sin_addr), client_str, INET_ADDRSTRLEN );
    syslog(LOG_INFO, "Accepted connection from %s", client_str);

    size_t packet_size = 0;
    bool packet_finished = false;

    while(!client->thread_finish_requested)
    {
        int received_bytes = 0;
        received_bytes = recv(clientfd, recv_buffer, sizeof(recv_buffer), 0);
        
        if(received_bytes < 0)
        {
            //ERROR
            perror("Error after receive");
	        client->thread_finished = true;
            close(clientfd);
            break;
        }

        if(received_bytes == 0 )
        {
            packet_size = 0;
            syslog(LOG_INFO, "Closed connection from %s", client_str);
            close(clientfd);
            client->thread_finished = true;
            free(client->packet_buffer);
            client->packet_buffer = NULL;
            return NULL;
        }

        while(client->packet_buffer_size <= (packet_size + received_bytes))
        {
            client->packet_buffer_size += BUFFER_SIZE;
            if((client->packet_buffer=realloc(client->packet_buffer, client->packet_buffer_size)) == 0)
            {
                perror("Failed to allocate memory");
                exit(-1);
            }                
        }

        for(int i = 0; i < BUFFER_SIZE; i++)
        {
            client->packet_buffer[packet_size] = recv_buffer[i];
            packet_size++;
            if(recv_buffer[i] == '\n')
            {
                packet_finished = true;
                break;
            }
        }

        if(packet_finished)
        {
	        pthread_mutex_lock(client->output_lock);
            write_package(client->packet_buffer, packet_size);
            packet_finished = false;
            write_response(clientfd);
	        pthread_mutex_unlock(client->output_lock);
        }

    }

    client->thread_finished = true;
    free(client->packet_buffer);
    client->packet_buffer = NULL;
    return NULL;
}

static void write_package(const char *package_buffer, size_t packet_size)
{
    #if (USE_AESD_CHAR_DEVICE == 0)
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        int out_fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_WRONLY | O_APPEND);
    #else
        int out_fd = open("/dev/aesdchar", O_WRONLY | O_APPEND);
    #endif
    int written_bytes = 0;
    while(written_bytes < packet_size){
        written_bytes += write(out_fd, package_buffer, packet_size - written_bytes);
    }
    close(out_fd);            
    packet_size = 0;
}

static void write_response(int clientfd)
{
    #if (USE_AESD_CHAR_DEVICE == 0)
        int out_fd = open("/var/tmp/aesdsocketdata", O_RDONLY);
    #else
        int out_fd = open("/dev/aesdchar", O_RDONLY);
    #endif
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
    
static int setup_signal_handling(void)
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

static int setup_timer_handling()
{
    int res;
    timer_t timer_id = 0;
    struct sigevent sev = {0};
    struct itimerspec its = {
        .it_value.tv_sec = 10,
        .it_value.tv_nsec = 0,
        .it_interval.tv_sec = 10,
        .it_interval.tv_nsec = 0	
    };

    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = &timer_expired;

    res = timer_create(CLOCK_MONOTONIC, &sev, &timer_id);
    if(res != 0) {
         perror("Error creating timer");
	 return -1;
    }

    res = timer_settime(timer_id, 0, &its, NULL);
    if(res != 0) {
         perror("Error timer_settime");
	 return -1;
    }

    return 0;
}

static void timer_expired(union sigval timer_data) {
    
    time_t t = time(NULL);
    char output_str[200];
    struct tm *tmp;
    tmp = localtime(&t);
    int num_bytes = strftime(output_str, sizeof(output_str), "timestamp:%a, %d %b %Y %T", tmp);

    if(num_bytes > 0)
    {
	    strcat(output_str, "\n");
	    num_bytes += 1;
        pthread_mutex_lock(&output_lock);    
        mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
        int out_fd = open("/var/tmp/aesdsocketdata", O_CREAT | O_WRONLY | O_APPEND, mode);
        int written_bytes = 0;
        while(written_bytes < num_bytes){
            written_bytes += write(out_fd, &output_str[written_bytes], num_bytes - written_bytes);
        }
        close(out_fd);            
	    pthread_mutex_unlock(&output_lock);
    }	
}

static void signal_handler(int signo)
{
    switch(signo)
    {
        case SIGINT:
        case SIGTERM:
        {
            struct client_thread_args *current;
            struct client_thread_args *np_tmp;
	        SLIST_FOREACH(current, &client_thread_list, entries) {
		        current->thread_finish_requested = true;
	        }
            SLIST_FOREACH_SAFE(current, &client_thread_list, entries, np_tmp) {
               if(current->thread_finished)
	            {
	                SLIST_REMOVE(&client_thread_list, current, client_thread_args, entries);
	                pthread_join(current->thread_id, NULL);
                    if(current->packet_buffer)
                    {
                        free(current->packet_buffer);
                    }
	                free(current);
	            }
	        }
            syslog(LOG_INFO, "Caught signal, exiting\n");
            if(accepting_connection)
            {
                if(unlink("/var/tmp/aesdsocketdata") != 0)
                    perror("Error deleting file");
                close(socketfd);
                exit(EXIT_SUCCESS);
            }
            server_active = false;
	    }
        default:
            //ERROR;
            break;
    }
}
