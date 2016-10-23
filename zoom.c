#define LINUX

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/semaphore.h>
#include <linux/vmalloc.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <linux/pid.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group_7");
MODULE_DESCRIPTION("ZOOM Project");

//#define DEBUG 1
#define MAX_STR 80
#define MAX_ENTRIES 10
// period in milliseconds
#define PERIOD 500
#define NPAGES 128
// Adding this definition for memory pressure modeling
#define RSS_THRES 1000
#define LOW_PRESSURE 0
#define MED_PRESSURE 1
#define HIGH_PRESSURE 2
#define EMERG_PRESSURE 3

// JRF:  Adding this type for memory pressure modeling
typedef struct {
  unsigned long tot_rss;
  unsigned long prev_tot_rss;
  int pressure_state;
} mem_pressure_t;

typedef struct {
  struct task_struct *zoom_task;
  unsigned int major_faults;
  unsigned int minor_faults; 
  unsigned int process_utilization;
  int pid;
  struct list_head list;
} zoom_PCB;

typedef struct {
  struct delayed_work zoom_work;
  //struct work_struct zoom_work;
  //unsigned int pid;
} zoom_work_t;

// Adding this variable for cache assignment
static struct kmem_cache *zoom_list_struct_cachep;

//static int counter = 0;

struct proc_dir_entry *zoom_proc_dir_entry;
struct proc_dir_entry *zoom_proc_file;
// TODO:  Change task list to a linked list
// Note:  Head element is a stack allocation but all other entries are heap
static zoom_PCB zoom_task_list;
static int current_num_tasks;

// The spin lock for the task list
//spinlock_t zoom_lock;
static DEFINE_SEMAPHORE(zoom_lock);

// workqueue for handling bottom half work
static struct workqueue_struct *zoom_wq;
zoom_work_t *zoom_work;

// Pointer for memory reference
static void *memBuf;

// Structure for character device
static struct cdev *node_dev;
dev_t dev_no,dev;
static int Major;

// JRF:  For memory pressure modeling
static mem_pressure_t mem_press;

static int zoom_show(struct seq_file *m, void *v);
static int zoom_open(struct inode *node, struct file *fp);
static ssize_t zoom_write(struct file *fp, const __user char *buffer, size_t length, loff_t *offset);
static int get_token_from_proc(char *input_string, int length, int *offset);
static int perform_register(unsigned int pid);
static int perform_deregister(unsigned int pid);
static int PID_already_registered(unsigned int pid);
static void zoom_wq_function(struct work_struct *work);
// prototypes for char device driver
static int dev_open(struct inode *node, struct file *fp);
static int dev_release(struct inode *node, struct file *fp);
static int dev_mmap(struct file *fp, struct vm_area_struct *v);
static int mmap_vmem(struct file *filp, struct vm_area_struct *vma);
struct task_struct* find_task_by_pid(unsigned int nr);
int get_mem_stats(int pid, unsigned long *min_flt, unsigned long *maj_flt, unsigned long *rss, unsigned long *hiwater);
// JRF:  Adding this for memory pressure
static void calc_mem_pressure(mem_pressure_t *mem_press, unsigned long rss);
static void check_mem_pressure(mem_pressure_t *mem_press, unsigned int pid);

static const struct file_operations zoom_file_ops = {  
  .owner = THIS_MODULE,
  .open = zoom_open,
  .read = seq_read,
  .write = zoom_write,
};

static const struct file_operations dev_file_ops = {  
  .owner = THIS_MODULE,
  .open = dev_open,
  .release = dev_release,
  .mmap = dev_mmap,
};


