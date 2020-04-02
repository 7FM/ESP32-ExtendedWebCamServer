#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
static const char LIST_FORM[] = {
    // FOURCC LIST
    'L', 'I', 'S', 'T',
    // listSize (includes listType and listData)
    0x00, 0x00, 0x00, 0x00,
    // FOURCC listType (AVI)
    //'h', 'd', 'r', 'l',
    ' ', ' ', ' ', ' '
    // listData
};

static const char RIFF_AVI_FORM[] = {
    // FOURCC RIFF identifier
    'R',
    'I',
    'F',
    'F'
    // File size (including fileType)
    0x00,
    0x00,
    0x00,
    0x00,
    // FOURCC fileType
    'A',
    'V',
    'I',
    ' ',
};

static const char CK_ID_FORM[]{
    // ckID :  chunk ID
    ' ', ' ', ' ', ' ',
    // ckSize : chunk size (size of ckData without padding (32 words))
    0x00, 0x00, 0x00, 0x00,
    // ckData : chunk data
};
*/

/* AVI File format:
RIFF ('AVI '
      LIST ('hdrl'
            'avih'(<Main AVI Header>)
            LIST ('strl'
                  'strh'(<Stream header>)
                  'strf'(<Stream format>)
                  [ 'strd'(<Additional header data>) ]
                  [ 'strn'(<Stream name>) ]
                  ...
                 )
             ...
           )

      LIST ('movi'
            {SubChunk | LIST ('rec '
                              SubChunk1
                              SubChunk2
                              ...
                             )
               ...
            }
            ...
           )

      ['idx1' (<AVI Index>) ]
     )
*/

// Makro to create a 32 bit int from FOURCC char array such that it can be written in little endian without problems
#define CONVERT_TO_FCC(cstr) (cstr[0] | cstr[1] << 8 | cstr[2] << 16 | cstr[3] << 24)

// Sizes of the basic headers
#define RIFF_LIST_HEADER_SIZE 12
#define RIFF_CHUNK_HEADER_SIZE 8

// Offsets size fields of the blocks needed to be updated when adding a new frame
#define AVI_RIFF_BLOCK_SIZE_OFFSET 4
#define AVI_MOVI_BLOCK_SIZE_OFFSET 208

// Offsets to structure starts
#define AVI_MAIN_HEADER_START 32
#define AVI_STREAM_HEADER_START 108
#define AVI_STREAM_FORMAT_START 164

inline void writeLittleEndian(char *target, uint32_t toBeConverted) {
    for (size_t i = 0; i < sizeof(toBeConverted); ++i) {
        target[i] = toBeConverted % 0x100;
        toBeConverted = toBeConverted >> 8;
    }
}

inline void writeStruct(char *target, const void *toBeWritten, size_t byteSize) {
    const uint32_t *writeMe = (const uint32_t *)toBeWritten;

    for (size_t i = 0; i < byteSize / sizeof(*writeMe); ++i) {
        writeLittleEndian(target, writeMe[i]);
        target += sizeof(*writeMe);
    }
}

static inline size_t _write_riff_header(char *buf, size_t *offset, const char FOURCC[4], const char typeName[4] = NULL, size_t size = 0) {
    memcpy(buf, FOURCC, 4);

    size_t blockSize;

    // Check if typeName is given. If not we are writing a chunk header
    if (typeName) {
        memcpy(buf + 8, typeName, 4);
        *offset += blockSize = RIFF_LIST_HEADER_SIZE;
    } else {
        *offset += blockSize = RIFF_CHUNK_HEADER_SIZE;
    }

    if (size) {
        // Write the given size of data block
        writeLittleEndian(buf + 4, size);

        // Return total size
        return size + blockSize;
    }

    // Return offset needed to patch the size later
    return *offset - blockSize + 4;
}

inline size_t RIFF(char *buf, size_t *offset, const char fileType[4] = "AVI ") {
    return _write_riff_header(buf, offset, "RIFF", fileType);
}

inline size_t LIST(char *buf, size_t *offset, const char listType[4], int listDataSize = -4) {
    return _write_riff_header(buf, offset, "LIST", listType, listDataSize + 4);
}

inline size_t CHUNK(char *buf, size_t *offset, const char ckID[4], size_t chunkSize = 0) {
    return _write_riff_header(buf, offset, ckID, NULL, chunkSize);
}

