#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>


#define NUMBER_JOBS
#define NUMBER_PRODUCE
#define NUMBER_CONSUME

#define min(X, Y) (((X) < (Y)) ? (X) : (Y))

#define QUEUE_SHMSZ   25
#define MAX_PRINTS  30
#define MIN_PRINTS   1
#define MAX_PRINT_SIZE  1000
#define MIN_PRINT_SIZE   100
#define QUEUE_SHM_START 521809
#define SEM_SHM_START   245671

void signal_handler(int handle){
  printf("Terminal signal not accepted!\n");
}

typedef struct print_job
{
  int size;
  int position;
  int is_print;
  int done_print;
  long int in_queue;
  long int out_queue;
} print_job;

typedef struct my_sem
{
  int val;
  sem_t wait, mutex;
} my_sem;

void mysem_init(my_sem* sem, int pshared, int initial){
  sem->val = initial;
  sem_init(&(sem->wait), pshared, min(1, sem->val));
  sem_init(&(sem->mutex), pshared, 1);
  return;
}

void mysem_wait(my_sem* sem){
  sem_wait(&(sem->wait));
  sem_wait(&(sem->mutex));
  sem->val -= 1;
  if(sem->val > 0)
    sem_post(&(sem->wait));
  sem_post(&(sem->mutex));
  return;
}

void mysem_post(my_sem* sem){
  sem_wait(&(sem->mutex));
  sem->val += 1;
  if(sem->val == 1)
    sem_post(&(sem->wait));
  sem_post(&(sem->mutex));
  return;
}

int mysem_getvalue(my_sem* sem, int* value){
  *value = sem->val;
  return 1;
}

typedef struct sem_stuff
{
  my_sem mutex;
  my_sem full;
  my_sem empty;
  int total_jobs;
  long int total_wait;
  int print_flag;
  int sjf_flag;
}sem_stuff;

print_job * attachQueueSHM(void);
sem_stuff * attachSemSHM(void);
void print_queue(void);
void * count(void *);
static int produce_create(int);
void * consumer_create(void *);
void * consumer_create_sjf(void *);
void print(print_job *);
long currMilli(void);



int main(int argc, char *argv[])
{
  char c;
  int shmid, shmid_sem;
  key_t key;
  print_job *shm, *s;
  sem_stuff *shm_sem;
  int producers, consumers, i, pid;
  double job_input, consume_input;
  print_job *array;
  print_job *array1;


  printf("time now is: %ld\n", currMilli());

  job_input = strtod(argv[1], NULL);   // how many devices are on network
  consume_input = strtod(argv[2], NULL);  // how many printers are on network

  producers = (int) job_input;
  consumers = (int) consume_input;


  pthread_t tid1[consumers];         //thread id created for each printer



  if( (shmid_sem = shmget(SEM_SHM_START,
    (sizeof(sem_stuff)), IPC_CREAT | 0666)) < 0 )
  {
    perror("shmget");
    exit(1);
  }

  if( (shm_sem = shmat(shmid_sem, NULL, 0)) == (sem_stuff *) -1 )
  {
    perror("shmat");
    exit(1);
  }
  
  mysem_init(&shm_sem->mutex, 1, 1);
  mysem_init(&shm_sem->full, 1, 0);
  mysem_init(&shm_sem->empty, 1, QUEUE_SHMSZ);
  shm_sem->total_jobs = 0;
  shm_sem->total_wait = 0;
  shm_sem->print_flag = 0;

  key = 5678;
  if( (shmid = shmget(QUEUE_SHM_START,
    (sizeof(print_job) * 25), IPC_CREAT | 0666)) < 0 )
  {
    perror("shmget");
    exit(1);
  }

  if( (shm = shmat(shmid, NULL, 0)) == (print_job *) -1 )
   {
    perror("shmat");
    exit(1);
   }
  s = shm;

  for(i = 0; i < 25; i++)
  {
    s[i] = (print_job){0,i,0,0,0,0};
  }

  printf("INITIALIZING....\n");

  print_queue();

  s = shm;
  printf("SJF (1/0)?\n");          //prompting user for sjf algorithm or not
  scanf("%d", &shm_sem->sjf_flag);


  for (i = 0; i < producers; i++)      //creates the producers by starting a process for each one
  {
    pid = fork();
    if(pid < 0){
      perror("Fork Failed");
    }
    else if (pid == 0){
        return produce_create(i);
    }
    else{
      fprintf(stdout, "Created new process: %d\n", pid);
    }

  }
  signal(SIGINT, signal_handler);
  //wait(&sem->mutex)
  printf("consume input is: %d\n", consumers);
  for (i = 0; i < consumers; i++)          //creates consumers (printers) by starting a consumer thread for each one
  {
    if(shm_sem->sjf_flag == 1){
      if(pthread_create(&tid1[i], NULL, consumer_create_sjf, NULL))
      {
        printf("\n ERROR creating thread 1");
        exit(1);
   }
    }
    else{
      if(pthread_create(&tid1[i], NULL, consumer_create, NULL))
      {
        printf("\n ERROR creating thread 1");
        exit(1);
      }
    }
  }

  printf("done");
  while (wait(NULL) != -1);
  s = shm;

  pthread_exit(NULL);

  printf("done\n");
}

