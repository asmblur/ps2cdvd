struct MG_sectionStruct {
	unsigned int size;
	unsigned int flags;
	unsigned int key[2];
};

struct MG_BIT_struct {
	unsigned int secStart;
	unsigned int numSecs;
	struct MG_sectionStruct secs[0];
};

struct MG_headerStruct {
	unsigned char dummy1[16];
	unsigned int resultSize;
	unsigned short headerSize;
};
