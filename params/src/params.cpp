/*
 * Copyright (C) 1998-2016 ALPS Collaboration. See COPYRIGHT.TXT
 * All rights reserved. Use is subject to license terms. See LICENSE.TXT
 * For use in publications, see ACKNOWLEDGE.TXT
 */

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string.h>
#include <fstream>
#include <cassert>
//#include <vector>
//#include <algorithm>
//#include <iterator>
#include "boost/foreach.hpp"
// #include "boost/preprocessor.hpp"
#include "boost/algorithm/string/trim.hpp"
#include "boost/algorithm/string/erase.hpp"
#include "boost/algorithm/string/classification.hpp"
#include "boost/algorithm/string/predicate.hpp"

#include "boost/optional.hpp"

// #include <alps/hdf5/archive.hpp>
#include <alps/hdf5.hpp>

#include <alps/params.hpp>

#ifdef ALPS_HAVE_MPI
#include <alps/utilities/mpi.hpp>
#include <alps/utilities/mpi_vector.hpp>
#include <alps/utilities/mpi_optional.hpp>
#endif


// /* Supported parameter types: */
// #ifndef ALPS_PARAMS_SUPPORTED_TYPES
// #define ALPS_PARAMS_SUPPORTED_TYPES (5, (int,unsigned,double,bool,std::string))
// #endif


// Anonymous namespace for service functions & classes
namespace {
    // Service functor class to convert C-string pointer to an std::string
    struct cstr2string {
        std::string operator()(const char* cstr)
        {
            return std::string(cstr);
        }
    };

    // Service function to try to open an HDF5 archive, return "none" if it fails
    boost::optional<alps::hdf5::archive> try_open_ar(const std::string& fname, const char* mode)
    {
        try {
            //read in hdf5 checksum of file and verify it's a hdf5 file
            {
              std::ifstream f(fname.c_str(),std::ios::binary);
              if(!f.good()) return boost::none;
              char hdf5_checksum[]={(char)137,72,68,70,13,10,26,10};
              char firstbytes[8]; 
              f.read(firstbytes, 8);
              if(!f.good() || strncmp(hdf5_checksum,firstbytes,8)!=0) return boost::none;
            }
            return alps::hdf5::archive(fname, mode);
        } catch (alps::hdf5::archive_error& ) {
            return boost::none;
        }
    };
}

namespace alps{ 
    namespace params_ns {
  
        namespace po=boost::program_options;

        void params::preparse_ini()
        {
            po::parsed_options parsed=po::parse_config_file<char>(infile_.c_str(),po::options_description(),true);
            BOOST_FOREACH(const po::option& opt, parsed.options) {
                assert(opt.unregistered && "boost::program_options should mark all opts as unregistered!");
                if (opt.value.size()!=1) {
                    throw std::logic_error("params::ctor: Unexpected value size from boost::program_options:"
                                           " key='" + boost::lexical_cast<std::string>(opt.string_key)
                                           + " ' size=" + boost::lexical_cast<std::string>(opt.value.size())
                                           + ALPS_STACKTRACE);
                }
                file_content_.append(opt.string_key + "=" + opt.value[0] + "\n");
            }
        }
    
        void params::init(unsigned int argc, const char* const* argv, const char* hdfpath)
        {
            if (argc>0) argv0_=argv[0];
            if (argc>1) {
                if (std::string(argv[1]).substr(0,2)!="--") {
                    // first argument exists and is not an option
                    infile_=argv[1];
                    boost::optional<alps::hdf5::archive> ar=try_open_ar(infile_, "r");
                    if (ar) { // valid HDF5!
                        if (hdfpath) { // and we want it!
                            this->load(*ar, hdfpath);
                            archname_=argv[1]; 
                            infile_.clear();
                            argvec_.clear();
                            argv0_=argv[0];
                        }
                    } else {
                        // it's presumably an INI: pre-parse and remember
                        preparse_ini();
                    }
                    
                    // skip the first argument
                    --argc;
                    ++argv;
                }
                // save the command line
                std::transform(argv+1,argv+argc, std::back_inserter(argvec_), cstr2string());
            }
            if (is_restored()) {
                // re-parse the command line (overriding values)
                // and skip default initialization
                certainly_parse(true);
            } else {
                // no use to parse command line, default init is needed
                init();
            }
        }

#ifdef ALPS_HAVE_MPI      
        params::params(unsigned int argc, const char* const* argv, const alps::mpi::communicator& comm,
                                   int root, const char* hdfpath)
        {
            if (comm.rank()==root) {
                init(argc, argv, hdfpath);
            }
            this->broadcast(comm,root);
        }
#endif
            
        /// @brief Convenience function: returns the "origin name"
        /// @Returns (parameter_file_name || restart_file name || program_name || "")
        // Rationale: the "origin name" is a useful info, otherwise
        // inaccessible. But it is a caller's responsibility to make
        // sense of it.
        std::string params::get_origin_name() const
        {
            if (!infile_.empty()) return infile_;
            if (archname_) return *archname_;
            return argv0_;
        }

