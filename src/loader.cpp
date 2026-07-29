#include "loader.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>


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
	if ( (unsigned long)fileSizeLong > UINT32_MAX ) {
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
