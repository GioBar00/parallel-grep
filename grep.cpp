#include <fstream>
#include <iostream>
#include <sstream>

#include <mpi.h>

#include "grep.h"

namespace grep {

    struct num_and_line_t
    {
        unsigned line_number;
        char line[LINELENGTH];
    };

    void get_lines(std::vector<std::string> &input_string, const std::string &file_name, unsigned &line_offset) {
        // get the rank and the size of the communicator
        int rk, sz;
        MPI_Comm_rank(MPI_COMM_WORLD, &rk);
        MPI_Comm_size(MPI_COMM_WORLD, &sz);

        // print the rank and the size of the communicator
        std::cout << "Rank: " << rk << " Size: " << sz << std::endl;

        int* sendcounts = new int[sz];
        // the buffer to be used by the scatterv
        char* buffer;

        if (rk == 0) {
            std::ifstream f_stream (file_name);

            // read input file line by line and add it to the vector
            for (std::string line; std::getline (f_stream, line);) {
                line.resize(LINELENGTH);
                input_string.push_back(line);
            }
            std::cout << "Size: " << input_string.size() << std::endl;

            // create the buffer to be used by the scatterv
            char* newbuffer = new char[input_string.size() * LINELENGTH];
            for (int i = 0; i < input_string.size(); ++i) {
                //std::cout << "Size: " << input_string[i].size() << " Line: " << i << std::endl;
                for (int j = 0; j < LINELENGTH; ++j) {
                    newbuffer[i * LINELENGTH + j] = input_string[i][j];
                }
            }

            int* displs = new int[sz];
            // compute the number of characters to be received by each process and the displacements
            // the number of characters to be received by each process is a multiple of LINELENGTH
            int residual = input_string.size() % sz;
            std::cout << "Residual: " << residual << std::endl;
            int current_displ = 0;
            for (int i = 0; i < sz; ++i) {
                sendcounts[i] = input_string.size() / sz * LINELENGTH;
                if (residual > 0) {
                    sendcounts[i] += LINELENGTH;
                    --residual;
                }
                displs[i] = current_displ;
                current_displ += sendcounts[i];
                std::cout << " Sendcount: " << sendcounts[i] << " Displ: " << displs[i] << " Curr Displ: " << current_displ << std::endl;
            }

            // broadcast the number of characters to be received by each process
            MPI_Bcast(sendcounts, sz, MPI_INT, 0, MPI_COMM_WORLD);

            // scatter the lines
            buffer = new char[sendcounts[0]];
            MPI_Scatterv(newbuffer, sendcounts, displs, MPI_CHAR, buffer, sendcounts[0], MPI_CHAR, 0, MPI_COMM_WORLD);

        } else {
            std::cout << "Rank: " << rk << " Waiting broadcast" << std::endl;
            // receive the number of characters to be received by each process
            MPI_Bcast(sendcounts, sz, MPI_INT, 0, MPI_COMM_WORLD);
            std::cout << "Rank: " << rk << " Received broadcast" << std::endl;
            // print broadcast
            for (int i = 0; i < sz; ++i) {
                std::cout << "Rank: " << rk << " Sendcount: " << sendcounts[i] << std::endl;
            }
            
            // receive the lines
            buffer = new char[sendcounts[rk]];
            MPI_Scatterv(NULL, NULL, NULL, MPI_CHAR, buffer, sendcounts[rk], MPI_CHAR, 0, MPI_COMM_WORLD);
        }

        // clear the input string
        input_string.clear();

        // convert the buffer to a vector of strings
        for (int i = 0; i < sendcounts[rk] / LINELENGTH; ++i) {
            std::string line;
            for (int j = 0; j < LINELENGTH; ++j) {
                line += buffer[i * LINELENGTH + j];
            }
            input_string.push_back(line);
        }
        
        // compute the line offset
        line_offset = 0;
        for (int i = 0; i < rk; ++i) {
            line_offset += sendcounts[i] / LINELENGTH;
        }
        // print the line offset
        std::cout << "Rank: " << rk << " Line offset: " << line_offset << std::endl;
    }

    void search_string(const std::vector<std::string> & input_strings, const std::string & search_string, lines_found &lines, unsigned line_offset) {
        for (int i = 0; i < input_strings.size(); ++i) {
            if (input_strings[i].find(search_string) != std::string::npos) {
                lines.push_back(number_and_line(i + line_offset + 1, input_strings[i]));
            }
        }
        // print the lines found
        int rk;
        MPI_Comm_rank(MPI_COMM_WORLD, &rk);
        std::cout << "Rank: " << rk << " Lines found: " << lines.size() << std::endl;
        for (int i = 0; i < lines.size(); ++i) {
            std::cout << "Rank: " << rk << " Line: " << lines[i].first << " Line: " << lines[i].second << std::endl;
        }
    }