// zoom_init - Called when module is loaded
int __init zoom_init(void)
{
  //static int page_size;
  unsigned long page_alloc;
  int ret;

   #ifdef DEBUG
   printk(KERN_ALERT "ZOOM MODULE LOADING\n");
   #endif
   // Insert your code here ...
   //printk(KERN_ALERT "Hello world\n");

   // Initialize the pointers for the list
   INIT_LIST_HEAD(&zoom_task_list.list);

   // Initialize other various pieces of task list
   zoom_task_list.pid = -1;

   // create proc directory
   zoom_proc_dir_entry = proc_mkdir("zoom",NULL);

   if(zoom_proc_dir_entry == NULL) {
     printk(KERN_INFO "Could not create zoom directory\n");
     return -ENOMEM;
   }

   // create proc file here
   zoom_proc_file = proc_create("status", 0666, zoom_proc_dir_entry, &zoom_file_ops);

   if(zoom_proc_file == NULL) {
     printk(KERN_INFO "Could not create status file\n");
     remove_proc_entry("zoom",zoom_proc_dir_entry);
     return -ENOMEM;
   }

   // Initialize the cache
   zoom_list_struct_cachep = kmem_cache_create("zoom_task_list",
					      sizeof(zoom_PCB),
					      sizeof(zoom_PCB),
					      0,
					      NULL);

   // initialize these to null to let clean-up know not to free if not allocated
   //zoom_wq = NULL;
   zoom_wq = create_workqueue("zoom_queue");
   zoom_work = NULL;

   // initialize the current number of tasks to zero
   current_num_tasks = 0;

   // Allocate virtual memory here
   //page_size = PAGE_SIZE;
   //printk(KERN_ALERT "The page size is %d\n",page_size);
   page_alloc = NPAGES * PAGE_SIZE;
   memBuf = (void*)vmalloc(page_alloc);
   if(!memBuf) {
     printk(KERN_ALERT "Aaargh:  Memory could not be allocated\n");
   }

   // initialize the character device here
   node_dev = cdev_alloc();
   node_dev->ops = &dev_file_ops;
   node_dev->owner = THIS_MODULE;

   // get the device number
   ret = alloc_chrdev_region(&dev_no,0, 1, "tstdvr");
   Major = MAJOR(dev_no);
   dev = MKDEV(Major,0);

   // add the device driver
   ret = cdev_add(node_dev,dev,1);

   // JRF:  Initialize the memory pressure variable
   mem_press.tot_rss = 0;
   mem_press.prev_tot_rss = 0;
   mem_press.pressure_state = LOW_PRESSURE;   

   printk(KERN_ALERT "ZOOM MODULE LOADED\n");
   return 0;   
}

// zoom_exit - Called when module is unloaded
void __exit zoom_exit(void)
{
  zoom_PCB *tmp_task_entry;
  struct list_head *q, *pos;
  int ret = 0;

#ifdef DEBUG
  printk(KERN_ALERT "ZOOM MODULE UNLOADING\n");
#endif
  // Insert your code here ...
  printk(KERN_ALERT "Goodbye world\n");

  // remove the file entry here
  remove_proc_entry("status",zoom_proc_dir_entry);

  // remove the directory entry last
  remove_proc_entry("zoom",NULL);

  down(&zoom_lock);  

  // Cancel pending workqueue entry
  if(zoom_work != NULL) {
    ret = cancel_delayed_work((struct delayed_work*)zoom_work);
  }
  flush_workqueue(zoom_wq);
  destroy_workqueue(zoom_wq);
    //  }

  // free the zoom_work struct
  if(zoom_work != NULL)
    kfree((void*)zoom_work);
  
  // Loop over list to check 
  // TODO:  Change to handle linked list
  list_for_each_safe(pos,q,&zoom_task_list.list) {
    tmp_task_entry = list_entry(pos, zoom_PCB, list);
    list_del(pos);
    kmem_cache_free(zoom_list_struct_cachep, tmp_task_entry);
  }
  // free the cache
  kmem_cache_destroy(zoom_list_struct_cachep);
  
  up(&zoom_lock);

  // Free the virtual memory
  vfree(memBuf);

  // initialize the character device here
  cdev_del(node_dev);
  unregister_chrdev_region(dev,1);
  
  printk(KERN_ALERT "ZOOM MODULE UNLOADED\n");
}

