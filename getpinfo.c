#include "types.h"
#include "param.h"
#include "stat.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "user.h"

int main(int argc, char* argv[]) {
    if(argc == 1) exit();
    int pid = atoi(argv[1]);
    getpinfo(pid);
    exit();
}