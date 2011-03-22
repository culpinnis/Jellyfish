/*  This file is part of Jellyfish.

    Jellyfish is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Jellyfish is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Jellyfish.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <vector>
#include <iostream>
#include <fstream>

#include <jellyfish/err.hpp>
#include <jellyfish/misc.hpp>
#include <jellyfish/square_binary_matrix.hpp>
#include <jellyfish/randomc.h>
#include <jellyfish/generate_sequence_cmdline.hpp>

class rDNAg_t {
public:
  rDNAg_t(CRandomMersenne *_rng) : rng(_rng), i(15), buff(0) {}
  char letter() {
    i = (i+1) % 16;
    if(i == 0)
      buff = rng->BRandom();
    char res = letters[buff & 0x3];
    buff >>= 2;
    return res;
  }
  char qual_Illumina() {
    return rng->IRandom(66, 104);
  }
private:
  CRandomMersenne *rng;
  int i;
  uint32_t buff;
  static const char letters[4];
};
const char rDNAg_t::letters[4] = { 'A', 'C', 'G', 'T' };

struct generate_sequence_args args;

void create_path(char *path, unsigned int path_size, const char *ext, bool many, int i) {
  int len;
  if(many)
    len = snprintf(path, path_size, "%s_%d.%s", args.output_arg, i, ext);
  else
    len = snprintf(path, path_size, "%s.%s", args.output_arg, ext);
  if(len < 0)
    die << "Error creating the fasta file '" << path << "'" << err::no;
  if((unsigned int)len >= path_size)
    die << "Output prefix too long '" << args.output_arg << "'";
}

int main(int argc, char *argv[])
{
  
  if(generate_sequence_cmdline(argc, argv, &args) != 0)
    die << "Command line parser failed";

  if(args.inputs_num == 0)
    die << "Need at least 1 length\n" 
        << generate_sequence_args_usage << "\n" << generate_sequence_args_help;
  
  if(args.verbose_flag)
    std::cout << "Seed: " << args.seed_arg << "\n";
  CRandomMersenne rng(args.seed_arg);
  
  // Generate matrices
  uint64_t lines[64];
  for(unsigned int j = 0; j < args.mer_given; j++) {
    if(args.mer_arg[j] <= 0 || args.mer_arg[j] > 31)
      die << "Mer size (" << args.mer_arg[j] << ") must be between 1 and 31.";
    int matrix_size = args.mer_arg[j] << 1;
    SquareBinaryMatrix mat(matrix_size), inv(matrix_size);
    while(true) {
      for(int i = 0; i < matrix_size; i++)
        lines[i] = (uint64_t)rng.BRandom() | ((uint64_t)rng.BRandom() << 32);
      mat = SquareBinaryMatrix(lines, matrix_size);
      try {
        inv = mat.inverse();
        break;
      } catch(SquareBinaryMatrix::SingularMatrix e) {}
    }
    char path[4096];
    int len = snprintf(path, sizeof(path), "%s_matrix_%d", args.output_arg, 
                       args.mer_arg[j]);
    if(len < 0)
      die << "Error creating the matrix file '" << path << "'" << err::no;
    if((unsigned int)len >= sizeof(path))
      die << "Output prefix too long '" << args.output_arg << "'";
    std::ofstream fd(path);
    if(!fd.good())
      die << "Can't open matrix file '" << path << "'" << err::no;
    if(args.verbose_flag)
      std::cout << "Creating matrix file '" << path << "'\n";
    mat.dump(&fd);
    if(!fd.good())
      die << "Error while writing matrix '" << path << "'" << err::no;
    fd.close();
  }
  
  // Output sequence
  rDNAg_t rDNAg(&rng);
  char path[4096];
  bool many = args.inputs_num > 1;
  
  for(unsigned int i = 0; i < args.inputs_num; ++i) {
    size_t length = atol(args.inputs[i]);;

    if(args.fastq_flag) {
      create_path(path, sizeof(path), "fq", many, i);
      std::ofstream fd(path);
      if(!fd.good())
        die << "Can't open fasta file '" << path << err::no;
      if(args.verbose_flag)
        std::cout << "Creating fastq file '" << path << "'\n";
      size_t total_len = 0;
      unsigned long read_id = 0;
      while(total_len < length) {
        fd << "@read_" << (read_id++) << "\n";
        int i;
        for(i = 0; i < 70 && total_len < length; i++, total_len++)
          fd << rDNAg.letter();
        fd << "\n+\n";
        for(int j = 0; j < i; j++)
          fd << rDNAg.qual_Illumina();
        fd << "\n";
      }
      if(!fd.good())
        die << "Error while writing fasta file '" << path << err::no;
      fd.close();
    } else {
      create_path(path, sizeof(path), "fa", many, i);
      std::ofstream fd(path);
      if(!fd.good())
        die << "Can't open fasta file '" << path << err::no;
      if(args.verbose_flag)
        std::cout << "Creating fasta file '" << path << "'\n";
      fd << ">read\n";
      size_t total_len = 0;
      while(total_len < length) {
        for(int i = 0; i < 70 && total_len < length; i++) {
          fd << rDNAg.letter();
          total_len++;
        }
        fd << "\n";
      }
      if(!fd.good())
        die << "Error while writing fasta file '" << path << err::no;
      fd.close();
    }
  }

  return 0;
}