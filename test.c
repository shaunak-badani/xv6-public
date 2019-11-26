#include "types.h"
#include "param.h"
#include "stat.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "user.h"

int
main(int argc, char* argv[])
{
    int pid = fork();
    if(pid == 0) {
        exec(argv[1], argv + 1);      
        exit();
    }
    int rtime;
    int wtime;
    waitx(&wtime, &rtime);
    printf(1, "Wait times and run times of current process : %d %d\n", wtime, rtime);
    exit();
}