#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>

#define N_ITERATION 20

char *buffer[1024];
void sigusr_handler(int sig);
struct sigaction sa;
pid_t mypid;

int msize;

// JRF:  Added for signal handling
void sigusr_handler(int sig)
{
  printf("SIGUSR1 received for pid %u\n",mypid);
}


// This function emualtes a random memory access
void rand_access()
{
  int target;
  int blk;

  target = rand() % (msize * 1024*1024);
  if(target<0)
    target *= -1;
  blk = target / 1024/1024;
  buffer[blk][target-blk*1024*1024] = '*';
}

// This function emualtes a memory access that has a temporal locality
int local_access(int addr)
{
  if(rand() < 0){
    return addr + 1;
  }
  else{
    return addr + rand() % 300;
  }
}

int main(int argc, char* argv[])
{
  char cmd[120];

  int i, j, k;
  int locality;
  int naccess;
  int nchildren;
  pid_t children[100];
  pid_t ret;
  int child_num;
  int parent;
  int status;

  if(argc<5){
    // JRF:  Adding new argument for number of children
    printf("usage: work <memsize in MB> <locality: R for Random or T for Temporal> <# of memory accesses per iteration> <# children>\n");
    return -1;
  }

  msize = atoi(argv[1]);
  if(msize>1024 || msize<1){
    printf("memsize shall be between 1 and 1024\n");
    return -1;
  }

  locality = (argv[2][0]=='R')?0:1;

  naccess = atoi(argv[3]);
  if(naccess<1){
    printf("naccess shall be >=1\n");
    return -1;
  }

  nchildren = atoi(argv[4]);
  if(nchildren < 0 || nchildren > 100) {
    printf("number children needs to be greater than zero and less than 101\n");
    return -1;
  }

  // JRF:  Loop over num children and spawn a new child each time
  child_num = 0;
  parent = 1;
  while(nchildren > 0) {
    ret = fork();
    // check for child process
    if(!ret) {
      // set as a child and break out of loop
      parent = 0;
      break;
    }
    //sleep(3);
    nchildren--;
    children[child_num] = ret;
    child_num++;
  }

  sa.sa_handler = sigusr_handler;
  sa.sa_flags = 0; // or SA_RESTART
  sigemptyset(&sa.sa_mask);

  if (sigaction(SIGUSR1, &sa, NULL) == -1) {
    perror("sigaction");
    exit(1);
  }

  printf("A work process starts (configuration: %d %d %d)\n", msize, locality, naccess); 

  // 1. Register itself to the zoom kernel module for profiling.
  mypid = syscall(__NR_gettid);
  sprintf(cmd, "echo 'R %u'>//proc/zoom/status", mypid);
  system(cmd);

  printf("The registration string:  %s\n",cmd);

  // 2. Allocate memory blocks
  for(i=0; i<msize; i++){
    buffer[i] = malloc(1024*1024);
    if(buffer[i] == NULL){
      for(i--; i>=0; i--)
        free(buffer[i]);
      printf("Out of memory error! (failed at %dMB)\n", i);
      return -1;
    }
  }

  // 3. Access allocated memory blocks using the specified access policy
  int addr = 0;
  for (k=0;k<N_ITERATION; k++){
     printf("[%d] %d iteration\n", mypid, k);
     if(!locality){
       for(j=0; j<naccess; j++){
         rand_access();
       }
     }
     else{
       for(j=0; j<naccess; j++){
         int locality = rand() % 10;
         if(locality > -2 && locality < 2){ /* random access */
           rand_access();
         }
         else{ /* local access */
           addr = local_access(addr);
	 }
       }
     }
     sleep(1);
  }
  
  // 4. Free memory blocks
  for(i=0; i<msize; i++){
    free(buffer[i]);
  }

  // 5. Unregister itself to stop the profiling
  sprintf(cmd, "echo 'U %u'>//proc/zoom/status", mypid);
  system(cmd);

  // if parent, wait for all the children to prevent zombies
  for(i=0;i<child_num;i++)
    waitpid(children[i],&status,0);
  
}