static int zoom_show(struct seq_file *m, void *v) {
  
  struct list_head *pos;
  zoom_PCB *tmp;

  // TODO:  Implement listing of the tasks here
  // This should loop over the list of tasks and print out the process id, process state
  seq_printf(m, "The process list is as follows:\n");

  //  spin_lock(&zoom_lock);
  down(&zoom_lock);

  list_for_each(pos, &zoom_task_list.list) {

    tmp = list_entry(pos,zoom_PCB,list);
    
    seq_printf(m, "pid = %d major faults = %d minor faults %d\n",tmp->pid, tmp->major_faults, tmp->minor_faults);
  }

  //  spin_unlock(&zoom_lock);
  up(&zoom_lock);

  return 0;
}

int zoom_open(struct inode *node, struct file *fp) {
  return single_open(fp, zoom_show, NULL);
}

static ssize_t zoom_write(struct file *fp, const __user char *buffer, size_t length, loff_t *offset) {

  char input_buffer[MAX_STR];
  int pid = -1;
  int shift;
  char *send_string;
  int i_length = (int)length;
  int ret;

  send_string = input_buffer;

  if(length > (MAX_STR - 1))
    length = MAX_STR - 1;

  ret = copy_from_user(input_buffer,buffer,length);
  input_buffer[length] = '\0';
 
  switch(input_buffer[0]) {

    // The format for registration R <PID>
  case 'R':
    printk(KERN_INFO "A registration command was received\n");
    // Read the PID here
    send_string += 2;
    i_length -= 2;
    pid = get_token_from_proc(send_string,i_length,&shift);
    printk(KERN_INFO "The pid read is %d\n",pid);
    // Perform the process registration here
    // Error checking first
    if(pid != -1)
      ret = perform_register((unsigned int)pid);
    else
      ret = -1;
    // Check return value here
    if(ret == -1)
      printk(KERN_INFO "Registration failed!!\n");
    else
      printk(KERN_INFO "Registration successful!!\n");
    break;
    // The format for deregistration U <PID>
  case 'U':
    printk(KERN_INFO "A deregistration command was received\n");
    send_string += 2;
    i_length -= 2;
    pid = get_token_from_proc(send_string,i_length,&shift);
    printk(KERN_INFO "The pid read is %d\n",pid);
    ret = perform_deregister(pid);
    break;
  default:
    printk(KERN_INFO "An unknown command was received\n");
    break;
  }

  return length;
}

// This returns the PID from the input string
static int get_token_from_proc(char *input_string, int length, int *offset) {
  
  int i = 0;
  int not_at_end = 1;
  long token;
  //long long_token;
  char scratch_space[80];
  int ret;

  if(input_string[0] == ',')
    return -1;
  else
    // find the PID between the current start and the next comma or end of length
    while(not_at_end) {
      if(input_string[i] == ',' || i >= length) {
	not_at_end = 0;
      }
      else
	i = i + 1;
    }
  //printk(KERN_INFO "The value of i is %d\n",i);
  // copy the length of input string to scratch for conversion
  if(i==0)
    return -1;
  else
    memcpy(scratch_space,input_string,i);

  // Return the offset into the string for cases where a search should be done again
  *offset = i;

  // Convert the string to an int 
  scratch_space[i] = '\0';
  ret = kstrtol(scratch_space,10,&token);
  //printk(KERN_INFO "The string is %s\n",scratch_space);
  //printk(KERN_INFO "The converted value is %ld\n",token);
  if (!ret)
    return (int)token;
  else
    return -1;
  
}

