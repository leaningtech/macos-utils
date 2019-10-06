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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>
#include <vector>
#include <arpa/inet.h>

struct __attribute__((packed)) AliasFile
{
	uint32_t creatorCode;
	uint16_t recordSize;
	uint16_t recordVersion;
	uint16_t aliasKind;
	uint8_t volumeLenAndName[28];
	uint32_t volumeCreateDate;
	uint16_t volumeSig;
	uint16_t driveType;
	uint32_t parentInode;
	uint8_t fileLenAndName[64];
	uint32_t fileInode;
	uint32_t fileCreateDate;
	uint32_t fileType;
	uint32_t fileCreator;
	uint16_t fileFrom;
	uint16_t fileTo;
	uint32_t volumeAttributes;
	uint16_t volumeFs;
	uint8_t reserved[10];
	uint8_t extraData[0];
};

struct __attribute__((packed)) PctBRecord
{
	uint8_t type[4];
	uint32_t aliasLen;
	// Pad to 12 bytes
	uint32_t pad;
};

struct __attribute__((packed)) FinderWindowRecord
{
	uint16_t top;
	uint16_t left;
	uint16_t bottom;
	uint16_t right;
	uint8_t viewType[4];
	uint32_t pad;
};

struct __attribute__((packed)) Icv4Record
{
	uint8_t type[4];
	uint16_t iconSize;
	uint8_t arrangedBy[4];
	uint8_t labelPosition[4];
	uint8_t pad[12];
};

class Record: public std::vector<uint8_t>
{
protected:
	uint32_t curOffset;
public:
	Record(uint32_t size):std::vector<uint8_t>(size, 0),curOffset(0)
	{
	}
	void writeInt8(uint8_t v)
	{
		std::vector<uint8_t>& d = *this;
		d[curOffset] = v;
		curOffset++;
	}
	void writeInt16(uint16_t v)
	{
		std::vector<uint8_t>& d = *this;
		d[curOffset + 1] = v;
		d[curOffset + 0] = v >> 8;
		curOffset += 2;
	}
	void writeInt32(uint32_t v)
	{
		std::vector<uint8_t>& d = *this;
		d[curOffset + 3] = v;
		d[curOffset + 2] = v >> 8;
		d[curOffset + 1] = v >> 16;
		d[curOffset + 0] = v >> 24;
		curOffset += 4;
	}
	void writeStr(const char* s)
	{
		std::vector<uint8_t>& d = *this;
		uint32_t len = strlen(s);
		for(uint32_t i=0;i<len;i++)
			d[curOffset + i] = s[i];
		curOffset += len;
	}
	void writeData(const std::vector<uint8_t>& data)
	{
		std::vector<uint8_t>& d = *this;
		for(uint32_t i=0;i<data.size();i++)
			d[curOffset + i] = data[i];
		curOffset += data.size();
	}
	void seek(uint32_t o)
	{
		curOffset = o;
	}
};

class Block: public Record
{
private:
	const uint32_t addr;
public:
	Block(uint32_t addr, uint32_t size):Record(size),addr(addr)
	{
	}
	inline uint32_t getAddr() const
	{
		return addr;
	}
};

class BuddyAllocator
{
private:
	std::vector<Block> blocks;
	uint32_t curAddr;
	uint32_t powerOf2Ceil(uint32_t v)
	{
		v = v - 1;
		v |= (v >> 1);
		v |= (v >> 2);
		v |= (v >> 4);
		v |= (v >> 8);
		v |= (v >> 16);
		return v + 1;
	}
	uint32_t getLog2(uint32_t blockSize)
	{
		return 31 - __builtin_clz(blockSize);
	}
public:
	BuddyAllocator():curAddr(0)
	{
		// Allocate the buddy header
		allocateBlock(32);
		// Allocate the metaData block, we need this to be block 0
		allocateBlock(2048);
	}
	// Allocate a block of the given size and return the block id
	uint32_t allocateBlock(uint32_t size)
	{
		uint32_t blockSize = powerOf2Ceil(size);
		blocks.emplace_back(curAddr, blockSize);
		curAddr += blockSize;
		// The first 2 blocks are (header, metadata), valid indexes are > 0
		return blocks.size() - 2;
	}
	Block& getBlock(uint32_t blockId)
	{
		assert(blockId != 0xffffffff);
		return blocks.at(blockId + 1);
	}
	void createMetaDataBlock(uint32_t bTreeBlockId)
	{
		// Serialize allocator data into a new block
		// NOTE: The allocated list is 1024 bytes big, allocate 2048 bytes
		Block& metaData = blocks.at(1);
		metaData.writeInt32(blocks.size() - 1);
		metaData.writeInt32(0);
		// We need to populate 256 entries unconditionally
		for(uint32_t i=0;i<256;i++)
		{
			if(i < (blocks.size() - 1))
			{
				// Encoding is addr | log_2_blockSize
				Block& b = blocks[i + 1];
				uint32_t log2Size = getLog2(b.size());
				uint32_t addr = b.getAddr();
				assert((addr & 0x1f) == 0);
				metaData.writeInt32(addr | log2Size);
			}
			else
				metaData.writeInt32(0);
		}
		// Forge the directory, only 1 entry seems to exist
		metaData.writeInt32(1);
		metaData.writeInt8(4);
		metaData.writeStr("DSDB");
		metaData.writeInt32(bTreeBlockId);
		// Since we use a bump allocator each bucket in the free-list can only have 1 or 0 entries
		// Buckets are for 2^0 .. 2^31
		for(uint32_t i=0;i<32;i++)
		{
			uint32_t mask = 1<<i;
			if(curAddr & mask)
			{
				// Add an entry and bump the address
				metaData.writeInt32(1);
				metaData.writeInt32(curAddr);
				curAddr += mask;
			}
			else
			{
				metaData.writeInt32(0);
			}
		}
		assert(curAddr == 0);
		// Finalize the header block
		Block& header = blocks.at(0);
		header.writeStr("Bud1");
		header.writeInt32(metaData.getAddr());
		header.writeInt32(metaData.size());
		header.writeInt32(metaData.getAddr());
	}
	void writeFile(FILE* f)
	{
		// All the data is preceeded by an unaccounted 4-byte value (1)
		uint32_t preHeader = htonl(1);
		fwrite(&preHeader, 1, 4, f);
		// We can blindly output all the blocks, they are fully sequential
		for(Block& b: blocks)
			fwrite(b.data(), 1, b.size(), f);
	}
};

