#include "CameraWebServer/main/avi_helper.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <vector>

using namespace std;

#define FRAMES_COUNT 60

inline uint32_t readLittleEndian(FILE *from) {
    uint32_t result = 0;
    unsigned char buf[4] = {0};

    fread(buf, 1, 4, from);

    for (int i = sizeof(uint32_t) - 1; i >= 0 ; --i) {
        result = result << 8;
        result |= buf[i];
    }

    return result;
}

inline void readStruct(FILE *from, size_t offset, void *toBeWritten, size_t byteSize) {
    fseek(from, offset, SEEK_SET);

    uint32_t *writeMe = (uint32_t *)toBeWritten;

    for (size_t i = 0; i < byteSize / sizeof(*writeMe); ++i) {
        writeMe[i] = readLittleEndian(from);
    }
}

template<class T>
void printStruct(const T& header) {
    for (int i = 0; i < sizeof(T) / sizeof(uint32_t); ++i) {
        printf("Value: Decimal: %u Hex: %x\n", ((uint32_t *)&header)[i], ((uint32_t *)&header)[i]);
    }
}

int main() {
    FILE *jpgFile = fopen("test.jpg", "rb");

    vector<char> jpg;
    for (char data; fread(&data, 1, 1, jpgFile);) {
        jpg.push_back(data);
    }

    fclose(jpgFile);

    printf("INFO: jpg Size: %d bytes\n", jpg.size());

    AVIMainHeader header;
    AVIStreamHeader streamHeader;
    AVIStreamFormat streamFormat;

    streamHeader.scale = 1;
    streamHeader.rate = 2;
    streamFormat.biWidth = header.width = 1024;
    streamFormat.biHeight = header.height = 768;
    header.microSecPerFrame = (uint32_t)((((double)(streamHeader.scale * 1000000)) / streamHeader.rate));

    // Fields that normally needs to be patched afterwards
    header.maxBytesPerSec = (uint32_t)((((double)(jpg.size() * streamHeader.scale)) / streamHeader.rate));
    streamHeader.length = header.totalFrames = FRAMES_COUNT;

    FILE *file = fopen("test.avi", "wb");
    FILE *idxFile = fopen("test.avi.idx", "wb+");

    size_t offset = createAVI_File(file, header, streamHeader, streamFormat);

    for (size_t i = 0; i < FRAMES_COUNT; ++i) {
        writeFrame(file, idxFile, &offset, jpg.data(), jpg.size());
    }

    mergeAVIAndIndexFile(file, idxFile, &offset);

    fclose(file);
    fclose(idxFile);

    remove("test.avi.idx");

    /*
    FILE *binaryFile = fopen("test.avi", "rb");

    int i = 0;
    for (char data; fread(&data, 1, 1, binaryFile); ++i) {
        if (i != 15) {
            printf("0x%02X, ", (((unsigned)data) & 0xFF));
        } else {
            printf("0x%02X,\n", (((unsigned)data) & 0xFF));
            i = -1;
        }
    }

    fclose(binaryFile);
    */

    FILE *reference = fopen("test.avi", "rb");

    readStruct(reference, AVI_MAIN_HEADER_START, &header, sizeof(header));
    readStruct(reference, AVI_STREAM_HEADER_START, &streamHeader, sizeof(streamHeader));
    readStruct(reference, AVI_STREAM_FORMAT_START, &streamFormat, sizeof(streamFormat));

    fclose(reference);

    printf("\nMain Header:\n");
    printStruct(header);

    printf("\nStream Header:\n");
    printStruct(streamHeader);

    printf("\nStream Format:\n");
    printStruct(streamFormat);

    printf("\n\n");

    reference = fopen("test_reference.avi", "rb");

    readStruct(reference, AVI_MAIN_HEADER_START, &header, sizeof(header));
    readStruct(reference, AVI_STREAM_HEADER_START, &streamHeader, sizeof(streamHeader));
    readStruct(reference, AVI_STREAM_FORMAT_START, &streamFormat, sizeof(streamFormat));

    fclose(reference);

    printf("\nMain Header:\n");
    printStruct(header);

    printf("\nStream Header:\n");
    printStruct(streamHeader);

    printf("\nStream Format:\n");
    printStruct(streamFormat);
}