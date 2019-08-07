/* 
 * Copyright (C) 2012-2014 Chris McClelland
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <makestuff.h>
#include <libfpgalink.h>
#include <libbuffer.h>
#include <liberror.h>
#include <libdump.h>
#include <argtable2.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <math.h>
#include <time.h>
#ifdef WIN32
#include <Windows.h>
#else
#include <sys/time.h>
#endif
#define BUZZ_SIZE 1024
bool sigIsRaised(void);
void sigRegisterHandler(void);
enum { MAXC = 512 };

int coord1[64]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
static const char *ptr;
static bool enableBenchmarking = false;
char * line2;
static bool isHexDigit(char ch) {
	return
		(ch >= '0' && ch <= '9') ||
		(ch >= 'a' && ch <= 'f') ||
		(ch >= 'A' && ch <= 'F');
}

static uint16 calcChecksum(const uint8 *data, size_t length) {
	uint16 cksum = 0x0000;
	while ( length-- ) {
		cksum = (uint16)(cksum + *data++);
	}
	return cksum;
}

static bool getHexNibble(char hexDigit, uint8 *nibble) {
	if ( hexDigit >= '0' && hexDigit <= '9' ) {
		*nibble = (uint8)(hexDigit - '0');
		return false;
	} else if ( hexDigit >= 'a' && hexDigit <= 'f' ) {
		*nibble = (uint8)(hexDigit - 'a' + 10);
		return false;
	} else if ( hexDigit >= 'A' && hexDigit <= 'F' ) {
		*nibble = (uint8)(hexDigit - 'A' + 10);
		return false;
	} else {
		return true;
	}
}

static int getHexByte(uint8 *byte) {
	uint8 upperNibble;
	uint8 lowerNibble;
	if ( !getHexNibble(ptr[0], &upperNibble) && !getHexNibble(ptr[1], &lowerNibble) ) {
		*byte = (uint8)((upperNibble << 4) | lowerNibble);
		byte += 2;
		return 0;
	} else {
		return 1;
	}
}

static const char *const errMessages[] = {
	NULL,
	NULL,
	"Unparseable hex number",
	"Channel out of range",
	"Conduit out of range",
	"Illegal character",
	"Unterminated string",
	"No memory",
	"Empty string",
	"Odd number of digits",
	"Cannot load file",
	"Cannot save file",
	"Bad arguments"
};

typedef enum {
	FLP_SUCCESS,
	FLP_LIBERR,
	FLP_BAD_HEX,
	FLP_CHAN_RANGE,
	FLP_CONDUIT_RANGE,
	FLP_ILL_CHAR,
	FLP_UNTERM_STRING,
	FLP_NO_MEMORY,
	FLP_EMPTY_STRING,
	FLP_ODD_DIGITS,
	FLP_CANNOT_LOAD,
	FLP_CANNOT_SAVE,
	FLP_ARGS
} ReturnCode;

static ReturnCode doRead(
	struct FLContext *handle, uint8 chan, uint32 length, FILE *destFile, uint16 *checksum,
	const char **error)
{
	ReturnCode retVal = FLP_SUCCESS;
	uint32 bytesWritten;
	FLStatus fStatus;
	uint32 chunkSize;
	const uint8 *recvData;
	uint32 actualLength;
	const uint8 *ptr;
	uint16 csVal = 0x0000;
	#define READ_MAX 65536

	// Read first chunk
	chunkSize = length >= READ_MAX ? READ_MAX : length;
	fStatus = flReadChannelAsyncSubmit(handle, chan, 1, NULL, error);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");
	length = length - chunkSize;

	while ( length ) {
		// Read chunk N
		chunkSize = length >= READ_MAX ? READ_MAX : length;
		fStatus = flReadChannelAsyncSubmit(handle, chan, chunkSize, NULL, error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");
		length = length - chunkSize;
		
		// Await chunk N-1
		fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");

		// Write chunk N-1 to file
		bytesWritten = (uint32)fwrite(recvData, 1, actualLength, destFile);
		CHECK_STATUS(bytesWritten != actualLength, FLP_CANNOT_SAVE, cleanup, "doRead()");

		// Checksum chunk N-1
		chunkSize = actualLength;
		ptr = recvData;
		while ( chunkSize-- ) {
			csVal = (uint16)(csVal + *ptr++);
		}
	}

	// Await last chunk
	fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, error);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doRead()");
	
	// Write last chunk to file
	bytesWritten = (uint32)fwrite(recvData, 1, actualLength, destFile);
	CHECK_STATUS(bytesWritten != actualLength, FLP_CANNOT_SAVE, cleanup, "doRead()");

	// Checksum last chunk
	chunkSize = actualLength;
	ptr = recvData;
	while ( chunkSize-- ) {
		csVal = (uint16)(csVal + *ptr++);
	}
	
	// Return checksum to caller
	*checksum = csVal;
cleanup:
	return retVal;
}

static ReturnCode doWrite(
	struct FLContext *handle, uint8 chan, FILE *srcFile, size_t *length, uint16 *checksum,
	const char **error)
{
	ReturnCode retVal = FLP_SUCCESS;
	size_t bytesRead, i;
	FLStatus fStatus;
	const uint8 *ptr;
	uint16 csVal = 0x0000;
	size_t lenVal = 0;
	#define WRITE_MAX (65536 - 5)
	uint8 buffer[WRITE_MAX];

	do {
		// Read Nth chunk
		bytesRead = fread(buffer, 1, WRITE_MAX, srcFile);
		if ( bytesRead ) {
			// Update running total
			lenVal = lenVal + bytesRead;

			// Submit Nth chunk
			fStatus = flWriteChannelAsync(handle, chan, bytesRead, buffer, error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doWrite()");

			// Checksum Nth chunk
			i = bytesRead;
			ptr = buffer;
			while ( i-- ) {
				csVal = (uint16)(csVal + *ptr++);
			}
		}
	} while ( bytesRead == WRITE_MAX );

	// Wait for writes to be received. This is optional, but it's only fair if we're benchmarking to
	// actually wait for the work to be completed.
	fStatus = flAwaitAsyncWrites(handle, error);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup, "doWrite()");

	// Return checksum & length to caller
	*checksum = csVal;
	*length = lenVal;
cleanup:
	return retVal;
}

static int parseLine(struct FLContext *handle, const char *line, const char **error) {
	ReturnCode retVal = FLP_SUCCESS, status;
	FLStatus fStatus;
	struct Buffer dataFromFPGA = {0,};
	BufferStatus bStatus;
	uint8 *data = NULL;
	char *fileName = NULL;
	FILE *file = NULL;
	double totalTime, speed;
	#ifdef WIN32
		LARGE_INTEGER tvStart, tvEnd, freq;
		DWORD_PTR mask = 1;
		SetThreadAffinityMask(GetCurrentThread(), mask);
		QueryPerformanceFrequency(&freq);
	#else
		struct timeval tvStart, tvEnd;
		long long startTime, endTime;
	#endif
	bStatus = bufInitialise(&dataFromFPGA, 1024, 0x00, error);
	CHECK_STATUS(bStatus, FLP_LIBERR, cleanup);
	ptr = line;
	do {
		while ( *ptr == ';' ) {
			ptr++;
		}
		switch ( *ptr ) {
		case 'r':{
			uint32 chan;
			uint32 length = 1;
			char *end;
			ptr++;
			
			// Get the channel to be read:
			errno = 0;
			chan = (uint32)strtoul(ptr, &end, 16);
			CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);

			// Ensure that it's 0-127
			CHECK_STATUS(chan > 127, FLP_CHAN_RANGE, cleanup);
			ptr = end;
			
			// Only three valid chars at this point:
			CHECK_STATUS(*ptr != '\0' && *ptr != ';' && *ptr != ' ', FLP_ILL_CHAR, cleanup);

			if ( *ptr == ' ' ) {
				ptr++;

				// Get the read count:
				errno = 0;
				length = (uint32)strtoul(ptr, &end, 16);
				CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);
				ptr = end;
				
				// Only three valid chars at this point:
				CHECK_STATUS(*ptr != '\0' && *ptr != ';' && *ptr != ' ', FLP_ILL_CHAR, cleanup);
				if ( *ptr == ' ' ) {
					const char *p;
					const char quoteChar = *++ptr;
					CHECK_STATUS(
						(quoteChar != '"' && quoteChar != '\''),
						FLP_ILL_CHAR, cleanup);
					
					// Get the file to write bytes to:
					ptr++;
					p = ptr;
					while ( *p != quoteChar && *p != '\0' ) {
						p++;
					}
					CHECK_STATUS(*p == '\0', FLP_UNTERM_STRING, cleanup);
					fileName = malloc((size_t)(p - ptr + 1));
					CHECK_STATUS(!fileName, FLP_NO_MEMORY, cleanup);
					CHECK_STATUS(p - ptr == 0, FLP_EMPTY_STRING, cleanup);
					strncpy(fileName, ptr, (size_t)(p - ptr));
					fileName[p - ptr] = '\0';
					ptr = p + 1;
				}
			}
			if ( fileName ) {
				uint16 checksum = 0x0000;

				// Open file for writing
				file = fopen(fileName, "wb");
				CHECK_STATUS(!file, FLP_CANNOT_SAVE, cleanup);
				free(fileName);
				fileName = NULL;

				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					status = doRead(handle, (uint8)chan, length, file, &checksum, error);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					status = doRead(handle, (uint8)chan, length, file, &checksum, error);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Read %d bytes (checksum 0x%04X) from channel %d at %f MiB/s\n",
						length, checksum, chan, speed);
				}
				CHECK_STATUS(status, status, cleanup);

				// Close the file
				fclose(file);
				file = NULL;
			} else {
				size_t oldLength = dataFromFPGA.length;
				bStatus = bufAppendConst(&dataFromFPGA, 0x00, length, error);
				CHECK_STATUS(bStatus, FLP_LIBERR, cleanup);
				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					fStatus = flReadChannel(handle, (uint8)chan, length, dataFromFPGA.data + oldLength, error);
		
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					fStatus = flReadChannel(handle, (uint8)chan, length, dataFromFPGA.data + oldLength, error);
					gettimeofday(&tvEnd, NULL);
					line2= *(dataFromFPGA.data + oldLength);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Read %d bytes (checksum 0x%04X) from channel %d at %f MiB/s\n",
						length, calcChecksum(dataFromFPGA.data + oldLength, length), chan, speed);
				}
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			}
			break;
		}
		case 'w':{
			unsigned long int chan;
			size_t length = 1, i;
			char *end, ch;
			const char *p;
			ptr++;
			
			// Get the channel to be written:
			errno = 0;
			chan = strtoul(ptr, &end, 16);
			CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);

			// Ensure that it's 0-127
			CHECK_STATUS(chan > 127, FLP_CHAN_RANGE, cleanup);
			ptr = end;

			// There must be a space now:
			CHECK_STATUS(*ptr != ' ', FLP_ILL_CHAR, cleanup);

			// Now either a quote or a hex digit
		   ch = *++ptr;
			if ( ch == '"' || ch == '\'' ) {
				uint16 checksum = 0x0000;

				// Get the file to read bytes from:
				ptr++;
				p = ptr;
				while ( *p != ch && *p != '\0' ) {
					p++;
				}
				CHECK_STATUS(*p == '\0', FLP_UNTERM_STRING, cleanup);
				fileName = malloc((size_t)(p - ptr + 1));
				CHECK_STATUS(!fileName, FLP_NO_MEMORY, cleanup);
				CHECK_STATUS(p - ptr == 0, FLP_EMPTY_STRING, cleanup);
				strncpy(fileName, ptr, (size_t)(p - ptr));
				fileName[p - ptr] = '\0';
				ptr = p + 1;  // skip over closing quote

				// Open file for reading
				file = fopen(fileName, "rb");
				CHECK_STATUS(!file, FLP_CANNOT_LOAD, cleanup);
				free(fileName);
				fileName = NULL;
				
				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					status = doWrite(handle, (uint8)chan, file, &length, &checksum, error);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					status = doWrite(handle, (uint8)chan, file, &length, &checksum, error);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Wrote "PFSZD" bytes (checksum 0x%04X) to channel %lu at %f MiB/s\n",
						length, checksum, chan, speed);
				}
				CHECK_STATUS(status, status, cleanup);

				// Close the file
				fclose(file);
				file = NULL;
			} else if ( isHexDigit(ch) ) {
				// Read a sequence of hex bytes to write
				uint8 *dataPtr;
				p = ptr + 1;
				while ( isHexDigit(*p) ) {
					p++;
				}
				CHECK_STATUS((p - ptr) & 1, FLP_ODD_DIGITS, cleanup);
				length = (size_t)(p - ptr) / 2;
				data = malloc(length);
				dataPtr = data;
				for ( i = 0; i < length; i++ ) {
					getHexByte(dataPtr++);
					ptr += 2;
				}
				#ifdef WIN32
					QueryPerformanceCounter(&tvStart);
					fStatus = flWriteChannel(handle, (uint8)chan, length, data, error);
					QueryPerformanceCounter(&tvEnd);
					totalTime = (double)(tvEnd.QuadPart - tvStart.QuadPart);
					totalTime /= freq.QuadPart;
					speed = (double)length / (1024*1024*totalTime);
				#else
					gettimeofday(&tvStart, NULL);
					fStatus = flWriteChannel(handle, (uint8)chan, length, data, error);
					gettimeofday(&tvEnd, NULL);
					startTime = tvStart.tv_sec;
					startTime *= 1000000;
					startTime += tvStart.tv_usec;
					endTime = tvEnd.tv_sec;
					endTime *= 1000000;
					endTime += tvEnd.tv_usec;
					totalTime = (double)(endTime - startTime);
					totalTime /= 1000000;  // convert from uS to S.
					speed = (double)length / (1024*1024*totalTime);
				#endif
				if ( enableBenchmarking ) {
					printf(
						"Wrote "PFSZD" bytes (checksum 0x%04X) to channel %lu at %f MiB/s\n",
						length, calcChecksum(data, length), chan, speed);
				}
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				free(data);
				data = NULL;
			} else {
				FAIL(FLP_ILL_CHAR, cleanup);
			}
			break;
		}
		case '+':{
			uint32 conduit;
			char *end;
			ptr++;

			// Get the conduit
			errno = 0;
			conduit = (uint32)strtoul(ptr, &end, 16);
			CHECK_STATUS(errno, FLP_BAD_HEX, cleanup);

			// Ensure that it's 0-127
			CHECK_STATUS(conduit > 255, FLP_CONDUIT_RANGE, cleanup);
			ptr = end;

			// Only two valid chars at this point:
			CHECK_STATUS(*ptr != '\0' && *ptr != ';', FLP_ILL_CHAR, cleanup);

			fStatus = flSelectConduit(handle, (uint8)conduit, error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			break;
		}
		default:
			FAIL(FLP_ILL_CHAR, cleanup);
		}
	} while ( *ptr == ';' );
	CHECK_STATUS(*ptr != '\0', FLP_ILL_CHAR, cleanup);

	dump(0x00000000, dataFromFPGA.data, dataFromFPGA.length);

cleanup:
	bufDestroy(&dataFromFPGA);
	if ( file ) {
		fclose(file);
	}
	free(fileName);
	free(data);
	if ( retVal > FLP_LIBERR ) {
		const int column = (int)(ptr - line);
		int i;
		fprintf(stderr, "%s at column %d\n  %s\n  ", errMessages[retVal], column, line);
		for ( i = 0; i < column; i++ ) {
			fprintf(stderr, " ");
		}
		fprintf(stderr, "^\n");
	}
	return retVal;
}

static const char *nibbles[] = {
	"0000",  // '0'
	"0001",  // '1'
	"0010",  // '2'
	"0011",  // '3'
	"0100",  // '4'
	"0101",  // '5'
	"0110",  // '6'
	"0111",  // '7'
	"1000",  // '8'
	"1001",  // '9'

	"XXXX",  // ':'
	"XXXX",  // ';'
	"XXXX",  // '<'
	"XXXX",  // '='
	"XXXX",  // '>'
	"XXXX",  // '?'
	"XXXX",  // '@'

	"1010",  // 'A'
	"1011",  // 'B'
	"1100",  // 'C'
	"1101",  // 'D'
	"1110",  // 'E'
	"1111"   // 'F'
};
////////////////////////////////////////////



///////////////////////////////////////////

////////////////////////////////////////////




int* recv(struct FLContext *handle,const char *error)
{
int a[32]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
for(int j=0;j<4;j++)
{
char *line;
int x1=1,y1=1;
/*line="r0 1 \"outp.dat\"";
//parseLine (handle, line, &error);
//FILE *fptr;
//char filename[8]={'o','u','t','p','.','d','a','t'};
fptr = fopen(filename, "r");
char buff[BUZZ_SIZE];
fgets(buff, BUZZ_SIZE, fptr);
int x1,y1;
if(isalpha(buff[0]))
	{
	x1=buff[0]-87;
	}
else 
	{
	x1=buff[0]-48;
	}
if(isalpha(buff[1]))
	{
	y1=buff[1]-87;
	}
else 
	{
	y1=buff[1]-48;
	}*/
