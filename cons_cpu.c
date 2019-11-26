#include "types.h"
#include "param.h"
#include "stat.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "user.h"

int main(int argc, char* argv[])
{
    #ifdef PBS
    int no_of_process_generate;
    if(argc != 2) no_of_process_generate = 1;
    else no_of_process_generate = atoi(argv[1]);
    double sum = 0, j = 0;

    for(int i = 0 ; i < no_of_process_generate ; i++) {
        int pid = fork();
        if(pid == 0) {
            printf(1, "Child process %d\n", getpid());
            for(j = 0 ; j < 100000000 ; j+= 0.01) 
                sum += i;
            break;
        }
        else {
            wait();
        }
    }
    exit();
    #else

    double sum = 0, i = 0;
    // for(i = 0 ; i < 4000000 ; i += 0.01) {
    for(i = 0 ; ; i += 0.01) {
        sum+= i;
    }
    #endif

    // for(;;) {

    // }
    exit();
}
