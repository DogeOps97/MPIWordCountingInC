#include <stdio.h>
#include <math.h>
#include <mpi.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#define MAX_FILE_READ 50

#include "wordDict.h"

// ===============================================
// TODO :

// MPI_multiFile.c ---------------

// Possible stack crashing due to massive array being allocated on stack
// Might need to change to heap

// Cleaning ignore invalid files
// currently it will half entirely if any of the file cannot be opened

// Rename some of the function and code cleanup
// It's a fucking mess
// i.e string_split() doesn't split, it just pads (prepares the string to be split with MPI_Gather at best)

// Input sanitization
// undefined behaviour on weird inputs (im not sure how cast string to int might work internally)

// wordDict.h --------------------

// dictInsertText word splitting has significant performance penalty
// Optimize that?

// ===============================================

// string_split()

// Not really splitting the string here, just giving it similar sized buffer so
// mpi_scatter can do its job
// i.e
// on 200-size string with 4 processer
// it'll return a string with length (50 + max(x) + 1) * 4, in which x is num of padded chars
// returns the extended string
// (50 + max(x) + 1) * 4

// Given a textblock in (textblock), it will create a new string with additional padding so it splits
// nicely between (numprocs) and any words should not be split in half
//      - only checks if the word is alphabetical, might split if it uses - or ' (or other weird quotations of sorts)
// returns the size of padded string in (new_size)

// Param
// textblock - wall of text being split
// new_size - size of the returned padded string
// size - size of the given test
// numprocs - num of processors/task to divide into

char *string_split(char *textblock, int *new_size, const int size, const int numprocs)
{
    // Error checking here ==========================
    if (new_size == NULL)
    {
        fprintf(stderr, "Error: new_size is NULL");
        return NULL;
    }

    if (numprocs < 1)
    {
        fprintf(stderr, "Error: numprocs cannot be less than 1");
        return NULL;
    }

    // why though
    // honestly, it's there, but i really don't want to test this case
    if (size < numprocs)
    {
        fprintf(stderr, "Warning: string might not be padded correctly");
    }
    // =============================================

    // Get the pos of char in the buffer to split to
    int *char_buff_size = (int *)calloc(sizeof(int), numprocs + 1);
    if (char_buff_size == NULL)
    {
        fprintf(stderr, "Error : char_buff_size not initialized properly");
        return NULL;
    }

    int padded_size = size + (numprocs - (size % numprocs));
    int each_size = padded_size / numprocs;
    // printf("size - %d\n", each_size);

    // ======================================
    // TODO
    // if it's split on characters (or '), move the character buffer forward
    // until whitespace or other characters
    //
    // Add backtrack if after certain characters (like dashes) doesn't have alphabet directly proceeding it
    // ======================================

    int max_size = 0;
    for (int i = 0; i < numprocs; i++)
    {
        char_buff_size[i + 1] = each_size * (i + 1);
        while (char_buff_size[i + 1] < size && isalpha(textblock[char_buff_size[i + 1]]))
        {
            char_buff_size[i + 1]++;
        }
        if (i > 0)
        {
            if (max_size < (char_buff_size[i] - char_buff_size[i - 1] + 1))
            {
                max_size = char_buff_size[i] - char_buff_size[i - 1] + 1;
            }
        }
    }

    // for (int i = 0; i < numprocs + 1; i++)
    // {
    //     printf("%d\n", char_buff_size[i]);
    // }

    // printf("max_size - %d\n", max_size);
    *new_size = max_size * numprocs;

    // This should zero the entire block properly (if not, undefined behavior ahead)
    char *new_textblock = (char *)calloc(sizeof(char), *new_size);
    if (new_textblock == NULL)
    {
        fprintf(stderr, "Error : new_textblock not initialized properly");
        return NULL;
    }
    // copy the string to lengthened buffer
    for (int i = 0; i < numprocs; i++)
    {
        // printf("current_size - %d\n", char_buff_size[i + 1] - char_buff_size[i]);
        for (int x = 0; x < char_buff_size[i + 1] - char_buff_size[i]; x++)
        {
            new_textblock[(max_size * i) + x] = textblock[char_buff_size[i] + x];
        }
    }

    free(char_buff_size);

    // This should always be true (padded size should always be divisible by numprocs)
    // If this is not true, then it will definitely ruin the calculation somewhere down the line
    assert(max_size == (*new_size / numprocs));

    return new_textblock;
}

