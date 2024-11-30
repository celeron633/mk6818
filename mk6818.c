#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BLKSIZE						(512)

#define SECBOOT_NSIH_POSITION		(1)
#define SECBOOT_POSITION			(2)
#define BOOTLOADER_NSIH_POSITION	(64)
#define BOOTLOADER_POSITION			(65)

#define MAX_BUFFER_SIZE				(32 * 1024 * 1024)

struct nand_bootinfo_t
{
	uint8_t	addrstep;
	uint8_t	tcos;
	uint8_t	tacc;
	uint8_t	toch;
	uint32_t pagesize;
	uint32_t crc32;
};

struct spi_bootinfo_t
{
	uint8_t	addrstep;
	uint8_t	reserved0[3];
	uint32_t reserved1;
	uint32_t crc32;
};

struct sdmmc_bootinfo_t
{
	uint8_t	portnumber;
	uint8_t	reserved0[3];
	uint32_t reserved1;
	uint32_t crc32;
};

struct sdfs_bootinfo_t
{
	char bootfile[12];
};

union device_bootinfo_t
{
	struct nand_bootinfo_t nandbi;
	struct spi_bootinfo_t spibi;
	struct sdmmc_bootinfo_t sdmmcbi;
	struct sdfs_bootinfo_t sdfsbi;
};

struct ddr_initinfo_t
{
	uint8_t	chipnum;
	uint8_t	chiprow;
	uint8_t	buswidth;
	uint8_t	reserved0;

	uint16_t chipmask;
	uint16_t chipbase;

	uint8_t	cwl;
	uint8_t	wl;
	uint8_t	rl;
	uint8_t	ddrrl;

	uint32_t phycon4;
	uint32_t phycon6;

	uint32_t timingaref;
	uint32_t timingrow;
	uint32_t timingdata;
	uint32_t timingpower;
};

struct boot_info_t
{
	uint32_t vector[8];					// 0x000 ~ 0x01C
	uint32_t vector_rel[8];				// 0x020 ~ 0x03C

	uint32_t deviceaddr;				// 0x040
	uint32_t loadsize;					// 0x044
	uint32_t loadaddr;					// 0x048
	uint32_t launchaddr;				// 0x04C

	union device_bootinfo_t dbi;		// 0x050 ~ 0x058

	uint32_t pll[4];					// 0x05C ~ 0x068
	uint32_t pllspread[2];				// 0x06C ~ 0x070
	uint32_t dvo[5];					// 0x074 ~ 0x084

	struct ddr_initinfo_t dii;			// 0x088 ~ 0x0A8

	uint32_t axibottomslot[32];			// 0x0AC ~ 0x128
	uint32_t axidisplayslot[32];		// 0x12C ~ 0x1A8

	uint32_t stub[(0x1F8 - 0x1A8) / 4];	// 0x1AC ~ 0x1F8
	uint32_t signature;					// 0x1FC "NSIH"
};

int write_buf_to_file(char *buf, size_t len, const char *fname);
void write_nsih2_with_uboot(const char *raw_buffer, size_t buffer_len, size_t uboot_file_len, const char *out_file_name);
void write_nsih1_with_bl1(const char *raw_buffer, size_t buffer_len, size_t bl1_file_len, const char *out_file_name);

static int process_nsih(const char * filename, unsigned char * outdata)
{
	FILE * fp;
	char ch;
	int writesize, skipline, line, bytesize, i;
	unsigned int writeval;

	fp = fopen(filename, "r+b");
	if(!fp)
	{
		printf("Failed to open %s file.\n", filename);
		return 0;
	}

	bytesize = 0;
	writeval = 0;
	writesize = 0;
	skipline = 0;
	line = 0;

	while(0 == feof(fp))
	{
		ch = fgetc (fp);

		if (skipline == 0)
		{
			if (ch >= '0' && ch <= '9')
			{
				writeval = writeval * 16 + ch - '0';
				writesize += 4;
			}
			else if (ch >= 'a' && ch <= 'f')
			{
				writeval = writeval * 16 + ch - 'a' + 10;
				writesize += 4;
			}
			else if (ch >= 'A' && ch <= 'F')
			{
				writeval = writeval * 16 + ch - 'A' + 10;
				writesize += 4;
			}
			else
			{
				if(writesize == 8 || writesize == 16 || writesize == 32)
				{
					for(i=0 ; i<writesize/8 ; i++)
					{
						outdata[bytesize++] = (unsigned char)(writeval & 0xFF);
						writeval >>= 8;
					}
				}
				else
				{
					if (writesize != 0)
						printf("Error at %d line.\n", line + 1);
				}

				writesize = 0;
				skipline = 1;
			}
		}

		if(ch == '\n')
		{
			line++;
			skipline = 0;
			writeval = 0;
		}
	}

	printf("NSIH : %d line processed.\n", line + 1);
	printf("NSIH : %d bytes generated.\n", bytesize);

	fclose(fp);
	return bytesize;
}

