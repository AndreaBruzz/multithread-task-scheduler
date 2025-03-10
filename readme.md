# **Dynamic Periodic Task Execution Simulation**

## **Overview**

Simulation of dynamic periodic task execution. A pre-defined set of routines (with unique
assigned name) shall be defined in advance with given processor usage, period and deadline.
Every routine shall be composed of a program loop followed by a nanosleep() call. The exact
amount of CPU time and consequently of the processor utilization can be done in advance
using the time Linux command. The execution supervisor shall listen in TCP/IP for requests
for task activation/deactivation. The received message shall specify the name of the task to
be activated. A given task can be activated multiple times, starting every time a new thread
running the selected routine. Before accepting a request for a new task, a response time
analysis shall be carried out in order to assess the schedulability of the system.

---

This project is part of the final evaluation for the Concurrent And Real Time Programming @UNIPD

---

## **Contributors**

Bruttomesso Andrea 2021933
