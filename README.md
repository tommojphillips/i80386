# I80286

Intel 80286 CPU Interpreter written in C. This started out as an Intel 8086 CPU Interpreter. Only real-mode is implemented. There isn't much software that is built for protected mode so there is little motivation to implement it. I have moved on to the i80386.

Passes **all** I80286 hardware-generated **real mode** single step tests
  - See: [SingleStepTests/80286](https://github.com/SingleStepTests/80286) (v1_real_mode)

You can run the tests yourself using the test program 
  - See: [I80286 Test](https://github.com/tommojphillips/i80286_test)