std::vector<uint8_t> createAliasFile(const char* volumeName, const char* fileName)
{
	// We need to include the full path, in the form volumeName:fileName 
	uint32_t volumeNameLen = strlen(volumeName);
	uint32_t fileNameLen = strlen(fileName);
	uint32_t fullPathSize = volumeNameLen + fileNameLen + 1;
	// We need to align the size to 2 (for the extra data)
	if(fullPathSize & 1)
		fullPathSize++;
	uint32_t recordSize = sizeof(AliasFile) + 8 + fullPathSize;
	std::vector<uint8_t> ret(recordSize, 0);
	AliasFile* aliasFile = (AliasFile*)ret.data();
	aliasFile->recordSize = htons(recordSize);
	aliasFile->recordVersion = htons(2);
	aliasFile->volumeLenAndName[0] = volumeNameLen;
	memcpy(aliasFile->volumeLenAndName + 1, volumeName, volumeNameLen);
	aliasFile->volumeSig = htons(0x482b); // H+
	// NOTE: Assuming root here
	aliasFile->parentInode = htonl(0x2);
	aliasFile->fileLenAndName[0] = fileNameLen;
	memcpy(aliasFile->fileLenAndName + 1, fileName, fileNameLen);
	aliasFile->fileInode = 0;
	aliasFile->fileFrom = htons(0xffff);
	aliasFile->fileTo = htons(0xffff);
	uint8_t* extraData = aliasFile->extraData;
	// '2' for absolute path
	uint16_t* extraData16 = (uint16_t*)extraData;
	// 16-bit '2' (big endian) for for absolute path
	extraData16[0] = htons(2);
	extraData16[1] = htons(fullPathSize);
	memcpy(extraData + 4, volumeName, volumeNameLen);
	extraData[4 + volumeNameLen] = ':';
	memcpy(extraData + 5 + volumeNameLen, fileName, fileNameLen);
	// End of extra data
	extraData[4 + fullPathSize] = 0xff;
	extraData[5 + fullPathSize] = 0xff;
	return ret;
}

uint32_t getInt(const char* f)
{
	char* endPtr;
	uint32_t ret = strtol(f, &endPtr, 10);
	if(*endPtr != '\0')
	{
		printf("Expected int: %s\n", f);
		exit(1);
	}
	// The whole string was parsed
	return ret;
}

