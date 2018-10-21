/*-------------------------------------------------------------------------------
 * 								EDACS96 CC finder
 * 
 * 
 * rtl_fm sample rate 28.8kHz (3*9600baud - 3 samples per symbol)
 * 
 * 
 * XTAL Labs
 * 27 IV 2016
 *-----------------------------------------------------------------------------*/
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>

#define UDP_BUFLEN		5				//maximum UDP buffer length
#define SRV_IP 			"127.0.0.1"		//IP
#define UDP_PORT 		6020			//UDP port

#define	SAMP_NUM		5* (48+6*40) *2*3				//number of samples (5*288bit)
#define	SYNC_FRAME		0x555557125555FC<<(64-52)		//EDACS96 synchronization frame (12*4=48bit)
#define	SYNC_MASK		0xFFFFFFFFFFFFFF<<(64-52)		//EDACS96 synchronization frame mask

#define	MAX_LCN_NUM		64		//maximum number of Logical Channels

unsigned char samples[SAMP_NUM];				//8-bit samples from rtl_fm (or rtl_udp)
signed short int raw_stream[SAMP_NUM/2];		//16-bit signed int samples

signed int AFC=0;								//Auto Frequency Control -> DC offset
signed int min=SHRT_MAX, max=SHRT_MIN;			//min and max sample values
signed short int avg_arr[SAMP_NUM/2/3];			//array containing 16-bit samples
unsigned int avg_cnt=0;							//avg array index variable

unsigned long long sr_0=0;						//shift registers for pushing decoded binary data
unsigned long long sr_1=0;

unsigned char current_lcn=1;					//current LCN
unsigned long long int LCN_list[MAX_LCN_NUM];	//LCN list

int handle;						//for UDP
unsigned short port = UDP_PORT;	//
char data[UDP_BUFLEN]={0};		//
struct sockaddr_in address;		//

//--------------------------------------------
int init_udp()		//UDP init
{
	handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (handle <= 0)
	{
			printf("Failed to create socket\n");
			return 1;
	}

	printf("Sockets successfully initialized\n");

	memset((char *) &address, 0, sizeof(address));
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = inet_addr(SRV_IP); //address of host
	address.sin_port = htons(port);

	return 0;
}

//--------------------------------------------
void tune(unsigned long long int freq)		//tuning to freq
{
	data[0]=0;
	data[1]=freq&0xFF;
	data[2]=(freq>>8)&0xFF;
	data[3]=(freq>>16)&0xFF;
	data[4]=(freq>>24)&0xFF;
	
	sendto(handle, data, UDP_BUFLEN, 0, (const struct sockaddr*) &address, sizeof(struct sockaddr_in));
}

void loadLCN(char* filename)		//load LCN frequencies from file
{
	FILE *fl;
	char *line = NULL;
	size_t len = 0;
	
	char* path = NULL;
	asprintf(&path, "/edacs/systems/%s", filename);
	
	fl = fopen(path, "r+");
	
	if (fl == NULL)
		printf("Error opening LCN file: %s", filename);
	
	for(short int i=0; i<MAX_LCN_NUM-1; i++)
	{
		if (getline(&line, &len, fl) != -1)
			LCN_list[i]=atoi(line);
			if(LCN_list[i]!=0)
				printf("LCN[%d]=%lldHz\n", i+1, LCN_list[i]);
    }
}

//--------------------------------------------MAIN--------------------------------------
int main(int argc, char **argv)
{
	signed int avg=0;		//sample average
	
	init_udp();
	loadLCN("psy");		//load LCN freq list file
	sleep(2);			//patience is a virtue
	
	for(int i=0; i<SAMP_NUM/2/3-1; i++)	//zero array
	{
		avg_arr[i]=0;
	}
	
	tune(LCN_list[current_lcn-1]);
	//sleep(1);
	
	while(1)
	{
		read(0, samples, 3*2);	//read 3 samples (6 unsigned chars)
		raw_stream[0]=(signed short int)((samples[0+1]<<8)|(samples[0]&0xFF));
		raw_stream[1]=(signed short int)((samples[2+1]<<8)|(samples[2]&0xFF));
		raw_stream[2]=(signed short int)((samples[4+1]<<8)|(samples[4]&0xFF));
		avg=(raw_stream[0]+raw_stream[1]+raw_stream[2])/3;
		
		//AFC recomputing using averaged samples
		avg_arr[avg_cnt]=avg;
		avg_cnt++;
		if (avg_cnt>=SAMP_NUM/2/3-1)	//reset after filling avg_array
		{
			avg_cnt=0;
			min=SHRT_MAX;
			max=SHRT_MIN;
			
			for(unsigned short int i=1000; i<SAMP_NUM/2/3-1; i++)	//simple min/max detector
			{
				if (avg_arr[i]>max)
					max=avg_arr[i];
				if (avg_arr[i]<min)
					min=avg_arr[i];
			}
			
			AFC=(min+max)/2;
			
			for (unsigned short int i=0; i<SAMP_NUM/2/3-1; i++)	//convert signed ints to bitstream
			{
				sr_0=(sr_0<<1)|(sr_1>>63);
				sr_1=sr_1<<1;

				if (avg_arr[i]<AFC)
				sr_1|=1;
		
				if ((sr_0&SYNC_MASK)==SYNC_FRAME)
				{
					printf("CC_LCN=%d\n%016llX\n", current_lcn, sr_0);
					return 0;
				}
			}
			
			current_lcn++;
			if (current_lcn>=MAX_LCN_NUM || LCN_list[current_lcn-1]==0)
				current_lcn=1;
			tune(LCN_list[current_lcn-1]);
			sleep(0.7);
		}
	}

	return 0;
}
