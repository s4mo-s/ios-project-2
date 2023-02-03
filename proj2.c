#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <stdarg.h>

typedef struct arguments{
    int NO;    // Number of oxygen.
    int NH;    // Number of hydrogen.
    int TI;    // The time during which the atom of O/H waits before is queued.
    int TB;    // Maximum time necessary to form a single molecule.
} Targs;

void mutex_print(const char *format, ...);
void parse_arguments(int argc, char *argv[], Targs *args);
void oxygen(int i, Targs args);
void hydrogen(int i, Targs args);
void semaphores(void);
void shared_memory(void);
void clear_semaphores(void);
void free_memory(void);

sem_t
        *s_mutex,
        *s_output,
        *s_oxyQueue,
        *s_hydroQueue,
        *s_barrier_mutex,
        *s_barrier1,
        *s_barrier2;

int shm_action, shm_oxygen, shm_hydrogen, shm_molecule, shm_count, shm_oxygenAmount, shm_hydrogenAmount, shm_killFlag;

int *p_action, *p_oxygen, *p_hydrogen, *p_molecule, *p_count, *p_oxygenAmount, *p_hydrogenAmount, *p_killFlag;

FILE *file;

int main (int argc, char *argv[])
{
    if ((file = fopen("proj2.out","w")) == NULL) {
        fprintf(stderr, "FILE_ERR: failed opening file.\n");
        exit(1);
    }

    Targs args;
    pid_t pid_main;

    parse_arguments(argc, argv, &args);
    shared_memory();
    semaphores();

    *p_oxygenAmount = args.NO;
    *p_hydrogenAmount = args.NH;

    for (int i = 0; i < args.NO; ++i) {
        pid_main = fork();

        if (pid_main == 0) {
            oxygen(i, args);
            exit(0);
        }
        else if (pid_main < 0){
            fprintf(stderr, "FORK_ERR: failed creating child failed.\n");
            exit(0);
        }
    }

    for (int i = 0; i < args.NH; ++i) {
        pid_main = fork();

        if (pid_main == 0) {
            hydrogen(i, args);
            exit(0);
        }
        else if (pid_main < 0){
            fprintf(stderr, "FORK_ERR: failed creating child failed.\n");
            exit(0);
        }
    }

    while(wait(NULL) > 0);

    clear_semaphores();
    free_memory();
    fclose(file);

    return 0;
}

void mutex_print(const char *format, ...){
    sem_wait(s_output);
    va_list args;
    va_start(args, format);
    fprintf(file, "%d: ", *p_action);
    (*p_action)++;
    vfprintf(file, format, args);
    fflush(file);
    va_end(args);
    sem_post(s_output);
}

void parse_arguments(int argc, char *argv[], Targs *args)
{
    if(argc != 5){
        fprintf(stderr, "PARSER_ERR: bad number of arguments.\n");
        exit(1);
    }

    char *endPtr;

    args->NO = strtol(argv[1], &endPtr, 10);
    args->NH = strtol(argv[2], &endPtr, 10);
    args->TI = strtol(argv[3], &endPtr, 10);
    args->TB = strtol(argv[4], &endPtr, 10);

    if (*endPtr != 0 || (args->NO < 1) || (args->NH < 1) || !(0 <= args->TI && args->TI <= 1000) || !(0 <= args->TB && args->TB <= 1000)){
        fprintf(stderr, "PARSER_ERR: given argument is not in range.\n");
        exit(1);
    }
}

