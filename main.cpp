#include <stdio.h>
#include <math.h>
#include <windows.h>

#define USE_EMUL_DSO
#include "emulDSO.h"
#include "gs_file.hpp"

int main()
{
    int i = 0;
	printf("Hello, emulDSO!\r\n");

	//basic configuration
	emulDSO_create("test", 600, 200);

    gs_sample s("flick_down.txt", GS_FORMAT_SSV_6T, 25, 100);// , 0, end_sec);
    while (s.scanf()) //skip 3 of every 3+1 sample
    {
		float mag = sqrt((float)(s.x*s.x + s.y*s.y + s.z*s.z));

		//signals in default group
        emulDSO_record("signal.x", "c0w2", s.x);
        emulDSO_record("signal.y", "c1p", s.y);
        emulDSO_record("signal.z", "c2p", s.z);
        emulDSO_record("mag", "c8p", mag/10000);
		emulDSO_record("atate.magabove200", "d", (mag>1100?1:2));
        emulDSO_record("atate.magabove100", "d", (mag>800 ? 1 : 2));
        emulDSO_record("atate.test", "d", (i++)/5);
		emulDSO_record("zz", "c3", s.z);
		emulDSO_record("xx", "c4", s.z);

        
		//signals in t0 group
        emulDSO_record("mag@t0", "c8p", mag/10000);
		emulDSO_record("atate.magabove200@t0", "d", (mag>1100?1:2));
        emulDSO_record("atate.magabove100@t0", "d", (mag>800 ? 1 : 2));
        emulDSO_record("atate.test@t0", "d", (i++)/5);

		//signals in t1 group
		emulDSO_record("zz@t1", "c3", s.z);
		emulDSO_record("xx@t1", "c4", s.z);

		emulDSO_ticktock(NULL,1.0f / 25);
        emulDSO_ticktock("t0",1.0f / 25);
		emulDSO_ticktock("t1",1.0f / 25);
    }
	//emulDSO_update();
	//Sleep(100);

	//freqz of an IIR filter
	float fb[]={0.25,0.25,0.25,0.25};
	float fa[]={1};
	emulDSO_freqz("filter1",
				   fb, sizeof(fb)/sizeof(fb[0]), 
			       fa, sizeof(fa)/sizeof(fa[0]), 5);
	
    printf("wait for DSO to exit\n");

	emulDSO_close(1);
	//Sleep(1000*2);
	return 0;
}
