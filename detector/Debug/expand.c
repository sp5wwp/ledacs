#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{
	unsigned char sample_in[2];
	unsigned char sample_out[2];
	signed short int stream;

	while(1)
	{
		if(read(0, sample_in, 2)>0)	//read 2samples
		{
			stream=(signed short int)((sample_in[1]<<8)|(sample_in[0]&0xFF));
			
			if(abs(stream)<1000) stream*=3;

			sample_out[0]=(unsigned char)stream&0xFF;
			sample_out[1]=(unsigned char)(stream>>8);
			
			write(1, sample_out, 2);
		}
	}

	return 0;
}
