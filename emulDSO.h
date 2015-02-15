#ifndef _EMULDSO_H_
#define _EMULDSO_H_


#ifdef __cplusplus
extern "C" {
#endif


void emulDSO_create(const char * title, int width, int height);
void emulDSO_record(const char * data_name, const char * style, float value);
void emulDSO_ticktock(float step_sec);
void emulDSO_close(int waitForUser);



#ifdef __cplusplus
}
#endif

#endif