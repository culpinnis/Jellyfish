#include <stdio.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>

#include <jellyfish/thread_exec.hpp>
#include <jellyfish/intersection_array.hpp>
#include <jellyfish/mer_dna.hpp>
#include <jellyfish/locks_pthread.hpp>
#include <jellyfish/mer_overlap_sequence_parser.hpp>
#include <jellyfish/mer_iterator.hpp>
#include <jflib/multiplexed_io.hpp>
#include <sub_commands/intersection_cmdline.hpp>

typedef jellyfish::intersection_array<jellyfish::mer_dna> inter_array;
typedef std::vector<const char*> file_vector;
typedef jellyfish::mer_overlap_sequence_parser<std::ifstream*> sequence_parser;
typedef jellyfish::mer_iterator<sequence_parser, jellyfish::mer_dna> mer_iterator;

intersection_cmdline args;


class compute_intersection : public thread_exec {
  int                     nb_threads_;
  locks::pthread::barrier barrier_;
  inter_array&            ary_;
  file_vector&            files_;

  sequence_parser* volatile      parser_;
  jflib::o_multiplexer* volatile multiplexer_;

public:
  compute_intersection(int nb_threads, inter_array& ary, file_vector& files) :
    nb_threads_(nb_threads),
    barrier_(nb_threads),
    ary_(ary),
    files_(files)
  { }

  virtual void start(int thid) {
    load_in_files(thid);
    output_intersection(thid);
    output_uniq(thid);
  }

  void load_in_files(int thid) {
    unsigned int   file_index = 0;
    std::ifstream* file       = 0;

    while(true) {
      if(thid == 0) {
        if(file_index != files_.size()) {
          file    = new std::ifstream(files_[file_index++]);
          parser_ = new sequence_parser(jellyfish::mer_dna::k(), 3 * nb_threads_, 4096,
                                        file, file + 1);
        }
      }
      barrier_.wait();
      if(!parser_)
        break;

      add_mers_to_ary(thid);
      barrier_.wait();
      if(thid == 0) {
        delete parser_;
        parser_ = 0;
        delete file;
        file    = 0;
      }
    }
  }

  void add_mers_to_ary(int thid) {
    for(mer_iterator mers(*parser_) ; mers; ++mers)
      ary_.add(*mers);
    ary_.done();
    ary_.postprocess(thid);
  }


  void output_intersection(int thid) {
    std::ofstream* file_out = 0;
    if(thid == 0) {
      file_out     = new std::ofstream(args.intersection_arg);
      multiplexer_ = new jflib::o_multiplexer(file_out, 3 * nb_threads_, 4096);
    }

    barrier_.wait();
    output_intersection_mers(thid);
    barrier_.wait();
    if(thid == 0) {
      delete multiplexer_;
      multiplexer_ = 0;
      delete file_out;
    }
  }

  void output_intersection_mers(int thid) {
    jflib::omstream out(*multiplexer_);
    inter_array::array::lazy_iterator it = ary_.ary()->lazy_iterator_slice(thid, nb_threads_);
    while(out && it.next()) {
      inter_array::mer_info info = ary_.info_at(it.id());
      if(!info.info.nall) {
        out << it.key() << "\n";
        out << jflib::endr;
      }
    }
  }

  void output_uniq(int thid) {
    unsigned int          file_index  = 0;
    std::ifstream*        file        = 0;
    std::ofstream*        out         = 0;

    while(true) {
      if(thid == 0) {
        if(file_index != files_.size()) {
          unsigned int cindex    = file_index++;
          file                   = new std::ifstream(files_[cindex]);
          parser_                = new sequence_parser(jellyfish::mer_dna::k(), 3 * nb_threads_, 4096, file, file + 1);
          std::string  out_name  = args.prefix_arg;
          out_name              += files_[cindex];
          out                    = new std::ofstream(out_name.c_str());
          multiplexer_           = new jflib::o_multiplexer(out, 3 * nb_threads_, 4096);
        }
      }
      barrier_.wait();
      if(!parser_)
        break;

      output_uniq_mers_file(thid);
      barrier_.wait();
      if(thid == 0) {
        delete multiplexer_;
        multiplexer_ = 0;
        delete out;
        out          = 0;
        delete parser_;
        parser_      = 0;
        delete file;
        file         = 0;
      }
    }
  }

  void output_uniq_mers_file(int thid) {
    jflib::omstream out(*multiplexer_);

    for(mer_iterator mers(*parser_); mers; ++mers) {
      inter_array::mer_info info = ary_[*mers];
      if(!info.info.mult) {
        out << *mers << "\n";
        out << jflib::endr;
      }
    }
  }
};

int intersection_main(int argc, char* argv[]) {
  args.parse(argc, argv);
  jellyfish::mer_dna::k(args.mer_arg);

  inter_array ary(args.size_arg, jellyfish::mer_dna::k() * 2, args.thread_arg);
  compute_intersection workers(args.thread_arg, ary, args.genome_arg);
  workers.exec_join(args.thread_arg);

  return EXIT_SUCCESS;
}