/*This process initiates the "devices" on the network, and locks them out of the queue using a built mutex lock
  which locks when the queue is full. Jobs will enter the queue once there is space in the queue again */
  
static int produce_create(int num_job)
{
  print_job * s;
  sem_stuff *shm;
  int mutex_val, full_val;
  int jobAmount, jobComplete, jobCompleteFlag = 0;
  int i = 0, j = 0, shmid;

  shm = attachSemSHM();
  srand(time(0));
  jobAmount = 1 + rand() %(MAX_PRINTS +1 - MIN_PRINTS);

  srand(time(0) + num_job);
  s = attachQueueSHM();
  mysem_getvalue(&shm->mutex, &mutex_val);

  for(i = 0; i < jobAmount; i++)
  {
    mysem_wait(&shm->empty);
    mysem_wait(&shm->mutex);
    for (j = 0; j < 25; j++)
    {
      srand(time(0)+i+j);
       if (s[j].size == 0 && (jobCompleteFlag != 1))
       {
         s[j].size = 100 + rand() %(MAX_PRINT_SIZE +1 - MIN_PRINT_SIZE);
        s[j].in_queue = currMilli();
        jobComplete++;
        if (jobComplete == jobAmount) jobCompleteFlag = 1;
        mysem_post(&shm->full);
        break;
       }
    }
    mysem_post(&shm->mutex);
  }
  return 0;
}


/* This is used to print the current contents of the queue */
void print_queue(void)
{
  print_job *s;
  int i;
  s = attachQueueSHM();

  printf("-------Actual Queue-----------\n");
  for(i = 0; i < 25; i++)
  {
    if(s[i].is_print == 1)
    {
      printf("job %d:\tsize = %d  ..printing from: %d..\n", i, s[i].size, pthread_self());
    }
    else printf("job %d:\tsize = %d\n", i, s[i].size, s[i].position);
  }
}

