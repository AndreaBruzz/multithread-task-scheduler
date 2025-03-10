#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <math.h>

#define MAX_THREADS 10
#define MAX_TASKS 50 // Maximum number of tasks from tasks.config

pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
    Struct to store each task with:
        - name
        - cpu requirement C
        - period          T
        - deadline        D
*/
typedef struct
{
    char task_name[20];
    double execution_time;
    double period;
    double deadline;
} TaskConfig;

/*
    Struct for activaded tasks with their
    thread and data
*/
typedef struct
{
    char task_name[20];
    int active;
    pthread_t thread_id;
    TaskConfig config;
} Task;

TaskConfig predefined_tasks[MAX_TASKS]; // Stores tasks from tasks.config
int num_tasks = 0;                      // Actual number of tasks loaded
Task task_list[MAX_THREADS] = {0};      // Activated tasks

/* Function to get current time in milliseconds */
double get_time_ms()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Load tasks from tasks.config */
void load_task_configurations()
{
    FILE *file = fopen("tasks.config", "r");
    if (!file)
    {
        perror("[SERVER]: Failed to open tasks.config");
        exit(1);
    }

    num_tasks = 0;
    while (num_tasks < MAX_TASKS &&
           fscanf(file, "%s %lf %lf %lf",
                  predefined_tasks[num_tasks].task_name,
                  &predefined_tasks[num_tasks].execution_time,
                  &predefined_tasks[num_tasks].period,
                  &predefined_tasks[num_tasks].deadline) == 4)
    {
        num_tasks++;
    }
    fclose(file);

    printf("[SERVER]: Loaded %d tasks from tasks.config\n", num_tasks);
}

/* Simulated periodic task execution with deadline monitoring */
void *task_runner(void *arg)
{
    Task *task = (Task *)arg;
    printf("[TASK MANAGER]: Running %s (C=%.1fms, T=%.1fms, D=%.1fms) on thread %lu...\n",
           task->task_name, task->config.execution_time, task->config.period, task->config.deadline, task->thread_id);

    double next_release_time = get_time_ms();

    while (task->active)
    {
        double start_time = get_time_ms();

        // Simulate task execution
        struct timespec exec_time = {
            .tv_sec = (int)(task->config.execution_time / 1000),
            .tv_nsec = (long)((fmod(task->config.execution_time, 1000)) * 1e6)};
        nanosleep(&exec_time, NULL);

        double end_time = get_time_ms();
        double response_time = end_time - start_time;

        // Check if the task met its deadline
        if (response_time > task->config.deadline)
        {
            printf("[TASK MANAGER]: Deadline Missed for %s! (Response Time: %.2fms, Deadline: %.2fms)\n",
                   task->task_name, response_time, task->config.deadline);
        }
        else
        {
            printf("[TASK MANAGER]: %s finished execution in %.2fms at thread %lu\n",
                   task->task_name, response_time, task->thread_id);
        }

        // Wait until the next release time
        next_release_time += task->config.period;
        double sleep_time = next_release_time - get_time_ms();
        if (sleep_time > 0)
        {
            struct timespec sleep_ts = {
                .tv_sec = (int)(sleep_time / 1000),
                .tv_nsec = (long)((sleep_time - ((int)sleep_time)) * 1e6)};
            nanosleep(&sleep_ts, NULL);
        }
    }

    printf("[TASK MANAGER]: %s stopped.\n", task->task_name);
    pthread_exit(0);
}

/* Function to determine which task has higher priority based on DM principle */
int compare_tasks(const void *a, const void *b)
{
    TaskConfig *taskA = (TaskConfig *)a;
    TaskConfig *taskB = (TaskConfig *)b;
    return (taskA->deadline > taskB->deadline) - (taskA->deadline < taskB->deadline);
}

/* Response Time Analysis */
int response_time_analysis(TaskConfig *new_task)
{
    TaskConfig active_tasks[MAX_THREADS];
    int active_count = 0;

    pthread_mutex_lock(&task_mutex);
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (task_list[i].active)
        {
            active_tasks[active_count++] = task_list[i].config;
        }
    }
    pthread_mutex_unlock(&task_mutex);

    active_tasks[active_count++] = *new_task;
    qsort(active_tasks, active_count, sizeof(TaskConfig), compare_tasks);

    for (int i = 0; i < active_count; i++)
    {
        TaskConfig *task = &active_tasks[i];
        double Ri = task->execution_time;
        double prev_Ri = 0;

        while (Ri != prev_Ri)
        {
            prev_Ri = Ri;
            Ri = task->execution_time;

            for (int j = 0; j < i; j++)
            {
                Ri += ceil(prev_Ri / active_tasks[j].period) * active_tasks[j].execution_time;
            }

            if (Ri > task->deadline)
            {
                printf("[RTA] Task %s CANNOT be scheduled (Ri=%.2f, Di=%.2f)\n",
                       new_task->task_name, Ri, task->deadline);
                return 0;
            }
        }
    }

    return 1;
}