static char * to_readable_msg(char * buf, int len)
{
    static char msg[4096];
	int i, n;

	for(i = 0; i < len; i++)
	{
		n = i % 5;
		if(n == 0)
			buf[i] ^= 0x24;
		else if (n == 1)
			buf[i] ^= 0x36;
		else if (n == 2)
			buf[i] ^= 0xAC;
		else if (n == 3)
			buf[i] ^= 0xB2;
		else if (n == 4)
			buf[i] ^= 0x58;
	}

    memset(msg, 0, sizeof(msg));
    memcpy(msg, buf, len);
    return msg;
}

int main(int argc, char *argv[])
{
	FILE * fp;
	struct boot_info_t * bi;
	unsigned char nsih[512];
	char * buffer;
	int length, reallen;
	int nbytes, filelen;

	char nsih1_with_bl1_fname[64];
	char nsih2_with_uboot_fname[64];
	sprintf(nsih1_with_bl1_fname, "nsih1_with_bl1_%s", argv[1]);
	sprintf(nsih2_with_uboot_fname, "nsih2_with_uboot_%s", argv[1]);

	if(argc < 5)
	{
		printf("Usage: mk6818 <destination> <nsih> <2ndboot> <bootloader> [is64BitMode]\n");
		return -1;
	}

	if(process_nsih(argv[2], &nsih[0]) != 512)
		return -1;

	buffer = malloc(MAX_BUFFER_SIZE);
	memset(buffer, 0, MAX_BUFFER_SIZE);

	/* 2ndboot nsih */
	memcpy(&buffer[(SECBOOT_NSIH_POSITION - 1) * BLKSIZE], &nsih[0], 512);

	/* 2ndboot */
	fp = fopen(argv[3], "r+b");
	if(fp == NULL)
	{
		printf("Open file 2ndboot error\n");
		free(buffer);
		return -1;
	}

	fseek(fp, 0L, SEEK_END);
	filelen = ftell(fp);
	int bl1_len = filelen;
	fseek(fp, 0L, SEEK_SET);

	nbytes = fread(&buffer[(SECBOOT_POSITION - 1) * BLKSIZE], 1, filelen, fp);
	if(nbytes != filelen)
	{
		printf("Read file 2ndboot error\n");
		free(buffer);
		fclose(fp);
		return -1;
	}
	fclose(fp);

	/* fix 2ndboot nsih */
	bi = (struct boot_info_t *)(&buffer[(SECBOOT_NSIH_POSITION - 1) * BLKSIZE]);
	/* ... */
	printf("2ndboot loadaddr: [0x%08X]\n", bi->loadaddr);
	printf("2ndboot launchaddr: [0x%08X]\n", bi->launchaddr);

	/* bootloader nsih */
	memcpy(&buffer[(BOOTLOADER_NSIH_POSITION - 1) * BLKSIZE], &nsih[0], 512);

	/* bootloader */
	fp = fopen(argv[4], "r+b");
	if(fp == NULL)
	{
		printf("Open file bootloader error\n");
		free(buffer);
		return -1;
	}

	fseek(fp, 0L, SEEK_END);
	filelen = ftell(fp);
	int uboot_len = filelen;
	reallen = (BOOTLOADER_POSITION - 1) * BLKSIZE + filelen;
	fseek(fp, 0L, SEEK_SET);

	nbytes = fread(&buffer[(BOOTLOADER_POSITION - 1) * BLKSIZE], 1, filelen, fp);
	if(nbytes != filelen)
	{
		printf("Read file bootloader error\n");
		free(buffer);
		fclose(fp);
		return -1;
	}
	fclose(fp);

	int is64BitMode = 1;
	if (argc == 6) {
		is64BitMode = atoi(argv[5]);
	}
	printf("is64BitMode: [%d]\n", is64BitMode);

	/* fix bootloader nsih */
	bi = (struct boot_info_t *)(&buffer[(BOOTLOADER_NSIH_POSITION - 1) * BLKSIZE]);
	bi->deviceaddr = 0x00008000;
	bi->loadsize = ((filelen + 512 + 512) >> 9) << 9;
	if (is64BitMode > 0) {
		/* once cpu run in aarch64 mode, you can not jump to reset vector address anymore
		will cause a ARM exception
		the first instruction will no longer be 'MOV PC, ResetV'
		we need to jump to the first address of u-boot */
		bi->loadaddr = 0x43bffe00;
		bi->launchaddr = 0x43C00000;
	} else {
		/* in arrch32 mode, this won't be a issue, jump to start of vector
		and it will jump to startaddr of u-boot by set the value of pc */
		bi->loadaddr = 0x43C00000;
		bi->launchaddr = 0x43C00000;
	}
	
	printf("uboot loadaddr: [0x%08X]\n", bi->loadaddr);
	printf("uboot launchaddr: [0x%08X]\n", bi->launchaddr);

	// write nsih1 + bl1 part
	write_nsih1_with_bl1(buffer, sizeof(buffer), bl1_len, nsih1_with_bl1_fname);

	// write nsih2 + bl2(uboot) part
	write_nsih2_with_uboot(buffer, sizeof(buffer), uboot_len, nsih2_with_uboot_fname);

	// write nsih1 + bl1 + nsih2 + bl2 to file (ALL)
	(void)write_buf_to_file(buffer, reallen, argv[1]);

	printf("Generate destination file: [%s], [%s], [%s]\n", argv[1], nsih1_with_bl1_fname, nsih2_with_uboot_fname);
	return 0;
}