inline size_t PATCH_CHUNK_SIZE(FILE *file, size_t sizeOffset, size_t chunkSize) {
    fseek(file, sizeOffset, SEEK_SET);
    char buf[4] = {0};
    writeLittleEndian(buf, chunkSize);
    fwrite(buf, 1, 4, file);

    return RIFF_CHUNK_HEADER_SIZE + chunkSize;
}

inline size_t PATCH_CHUNK_SIZE(char *bufBase, size_t sizeOffset, size_t chunkSize) {
    writeLittleEndian(bufBase + sizeOffset, chunkSize);

    return RIFF_CHUNK_HEADER_SIZE + chunkSize;
}

template <class T>
inline size_t PATCH_LIST_RIFF_SIZE(T dest, size_t sizeOffset, size_t listDataSize) {
    // add lenght for listType/fileType FOURCC
    return PATCH_CHUNK_SIZE(dest, sizeOffset, listDataSize + 4);
}

template <class T>
inline void patchNonChunkSize(T dest, size_t writeOffset, size_t patchSizeOffset) {
    size_t blockSize = writeOffset - patchSizeOffset - 4;
    PATCH_CHUNK_SIZE(dest, patchSizeOffset, blockSize);
}

inline void updateAVISize(FILE *aviFile, size_t writeOffset) {
    patchNonChunkSize(aviFile, writeOffset, AVI_RIFF_BLOCK_SIZE_OFFSET);
    patchNonChunkSize(aviFile, writeOffset, AVI_MOVI_BLOCK_SIZE_OFFSET);
}

inline void updateAVISize(char *baseBuffer, size_t writeOffset) {
    patchNonChunkSize(baseBuffer, writeOffset, AVI_RIFF_BLOCK_SIZE_OFFSET);
    patchNonChunkSize(baseBuffer, writeOffset, AVI_MOVI_BLOCK_SIZE_OFFSET);
}

/*
    Common header offsets are:
        - AVI_MAIN_HEADER_START
        - AVI_STREAM_HEADER_START
        - AVI_STREAM_FORMAT_START
*/
template<class T>
inline void patchHeader(FILE *aviFile, const T &header, size_t patchOffset) {

    // Go to patch offset
    fseek(aviFile, patchOffset, SEEK_SET);

    char buf[sizeof(T)];
    writeStruct(buf, &header, sizeof(T));

    // Finally patch the file
    fwrite(buf, 1, sizeof(T), aviFile);
}

static inline void fwrite_wrapper(FILE *dest, const char *src, size_t size, size_t offset) {
    fwrite(src, sizeof(*src), size, dest);
}

static inline void memcpy_wrapper(char *dest, const char *src, size_t size, size_t offset) {
    memcpy(dest + offset, src, size);
}

// At most we need to pad by 3 bytes
static const char paddingBuffer[3] = {0};

template <class T, class F>
static inline size_t writePaddedData(T dest, size_t *offset, const char *data, size_t dataSize, F writeFn) {

    writeFn(dest, data, dataSize, 0);

#define PADDING_SIZE 2

    // Padding to multiples of 2 bytes
    // Calculate how many padding bytes are needed
    int neededPadding = (PADDING_SIZE - (dataSize & (PADDING_SIZE - 1))) & (PADDING_SIZE - 1);

    if (neededPadding) {
        printf("WARNING: PADDING NEEDED: %d, SIZE: %d\n", neededPadding, dataSize);
        writeFn(dest, paddingBuffer, neededPadding, dataSize);
        return *offset += dataSize + neededPadding;
    } else {
        return *offset += dataSize;
    }
}

inline size_t writePaddedData(char *buffer, size_t *offset, const char *data, size_t dataSize) {
    return writePaddedData(buffer, offset, data, dataSize, memcpy_wrapper);
}

inline size_t writePaddedData(FILE *file, size_t *offset, const char *data, size_t dataSize) {
    return writePaddedData(file, offset, data, dataSize, fwrite_wrapper);
}

