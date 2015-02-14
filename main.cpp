#include <stdio.h>
#include <windows.h>

#include "emulDSO.h"


int main()
{
	printf("Hello, emulDSO!\r\n");
	emulDSO_create("test", 800, 600);

	Sleep(1000*20);
	emulDSO_close();
	//Sleep(1000*2);
	return 0;
}