void oxygen(int i, Targs args)
{
    int id_O = i;
    id_O++;
    int start_time = ((rand() % (args.TI + 1)) * 1000);
    int creating_time = ((rand() % (args.TB + 1)) * 1000);

    mutex_print("O %d: started\n", id_O);             // START

    usleep(start_time);

    mutex_print("O %d: going to queue\n", id_O);      // QUEUE

    sem_wait(s_mutex);          // Mutex --
    (*p_oxygen)++;

    if (args.NO == 1 && args.NH == 1){
        mutex_print("O %d: not enough H\n", id_O);    // NOT ENOUGH
        sem_post(s_mutex);
        exit(0);
    }
    else if ((*p_hydrogen) >= 2){
        sem_post(s_hydroQueue); // HydroQueue +2
        sem_post(s_hydroQueue);
        (*p_hydrogen)-=2;
        sem_post(s_oxyQueue);   // OxyQueue++
        (*p_oxygen)--;
    }
    else
        sem_post(s_mutex);      // Mutex++

    sem_post(s_output);

    sem_wait(s_oxyQueue);       // OxyQueue--

    if (*p_killFlag) {
        sem_post(s_oxyQueue);
        mutex_print("O %d: not enough H\n", id_O);    // NOT ENOUGH
        exit(0);
    }

    mutex_print("O %d: creating molecule %d\n", id_O, *p_molecule);   // CREATING
    usleep(creating_time);

    // Reusable barrier solution (inspired by 'The Little Book of Semaphores')
    sem_wait(s_barrier_mutex);  // Barrier mutex--
        (*p_count)++;
        if ((*p_count) == 3){
            sem_wait(s_barrier2);
            sem_post(s_barrier1);
        }
    sem_post(s_barrier_mutex);  // Barrier mutex++

    sem_wait(s_barrier1);       // First turnstile
    sem_post(s_barrier1);

    mutex_print("O %d: molecule %d created\n", id_O, *p_molecule);    // DONE

    sem_wait(s_barrier_mutex);  // Barrier mutex--
        (*p_count)--;
        if ((*p_count) == 0) {
            (*p_molecule)++;
            sem_wait(s_barrier1);
            sem_post(s_barrier2);
        }
    sem_post(s_barrier_mutex);  // Barrier mutex++

    sem_wait(s_barrier2);       // Second turnstile
    sem_post(s_barrier2);

    sem_post(s_mutex);

    (*p_oxygenAmount)--;
}

void hydrogen(int i, Targs args)
{
    int id_H = i;
    id_H++;
    int start_time = ((rand() % (args.TI + 1)) * 1000);

    mutex_print("H %d: started\n", id_H);             // START

    usleep(start_time);

    mutex_print("H %d: going to queue\n", id_H);      // QUEUE

    sem_wait(s_mutex);          // Mutex--
    (*p_hydrogen)++;

    if (args.NO == 1 && args.NH == 1){
        mutex_print("H %d: not enough O or H\n", id_H);   // NOT ENOUGH
        sem_post(s_mutex);
        exit(0);
    }

    else if (((*p_oxygenAmount) < 1) || ((*p_hydrogenAmount) < 2))
    {
        mutex_print("H %d: not enough O or H\n", id_H);   // NOT ENOUGH
        sem_post(s_mutex);
        exit(0);
    }
    if (*p_hydrogen >= 2 && *p_oxygen >= 1) {
        sem_post(s_hydroQueue); // HydroQueue +2
        sem_post(s_hydroQueue);
        (*p_hydrogen)-=2;
        sem_post(s_oxyQueue);   // OxyQueue++
        (*p_oxygen)--;
    }
    else
        sem_post(s_mutex);      // Mutex++

    sem_post(s_output);

    sem_wait(s_hydroQueue);     // HydroQueue--

    if (*p_killFlag) {
        sem_post(s_hydroQueue);
        mutex_print("H %d: not enough O or H\n", id_H);   // NOT ENOUGH
        exit(0);
    }

    mutex_print("H %d: creating molecule %d\n", id_H, *p_molecule);   // CREATING

    // Reusable barrier solution (inspired by 'The Little Book of Semaphores')
    sem_wait(s_barrier_mutex);  // Barrier mutex--
    (*p_count)++;
    if ((*p_count) == 3){
        sem_wait(s_barrier2);
        sem_post(s_barrier1);
    }
    sem_post(s_barrier_mutex);  // Barrier mutex++

    sem_wait(s_barrier1);       // First turnstile
    sem_post(s_barrier1);

    mutex_print("H %d: molecule %d created\n", id_H, *p_molecule);    // DONE

    sem_wait(s_barrier_mutex);  // Barrier mutex--
    (*p_count)--;
    if ((*p_count) == 0) {
        (*p_molecule)++;
        sem_wait(s_barrier1);
        sem_post(s_barrier2);
    }
    sem_post(s_barrier_mutex);  // Barrier mutex++

    sem_wait(s_barrier2);       // Second turnstile
    sem_post(s_barrier2);

    (*p_hydrogenAmount)--;
    if (((*p_hydrogenAmount) < 2) || ((*p_oxygenAmount) < 1)) {
        (*p_killFlag) = 1;
        sem_post(s_hydroQueue);
        sem_post(s_oxyQueue);
    }
}