int h=32-j*8;
for(int i=h-3;i<=h;i++)    
{    
	if(x1>0)
	a[i]=x1%2;
	else
	a[i]=0;x1=x1/2;  
} 
for(int i=h-7;i<=h-4;i++)    
{    
	if(y1>0)
	a[i]=y1%2;    
	else
	a[i]=0;y1=y1/2; 
} 
 //fclose(fptr);
}
return a;
}
///////////////////////////////////////

int* decToBinary(uint8 n)
{
    int binaryNum[8];
    for(int i=0;i<8;i++){
        binaryNum[i] = n % 2;
        n = (n/2);
    }
	return binaryNum;
}

//////////////////////////////////////////////////
static int * decrypt(int *d2)
{
	int *deco=d2;
	int i=0,n=0;
	int g[4];
	int x[32]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};     //x is the key k
	while(i!=32)
	{
		if(x[i]==0)
		{
   			n++;
   		}
   		i++;
	}
	g[3] = (x[0]+x[4]+x[8]+x[12]+x[16]+x[20]+x[24]+x[28]) % 2;
	g[2] = (x[1]+x[5]+x[9]+x[13]+x[17]+x[21]+x[25]+x[29]) % 2;
	g[1] = (x[2]+x[6]+x[10]+x[14]+x[18]+x[22]+x[26]+x[30]) % 2;
	g[0] = (x[3]+x[7]+x[11]+x[15]+x[19]+x[23]+x[27]+x[31]) % 2;
	int t[4],bs[4];
	bs[0]=1;bs[1]=1;bs[2]=1;bs[3]=1;                    //b is for 15
	int carry=0;
	for(int i = 3; i >= 0; i--)
	{
        	if((g[i] + bs[i] + carry) == 0)
		{
            	t[i] = 0;
            	carry = 0;
        	}
        	else if((g[i] + bs[i] + carry) == 1)
		{
            	t[i] = 1;
            	carry = 0;
        	}
        	else if((g[i] + bs[i] + carry) == 2)
		{
            	t[i] = 0;
            	carry = 1;
        	}
		else if((g[i] + bs[i] + carry) > 2)
		{
            	t[i] = 1;
            	carry = 1;
        	}
    	}
        for(int j=0;j<n;j++)
	{
		for(int l=0;l<32;l++)
		{
			if(deco[l]==t[l%4])
			{
				deco[l]=0;
			}
			else
			{
				deco[l]=1;
			}
		}

		carry=0;
		for(int i = 3; i >= 0; i--)
		{
			if(t[i] + bs[i] + carry == 0)
			{
	           	 	t[i] = 0;
	           	 	carry = 0;
			}
	        	else if(t[i] + bs[i] + carry == 1)
			{
	            		t[i] = 1;
	            		carry = 0;
        		}
        		else if(t[i] + bs[i] + carry == 2)
			{
            			t[i] = 0;
				carry = 1;
	        	}
	        	else if(t[i] + bs[i] + carry > 2)
			{
		            	t[i] = 1;
		            	carry = 1;
	        	}
	    	}
      	}
	return deco;
}
static int* encrypt(int* p)
{
	int *c=p;
	int i=0,n=0;
	int t[4];
	int x[32]={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};     //x is the key k
	while(i!=32)
	{
	        if(x[i]==1)
		{
	            n++;
	        }
	        i++;
	}
	t[3] = (x[0]+x[4]+x[8]+x[12]+x[16]+x[20]+x[24]+x[28]) % 2;
	t[2] = (x[1]+x[5]+x[9]+x[13]+x[17]+x[21]+x[25]+x[29]) % 2;
	t[1] = (x[2]+x[6]+x[10]+x[14]+x[18]+x[22]+x[26]+x[30]) % 2;
	t[0] = (x[3]+x[7]+x[11]+x[15]+x[19]+x[23]+x[27]+x[31]) % 2;
	int b[4];
	b[0]=0;b[1]=0;b[2]=0;b[3]=1;                    //b is for 15
	int carry=0;
	for(int j=0;j<n;j++)
	{
        	for(int l=0;l<32;l++)
		{
	         	if(c[l]==t[l%4])
			{
        		         c[l]=0;
        		}
            		else
			{
                		c[l]=1;
             		}
       	 	}
        	carry=0;
        	for(int i = 3; i >= 0; i--)
		{
			if(t[i] + b[i] + carry == 0)
			{
		                t[i] = 0;
		                carry = 0;
			}
			else if(t[i] + b[i] + carry == 1)
			{
		                t[i] = 1;
		                carry = 0;
			}
			else if(t[i] + b[i] + carry == 2)
			{
		                t[i] = 0;
		                carry = 1;
			}
			else if(t[i] + b[i] + carry > 2)
			{
		                t[i] = 1;
		                carry = 1;
			}
	        }
	}
	return c;
}
/////////////////////////
static char* getfield(char* line, int num)
{
    char* tok;
    for (tok = strtok(line, ",");
            tok && *tok;
            tok = strtok(NULL, ",\n"))
    {
        if (!--num)
            return tok;
    }
    return NULL;
}
///////////////////////////////////
static int* tablelook(int w,int v)
{
	int coordt[64]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
			0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
	int *coord=coordt;
	for(int i=0;i<8;i++)
	{
		char x= i+48; 
		char cox=w+48;
		char yox=v+48;
		char yd[5]={cox,',',yox,',',x};
		char xd[14]={'t','r','a','c','k','_','d','a','t','a','.','c','s','v'};
		char * argk[2]={xd,yd};
		char *cptr;
		int direction[3];
		int xx=i;int h=8*i;
		for(int k=2;k>=0;k--)
		{
			direction[k]=xx%2;
			xx=xx/2;
		}
		char buf[MAXC] = "";
		char *term = argk[1];
		FILE* fp = fopen(argk[0], "r");
		char line[1024];
		while (fgets(line, 1024, fp))
		{
		        char* tmp = strdup(line);char *s1 =getfield(tmp, 1);
			char* tmp1 = strdup(line);char *s2 =getfield(tmp1, 2);
			char* tmp2 = strdup(line);char *s3 =getfield(tmp2, 3);
			char* tmp3 = strdup(line);char *s4 =getfield(tmp3, 4);
			char* tmp4 = strdup(line);char *s5 =getfield(tmp4, 5);		
		        if(w==s1[0]-48 && v==s2[0]-48 && i==s3[0]-48) 
			{
				int nxs=s5[0]-48;
				int nxsb[3];
				for(int k=2;k>=0;k--)
				{
					nxsb[k]=nxs%2;
					nxs=nxs/2;
				}
				coord[63-h]=1;
				coord[62-h]=s4[0]-48;
				coord[61-h]=direction[0];
				coord[60-h]=direction[1];
				coord[59-h]=direction[2];
				coord[58-h]=nxsb[0];
				coord[57-h]=nxsb[1];
				coord[56-h]=nxsb[2];
				  break;        
			}
			else	
			{
				coord[63-h]=0;	
				coord[62-h]=0;
				coord[61-h]=direction[0];
				coord[60-h]=direction[1];
				coord[59-h]=direction[2];
				coord[58-h]=0;
				coord[57-h]=0;
				coord[56-h]=0;
			}
       		}
	}
	return coord;
}



