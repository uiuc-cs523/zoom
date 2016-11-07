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
#include <string.h>
#include <time.h>

#define N_ITERATION 20
#define LOW_PRESSURE 0
#define MED_PRESSURE 1
#define HI_PRESSURE 2
#define EMER_PRESSURE 3
#define LOW_GRADIENT 0
#define HI_GRADIENT 1
#define TOP_OFFENDER 10


char *buffer[1024];
void sigusr_handler(int sig);
//struct sigaction sa;
pid_t mypid;
int initDelay[100]; // in milliseconds
int sleepBetweenIters[100]; // in milliseconds
int adminRelief[100];
int percentRecover[100];
int prime_offender;


// JRF:  Added msize_orig and pressure_level to store original mem size and current level of pressure
int msize, msize_orig;
int pressure_level;
int gradient_level;

// JRF:  Added for signal handling
static void hdl_mem_pressure_not (int sig, siginfo_t *siginfo, void *context)
{
  int pressure_state = siginfo->si_errno;
  if(pressure_state > TOP_OFFENDER) {
    prime_offender = 1; 
    pressure_state -= TOP_OFFENDER;
    printf("Process is a top offender\n");
  }
  if(pressure_state == 0) {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with low pressure\n",mypid);
#endif
    pressure_level = LOW_PRESSURE;
    gradient_level = LOW_GRADIENT;
  }
  else if(pressure_state == 1) {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with medium pressure and low gradient\n",mypid);
#endif
    pressure_level = MED_PRESSURE;
    gradient_level = LOW_GRADIENT;    
  }
  else if(pressure_state == 2) {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with medium pressure and high gradient\n",mypid);
#endif
    pressure_level = MED_PRESSURE;
    gradient_level = HI_GRADIENT; 
  }
  else if(pressure_state == 3)  {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with high pressure and low gradient\n",mypid);
#endif
    pressure_level = HI_PRESSURE;
    gradient_level = LOW_GRADIENT;
  }
  else if(pressure_state == 4)  {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with high pressure and high gradient\n",mypid);
#endif
    pressure_level = HI_PRESSURE;
    gradient_level = HI_GRADIENT;
  }
  else if(pressure_state == 5)  {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with emergency pressure and low gradient\n",mypid);
#endif
    pressure_level = EMER_PRESSURE;
    gradient_level = LOW_GRADIENT;
  }
  else {
#ifdef DEBUG
    printf("SIGUSR1 received for pid %u with emergency pressure and high gradient\n",mypid); 
#endif
    pressure_level = EMER_PRESSURE;
    gradient_level = HI_GRADIENT;
  }
}

// JRF:  Adding this function to relieve some memory usage
void relieve_memory() {

  // A first cut would be to reduce memory usage by 10%
  
  int memory_usage;
  int i;

  if(prime_offender == 1) {
    memory_usage = msize * (10 - (pressure_level + 1)) / 10;
    prime_offender = 0;
    printf("Prime offender will give til it hurts\n");
  }
  else
    memory_usage = msize * (10 - pressure_level) / 10;

  // free memory 
  for(i=memory_usage; i<msize; i++){
    free(buffer[i]);
  }
  //resize the memory amount
    msize = memory_usage;

}