    void print_result(const lines_found & lines) {
        int rk, sz;
        MPI_Comm_rank(MPI_COMM_WORLD, &rk);
        MPI_Comm_size(MPI_COMM_WORLD, &sz);

        // get the number of lines found by all processes
        int* lines_found = new int[sz];
        int n = lines.size();
        MPI_Allgather(&n, 1, MPI_INT, lines_found, 1, MPI_INT, MPI_COMM_WORLD);

        // compute the total number of lines
        int total_lines = 0;
        for (int i = 0; i < sz; ++i) {
            total_lines += lines_found[i];
        }

        if (total_lines == 0) {
            return;
        }

        // create output string
        std::string output_string;
        for (int i = 0; i < lines.size(); ++i) {
            std::string temp;
            for (int j = 0; j < LINELENGTH && lines[i].second[j] != '\0'; ++j) {
                temp += lines[i].second[j];
            }
            output_string += std::to_string(lines[i].first) + ":" + temp + "\n";
        }

        // gather the number of characters to be received by each process
        int* recieve_counts;
        if (rk == 0) {
            recieve_counts = new int[sz];
        }
        int x = output_string.size();
        MPI_Gather(&x, 1, MPI_INT, recieve_counts, 1, MPI_INT, 0, MPI_COMM_WORLD);

        int totlen = 0;
        int* displs;
        char* totalstring;

        if (rk == 0) {
            displs = new int[sz];

            displs[0] = 0;
            totlen += recieve_counts[0]+1;

            for (int i=1; i < sz; i++) {
                totlen += recieve_counts[i] + 1;   /* plus one for space or \0 after words */
                displs[i] = displs[i-1] + recieve_counts[i-1] + 1;
            }

            /* allocate string, pre-fill with spaces and null terminator */
            totalstring = new char[totlen];        
            for (int i=0; i<totlen - 1; i++)
                totalstring[i] = ' ';
            totalstring[totlen-1] = '\0';
        }

        MPI_Gatherv(output_string.c_str(), output_string.size(), MPI_CHAR, totalstring, recieve_counts, displs, MPI_CHAR, 0, MPI_COMM_WORLD);

        if (rk == 0) {
            std::cout << totalstring;
        }
    }

/*
    void print_result(const lines_found & lines) {
        int rk, sz;
        MPI_Comm_rank(MPI_COMM_WORLD, &rk);
        MPI_Comm_size(MPI_COMM_WORLD, &sz);

        // create a new datatype
        MPI_Datatype NumLineType;
        MPI_Datatype type[2] = { MPI_UNSIGNED, MPI_CHAR };
        int blocklen[2] = { 1, LINELENGTH };
        MPI_Aint displacements[2];
        struct num_and_line_t dummy;
        MPI_Aint base_address;
        MPI_Get_address(&dummy, &base_address);
        MPI_Get_address(&dummy.line_number, &displacements[0]);
        MPI_Get_address(&dummy.line, &displacements[1]);
        displacements[0] = MPI_Aint_diff(displacements[0], base_address);
        displacements[1] = MPI_Aint_diff(displacements[1], base_address);
        MPI_Type_create_struct(3, blocklen, displacements, type, &NumLineType);
        MPI_Type_commit(&NumLineType);

        // convert the lines to an array of num_and_line_t
        num_and_line_t* lines_array = new num_and_line_t[lines.size()];
        for (int i = 0; i < lines.size(); ++i) {
            lines_array[i].line_number = lines[i].first;
            std::string line = lines[i].second;
            line.resize(LINELENGTH, '\0');
            for (int j = 0; j < LINELENGTH; ++j) {
                lines_array[i].line[j] = line[j];
            }
        }

        // print lines_array
        for (int i = 0; i < lines.size(); ++i) {
            std::cout << "Rank: " << rk << " Line number: " << lines_array[i].line_number << " Line: " << lines_array[i].line << std::endl;
        }

        // get the number of lines found by all processes
        int* lines_found = new int[sz];
        int n = lines.size();
        MPI_Allgather(&n, 1, MPI_INT, lines_found, 1, MPI_INT, MPI_COMM_WORLD);

        // print the lines found
        for (int i = 0; i < sz; ++i) {
            std::cout << "Rank: " << rk << " Lines found: " << lines_found[i] << std::endl;
        }

        // compute the total number of lines
        int total_lines = 0;
        for (int i = 0; i < sz; ++i) {
            total_lines += lines_found[i];
        }

        if (total_lines == 0) {
            return;
        }

        // gather the lines to the root process
        num_and_line_t* global_lines_array;
        int* displs = new int[sz];
        if (rk == 0) {
            // print the total number of lines
            std::cout << "Rank: " << rk << " Total lines: " << total_lines << std::endl;
            global_lines_array = new num_and_line_t[total_lines];
            for (int i = 0; i < sz; ++i) {
                displs[i] = i == 0 ? 0 : displs[i - 1] + lines_found[i - 1];
            }
            // print the displacements
            for (int i = 0; i < sz; ++i) {
                std::cout << "Rank: " << rk << " Displacement: " << displs[i] << std::endl;
            }
            MPI_Gatherv(lines_array, lines.size(), NumLineType, global_lines_array, lines_found, displs, NumLineType, 0, MPI_COMM_WORLD);
        }
        else {
            MPI_Gatherv(lines_array, lines.size(), NumLineType, NULL, NULL, NULL, NumLineType, 0, MPI_COMM_WORLD);
        }

        // print arrived here
        std::cout << "Rank: " << rk << " Arrived here" << std::endl;
        
        if (rk == 0) {
            // print the lines
            for (int i = 0; i < lines.size() * sz; ++i) {
                std::cout << global_lines_array[i].line_number << ":" << global_lines_array[i].line << std::endl;
            }
        }
        
        MPI_Type_free(&NumLineType);
    }
    */
}