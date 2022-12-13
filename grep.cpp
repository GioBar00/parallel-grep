#include <fstream>
#include <iostream>
#include <sstream>

#include <mpi.h>

#include "grep.h"

namespace grep {

    void get_lines(std::vector<std::string> &input_string, const std::string &file_name, unsigned &line_offset) {
        // get the rank and the size of the communicator
        int rk, sz;
        MPI_Comm_rank(MPI_COMM_WORLD, &rk);
        MPI_Comm_size(MPI_COMM_WORLD, &sz);

        // the number of characters to be received by each process
        int* sendcounts = new int[sz];
        // the buffer to be used by the scatterv
        char* buffer;

        if (rk == 0) {
            std::ifstream f_stream (file_name);

            // read input file line by line and add it to the vector
            for (std::string line; std::getline (f_stream, line);) {
                // resize the line to LINELENGTH
                line.resize(LINELENGTH);

                input_string.push_back(line);
            }

            // create the buffer to be used by the scatterv
            char* newbuffer = new char[input_string.size() * LINELENGTH];
            for (int i = 0; i < input_string.size(); ++i) {
                for (int j = 0; j < LINELENGTH; ++j) {
                    newbuffer[i * LINELENGTH + j] = input_string[i][j];
                }
            }

            int* displs = new int[sz];
            // compute the number of characters to be received by each process and the displacements
            // the number of characters to be received by each process is a multiple of LINELENGTH
            int residual = input_string.size() % sz;
            int current_displ = 0;
            for (int i = 0; i < sz; ++i) {
                sendcounts[i] = input_string.size() / sz * LINELENGTH;
                if (residual > 0) {
                    sendcounts[i] += LINELENGTH;
                    --residual;
                }
                displs[i] = current_displ;
                current_displ += sendcounts[i];
            }

            // broadcast the number of characters to be received by each process
            MPI_Bcast(sendcounts, sz, MPI_INT, 0, MPI_COMM_WORLD);

            // scatter the lines
            buffer = new char[sendcounts[0]];
            MPI_Scatterv(newbuffer, sendcounts, displs, MPI_CHAR, buffer, sendcounts[0], MPI_CHAR, 0, MPI_COMM_WORLD);

        } else {
            // receive the number of characters to be received by each process
            MPI_Bcast(sendcounts, sz, MPI_INT, 0, MPI_COMM_WORLD);
            
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
    }

    void search_string(const std::vector<std::string> & input_strings, const std::string & search_string, lines_found &lines, unsigned line_offset) {
        // search for the search string in the input strings
        for (int i = 0; i < input_strings.size(); ++i) {
            if (input_strings[i].find(search_string) != std::string::npos) {
                // add the line to the lines found
                lines.push_back(number_and_line(i + line_offset + 1, input_strings[i]));
            }
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

        // if there are no lines found, return
        if (total_lines == 0) {
            if (rk == 0) {
                // override output file
                std::ofstream f_stream ("/home/mpi/program_result.txt");
                f_stream << "";
                f_stream.close();
            }
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

            // compute the displacements and the total length of the complete string
            displs[0] = 0;
            totlen += recieve_counts[0] + 1;

            for (int i = 1; i < sz; ++i) {
                totlen += recieve_counts[i];
                displs[i] = displs[i - 1] + recieve_counts[i - 1];
            }

            totalstring = new char[totlen];        
            totalstring[totlen - 1] = '\0';
        }
        // gather all the lines
        MPI_Gatherv(output_string.c_str(), output_string.size(), MPI_CHAR, totalstring, recieve_counts, displs, MPI_CHAR, 0, MPI_COMM_WORLD);

        if (rk == 0) {
            // write results to file
            std::ofstream output_file;
            output_file.open("/home/mpi/program_result.txt");
            output_file << totalstring;
            output_file.close();
        }
    }
}