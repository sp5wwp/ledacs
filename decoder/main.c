/*-------------------------------------------------------------------------------
 * 								EDACS96 rtl_fm decoder
 * 
 * 
 * rtl_fm sample rate 28.8kHz (3*9600baud - 3 samples per symbol)
 * 
 * 
 * XTAL Labs
 * 3 V 2016
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
#include <time.h>

#define UDP_BUFLEN		5				//maximum UDP buffer length
#define SRV_IP 			"127.0.0.1"		//IP
#define UDP_PORT 		6020			//UDP port

#define	SAMP_NUM		(48+6*40) *2*3				//EDACS96 288-bit cycle
#define	SYNC_FRAME		0x555557125555<<(64-48)		//EDACS96 synchronization frame (12*4=48bit)
#define	SYNC_MASK		0xFFFFFFFFFFFF<<(64-48)		//EDACS96 synchronization frame mask

#define CMD_SHIFT		8					//data location in sr_0
#define CMD_MASK		0xFF<<CMD_SHIFT		//DON'T TOUCH
#define LCN_SHIFT		3					//for other SR-s these values are hardcoded below
#define LCN_MASK		0xF8
#define STATUS_MASK		0x07

#define	SYNC_TIMEOUT	3		//maximum time between sync frames in seconds
#define VOICE_TIMEOUT	1		//maximum time of unsquelched audio after last voice channel assignment command in seconds

#define	IDLE_CMD		0xFC	//CC commands
#define	VOICE_CMD		0xEE	//
#define	MAX_LCN_NUM		32		//maximum number of Logical Channels

unsigned char samples[SAMP_NUM];				//8-bit samples from rtl_fm (or rtl_udp)
signed short int raw_stream[SAMP_NUM/2];		//16-bit signed int samples

signed int AFC=0;								//Auto Frequency Control -> DC offset
signed int min=SHRT_MAX, max=SHRT_MIN;			//min and max sample values
signed short int avg_arr[SAMP_NUM/2/3];			//array containing 16-bit samples
unsigned int avg_cnt=0;							//avg array index variable

unsigned long long sr_0=0;						//64-bit shift registers for pushing decoded binary data
unsigned long long sr_1=0;						//288/64=4.5
unsigned long long sr_2=0;						//
unsigned long long sr_3=0;						//
unsigned long long sr_4=0;						//

unsigned short a_len=4;							//AFS allocation type (see readme.txt)
unsigned short f_len=4;							//bit lengths
unsigned short s_len=3;							//
unsigned short a_mask=0x780;					//and corresponding masks
unsigned short f_mask=0x078;					//
unsigned short s_mask=0x007;					//
unsigned long long afs=0;						//AFS 11-bit info

unsigned char command=0;						//read from control channel
unsigned char lcn=0;							//
unsigned char status=0;							//
unsigned char agency=0, fleet=0, subfleet=0;	//
unsigned char CRC=0;							//

unsigned long long int last_sync_time=0;		//last received sync timestamp
unsigned long long int last_voice_time=0;		//last received voice channel assignment timestamp

unsigned char current_lcn=0;					//current LCN
unsigned long long int LCN_list[MAX_LCN_NUM];	//LCN list
unsigned char lcn_num=0;						//number of logical channels
unsigned char cc=1;								//control channel LCN

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

char* getTime(void)		//get pretty hh:mm:ss timestamp
{
	time_t t = time(NULL);

	char* curr;
	char* stamp = asctime(localtime(&t));

	curr = strtok(stamp, " ");
	curr = strtok(NULL, " ");
	curr = strtok(NULL, " ");
	curr = strtok(NULL, " ");

	return curr;
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

void squelchSet(unsigned long long int sq)		//squelch
{
	data[0]=2;
	data[1]=sq&0xFF;
	data[2]=(sq>>8)&0xFF;
	data[3]=(sq>>16)&0xFF;
	data[4]=(sq>>24)&0xFF;
	
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
	
	lcn_num=0;
	
	for(short int i=0; i<MAX_LCN_NUM-1; i++)
	{
		if (getline(&line, &len, fl) != -1)
			LCN_list[i]=atoi(line);
			if(LCN_list[i]!=0)
			{
				printf("LCN[%d]=%lldHz\n", i+1, LCN_list[i]);
				lcn_num++;
			}
    }
}

//--------------------------------------------MAIN--------------------------------------
int main(int argc, char **argv)
{
	signed int avg=0;		//sample average
	
	FILE *fp; int fread;
	
	init_udp();
	sleep(1);			//patience is a virtue	
	
	//load arguments-----------------------------------
	if(argc>4)	
	{
		loadLCN(argv[1]);	//load LCN freq list file
		if(lcn_num>MAX_LCN_NUM)
		{
			printf("****************************ERROR*****************************\n");
			printf("Too many LCNs!\n");
			printf("**************************************************************\n");
			
			return 1;
		}
		
		cc=strtol(argv[2], NULL, 10);
		printf("CC=LCN[%d]\n", cc);
		
		//load AFS allocation info
		a_len=strtol(argv[3], NULL, 10);
		f_len=strtol(argv[4], NULL, 10);
		s_len=11-(a_len+f_len);
		
		a_mask=0; f_mask=0; s_mask=0;
		
		for(unsigned short int i=0; i<a_len; i++)	//A
		{
			a_mask=a_mask<<1;
			a_mask|=1;
		}
		a_mask=a_mask<<(11-a_len);
		
		for(unsigned short int i=0; i<f_len; i++)	//F
		{
			f_mask=f_mask<<1;
			f_mask|=1;
		}
		f_mask=f_mask<<s_len;
		
		for(unsigned short int i=0; i<s_len; i++)	//S
		{
			s_mask=s_mask<<1;
			s_mask|=1;
		}
		
	} else {
		printf("****************************ERROR*****************************\n");
		printf("Not enough parameters!\n");
		printf("Usage: ./decoder input a f [A F S]\n\n");
		printf("input - file with LCN frequency list\n");
		printf("        must be located in /edacs/systems\n");
		printf("cc    - control channel number\n");
		printf("a f   - AFS agency and fleet bit lengths, for example 4 4\n");
		printf("        4-bits agency, 4-bits fleet and 3-bits subfleet number\n");
		printf("A F S - optional filter, for example 8 12 4 means 08-124\n\n");
		printf("Exiting.\n");
		printf("**************************************************************\n");
		
		return 1;
	}
	//-------------------------------------------------
	
	last_sync_time = time(NULL);
	last_voice_time = time(NULL);
	unsigned char voice_to=0;		//0 - no timeout, 1 - last voice channel assignment more than VOICE_TIMEOUT seconds ago
	
	for(int i=0; i<SAMP_NUM/2/3-1; i++)	//zero array
	{
		avg_arr[i]=0;
	}
	
	//let's get the party started
	while(1)
	{
		if ((time(NULL)-last_sync_time)>SYNC_TIMEOUT)	//check if the CC is still there
		{
			printf("CC lost. Timeout %llds. Exiting.\n", time(NULL)-last_sync_time);
			
			fp = fopen("/tmp/squelch", "w");
			fputc('1', fp);
			squelchSet(5000);	//mute
			fclose(fp);
			
			return 0;
		}
		
		//printf("to=%lld, variable=%d\n", time(NULL)-last_voice_time, voice_to);
		if ((time(NULL)-last_voice_time)>VOICE_TIMEOUT)	//mute if theres no more voice channel assignments
		{
			//fp = fopen("/tmp/squelch", "r+");
			//fread=fgetc(fp);
			
			if (voice_to==0)
			{
				squelchSet(5000);	//mute
				//usleep(500*1000);
				fp = fopen("/tmp/squelch", "w");
				fputc('1', fp);
				
				fclose(fp);
				voice_to=1;
			}
		} else
			voice_to=0;
		
		read(0, samples, 3*2);	//read 3 samples (6 unsigned chars)
		raw_stream[0]=(signed short int)((samples[0+1]<<8)|(samples[0]&0xFF));
		raw_stream[1]=(signed short int)((samples[2+1]<<8)|(samples[2]&0xFF));
		raw_stream[2]=(signed short int)((samples[4+1]<<8)|(samples[4]&0xFF));
		avg=(raw_stream[0]+raw_stream[1]+raw_stream[2])/3;	//
		
		//AFC recomputing using averaged samples
		avg_arr[avg_cnt]=avg;
		avg_cnt++;
		if (avg_cnt>=SAMP_NUM/2/3-1)	//reset after filling avg_array
		{
			avg_cnt=0;
			min=SHRT_MAX;
			max=SHRT_MIN;
			
			for(int i=0; i<SAMP_NUM/2/3-1; i++)	//simple min/max detector
			{
				if (avg_arr[i]>max)
					max=avg_arr[i];
				if (avg_arr[i]<min)
					min=avg_arr[i];
			}
			AFC=(min+max)/2;
		}
		//--------------------------------------
		
		//pushing data into shift registers
		sr_0=(sr_0<<1)|(sr_1>>63);
		sr_1=(sr_1<<1)|(sr_2>>63);
		sr_2=(sr_2<<1)|(sr_3>>63);
		sr_3=(sr_3<<1)|(sr_4>>63);
		sr_4=sr_4<<1;

		if (avg<AFC)
			sr_4|=1;
		//---------------------------------
		
		if ((sr_0&SYNC_MASK)==SYNC_FRAME)	//extract data after receiving the sync frame
		{
			//primitive error detection
			unsigned short int data, _data;
			
			//extract CMD
			data=((sr_0&CMD_MASK)>>CMD_SHIFT)&0xFF;
			_data=(~((sr_1&(0xFF00000000))>>32))&0xFF;
			if ( data == _data )	
				command=data;
			
			//extract LCN
			data=((sr_0&LCN_MASK)>>LCN_SHIFT)&0x1F;
			_data=(~((sr_1&(0xF8000000))>>27))&0x1F;
			if ( data == _data )
				lcn=data;
			
			//extract STATUS
			data=(((sr_0&STATUS_MASK)<<1)|(sr_1>>63))&0x0F;
			_data=(~((sr_1&(0x3800000))>>23))&0x0F;
			if ( data == _data )
				status=data;
			
			//extract AFS
			data=((sr_1&(0x7FF0000000000000))>>52)&0x7FF;
			_data=(~((sr_1&(0x7FF000))>>12))&0x7FF;
			if ( data == _data )
				afs=data;
				
			//test
			unsigned short int sender;
			data=( ((sr_2&0x3FF)<<4) | ((sr_3&0xF000000000000000)>>60) )&0x3FFF;
			//_data=(~((sr_1&(0x7FF000))>>12))&0x7FF;
			//if ( data == _data )
				sender=data;
			
			agency=(afs&a_mask)>>(11-a_len);
			fleet=(afs&f_mask)>>s_len;
			subfleet=(afs&s_mask);
			//-------------------------------------------------
			
			last_sync_time = time(NULL);	//set timestamp
			
			if (command==IDLE_CMD)		//IDLE
			{
				//printf("%s:  AFC=%d\tIDLE\t%d\t%d%d%d%d\t%2d-%02d%d\n", getTime(), AFC, lcn, (status>>3)%2, (status>>2)%2,
				//(status>>1)%2, status%2, agency, fleet, subfleet);
			}
			else if (command==VOICE_CMD && lcn>0 && lcn<lcn_num && lcn!=cc /* && ((status>>2)%2)==1*/)		//voice channel assignment
			{
				if (lcn==current_lcn) //lcn==current_lcn
				{
					last_voice_time = time(NULL); 
					voice_to=0;
				}
				
				if((status%2)==1) printf("%s:  AFC=%d\tVOICE\t%d\t%d%d%d%d\t%02d-%02d%d <- %d\n", getTime(), AFC, lcn, (status>>3)%2, (status>>2)%2,
				(status>>1)%2, status%2, agency, fleet, subfleet, sender);
				else printf("%s:  AFC=%d\tVOICE\t%d\tPVT\t%lld <- %d\n", getTime(), AFC, lcn, ((status&0x07)<<11)|afs, sender);

				if(argc<=5 || (argc>7 && agency==strtol(argv[5], NULL, 10) &&  fleet==strtol(argv[6], NULL, 10) && subfleet==strtol(argv[7], NULL, 10)))
				{
					fp = fopen("/tmp/squelch", "r+");
					fread=fgetc(fp);
					
					if(fread=='1')	//if we are currently NOT listening to anything else
					{
						if (lcn!=current_lcn)		//tune to LCN
						{
							tune(LCN_list[lcn-1]);
							current_lcn=lcn;
						}
					
						fp = freopen("/tmp/squelch", "w", fp);
						fputc('0', fp);
						squelchSet(0);	//unmute
						printf("LISTEN: %02d-%02d%d\n", agency, fleet, subfleet);
					}
			
					fclose(fp);
				}
			}
			else
			{
				//printf("%s:  AFC=%d\t%02X\t%d\t%d%d%d%d\t%02d-%02d%d\n", getTime(), AFC, command, lcn, (status>>3)%2, (status>>2)%2,
				//(status>>1)%2, status%2, agency, fleet, subfleet);
				//printf("1=%lld, 2=%d\n", afs, sender&0x7FF);
			}
		}
	}

	return 0;
}