        /// Function to check for validity/redefinition of an option (throws!)
        void params::check_validity(const std::string& optname) const
        {
            if ( ! (boost::all(optname, boost::is_alnum()||boost::is_any_of("_.-"))
                    && boost::all(optname.substr(0,1), boost::is_alnum()) ))  {
                // The name is not alphanum-underscore-dot-dash, or does not start with alnum
                throw invalid_name(optname, "Invalid parameter name");
            }
            if (descr_map_.count(optname)) {
                // The option was already defined
                throw double_definition(optname,"Attempt to define already defined parameter");
            }
            if (optmap_.count(optname)) {
                // The option was already explicitly assigned or defined
                throw extra_definition(optname, "Attempt to define explicitly assigned parameter");
            }
        }
        
        
        void params::certainly_parse(po::options_description& odescr, bool reassign) const
        {
            // First, create program_options::options_description from the description map
            BOOST_FOREACH(const detail::description_map_type::value_type& kv, descr_map_)
            {
                kv.second.add_option(odescr, kv.first);
            }

            // Second, parse the parameters according to the description into the variables_map
            po::variables_map vm;

            // Parse even if the commandline is empty, to set default values.
            {
                po::parsed_options cmdline_opts=
                    po::command_line_parser(argvec_).
                    allow_unregistered().
                    options(odescr).
                    run();

                po::store(cmdline_opts,vm);
            }

             if (!file_content_.empty()) {
                 std::istringstream istr(file_content_);
                // parse the file, allow unregistered options
                 po::parsed_options cfgfile_opts=po::parse_config_file(istr,odescr,true);

                 po::store(cfgfile_opts,vm);
             }

            // Now for each defined option, copy the corresponding parsed option to the this's option map

            // NOTE#1: Only options that are not yet in optmap_ are
            //         affected here, to avoid overwriting an option
            //         that was assigned earlier) --- unless 
            //         `reassign` is set to true, indicating that the
            //         command line options take precedence.
            // NOTE#2:
            //         The loop is over the content of the define()'d
            //         options (descr_map_) so that options that are
            //         defined but are not in the command line and are
            //         without default will be set: it is needed for
            //         "trigger" options. It may also be an
            //         opportunity to distinguish between options that
            //         are define()'d but are missing and those which
            //         were never even define()'d.

            defaulted_options_.clear(); // FIXME!!! can we avoid this lengthy operation?
            BOOST_FOREACH(const detail::description_map_type::value_type& slot, descr_map_) {
                const std::string& k=slot.first;
                const detail::description_map_type::mapped_type& dscval=slot.second;
                const po::variables_map::mapped_type& cmdline_var=vm[k];
                
                // Mark the options that have default values (not in command line)
                if (cmdline_var.defaulted()) defaulted_options_.insert(k); // FIXME: it's a temporary hack
                  
                if (reassign) {
                    // options in the command line must override stored options
                    if (cmdline_var.empty() || cmdline_var.defaulted()) continue; // skip options missing from cmdline
                } else {
                    // no override, stored options take precedence
                    if (optmap_.count(k)) continue; // skip the keys that are already stored and have value assigned
                }
                dscval.set_option(optmap_[k], cmdline_var.value());
            }
            is_valid_=true;
        }        

        /// @todo: FIXME: only weak exception guarantee
        void params::save(hdf5::archive& ar) const
        {
            possibly_parse();
            const std::string context=ar.get_context();
            const std::string dict_group=context+"/dictionary";
            const std::string def_group=context+"/definitions";
            const std::string state_group=context+"/object_state";

            if (ar.is_group(context)) {
                ar.delete_group(context);
                ar.create_group(context);
                ar.set_context(context);
            }
            
            ar.create_group(dict_group);
            ar.set_context(dict_group);
            BOOST_FOREACH(const options_map_type::value_type& slot, optmap_)
            {
                slot.second.save(ar);
            }

            ar.create_group(def_group);
            ar.set_context(def_group);
            BOOST_FOREACH(const detail::description_map_type::value_type& slot, descr_map_) {
                const detail::option_description_type& opt=slot.second;
                const std::string& key=slot.first;
                opt.save(ar,key);
            }
            
            ar.create_group(state_group);
            ar.set_context(state_group);

            ar["help"] << helpmsg_;
            ar["argv"] << argvec_;
            ar["inifile"] << file_content_;
            ar["ininame"] << infile_;
            ar["argv0"] << argv0_;
            if (archname_) ar["archname"] << *archname_;
            
            ar.set_context(context);
        }

        void params::save(hdf5::archive& ar, const std::string& path) const
        {
            std::string context = ar.get_context();
            ar.set_context(path);
            save(ar);
            ar.set_context(context);
        }