/*

typedef struct _AviIndexEntry {
    int ChunkId;
    int Flags;
#define AVIIF_LIST 0x00000001
#define AVIIF_KEYFRAME 0x00000010
#define AVIIF_NO_TIME 0x00000100
#define AVIIF_COMPRESSOR 0x0FFF0000
    int Offset; // >Including< the header, i.e. before 
    int Size; // Not including the header 
} AviIndexEntry;

*/
inline void appendIndex(FILE *indexFile, size_t offset, size_t size) {
    char buffer[8];

    // We need a relative offset from the start of the 'movi' FOURCC to the fram '00dc' FOURCC
    // AVI_MOVI_BLOCK_SIZE_OFFSET + 4 is the offset of the 'movi' FOURCC
    // variable offset starts after the header so we need to substract RIFF_CHUNK_HEADER_SIZE as well
    // to have a offset at the start of the header
    writeLittleEndian(buffer, offset - (AVI_MOVI_BLOCK_SIZE_OFFSET + 4 + RIFF_CHUNK_HEADER_SIZE));

    // TODO padded size or dataSize?
    writeLittleEndian(buffer + 4, size);

    fwrite(buffer, 1, 8, indexFile);
}

inline size_t writeFrame(FILE *aviFile, FILE *indexFile, size_t *offset, const char *jpgFrame, size_t size) {
    char buffer[RIFF_CHUNK_HEADER_SIZE];

    CHUNK(buffer, offset, "00dc", size);

    // We expect the file to be at the correct write position
    fwrite(buffer, 1, RIFF_CHUNK_HEADER_SIZE, aviFile);

    const size_t dataOffset = *offset;

    // Write padded data and update the write position
    const size_t writeOffset = writePaddedData(aviFile, offset, jpgFrame, size);

    const size_t dataBlockSize = writeOffset - dataOffset;
    appendIndex(indexFile, dataOffset, dataBlockSize);

    return writeOffset;
}

inline size_t writeFrameAndUpdate(FILE *aviFile, FILE *indexFile, size_t *offset, const char *jpgFrame, size_t size) {
    //const size_t dataOffset = *offset;

    const size_t writeOffset = writeFrame(aviFile, indexFile, offset, jpgFrame, size);

    // Here is the FOURCC "JFIF" (JPEG header)
    // Overwrite "JFIF" (still images) with more appropriate "AVI1"
    //fseek(aviFile, dataOffset + 6, SEEK_SET);
    //fwrite("AVI1", 1, 4, aviFile);

    // Patch the sizes
    updateAVISize(aviFile, writeOffset);

    // Go back to the end of the file
    fseek(aviFile, writeOffset, SEEK_SET);

    return writeOffset;
}

inline void mergeAVIAndIndexFile(FILE *aviFile, FILE *indexFile, size_t *offset) {
    // Buffer containing the static parts that wont change: 00dc for compressed data and no flags
    static char buffer[16] = {'0', '0', 'd', 'c', 0x00, 0x00, 0x00, 0x00};
    static char *const overwriteBuf = buffer + 8;

    const size_t moviSize = *offset;

    // Create Index chunk by using the already existing buffer which offers enough space for the chunk header
    const size_t idxSizeOffset = CHUNK(overwriteBuf, offset, "idx1");

    // Lets write the index chunk
    fwrite(overwriteBuf, 1, RIFF_CHUNK_HEADER_SIZE, aviFile);

    // Read the index file from the beginning
    fseek(indexFile, 0, SEEK_SET);

    uint32_t size = 0;
    // Lets write the index entries
    for (; fread(overwriteBuf, 1, 8, indexFile); size += sizeof(buffer)) {
        fwrite(buffer, 1, sizeof(buffer), aviFile);
    }

    // Update the offset
    size_t writeOffset = (*offset += size);

    // Patch the idx1 chunk size
    PATCH_CHUNK_SIZE(aviFile, idxSizeOffset, size);

    // Patch movi size with size saved before starting to write idices
    patchNonChunkSize(aviFile, moviSize, AVI_MOVI_BLOCK_SIZE_OFFSET);

    // Patch the riff size
    patchNonChunkSize(aviFile, writeOffset, AVI_RIFF_BLOCK_SIZE_OFFSET);
}

inline void mergeAndPatch(FILE *aviFile, FILE *indexFile, size_t *offset, size_t framesTaken, size_t maxFrameBytes, size_t videoFPS) {
    mergeAVIAndIndexFile(aviFile, indexFile, offset);
    //TODO patch header fields
}

/*
    0xA0, 0x86, 0x01, 0x00,  // 1 000 00 => 0,1s
    0x80, 0x66, 0x01, 0x00,  // max. 91 776 bytes/second
     0x00, 0x00, 0x00, 0x00,
      0x10, 0x00, 0x00, 0x00,

  0x64, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00,

  0x80, 0x02, 0x00, 0x00,
   0xe0, 0x01, 0x00, 0x00,

    0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
*/

/*
 Fields that needs to be patched: maxBytesPerSec, totalFrames
 */
