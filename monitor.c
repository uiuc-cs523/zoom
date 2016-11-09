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
#include <string.h>

#define NPAGES (128)   // The size of profiler buffer (Unit: memory page)
#define BUFD_MAX 48000 // The max number of profiled samples stored in the profiler buffer
#define MAX_FILESIZE 200

static int buf_fd = -1;
static int buf_len;

// This function opens a character device (which is pointed by a file named as fname) and performs the mmap() operation. If the operations are successful, the base address of memory mapped buffer is returned. Otherwise, a NULL pointer is returned.
void *buf_init(char *fname)
{
  unsigned int *kadr;

  if(buf_fd == -1){
    buf_len = NPAGES * getpagesize();
    if ((buf_fd=open(fname, O_RDWR|O_SYNC))<0){
        printf("file open error. %s\n", fname);
        return NULL;
    }
  }
  kadr = mmap(0, buf_len, PROT_READ|PROT_WRITE, MAP_SHARED, buf_fd, 0);
  if (kadr == MAP_FAILED){
      printf("buf file open error.\n");
      return NULL;
  }

  return kadr;
}

// This function closes the opened character device file.
void buf_exit()
{
  if(buf_fd != -1){
    close(buf_fd);
    buf_fd = -1;
  }
}

int main(int argc, char* argv[])
{
  long *buf;
  //int index = 0;
  long index = 0;
  int i;
  char raw_results[MAX_FILESIZE];
  char summary_results[MAX_FILESIZE];
  int copy_length;
  FILE *fpRaw;
  FILE *fpSummary;
  long current_time = 0;
  long sum_vm = 0;
  long sum_rss = 0;

  // Adding two new arguments for composing different files (raw and summary results)
  if(argc != 3) {
    printf("Usage:  monitor <raw results> <summary results>\n");
    return -1;
  }

  //  Copy string of first filename to raw_results array
  if(strlen(argv[1]) < MAX_FILESIZE)
     copy_length = strlen(argv[1]);
  else
    copy_length = MAX_FILESIZE;
  strncpy(raw_results,argv[1],copy_length);

  //printf("The raw results file is %s from %s\n",raw_results,argv[1]);

  // Open file for raw results
  if((fpRaw = fopen(argv[1],"w+")) == NULL) {
    printf("Could not open file %s\n",raw_results);
    return -1;
  }

  //  Copy string of second filename to summary_results array
  if(strlen(argv[2]) < MAX_FILESIZE)
     copy_length = strlen(argv[2]);
  else
    copy_length = MAX_FILESIZE;
  strncpy(summary_results,argv[2],copy_length);

  //printf("The summary results file is %s from %s\n",summary_results,argv[2]);

  // Open file for raw results
  if((fpSummary = fopen(argv[2],"w+")) == NULL) {
    fclose(fpRaw);
    printf("Could not open file %s\n",summary_results);
    return -1;
  }

  // Open the char device and mmap()
  buf = buf_init("node");
  if(!buf)
    return -1;
  
  // Read and print profiled data
  for(index=0; index<BUFD_MAX; index++)
    if(buf[index] != -1) break;


  // Print description for each column
  //printf("Time_(jiffies) Minor_faults Major_faults CPU_utilization\n");
  //printf("PID \t Minor_faults \t Major_faults \t RSS\n");
  fprintf(fpRaw,"Time \t PID \t VM \t RSS\n");
  fprintf(fpSummary,"Time \t VM \t RSS\n");

  i = 0;
  // loop over entire length of buffer and write -1 after printing out current value
  while(buf[index] != -1){

    // Check if time is diffent than current time
    if(current_time != buf[index]) {
 
      // Check for initial condition
      if(sum_rss !=0 && sum_vm != 0) {
	fprintf(fpSummary,"%ld \t\t ",current_time);
	fprintf(fpSummary,"%ld \t\t ",sum_vm);
	fprintf(fpSummary,"%ld\n",sum_rss);
      }
      current_time = buf[index];
      sum_vm = 0;
      sum_rss = 0;
    }

    // Time
    fprintf(fpRaw,"%ld \t\t ", buf[index]);
    buf[index++] = -1;
    if(index >= BUFD_MAX)
      index = 0;

    // PID
    fprintf(fpRaw,"%ld \t\t", buf[index]);
    buf[index++] = -1;
    if(index >= BUFD_MAX)
      index = 0;

    // VM
    fprintf(fpRaw,"%ld \t\t", buf[index]);
    // sum VM
    sum_vm += buf[index];
    // initialize memory
    buf[index++] = -1;
    if(index >= BUFD_MAX)
      index = 0;

    // RSS
    fprintf(fpRaw,"%ld\n", buf[index]);
    // sum VM
    sum_rss += buf[index];
    // initialize memory
    buf[index++] = -1;
    if(index >= BUFD_MAX)
      index = 0;
    i++;
  }
  fprintf(fpRaw,"read %d profiled data\n", i);

  // Close the char device
  buf_exit();
  fclose(fpRaw);
  fclose(fpSummary);
}