static int perform_register(unsigned int pid) {

  zoom_PCB *new_task_entry;
  int ret;

  // first, check if the number of entries is exceeded
  if(current_num_tasks >= MAX_ENTRIES) {
    printk(KERN_INFO "Max number of entries exceeded:  deregister a task please\n");
    return -1;
  }

  // Check if pid is already registered
  if(PID_already_registered(pid)) {
    printk(KERN_INFO "pid %d already registered\n",pid);
    return -1;
  }

  //if(current_num_tasks == 0 && (zoom_wq != NULL)) {
  //  printk(KERN_INFO "something wrong definely happened -- zoom_wq should be NULL\n");
  //  return -1;
  //}

  // TODO:  If the current_num_task is zero, create work queue here
  // probably should just check for zoom_wq == NULL
  //  if(current_num_tasks == 0) {
  //    printk(KERN_INFO "Create work queue here\n");
  //    zoom_wq = create_workqueue("zoom_queue");
  //  }
  
  // Should only create a work structure if one does not currently exist
  // if(zoom_wq) {
    // See if the work struct currently exists or is NULL.  If NULL, allocate.
  //printk(KERN_INFO "On registration of process %d the value of zoom work is %p\n",pid,zoom_work);
  if(zoom_work == NULL) {
    //printk("Trying to allocate a work structure\n");
    zoom_work = (zoom_work_t*) kmalloc(sizeof(zoom_work_t),GFP_KERNEL);
    if(zoom_work) {
      printk("allocated the work structure\n");
      INIT_DELAYED_WORK((struct delayed_work *)zoom_work,zoom_wq_function);
      //INIT_WORK((struct work_struct *)zoom_work,zoom_wq_function);
      // JRF:  Comment out this for now
      //zoom_work->pid = pid;
      ret = queue_delayed_work(zoom_wq, (struct delayed_work *)zoom_work, msecs_to_jiffies(PERIOD));
      //ret = queue_work(zoom_wq, (struct work_struct*)zoom_work);
    }
    else {
      printk(KERN_INFO "Something happened wrong with work structure allocation\n");
      return -1;
    }
  } // end if zoom_work is NULL
    //  } // end if zoom_wq is NULL
  //else {
  //  printk(KERN_INFO "Something happened wrong with work queue allocation\n");
  //  return -1;
  //}
  
  // Add to the current list of tasks
  current_num_tasks++;

  // Allocate entry for this new task
  new_task_entry = kmem_cache_alloc(zoom_list_struct_cachep, GFP_KERNEL);
  if(!new_task_entry) {
    printk(KERN_INFO "No space available for cache allocation for pid = %d\n",pid);
    return -1;
  }

  // Create entry and assign values to the linked list
  new_task_entry->pid = pid;
  new_task_entry->major_faults = 0;
  new_task_entry->minor_faults = 0;
  new_task_entry->process_utilization = 0;

  // use the period to link the correct pcb to the task list
  new_task_entry->zoom_task = find_task_by_pid(pid);

  // Need to add this to the list
  list_add(&new_task_entry->list,&zoom_task_list.list);
  
  return 0;

}

// TODO:  Need to implement this
static int PID_already_registered(unsigned int pid) {
  return 0;
}

static int perform_deregister(unsigned int pid) {

  struct list_head *q, *pos;
  zoom_PCB *tmp;
  //int index = -1;
  int isPidFound = 0;
  int ret;

  // first check current number of tasks is not zero
  if(current_num_tasks == 0) {
    printk(KERN_INFO "Could not deregister process %d since num tasks in list is zero\n",pid);
    return -1;
  }

  // get the spinlock
  //  spin_lock(&zoom_lock);
  down(&zoom_lock);

  // Search list for pid
  list_for_each_safe(pos,q, &zoom_task_list.list) {

    tmp = list_entry(pos,zoom_PCB,list);

    // If pid matches the value to remove, remove the entry
    if(tmp->pid == pid) {
      // First remove from list
      list_del(&tmp->list);
      // Next free the memory
      kmem_cache_free(zoom_list_struct_cachep, tmp);
      // decrement process count
      current_num_tasks--;
      // pid is found
      isPidFound = 1;
    }
  }

  // release the lock
  //  spin_unlock(&zoom_lock);
  up(&zoom_lock);

  // pid not found in list
  if(isPidFound == 0) {
    printk(KERN_INFO "Process %d not found in list\n",pid);
    return 0;  
  }  

  // Clear the work_struct if no processes left
  if(current_num_tasks == 0) {
    ret = cancel_delayed_work((struct delayed_work*)zoom_work);
    flush_workqueue(zoom_wq);

    if(zoom_work != NULL) {
      kfree((void*)zoom_work);
      zoom_work = NULL;
    } // end zoom_work if
    printk(KERN_INFO "Freeing the wq since there are no more tasks in list\n");
    // Set mem pressure to low since there are no registered tasks
    mem_press.pressure_state = LOW_PRESSURE;
    return 0;
    } // end current_num_tasks if
    
  return 0;
}

