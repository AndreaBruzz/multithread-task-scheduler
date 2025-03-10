#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#define MAX_RETRIES 5
#define MAX_TASKS 50
#define RANDOM_EXECUTION_TIME 600
#define MAX_SERVER_THREADS 10

/*
    Receive routine: use recv to receive from socket and manage
    the fact that recv may return after having read less bytes than
    the passed buffer size
    In most cases recv will read ALL requested bytes, and the loop body
    will be executed once. This is not however guaranteed and must
    be handled by the user program. The routine returns 0 upon
    successful completion, -1 otherwise
*/
static int receive(int sd, char *retBuf, int size)
{
    int totSize = 0, currSize;
    while (totSize < size)
    {
        currSize = recv(sd, &retBuf[totSize], size - totSize, 0);
        if (currSize <= 0)
            return -1;
        totSize += currSize;
    }
    return 0;
}

/*
    Load task names from tasks.config
*/
void load_task_names(char tasks[MAX_TASKS][20], int *tasks_count)
{
    FILE *file = fopen("tasks.config", "r");
    if (!file)
    {
        perror("Failed to open tasks.config");
        exit(1);
    }

    int i = 0;
    char task_name[20];
    double C, T, D; // Read but ignored
    while (fscanf(file, "%s %lf %lf %lf", task_name, &C, &T, &D) == 4)
    {
        strcpy(tasks[i], task_name);
        i++;
        if (i >= MAX_TASKS)
            break;
    }
    *tasks_count = i;
    fclose(file);

    if (*tasks_count == 0)
    {
        printf("No valid tasks found in tasks.config!\n");
        exit(1);
    }
}

/*
    Sends a request to the server to activate/deactivate a given task
*/
void send_request(const char *hostname, int port, const char *command)
{
    int sd;
    unsigned int netLen;
    struct sockaddr_in sin;
    struct hostent *hp;

    if ((hp = gethostbyname(hostname)) == 0)
    {
        perror("gethostbyname");
        exit(1);
    }

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr_list[0]))->s_addr;
    sin.sin_port = htons(port);

    int retry_count = 0;
    int sleep_time = 2;
    while (retry_count < MAX_RETRIES)
    {
        if (connect(sd, (struct sockaddr *)&sin, sizeof(sin)) == -1)
        {
            perror("[CONNECTION]");
            retry_count++;
            printf("[CONNECTION]: Retrying in %d seconds... (Attempt %d/%d)\n", sleep_time, retry_count, MAX_RETRIES);
            sleep(sleep_time);
            sleep_time *= 2;
        }
        else
        {
            break;
        }
    }

    if (retry_count == MAX_RETRIES)
    {
        printf("[CONNECTION]: Failed to connect after %d attempts. Exiting.\n", retry_count);
        close(sd);
        exit(1);
    }

    int len = strlen(command);
    netLen = htonl(len);

    printf("[CLIENT]: %s\n", command);

    if (send(sd, &netLen, sizeof(netLen), 0) == -1 || send(sd, command, len, 0) == -1)
    {
        perror("send");
        close(sd);
        exit(1);
    }

    if (receive(sd, (char *)&netLen, sizeof(netLen)))
    {
        perror("recv");
        close(sd);
        exit(1);
    }

    len = ntohl(netLen);
    char *answer = malloc(len + 1);
    if (receive(sd, answer, len))
    {
        perror("recv");
        free(answer);
        close(sd);
        exit(1);
    }

    answer[len] = 0;
    printf("%s\n", answer);
    free(answer);
    close(sd);
}

/*
    Execute a test with 4 different routines in order to check:
        - Basic activation/deactivation tasks
        - Rejection of a task due to overload
        - Run multiple threads of the same routine and deactivate them
        - Stress rapid commands sending
        - Check max threads controlls
*/
void execute_test_routines(const char *hostname, int port)
{
    printf("\n=== Test: Correctly Schedulable Tasks ===\n");
    send_request(hostname, port, "1 taskB");
    send_request(hostname, port, "1 taskA");
    sleep(10);
    send_request(hostname, port, "0 taskA");
    send_request(hostname, port, "0 taskB");

    sleep(3);

    printf("\n=== Test: Overload Case ===\n");
    send_request(hostname, port, "1 taskD");
    send_request(hostname, port, "1 taskE");
    send_request(hostname, port, "1 taskF");
    sleep(8);
    send_request(hostname, port, "0 taskD");
    send_request(hostname, port, "0 taskE");

    sleep(3);

    printf("\n=== Test: Multiple Instances of a task ===\n");
    send_request(hostname, port, "1 taskG");
    send_request(hostname, port, "1 taskG");
    send_request(hostname, port, "1 taskH");
    sleep(10);
    send_request(hostname, port, "0 taskG");
    sleep(15);
    send_request(hostname, port, "0 taskH");

    sleep(3);

    printf("\n=== Stress Test: Rapid Activations & Deactivations ===\n");
    for (int i = 0; i < 3; i++)
    {
        send_request(hostname, port, "1 taskI");
        send_request(hostname, port, "1 taskI");
        send_request(hostname, port, "1 taskI");
        send_request(hostname, port, "0 taskA");
    }

    sleep(15);

    printf("\n=== Stress Test: Max Concurrent Tasks ===\n");
    for (int i = 0; i < MAX_SERVER_THREADS + 5; i++)
    {
        char command[50] = "1 taskI";
        printf("%s\n", command);
        send_request(hostname, port, command);
    }

    send_request(hostname, port, "0 taskI");
}

/*
    Execute a test with randomized activation/deactivation requests.
*/
void execute_random_requests(const char *hostname, int port, char tasks[MAX_TASKS][20], int task_count)
{
    printf("\n=== Test: Randomized Execution ===\n");

    srand(time(NULL));
    int elapsed_time = 0;

    while (elapsed_time < RANDOM_EXECUTION_TIME)
    {
        char command[50];
        int task_index = rand() % task_count;
        int action = rand() % 2;
        snprintf(command, 50, "%d %s", action, tasks[task_index]);

        printf("[TIME: %d sec] Sending: %s\n", elapsed_time, command);
        send_request(hostname, port, command);

        int sleep_time = (rand() % 5) + 1;
        sleep(sleep_time);
        elapsed_time += sleep_time;
    }

    printf("Random execution completed.\n");
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("Usage: %s <hostname> <port> <test_case>\n", argv[0]);
        printf("Test cases:\n");
        printf("  T - Run tests to check differens situations\n");
        printf("  R - Randomized Execution\n");
        exit(1);
    }

    char *hostname = argv[1];
    int port = atoi(argv[2]);
    char test_case = argv[3][0];

    char tasks[MAX_TASKS][20];
    int task_count = 0;
    load_task_names(tasks, &task_count);

    switch (test_case)
    {
    case 'T':
        execute_test_routines(hostname, port);
        break;

    case 'R':
        execute_random_requests(hostname, port, tasks, task_count);
        break;

    default:
        printf("Invalid test case!\n");
        printf("Test cases:\n");
        printf("  T - Run tests to check differens situations\n");
        printf("  R - Randomized Execution\n");
        exit(1);
    }

    return 0;
}
