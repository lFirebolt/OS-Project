#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <signal.h>

// ID of all IPC to use
#define SHARED_MEMORY_ID 25
#define MESSAGE_ID 26
#define PRODUCTION_LOC_ID 27
#define CONSUMPTION_LOC_ID 28
#define PROTECT_MEM_ID 29
#define FULL_SLOTS_ID 30

#define BUFF_SIZE 20


// message to use in message sending
struct msgbuff
{
    long mtype;
    char mtext;
};


/* arg for semctl system calls. */
union Semun
{
    int val;               /* value for SETVAL */
    struct semid_ds *buf;  /* buffer for IPC_STAT & IPC_SET */
    ushort *array;         /* array for GETALL & SETALL */
    struct seminfo *__buf; /* buffer for IPC_INFO */
    void *__pad;
};


// down the semaphore
void down(int sem)
{
    struct sembuf p_op;

    p_op.sem_num = 0;
    p_op.sem_op = -1;
    p_op.sem_flg = !IPC_NOWAIT;

    if (semop(sem, &p_op, 1) == -1)
    {
        perror("Error in down()");
        exit(-1);
    }
}


// up the semaphore
void up(int sem)
{
    struct sembuf v_op;

    v_op.sem_num = 0;
    v_op.sem_op = 1;
    v_op.sem_flg = !IPC_NOWAIT;

    if (semop(sem, &v_op, 1) == -1)
    {
        perror("Error in up()");
        exit(-1);
    }
}


// get semaphore value
int get_semaphore(int semId, union Semun arg) {
    return semctl(semId, 0, GETVAL, arg);
}


// get semaphore if it exists
// creates semaphore and initializes it if it exists
int create_semaphore(int ID, int init_val) {
    int sem_id = semget(ID, 1, 0666 | IPC_CREAT | IPC_EXCL);
    union Semun semun;
    if (sem_id == -1) {
        // already exists
        sem_id = semget(ID, 1, 0666 | IPC_CREAT);
    }
    else {
        // i created it
        semun.val = init_val;
        semctl(sem_id, 0, SETVAL, semun);
    }
    return sem_id;
}


// put these variables here to use them in cleaning resources
int msgq_id, shmid, production_loc, consumption_loc, protect_mem, full_slots;
union Semun semun;


// clean resources after getting killed
void cleanResources(int sigNum) {

    // remove message queue
    msgctl(msgq_id, IPC_RMID, (struct msqid_ds *)0);
    // remove shared memory
    shmctl(shmid, IPC_RMID, (struct shmid_ds *)0);
    // remove semaphores
    semctl(production_loc, 0, IPC_RMID, semun);
    semctl(consumption_loc, 0, IPC_RMID, semun);
    semctl(protect_mem, 0, IPC_RMID, semun);
    semctl(full_slots, 0, IPC_RMID, semun);

    // exit
    exit(0);
}

int main() {

    // put sig int callback to clean resources in it
    signal(SIGINT, cleanResources);

    // variables to use
    struct msgbuff message;
    message.mtype = 8;


    // id of message queue to send and receive messages
    msgq_id = msgget(MESSAGE_ID, 0666 | IPC_CREAT);


    // id of shared memory segment
    shmid = shmget(SHARED_MEMORY_ID, BUFF_SIZE * sizeof(int), IPC_CREAT | 0644);
    int* shmaddr = shmat(shmid, 0, 0);


    // id of production location so far
    production_loc = create_semaphore(PRODUCTION_LOC_ID, 0);


    // id of consumption location so far
    consumption_loc = create_semaphore(CONSUMPTION_LOC_ID, 0);


    // id of the binary semaphore that protects access to memory
    protect_mem = create_semaphore(PROTECT_MEM_ID, 1);

    // id of semaphore that tell how many items are full in buffer
    full_slots = create_semaphore(FULL_SLOTS_ID, 0);


    // consumer keeps working forever
    while (1) {
        // if the full slots is 0 then no items to consume so wait
        // for message from producer
        if (get_semaphore(full_slots, semun) == 0) {
            while (msgrcv(msgq_id, &message, sizeof(message.mtext), 7, !IPC_NOWAIT) == -1);
        }

        down(full_slots);
        down(protect_mem);

        // get location to consume from
        int loc = get_semaphore(consumption_loc, semun);
        // get item from buffer
        int item = shmaddr[loc % BUFF_SIZE];
        // print about the item
        printf("Consumed item: %d\tat loc: %d\n", item, loc % BUFF_SIZE);

        // if full slots were at buffer size then producer was waiting
        // for message so send it to tell him that i consumed an item
        if ((get_semaphore(full_slots, semun) + 1) == BUFF_SIZE) {
            message.mtype = 8;
            msgsnd(msgq_id, &message, sizeof(message.mtext), !IPC_NOWAIT);
        }

        up(consumption_loc);
        up(protect_mem);
    }


    return 0;
}