class BTree
{
private:
	BuddyAllocator& buddy;
	uint32_t entryCount;
	uint32_t curPageId;
	void writeFileName(const char* s)
	{
		Block& b = buddy.getBlock(curPageId);
		uint32_t nameLen = strlen(s);
		b.writeInt32(nameLen);
		for(uint32_t i=0;i<nameLen;i++)
			b.writeInt16(s[i]);
	}
public:
	BTree(BuddyAllocator& a):buddy(a),entryCount(0),curPageId(0)
	{
		// Only 1 page is supported, should be plenty for our purpose
		// NOTE: Although the master block declares 4096 as the size, it seems that the leaf is 2048
		curPageId = buddy.allocateBlock(2048);
		Block& b = buddy.getBlock(curPageId);
		// Leave the first int32 to 0, that signals that this is a child leaf
		b.writeInt32(0);
		// Skip another 4 bytes, they will contain the record count
		b.writeInt32(0);
	}
	// NOTE: The user is responsible for adding values in lexicographical order
	void addBlob(const char* fileName, const char* recordType, std::vector<uint8_t>& data)
	{
		entryCount++;
		writeFileName(fileName);
		Block& b = buddy.getBlock(curPageId);
		b.writeStr(recordType);
		b.writeStr("blob");
		b.writeInt32(data.size());
		b.writeData(data);
	}
	void addBool(const char* fileName, const char* recordType, uint8_t v)
	{
		entryCount++;
		writeFileName(fileName);
		Block& b = buddy.getBlock(curPageId);
		b.writeStr(recordType);
		b.writeStr("bool");
		b.writeInt8(v);
	}
	void addShort(const char* fileName, const char* recordType, uint16_t v)
	{
		entryCount++;
		writeFileName(fileName);
		Block& b = buddy.getBlock(curPageId);
		b.writeStr(recordType);
		b.writeStr("shor");
		// Uses 4 bytes anyway
		b.writeInt32(v);
	}
	// Returns the page id of the master block
	uint32_t finish()
	{
		// Write the final entry count
		Block& b = buddy.getBlock(curPageId);
		b.seek(4);
		b.writeInt32(entryCount);
		// Create the master block for the Btree
		uint32_t rootBlockID = curPageId;
		curPageId = buddy.allocateBlock(20);
		Block& master = buddy.getBlock(curPageId);
		master.writeInt32(rootBlockID);
		// Tree depth = 0, we only support a single leaf page
		master.writeInt32(0);
		master.writeInt32(entryCount);
		// Total nodes = 1, a single leaf page
		master.writeInt32(1);
		// Page size
		master.writeInt32(4096);
		return curPageId;
	}
};

int main(int argc, char* argv[])
{
	if(argc < 8 || ((argc - 8) % 3) != 0)
	{
		printf("Usage: %s output_file bg.img bg_width bg_height volume_name icon_size text_size [file_name file_center_x file_center_y]+\n", argv[0]);
		return 1;
	}
	const char* outFileName = argv[1];
	const char* bgFileName = argv[2];
	const char* bgWidth = argv[3];
	const char* bgHeight = argv[4];
	const char* volumeName = argv[5];
	const char* iconSize = argv[6];
	const char* textSize = argv[7];
	// Create the alias file first, we need to know the size to build the Btree
	std::vector<uint8_t> aliasFile = createAliasFile(volumeName, bgFileName);
	BuddyAllocator buddy;
	BTree bTree(buddy);
	// Forge a PctB blob for the bg
	std::vector<uint8_t> PctB(12, 0);
	PctBRecord* pctBRecord = (PctBRecord*)PctB.data();
	memcpy(pctBRecord->type, "PctB", 4);
	pctBRecord->aliasLen = htonl(aliasFile.size());
	bTree.addBlob(".", "BKGD", PctB);
	bTree.addBool(".", "ICVO", 1);
	// Forge a Finder Window blob
	std::vector<uint8_t> Fw(16, 0);
	FinderWindowRecord* fw = (FinderWindowRecord*)Fw.data();
	fw->top = htons(200);
	fw->left = htons(300);
	fw->bottom = htons(200 + getInt(bgHeight));
	fw->right = htons(300 + getInt(bgWidth));
	memcpy(fw->viewType, "icnv", 4);
	bTree.addBlob(".", "fwi0", Fw);
	// Force an Icon View record
	std::vector<uint8_t> ivData(26, 0);
	Icv4Record* iv = (Icv4Record*)ivData.data();
	memcpy(iv->type, "icv4", 4);
	iv->iconSize = htons(getInt(iconSize));
	memcpy(iv->arrangedBy, "none", 4);
	memcpy(iv->labelPosition, "botm", 4);
	bTree.addBlob(".", "icvo", ivData);
	bTree.addShort(".", "icvt", getInt(textSize));
	bTree.addBlob(".", "pict", aliasFile);
	for(int i=8;i<argc;i+=3)
	{
		const char* fileName = argv[i];
		uint32_t centerX = getInt(argv[i+1]);
		uint32_t centerY = getInt(argv[i+2]);
		Record ilocRecord(16);
		ilocRecord.writeInt32(centerX);
		ilocRecord.writeInt32(centerY);
		ilocRecord.writeInt16(0xffff);
		ilocRecord.writeInt16(0xffff);
		ilocRecord.writeInt16(0xffff);
		bTree.addBlob(fileName, "Iloc", ilocRecord);
	}
	uint32_t bTreeBlockId = bTree.finish();
	buddy.createMetaDataBlock(bTreeBlockId);
	FILE* outFile = fopen(outFileName, "w");
	buddy.writeFile(outFile);
	fclose(outFile);
	return 0;
}