typedef struct _avimainheader {
    // time delay between frames in microseconds
    uint32_t microSecPerFrame = 1000000;
    // data rate of AVI data
    uint32_t maxBytesPerSec = 0;
    // padding multiple size, typically 2048
    uint32_t _paddingGranularity = 0;

    // parameter flags:
/* has idx1 chunk */
#define AVIF_HASINDEX 0x00000010
/* must use idx1 chunk to determine order */
#define AVIF_MUSTUSEINDEX 0x00000020
/* AVI file is interleaved */
#define AVIF_ISINTERLEAVED 0x00000100
#define AVIF_TRUSTCKTYPE 0x00000800
/* specially allocated used for capturing real time video */
#define AVIF_WASCAPTUREFILE 0x00010000
/* contains copyrighted data */
#define AVIF_COPYRIGHTED 0x00020000
    uint32_t flags = AVIF_HASINDEX;

    // number of video frames
    uint32_t totalFrames = 0;
    // number of preview frames
    uint32_t _initialFrames = 0;
    // number of data streams (1 or 2) here: 1 because we have only video/images
    uint32_t _streams = 1;
    // suggested playback buffer size in bytes
    uint32_t _suggestedBufferSize = 0;

    // width of video image in pixels
    uint32_t width = 0;
    // height of video image in pixels
    uint32_t height = 0;

    // time scale, typically 30
    // data rate(frame rate = data rate / time scale)
    // starting time, typically 0
    // size of AVI data chunk in time scale units
    uint32_t _reserved[4] = {0};
} AVIMainHeader;

/*
0x76, 0x69, 0x64, 0x73, // vids
0x4D, 0x4A, 0x50, 0x47, // MJPEG
0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 
0x00, 0x00,

0x00, 0x00, 0x00, 0x00,
 0x01, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
   
 0x0A, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
*/

#define AVI_STREAM_HEADER_FCCTYPE_AUDIO CONVERT_TO_FCC("auds")
#define AVI_STREAM_HEADER_FCCTYPE_MIDI CONVERT_TO_FCC("mids")
#define AVI_STREAM_HEADER_FCCTYPE_TEXT CONVERT_TO_FCC("txts")
#define AVI_STREAM_HEADER_FCCTYPE_VIDEO CONVERT_TO_FCC("vids")

/*
 Fields that needs to be patched: length
 */
typedef struct _AVIStreamHeader {
    uint32_t _fccType = AVI_STREAM_HEADER_FCCTYPE_VIDEO;
    uint32_t _fccHandler = CONVERT_TO_FCC("MJPG");
    uint32_t _flags = 0;
    uint16_t _priority = 0;
    uint16_t _language = 0;

    //Number of the frst block of the stream that is present in the file
    uint32_t _initialFrames = 0;
    /*
    rate / scale =
    samples / second (audio) or
    frames / second (video).
    */
    uint32_t scale = 1;
    uint32_t rate = 1;
    //Start time of stream. In the case of VBR audio, this value indicates the number of
    //silent frames to be played before the stream starts.
    uint32_t _start = 0;

    //size of stream in units as defned in rate and scale
    uint32_t length = 0;
    //Size of buffer necessary to store blocks of that stream. Can be 0 (in that case the application has to guess).
    uint32_t _suggestedBufferSize = 0;
    //should indicate the quality of the stream. Not important
    uint32_t _quality = 0;
    //number of bytes of one stream atom (that should not be split any further).
    uint32_t _sampleSize = 0;
} AVIStreamHeader;

