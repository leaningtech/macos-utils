/*
 * The MIT License (MIT)
 * MIT License
 * 
 * Copyright (c) 2019 Leaning Technologies Ltd
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <arpa/inet.h>

class Record: public std::vector<uint8_t>
{
private:
	uint32_t curOffset;
public:
	Record():curOffset(0)
	{
	}
	void writeInt8(uint8_t v)
	{
		std::vector<uint8_t>& d = *this;
		if(curOffset + 1 >= d.size())
			d.resize(curOffset + 1);
		d[curOffset + 0] = v;
		curOffset++;
	}
	void writeInt16(uint16_t v)
	{
		std::vector<uint8_t>& d = *this;
		if(curOffset + 2 >= d.size())
			d.resize(curOffset + 2);
		d[curOffset + 1] = v;
		d[curOffset + 0] = v >> 8;
		curOffset += 2;
	}
	void writeInt32(uint32_t v)
	{
		std::vector<uint8_t>& d = *this;
		if(curOffset + 4 >= d.size())
			d.resize(curOffset + 4);
		d[curOffset + 3] = v;
		d[curOffset + 2] = v >> 8;
		d[curOffset + 1] = v >> 16;
		d[curOffset + 0] = v >> 24;
		curOffset += 4;
	}
	void writeStr(const char* s)
	{
		uint32_t strLen = strlen(s);
		std::vector<uint8_t>& d = *this;
		if(curOffset + strLen >= d.size())
			d.resize(curOffset + strLen);
		for(uint32_t i=0;i<strLen;i++)
			d[curOffset + i] = s[i];
		curOffset += strLen;
	}
	void seek(uint32_t o)
	{
		curOffset = o;
	}
};

int main(int argc, char* argv[])
{
	if(argc < 3)
	{
		printf("Usage %s output_file file.icns\n", argv[0]);
		return 1;
	}
	const char* outFileName = argv[1];
	const char* fileName = argv[2];
	FILE* f = fopen(fileName, "r");
	if(f == nullptr)
	{
		printf("File not found\n");
		return 1;
	}
	// Forge the resource map first, the first 16-bytes are also equal to the file header
	Record resMap;
	fseek(f, 0, SEEK_END);
	uint32_t fileLen = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint32_t resLen = fileLen + 4;
	// It seems that some space must be left alone for the "system"
	const uint32_t startOffset = 0x100;
	// The offset to the resource from the start of the file
	resMap.writeInt32(startOffset);
	// The end of the resource (start of the map)
	resMap.writeInt32(startOffset + resLen);
	// The lenght of the resource
	resMap.writeInt32(resLen);
	// We need to fixup the map size later on
	uint32_t mapSizePos = resMap.size();
	resMap.writeInt32(0);
	// Next map (not present)
	resMap.writeInt32(0);
	// File reference number
	// TODO: How is this determined?
	resMap.writeInt16(0xaa09);
	// Resource fork attributes
	resMap.writeInt16(0);
	// Offset from map start to type list, to fixup
	uint32_t mapToTypeListPos = resMap.size();
	resMap.writeInt16(0);
	// Offset from map start to name list, to fixup
	resMap.writeInt16(0);
	uint32_t typeListStartPos = resMap.size();
	// Number of types - 1
	resMap.writeInt16(0);
	// Type ID
	resMap.writeStr("icns");
	// Number of resources for this type - 1
	resMap.writeInt16(0);
	// Offset from type list start to res list, to fixup
	uint32_t typeListToResListPos = resMap.size();
	resMap.writeInt16(0);
	uint32_t resListStartPos = resMap.size();
	// Resource ID (is this fixed?)
	resMap.writeInt16(0xbfb9);
	// Offset to name (no name, so 0xffff)
	resMap.writeInt16(0xffff);
	// Atributes | Offset to data
	resMap.writeInt32(0);
	// Resource handle (is this fixed?)
	resMap.writeInt32(0xb0000000);
	// Fixup the full map length
	resMap.seek(mapSizePos);
	resMap.writeInt32(resMap.size());
	// Fixup the offset from the map to the type and name list
	// as we have no name list the offset is equal to the map size
	resMap.seek(mapToTypeListPos);
	resMap.writeInt16(typeListStartPos);
	resMap.writeInt16(resMap.size());
	// Fixup the last one
	resMap.seek(typeListToResListPos);
	resMap.writeInt16(resListStartPos - typeListStartPos);
	FILE* outFile = fopen(outFileName, "w");
	// Write out the header first
	fwrite(resMap.data(), 1, 16, outFile);
	// Skip the "system" reserved part
	fseek(outFile, startOffset, SEEK_SET);
	// The resource itself, plus the size as an header
	uint32_t beFileLen = htonl(fileLen);
	fwrite(&beFileLen, 1, 4, outFile);
	// Copy over the whole file
	uint8_t buf[1024];
	uint32_t r = 0;
	do
	{
		r = fread(buf, 1, 1024, f);
		fwrite(buf, 1, r, outFile);
	}
	while(r == 1024);
	// Copy over the map
	fwrite(resMap.data(), 1, resMap.size(), outFile);
	fclose(f);
	fclose(outFile);
	return 0;
}
