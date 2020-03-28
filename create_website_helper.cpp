#include <boost/format.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <fstream>
#include <iostream>
#include <istream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#define WEBINDEX_HEADER_PATH "CameraWebServer/main/camera_website_index.h"

#include WEBINDEX_HEADER_PATH

using namespace std;

class membuf : public basic_streambuf<char> {
  public:
    membuf(const uint8_t *p, size_t l) {
        setg((char *)p, (char *)p, (char *)p + l);
    }
};

class memstream : public istream {
  public:
    memstream(const uint8_t *p, size_t l) : istream(&_buffer), _buffer(p, l) {
        rdbuf(&_buffer);
    }

  private:
    membuf _buffer;
};

template <class In, class Out>
static void decompress(In &inStream, Out &outStream) {
    boost::iostreams::filtering_streambuf<boost::iostreams::input> decompresser;
    decompresser.push(boost::iostreams::gzip_decompressor());
    decompresser.push(inStream);
    boost::iostreams::copy(decompresser, outStream);
}

template <class In, class Out>
static void compress(In &inStream, Out &outStream) {
    boost::iostreams::filtering_streambuf<boost::iostreams::input> compressor;
    compressor.push(boost::iostreams::gzip_compressor());
    compressor.push(inStream);
    boost::iostreams::copy(compressor, outStream);
}

static void compressAndformat(const char *const file, ofstream &camera_index) {
    ifstream inputFile(file, ios_base::in);

    const size_t fileStrSize = strlen(file);
    char *buf = new char[fileStrSize + 4];
    memcpy(buf, file, fileStrSize);
    memcpy(buf + fileStrSize, ".gz", 4);

    ofstream tmpFile(buf, ios_base::out | ios_base::binary);
    compress(inputFile, tmpFile);

    ifstream compressedFile(buf, ios_base::in | ios_base::binary);

    int i = 0;
    camera_index << "    ";
    for (char data; compressedFile.read(&data, 1); ++i) {
        if (i != 15) {
            camera_index << boost::format("0x%02X, ") % (((unsigned)data) & 0xFF);
        } else {
            camera_index << boost::format("0x%02X,") % (((unsigned)data) & 0xFF) << endl
                         << "    ";
            i = -1;
        }
    }

    // Delete the temporary file
    int ret = remove(buf);
    if (ret) {
        cerr << "Could not delete temporary file: " << buf << endl;
        cerr << "Error code: " << ret << endl;
    }

    delete[] buf;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        cout << "Decompress " << WEBINDEX_HEADER_PATH << "!" << endl;

        memstream file(index_ov2640_html_gz, index_ov2640_html_gz_len);
        ofstream outFile("index_ov2640.html", ios_base::out);
        decompress(file, outFile);

        memstream file2(index_ov3660_html_gz, index_ov3660_html_gz_len);
        ofstream outFile2("index_ov3660.html", ios_base::out);
        decompress(file2, outFile2);
    } else {
        cout << "Writing new " << WEBINDEX_HEADER_PATH << " file!" << endl;

        ofstream camera_index(WEBINDEX_HEADER_PATH, ios_base::out);

        camera_index << "#pragma once" << endl
                     << endl;
        camera_index << "#define NELEMS(x)  (sizeof(x) / sizeof((x)[0]))" << endl
                     << endl;

        camera_index << "const uint8_t index_ov2640_html_gz[] = {" << endl;
        compressAndformat("index_ov2640.html", camera_index);
        camera_index << endl
                     << "};" << endl
                     << endl
                     << endl;

        camera_index << "const uint8_t index_ov3660_html_gz[] = {" << endl;
        compressAndformat("index_ov3660.html", camera_index);
        camera_index << endl
                     << "};" << endl
                     << endl
                     << endl;

        camera_index << "#define index_ov2640_html_gz_len NELEMS(index_ov2640_html_gz)" << endl;
        camera_index << "#define index_ov3660_html_gz_len NELEMS(index_ov3660_html_gz)" << endl;
    }
}
