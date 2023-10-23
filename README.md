# MPIWordCountingInC
MPI Distributed Memory Parallel Word Counting

Assignment for TSN3151 Parallel Processsing Class. Given a variable number of text files, this program would count the frequency of words in the given text files concurrently across different process using MPI. This program is programmed in C with MPI library.


## Compiling and Running
### Compile 
```mpicc MPI_multiFile_wLimit.c -o wordFreqCount -lm```

### Run
```mpirun wordFreqCount```

You can also specify the number of processes by adding `-np`, i.e `-np 4` for 4 number of processes