/*
  0x28, 0x00, 0x00, 0x00,
   0x80, 0x02, 0x00, 0x00,  // 640
    0xe0, 0x01, 0x00, 0x00, // 480
     0x01, 0x00, 
     0x18, 0x00, // 24

   0x4D, 0x4A, 0x50, 0x47, //MJPEG
    0x00, 0x84, 0x03, 0x00, // 230.400
     0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,

   0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,

     0x49, 0x4E, 0x46, 0x4F, //INFO
      0x10, 0x00, 0x00, 0x00, // length
      
   0x6A, 0x61, 0x6D, 0x65,      //
    0x73, 0x7A, 0x61, 0x68,     // 'jameszahary v60 '
     0x61, 0x72, 0x79, 0x20,    //
      0x76, 0x36, 0x30, 0x20    //
*/
typedef struct _AVIStreamFormat {
    /*
    Specifies the number of bytes required by the structure.
    This value does not include the size of the color table
    or the size of the color masks, if they are appended to the end of structure.
    */
    uint32_t _biSize = sizeof(_AVIStreamFormat);
    // Specifies the width of the bitmap, in pixels
    uint32_t biWidth = 0;
    // Specifies the height of the bitmap, in pixels.
    uint32_t biHeight = 0;
    // Specifies the number of planes for the target device. This value must be set to 1.
    //uint16_t biPlanes = 1;
    /*
    Specifies the number of bits per pixel (bpp). 
    For uncompressed formats, this value is the average number of bits per pixel.
    For compressed formats, this value is the implied bit depth of the uncompressed image,
    after the image has been decoded.
    */
    //uint16_t biBitCount = 24;
    // We need to swap sides! (uint16_t + little Endian = PITA!)
    uint32_t _merged = (24 << 16) | 1;

    // For compressed video and YUV formats, this member is a FOURCC code, specified as a DWORD in little-endian order.
    uint32_t _biCompression = CONVERT_TO_FCC("MJPG");
    // Specifies the size, in bytes, of the image. This can be set to 0 for uncompressed RGB bitmaps.
    uint32_t _biSizeImage = 0;
    // Specifies the horizontal resolution, in pixels per meter, of the target device for the bitmap.
    uint32_t _biXPelsPerMeter = 0;
    // Specifies the vertical resolution, in pixels per meter, of the target device for the bitmap.
    uint32_t _biYPelsPerMete = 0;

    // Specifies the number of color indices in the color table that are actually used by the bitmap.
    uint32_t _biClrUsed = 0;
    // Specifies the number of color indices that are considered important for displaying the bitmap. If this value is zero, all colors are important.
    uint32_t _biClrImportant = 0;
} AVIStreamFormat;

inline size_t createAVI_File(FILE *outFile, const AVIMainHeader &aviHeader, const AVIStreamHeader &streamHeader, const AVIStreamFormat &streamFormat) {

#define AVI_BUFFER_SIZE 216

    char buffer[AVI_BUFFER_SIZE];

    size_t offset = 0;

    // RIFF start
    RIFF(buffer + offset, &offset);

    size_t hdrl_size_offset = LIST(buffer + offset, &offset, "hdrl");
    size_t avihBlockSize = CHUNK(buffer + offset, &offset, "avih", sizeof(aviHeader));

    writeStruct(buffer + offset, &aviHeader, sizeof(aviHeader));
    offset += sizeof(aviHeader);

    size_t strl_size_offset = LIST(buffer + offset, &offset, "strl");

    //Stream Header
    size_t strhBlockSize = CHUNK(buffer + offset, &offset, "strh", sizeof(streamHeader));
    writeStruct(buffer + offset, &streamHeader, sizeof(streamHeader));
    offset += sizeof(streamHeader);

    //Stream Format
    size_t strfBlockSize = CHUNK(buffer + offset, &offset, "strf", sizeof(streamFormat));
    writeStruct(buffer + offset, &streamFormat, sizeof(streamFormat));
    offset += sizeof(streamFormat);

    // Patch strl list
    size_t strlBlockSize = PATCH_LIST_RIFF_SIZE(buffer, strl_size_offset, strhBlockSize + strfBlockSize);
    // Patch hdrl list containing everything until here
    PATCH_LIST_RIFF_SIZE(buffer, hdrl_size_offset, strlBlockSize + avihBlockSize);

    // Create list which will contain the frames
    LIST(buffer + offset, &offset, "movi");

    // Ensure that we always have a valid avi file!
    updateAVISize(buffer, offset);

    // Finally lets write the file
    fwrite(buffer, 1, offset, outFile);

    return AVI_BUFFER_SIZE;
}

inline size_t createAVI_File(FILE *outFile, uint32_t biWidth, uint32_t biHeight, uint32_t rate, uint32_t scale = 1) {
    static AVIMainHeader tmp1;
    static AVIStreamHeader tmp2;
    static AVIStreamFormat tmp3;
    tmp1.width = tmp3.biWidth = biWidth;
    tmp1.height = tmp3.biHeight = biHeight;
    tmp2.scale = scale;
    tmp2.rate = rate;
    // (Seconds/Frames) * (Mircoseconds/Seconds) = Mircoseconds/Frame
    tmp1.microSecPerFrame = (scale * 1000000) / rate;

    return createAVI_File(outFile, tmp1, tmp2, tmp3);
}