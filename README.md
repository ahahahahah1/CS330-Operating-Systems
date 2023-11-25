# CS330-Operating-Systems
This repository contains the code for the assignments in my junior year course of Operating Systems at IIT Kanpur under the instruction of [Prof. Debadatta Mishra](https://www.cse.iitk.ac.in/users/deba/). All the assignments were completed individually. The tasks were as follows:

<table>
  <tr>
    <th><b>Assignment</b></th>
    <th><b>Task</b></th>
    <th><b>Details</b></th>
  </tr>
  <tr>
    <td rowspan="3">Assignment 1</td>
    <td>Chain of Unary Operations</td>
    <td>To perform operations based on a series provided by the user in the terminal</td>
  </tr>
  <tr>
    <td>Directory Space Usage</td>
    <td>Find the space used by a directory</td>
  </tr>
  <tr>
    <td>Dynamic memory management library</td>
    <td>Write memalloc() and memfree(), customized versions of malloc and free from standard C library</td>
  </tr>
  <tr>
    <td rowspan="3">Assignment 2</td>
    <td>Trace Buffer Support in gemos</td>
    <td>To implement a unidirectional data channel (similar to pipe) to store and retrieve data</td>
  </tr>
  <tr>
    <td>Implementing system call tracing functionality</td>
    <td>Implement functionality similar to strace</td>
  </tr>
  <tr>
    <td>Implementing function call tracing functionality</td>
    <td>Implement the functionality to maintain a track of function calls</td>
  </tr>
  <tr>
    <td rowspan="3">Assignment 3</td>
    <td>Memory Mapping Support in gemOS</td>
    <td>To implement the mmap(), munmap() and mprotect() syscalls, analogous to Unix syscalls</td>
  </tr>
  <tr>
    <td>Page Table Manipulations</td>
    <td>To follow the lazy allocation policy of physical memory and allocate physical pages on demand by manipulating the page table entries</td>
  </tr>
  <tr>
    <td>Copy-on-Write fork</td>
    <td>To implement the cfork() syscall which copies data from parent to child following the Copy-on-Write policy</td>
  </tr>
</table>



The deliverables were: 
- <i>tracer.c</i> and <i>tracer.h</i> for Assignment 2
- <i>v2p.c</i> for Assignment 3

Assignments 2 and 3 were completed on a custom teaching-OS called gemOS<sup>[[1]](https://dl.acm.org/doi/pdf/10.1145/3338698.3338887)</sup> which is built upon the hardware simulator gem5<sup>[[2]](https://www.gem5.org/)</sup> and were run and tested using a Docker Container.

#### References
[1] Debadatta Mishra. 2019. gemOS: Bridging the Gap between Architecture and Operating System in Computer System Education. In Workshop on Computer Architecture Education (WCAE’19), June 22, 2019, Phoenix, AZ, USA. ACM, New York, NY, USA, 8 pages.

[2] gem5 simulator: The gem5 simulator is a modular platform for computer-system architecture research, encompassing system-level architecture as well as processor microarchitecture. It is primarily used to evaluate new hardware designs, system software changes, and compile-time and run-time system optimizations.
