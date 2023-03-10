#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) 
{
   int i;
   char *args[MAXARG];

   if(argc < 3 || (argv[1][0] < '0' || argv[1][0] > '9'))
   {
       fprintf(2, "Try : %s mask command !!\n", argv[0]);
       exit(1);
   }

   if (trace(atoi(argv[1])) < 0) 
   {
       fprintf(2, "%s: strace failed !!\n", argv[0]);
       exit(1);
   }

   for(i = 2; i < argc && i < MAXARG; i++)
   	args[i-2] = argv[i];

   exec(args[0], args);
   exit(0);
}