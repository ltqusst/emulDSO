#ifndef _EMULDSO_H_
#define _EMULDSO_H_


#ifdef __cplusplus
extern "C" {
#endif

#ifdef USE_EMUL_DSO
void emulDSO_create(const char * title, int width, int height);
void emulDSO_record(const char * data_name, const char * style, float value);
void emulDSO_ticktock(const char * dso_name, float step_sec);
float emulDSO_curtick(const char * dso_name);
void emulDSO_close(int waitForUser);
void emulDSO_update(void);

#else
#define emulDSO_create(title, width, height)
#define emulDSO_record(data_name, style, value)
#define emulDSO_ticktock(dso_name,step_sec)
#define emulDSO_curtick(dso_name)
#define emulDSO_close(waitForUser)
#define emulDSO_update()
#endif

#ifdef __cplusplus
}
#endif

#endif