// This function performs the following:
// 1.  Checks that the given pid is present in the registered list of processes
// 1a. If listed, continues with function
// 1b. If not listed, frees work structure and returns
// 2.  Since listed, it updates the fault count and utilization numbers
// 3.  It queues the work_struct for the next period
static void zoom_wq_function(struct work_struct *work) {

  //zoom_work_t *work_fun = (zoom_work_t*)work;
  //int found_in_list = 0;
  struct list_head *pos;
  zoom_PCB *tmp;
  unsigned long min_flt, maj_flt, rss, hiwater;
  int ret;
  //static int count = 0;
  unsigned long scratch;
  //unsigned long utilization;
  unsigned long *buffer;
  static int index = 0;
  unsigned int limit;
  unsigned long lpid;
  unsigned long tot_rss = 0;

  // JRF:  Comment this out for now
  //printk(KERN_INFO "entered work queue function\n");

  //return;

  buffer = (unsigned long*)memBuf;

  // get the spinlock
  //  spin_lock(&zoom_lock);
  down_trylock(&zoom_lock);

  // loop over list and check for pid in list
  list_for_each(pos, &zoom_task_list.list) {
    tmp = list_entry(pos,zoom_PCB,list);
    // Get stats for task in list
    get_mem_stats(tmp->pid, &min_flt, &maj_flt, &rss, &hiwater);
    
    // Copy time in jiffies to queue
    scratch = jiffies;
    // Compute utilization here
    //utilization = (rss + hiwater) * HZ;

    // TODO:  have index wrap-around the page size
    limit = (NPAGES * PAGE_SIZE / sizeof(unsigned long)) - 1;
    if(index >= limit)
       index = 0;
    // Copy cpu utilization to queue
    lpid = (unsigned long) tmp->pid;
    buffer[index++] = lpid;
    // Copy minor fault count to queue
    buffer[index++] = min_flt;
    // Copy major fault count to queue
    buffer[index++] = maj_flt;
    // Copy cpu utilization to queue
    buffer[index++] = rss; 
    
    // Sum up the rss here
    tot_rss += rss;

    // JRF:  Decide signal here
    check_mem_pressure(&mem_press, tmp->pid); 

    // For now just print to kernel log
#ifdef DEBUG
    printk(KERN_INFO "For process %d, min flt = %lu, maj flt = %lu, rss = %lu, hiwater = %lu\n",tmp->pid,min_flt,maj_flt,rss,hiwater); 
#endif   
  }

  // release the lock
  //  spin_unlock(&zoom_lock);
  up(&zoom_lock);
  
  // JRF:  Calculate memory pressure level here (TODO:  add it loop above when more than one process is registered)    
  calc_mem_pressure(&mem_press, tot_rss);
  //printk(KERN_INFO "The total rss value is %d\n",tot_rss);

  // place it back in queue
  //  if(counter < 200) {
  ret = queue_delayed_work(zoom_wq, (struct delayed_work *)work, msecs_to_jiffies(PERIOD));
    //    counter++;
    //  }

  return;

}

 static int dev_open(struct inode *node, struct file *fp) {
   return 0;
 }

 static int dev_release(struct inode *node, struct file *fp) {
   return 0;
 }

 static int dev_mmap(struct file *fp, struct vm_area_struct *v) {

   return mmap_vmem(fp, v);
     
 }