void shared_memory(void)
{
    // Returns the identifier of the shared memory segment.
    if(((shm_action = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_oxygen = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_hydrogen = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_molecule = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_oxygenAmount = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_hydrogenAmount = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1) ||
        ((shm_killFlag = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666)) == -1))
    {
        fprintf(stderr, "SHARED_MEM_ERR: failed returning the id of the shared memory segment.\n");
        exit(1);
    }

    // Attaches the shared memory segment.
    if (((p_action = (int *)shmat(shm_action, NULL, 0)) == (void *) -1) ||
        ((p_oxygen = (int *)shmat(shm_oxygen, NULL, 0)) == (void *) -1) ||
        ((p_hydrogen = (int *)shmat(shm_hydrogen, NULL, 0)) == (void *) -1) ||
        ((p_molecule = (int *)shmat(shm_molecule, NULL, 0)) == (void *) -1) ||
        ((p_count = (int *)shmat(shm_count, NULL, 0)) == (void *) -1) ||
        ((p_oxygenAmount = (int *)shmat(shm_oxygenAmount, NULL, 0)) == (void *) -1) ||
        ((p_hydrogenAmount = (int *)shmat(shm_hydrogenAmount, NULL, 0)) == (void *) -1) ||
        ((p_killFlag = (int *)shmat(shm_killFlag, NULL, 0)) == (void *) -1))
    {
        fprintf(stderr, "SHARED_MEM_ERR: failed attaching the shared memory segment.\n");
        exit(1);
    }

    // Initialization of shared variables
    *p_action = 1;
    *p_oxygen = 0;
    *p_hydrogen = 0;
    *p_molecule = 1;
    *p_count = 0;
    *p_killFlag = 0;
}

void free_memory(void)
{
    // Detaches the shared memory segment
    shmdt(p_action);
    shmdt(p_oxygen);
    shmdt(p_hydrogen);
    shmdt(p_molecule);
    shmdt(p_count);
    shmdt(p_oxygenAmount);
    shmdt(p_hydrogenAmount);
    shmdt(p_killFlag);

    // Performs the control operation on the shared memory segment
    if (((shmctl(shm_action, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_oxygen, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_hydrogen, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_molecule, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_count, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_oxygenAmount, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_hydrogenAmount, IPC_RMID, NULL)) == -1) ||
        ((shmctl(shm_killFlag, IPC_RMID, NULL)) == -1))
    {
        fprintf(stderr, "FREE_MEM_ERR: failed performing the control operation on the shared memory segment.\n");
        exit(1);
    }
}

void semaphores(void)
{
    // Creates a new mapping in the virtual address space of the calling process
    if(((s_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) ||
        ((s_output = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) ||
        ((s_oxyQueue = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) ||
        ((s_hydroQueue = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) ||
        ((s_barrier_mutex = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) ||
        ((s_barrier1 = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED) ||
        ((s_barrier2 = mmap(NULL, sizeof(sem_t), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, 0, 0)) == MAP_FAILED))
    {
        free_memory();
        fprintf(stderr, "SEMAPHORE_ERR: failed mapping a semaphore.\n");
        exit(1);
    }

    // Initialization
    if(((sem_init(s_mutex, 1, 1)) == -1) ||
        ((sem_init(s_output, 1, 1)) == -1) ||
        ((sem_init(s_oxyQueue, 1, 0)) == -1) ||
        ((sem_init(s_hydroQueue, 1, 0)) == -1) ||
        ((sem_init(s_barrier_mutex, 1, 1)) == -1) ||
        ((sem_init(s_barrier1, 1, 0)) == -1) ||
        ((sem_init(s_barrier2, 1, 1)) == -1))
    {
        free_memory();
        fprintf(stderr, "SEMAPHORE_ERR: failed initializing a semaphore.\n");
        exit(1);
    }
}

void clear_semaphores(void)
{
    // Destroy
    if((sem_destroy(s_mutex)) == -1 ||
        (sem_destroy(s_output)) == -1 ||
        ((sem_destroy(s_oxyQueue)) == -1) ||
        ((sem_destroy(s_hydroQueue)) == -1) ||
        ((sem_destroy(s_barrier_mutex)) == -1) ||
        ((sem_destroy(s_barrier1)) == -1) ||
        ((sem_destroy(s_barrier2)) == -1))
    {
        fprintf(stderr, "CLEAR_SEMAPHORE_ERR: failed clearing a semaphore.\n");
        exit(1);
    }
}
