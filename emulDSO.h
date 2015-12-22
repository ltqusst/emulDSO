#ifndef _EMULDSO_H_
#define _EMULDSO_H_


#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_EMUL_DSO
#include <tchar.h>
//all plots in an window are of same domain, so when user do time-scaling and shifting, 
//all plots will be scaled and shifted together.

void emulDSO_create(const TCHAR * title, int width, int height);
//data_name format:
//  group_name.data_name@domain_name	domain_name is dso_name, corresponding window name
//or
//  data_name@domain_name  in this format, group_name is null string "" for default

//feature1
//using internal time counter as x coordinate, data input in serial
void emulDSO_record(const TCHAR * data_name, const TCHAR * style, float value);
void emulDSO_ticktock(const TCHAR * dso_name, float step_sec);
float emulDSO_curtick(const TCHAR * dso_name);
void emulDSO_settick(const TCHAR * dso_name, float time);

//feature2
//user supply x coordinate, data input in serial
void emulDSO_record2(const TCHAR * data_name, const TCHAR * style, float x, float value);

//feature3
//user specify the previous ticks id to add passed data
void emulDSO_record3(const TCHAR * data_name, const TCHAR * style, int tick_offset, float value);

//feature2
//a freqz method similar to matlab version, internally based on FFT-based DTFT and feature1
void emulDSO_freqz(const TCHAR * dso_name, float * b, int bn, float * a, int an, int exponentN1, int use_dB);


void emulDSO_close(int waitForUser);
void emulDSO_update(const TCHAR *dso_name);

void emulDSO_get_text(const TCHAR * data_name, const TCHAR * txt, 
					  int size, 
					  char * pdata, int w, int h);
void emulDSO_generate_font(const char * out_file, const char * strFont, int w, int h);

#else
#define emulDSO_create(title, width, height)
#define emulDSO_ticktock(dso_name,step_sec)
#define emulDSO_curtick(dso_name)
#define emulDSO_close(waitForUser)
#define emulDSO_update()
#define emulDSO_record(data_name, style, value)
#define emulDSO_record2( data_name, style, x, value)
#define emulDSO_record3( data_name, style, tick_offset, value)

#endif

#ifdef __cplusplus
}
#endif

#endif