// helper function, mmap's the vmalloc'd area which is not physically contiguous
static int mmap_vmem(struct file *filp, struct vm_area_struct *vma)
{
  int ret;
  long length = vma->vm_end - vma->vm_start;
  unsigned long start = vma->vm_start;
  void *vmalloc_area_ptr = memBuf;
  unsigned long pfn;

  printk(KERN_INFO"mmap_vmem is invoked\n");
  //return 0;
  /* check length - do not allow larger mappings than the number of pages allocated */
  if (length > NPAGES * PAGE_SIZE)
    return -EIO;

  /* loop over all pages, map it page individually */
  while (length > 0) {
    pfn = vmalloc_to_pfn(vmalloc_area_ptr);
    if ((ret = remap_pfn_range(vma, start, pfn, PAGE_SIZE,PAGE_SHARED)) < 0) {
      return ret;
    }
    start += PAGE_SIZE;
    vmalloc_area_ptr += PAGE_SIZE;
    length -= PAGE_SIZE;
  }
  return 0;
}


struct task_struct* find_task_by_pid(unsigned int nr)
{
    struct task_struct* task = NULL;
    rcu_read_lock();
    task=pid_task(find_vpid(nr), PIDTYPE_PID);
    rcu_read_unlock();
    if (task == NULL)
        printk(KERN_INFO "find_task_by_pid: couldnt find pid %d\n", nr);
    return task;
}

// THIS FUNCTION RETURNS 0 IF THE PID IS VALID. IT ALSO RETURNS THE
// PROCESS CPU TIME IN JIFFIES AND MAJOR AND MINOR PAGE FAULT COUNTS
// SINCE THE LAST INVOCATION OF THE FUNCTION FOR THE SPECIFIED PID.
// OTHERWISE IT RETURNS -1
int get_mem_stats(int pid, unsigned long *min_flt, unsigned long *maj_flt,
         unsigned long *rss, unsigned long *hiwater)
{
        int ret = -1;
        struct task_struct* task;

	// Get the read lock
        rcu_read_lock();
	// get the task structure
        task=find_task_by_pid(pid);
	// test if the task is available
        if (task!=NULL) {
	  *min_flt=task->min_flt;
	  *maj_flt=task->maj_flt;
	  // Rather than uptime, get the rss value
	  *rss = get_mm_rss(task->mm);
	  *hiwater = get_mm_hiwater_rss(task->mm);
	  // Reset the number of page faults
	  task->maj_flt = 0;
	  task->min_flt = 0;
	  ret = 0;
        }
	// hand back the read lock
        rcu_read_unlock();

        return ret;
}

// JRF:  For memory pressure modeling.  This routine updates the memory pressure structure
// in accordance with the rss value.
static void calc_mem_pressure(mem_pressure_t *mem_press, unsigned long rss) {

  mem_press->tot_rss = rss;

  return;

}

// JRF:  For memory pressure modeling.  This routine updates the memory pressure structure
// in accordance with the rss value.
static void check_mem_pressure(mem_pressure_t *mem_press, unsigned int pid) {
  
  int ret;
  struct siginfo si;
  struct task_struct *task;
  
  if(mem_press->tot_rss > RSS_THRES && mem_press->pressure_state == LOW_PRESSURE) {
    // Set state to medium pressure
    mem_press->pressure_state = MED_PRESSURE;
    // Get the task struct
    task = find_task_by_pid(pid);
    // send a signal to the process of medium pressure
    si.si_signo = SIGUSR1;
    si.si_code = SI_QUEUE;
    si.si_errno = 0;
    ret = send_sig_info(SIGUSR1,&si,task);
    printk(KERN_INFO "Sent signal for process %d for rss %lu",pid,mem_press->tot_rss);
    if(ret < 0) {
      printk(KERN_INFO "Problem sending signal\n");
      return;
    }
    if(mem_press->tot_rss < RSS_THRES && mem_press->pressure_state == MED_PRESSURE) {
      mem_press->pressure_state = LOW_PRESSURE;
    }      
  }  
  return;  
}

// Register init and exit funtions
module_init(zoom_init);
module_exit(zoom_exit);
