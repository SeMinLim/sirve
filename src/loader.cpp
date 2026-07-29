#include "loader.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define ELF32_HEADER_SIZE 52
#define ELF32_PROGRAM_HEADER_SIZE 32

#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_RISCV 243

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_TLS 7

#define PF_X 0x1


typedef struct {
	uint32_t fileOffset;
	uint32_t virtualAddr;
	uint32_t physicalAddr;
	uint32_t fileSize;
	uint32_t memorySize;
	uint32_t flags;
	uint32_t align;
} Elf32LoadSegment;


static void clearError( LoaderError *error ) {
	if ( error == NULL ) return;
	error->message[0] = '\0';
}

static bool setError( LoaderError *error, const char *message ) {
	if ( error != NULL ) snprintf(error->message, sizeof(error->message), "%s", message);
	return false;
}

static bool setFormattedError( LoaderError *error, const char *format, const char *value ) {
	if ( error != NULL ) snprintf(error->message, sizeof(error->message), format, value);
	return false;
}

static uint16_t read16LE( const uint8_t *data, uint32_t offset ) {
	return (uint16_t)data[offset] |
	       ((uint16_t)data[offset + 1] << 8);
}

static uint32_t read32LE( const uint8_t *data, uint32_t offset ) {
	return (uint32_t)data[offset] |
	       ((uint32_t)data[offset + 1] << 8) |
	       ((uint32_t)data[offset + 2] << 16) |
	       ((uint32_t)data[offset + 3] << 24);
}

static bool isPowerOfTwo( uint32_t value ) {
	return value != 0 && (value & (value - 1u)) == 0;
}

static bool rangesOverlap(
	uint32_t firstStart,
	uint32_t firstEnd,
	uint32_t secondStart,
	uint32_t secondEnd
) {
	return firstStart < secondEnd && secondStart < firstEnd;
}

static bool readCompleteFile(
	const char *filename,
	uint8_t **fileData,
	uint32_t *fileSize,
	LoaderError *error
) {
	if ( fileData == NULL || fileSize == NULL ) {
		return setError(error, "ELF file destination is unavailable");
	}
	*fileData = NULL;
	*fileSize = 0;

	FILE *input = fopen(filename, "rb");
	if ( input == NULL ) {
		return setFormattedError(error, "Unable to open ELF32 executable: %s", filename);
	}
	if ( fseek(input, 0, SEEK_END) != 0 ) {
		fclose(input);
		return setError(error, "Unable to determine ELF32 file size");
	}
	long fileSizeLong = ftell(input);
	if ( fileSizeLong < 0 || fseek(input, 0, SEEK_SET) != 0 ) {
		fclose(input);
		return setError(error, "Unable to determine ELF32 file size");
	}
	if ( fileSizeLong == 0 ) {
		fclose(input);
		return setError(error, "ELF32 executable is empty");
	}
	if ( (unsigned long long)fileSizeLong > UINT32_MAX ) {
		fclose(input);
		return setError(error, "ELF32 executable is too large");
	}

	uint8_t *data = (uint8_t*)malloc((size_t)fileSizeLong);
	if ( data == NULL ) {
		fclose(input);
		return setError(error, "Unable to allocate ELF32 file buffer");
	}

	size_t readCnt = fread(data, 1, (size_t)fileSizeLong, input);
	if ( readCnt != (size_t)fileSizeLong ) {
		free(data);
		fclose(input);
		return setError(error, "Unable to read complete ELF32 executable");
	}
	if ( fclose(input) != 0 ) {
		free(data);
		return setError(error, "Unable to close ELF32 executable");
	}

	*fileData = data;
	*fileSize = (uint32_t)fileSizeLong;
	return true;
}