/* consume create sjf creates "printers" which clear jobs from the queue, prioritizing the shortest job first*/
void * consumer_create_sjf(void * a){
  print_job *s, *s_begin, tmp;
  sem_stuff *shm;
  int i, shmid, mutex_val, full_val, i_min, k, min;
  int amt_done = 0, size_temp;

  shm = attachSemSHM();
  mysem_wait(&shm->full);
  s_begin = attachQueueSHM();
  s = s_begin;
  while(amt_done < 25)
  {
    amt_done = 0;
    s = s_begin;
    mysem_wait(&shm->mutex);
    for(i = 0; i < QUEUE_SHMSZ; i++)
    {
      if(s[i].size != 0)
      {
        min = s[i].size;
        i_min = i;
        break;
      }
    }

    for(i = 0; i < 25; i++){
      if(s[i].size < min && s[i].size != 0){
        min = s[i].size;
        i_min = i;
      }
    }
      if(s[i_min].size != 0){
        s[i_min].is_print = 1;
        print_queue();
        size_temp = s[i_min].size;
        s[i_min].is_print = 0;
        s[i_min].size = 0;
        s[i_min].out_queue = currMilli();
        shm->total_jobs += 1;
        shm->total_wait += (s[i].out_queue - s[i].in_queue);
        mysem_post(&shm->mutex);
        usleep(size_temp * 100);
        mysem_post(&shm->empty);
    }
    for(i = 0; i < 25; i++){
      if(s[i].size == 0) amt_done++;
    }
  }
  mysem_wait(&shm->mutex);
  if(shm->print_flag == 0)
  {
    shm->print_flag = 1;
    printf("total jobs printed: %d\n", shm->total_jobs);
    printf("total wait time: %dl ms\n", shm->total_wait);
    printf("average wait time: %dl ms\n", (shm->total_wait) / (shm->total_jobs));
  }
  mysem_post(&shm->mutex);
}

/* creates "printers to clear jobs from queue in the order which they were entered (FIFO)*/

void * consumer_create(void * a){
  print_job *s, *s_begin, tmp;
  sem_stuff *shm;
  int i, shmid, mutex_val, full_val, k, l;
  int amt_done = 0, size_temp;

  shm = attachSemSHM();
  mysem_wait(&shm->full);
  s_begin = attachQueueSHM();
  s = s_begin;
  while(amt_done != QUEUE_SHMSZ)
  {
    amt_done = 0;
    s = s_begin;
    mysem_wait(&shm->mutex);
    for(i = 0; i < QUEUE_SHMSZ; i++)
    {
      if(s[i].size == 0)
      {
        amt_done++;
        size_temp = 0;
      }
      else if(s[i].size != 0)
      {
        printf("got here9\n");
        s[i].is_print = 1;
        print_queue();
        size_temp = s[i].size;
        s[i].is_print = 0;
        s[i].size = 0;
        s[i].out_queue = currMilli();
        shm->total_jobs += 1;
        shm->total_wait += (s[i].out_queue - s[i].in_queue);
        mysem_post(&shm->mutex);
        usleep(size_temp * 100);
        mysem_post(&shm->empty);
      }
    }
  }
  mysem_wait(&shm->mutex);
  if(shm->print_flag == 0)
  {
    shm->print_flag = 1;
    printf("total jobs printed: %d\n", shm->total_jobs);
    printf("total wait time: %dl ms\n", shm->total_wait);
    printf("average wait time: %dl ms\n", (shm->total_wait) / (shm->total_jobs));
  }
  mysem_post(&shm->mutex);
}



print_job * attachQueueSHM()
{
  int shmid, i;
  key_t key;

  print_job *shm;

  if( (shmid = shmget(QUEUE_SHM_START,
    sizeof(print_job) * QUEUE_SHMSZ, 0666)) < 0 )
    {
        perror("shmget");
        exit(1);
    }

  if( (shm = shmat(shmid, NULL, 0)) == (print_job *) -1 )
    {
        perror("shmat");
        exit(1);
    }
    return shm;
}

sem_stuff * attachSemSHM()
{
  int shmid, i;
  sem_stuff *shm;

  if( (shmid = shmget(SEM_SHM_START,
    sizeof(sem_stuff), 0666)) < 0 )
    {
        perror("shmget");
        exit(1);
    }

  if( (shm = shmat(shmid, NULL, 0)) == (sem_stuff *) -1 )
    {
        perror("shmat");
        exit(1);
    }
    return shm;
}

long currMilli()
{
  struct timespec currTime;
  clock_gettime(CLOCK_REALTIME, &currTime);
  return currTime.tv_sec * 1000 + currTime.tv_nsec / 1000000;
}
