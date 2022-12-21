#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"
int
main(int argc,const char *argv[]){
   
     if(argc != 3 )
   {
       fprintf(2,"wrong input\n");
       exit(1);
   }
    int priority=atoi(argv[1]);
    int pid=atoi(argv[2]);
if(priority<0||priority>100) {
     fprintf(2,"Invalid: Priority should range from 0 to 100\n");
    exit(1);
}
printf("the value of setpriority has changed to %d from %d for pid %d\n",priority,set_priority(priority,pid),pid);
exit(1);
}