        /// @todo: FIXME: only weak exception guarantee
        void params::load(hdf5::archive& ar)
        {
            // FXIME: implement and use swap() here
            const std::string context=ar.get_context();
            const std::string dict_group=context+"/dictionary";
            const std::string def_group=context+"/definitions";
            const std::string state_group=context+"/object_state";

            ar.set_context(dict_group);
            std::vector<std::string> dict_names=ar.list_children(".");
            optmap_.clear();
            BOOST_FOREACH(const std::string& key, dict_names) {
                if (ar.is_group(key)) continue;
                optmap_.insert(options_map_type::value_type(key, option_type::get_loaded(ar,key)));
            }
            
            ar.set_context(def_group);
            std::vector<std::string> def_names=ar.list_children(".");
            descr_map_.clear();
            BOOST_FOREACH(const std::string& key, def_names) {
                descr_map_.insert(detail::description_map_type::value_type(key, detail::option_description_type::get_loaded(ar,key)));
            }

            // FIXME: once swap() is implemented and used, we don't need `else ...clear()`
            if (ar.is_group(state_group)) {
                ar.set_context(state_group);
                if (ar.is_data("help")) ar["help"] >> helpmsg_; else helpmsg_.clear();
                if (ar.is_data("argv")) ar["argv"] >> argvec_; else argvec_.clear();
                if (ar.is_data("inifile")) ar["inifile"] >> file_content_; else file_content_.clear();
                if (ar.is_data("ininame")) ar["ininame"] >> infile_; else infile_.clear();
                if (ar.is_data("argv0")) ar["argv0"] >> argv0_; else argv0_.clear();
                if (ar.is_data("archname")) {
                    std::string archname;
                    ar["archname"] >> archname;
                    archname_=archname;
                } else {
                    archname_=boost::none;
                }
            } else {
                // FIXME: once swap() is implemented and used, we don't need this
                helpmsg_.clear();
                argvec_.clear();
                file_content_.clear();
                infile_.clear();
                argv0_.clear();
                archname_=boost::none;
            }
             
            ar.set_context(context);
            certainly_parse();
        }

        void params::load(hdf5::archive& ar, const std::string& path)
        {
            std::string context = ar.get_context();
            ar.set_context(path);
            load(ar);
            ar.set_context(context);
        }

        bool params::help_requested(std::ostream& ostrm) const
        {
            if (help_requested()) { 
                print_help(ostrm);
                return true;
            }
            return false;
        }
        
        void params::print_help(std::ostream& ostrm) const
        {
            po::options_description odescr;
            certainly_parse(odescr);
            ostrm << helpmsg_ << std::endl;
            ostrm << odescr;
        }        

        bool params::has_missing(std::ostream& ostrm) const
        {
            missing_params_iterator it=begin_missing();
            missing_params_iterator end=end_missing();
            if (it==end) return false;
            ostrm << "The following mandatory arguments are missing:\n";
            for (; it!=end; ++it) {
                ostrm << "\"" << *it << "\"\n";
            }
            ostrm << "Use \"" << argv0_ << " --help\" to see the list of options.\n";
            return true;
        }
            
      
#ifdef ALPS_HAVE_MPI
        /** @NOTE  Implemented as serialization followed by string broadcast (FIXME!) */
        void params::broadcast(alps::mpi::communicator const & comm, int root)
        {
            // throw std::logic_error("Not implemented");

            // FIXME: correct implementation would use swap()

            using alps::mpi::broadcast;
            
            possibly_parse();

            broadcast(comm, helpmsg_,root);
            broadcast(comm, file_content_, root);
            broadcast(comm, infile_, root);
            broadcast(comm, argv0_, root);

            broadcast(comm, argvec_, root);

            broadcast(comm, archname_, root);

            optmap_.broadcast(comm, root);

            broadcast(comm, descr_map_, root);

            certainly_parse();
            
            // std::string buf;
            // if (comm.rank()==root) {
            //     possibly_parse();
            //     // boost_ar << *this;
            //     // buf=outs.str();
            // }
            // alps::mpi::broadcast(comm, buf, root);
            // if (comm.rank()!=root) {
            //     // std::istringstream ins(buf);
            //     // boost::archive::text_iarchive boost_ar(ins);
            //     // boost_ar >> *this;
            // }
        }
#endif

        std::ostream& operator<<(std::ostream& str, params const& x) 
        {
            for (params::const_iterator it = x.begin(); it != x.end(); ++it) { 
                if (!(it->second).isNone()) {
                    str << it->first << " : " << it->second << std::endl;
                }
            } 
            return str;
        }


        namespace detail {
            // Validator for strings
            void validate(boost::any& outval, const std::vector<std::string>& strvalues,
                          string_container* target_type, int)
            {
                namespace po=boost::program_options;
                namespace pov=po::validators;
                namespace alg=boost::algorithm;
        
                pov::check_first_occurrence(outval); // check that this option has not yet been assigned
                std::string in_str=pov::get_single_string(strvalues); // check that this option is passed a single value

                // Now, do parsing:
                alg::trim(in_str); // Strip trailing and leading blanks
                if (in_str[0]=='"' && in_str[in_str.size()-1]=='"') { // Check if it is a "quoted string"
                    // Strip surrounding quotes:
                    alg::erase_tail(in_str,1);
                    alg::erase_head(in_str,1);
                }
                outval=boost::any(in_str);
                // std::cerr << "***DEBUG: returning from validate(...std::string*...) ***" << std::endl;
            }
        } // detail
    } // params_ns
} // alps