/* Activate Task if possible */
int activate_task(const char *task_name)
{
    TaskConfig *config = NULL;
    for (int i = 0; i < num_tasks; i++)
    {
        if (strcmp(predefined_tasks[i].task_name, task_name) == 0)
        {
            config = &predefined_tasks[i];
            break;
        }
    }

    if (!config)
        return -2;

    int active_tasks = 0;
    pthread_mutex_lock(&task_mutex);
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (task_list[i].active == 1)
        {
            active_tasks++;
        }
    }
    pthread_mutex_unlock(&task_mutex);

    if (active_tasks == MAX_THREADS)
        return -3;

    if (!response_time_analysis(config))
        return -1;

    pthread_mutex_lock(&task_mutex);
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (task_list[i].active == 0)
        {
            strcpy(task_list[i].task_name, task_name);
            task_list[i].active = 1;
            task_list[i].config = *config;
            pthread_create(&task_list[i].thread_id, NULL, task_runner, &task_list[i]);
            pthread_mutex_unlock(&task_mutex);
            return 1;
        }
    }
    pthread_mutex_unlock(&task_mutex);
}

/* Deactivate Task on all threads related to it */
void deactivate_task(const char *task_name)
{
    for (int i = 0; i < MAX_THREADS; i++)
    {
        if (task_list[i].active == 1 && strcmp(task_list[i].task_name, task_name) == 0)
        {
            pthread_mutex_lock(&task_mutex);
            task_list[i].active = 0;
            pthread_t thread_to_join = task_list[i].thread_id;
            pthread_mutex_unlock(&task_mutex);
            pthread_join(thread_to_join, NULL);
        }
    }
}

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

static void handleConnection(int currSd)
{
    unsigned int netLen;
    int len;
    char command[50];
    char response[256];

    for (;;)
    {
        if (receive(currSd, (char *)&netLen, sizeof(netLen)))
            break;
        len = ntohl(netLen);

        if (receive(currSd, command, len))
            break;
        command[len] = '\0';

        int action;
        char task_name[20];

        if (sscanf(command, "%d %s", &action, task_name) == 2)
        {
            if (action == 1)
            {
                int status = activate_task(task_name);
                if (status == 1)
                    snprintf(response, sizeof(response), "[SERVER]: Task %s activated", task_name);
                else if (status == -1)
                    snprintf(response, sizeof(response), "[SERVER]: Task %s cannot be scheduled (System overloaded)", task_name);
                else if (status == -2)
                    snprintf(response, sizeof(response), "[SERVER]: Task %s not found", task_name);
                else if (status == -3)
                    snprintf(response, sizeof(response), "[SERVER]: Maximum tasks reached, cannot activate %s", task_name);
            }
            else if (action == 0)
            {
                deactivate_task(task_name);
                snprintf(response, sizeof(response), "[SERVER]: Task %s deactivated", task_name);
            }
            else
            {
                snprintf(response, sizeof(response), "[SERVER]: Invalid action");
            }
        }
        else
        {
            snprintf(response, sizeof(response), "[SERVER]: Invalid command format");
        }

        len = strlen(response);
        netLen = htonl(len);
        if (send(currSd, &netLen, sizeof(netLen), 0) == -1)
            break;
        if (send(currSd, response, len, 0) == -1)
            break;
    }
    close(currSd);
}

static void *connectionHandler(void *arg)
{
    int currSock = *(int *)arg;
    free(arg);
    handleConnection(currSock);
    pthread_exit(0);
}

int main(int argc, char *argv[])
{
    int sd, *currSd;
    struct sockaddr_in sAddr, cAddr;
    socklen_t sAddrLen;
    int port;

    if (argc < 2)
    {
        printf("Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    sscanf(argv[1], "%d", &port);

    // Load tasks from tasks.config
    load_task_configurations();

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    memset(&sAddr, 0, sizeof(sAddr));
    sAddr.sin_family = AF_INET;
    sAddr.sin_addr.s_addr = INADDR_ANY;
    sAddr.sin_port = htons(port);

    if (bind(sd, (struct sockaddr *)&sAddr, sizeof(sAddr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    listen(sd, 5);

    printf("[SERVER]: Listening on port %d...\n", port);

    while (1)
    {
        int clientSock = accept(sd, NULL, NULL);
        if (clientSock == -1)
        {
            perror("[SERVER]: Failed to accept connection");
            continue;
        }

        currSd = malloc(sizeof(int));
        *currSd = clientSock;
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, connectionHandler, currSd);
        pthread_detach(thread_id);
    }

    close(sd);
    return 0;
}
