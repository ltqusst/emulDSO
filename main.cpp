#include <stdio.h>
#include <math.h>
#include <windows.h>

#include "emulDSO.h"
#include "gs_file.hpp"

int main()
{
	printf("Hello, emulDSO!\r\n");
	emulDSO_create("test", 800, 600);

    gs_sample s("flick_down.txt", GS_FORMAT_SSV_6T, 100, 100);// , 0, end_sec);
    while (s.scanf()) //skip 3 of every 3+1 sample
    {
        emulDSO_record("signal.x", "c0w2", s.x);
        emulDSO_record("signal.y", "c1p", s.y);
        emulDSO_record("signal.z", "c2", s.z);

        float mag = sqrt((float)(s.x*s.x + s.y*s.y + s.z*s.z));
        emulDSO_record("mag", "c8p", mag/10000);

        emulDSO_ticktock(1.0f / 25);
    }

	Sleep(1000*2);
    printf("wait for DSO to exit\n");

	emulDSO_close(1);
	//Sleep(1000*2);
	return 0;
}
