#include <stdio.h>
#include <stdlib.h>
#include <modbus-tcp.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#define SERVER_PORT 10000
#define SERVER_ADDR "127.0.0.1"
#define DATA_LENGTH 256

struct list_object_s {
    char *string;                   /* 8 bytes */
    int strlen;                     /* 4 bytes */
    struct list_object_s *next;     /* 8 bytes */
};
/* list_head is initialised to NULL on application launch as it is located in 
 * the .bss. list_head must be accessed with list_lock held */
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;
static struct list_object_s *list_head;

static void add_to_list(char *input) {
    /* Allocate memory */
    struct list_object_s *last_item;
    struct list_object_s *new_item = malloc(sizeof(struct list_object_s));
    if (!new_item) {
        fprintf(stderr, "Malloc failed\n");
        exit(1);
    }

    /* Set up the object */
    new_item->string = strdup(input);
    new_item->strlen = strlen(input);
    new_item->next = NULL;

    /* list_head is shared between threads, need to lock before access */
    pthread_mutex_lock(&list_lock);

    if (list_head == NULL) {
        /* Adding the first object */
        list_head = new_item;
    } else {
        /* Adding the nth object */
        last_item = list_head;
        while (last_item->next) last_item = last_item->next;
        last_item->next = new_item;
    }

    /* Inform print_and_free that data is available */
    pthread_cond_signal(&list_data_ready);
    /* Release shared data lock */
    pthread_mutex_unlock(&list_lock);
}

static struct list_object_s *list_get_first(void) {
    struct list_object_s *first_item;

    first_item = list_head;
    list_head = list_head->next;

    return first_item;
}

static void *print_and_free(void *arg) {
    struct list_object_s *cur_object;

    printf("thread is starting\n");

    while (1) {
        /* Wait until some data is available */
        pthread_mutex_lock(&list_lock);

        while (!list_head)
            pthread_cond_wait(&list_data_ready, &list_lock);

        cur_object = list_get_first();
        /* Release lock, all further accesses are not shared */
        pthread_mutex_unlock(&list_lock);


        printf("t2: String is: %s\n", cur_object->string);
        printf("t2: String length is %i\n", cur_object->strlen);
        free(cur_object->string);
        free(cur_object);

        /* Inform list_flush that some work has been completed */
        pthread_cond_signal(&list_data_flush);
    }
}

static void list_flush(void) {
    pthread_mutex_lock(&list_lock);

    while (list_head) {
        pthread_cond_signal(&list_data_flush);
        pthread_cond_wait(&list_data_flush, &list_lock);
    }

    pthread_mutex_unlock(&list_lock);
}

static void server(void) {
    int sock, bytes;
    pthread_t print_thread;
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    char data[DATA_LENGTH];

    printf("Server starting...\n");

    /* Print out all items of linked list and free them */
    /* Fork a new thread */
    pthread_create(&print_thread, NULL, print_and_free, NULL);

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("Error opening socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr *) &server_addr,
                            sizeof(struct sockaddr_in)) < 0) {
        printf("Error binding socket\n");
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_addr_len = sizeof(client_addr);
        bytes = recvfrom(sock, data, sizeof(data), 0,
                        (struct sockaddr *) &client_addr, &client_addr_len);

        if (bytes < 0) {
            printf("Recvfrom error\n");
            exit(EXIT_FAILURE);
        }

        printf("Got data from %s\n", inet_ntoa(client_addr.sin_addr));
        add_to_list(data);
    }

    /* Block here until all objects have been printed */
    list_flush();
}

static void client(int counter_given, int counter) {
    int sock;
    struct sockaddr_in addr;
    char input[256]; /* On the stack */

    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        printf("Error opening socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    if (connect(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        printf("Error connecting to server\n");
        exit(EXIT_FAILURE);
    }

    while (scanf("%256s", input) != EOF) {
        if (send(sock, input, strlen(input) + 1, 0) < 0) {
            printf("Send failed\n");
            exit(EXIT_FAILURE);
        }

        if (counter_given) {
            counter--;
            if (!counter) break;
        }
    }
}

int main(int argc, char **argv) {
    modbus_t *ctx;
    int option_index, c, counter, counter_given = 0, run_server = 0;

    struct option long_options[] = {
        { "count",      required_argument,  0, 'c' },
        { "directive",  no_argument,        0, 'd' },
        { "server",     no_argument,        0, 's' },
        { 0 }
    };

    while (1) {
        c = getopt_long(argc, argv, "c:ds", long_options, &option_index);

        if (c == -1) break;

        switch (c) {
            case 'c':
                printf("Got count argument with value %s\n", optarg);
                counter = atoi(optarg);
                counter_given = 1;
                break;
            case 'd':
                printf("Got directive argument\n");
                break;
            case 's':
                run_server = 1;
                break;
        }
    }

    if (run_server) server();
    else client(counter_given, counter);

    return 0;
}