// JRF:  Expand memory here due to relieved pressure
void expand_memory(int child_num) {

  int i;
  int increased_memory = (msize * (percentRecover[child_num] + 100)) / 100;

  if(increased_memory > msize_orig)
    increased_memory = msize_orig;

  // free memory 
  for(i=msize; i<increased_memory; i++){
    buffer[i] = malloc(1024*1024);
  }
  //resize the memory amount
    msize = increased_memory;
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

// This routine reads the children.properties file and loads in the properties for the children
// The children.properties file has the following line properties
// initdelay iterationDelay adminRelief, percentRecovery
int parseChildrenFile(FILE *fp, int numLines) {
  int i;
  for(i=0;i < numLines;i++) {
    if(fscanf(fp,"%d %d %d %d",&initDelay[i],&sleepBetweenIters[i],&adminRelief[i],&percentRecover[i]) == EOF)
      return -1;   
  }
  return 0;
}

void outputChildrenSettings(int numLines) {
  int i;

  printf("---------------------------------\n");
  printf("number children: %d\n",numLines);
  for(i=0;i < numLines;i++)
    if(adminRelief[i] == 1)
      printf("line %d:  delay = %d, sleep = %d, percentRecovery = %d and apply relief\n",i,initDelay[i],sleepBetweenIters[i],percentRecover[i]);
    else
      printf("line %d:  delay = %d, sleep = %d, percentRecovery = %d and without relief\n",i,initDelay[i],sleepBetweenIters[i],percentRecover[i]);     
  printf("---------------------------------\n");
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
  struct sigaction act;
  FILE *fp;
  int numLines = 0;
  struct timespec initialDelay;
  struct timespec iterationDelay;
  int seconds, milliseconds;
  struct timeval outputTime;
  struct timeval initTime;
  unsigned long initSeconds,initMicroseconds;
  

  // 
  gettimeofday(&initTime,NULL);

  // initialize the pressure level and gradient level
  pressure_level = 0;
  gradient_level = 0;

  if(argc<5){
    // JRF:  Adding new argument for number of children
    printf("usage: work <memsize in MB> <locality: R for Random or T for Temporal> <# of memory accesses per iteration> <# children>\n");
    return -1;
  }

  msize = atoi(argv[1]);
  // JRF:  Save the memory size original value
  msize_orig = msize;
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

  // Parse a children properties file if children are required
  // first, check that children are required
  if(nchildren > 0) {
    // second, open the file in the cwd named children.properties
    if((fp = fopen("children.properties","r")) == NULL) {
      fclose(fp);
      printf("Could not open children.properties\n");
      return -1;
    }
    // Read the first line of the file
    fscanf(fp,"%d",&numLines);
    // Limit the number of lines to 100
    if(numLines > 100 || numLines < 0) {
      printf("Error:  numLines in children.properties should be greater than zero and less than 101\n");
      return -1;
    }
    if(parseChildrenFile(fp, numLines)) {
      printf("Error parsing children.properties file\n");
      fclose(fp);
      return -1;
    }
  }

  // print out children.properties file here for debug
  outputChildrenSettings(numLines);

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

  // JRF:  Define signal handler for memory pressure
  memset (&act, '\0', sizeof(act));
 
  /* Use the sa_sigaction field because the handles has two additional parameters */
  act.sa_sigaction = &hdl_mem_pressure_not;
 
  /* The SA_SIGINFO flag tells sigaction() to use the sa_sigaction field, not sa_handler. */
  act.sa_flags = SA_SIGINFO;
  
  if (sigaction(SIGUSR1, &act, NULL) < 0) {
    perror ("sigaction");
    return 1;
  }

  printf("A work process starts (configuration: %d %d %d)\n", msize, locality, naccess); 

  // 1. Register itself to the zoom kernel module for profiling.
  mypid = syscall(__NR_gettid);
  sprintf(cmd, "echo 'R %u'>//proc/zoom/status", mypid);
  system(cmd);

  printf("The registration string:  %s\n",cmd);


  // 2. Set up child specific settings
  // start with initial delay
  milliseconds = initDelay[child_num] % 1000;
  seconds = initDelay[child_num] / 1000;
  initialDelay.tv_sec = seconds;
  initialDelay.tv_nsec = milliseconds * 1000000;
  printf("This is the child_num = %d and the init delay = %d seconds and %d milliseconds\n",child_num,seconds,milliseconds);
  // calculate iterative delay
  milliseconds = sleepBetweenIters[child_num] % 1000;
  seconds = sleepBetweenIters[child_num] / 1000;
  iterationDelay.tv_sec = seconds;
  iterationDelay.tv_nsec = milliseconds;  
  printf("This is the child_num = %d and the iter delay = %d seconds and %d milliseconds\n",child_num,seconds,milliseconds);
#ifdef DEBUG
  // print start time here
  gettimeofday(&outputTime,NULL);
  initSeconds = outputTime.tv_sec - initTime.tv_sec;
  initMicroseconds =  outputTime.tv_usec - initTime.tv_usec;
  printf("Presleep: current time of %lu and %lu\n",initSeconds,initMicroseconds);
#endif
  nanosleep(&initialDelay,NULL);
#ifdef DEBUG
  // print end time here
  gettimeofday(&outputTime,NULL);
  initSeconds = outputTime.tv_sec - initTime.tv_sec;
  initMicroseconds =  outputTime.tv_usec - initTime.tv_usec;
  printf("Postsleep:  current time of %lu seconds and %lu microseconds\n",initSeconds,initMicroseconds);
#endif
  // 3. Allocate memory blocks
    for(i=0; i<msize; i++){
    buffer[i] = malloc(1024*1024);
    // if allocation fails, unwind and dealloc as you go
    if(buffer[i] == NULL){
      for(i--; i>=0; i--)
        free(buffer[i]);
      printf("Out of memory error! (failed at %dMB)\n", i);
      return -1;
    }
  }





  // 4. Access allocated memory blocks using the specified access policy
  int addr = 0;
  for (k=0;k<N_ITERATION; k++){
    // JRF:  Add this here to perform iteration by iteration delay
    nanosleep(&iterationDelay,NULL);    
    // JRF:  Add this check for memory pressure relief
    if(pressure_level != 0 && adminRelief[child_num]) {
#ifdef DEBUG
      printf("Relief on its way, new msize = %d\n",msize);
#endif
      relieve_memory();
    }
    // JRF:  Add this check for memory expansion if pressure is relieved
    if(pressure_level == 0 && msize < msize_orig) {
#ifdef DEBUG
      printf("Expand a might bit, new msize = %d\n",msize);
#endif
      expand_memory(child_num);
    }
     printf("[%d] %d iteration\n", mypid, k);
     if(!locality){
       for(j=0; j<naccess; j++){
         rand_access();
       }
     }
     else{
       for(j=0; j<naccess; j++){
         //int locality = rand() % 10;
	 int locality = 0;
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
  
  // 5. Free memory blocks
  for(i=0; i<msize; i++){
    free(buffer[i]);
  }

  // 6. Unregister itself to stop the profiling
  sprintf(cmd, "echo 'U %u'>//proc/zoom/status", mypid);
  system(cmd);

  // if parent, wait for all the children to prevent zombies
  for(i=0;i<child_num;i++)
    waitpid(children[i],&status,0);
  
}