////////////////////////////////////////////////////////////////
static void tableput(int a[8],int x,int y)
{
   	int direction=a[3]+a[4]*2+a[5]*4;
   	int nxtsig=a[0]+a[1]*2+a[2]*4;
	char vala6[1];
   	if(a[6]==0)
		vala6[0]='0';
	else
		vala6[0]='1'	;	
   	char yd[5]={x,',',y,',',direction};
   	char file[]="track_data.csv";
   	char file2[]="add.csv";
   	FILE* f = fopen(file, "r+");
   	FILE* j = fopen(file2,"w+");
   	char line[1024];
	char *aa;
	while (fgets(line, 1024, f))
	{
	        char* tmp = strdup(line);char *s1 =getfield(tmp, 1);
		char* tmp1 = strdup(line);char *s2 =getfield(tmp1, 2);
		char* tmp2 = strdup(line);char *s3 =getfield(tmp2, 3);
		char* tmp3 = strdup(line);char *s4 =getfield(tmp3, 4);
		char* tmp4 = strdup(line);char *s5 =getfield(tmp4, 5);
		if(x==s1[0]-48 && y==s2[0]-48 && direction==s3[0]-48)
		{
			char val[14]={x+48,',',y+48,',',direction+48,',',a[6]+48,',',nxtsig+48,'\n','\0','\0','\0','\0'};
			fputs(val,j);
		}
		if(x!=s1[0]-48 || y!=s2[0]-48 || direction!=s3[0]-48)
		{
			fputs(line,j);
		}
	}
	fclose(j);fclose(f);
	remove(file);
	rename(file2,file);
}

