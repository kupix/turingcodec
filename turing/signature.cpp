/*
Copyright (C) 2016 British Broadcasting Corporation, Parabola Research
and Queen Mary University of London.

This file is part of the Turing codec.

The Turing codec is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

The Turing codec is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

Commercial support and intellectual property rights for
the Turing codec are also available under a proprietary license.
For more information, contact us at info @ turingcodec.org.
 */
// tests to detect changes in encoder functionality

#include <fstream>
#include <cassert>
#include <string>
#include <sstream>
#include <vector>
#include <iostream>
#include <iomanip>
#include <array>
#include <iterator>
#include "md5.h"


#pragma optimize ("", off)


int decode(int argc, const char* const argv[], std::ostream &cout, std::ostream &cerr);
int encode(int argc, const char* const argv[]);
int signature(int argc, const char* const argv[], std::ostream &cout, std::ostream &cerr);

template <typename T, int n>
constexpr int array_size(T const (&)[n])
{
    return n;
}

namespace {

    std::string md5Sum(const char *filename)
    {
        // read file into buffer
        std::ifstream ifs(filename, std::ios::binary);
        ifs.seekg(0, std::ios::end);
        auto const bytes = ifs.tellg();
        ifs.seekg(std::ios::beg);
        std::vector<char> buffer(bytes);
        ifs.read(&buffer.front(), bytes);

        // compute MD5 sum of buffer
        md5_state_t state;
        md5_init(&state);
        md5_append(&state, reinterpret_cast<md5_byte_t *>(&buffer.front()), static_cast<int>(bytes));
        std::array<md5_byte_t, 16> digest;
        md5_finish(&state, &digest.front());

        std::ostringstream s;

        // only return first four bytes of digest for brevity of checksums in code
        for (auto i = 0; i < 4; ++i)
            s << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(digest[i]);

        return s.str();
    }

    bool fileDiff(const char *filenames[2])
    {
        std::vector<char> buffer[2];
        for (int i = 0; i < 2; ++i)
        {
            std::ifstream ifs(filenames[i], std::ios::binary);
            ifs.seekg(std::ios::end);
            auto const bytes = ifs.tellg();
            ifs.seekg(std::ios::beg);
            buffer[i].resize(bytes);
            ifs.read(&buffer[i].front(), bytes);
        }

        if (buffer[0].size() != buffer[1].size())
            return true;

        for (int i = 0; i < buffer[0].size(); ++i)
        {
            if (buffer[0][i] != buffer[1][i])
                return true;
        }

        return false;
    }

}


// returns 1 if MD5 different, -1 on failure
int runEncode(std::string sourceFolder, std::string sourceFilename, std::string options, std::string &streamMd5, std::string &yuvMd5, std::ostream &cout, std::ostream &cerr)
{
    auto const md5len = 4; // only use first four bytes of MD5 sum for brevity of code

    auto sourceYuvFilename = sourceFilename + ".yuv";
    auto sourceBitstreamPath = sourceFolder + sourceFilename;

    // decode the source bitstream to YUV
    {
        char const *args[] =
        {
                "decode",
                "-o", sourceYuvFilename.c_str(),
                sourceBitstreamPath.c_str()
        };

        if (decode(array_size(args), args, cout, cerr))
        {
            cerr << "failed to decode " << sourceFilename << "\n";
            return -1;
        }
    }

    // encode YUV to HEVC using specified options
    {
        std::vector<char const *> argv{
            "encode",
            "-o", "encoded.hevc",
            "--dump-pictures", "encoded.yuv" };

        if (!options.empty())
            argv.push_back(&options.front());

        for (auto &c : options)
            if (c == ' ')
            {
                c = '\0';
                argv.push_back(&c + 1);
            }

        argv.push_back(sourceYuvFilename.c_str());

        if (encode(static_cast<int>(argv.size()), &argv.front()))
            return -1;
    }

    auto md5Stream = md5Sum("encoded.hevc");
    auto md5Enc = md5Sum("encoded.yuv");

    // decode
    {
        const char *args[] =
        {
                "decode",
                "-o", "decoded.yuv",
                "encoded.hevc"
        };

        if (decode(array_size(args), args, cout, cerr))
        {
            cerr << "decode failed\n";
            return -1;
        }
    }

    const auto md5Dec = md5Sum("decoded.yuv");

    if (md5Dec != md5Enc)
    {
        cerr << "encoder and decoder reconstruction mismatch\n";
        return -1;
    }

    if (yuvMd5 != md5Enc)
    {
        // encoder output is different than expected
        yuvMd5 = md5Enc;
        return 1;
    }

    yuvMd5 = md5Enc;
    return 0;
}


int signature(int argc, const char* const argv[], std::ostream &cout, std::ostream &cerr)
{
    std::string yuvMd5;

    struct Source
    {
        const char *filename;
        const char *options;
    };

    struct Test
    {
        const char *streamMd5;
        const char *yuvMd5;
        Source source;
        const char *options;
    };

    static const Source caminandes{ "excerpt_(CC)_caminandes.com_640x360.hevc", "--input-res 640x360 --frame-rate 24 --frames 120" };
    static const Source caminandes2{ "excerpt_(CC)_caminandes.com_640x360.hevc", "--input-res 640x360 --frame-rate 24 --frames 2" };

    static const Test tests[] = {
            { "57f48098", "978643d0", caminandes, "" },
    };

    auto mismatchCount = 0;

    for (auto const &test : tests)
    {
        std::string options = test.source.options;
        if (*test.options)
        {
            options += " ";
            options += test.options;
        }

        std::string  streamMd5 = test.streamMd5;
        std::string  yuvMd5 = test.yuvMd5;

        int rv = runEncode(argv[1], test.source.filename, options.c_str(), streamMd5, yuvMd5, std::cout, std::cerr);

        if (rv < 0)
        {
            cerr << "signature test failed: \"" << test.source.filename << "\" " << test.source.options << " " << test.options << "\n";
            return rv;
        }

        if (rv > 0)
        {
            cout << "signature test mismatch: \"" << test.source.filename << "\" " << test.source.options << " " << test.options << "\n";
            cout << "YUV expected " << test.streamMd5 << " actual " << streamMd5 << "\n";
            cout << "YUV expected " << test.yuvMd5 << " actual " << yuvMd5 << "\n";
            ++mismatchCount;
        }
    }

    return mismatchCount;
}