int main(int argc, char *argv[])
{
    int numprocs, rank, namelen;
    char processor_name[MPI_MAX_PROCESSOR_NAME];

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Get_processor_name(processor_name, &namelen);


    // Timers
    double start, end;

    // For scattering of the text file
    char buffer[MAX_WORD_LENGTH];
    char *padded_string = NULL;
    char *local_padded_string;
    int padded_size = 0, each_size = 0;

    // min and max word length / char limit
    int min_char_limit = 0;
    int max_char_limit = 0;
    // ===========================

    // For sending and recv dict
    int *packet;
    int *gathered_packets;

    // size of each packets
    int packet_size = MAX_WORD_COUNT + (MAX_WORD_COUNT * MAX_WORD_LENGTH) + 1;
    // ========================

    // I/0 - files
    FILE *infile, *outfile;
    char buf[30];
    int num_text_files = 0; // num of text files being read
    char text_files_names[MAX_FILE_READ][MAX_WORD_COUNT];
    // ========================

    // Initialize local_dict
    WordDictFreq *local_dict;
    local_dict = (WordDictFreq *)malloc(sizeof(WordDictFreq));
    if (local_dict == NULL)
    {
        fprintf(stderr, "Error: local_dict failed to initialize\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }
    initializeWordFreq(local_dict);
    // ========================

    // For rank 0, initialize combined_dict
    WordDictFreq *combined_dict;

    

    // Check if correct number of argument supplied
    if (!rank)
    {
        if (argc < 4)
        {
            fprintf(stderr, "Usage : %s <file_input_1> <file_input_2> ... <min_word_length> <max_word_length> <file_output>\n", argv[0]);
            fprintf(stderr, "Example : %s t8.shakespeare.txt 3 50 out.txt\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        for (int i = 1; i < argc - 2; i++)
        {
            strcpy(text_files_names[i - 1], argv[i]);
        }
    }

    if (!rank)
    {
        fprintf(stdout, "Enter the number of text file : ");
        fflush(stdout);
        scanf("%d", &num_text_files);

        // TODO : Input sanitization
        // Break on input other than integers

        printf("Num of text files - %d\n", num_text_files);
        if (num_text_files >= MAX_FILE_READ)
        {
            fprintf(stderr, "Error : Please enter number less than or equal to %d\n", MAX_FILE_READ);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        fprintf(stdout, "Enter min word length : ");
        fflush(stdout);
        scanf("%d", &min_char_limit);

        fprintf(stdout, "Enter max word length : ");
        fflush(stdout);
        scanf("%d", &max_char_limit);

        for (int i = 0; i < num_text_files; i++)
        {
            fprintf(stdout, "Enter the path of text file %d: ", i + 1);
            fflush(stdout);
            scanf("%s", buffer);
            strcpy(text_files_names[i], buffer);
            // printf("%s\n", text_files_names[i]);
        }

        // allocate buffer for gathered_packets later
        gathered_packets = (int *)calloc(sizeof(int), packet_size * numprocs);
        if (gathered_packets == NULL)
        {
            fprintf(stderr, "Error: gathered_packets failed to initialize");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        // Initialize combined dict here for rank 0
        combined_dict = (WordDictFreq *)malloc(sizeof(WordDictFreq));
        if (combined_dict == NULL)
        {
            fprintf(stderr, "Error: combined_dict failed to initialize\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
        initializeWordFreq(combined_dict);
    }

    // Can reduce this to only one MPI calls
    // array of int would work just fine
    MPI_Bcast(&num_text_files, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&min_char_limit, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&max_char_limit, 1, MPI_INT, 0, MPI_COMM_WORLD);
    // printf("Rank %d - num - %d\n", rank, num_text_files);

    start = MPI_Wtime();

    // READ AND COUNT for EACH PROCESS
    // Read through all the files and populate each local dict ===========
    for (int i = 0; i < num_text_files; i++)
    {
        // printf("Rank %d - Reading file %d\n", rank, i + 1);
        if (!rank)
        {
            // Reading the file ============================
            infile = fopen(text_files_names[i], "rb");
            // TODO : Figure out to cleanly ignore invalid files
            // If files cannot be opened, then other processes would be blocked
            if (infile == NULL)
            {
                fprintf(stderr, "Error : File %s not found. Halting.\n", text_files_names[i]);
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }

            fseek(infile, 0, SEEK_END);
            long fsize = ftell(infile);
            fseek(infile, 0, SEEK_SET); /* same as rewind(f); */

            char *string = malloc(fsize + 1);
            fread(string, fsize, 1, infile);
            fclose(infile);
            // ==============================================

            // Holy shit, I got too fucking retarded and have wrong error checking
            // so it forced null here
            // absolutely amazing
            padded_string = string_split(string, &padded_size, (int)fsize + 1, numprocs);
            if (padded_string == NULL)
            {
                fprintf(stderr, "Error: string_split() returns null");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }

            assert(padded_string != NULL);
            assert(padded_size != 0);

            free(string);
        }

        // Scatter the string to all process =======================
        MPI_Bcast(&padded_size, 1, MPI_INT, 0, MPI_COMM_WORLD);

        each_size = padded_size / numprocs;
        // fprintf(stdout, "Rank %d - padded_size = %d, each_size = %d\n", rank, padded_size, each_size);

        local_padded_string = (char *)calloc(sizeof(char), each_size);
        if (local_padded_string == NULL)
        {
            fprintf(stderr, "Error: Rank %d - Failed to initialize local_padded_string", rank);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        MPI_Scatter(padded_string, each_size, MPI_CHAR, local_padded_string, each_size, MPI_CHAR, 0, MPI_COMM_WORLD);

        // Insert the wall of text into local dict
        // dictInsertText(local_dict, local_padded_string, each_size);
        dictInsertText(local_dict, local_padded_string, each_size, min_char_limit, max_char_limit);

        // Free ===================================================
        free(local_padded_string);

        if (!rank)
        {
            free(padded_string);
        }
    }

    // =========================================

    // Gather =========================
    packet = dict_to_packet(local_dict);

    MPI_Gather(packet, packet_size, MPI_INT, gathered_packets, packet_size, MPI_INT, 0, MPI_COMM_WORLD);
    free(packet);
    // =================================

    // COMBINE
    // Combine all of the dictionaries for all processes
    if (!rank)
    {
        // For some reason
        // Statically allocating this cause crashes
        //
        // Code example below -----
        // WordDictFreq to_combine_dict
        // initializeWordFreq(&to_combine_dict) <- crashes entirely when this line is added
        // ------- code end
        // possible stack crashing?
        WordDictFreq *to_combine_dict = (WordDictFreq *)malloc(sizeof(WordDictFreq));
        if (to_combine_dict == NULL)
        {
            fprintf(stderr, "Error : to_combine_dict not initialized properly\n");
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }
        initializeWordFreq(to_combine_dict);

        int *individual_packet = (int *)calloc(sizeof(int), packet_size);
        for (int i = 0; i < numprocs; i++)
        {
            memcpy(individual_packet, gathered_packets + (packet_size * i), packet_size * sizeof(int));
            packet_to_dict(to_combine_dict, individual_packet);
            dictCombine(combined_dict, to_combine_dict);
        }
        // fprintf(stdout, "Rank %d - Combining", rank);

        // OUTPUT TO FILE ==============
        // Print it out to txt file
        snprintf(buf, sizeof(buf), "combined_dict.txt");
        outfile = fopen(buf, "w");
        printDictToOutput(combined_dict, outfile);
        fclose(outfile);

        fprintf(stdout, "Printed output to combined_dict.txt\n");
        // =============================

        free(individual_packet);
        free(to_combine_dict);

        end = MPI_Wtime();
        printf("CPU time used: %f seconds\n",
               ((double)(end - start)));
    }

    // Free everything -------------
    free(local_dict);

    // Rank 0 stuffs ---------------
    if (!rank)
    {
        free(combined_dict);
        free(gathered_packets);
    }

    // MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
}