static bool validateElf32Header(
	const uint8_t *fileData,
	uint32_t fileSize,
	uint32_t *entryAddr,
	uint32_t *programHeaderOffset,
	uint16_t *programHeaderCnt,
	LoaderError *error
) {
	if ( fileSize < ELF32_HEADER_SIZE ) {
		return setError(error, "ELF32 header is truncated");
	}
	if ( fileData[0] != 0x7f || fileData[1] != 'E' || fileData[2] != 'L' ||
	     fileData[3] != 'F' ) {
		return setError(error, "Invalid ELF magic");
	}
	if ( fileData[4] != ELFCLASS32 ) {
		return setError(error, "ELF executable is not 32-bit");
	}
	if ( fileData[5] != ELFDATA2LSB ) {
		return setError(error, "ELF32 executable is not little-endian");
	}
	if ( fileData[6] != EV_CURRENT ) {
		return setError(error, "Unsupported ELF identification version");
	}
	if ( read16LE(fileData, 16) != ET_EXEC ) {
		return setError(error, "ELF32 file is not an executable");
	}
	if ( read16LE(fileData, 18) != EM_RISCV ) {
		return setError(error, "ELF32 executable is not for RISC-V");
	}
	if ( read32LE(fileData, 20) != EV_CURRENT ) {
		return setError(error, "Unsupported ELF32 executable version");
	}
	if ( read32LE(fileData, 36) != 0 ) {
		return setError(error, "Unsupported RISC-V ELF flags");
	}
	if ( read16LE(fileData, 40) != ELF32_HEADER_SIZE ) {
		return setError(error, "Unexpected ELF32 header size");
	}
	if ( read16LE(fileData, 42) != ELF32_PROGRAM_HEADER_SIZE ) {
		return setError(error, "Unexpected ELF32 program-header size");
	}

	uint32_t phOffset = read32LE(fileData, 28);
	uint16_t phCnt = read16LE(fileData, 44);
	if ( phCnt == 0 ) {
		return setError(error, "ELF32 executable has no program headers");
	}
	uint64_t phEnd = (uint64_t)phOffset +
	                 ((uint64_t)phCnt * ELF32_PROGRAM_HEADER_SIZE);
	if ( phOffset > fileSize || phEnd > fileSize ) {
		return setError(error, "ELF32 program-header table is truncated");
	}

	*entryAddr = read32LE(fileData, 24);
	*programHeaderOffset = phOffset;
	*programHeaderCnt = phCnt;
	return true;
}

static void sortLoadSegments( Elf32LoadSegment *segments, uint32_t segmentCnt ) {
	for ( uint32_t i = 1; i < segmentCnt; i ++ ) {
		Elf32LoadSegment value = segments[i];
		uint32_t position = i;
		while ( position > 0 && segments[position - 1].virtualAddr > value.virtualAddr ) {
			segments[position] = segments[position - 1];
			position --;
		}
		segments[position] = value;
	}
}


bool loadRawBinary(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	uint32_t loadAddr,
	uint32_t entryAddr,
	RawBinaryResult *result,
	LoaderError *error
) {
	clearError(error);
	if ( result != NULL ) memset(result, 0, sizeof(*result));
	if ( filename == NULL || filename[0] == '\0' ) {
		return setError(error, "Raw binary filename is missing");
	}
	if ( memory == NULL || memorySize == 0 ) {
		return setError(error, "Emulated memory is unavailable");
	}
	if ( (loadAddr & 0x3u) != 0 ) {
		return setError(error, "Raw binary load address is not 4-byte aligned");
	}
	if ( (entryAddr & 0x3u) != 0 ) {
		return setError(error, "Raw binary entry address is not 4-byte aligned");
	}
	if ( loadAddr >= memorySize ) {
		return setError(error, "Raw binary load address is outside emulated memory");
	}

	FILE *input = fopen(filename, "rb");
	if ( input == NULL ) {
		return setFormattedError(error, "Unable to open raw binary: %s", filename);
	}
	if ( fseek(input, 0, SEEK_END) != 0 ) {
		fclose(input);
		return setError(error, "Unable to determine raw binary size");
	}
	long fileSizeLong = ftell(input);
	if ( fileSizeLong < 0 || fseek(input, 0, SEEK_SET) != 0 ) {
		fclose(input);
		return setError(error, "Unable to determine raw binary size");
	}
	if ( fileSizeLong == 0 ) {
		fclose(input);
		return setError(error, "Raw binary is empty");
	}
	if ( (unsigned long long)fileSizeLong > UINT32_MAX ) {
		fclose(input);
		return setError(error, "Raw binary is too large");
	}

	uint32_t fileSize = (uint32_t)fileSizeLong;
	if ( fileSize > memorySize - loadAddr ) {
		fclose(input);
		return setError(error, "Raw binary exceeds emulated memory");
	}
	uint32_t loadedEnd = loadAddr + fileSize;
	if ( fileSize < 4 || entryAddr < loadAddr || entryAddr >= loadedEnd ||
	     4 > loadedEnd - entryAddr ) {
		fclose(input);
		return setError(error, "Raw binary entry address is outside the loaded image");
	}

	size_t readCnt = fread(memory + loadAddr, 1, fileSize, input);
	if ( readCnt != fileSize ) {
		fclose(input);
		return setError(error, "Unable to read complete raw binary");
	}
	if ( fclose(input) != 0 ) {
		return setError(error, "Unable to close raw binary");
	}

	if ( result != NULL ) {
		result->loadAddr = loadAddr;
		result->entryAddr = entryAddr;
		result->loadedByteCnt = fileSize;
		result->loadedEnd = loadedEnd;
	}
	return true;
}

