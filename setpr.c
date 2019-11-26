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
    if(argc < 2) { printf(1, "Usage : setpr <pid> <priority>\n"); exit(); }

    int pr = atoi(argv[2]);
    int process_id = atoi(argv[1]);
    printf(1, "setting priority : %d %d\n", process_id, pr);
    set_priority(process_id, pr);
    exit();
}