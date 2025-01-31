/*
    Copyright 2019 Tenable, Inc.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                *

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
        list of conditions and the following disclaimer.

    2. Redistributions in binary form must reproduce the above copyright notice,
        this list of conditions and the following disclaimer in the documentation
        and/or other materials provided with the distribution.

    3. Neither the name of the copyright holder nor the names of its contributors
        may be used to endorse or promote products derived from this software
        without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/
#include <list>
#include <regex>
#include <cstdlib>
#include <fstream>
#include <ctime>
#include <iostream>
#include <boost/cstdint.hpp>
#include <boost/program_options.hpp>
#include <boost/algorithm/string.hpp>
#include <GeoLite2PP.hpp>

#include "winbox_session.hpp"
#include "winbox_message.hpp"

namespace
{
    const char s_version[] = "Winbox Scanner 1.1";

    bool parseCommandLine(int p_argCount, const char* p_argArray[],
                          std::string& p_ipFile, std::string& p_outFile,
                          bool& p_list_scan)
    {
        boost::program_options::options_description description("options");
        description.add_options()
            ("help,h", "A list of command line options")
            ("version,v", "Display version information")
            ("in,i", boost::program_options::value<std::string>(), "The list of addresses to scan")
            ("out,o", boost::program_options::value<std::string>(), "The file to write to")
            ("list_scan", boost::program_options::value<bool>()->default_value(false), "Use the mproxy 2,2 list method");

        boost::program_options::variables_map argv_map;
        try
        {
            boost::program_options::store(
                boost::program_options::parse_command_line(
                    p_argCount, p_argArray, description), argv_map);
        }
        catch (const std::exception& e)
        {
            std::cerr << e.what() << std::endl;
            std::cerr << description << std::endl;
            return false;
        }

        boost::program_options::notify(argv_map);
        if (argv_map.empty() || argv_map.count("help"))
        {
            std::cerr << description << std::endl;
            return false;
        }

        if (argv_map.count("version"))
        {
            std::cerr << "Version: " << ::s_version << std::endl;
            return false;
        }

        if (argv_map.count("in") && argv_map.count("out") &&
            argv_map.count("list_scan"))
        {
            p_ipFile.assign(argv_map["in"].as<std::string>());
            p_outFile.assign(argv_map["out"].as<std::string>());
            p_list_scan = argv_map["list_scan"].as<bool>();
            return true;
        }
        else
        {
            std::cout << description << std::endl;
        }

        return false;
    }

    void do_list_scan(const std::string& p_address, const std::string& p_geo, std::ofstream& p_csv_results)
    {
        Winbox_Session winboxSession(p_address, "19889");
        if (!winboxSession.connect())
        {
            return;
        }

        std::string list;
        bool result = winboxSession.old_mproxy_get_file("list", list);
        if (result == false)
        {
            if (list.empty())
            {
                return;
            }
            else
            {
                p_csv_results << p_address << "|" << p_geo<< "|" << "old_ver" << std::endl;
                return;
            }
        }
        else
        {
            if (list.empty())
            {
                p_csv_results << p_address << "|" << p_geo << "|" << "new_ver" << std::endl;
                return;
            }
        }

        if (!list.empty())
        {
            std::smatch matches;
            // version: "6.39.3"
            std::regex version_match("version: \"([^\"]+)\"");
            std::regex_search(list, matches, version_match);
            if (!matches.empty())
            {
                p_csv_results << p_address << "|" << p_geo << "|" << matches[1] << std::endl;
                return;
            }
        }

        p_csv_results << p_address << "|" << p_geo << "|" << "unknown" << std::endl;
    }

}

int main(int p_argc, const char** p_argv)
{
    std::string ipList;
    std::string outFile;
    bool list_scan = false;
    if (!parseCommandLine(p_argc, p_argv, ipList, outFile, list_scan))
    {
        return EXIT_FAILURE;
    }

    GeoLite2PP::DB geoDB("/var/lib/GeoIP/GeoLite2-City.mmdb");

    std::cout << "[+] Creating output file." << std::endl;
    std::ofstream csv_results(outFile);
    if (!csv_results.is_open())
    {
        std::cerr << "Couldn't create the results file." << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[+] Loading IP address list..." << std::endl;
    std::ifstream ipFile(ipList);
    if (!ipFile.is_open())
    {
        std::cerr << "Couldn't open the IP file." << std::endl;
        return EXIT_FAILURE;
    }

    // load all the ips into a list beforehand, I guess. 
    std::list<std::pair<std::string, std::string> > ip_list;

    std::string ip_address;
    while (std::getline(ipFile, ip_address))
    {
        if (ip_address.empty())
        {
            continue;
        }
        std::string country = "unknown";
        try
        {
            country = geoDB.get_field(ip_address, "en", GeoLite2PP::VCStr { "country", "names" });
        }
        catch (const std::exception&)
        {
        }
        ip_list.push_back(std::make_pair(ip_address, country));
        ip_address.clear();
    }
    ipFile.close();

    std::cout << "[!] " << ip_list.size() << " IP addresses loaded" << std::endl;

    int start = time(NULL);
    double per_second = 0;
    int count = 1;
    for (std::list<std::pair< std::string, std::string> >::const_iterator iter = ip_list.begin();
        iter != ip_list.end(); iter++, count++)
    {
        if ((count % 100) == 0)
        {
            per_second = (double)(time(NULL) - start) / count;
        }

        std::cout << "\r" << count << " / " << ip_list.size() << " | " << per_second << " per second" << std::flush;
    
        try
        {
            if (list_scan)
            {
                do_list_scan(iter->first, iter->second, csv_results);
            }
            else 
            {
                std::cerr << "Do nothing?" << std::endl;
                return EXIT_FAILURE;
            }
        }
        catch (const std::exception&)
        {
        }
    }

    csv_results.close();

    return EXIT_SUCCESS;
}