int main(int argc, char *argv[]) {
	ReturnCode retVal = FLP_SUCCESS, pStatus;
	struct arg_str *ivpOpt = arg_str0("i", "ivp", "<VID:PID>", "            vendor ID and product ID (e.g 04B4:8613)");
	struct arg_str *vpOpt = arg_str1("v", "vp", "<VID:PID[:DID]>", "       VID, PID and opt. dev ID (e.g 1D50:602B:0001)");
	struct arg_str *fwOpt = arg_str0("f", "fw", "<firmware.hex>", "        firmware to RAM-load (or use std fw)");
	struct arg_str *portOpt = arg_str0("d", "ports", "<bitCfg[,bitCfg]*>", " read/write digital ports (e.g B13+,C1-,B2?)");
	struct arg_str *queryOpt = arg_str0("q", "query", "<jtagBits>", "         query the JTAG chain");
	struct arg_str *progOpt = arg_str0("p", "program", "<config>", "         program a device");
	struct arg_uint *conOpt = arg_uint0("c", "conduit", "<conduit>", "        which comm conduit to choose (default 0x01)");
	struct arg_str *actOpt = arg_str0("a", "action", "<actionString>", "    a series of CommFPGA actions");
	struct arg_lit *shellOpt  = arg_lit0("s", "shell", "                    start up an interactive CommFPGA session");
	struct arg_lit *benOpt  = arg_lit0("b", "benchmark", "                enable benchmarking & checksumming");
	struct arg_lit *rstOpt  = arg_lit0("r", "reset", "                    reset the bulk endpoints");
	struct arg_str *dumpOpt = arg_str0("l", "dumploop", "<ch:file.bin>", "   write data from channel ch to file");
	struct arg_lit *helpOpt  = arg_lit0("h", "help", "                     print this help and exit");
	struct arg_str *eepromOpt  = arg_str0(NULL, "eeprom", "<std|fw.hex|fw.iic>", "   write firmware to FX2's EEPROM (!!)");
	struct arg_str *backupOpt  = arg_str0(NULL, "backup", "<kbitSize:fw.iic>", "     backup FX2's EEPROM (e.g 128:fw.iic)\n");
	struct arg_end *endOpt   = arg_end(20);
	void *argTable[] = {
		ivpOpt, vpOpt, fwOpt, portOpt, queryOpt, progOpt, conOpt, actOpt,
		shellOpt, benOpt, rstOpt, dumpOpt, helpOpt, eepromOpt, backupOpt, endOpt
	};
	const char *progName = "flcli";
	int numErrors;
	struct FLContext *handle = NULL;
	FLStatus fStatus;
	const char *error = NULL;
	const char *ivp = NULL;
	const char *vp = NULL;
	bool isNeroCapable, isCommCapable;
	uint32 numDevices, scanChain[16], i;
	const char *line = NULL;
	uint8 conduit = 0x01;
//////////////////////////////////////////


int ack2[32]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int * coor;
coor=recv(handle,error);
//////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////
	if ( arg_nullcheck(argTable) != 0 ) {
		fprintf(stderr, "%s: insufficient memory\n", progName);
		FAIL(1, cleanup);
	}

	numErrors = arg_parse(argc, argv, argTable);

	if ( helpOpt->count > 0 ) {
		printf("FPGALink Command-Line Interface Copyright (C) 2012-2014 Chris McClelland\n\nUsage: %s", progName);
		arg_print_syntax(stdout, argTable, "\n");
		printf("\nInteract with an FPGALink device.\n\n");
		arg_print_glossary(stdout, argTable,"  %-10s %s\n");
		FAIL(FLP_SUCCESS, cleanup);
	}

	if ( numErrors > 0 ) {
		arg_print_errors(stdout, endOpt, progName);
		fprintf(stderr, "Try '%s --help' for more information.\n", progName);
		FAIL(FLP_ARGS, cleanup);
	}

	fStatus = flInitialise(0, &error);
	CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);

	vp = vpOpt->sval[0];

	printf("Attempting to open connection to FPGALink device %s...\n", vp);
	fStatus = flOpen(vp, &handle, NULL);
	if ( fStatus ) {
		if ( ivpOpt->count ) {
			int count = 60;
			uint8 flag;
			ivp = ivpOpt->sval[0];
			printf("Loading firmware into %s...\n", ivp);
			if ( fwOpt->count ) {
				fStatus = flLoadCustomFirmware(ivp, fwOpt->sval[0], &error);
			} else {
				fStatus = flLoadStandardFirmware(ivp, vp, &error);
			}
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			
			printf("Awaiting renumeration");
			flSleep(1000);
			do {
				printf(".");
				fflush(stdout);
				fStatus = flIsDeviceAvailable(vp, &flag, &error);
				CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
				flSleep(250);
				count--;
			} while ( !flag && count );
			printf("\n");
			if ( !flag ) {
				fprintf(stderr, "FPGALink device did not renumerate properly as %s\n", vp);
				FAIL(FLP_LIBERR, cleanup);
			}

			printf("Attempting to open connection to FPGLink device %s again...\n", vp);
			fStatus = flOpen(vp, &handle, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		} else {
			fprintf(stderr, "Could not open FPGALink device at %s and no initial VID:PID was supplied\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	printf(
		"Connected to FPGALink device %s (firmwareID: 0x%04X, firmwareVersion: 0x%08X)\n",
		vp, flGetFirmwareID(handle), flGetFirmwareVersion(handle)
	);

	if ( eepromOpt->count ) {
		if ( !strcmp("std", eepromOpt->sval[0]) ) {
			printf("Writing the standard FPGALink firmware to the FX2's EEPROM...\n");
			fStatus = flFlashStandardFirmware(handle, vp, &error);
		} else {
			printf("Writing custom FPGALink firmware from %s to the FX2's EEPROM...\n", eepromOpt->sval[0]);
			fStatus = flFlashCustomFirmware(handle, eepromOpt->sval[0], &error);
		}
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
	}

	if ( backupOpt->count ) {
		const char *fileName;
		const uint32 kbitSize = strtoul(backupOpt->sval[0], (char**)&fileName, 0);
		if ( *fileName != ':' ) {
			fprintf(stderr, "%s: invalid argument to option --backup=<kbitSize:fw.iic>\n", progName);
			FAIL(FLP_ARGS, cleanup);
		}
		fileName++;
		printf("Saving a backup of %d kbit from the FX2's EEPROM to %s...\n", kbitSize, fileName);
		fStatus = flSaveFirmware(handle, kbitSize, fileName, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
	}

	if ( rstOpt->count ) {
		// Reset the bulk endpoints (only needed in some virtualised environments)
		fStatus = flResetToggle(handle, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
	}

	if ( conOpt->count ) {
		conduit = (uint8)conOpt->ival[0];
	}

	isNeroCapable = flIsNeroCapable(handle);
	isCommCapable = flIsCommCapable(handle, conduit);

	if ( portOpt->count ) {
		uint32 readState;
		char hex[9];
		const uint8 *p = (const uint8 *)hex;
		printf("Configuring ports...\n");
		fStatus = flMultiBitPortAccess(handle, portOpt->sval[0], &readState, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		sprintf(hex, "%08X", readState);
		printf("Readback:   28   24   20   16    12    8    4    0\n          %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf("  %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s", nibbles[*p++ - '0']);
		printf(" %s\n", nibbles[*p++ - '0']);
		flSleep(100);
	}

	if ( queryOpt->count ) {
		if ( isNeroCapable ) {
			fStatus = flSelectConduit(handle, 0x00, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = jtagScanChain(handle, queryOpt->sval[0], &numDevices, scanChain, 16, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			if ( numDevices ) {
				printf("The FPGALink device at %s scanned its JTAG chain, yielding:\n", vp);
				for ( i = 0; i < numDevices; i++ ) {
					printf("  0x%08X\n", scanChain[i]);
				}
			} else {
				printf("The FPGALink device at %s scanned its JTAG chain but did not find any attached devices\n", vp);
			}
		} else {
			fprintf(stderr, "JTAG chain scan requested but FPGALink device at %s does not support NeroProg\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	if ( progOpt->count ) {
		printf("Programming device...\n");
		if ( isNeroCapable ) {
			fStatus = flSelectConduit(handle, 0x00, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flProgram(handle, progOpt->sval[0], NULL, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		} else {
			fprintf(stderr, "Program operation requested but device at %s does not support NeroProg\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	if ( benOpt->count ) {
		enableBenchmarking = true;
	}
	
	if ( actOpt->count ) {
		printf("Executing CommFPGA actions on FPGALink device %s...\n", vp);
	
		if ( isCommCapable ) {
			uint8 isRunning;
			fStatus = flSelectConduit(handle, conduit, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flIsFPGARunning(handle, &isRunning, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			if ( isRunning ) {
				pStatus = parseLine(handle, actOpt->sval[0], &error);
				CHECK_STATUS(pStatus, pStatus, cleanup);
			} else {
				fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --program?\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
		} else {
			fprintf(stderr, "Action requested but device at %s does not support CommFPGA\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

	if ( dumpOpt->count ) {
	
		const char *fileName;
		unsigned long chan = strtoul(dumpOpt->sval[0], (char**)&fileName, 10);
		FILE *file = NULL;
		const uint8 *recvData;
		uint32 actualLength;
		if ( *fileName != ':' ) {
			fprintf(stderr, "%s: invalid argument to option -l|--dumploop=<ch:file.bin>\n", progName);
			FAIL(FLP_ARGS, cleanup);
		}
		fileName++;
		printf("Copying from channel %lu to %s", chan, fileName);
		file = fopen(fileName, "wb");
		CHECK_STATUS(!file, FLP_CANNOT_SAVE, cleanup);
		sigRegisterHandler();
		fStatus = flSelectConduit(handle, conduit, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		fStatus = flReadChannelAsyncSubmit(handle, (uint8)chan, 22528, NULL, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		do {
			fStatus = flReadChannelAsyncSubmit(handle, (uint8)chan, 22528, NULL, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fwrite(recvData, 1, actualLength, file);
			printf(".");
		} while ( !sigIsRaised() );
		printf("\nCaught SIGINT, quitting...\n");
		fStatus = flReadChannelAsyncAwait(handle, &recvData, &actualLength, &actualLength, &error);
		CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
		fwrite(recvData, 1, actualLength, file);
		fclose(file);
	}

	if ( shellOpt->count ) {
		printf("\nEntering CommFPGA command-line mode:\n");
		if ( isCommCapable ) {
		   uint8 isRunning;
			fStatus = flSelectConduit(handle, conduit, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			fStatus = flIsFPGARunning(handle, &isRunning, &error);
			CHECK_STATUS(fStatus, FLP_LIBERR, cleanup);
			if ( isRunning ) {


FLStatus fStatus;
uint8 a,b,c,d;
int ack1[32]={1,1,0,0,1,0,1,0,1,1,0,0,1,0,1,0,1,1,0,0,1,0,1,0,1,1,0,0,1,0,1,0};
int ack2[32]={1,1,1,1,1,0,1,0,1,1,0,0,1,0,1,0,1,1,0,0,1,0,1,0,1,1,0,0,1,0,1,0};
while(true)
{
	for(int rch=0;rch<63;rch++) //checks for 64 channels
	{
		printf("rch%d\n",rch);	
		fStatus=flReadChannel(handle, rch*2, 1, &a, error); //reads coordinates from board
		fStatus=flReadChannel(handle, rch*2, 1, &b, error);
		fStatus=flReadChannel(handle, rch*2, 1, &c, error);
		fStatus=flReadChannel(handle, rch*2, 1, &d, error);
		int d2[32];
		uint8 a22,b22,c22,d22;
		a22=a;b22=b;c22=c;d22=d;
	   	 for(int i=0;i<8;i++) //makes a 32 bit array of received data
		 {
			d2[i+24] = a22 % 2;d2[i+16]=b22%2;d2[i+8]=c22%2;d2[i]=d22%2;
	        	
	        	a22 = a22/2;b22=b22/2;c22=c22/2;d22=d22/2;
	   	 }
	
		int *d2_d=decrypt(d2); //decrypts the coordinates
		int wd=d2_d[4]*4+d2_d[3]*2+d2_d[2]; 
		int vd=d2_d[7]*4+d2_d[6]*2+d2_d[5];
		int *cc=encrypt(d2_d);
		uint8 cor11=0,cor22=0,cor33=0,cor44=0;////encrypt and then convert into uint
		for(int j=0;j<8;j++)
		{
			int sum=1;
			for(int k=0;k<j;k++)
			{
				sum=sum*2;
			}
			cor11=cor11+cc[31-j]*sum;
			cor22=cor22+cc[23-j]*sum;
			cor33=cor33+cc[15-j]*sum;
			cor44=cor44+cc[7-j]*sum;
		}
		fStatus = flWriteChannel(handle, rch*2+1, 1, &a, error); //sends the coordinates back
		fStatus = flWriteChannel(handle, rch*2+1, 1, &b, error);
		fStatus = flWriteChannel(handle, rch*2+1, 1, &c, error);
		fStatus = flWriteChannel(handle, rch*2+1, 1, &d, error);
		int loop2time=0;int countloop=0;
		while(loop2time<2) //check the ack1 from host for two attempts
		{
			uint8 a3,b3,c3,d3;
			fStatus=flReadChannel(handle, rch*2, 1, &a3, error); //receives ack1 from board
			fStatus=flReadChannel(handle, rch*2, 1, &b3, error);
			fStatus=flReadChannel(handle, rch*2, 1, &c3, error);
			fStatus=flReadChannel(handle, rch*2, 1, &d3, error);
			int *checkack2[32];
		 	for(int i=0;i<8;i++)
			{
				checkack2[i+24] = a3 % 2;checkack2[i+16]=b3%2;checkack2[i+8]=c3%2;checkack2[i]=d3%2;
	        	
	        	a3=a3/2;b3=b3/2;c3=c3/2;d3=d3/2;
	   	 	}
			int recheckack1[32];
			for(int i=0;i<32;i++)
			{
				recheckack1[i]=checkack2[i]; //checks received data with ack1
			}
			int *checkack1_de=decrypt(recheckack1);
			for(int i=0;i<32;i++)
			{
				if(checkack1_de[i]!=ack1[31-i])
				{countloop=1;break;}
			}
			if(countloop==1)
			{
				loop2time=loop2time+1;
			}
			else if(loop2time==0)
			{
				break;
			}
			else if(loop2time==1)
			{
				sleep(5);
			}

		}
	
		if(loop2time==2)//if it doesn't receive ack in two attempts then checks on next channel
		{
			continue;
		}
		int channel=rch*2;
		int * myarray=tablelook(wd,vd); //data corresponding to the coordinates is received in myarray
		int data1[32],data2[32];
	  	for(int ii=63;ii>=0;ii--)
		{	
		printf("%d",myarray[ii]);
		if(ii%8==0) printf("\n");
		}   
		for(int z=0;z<32;z++)
		{
		data1[z]=myarray[z];data2[z]=myarray[z+32];
		}
		int * data1en=encrypt(data1);//the data is divided into two chunks and encrypted
		int * data2en=encrypt(data2);
	
		uint8 b11=0,c11=0,d11=0,a11=0,e11=0,f11=0,g11=0,h11=0;
		for(int j=0;j<8;j++) //encrypted data is converted into uints
		{
			int sum=1;
			for(int k=0;k<j;k++)
			{
				sum=sum*2;
			}
			a11=a11+data2en[j+24]*sum;
			b11=b11+data2en[j+16]*sum;
			c11=c11+data2en[j+8]*sum;
			d11=d11+data2en[j]*sum;
			e11=e11+data1en[j+24]*sum;
			f11=f11+data1en[j+16]*sum;
			g11=g11+data1en[j+8]*sum;
			h11=h11+data1en[j]*sum;
		}
		int ack2_rev[32];  //ack2 is encrypted and sent to board
		for(int zz=0;zz<32;zz++)
		{
			ack2_rev[zz]=ack2[31-zz];
		}
		int * enack2=encrypt(ack2_rev);
		uint8 enack2_1=0,enack2_2=0,enack2_3=0,enack2_4=0;
		for(int j=0;j<8;j++)
		{
			int sum=1;
			for(int k=0;k<j;k++)
			{
				sum=sum*2;
			}
			enack2_1=enack2_1+enack2[j+24]*sum;
			enack2_2=enack2_2+enack2[j+16]*sum;
			enack2_3=enack2_3+enack2[j+8]*sum;
			enack2_4=enack2_4+enack2[j]*sum;
			
		}
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_1, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_2, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_3, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_4, error);
		//first four bytes of data is sent
		fStatus = flWriteChannel(handle, channel+1, 1, &a11, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &b11, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &c11, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &d11, error);

		int loop260=0;
		int ckack3=0;
		clock_t start, end;
		double cpu_time_used;
		start = clock();
		while(loop260==0) //reads ack1 and timeouts after some time
		{
			printf("done");
			end = clock();
			cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
			ckack3=0;
			if(cpu_time_used>256)
			{
				loop260=1;break;
			}
			else
			{
				uint8 ack3_1,ack3_2,ack3_3,ack3_4;
				fStatus=flReadChannel(handle, channel, 1, &ack3_1, error);
				fStatus=flReadChannel(handle, channel, 1, &ack3_2, error);
				fStatus=flReadChannel(handle, channel, 1, &ack3_3, error);
				fStatus=flReadChannel(handle, channel, 1, &ack3_4, error);
				int *checkack3[32];
				 for(int i=0;i<8;i++)
				 {
					checkack3[i+24] = ack3_1 % 2;checkack3[i+16]=ack3_2%2;checkack3[i+8]=ack3_3%2;checkack3[i]=ack3_4%2;
		        	
			        	ack3_1=ack3_1/2;ack3_2=ack3_2/2;ack3_3=ack3_3/2;ack3_4=ack3_4/2;
			   	 }
				int recheckack3[32];
				for(int i=0;i<32;i++){recheckack3[i]=checkack3[i];}
				int *checkack3_de=decrypt(recheckack3);
				for(int i=0;i<32;i++)
				{
					if(checkack3_de[i]!=ack1[31-i])
					ckack3=1;
				}
	
				if(ckack3==0)
				break;	
			}
		}
		if(loop260==1){continue;}
		fStatus = flWriteChannel(handle, channel+1, 1, &e11, error); //sends next four bytes of data
		fStatus = flWriteChannel(handle, channel+1, 1, &f11, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &g11, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &h11, error);
		start = clock();
		ckack3=0;
		loop260=0;
		while(loop260==0) //reads ack1 and timeouts after some time
		{
			printf("done1");
			end = clock();
			cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
			ckack3=0;
			if(cpu_time_used>256)
			{
				loop260=1;break;
			}
			else
			{
				uint8 ack3_1,ack3_2,ack3_3,ack3_4;
				fStatus=flReadChannel(handle, channel, 1, &ack3_1, error);
				fStatus=flReadChannel(handle, channel, 1, &ack3_2, error);
				fStatus=flReadChannel(handle, channel, 1, &ack3_3, error);
				fStatus=flReadChannel(handle, channel, 1, &ack3_4, error);
				int *checkack3[32];
				for(int i=0;i<8;i++)
			 	{
					checkack3[i+24] = ack3_1 % 2;checkack3[i+16]=ack3_2%2;checkack3[i+8]=ack3_3%2;checkack3[i]=ack3_4%2;
			        	
			        	ack3_1=ack3_1/2;ack3_2=ack3_2/2;ack3_3=ack3_3/2;ack3_4=ack3_4/2;
			   	 }
				int recheckack3[32];
				for(int i=0;i<32;i++)
				{
					recheckack3[i]=checkack3[i];
				}
				int *checkack3_de=decrypt(recheckack3);
				for(int i=0;i<32;i++)
				{
					if(checkack3_de[i]!=ack1[31-i])
					ckack3=1;
				}
				if(ckack3==0)
				break;	
			}
		}
		if(loop260==1){continue;}
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_1, error);//sends final ack2 
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_2, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_3, error);
		fStatus = flWriteChannel(handle, channel+1, 1, &enack2_4, error);
		
		uint8 dataupswitch_1,dataupswitch_2,dataupswitch_3,dataupswitch_4;
		start = clock();
		int ckackfordata=0;
		int looptimeout=0;
		int *checkackdata[32];
		while(looptimeout==0) //checks for ack before receiving data from slider switches
		{
			end = clock();
			cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
			ckackfordata=0;
			if(cpu_time_used>40)
			{
				looptimeout=1;break;
			}
			else
			{
				uint8 ackdata_1,ackdata_2,ackdata_3,ackdata_4;
				fStatus=flReadChannel(handle, channel, 1, &ackdata_1, error);
				 for(int i=0;i<8;i++)
				{
					checkackdata[i+24] = ackdata_1 % 2;checkackdata[i+16]=ackdata_2%2;
					checkackdata[i+8]=ackdata_3%2;checkackdata[i]=ackdata_4%2;
		        	
			        	ackdata_1=ackdata_1/2;ackdata_2=ackdata_2/2;ackdata_3=ackdata_3/2;ackdata_4=ackdata_4/2;
				}
				int recheckackdata[32];
				for(int i=0;i<32;i++)
				{
					recheckackdata[i]=checkackdata[i];
				}
				int *checkackdata_de=decrypt(recheckackdata);
				for(int i=24;i<32;i++)
				{
					if(checkackdata[i]!=1)
					{
						ckackfordata=1;
					}
				}
				if(ckackfordata==0)
					break;
			}
		}
		fStatus=flReadChannel(handle, channel, 1, &dataupswitch_1, error); //receives data from slider switches 
		fStatus=flReadChannel(handle, channel, 1, &dataupswitch_2, error);
		fStatus=flReadChannel(handle, channel, 1, &dataupswitch_3, error);
		fStatus=flReadChannel(handle, channel, 1, &dataupswitch_4, error);
		int *datarecv[32];
		for(int i=0;i<8;i++) //converts the data into an array
		{
			datarecv[i+24] = dataupswitch_1 % 2;datarecv[i+16]=dataupswitch_2%2;
			datarecv[i+8]=dataupswitch_3%2;datarecv[i]=dataupswitch_4%2;
		       	dataupswitch_1=dataupswitch_1/2;dataupswitch_2=dataupswitch_2/2;
			dataupswitch_3=dataupswitch_3/2;dataupswitch_4=dataupswitch_4/2;
		}
		int redatarecv[32];
		for(int i=0;i<32;i++)
		{
			redatarecv[i]=datarecv[i];
		}
		int *datarecv_de=decrypt(redatarecv);//decrypts the data received
		for(int i=0;i<32;i++)
		{
			printf("%d",datarecv_de[i]);
		}
		printf("\n");
		int a[8];
		for(int i=0;i<8;i++)
		{
			a[i]=datarecv_de[i];printf("%d",a[i]);
		}
		printf("\n");
		tableput(a,2,2);//modifies the table based upon data received
		printf("done");
	}
}

	

			
			} else {
				fprintf(stderr, "The FPGALink device at %s is not ready to talk - did you forget --xsvf?\n", vp);
				FAIL(FLP_ARGS, cleanup);
			}
		} else {
			fprintf(stderr, "Shell requested but device at %s does not support CommFPGA\n", vp);
			FAIL(FLP_ARGS, cleanup);
		}
	}

cleanup:
	free((void*)line);
	flClose(handle);
	if ( error ) {
		fprintf(stderr, "%s\n", error);
		flFreeError(error);
	}
	return retVal;
}
