# xv-6 public

> This project provided the boilerplate for the 5th assignment of the course Operating Systems .

## Running the OS : 
```
make clean
sudo apt-get install qemu
```

As per the assignment requirements, three different job schedulers were implemented. To run each of them, follow this table and run appropriate command :
 
Scheduler  | Command
------------- | -------------
Round Robin (Part of boilerplate) | `make qemu SCHEDULER=RR`
Priority Based Scheduling | `make qemu SCHEDULER=PBS`
First Come First Serve Scheduling | `make qemu SCHEDULER=FCFS`
Multilevel Feedback Queue Scheduling | `make qemu SCHEDULER=MLFQ`