bool loadElf32(
	const char *filename,
	uint8_t *memory,
	uint32_t memorySize,
	Elf32Result *result,
	LoaderError *error
) {
	clearError(error);
	if ( result != NULL ) memset(result, 0, sizeof(*result));
	if ( filename == NULL || filename[0] == '\0' ) {
		return setError(error, "ELF32 filename is missing");
	}
	if ( memory == NULL || memorySize == 0 ) {
		return setError(error, "Emulated memory is unavailable");
	}

	uint8_t *fileData = NULL;
	uint32_t fileSize = 0;
	if ( !readCompleteFile(filename, &fileData, &fileSize, error) ) return false;

	uint32_t entryAddr = 0;
	uint32_t phOffset = 0;
	uint16_t phCnt = 0;
	if ( !validateElf32Header(fileData, fileSize, &entryAddr, &phOffset, &phCnt, error) ) {
		free(fileData);
		return false;
	}
	if ( (entryAddr & 0x3u) != 0 ) {
		free(fileData);
		return setError(error, "ELF32 entry address is not 4-byte aligned");
	}

	Elf32LoadSegment *segments = (Elf32LoadSegment*)calloc(phCnt, sizeof(Elf32LoadSegment));
	if ( segments == NULL ) {
		free(fileData);
		return setError(error, "Unable to allocate ELF32 segment table");
	}

	uint32_t segmentCnt = 0;
	for ( uint32_t i = 0; i < phCnt; i ++ ) {
		uint32_t headerOffset = phOffset + (i * ELF32_PROGRAM_HEADER_SIZE);
		uint32_t type = read32LE(fileData, headerOffset);
		if ( type == PT_DYNAMIC || type == PT_INTERP ) {
			free(segments);
			free(fileData);
			return setError(error, "Dynamic ELF32 executables are not supported");
		}
		if ( type == PT_TLS ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 thread-local storage is not supported");
		}
		if ( type != PT_LOAD ) continue;

		Elf32LoadSegment segment;
		segment.fileOffset = read32LE(fileData, headerOffset + 4);
		segment.virtualAddr = read32LE(fileData, headerOffset + 8);
		segment.physicalAddr = read32LE(fileData, headerOffset + 12);
		segment.fileSize = read32LE(fileData, headerOffset + 16);
		segment.memorySize = read32LE(fileData, headerOffset + 20);
		segment.flags = read32LE(fileData, headerOffset + 24);
		segment.align = read32LE(fileData, headerOffset + 28);

		if ( segment.fileSize > segment.memorySize ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 segment file size exceeds memory size");
		}
		if ( segment.fileOffset > fileSize || segment.fileSize > fileSize - segment.fileOffset ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 segment exceeds the input file");
		}
		if ( segment.memorySize > memorySize || segment.virtualAddr > memorySize - segment.memorySize ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 segment exceeds emulated memory");
		}
		if ( segment.physicalAddr != 0 && segment.physicalAddr != segment.virtualAddr ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 virtual and physical load addresses differ");
		}
		if ( segment.align > 1 ) {
			if ( !isPowerOfTwo(segment.align) ||
			     (segment.virtualAddr & (segment.align - 1u)) !=
			     (segment.fileOffset & (segment.align - 1u)) ) {
				free(segments);
				free(fileData);
				return setError(error, "ELF32 segment alignment is invalid");
			}
		}
		if ( (segment.flags & PF_X) != 0 ) {
			if ( (segment.virtualAddr & 0x3u) != 0 ) {
				free(segments);
				free(fileData);
				return setError(error, "Executable ELF32 segment is not 4-byte aligned");
			}
		}
		if ( segment.memorySize == 0 ) continue;

		segments[segmentCnt] = segment;
		segmentCnt ++;
	}

	if ( segmentCnt == 0 ) {
		free(segments);
		free(fileData);
		return setError(error, "ELF32 executable has no loadable segments");
	}
	sortLoadSegments(segments, segmentCnt);

	uint32_t imageStart = segments[0].virtualAddr;
	uint32_t imageEnd = segments[0].virtualAddr + segments[0].memorySize;
	uint32_t executableStart = 0;
	uint32_t executableLimit = 0;
	uint32_t loadedByteCnt = 0;
	uint32_t zeroByteCnt = 0;
	bool hasExecutableSegment = false;
	bool entryInExecutableFileData = false;

	for ( uint32_t i = 0; i < segmentCnt; i ++ ) {
		uint32_t segmentStart = segments[i].virtualAddr;
		uint32_t segmentEnd = segmentStart + segments[i].memorySize;
		if ( i > 0 && rangesOverlap(segments[i - 1].virtualAddr,
		                             segments[i - 1].virtualAddr + segments[i - 1].memorySize,
		                             segmentStart,
		                             segmentEnd) ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 loadable segments overlap");
		}
		if ( segmentStart < imageStart ) imageStart = segmentStart;
		if ( segmentEnd > imageEnd ) imageEnd = segmentEnd;
		if ( UINT32_MAX - loadedByteCnt < segments[i].fileSize ||
		     UINT32_MAX - zeroByteCnt < segments[i].memorySize - segments[i].fileSize ) {
			free(segments);
			free(fileData);
			return setError(error, "ELF32 segment byte count overflows");
		}
		loadedByteCnt += segments[i].fileSize;
		zeroByteCnt += segments[i].memorySize - segments[i].fileSize;

		if ( (segments[i].flags & PF_X) != 0 ) {
			if ( !hasExecutableSegment ) {
				executableStart = segmentStart;
				executableLimit = segmentEnd;
				hasExecutableSegment = true;
			} else {
				if ( segmentStart != executableLimit ) {
					free(segments);
					free(fileData);
					return setError(error, "Executable ELF32 segments are not contiguous");
				}
				executableLimit = segmentEnd;
			}
			uint32_t fileDataEnd = segmentStart + segments[i].fileSize;
			if ( entryAddr >= segmentStart && entryAddr < fileDataEnd &&
			     fileDataEnd - entryAddr >= 4 ) {
				entryInExecutableFileData = true;
			}
		}
	}

	if ( !hasExecutableSegment ) {
		free(segments);
		free(fileData);
		return setError(error, "ELF32 executable has no executable load segment");
	}
	if ( !entryInExecutableFileData ) {
		free(segments);
		free(fileData);
		return setError(error, "ELF32 entry address is outside executable file data");
	}

	for ( uint32_t i = 0; i < segmentCnt; i ++ ) {
		memcpy(memory + segments[i].virtualAddr,
		       fileData + segments[i].fileOffset,
		       segments[i].fileSize);
		memset(memory + segments[i].virtualAddr + segments[i].fileSize,
		       0,
		       segments[i].memorySize - segments[i].fileSize);
	}

	if ( result != NULL ) {
		result->entryAddr = entryAddr;
		result->imageStart = imageStart;
		result->imageEnd = imageEnd;
		result->executableStart = executableStart;
		result->executableLimit = executableLimit;
		result->segmentCnt = segmentCnt;
		result->loadedByteCnt = loadedByteCnt;
		result->zeroByteCnt = zeroByteCnt;
	}

	free(segments);
	free(fileData);
	return true;
}