int write_buf_to_file(char *buf, size_t len, const char *fname)
{
	printf("write buffer to file: [%s] begin!\n", fname);
	FILE *fp = fopen(fname, "w+b");
	if(fp == NULL)
	{
		printf("open file: [%s] error\n", fname);
		return -1;
	}
	int nbytes = fwrite(buf, 1, len, fp);
	if(nbytes != len)
	{
		printf("write file: [%s] error\n", fname);
		fclose(fp);
		return -1;
	}
	fclose(fp);
	printf("write buffer to file: [%s] success!\n", fname);
}

// 这个函数写出来的文件适合usb启动
void write_nsih2_with_uboot(const char *raw_buffer, size_t buffer_len, size_t uboot_file_len, const char *out_file_name)
{
	struct boot_info_t * bi;
	int final_file_len = 0;
	char *buffer = malloc(MAX_BUFFER_SIZE);
	memset(buffer, 0, MAX_BUFFER_SIZE);

	printf("uboot_file_len: [%ld]\n", uboot_file_len);
	memcpy(&buffer[(SECBOOT_NSIH_POSITION - 1) * BLKSIZE], &raw_buffer[(BOOTLOADER_NSIH_POSITION - 1) * BLKSIZE], 512);
	memcpy(&buffer[(SECBOOT_POSITION - 1) * BLKSIZE], 		&raw_buffer[(BOOTLOADER_POSITION - 1) * BLKSIZE], uboot_file_len);
	bi = (struct boot_info_t *)(&buffer[(SECBOOT_NSIH_POSITION - 1) * BLKSIZE]);
	bi->loadsize = (uboot_file_len);

	/* bl1 load u-boot with usb boot, do need to care the nsih header for u-boot anymore
	so set the load address and the launch address the same */
	bi->loadaddr = 0x43C00000;
	bi->launchaddr = 0x43C00000;
	final_file_len = (SECBOOT_POSITION - 1) * BLKSIZE + uboot_file_len;

	(void)write_buf_to_file(buffer, final_file_len, out_file_name);
	
	free(buffer);
}

// 带nsih的2ndboot, 可以用来调试usb启动
void write_nsih1_with_bl1(const char *raw_buffer, size_t buffer_len, size_t bl1_file_len, const char *out_file_name)
{
	struct boot_info_t * bi;
	int final_file_len = 0;
	char *buffer = malloc(MAX_BUFFER_SIZE);
	memset(buffer, 0, MAX_BUFFER_SIZE);

	printf("bl1_file_len: [%ld]\n", bl1_file_len);
	memcpy(&buffer[(SECBOOT_NSIH_POSITION - 1) * BLKSIZE], 	&raw_buffer[(SECBOOT_NSIH_POSITION - 1) * BLKSIZE], 512);
	memcpy(&buffer[(SECBOOT_POSITION - 1) * BLKSIZE], 		&raw_buffer[(SECBOOT_POSITION - 1) * BLKSIZE], bl1_file_len);
	final_file_len = (SECBOOT_POSITION - 1) * BLKSIZE + bl1_file_len;

	(void)write_buf_to_file(buffer, final_file_len, out_file_name);
	free(buffer);
}
